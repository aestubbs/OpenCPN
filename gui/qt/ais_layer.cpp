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

#include <QDateTime>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGNode>
#include <QSGTexture>
#include <QSGTransformNode>
#include <QSet>

#include "aa_line.h"
#include "ais_config.h"
#include "display_config.h"
#include "sg_builder.h"  // SgBuilder::renderText
#include "sg_helpers.h"

namespace ocpn::qtui {

namespace {
// Symbol size (logical px).
constexpr float kSymbolPx = 11.0f;
constexpr float kVectorPx = 2.0f;  // COG/SOG predictor line width

// COG/SOG predictor reach, in minutes. The AIS-target predictor length is
// user-set (Options > Ships > AIS Targets); when "sync with own ship" is on it
// follows the own-ship predictor (Options > Display > General) instead.
double predictMinutes() {
  const AisConfig& a = AisConfig::instance();
  return a.syncPredictorWithOwnShip()
             ? DisplayConfig::instance().cogPredictorMinutes()
             : a.predictorMinutes();
}

// Vessel category from the AIS ship-and-cargo type (0-99).
enum class AisCat { Default, Sailing, Pleasure, Fishing, Hsc, Service,
                    Passenger, Cargo, Tanker };

AisCat catOf(int st) {
  if (st == 36) return AisCat::Sailing;
  if (st == 37) return AisCat::Pleasure;
  if (st == 30) return AisCat::Fishing;
  if (st >= 40 && st <= 49) return AisCat::Hsc;
  if (st >= 50 && st <= 55) return AisCat::Service;  // pilot/SAR/tug/...
  if (st >= 60 && st <= 69) return AisCat::Passenger;
  if (st >= 70 && st <= 79) return AisCat::Cargo;
  if (st >= 80 && st <= 89) return AisCat::Tanker;
  return AisCat::Default;
}

// Dangerous targets (CPA/TCPA inside the warning thresholds) are drawn in a
// vivid alert red, overriding their category colour -- mirrors wx's AIS alert
// rendering.
const QColor kDangerColor(255, 0, 0);

// Selected-vessel trail: a thin light-grey poly-line of where it has been.
const QColor kTrailColor(190, 190, 190);
constexpr float kTrailPx = 2.0f;

QColor catColor(AisCat c) {
  switch (c) {
    case AisCat::Sailing:   return QColor(0, 150, 40);
    case AisCat::Pleasure:  return QColor(0, 150, 140);
    case AisCat::Fishing:   return QColor(200, 120, 0);
    case AisCat::Hsc:       return QColor(200, 0, 150);
    case AisCat::Service:   return QColor(0, 120, 200);
    case AisCat::Passenger: return QColor(40, 90, 210);
    case AisCat::Cargo:     return QColor(110, 140, 40);
    case AisCat::Tanker:    return QColor(200, 40, 40);
    default:                return QColor(0, 140, 0);
  }
}

bool isShip(AisCat c) {
  return c == AisCat::Cargo || c == AisCat::Tanker || c == AisCat::Passenger;
}

// Build the symbol shape for a category, pointing "north" (local -y), in
// logical px. Big ships get an elongated hull; small craft a triangle (HSC
// narrower). DrawTriangles only (Metal rejects fans).
QSGGeometryNode* makeSymbol(AisCat cat, bool dangerous) {
  const QColor col = dangerous ? kDangerColor : catColor(cat);
  const float h = kSymbolPx;
  if (isShip(cat)) {
    auto* n = sg::makeFlatColorNode(col, QSGGeometry::DrawTriangles, 9);
    QSGGeometry::Point2D* v = n->geometry()->vertexDataAsPoint2D();
    const QPointF bow(0, -h * 1.3f), mr(h * 0.45f, -h * 0.15f),
        sr(h * 0.38f, h), sl(-h * 0.38f, h), ml(-h * 0.45f, -h * 0.15f);
    const QPointF p[9] = {bow, mr, ml, mr, sr, sl, mr, sl, ml};
    for (int i = 0; i < 9; ++i) v[i].set(p[i].x(), p[i].y());
    return n;
  }
  const float wb = (cat == AisCat::Hsc) ? 0.32f : 0.5f;
  auto* n = sg::makeFlatColorNode(col, QSGGeometry::DrawTriangles, 3);
  QSGGeometry::Point2D* v = n->geometry()->vertexDataAsPoint2D();
  v[0].set(0.0f, -h);
  v[1].set(-h * wb, h * 0.5f);
  v[2].set(h * wb, h * 0.5f);
  return n;
}
}  // namespace

AisLayer::AisLayer(NavDataProvider* provider, const Viewport* viewport,
                   QObject* parent)
    : NavLayer(provider, viewport, parent) {
  setOwner(QStringLiteral("core.ais"));
  connectData(&NavDataProvider::dynamicChanged);
  // An AIS or own-ship-predictor setting change (predictor length, sync,
  // show-names) rebuilds the retained target nodes on the next sync. These
  // fire on user action, not per frame, so a wholesale rebuild is cheap.
  auto bump = [this]() {
    m_config_dirty = true;
    emit dirty();
  };
  connect(&AisConfig::instance(), &AisConfig::changed, this, bump);
  connect(&DisplayConfig::instance(), &DisplayConfig::changed, this, bump);
}

AisLayer::TargetNode AisLayer::buildTarget(const AisTarget& t,
                                           QQuickWindow* window) {
  TargetNode tn;
  tn.pos = new QSGTransformNode();

  // Symbol: a per-category shape pointing "north" (local -y), in logical px.
  // The symbolXf transform rotates it to the course and scales px -> world.
  tn.symbolXf = new QSGTransformNode();
  tn.shipType = t.shipType;
  tn.dangerous = t.dangerous;
  tn.sym = makeSymbol(catOf(t.shipType), t.dangerous);
  tn.symbolXf->appendChildNode(tn.sym);
  tn.pos->appendChildNode(tn.symbolXf);

  // COG/SOG predictor vector is built (and rebuilt on course change) in
  // updateTarget via the AA-line shader -- length in world units (scales
  // with zoom, as a real predicted distance should), width screen-fixed.

  // Name label: rendered once to a texture, screen-fixed via labelXf scale.
  // Suppressed when Options > Ships > AIS Targets > Show names is off.
  if (window && !t.name.isEmpty() && AisConfig::instance().showNames()) {
    const QImage img =
        SgBuilder::renderText(t.name, catColor(catOf(t.shipType)), 9.0f);
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
  const bool danger_changed = (t.dangerous != tn.dangerous);
  const bool first = !tn.built;  // must always initialise the transforms
  tn.built = true;

  // Ship type often arrives after the first position report (static message),
  // so rebuild the symbol shape/colour when it changes -- or when the
  // dangerous state flips (category colour <-> alert red).
  if (t.shipType != tn.shipType || danger_changed) {
    if (tn.sym) {
      tn.symbolXf->removeChildNode(tn.sym);
      delete tn.sym;
    }
    tn.sym = makeSymbol(catOf(t.shipType), t.dangerous);
    tn.symbolXf->appendChildNode(tn.sym);
    tn.shipType = t.shipType;
    tn.dangerous = t.dangerous;
  }

  // Symbol orientation + screen-fixed size: on first sight, course or zoom
  // change. rotate(cog) maps the local north-up apex (0,-1) to the
  // screen-correct heading; scale(wpp) fixes the px size. A target with no
  // course (cog < 0, "unavailable") points north.
  if (course_changed || scale_changed || first) {
    QMatrix4x4 m;
    m.scale(static_cast<float>(wpp));
    m.rotate(static_cast<float>(t.cog < 0.0 ? 0.0 : t.cog), 0.0f, 0.0f, 1.0f);
    tn.symbolXf->setMatrix(m);
  }

  // Predictor vector (world units, AA-line) -- rebuilt on course/speed
  // change. Drawn UNDER the symbol triangle so the marker stays on top.
  if (course_changed || danger_changed || first) {
    if (tn.predictor) {
      tn.pos->removeChildNode(tn.predictor);
      delete tn.predictor;
      tn.predictor = nullptr;
    }
    const double len_deg = t.sog * (predictMinutes() / 60.0) / 60.0;
    if (len_deg > 0.0) {
      const QColor vc = t.dangerous ? kDangerColor : catColor(catOf(t.shipType));
      tn.predictor = makeAaLineNode({QPointF(0, 0), headingVec(t.cog) * len_deg},
                                    vc, kVectorPx);
      if (tn.predictor)
        tn.pos->insertChildNodeBefore(tn.predictor, tn.symbolXf);
    }
  }

  // Label screen-fixed scale -- on first sight or zoom change.
  if (tn.labelXf && (scale_changed || first)) {
    QMatrix4x4 m;
    m.scale(static_cast<float>(wpp));
    tn.labelXf->setMatrix(m);
  }

  tn.cog = t.cog;
  tn.sog = t.sog;
}

QSGNode* AisLayer::updateSubtree(QSGNode* /*old*/, QQuickWindow* window) {
  if (!m_root) m_root = new QSGNode();

  // A settings change (predictor length / sync / show-names) drops the retained
  // nodes so they rebuild this frame with the new values; the per-MMSI loop
  // below then re-creates them via buildTarget.
  if (m_config_dirty) {
    m_config_dirty = false;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
      m_root->removeChildNode(it.value().pos);
      delete it.value().pos;  // deletes the whole group
    }
    m_nodes.clear();
    // The predictor length may have changed, so the trail window (5x) did too:
    // re-seed each trail from SQLite over the new window.
    for (auto it = m_trails.begin(); it != m_trails.end(); ++it) {
      it.value().seeded = false;
      it.value().dirty = true;
    }
  }

  const bool scale_changed = (currentScale() != m_built_scale);
  m_built_scale = currentScale();

  QList<AisTarget> targets =
      provider() ? provider()->aisTargets() : QList<AisTarget>();

  // wx menu parity: master visibility and the moored filter. A filtered
  // target simply never lands in `seen`, so the drop pass below removes
  // its node (and its trail ages out with it).
  const AisConfig& acfg = AisConfig::instance();
  if (!acfg.showTargets()) targets.clear();
  if (acfg.hideMoored()) {
    const double kts = acfg.suppressAnchoredSpeedMax();
    targets.removeIf([kts](const AisTarget& t) {
      return t.sog >= 0.0 && t.sog <= kts;
    });
  }

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

  if (acfg.showTargetTracks()) {
    updateTrails(targets, QDateTime::currentMSecsSinceEpoch());
  } else if (!m_trails.isEmpty()) {
    for (auto it = m_trails.begin(); it != m_trails.end(); ++it) {
      if (it.value().node) {
        m_root->removeChildNode(it.value().node);
        delete it.value().node;
      }
    }
    m_trails.clear();
  }
  return m_root;
}

void AisLayer::setTrailEnabled(int mmsi, bool on) {
  if (on) {
    if (!m_trails.contains(mmsi))
      m_trails.insert(mmsi, Trail{});  // seeded + drawn on the next sync
  } else if (auto it = m_trails.find(mmsi); it != m_trails.end()) {
    if (it.value().node && m_root) {
      m_root->removeChildNode(it.value().node);
      delete it.value().node;
    }
    m_trails.erase(it);
  }
  emit dirty();
}

void AisLayer::updateTrails(const QList<AisTarget>& targets, qint64 now_ms) {
  if (m_trails.isEmpty()) return;
  const qint64 window_ms = static_cast<qint64>(5.0 * predictMinutes() * 60000.0);
  const qint64 cutoff = now_ms - window_ms;

  for (auto it = m_trails.begin(); it != m_trails.end(); ++it) {
    const int mmsi = it.key();
    Trail& tr = it.value();

    // Seed the recorded history once (oldest-first) from the persistent store.
    if (!tr.seeded) {
      tr.points =
          provider() ? provider()->aisTrack(mmsi, cutoff) : QVector<AisTrackPoint>();
      tr.seeded = true;
      tr.dirty = true;
    }
    // Slide the live end: append the current fix if the target is present and
    // has moved since the last recorded point.
    for (const AisTarget& t : targets) {
      if (t.mmsi != mmsi) continue;
      if (tr.points.isEmpty() || tr.points.last().lat != t.lat ||
          tr.points.last().lon != t.lon) {
        tr.points.append({now_ms, t.lat, t.lon});
        tr.dirty = true;
      }
      break;
    }
    // Drop points that have slid out of the window (oldest at the front).
    int drop = 0;
    while (drop < tr.points.size() && tr.points[drop].t < cutoff) ++drop;
    if (drop > 0) {
      tr.points.remove(0, drop);
      tr.dirty = true;
    }

    if (!tr.dirty) continue;
    tr.dirty = false;
    if (tr.node) {
      m_root->removeChildNode(tr.node);
      delete tr.node;
      tr.node = nullptr;
    }
    if (tr.points.size() >= 2) {
      QList<QPointF> wpts;
      wpts.reserve(tr.points.size());
      for (const AisTrackPoint& p : tr.points) wpts.append(world(p.lat, p.lon));
      tr.node = makeAaLineNode(wpts, kTrailColor, kTrailPx);
      if (tr.node)
        m_root->prependChildNode(tr.node);  // draw under the target symbols
    }
  }
}

}  // namespace ocpn::qtui
