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
 * Implement nav_core.h.
 */

#include "nav_core.h"

#include <QColor>
#include <QString>

#include "model/ais_decoder.h"
#include "model/base_platform.h"
#include "model/navobj_db.h"
#include "model/route.h"        // pWayPointMan, RouteList
#include "model/routeman.h"     // g_pRouteMan, pRouteList, Routeman, WayPointman
#include "model/select.h"       // pSelect, pSelectAIS
#include "model/track.h"        // g_pActiveTrack

namespace ocpn::qtui {

void initNavCore() {
  // Dependency order matters (see ocpn_app.cpp): the platform provides the
  // navobj DB path + the selection radius; Select needs the platform; the
  // navobj loader needs pSelect / pRouteList / pWayPointMan / g_pRouteMan.
  if (!g_BasePlatform) g_BasePlatform = new BasePlatform();

  // Two selection registries: pSelect (routes/tracks/marks) and pSelectAIS
  // (AIS targets). Both are written unconditionally by their owners.
  if (!pSelect) pSelect = new Select();
  if (!pSelectAIS) pSelectAIS = new Select();

  if (!g_pRouteMan)
    g_pRouteMan = new Routeman(RoutePropDlgCtx(), RoutemanDlgCtx());
  if (!pRouteList) pRouteList = new RouteList();
  if (!pWayPointMan)
    pWayPointMan = new WayPointman([](QString) { return QColor(0, 0, 0); });

  if (!g_pAIS) g_pAIS = new AisDecoder(AisDecoderCallbacks());

  // Live own-ship track recorder (started/stopped with live mode).
  if (!g_pActiveTrack) g_pActiveTrack = new ActiveTrack();

  // Load persisted routes / tracks / waypoints from the navobj SQLite DB
  // (GetInstance auto-opens/creates it under the platform's data dir).
  NavObj_dB::GetInstance().LoadNavObjects();
}

}  // namespace ocpn::qtui
