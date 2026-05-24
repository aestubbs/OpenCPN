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
 * Implement ais_layer.h.
 */

#include "ais_layer.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGNode>
#include <QSGTexture>
#include <QSGTransformNode>
#include <QSet>

#include "sg_builder.h"  // SgBuilder::renderText
#include "sg_helpers.h"

namespace ocpn::qtui {

namespace {
// Symbol size (logical px) and AIS target colour.
constexpr float kSymbolPx = 11.0f;
const QColor kAisColor(0, 140, 0);
// COG/SOG predictor: how far ahead, in minutes. World length =
// sog_knots * (minutes/60) / 60 degrees.
constexpr double kPredictMinutes = 6.0;

// Set a 2-vertex line geometry from (0,0) to `end` (world units).
void setVector(QSGGeometryNode* node, const QPointF& end) {
  QSGGeometry* g = node->geometry();
  g->allocate(2);
  QSGGeometry::Point2D* v = g->vertexDataAsPoint2D();
  v[0].set(0.0f, 0.0f);
  v[1].set(static_cast<float>(end.x()), static_cast<float>(end.y()));
  node->markDirty(QSGNode::DirtyGeometry);
}
}  // namespace

AisLayer::TargetNode AisLayer::buildTarget(const AisTarget& t,
                                           QQuickWindow* window) {
  TargetNode tn;
  tn.pos = new QSGTransformNode();

  // Symbol: an isoceles triangle pointing "north" (local -y), in logical px.
  // The symbolXf transform rotates it to the course and scales px -> world.
  tn.symbolXf = new QSGTransformNode();
  {
    auto* tri = sg::makeFlatColorNode(kAisColor, QSGGeometry::DrawTriangles, 3);
    QSGGeometry::Point2D* v = tri->geometry()->vertexDataAsPoint2D();
    const float h = kSymbolPx;
    v[0].set(0.0f, -h);             // apex (north)
    v[1].set(-h * 0.5f, h * 0.5f);  // base left
    v[2].set(h * 0.5f, h * 0.5f);   // base right
    tn.symbolXf->appendChildNode(tri);
  }
  tn.pos->appendChildNode(tn.symbolXf);

  // COG/SOG predictor vector, in world units (length scales with zoom, as a
  // real predicted-distance should). Geometry filled in updateTarget.
  tn.predictor = sg::makeFlatColorNode(kAisColor, QSGGeometry::DrawLines, 0);
  tn.pos->appendChildNode(tn.predictor);

  // Name label: rendered once to a texture, screen-fixed via labelXf scale.
  if (window && !t.name.isEmpty()) {
    const QImage img = SgBuilder::renderText(t.name, kAisColor, 9.0f);
    if (!img.isNull()) {
      QSGTexture* tex = window->createTextureFromImage(
          img, QQuickWindow::TextureHasAlphaChannel);
      if (tex) {
        const qreal dpr =
            img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
        const qreal w = img.width() / dpr, hh = img.height() / dpr;
        auto* node = window->createImageNode();
        node->setTexture(tex);
        node->setOwnsTexture(true);  // label texture lives with this node
        node->setFiltering(QSGTexture::Linear);
        // Offset to the lower-right of the symbol (in px; scaled by labelXf).
        node->setRect(QRectF(kSymbolPx * 0.6, kSymbolPx * 0.4, w, hh));
        tn.labelXf = new QSGTransformNode();
        tn.labelXf->appendChildNode(node);
        tn.pos->appendChildNode(tn.labelXf);
      }
    }
  }
  return tn;
}

void AisLayer::updateTarget(TargetNode& tn, const AisTarget& t,
                            bool scale_changed) {
  // Position: every tick.
  {
    QMatrix4x4 m;
    const QPointF w = world(t.lat, t.lon);
    m.translate(static_cast<float>(w.x()), static_cast<float>(w.y()));
    tn.pos->setMatrix(m);
  }

  const double wpp = worldPerPx();
  const bool course_changed = (t.cog != tn.cog) || (t.sog != tn.sog);

  // Symbol orientation + screen-fixed size: on course or zoom change.
  // rotate(cog) maps the local north-up apex (0,-1) to (sin cog, -cos cog),
  // i.e. the screen-correct heading vector; scale(wpp) fixes the px size.
  if (course_changed || scale_changed) {
    QMatrix4x4 m;
    m.scale(static_cast<float>(wpp));
    m.rotate(static_cast<float>(t.cog), 0.0f, 0.0f, 1.0f);
    tn.symbolXf->setMatrix(m);
  }

  // Predictor vector (world units) -- on course/speed change.
  if (course_changed) {
    const double len_deg = t.sog * (kPredictMinutes / 60.0) / 60.0;
    setVector(tn.predictor, headingVec(t.cog) * len_deg);
  }

  // Label screen-fixed scale -- on zoom change.
  if (tn.labelXf && (scale_changed || tn.cog < 0.0)) {
    QMatrix4x4 m;
    m.scale(static_cast<float>(wpp));
    tn.labelXf->setMatrix(m);
  }

  tn.cog = t.cog;
  tn.sog = t.sog;
}

QSGNode* AisLayer::updateSubtree(QSGNode* /*old*/, QQuickWindow* window) {
  if (!m_root) m_root = new QSGNode();

  const bool scale_changed = (currentScale() != m_built_scale);
  m_built_scale = currentScale();

  const QList<AisTarget> targets =
      provider() ? provider()->aisTargets() : QList<AisTarget>();

  QSet<int> seen;
  seen.reserve(targets.size());
  for (const AisTarget& t : targets) {
    seen.insert(t.mmsi);
    auto it = m_nodes.find(t.mmsi);
    if (it == m_nodes.end()) {
      TargetNode tn = buildTarget(t, window);
      m_root->appendChildNode(tn.pos);
      it = m_nodes.insert(t.mmsi, tn);
    }
    updateTarget(it.value(), t, scale_changed);
  }

  // Drop targets that disappeared.
  for (auto it = m_nodes.begin(); it != m_nodes.end();) {
    if (seen.contains(it.key())) {
      ++it;
      continue;
    }
    m_root->removeChildNode(it.value().pos);
    delete it.value().pos;  // deletes the whole group
    it = m_nodes.erase(it);
  }

  return m_root;
}

}  // namespace ocpn::qtui
