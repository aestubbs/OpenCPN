/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * Implement chart_canvas.h.
 *
 * Owns the Viewport and the LayerCompositor. The compositor is fed a single
 * ChartLayer wrapping a RasterChartProvider (test chart for the prototype).
 * Mouse drag pans the Viewport; mouse wheel zooms about the cursor.
 *
 * Each frame, the WorldAnchored root's matrix is set from
 * Viewport::transformMatrix(width, height) so all world-anchored Layers
 * (chart, AIS, routes, ...) get the chart pan/zoom for free.
 */

#include "chart_canvas.h"

#include <algorithm>
#include <cmath>

#include <QDirIterator>
#include <QFileInfo>
#include <QVarLengthArray>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGTransformNode>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>

#include "chart_boundary_provider.h"
#include "chart_layer.h"
#include "chart_worker.h"
#include "gshhs_world_provider.h"
#include "layer_compositor.h"
#include "raster_chart_provider.h"
#include "s52_engine.h"
#include "s52_vector_chart_provider.h"
#include "test_chart.h"
#include "viewport.h"

#ifndef OCPN_QT_GSHHS_DIR
#define OCPN_QT_GSHHS_DIR ""
#endif

namespace ocpn::qtui {

namespace {
// Bounding box for the test chart -- a chunk of the North Sea.
constexpr double kTestNorth = 55.0;
constexpr double kTestSouth = 50.0;
constexpr double kTestWest = 0.0;
constexpr double kTestEast = 10.0;
}  // namespace

ChartCanvas::ChartCanvas(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::LeftButton);

  m_viewport = std::make_unique<Viewport>();
  // Centre on the test chart, with the scale ChartCanvas's QML host fits
  // to. (User can zoom from here.)
  m_viewport->setCenter((kTestNorth + kTestSouth) / 2.0,
                        (kTestWest + kTestEast) / 2.0);

  m_compositor = std::make_unique<LayerCompositor>();

  // World background -- the bundled GSHHS crude coastline, always present
  // under everything (lowest z) so the canvas shows a land/sea world map at
  // any zoom. ENC cells and overlays composite on top.
  auto* world = new GshhsWorldProvider(
      QString::fromUtf8(OCPN_QT_GSHHS_DIR) + "/poly-c-1.dat");
  auto* world_layer = new ChartLayer(world, m_viewport.get());
  world_layer->setZOrder(-1000);
  m_compositor->addLayer(world_layer);

  // Repaint when:
  //   - any Layer dirties (data change, visibility/z-order/opacity).
  //   - the Viewport pans / zooms (transform matrix changes).
  //   - the canvas resizes (transform depends on width/height).
  // Custom value types crossing the worker-thread -> main-thread queued
  // signal boundary must be registered.
  qRegisterMetaType<ocpn::qtui::CellExtent>();
  qRegisterMetaType<QList<ocpn::qtui::CellExtent>>();
  qRegisterMetaType<s52sg::Buffer>();

  // Pan/zoom fires Viewport::changed at mouse-move / wheel rate; coalesce a
  // burst into one visible-cell evaluation once the gesture settles.
  m_load_debounce = new QTimer(this);
  m_load_debounce->setSingleShot(true);
  m_load_debounce->setInterval(250);
  connect(m_load_debounce, &QTimer::timeout, this,
          [this]() { updateVisibleCells(); });

  connect(m_compositor.get(), &LayerCompositor::changed, this,
          [this]() { update(); });
  connect(m_viewport.get(), &Viewport::changed, this, [this]() {
    update();
    // Only chase visible cells once a catalog exists (async ENC path).
    if (!m_catalog.isEmpty()) m_load_debounce->start();
  });
  // On resize, repaint AND re-evaluate visible cells: the initial fit +
  // selection can run before the canvas has its real size (the catalog scan
  // starts at launch), sampling a too-small view rect and missing cells in
  // the part of the real window beyond it. Re-running on the real size fills
  // them in without waiting for a user pan/zoom.
  auto onResize = [this]() {
    update();
    if (!m_catalog.isEmpty()) m_load_debounce->start();
  };
  connect(this, &QQuickItem::widthChanged, this, onResize);
  connect(this, &QQuickItem::heightChanged, this, onResize);
}

ChartCanvas::~ChartCanvas() {
  // Stop the worker thread before the QObject teardown chain runs.
  if (m_worker_thread) {
    m_worker_thread->quit();
    m_worker_thread->wait();
  }
}

