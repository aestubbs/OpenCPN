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
 * Implement own_ship_layer.h.
 */

#include "own_ship_layer.h"

#include <QMatrix4x4>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGTransformNode>

#include "aa_line.h"
#include "sg_helpers.h"

namespace ocpn::qtui {

namespace {
constexpr float kShipPx = 15.0f;           // own-ship symbol size (logical px)
const QColor kOwnColor(200, 0, 0);
const QColor kLaylineColor(120, 120, 120);
constexpr double kPredictMinutes = 6.0;    // COG/SOG vector look-ahead
constexpr double kLaylineDeg = 40.0;       // tacking half-angle off COG
constexpr double kLaylineLenDeg = 0.6;     // layline length (world degrees)
constexpr float kVectorPx = 2.0f;          // COG/SOG predictor line width
constexpr float kLaylinePx = 1.5f;         // layline width
}  // namespace

void OwnShipLayer::buildOnce() {
  m_root = new QSGNode();
  m_opacity = new QSGOpacityNode();
  m_opacity->setOpacity(0.0);  // hidden until first valid fix
  m_root->appendChildNode(m_opacity);

  m_pos = new QSGTransformNode();
  m_opacity->appendChildNode(m_pos);

  // Laylines + COG/SOG predictor are AA-line nodes rebuilt on course change
  // (in updateSubtree); they're inserted UNDER the symbol so the marker
  // stays on top.
  m_symbolXf = new QSGTransformNode();
  {
    // A slim bow-heavy triangle pointing north (local -y), in logical px.
    auto* tri = sg::makeFlatColorNode(kOwnColor, QSGGeometry::DrawTriangles, 3);
    QSGGeometry::Point2D* v = tri->geometry()->vertexDataAsPoint2D();
    v[0].set(0.0f, -kShipPx);            // bow (north)
    v[1].set(-kShipPx * 0.45f, kShipPx * 0.6f);
    v[2].set(kShipPx * 0.45f, kShipPx * 0.6f);
    m_symbolXf->appendChildNode(tri);
  }
  m_pos->appendChildNode(m_symbolXf);
}

QSGNode* OwnShipLayer::updateSubtree(QSGNode* /*old*/, QQuickWindow* /*window*/) {
  if (!m_root) buildOnce();

  const OwnShipState s = provider() ? provider()->ownShip() : OwnShipState{};
  m_opacity->setOpacity(s.valid ? 1.0 : 0.0);
  if (!s.valid) return m_root;

  // Position: every tick.
  {
    QMatrix4x4 m;
    const QPointF w = world(s.lat, s.lon);
    m.translate(static_cast<float>(w.x()), static_cast<float>(w.y()));
    m_pos->setMatrix(m);
  }

  // Orient by heading if known, else COG.
  const double heading = (s.hdg < 360.0) ? s.hdg : s.cog;
  const bool scale_changed = (currentScale() != m_built_scale);
  m_built_scale = currentScale();
  const bool course_changed = (s.cog != m_cog) || (s.sog != m_sog);

  if (course_changed || scale_changed) {
    QMatrix4x4 m;
    m.scale(static_cast<float>(worldPerPx()));
    m.rotate(static_cast<float>(heading), 0.0f, 0.0f, 1.0f);
    m_symbolXf->setMatrix(m);
  }

  if (course_changed) {
    // Rebuild the AA-line vectors (world units; widths screen-fixed by the
    // shader). Laylines are dashed (cartographic convention). Re-insert
    // before the symbol so the marker draws on top: laylines first, then the
    // predictor just under the symbol.
    if (m_laylines) {
      m_pos->removeChildNode(m_laylines);
      delete m_laylines;
      m_laylines = nullptr;
    }
    if (m_predictor) {
      m_pos->removeChildNode(m_predictor);
      delete m_predictor;
      m_predictor = nullptr;
    }

    const QPointF port = headingVec(s.cog - kLaylineDeg) * kLaylineLenDeg;
    const QPointF stbd = headingVec(s.cog + kLaylineDeg) * kLaylineLenDeg;
    // Port -> origin -> starboard as one dashed V.
    m_laylines = makeAaLineNode({port, QPointF(0, 0), stbd}, kLaylineColor,
                                kLaylinePx, /*closed=*/false,
                                /*dash_on_px=*/8.0f, /*dash_off_px=*/6.0f);
    if (m_laylines) m_pos->insertChildNodeBefore(m_laylines, m_symbolXf);

    const double len = s.sog * (kPredictMinutes / 60.0) / 60.0;
    if (len > 0.0) {
      m_predictor = makeAaLineNode({QPointF(0, 0), headingVec(s.cog) * len},
                                   kOwnColor, kVectorPx);
      if (m_predictor) m_pos->insertChildNodeBefore(m_predictor, m_symbolXf);
    }
  }

  m_cog = s.cog;
  m_sog = s.sog;
  return m_root;
}

}  // namespace ocpn::qtui
