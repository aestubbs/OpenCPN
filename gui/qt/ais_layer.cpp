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

#include "aa_line.h"
#include "sg_builder.h"  // SgBuilder::renderText
#include "sg_helpers.h"

namespace ocpn::qtui {

namespace {
// Symbol size (logical px).
constexpr float kSymbolPx = 11.0f;
// COG/SOG predictor: how far ahead, in minutes. World length =
// sog_knots * (minutes/60) / 60 degrees.
constexpr double kPredictMinutes = 6.0;
constexpr float kVectorPx = 2.0f;  // COG/SOG predictor line width

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
QSGGeometryNode* makeSymbol(AisCat cat) {
  const QColor col = catColor(cat);
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

AisLayer::TargetNode AisLayer::buildTarget(const AisTarget& t,
                                           QQuickWindow* window) {
  TargetNode tn;
  tn.pos = new QSGTransformNode();

  // Symbol: a per-category shape pointing "north" (local -y), in logical px.
  // The symbolXf transform rotates it to the course and scales px -> world.
  tn.symbolXf = new QSGTransformNode();
  tn.shipType = t.shipType;
  tn.sym = makeSymbol(catOf(t.shipType));
  tn.symbolXf->appendChildNode(tn.sym);
  tn.pos->appendChildNode(tn.symbolXf);

  // COG/SOG predictor vector is built (and rebuilt on course change) in
  // updateTarget via the AA-line shader -- length in world units (scales
  // with zoom, as a real predicted distance should), width screen-fixed.

  // Name label: rendered once to a texture, screen-fixed via labelXf scale.
  if (window && !t.name.isEmpty()) {
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
  const bool first = !tn.built;  // must always initialise the transforms
  tn.built = true;

  // Ship type often arrives after the first position report (static message),
  // so rebuild the symbol shape/colour when it changes.
  if (t.shipType != tn.shipType) {
    if (tn.sym) {
      tn.symbolXf->removeChildNode(tn.sym);
      delete tn.sym;
    }
    tn.sym = makeSymbol(catOf(t.shipType));
    tn.symbolXf->appendChildNode(tn.sym);
    tn.shipType = t.shipType;
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
  if (course_changed || first) {
    if (tn.predictor) {
      tn.pos->removeChildNode(tn.predictor);
      delete tn.predictor;
      tn.predictor = nullptr;
    }
    const double len_deg = t.sog * (kPredictMinutes / 60.0) / 60.0;
    if (len_deg > 0.0) {
      tn.predictor = makeAaLineNode({QPointF(0, 0), headingVec(t.cog) * len_deg},
                                    catColor(catOf(t.shipType)), kVectorPx);
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