#ifndef OCPN_QT_TEST_ENC
#define OCPN_QT_TEST_ENC ""
#endif
#ifndef OCPN_QT_S57DATA_DIR
#define OCPN_QT_S57DATA_DIR ""
#endif

void ChartCanvas::setS52Engine(S52Engine* engine) {
  if (m_s52_engine == engine) return;
  m_s52_engine = engine;
  Q_EMIT s52EngineChanged();
  if (!m_s52_engine || !m_s52_engine->isOk()) return;

  // Real ENC cells if configured at build time (OCPN_QT_TEST_ENC -- a single
  // .000 file or a DIRECTORY of them); otherwise the synthetic demo chart.
  const QString enc_path = QString::fromUtf8(OCPN_QT_TEST_ENC);
  m_s57data_dir = QString::fromUtf8(OCPN_QT_S57DATA_DIR);

  if (!enc_path.isEmpty()) {
    // Async path: enumerate the cell set and hand it to the worker thread.
    // The catalog scan comes back first (boundaries + world navigation);
    // per-cell content streams in on demand as the user zooms/pans.
    QStringList cells;
    QFileInfo fi(enc_path);
    if (fi.isDir()) {
      QDirIterator it(enc_path, {"*.000"}, QDir::Files,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) cells << it.next();
      cells.sort();
    } else {
      cells << enc_path;
    }
    if (!cells.isEmpty()) startAsyncLoad(cells, m_s57data_dir);
    return;
  }

  // Demo fallback: synthetic chart, decoded inline (tiny, no thread).
  s52sg::Buffer buf = m_s52_engine->buildDemoChart(kTestNorth, kTestSouth,
                                                   kTestEast, kTestWest);
  if (!buf.empty()) {
    auto* provider = new S52VectorChartProvider(
        "demo.s52-chart", std::move(buf), kTestNorth, kTestSouth, kTestWest,
        kTestEast, m_viewport.get());
    applyDisplaySettings(provider);
    m_compositor->addLayer(new ChartLayer(provider, m_viewport.get()));
    LoadedCell lc;
    lc.layerId = "demo.s52-chart";
    lc.provider = provider;  // no extent -> never evicted (not in catalog)
    m_loaded.insert("demo", lc);
    update();
  }
}

void ChartCanvas::startAsyncLoad(const QStringList& cell_paths,
                                 const QString& s57data) {
  m_worker_thread = new QThread(this);
  m_worker = new ChartWorker(m_s52_engine, s57data);  // no parent (moved)
  m_worker->moveToThread(m_worker_thread);
  connect(m_worker_thread, &QThread::finished, m_worker,
          &QObject::deleteLater);

  // Results arrive on the main thread (queued; receiver lives here).
  connect(m_worker, &ChartWorker::extentsScanned, this,
          &ChartCanvas::onExtentsScanned);
  connect(m_worker, &ChartWorker::cellLoaded, this,
          &ChartCanvas::onCellLoaded);

  m_worker_thread->start();

  // Kick off the catalog scan on the worker.
  QMetaObject::invokeMethod(m_worker, "scanExtents", Qt::QueuedConnection,
                            Q_ARG(QStringList, cell_paths));
}

void ChartCanvas::onExtentsScanned(const QList<CellExtent>& cells) {
  m_catalog.clear();
  for (const CellExtent& c : cells) m_catalog.insert(c.name, c);

  // Boundary overlay: created once, drawn on top so the cell grid stays
  // visible over loaded chart content.
  if (!m_boundary_provider) {
    m_boundary_provider = new ChartBoundaryProvider("enc.boundaries");
    auto* layer = new ChartLayer(m_boundary_provider, m_viewport.get());
    layer->setZOrder(100);
    m_compositor->addLayer(layer);
  }
  m_boundary_provider->setExtents(cells);

  // Fit the viewport to the set once (on the first batch). Fit to the dense
  // cluster of content cells, excluding area outliers: a few overview/ocean
  // cells (e.g. US1PO02M Pacific, US1EEZ1M) cover enormous areas and would
  // blow the fit out to a near-global view where every cell is too fine to
  // show. We take the median content-cell area and ignore cells more than 8x
  // larger. The GSHHS backdrop is always present, so the user can zoom out
  // freely from here.
  QList<double> areas;
  for (const CellExtent& c : cells)
    if (c.navFeatures > 0 && c.valid())
      areas.append((c.north - c.south) * (c.east - c.west));
  std::sort(areas.begin(), areas.end());
  const double medianArea = areas.isEmpty() ? 0.0 : areas[areas.size() / 2];
  const double areaCap = medianArea * 8.0;
  double n = -90.0, s = 90.0, e = -180.0, w = 180.0;
  for (const CellExtent& c : cells) {
    if (c.navFeatures <= 0 || !c.valid()) continue;
    if (areaCap > 0.0 && (c.north - c.south) * (c.east - c.west) > areaCap)
      continue;  // area outlier (overview/ocean cell)
    if (c.north > n) n = c.north;
    if (c.south < s) s = c.south;
    if (c.east > e) e = c.east;
    if (c.west < w) w = c.west;
  }
  // Defer the one-time fit until the canvas has its real size, so the
  // initial scale (and the view rect the selection samples) is correct.
  if (!m_world_fitted && width() > 0 && height() > 0 && e > w && n > s) {
    m_world_fitted = true;
    m_viewport->setCenter((n + s) / 2.0, (e + w) / 2.0);
    const double fit =
        std::min(width() / (e - w), height() / (n - s)) * 0.9;
    m_viewport->setScale(fit);
  }
  update();

  // Pull in whatever's already big enough on screen.
  updateVisibleCells();
}

