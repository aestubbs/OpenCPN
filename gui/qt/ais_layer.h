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
 * AisLayer -- world-anchored Layer drawing AIS targets (P2.11).
 *
 * Retained scene graph: each target owns a small node group built ONCE (a
 * heading-oriented symbol triangle, a COG/SOG predictor vector, a name
 * label). On each provider update the layer only mutates per-target
 * QSGTransformNode matrices -- position every tick, orientation/size only
 * when the course or zoom changes -- so a moving fleet costs O(targets)
 * matrix writes, not a geometry rebuild. The target set is reconciled by
 * MMSI (add new, drop gone). This mirrors the S-52 billboard approach.
 */

#ifndef OCPN_QT_AIS_LAYER_H_
#define OCPN_QT_AIS_LAYER_H_

#include <QHash>
#include <QVector>

#include "nav_layer.h"

QT_BEGIN_NAMESPACE
class QSGNode;
class QSGGeometryNode;
class QSGTransformNode;
QT_END_NAMESPACE

namespace ocpn::qtui {

class AisLayer : public NavLayer {
  Q_OBJECT

public:
  AisLayer(NavDataProvider* provider, const Viewport* viewport,
           QObject* parent = nullptr);

  QString id() const override { return QStringLiteral("core.ais"); }
  QString name() const override { return QStringLiteral("AIS"); }

  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

  // Per-vessel trail toggle (driven from the AIS info popup via ChartCanvas).
  // A trail is drawn only for enabled MMSIs -- drawing every target's history
  // would swamp the chart. Trail length = 5x the COG/SOG predictor reach.
  void setTrailEnabled(int mmsi, bool on);
  bool trailEnabled(int mmsi) const { return m_trails.contains(mmsi); }

private:
  // One target's retained nodes. `pos` (child of the layer root) translates
  // to the world position and is updated every tick; the rest are built once
  // and only their scale/orientation transforms change on course/zoom change.
  struct TargetNode {
    QSGTransformNode* pos = nullptr;       // translate to world position
    QSGTransformNode* symbolXf = nullptr;  // rotate(cog) * scale(world/px)
    QSGGeometryNode* predictor = nullptr;  // COG/SOG vector (world units)
    QSGTransformNode* labelXf = nullptr;   // scale(world/px) for the name
    double cog = -1.0;  // last applied -- predictor/orientation rebuilt on change
    double sog = -1.0;
    bool built = false;  // false until the first updateTarget initialises the
                         // transforms (cog/sog can't double as a sentinel:
                         // AIS reports -1 for "course/speed unavailable").
    QSGGeometryNode* sym = nullptr;  // the symbol shape (child of symbolXf)
    int shipType = -1;   // last applied; symbol/colour rebuilt when it changes
    bool dangerous = false;  // last applied; symbol/predictor recoloured on flip
  };

  TargetNode buildTarget(const AisTarget& t, QQuickWindow* window);
  void updateTarget(TargetNode& tn, const AisTarget& t, bool scale_changed);

  // A selected vessel's trail: the recorded path seeded once from SQLite
  // (provider->aisTrack) then slid live each tick (new point on the front,
  // points past the window dropped off the back). One 2px light-grey AA-line
  // (a child of the layer root, world-anchored) per enabled MMSI.
  struct Trail {
    QSGGeometryNode* node = nullptr;
    QVector<AisTrackPoint> points;  // oldest-first (geo + epoch ms)
    bool seeded = false;            // SQLite history pulled yet?
    bool dirty = true;              // geometry needs a rebuild
  };
  void updateTrails(const QList<AisTarget>& targets, qint64 now_ms);

  QSGNode* m_root = nullptr;          // returned subtree; children are pos nodes
  QHash<int, TargetNode> m_nodes;     // by MMSI
  QHash<int, Trail> m_trails;         // by MMSI (enabled trails only)
  double m_built_scale = 0.0;         // scale at last transform refresh
  bool m_config_dirty = false;        // AisConfig/DisplayConfig changed -> rebuild
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_AIS_LAYER_H_
