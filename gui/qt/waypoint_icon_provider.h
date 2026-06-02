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
 * WaypointIconProvider -- a QQuickImageProvider exposing the model's
 * WayPointman waypoint-icon catalogue to QML as image://wpicon/<iconKey>, so
 * the mark editor's icon picker (and the drawer tiles) can show the real
 * symbols. Registered on the QML engine in main.cpp (P3.7).
 */

#ifndef OCPN_QT_WAYPOINT_ICON_PROVIDER_H_
#define OCPN_QT_WAYPOINT_ICON_PROVIDER_H_

#include <QQuickImageProvider>

namespace ocpn::qtui {

class WaypointIconProvider : public QQuickImageProvider {
public:
  WaypointIconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString& id, QSize* size,
                      const QSize& requestedSize) override;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_WAYPOINT_ICON_PROVIDER_H_