void ChartCanvas::onCellLoaded(const QString& id, const s52sg::Buffer& buffer,
                               double north, double south, double east,
                               double west) {
  // A quick pan/zoom while this cell was decoding may have moved it out of
  // view; drop the result rather than add an off-screen layer.
  CellExtent c;
  c.north = north; c.south = south; c.east = east; c.west = west;
  c.name = id;
  // Use the catalog entry (it carries the native scale + accurate extent);
  // fall back to the loaded bounds if somehow absent.
  CellExtent cat = m_catalog.value(id, c);
  // Dropped if the per-location selection moved off it while it was decoding.
  if (buffer.empty() || !m_needed.contains(id)) {
    m_requested.remove(id);  // allow a future re-request when needed again
    return;
  }

  const QString layerId = "enc." + id;
  auto* provider = new S52VectorChartProvider(layerId, buffer, north, south,
                                              west, east, m_viewport.get());
  applyDisplaySettings(provider);
  auto* layer = new ChartLayer(provider, m_viewport.get());
  layer->setZOrder(zOrderForScale(cat.nativeScale));
  m_compositor->addLayer(layer);
  LoadedCell lc;
  lc.extent = cat;
  lc.layerId = layerId;
  lc.provider = provider;
  m_loaded.insert(id, lc);
  update();
}

double ChartCanvas::displayScaleN(double scale) {
  // ~1:N display-scale denominator at a nominal 96 dpi (3.78 px/mm):
  //   N = (ground metres per degree) / (screen metres per pixel)
  //     = (111320 / scale) / (1 / (ppmm*1000))
  if (scale <= 0.0) return 1.0e12;
  constexpr double kPpmm = 3.78;
  return 111320.0 * kPpmm * 1000.0 / scale;  // ~ 4.21e8 / scale
}

int ChartCanvas::zOrderForScale(int native_scale) {
  // Finer (smaller 1:N) draws on top of coarser, so where they overlap the
  // coarse is hidden. Keep within [0, 90] -- under the boundary grid (100),
  // above the GSHHS world backdrop (-1000).
  if (native_scale <= 0) return 0;
  const double z = 90.0 - 10.0 * (std::log(static_cast<double>(native_scale)) -
                                  std::log(1000.0));
  return std::max(0, std::min(90, static_cast<int>(std::lround(z))));
}

