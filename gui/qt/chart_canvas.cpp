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

#include <QDirIterator>
#include <QFileInfo>
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
  connect(this, &QQuickItem::widthChanged, this, [this]() { update(); });
  connect(this, &QQuickItem::heightChanged, this, [this]() { update(); });
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
    provider->setDisplayCategory(m_display_category);
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

  // Fit the viewport to the set once (on the first batch). The GSHHS world
  // backdrop is always present, so the user can freely zoom back out to the
  // whole globe from here.
  const double n = m_boundary_provider->northLat();
  const double s = m_boundary_provider->southLat();
  const double e = m_boundary_provider->eastLon();
  const double w = m_boundary_provider->westLon();
  if (!m_world_fitted && e > w && n > s) {
    m_world_fitted = true;
    m_viewport->setCenter((n + s) / 2.0, (e + w) / 2.0);
    const double cw = width() > 0 ? width() : 1024.0;
    const double ch = height() > 0 ? height() : 720.0;
    const double fit = std::min(cw / (e - w), ch / (n - s)) * 0.9;
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
  const double scale = m_viewport->scale();
  const double cw = width() > 0 ? width() : 1024.0;
  const double ch = height() > 0 ? height() : 720.0;
  const double hlon = (cw / 2.0) / scale, hlat = (ch / 2.0) / scale;
  c.band = CellExtent::bandFromName(id);
  const int pb = primaryBand(scale);
  // Eviction window (also the "still wanted" test on arrival): one band
  // either side of the reference band, within a widened view.
  const bool wanted =
      !buffer.empty() &&
      cellWanted(c, m_viewport->centerLat() - hlat * 1.5,
                 m_viewport->centerLat() + hlat * 1.5,
                 m_viewport->centerLon() - hlon * 1.5,
                 m_viewport->centerLon() + hlon * 1.5, pb - 1, pb + 1);
  if (!wanted) {
    m_requested.remove(id);  // allow a future re-request when back in view
    return;
  }

  const QString layerId = "enc." + id;
  auto* provider = new S52VectorChartProvider(layerId, buffer, north, south,
                                              west, east, m_viewport.get());
  provider->setDisplayCategory(m_display_category);
  auto* layer = new ChartLayer(provider, m_viewport.get());
  // Quilt z-order: coarser bands under finer (band 1 overview at the bottom,
  // band 6 berthing on top), all under the boundary grid (100). So where a
  // finer cell covers a coarser one the coarse is hidden -- no cross-band
  // stacking / seam show-through.
  layer->setZOrder(c.band > 0 ? c.band : 1);
  m_compositor->addLayer(layer);
  LoadedCell lc;
  lc.extent = c;
  lc.layerId = layerId;
  lc.provider = provider;
  m_loaded.insert(id, lc);
  update();
}

int ChartCanvas::primaryBand(double scale) {
  // Map the viewport scale (pixels per degree) to an approximate 1:N display
  // scale (nominal ~96 dpi) and pick the NOAA usage band whose compilation
  // scale that's closest to. Zooming in steps the reference band 1 -> 6.
  if (scale <= 0.0) return 1;
  const double n = 4.23e8 / scale;  // ~ 1:n display-scale denominator
  if (n > 1000000.0) return 1;      // overview
  if (n > 350000.0) return 2;       // general
  if (n > 90000.0) return 3;        // coastal
  if (n > 30000.0) return 4;        // approach
  if (n > 8000.0) return 5;         // harbour
  return 6;                         // berthing
}

bool ChartCanvas::cellWanted(const CellExtent& c, double lat_min,
                             double lat_max, double lon_min, double lon_max,
                             int lo_band, int hi_band) const {
  if (c.band < lo_band || c.band > hi_band) return false;
  return c.intersects(lat_min, lat_max, lon_min, lon_max);
}

void ChartCanvas::updateVisibleCells() {
  if (!m_worker || m_catalog.isEmpty()) return;

  const double scale = m_viewport->scale();
  const double cw = width() > 0 ? width() : 1024.0;
  const double ch = height() > 0 ? height() : 720.0;
  const double half_lon = (cw / 2.0) / scale;
  const double half_lat = (ch / 2.0) / scale;
  const double c_lon = m_viewport->centerLon();
  const double c_lat = m_viewport->centerLat();
  const int pb = primaryBand(scale);

  // Quilting: show ONLY the reference band -- gaps fall through to the GSHHS
  // world backdrop rather than a coarser ENC band, so overlap zones don't
  // stack two bands of geometry/symbols (the big over-draw cost the user
  // hit). Eviction keeps a one-band-either-side window + a 50%-widened view
  // so a small zoom/pan across a band boundary doesn't immediately unload
  // (hysteresis; the debounce smooths the rest).
  // --- Load: in view, reference band, not already requested. ---
  for (auto it = m_catalog.cbegin(); it != m_catalog.cend(); ++it) {
    const CellExtent& c = it.value();
    if (m_requested.contains(c.name)) continue;
    if (!cellWanted(c, c_lat - half_lat, c_lat + half_lat, c_lon - half_lon,
                    c_lon + half_lon, pb, pb))
      continue;
    m_requested.insert(c.name);
    QMetaObject::invokeMethod(m_worker, "loadCell", Qt::QueuedConnection,
                              Q_ARG(ocpn::qtui::CellExtent, c));
  }

  // --- Evict: loaded cells now outside the widened view / band window. ---
  const double e_hlon = half_lon * 1.5;
  const double e_hlat = half_lat * 1.5;
  QList<QString> evict;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it) {
    const LoadedCell& lc = it.value();
    if (!lc.extent.valid()) continue;  // demo chart -- never evict
    if (!cellWanted(lc.extent, c_lat - e_hlat, c_lat + e_hlat, c_lon - e_hlon,
                    c_lon + e_hlon, pb - 1, pb + 1))
      evict.append(it.key());
  }
  for (const QString& name : evict) {
    m_compositor->removeLayer(m_loaded.value(name).layerId);
    m_loaded.remove(name);
    m_requested.remove(name);  // eligible to reload when back in view
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

void ChartCanvas::setDisplayCategory(int cat) {
  if (cat == m_display_category) return;
  m_display_category = cat;
  for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it)
    if (it.value().provider) it.value().provider->setDisplayCategory(cat);
  Q_EMIT displayCategoryChanged();
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
