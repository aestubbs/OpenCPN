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

#include <cmath>

#include "aa_line.h"
#include "display_config.h"
#include "own_ship_config.h"
#include "sg_helpers.h"

namespace ocpn::qtui {

namespace {
constexpr float kShipPx = 15.0f;           // own-ship symbol size (logical px)
const QColor kOwnColor(200, 0, 0);
const QColor kLaylineColor(120, 120, 120);
constexpr double kLaylineDeg = 40.0;       // tacking half-angle off COG
constexpr double kLaylineLenDeg = 0.6;     // layline length (world degrees)
constexpr float kVectorPx = 2.0f;          // COG/SOG predictor line width
constexpr float kLaylinePx = 1.5f;         // layline width

// Range rings.
const QColor kRingColor(90, 110, 150);     // muted blue-grey
constexpr float kRingPx = 1.0f;            // ring line width (screen-fixed)
constexpr int kRingSegments = 72;          // circle tessellation
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
// Re-size the rings only when the ship has moved this far in latitude (the
// world radius depends on cos(lat); a few NM of N/S drift is negligible).
constexpr double kRingLatEpsilon = 0.05;

// Ring spacing -> nautical miles. ringUnit: 0 = NM, 1 = km, 2 = statute miles.
double ringSpacingNm(double spacing, int unit) {
  switch (unit) {
    case 1: return spacing * 0.539957;   // km -> NM
    case 2: return spacing * 0.868976;   // statute mile -> NM
    default: return spacing;             // NM
  }
}
}  // namespace

OwnShipLayer::OwnShipLayer(NavDataProvider* provider, const Viewport* viewport,
                           QObject* parent)
    : NavLayer(provider, viewport, parent) {
  setOwner(QStringLiteral("core.ownship"));
  connectData(&NavDataProvider::dynamicChanged);
  // A range-ring setting change (enable / count / spacing / unit) re-sizes the
  // rings on the next sync; the COG-predictor length lives on DisplayConfig and
  // is picked up via the existing course-change rebuild.
  connect(&OwnShipConfig::instance(), &OwnShipConfig::changed, this, [this]() {
    m_rings_dirty = true;
    m_symbol_dirty = true;  // icon type / dimensions / GPS offset may change
    Q_EMIT dirty();
  });
}

void OwnShipLayer::rebuildRings(double lat) {
  if (!m_pos) return;
  if (m_rings) {
    m_pos->removeChildNode(m_rings);
    delete m_rings;  // deletes the child circle nodes
    m_rings = nullptr;
  }
  m_rings_lat = lat;
  m_rings_dirty = false;

  const OwnShipConfig& cfg = OwnShipConfig::instance();
  if (!cfg.showRangeRings() || cfg.ringCount() <= 0) return;
  const double spacing_nm = ringSpacingNm(cfg.ringSpacing(), cfg.ringUnit());
  if (spacing_nm <= 0.0) return;

  // World units per NM at this latitude: worldX = lon degrees, and 1 deg lon =
  // 60*cos(lat) NM, so 1 NM = 1/(60 cos lat) world units. Mercator is conformal
  // so the same scale holds in Y locally -- a geographic circle is a circle in
  // world space to first order. Clamp cos(lat) away from the poles.
  const double cos_lat = std::max(0.05, std::cos(lat * kDeg2Rad));
  const double world_per_nm = 1.0 / (60.0 * cos_lat);

  m_rings = new QSGNode();
  for (int k = 1; k <= cfg.ringCount(); ++k) {
    const double r = k * spacing_nm * world_per_nm;
    QList<QPointF> circle;
    circle.reserve(kRingSegments);
    for (int i = 0; i < kRingSegments; ++i) {
      const double a = (2.0 * 3.14159265358979323846 * i) / kRingSegments;
      circle.append(QPointF(std::cos(a) * r, std::sin(a) * r));
    }
    if (QSGGeometryNode* ring =
            makeAaLineNode(circle, cfg.ringColor(), kRingPx, /*closed=*/true))
      m_rings->appendChildNode(ring);
  }
  if (m_rings->childCount() == 0) {
    delete m_rings;
    m_rings = nullptr;
    return;
  }
  // Draw rings under the symbol so the marker stays on top.
  m_pos->insertChildNodeBefore(m_rings, m_symbolXf);
}

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
  m_pos->appendChildNode(m_symbolXf);
  makeTriangleSymbol();  // initial marker; swapped to a hull when real-scale on
}

