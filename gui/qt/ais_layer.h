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
           QObject* parent = nullptr)
      : NavLayer(provider, viewport, parent) {
    setOwner(QStringLiteral("core.ais"));
    connectData(&NavDataProvider::dynamicChanged);
  }

  QString id() const override { return QStringLiteral("core.ais"); }
  QString name() const override { return QStringLiteral("AIS"); }

  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

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
  };

  TargetNode buildTarget(const AisTarget& t, QQuickWindow* window);
  void updateTarget(TargetNode& tn, const AisTarget& t, bool scale_changed);

  QSGNode* m_root = nullptr;          // returned subtree; children are pos nodes
  QHash<int, TargetNode> m_nodes;     // by MMSI
  double m_built_scale = 0.0;         // scale at last transform refresh
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_AIS_LAYER_H_
