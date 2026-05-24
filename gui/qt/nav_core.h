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
 * Nav-core bootstrap (P2.11). Brings up the model singletons the live nav
 * data needs, in dependency order, mirroring what gui/src/ocpn_app.cpp does
 * for the wx app -- but as the minimal set the Qt app uses. Call once at
 * startup (from main, before the QML/ChartCanvas is created) so the canvas
 * and the ModelNavDataProvider can assume g_pAIS / pRouteList / pWayPointMan
 * / g_pActiveTrack etc. exist.
 *
 * This replaces the ad-hoc `new` calls that previously squatted in the
 * ChartCanvas constructor.
 */

#ifndef OCPN_QT_NAV_CORE_H_
#define OCPN_QT_NAV_CORE_H_

namespace ocpn::qtui {

/** Construct the nav-core singletons (idempotent) and load saved routes /
 *  tracks / waypoints from the navobj SQLite DB. */
void initNavCore();

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NAV_CORE_H_