void OwnShipLayer::makeTriangleSymbol() {
  if (m_symbol) {
    m_symbolXf->removeChildNode(m_symbol);
    delete m_symbol;
  }
  // A slim bow-heavy triangle pointing north (local -y), in logical px.
  auto* tri = sg::makeFlatColorNode(kOwnColor, QSGGeometry::DrawTriangles, 3);
  QSGGeometry::Point2D* v = tri->geometry()->vertexDataAsPoint2D();
  v[0].set(0.0f, -kShipPx);  // bow (north)
  v[1].set(-kShipPx * 0.45f, kShipPx * 0.6f);
  v[2].set(kShipPx * 0.45f, kShipPx * 0.6f);
  m_symbolXf->appendChildNode(tri);
  m_symbol = tri;
}

void OwnShipLayer::makeHullSymbol(double lat) {
  if (m_symbol) {
    m_symbolXf->removeChildNode(m_symbol);
    delete m_symbol;
  }
  const OwnShipConfig& c = OwnShipConfig::instance();
  const double cos_lat = std::max(0.05, std::cos(lat * kDeg2Rad));
  const double wpm = 1.0 / (60.0 * 1852.0 * cos_lat);  // world units per metre
  const double L = c.loa() * wpm;
  const double B = c.beam() * wpm;
  // GPS antenna offset from the hull centre (m): +x = starboard, +y = forward.
  // Shift the hull so the antenna (the fix) sits at the origin. Forward is -y
  // in the local (north-up) frame.
  const double ox = c.gpsOffsetX() * wpm;
  const double oy = -c.gpsOffsetY() * wpm;
  // Boat-shaped pentagon pointing north (local -y): pointed bow, square stern.
  const QPointF pts[5] = {
      {0.0, -L / 2.0},                 // bow
      {B / 2.0, -L / 2.0 + 0.30 * L},  // starboard shoulder
      {B / 2.0, L / 2.0},              // starboard stern
      {-B / 2.0, L / 2.0},             // port stern
      {-B / 2.0, -L / 2.0 + 0.30 * L}  // port shoulder
  };
  // Fan the pentagon into 3 explicit triangles (Metal rejects triangle fans).
  const int idx[9] = {0, 1, 2, 0, 2, 3, 0, 3, 4};
  auto* hull = sg::makeFlatColorNode(kOwnColor, QSGGeometry::DrawTriangles, 9);
  QSGGeometry::Point2D* v = hull->geometry()->vertexDataAsPoint2D();
  for (int i = 0; i < 9; ++i)
    v[i].set(static_cast<float>(pts[idx[i]].x() - ox),
             static_cast<float>(pts[idx[i]].y() - oy));
  m_symbolXf->appendChildNode(hull);
  m_symbol = hull;
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

  // Range rings: re-sized on a setting change or noticeable latitude drift
  // (the world radius depends on cos(lat)).
  if (m_rings_dirty || std::abs(s.lat - m_rings_lat) > kRingLatEpsilon)
    rebuildRings(s.lat);

  // Orient by true heading if available; else by COG while moving. With no
  // heading and a near-zero / dropped-out COG, HOLD the last orientation
  // instead of snapping to north -- a momentary COG dropout (e.g. the GPS /
  // velocity watchdog nulling gCog ~1 Hz on a gappy feed) would otherwise spin
  // the boat icon to north and back every second.
  double heading;
  if (s.hdg < 360.0)
    heading = s.hdg;
  else if (s.sog > 0.2)
    heading = s.cog;
  else
    heading = (m_heading >= 0.0) ? m_heading : s.cog;

  const bool scale_changed = (currentScale() != m_built_scale);
  m_built_scale = currentScale();
  const bool heading_changed = (heading != m_heading);
  const bool course_changed = (s.cog != m_cog) || (s.sog != m_sog);

  // Real-scale own-ship icon (Options > Ships > Own ship): draw a to-scale hull
  // (world units, so it grows with zoom) when the icon type is real-scale, the
  // dimensions are set, and the hull is big enough on screen; otherwise the
  // fixed-size marker. The min-size switch honours minScreenSize (mm, ~4 px/mm).
  const OwnShipConfig& osc = OwnShipConfig::instance();
  bool want_hull = false;
  if (osc.iconType() != 0 && osc.loa() > 0.0 && osc.beam() > 0.0) {
    const double cos_lat = std::max(0.05, std::cos(s.lat * kDeg2Rad));
    const double loa_px =
        (osc.loa() / (60.0 * 1852.0 * cos_lat)) * currentScale();
    const double min_px = std::max(14.0, osc.minScreenSize() * 4.0);
    want_hull = loa_px >= min_px;
  }
  bool symbol_rebuilt = false;
  if (want_hull) {
    if (!m_symbol_is_hull || m_symbol_dirty ||
        std::abs(s.lat - m_symbol_lat) > kRingLatEpsilon) {
      makeHullSymbol(s.lat);
      m_symbol_is_hull = true;
      m_symbol_lat = s.lat;
      symbol_rebuilt = true;
    }
  } else if (m_symbol_is_hull || m_symbol_dirty) {
    makeTriangleSymbol();
    m_symbol_is_hull = false;
    symbol_rebuilt = true;
  }
  m_symbol_dirty = false;

  if (heading_changed || scale_changed || symbol_rebuilt) {
    QMatrix4x4 m;
    // The hull is already in world units; the marker is fixed logical-px size.
    if (!m_symbol_is_hull) m.scale(static_cast<float>(worldPerPx()));
    m.rotate(static_cast<float>(heading), 0.0f, 0.0f, 1.0f);
    m_symbolXf->setMatrix(m);
  }
  m_heading = heading;

  // heading_changed: the HDT predictor (P3.6) follows the true heading, which
  // can move while COG/SOG hold steady.
  if (course_changed || heading_changed) {
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
    if (m_hdt_predictor) {
      m_pos->removeChildNode(m_hdt_predictor);
      delete m_hdt_predictor;
      m_hdt_predictor = nullptr;
    }

    const QPointF port = headingVec(s.cog - kLaylineDeg) * kLaylineLenDeg;
    const QPointF stbd = headingVec(s.cog + kLaylineDeg) * kLaylineLenDeg;
    // Port -> origin -> starboard as one dashed V.
    m_laylines = makeAaLineNode({port, QPointF(0, 0), stbd}, kLaylineColor,
                                kLaylinePx, /*closed=*/false,
                                /*dash_on_px=*/8.0f, /*dash_off_px=*/6.0f);
    if (m_laylines) m_pos->insertChildNodeBefore(m_laylines, m_symbolXf);

    const double predict_min = DisplayConfig::instance().cogPredictorMinutes();
    const double len = s.sog * (predict_min / 60.0) / 60.0;
    if (len > 0.0) {
      m_predictor = makeAaLineNode({QPointF(0, 0), headingVec(s.cog) * len},
                                   kOwnColor, kVectorPx);
      if (m_predictor) m_pos->insertChildNodeBefore(m_predictor, m_symbolXf);
    }
    // HDT predictor (wx g_ownship_HDTpredictor_miles): a fixed-length, thinner
    // line along the true heading -- only drawn when a heading is available
    // and it differs from COG (else it would hide under the COG predictor).
    const double hdt_nm = OwnShipConfig::instance().hdtPredictorNm();
    if (hdt_nm > 0.0 && s.hdg < 360.0 &&
        std::abs(s.hdg - s.cog) > 0.5) {
      const double hdt_len = hdt_nm / 60.0;  // NM -> world degrees
      m_hdt_predictor =
          makeAaLineNode({QPointF(0, 0), headingVec(s.hdg) * hdt_len},
                         kOwnColor, kVectorPx * 0.6f, /*closed=*/false,
                         /*dash_on_px=*/6.0f, /*dash_off_px=*/4.0f);
      if (m_hdt_predictor)
        m_pos->insertChildNodeBefore(m_hdt_predictor, m_symbolXf);
    }
  }

  m_cog = s.cog;
  m_sog = s.sog;
  return m_root;
}

}  // namespace ocpn::qtui
