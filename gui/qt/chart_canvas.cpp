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
#include <QWheelEvent>

#include "chart_layer.h"
#include "layer_compositor.h"
#include "raster_chart_provider.h"
#include "s52_engine.h"
#include "s52_vector_chart_provider.h"
#include "test_chart.h"
#include "viewport.h"

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

  // Test chart -- RasterChartProvider holding a programmatically-drawn
  // QImage. Real chart-DB integration plugs in by adding another
  // ChartProvider implementation (KAP/BSB, MBTiles, S-52 vector via
  // s52plib at P2.8).
  auto* provider = new RasterChartProvider(
      "demo.test-chart",
      MakeTestChart(kTestNorth, kTestSouth, kTestWest, kTestEast),
      kTestNorth, kTestSouth, kTestWest, kTestEast);
  m_compositor->addLayer(new ChartLayer(provider, m_viewport.get()));

  // Repaint when:
  //   - any Layer dirties (data change, visibility/z-order/opacity).
  //   - the Viewport pans / zooms (transform matrix changes).
  //   - the canvas resizes (transform depends on width/height).
  connect(m_compositor.get(), &LayerCompositor::changed, this,
          [this]() { update(); });
  connect(m_viewport.get(), &Viewport::changed, this,
          [this]() { update(); });
  connect(this, &QQuickItem::widthChanged, this, [this]() { update(); });
  connect(this, &QQuickItem::heightChanged, this, [this]() { update(); });
}

ChartCanvas::~ChartCanvas() = default;

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

  // Prefer real ENC cells if configured at build time (OCPN_QT_TEST_ENC);
  // otherwise fall back to the synthetic demo chart. OCPN_QT_TEST_ENC may
  // be a single .000 file or a DIRECTORY (every .000 under it is loaded
  // and merged into one surface). Either way we get a world-coordinate
  // buffer the vector provider turns into a static QSGGeometry tree.
  const QString enc_path = QString::fromUtf8(OCPN_QT_TEST_ENC);
  s52sg::Buffer buf;
  double n = kTestNorth, s = kTestSouth, e = kTestEast, w = kTestWest;
  QString id = "demo.s52-chart";

  if (!enc_path.isEmpty()) {
    QStringList cells;
    QFileInfo fi(enc_path);
    if (fi.isDir()) {
      QDirIterator it(enc_path, {"*.000"}, QDir::Files,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) cells << it.next();
      cells.sort();
      id = "enc.dir." + fi.fileName();
    } else {
      cells << enc_path;
      id = "enc." + enc_path.section('/', -1);
    }
    buf = m_s52_engine->loadEncCells(
        cells, QString::fromUtf8(OCPN_QT_S57DATA_DIR), &n, &s, &e, &w);
    // Recentre + fit the viewport to the combined extent. The canvas may
    // not be laid out yet, so fall back to the QML window's default size.
    if (!buf.empty() && e > w && n > s) {
      m_viewport->setCenter((n + s) / 2.0, (e + w) / 2.0);
      const double cw = width() > 0 ? width() : 1024.0;
      const double ch = height() > 0 ? height() : 720.0;
      const double fit = std::min(cw / (e - w), ch / (n - s)) * 0.9;
      m_viewport->setScale(fit);
    }
  } else {
    buf = m_s52_engine->buildDemoChart(kTestNorth, kTestSouth, kTestEast,
                                       kTestWest);
  }

  if (!buf.empty()) {
    auto* provider = new S52VectorChartProvider(id, std::move(buf), n, s, w, e,
                                                m_viewport.get());
    provider->setDisplayCategory(m_display_category);
    m_s52_provider = provider;
    m_compositor->addLayer(new ChartLayer(provider, m_viewport.get()));
    update();
  }
}

void ChartCanvas::setDisplayCategory(int cat) {
  if (cat == m_display_category) return;
  m_display_category = cat;
  if (m_s52_provider) m_s52_provider->setDisplayCategory(cat);
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