void ChartCanvas::updateVisibleCells() {
  if (!m_worker || m_catalog.isEmpty()) return;

  const double scale = m_viewport->scale();
  const double cw = width() > 0 ? width() : 1024.0;
  const double ch = height() > 0 ? height() : 720.0;
  // Widen the working view by 30% so cells just off-screen stay resident
  // (pan hysteresis) and decode ahead of being scrolled into view.
  const double half_lon = (cw / 2.0) / scale * 1.3;
  const double half_lat = (ch / 2.0) / scale * 1.3;
  const double lat0 = m_viewport->centerLat() - half_lat;
  const double lat1 = m_viewport->centerLat() + half_lat;
  const double lon0 = m_viewport->centerLon() - half_lon;
  const double lon1 = m_viewport->centerLon() + half_lon;

  // Candidate cells: every catalogued cell overlapping the working view that
  // has a known scale and actual chart content (administrative coverage-only
  // cells -- no depth areas/soundings/land -- are excluded so they never win
  // a location and render nothing useful).
  QList<const CellExtent*> cands;
  for (auto it = m_catalog.cbegin(); it != m_catalog.cend(); ++it) {
    const CellExtent& c = it.value();
    if (c.nativeScale > 0 && c.navFeatures > 0 &&
        c.intersects(lat0, lat1, lon0, lon1))
      cands.append(&c);
  }

  // Per-location quilt: sample the view on a grid; at each sample point pick
  // the candidate cell COVERING that point that best suits the zoom, then
  // union the per-point winners into the needed set. This guarantees a
  // location only falls through to the GSHHS world backdrop when NO ENC cell
  // covers it.
  //
  // Pick the chart for each sampled location (mirrors ECDIS quilting: show
  // the most detailed *useful* chart, fall back to coarser where finer is
  // absent). A chart is "eligible" if it's no more than kMaxUnderzoom times
  // finer than the display -- so we don't pull a harbour cell in at coastal
  // zoom -- but coarser charts are always eligible.
  //
  // Among eligible cells covering the point we take the FINEST, except: where
  // several overlap at *comparable* scale (within kComparable x of the finest)
  // we prefer the one with the higher chart-content DENSITY (features per
  // square degree -- not raw count, which would favour a coarser cell merely
  // for spanning more area). That stops a finer-but-sparse cell (e.g. a deep
  // channel cell with few soundings) being chosen over a comparably-scaled
  // neighbour that's rich with soundings, while still favouring detail. If a
  // point's only cover is finer than the threshold (zoomed right out past
  // every chart there), the coarsest is used so it still shows something.
  constexpr double kMaxUnderzoom = 8.0;
  constexpr double kComparable = 4.0;  // scale ratio treated as "same detail"
  const double threshold = displayScaleN(scale) / kMaxUnderzoom;
  const auto density = [](const CellExtent* c) -> double {
    const double a = (c->north - c->south) * (c->east - c->west);
    return a > 0.0 ? c->navFeatures / a : 0.0;
  };
  constexpr int kGrid = 24;
  m_needed.clear();
  for (int gy = 0; gy < kGrid; ++gy) {
    const double plat = lat0 + (gy + 0.5) / kGrid * (lat1 - lat0);
    for (int gx = 0; gx < kGrid; ++gx) {
      const double plon = lon0 + (gx + 0.5) / kGrid * (lon1 - lon0);
      // Gather cells whose actual coverage contains the point.
      QVarLengthArray<const CellExtent*, 16> cov;
      int finestEligible = 0;        // smallest 1:N among eligible
      const CellExtent* coarsest = nullptr;
      for (const CellExtent* c : cands) {
        if (!c->covers(plat, plon)) continue;
        cov.append(c);
        if (!coarsest || c->nativeScale > coarsest->nativeScale) coarsest = c;
        if (c->nativeScale >= threshold &&
            (finestEligible == 0 || c->nativeScale < finestEligible))
          finestEligible = c->nativeScale;
      }
      if (cov.isEmpty()) continue;  // no chart here -> GSHHS backdrop

      const CellExtent* pick = nullptr;
      if (finestEligible > 0) {
        // Among eligible cells within kComparable x of the finest, the
        // densest (richest per area); tie-break finer.
        const double band = finestEligible * kComparable;
        double bestDensity = -1.0;
        for (const CellExtent* c : cov) {
          if (c->nativeScale < threshold || c->nativeScale > band) continue;
          const double d = density(c);
          if (d > bestDensity + 1e-12 ||
              (std::abs(d - bestDensity) <= 1e-12 && pick &&
               c->nativeScale < pick->nativeScale)) {
            bestDensity = d;
            pick = c;
          }
        }
      }
      if (!pick) pick = coarsest;  // zoomed past every chart -> coarsest
      if (pick) m_needed.insert(pick->name);
    }
  }

  // --- Load: needed cells not already requested. ---
  for (const QString& name : m_needed) {
    if (m_requested.contains(name)) continue;
    auto it = m_catalog.constFind(name);
    if (it == m_catalog.cend()) continue;
    m_requested.insert(name);
    QMetaObject::invokeMethod(m_worker, "loadCell", Qt::QueuedConnection,
                              Q_ARG(ocpn::qtui::CellExtent, it.value()));
  }

  // --- Evict: loaded cells no longer in the needed set. ---
  QList<QString> evict;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it) {
    const LoadedCell& lc = it.value();
    if (!lc.extent.valid()) continue;  // demo chart -- never evict
    if (!m_needed.contains(it.key())) evict.append(it.key());
  }
  for (const QString& name : evict) {
    m_compositor->removeLayer(m_loaded.value(name).layerId);
    m_loaded.remove(name);
    m_requested.remove(name);  // eligible to reload when needed again
  }
  if (!evict.isEmpty()) update();
}

