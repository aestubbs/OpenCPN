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
 * OwnShipLayer -- world-anchored Layer drawing the own-ship marker, its
 * COG/SOG predictor vector, and port/starboard laylines (P2.11).
 *
 * Retained scene graph, like AisLayer: the symbol + vector + laylines are
 * built once; each update mutates only the position transform (every tick)
 * and the orientation/size transforms + vector geometry (on course/zoom
 * change). The whole group is hidden via an opacity node while the own-ship
 * fix is invalid.
 */

#ifndef OCPN_QT_OWN_SHIP_LAYER_H_
#define OCPN_QT_OWN_SHIP_LAYER_H_

#include "nav_layer.h"

QT_BEGIN_NAMESPACE
class QSGNode;
class QSGGeometryNode;
class QSGOpacityNode;
class QSGTransformNode;
QT_END_NAMESPACE

namespace ocpn::qtui {

class OwnShipLayer : public NavLayer {
  Q_OBJECT

public:
  OwnShipLayer(NavDataProvider* provider, const Viewport* viewport,
               QObject* parent = nullptr);

  QString id() const override { return QStringLiteral("core.ownship"); }
  QString name() const override { return QStringLiteral("Own ship"); }

  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

private:
  void buildOnce();
  // (Re)build the concentric range-ring circles centred on the ship, sized
  // from OwnShipConfig at the given latitude. No-op group when rings are off.
  void rebuildRings(double lat);
  // Set the symbol child of m_symbolXf: the fixed-size marker triangle, or a
  // to-scale hull (world units, from OwnShipConfig LOA/beam/GPS-offset at lat).
  void makeTriangleSymbol();
  void makeHullSymbol(double lat);

  QSGNode* m_root = nullptr;
  QSGOpacityNode* m_opacity = nullptr;   // hide while fix invalid
  QSGTransformNode* m_pos = nullptr;     // translate to world position
  QSGTransformNode* m_symbolXf = nullptr;// rotate(hdg/cog) [* scale(world/px)]
  QSGGeometryNode* m_symbol = nullptr;   // marker triangle OR real-scale hull
  QSGGeometryNode* m_predictor = nullptr;// COG/SOG vector (world units)
  QSGGeometryNode* m_laylines = nullptr; // port + starboard laylines
  QSGNode* m_rings = nullptr;            // range-ring group (child of m_pos)
  double m_rings_lat = 999.0;            // latitude the rings were sized for
  bool m_rings_dirty = true;             // OwnShipConfig changed -> resize
  bool m_symbol_is_hull = false;         // current symbol mode
  bool m_symbol_dirty = true;            // OwnShipConfig changed -> rebuild
  double m_symbol_lat = 999.0;           // latitude the hull was sized for
  double m_cog = -1.0;
  double m_sog = -1.0;
  double m_built_scale = 0.0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_OWN_SHIP_LAYER_H_
