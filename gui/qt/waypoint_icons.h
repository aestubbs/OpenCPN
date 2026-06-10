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
 * Populate the WayPointman mark-icon catalogue from the bundled SVGs. The wx
 * loader (WayPointmanGui::ProcessIcons) lives in the gui/ tree and is not
 * compiled into the Qt build, so without this pWayPointMan's icon array is
 * empty (no mark icons anywhere -- picker, free marks, route points).
 */

#ifndef OCPN_QT_WAYPOINT_ICONS_H_
#define OCPN_QT_WAYPOINT_ICONS_H_

#include <QStringList>

namespace ocpn::qtui {

/** Render the default mark icons (data/svg/markicons) into pWayPointMan's
 *  catalogue. Call once at startup after pWayPointMan exists. */
void loadDefaultWaypointIcons();

/** The picker's icon keys: one per distinct image — alias keys mapping to
 *  the same SVG (wx vocabulary, kept for persisted data) are skipped. */
QStringList pickerIconKeys();

}  // namespace ocpn::qtui

#endif  // OCPN_QT_WAYPOINT_ICONS_H_