void ChartCanvas::zoomIn() {
  const int w = static_cast<int>(width());
  const int h = static_cast<int>(height());
  m_viewport->zoomAt(w / 2.0, h / 2.0, 1.4, w, h);
}

void ChartCanvas::zoomOut() {
  const int w = static_cast<int>(width());
  const int h = static_cast<int>(height());
  m_viewport->zoomAt(w / 2.0, h / 2.0, 1.0 / 1.4, w, h);
}

void ChartCanvas::fitWorld() {
  if (!m_boundary_provider) return;
  const double n = m_boundary_provider->northLat();
  const double s = m_boundary_provider->southLat();
  const double e = m_boundary_provider->eastLon();
  const double w = m_boundary_provider->westLon();
  if (e <= w || n <= s) return;
  m_viewport->setCenter((n + s) / 2.0, (e + w) / 2.0);
  const double cw = width() > 0 ? width() : 1024.0;
  const double ch = height() > 0 ? height() : 720.0;
  m_viewport->setScale(std::min(cw / (e - w), ch / (n - s)) * 0.9);
}

void ChartCanvas::applyDisplaySettings(
    S52VectorChartProvider* provider) const {
  if (!provider) return;
  provider->setDisplayCategory(m_display_category);
  provider->setShowSoundings(m_show_soundings);
  provider->setShowText(m_show_text);
}

void ChartCanvas::setDisplayCategory(int cat) {
  if (cat == m_display_category) return;
  m_display_category = cat;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setDisplayCategory(cat);
  Q_EMIT displayCategoryChanged();
  update();
}

void ChartCanvas::setShowSoundings(bool on) {
  if (on == m_show_soundings) return;
  m_show_soundings = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowSoundings(on);
  Q_EMIT showSoundingsChanged();
  update();
}

void ChartCanvas::setShowText(bool on) {
  if (on == m_show_text) return;
  m_show_text = on;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setShowText(on);
  Q_EMIT showTextChanged();
  update();
}

QSGNode* ChartCanvas::updatePaintNode(QSGNode* old_node,
                                      UpdatePaintNodeData* /*update_data*/) {
  QSGNode* root = old_node;
  if (!root) {
    root = new QSGNode();
    m_world_anchored_root = new QSGTransformNode();
    root->appendChildNode(m_world_anchored_root);
    m_display_anchored_root = new QSGTransformNode();
    root->appendChildNode(m_display_anchored_root);
  }

  // World-anchored root: the Viewport transform. Pan/zoom mutates this
  // one matrix and all world-anchored Layer subtrees follow.
  if (m_world_anchored_root) {
    m_world_anchored_root->setMatrix(
        m_viewport->transformMatrix(static_cast<int>(width()),
                                    static_cast<int>(height())));
  }
  // Display-anchored root stays identity.

  m_compositor->syncToScene(m_world_anchored_root, m_display_anchored_root,
                            window());
  return root;
}

void ChartCanvas::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = true;
    m_drag_last_pos = event->position();
    event->accept();
  } else {
    QQuickItem::mousePressEvent(event);
  }
}

void ChartCanvas::mouseMoveEvent(QMouseEvent* event) {
  if (m_dragging) {
    const QPointF pos = event->position();
    const QPointF delta = pos - m_drag_last_pos;
    m_drag_last_pos = pos;
    m_viewport->panBy(delta.x(), delta.y());
    event->accept();
    return;
  }
  QQuickItem::mouseMoveEvent(event);
}

void ChartCanvas::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && m_dragging) {
    m_dragging = false;
    event->accept();
  } else {
    QQuickItem::mouseReleaseEvent(event);
  }
}

void ChartCanvas::wheelEvent(QWheelEvent* event) {
  // Vertical wheel: zoom. 120 units = one notch on a normal mouse wheel.
  // Trackpads use smaller pixelDelta values; both feed into angleDelta.
  const int deg = event->angleDelta().y() / 8;  // degrees (eighths)
  if (deg == 0) {
    QQuickItem::wheelEvent(event);
    return;
  }
  // Each notch (15°) zooms by sqrt(2) -- four notches doubles/halves.
  const double factor = std::pow(2.0, deg / 30.0);
  const QPointF p = event->position();
  m_viewport->zoomAt(p.x(), p.y(), factor,
                     static_cast<int>(width()),
                     static_cast<int>(height()));
  event->accept();
}

}  // namespace ocpn::qtui
