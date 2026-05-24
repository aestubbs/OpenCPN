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
 * RouteListViewModel -- exposes the active NavDataProvider's routes and
 * waypoints to QML for the route & mark manager (P3.7). Each entry carries a
 * name + bounding box (routes) or position (waypoints) so the UI can show a
 * list and "zoom to" an item. Refreshes on the provider's staticChanged().
 */

#ifndef OCPN_QT_ROUTE_LIST_VIEW_MODEL_H_
#define OCPN_QT_ROUTE_LIST_VIEW_MODEL_H_

#include <QObject>
#include <QVariantList>

namespace ocpn::qtui {

class NavDataProvider;

class RouteListViewModel : public QObject {
  Q_OBJECT
  // Each element a map: routes {name, points, north, south, east, west};
  // waypoints {name, lat, lon}.
  Q_PROPERTY(QVariantList routes READ routes NOTIFY changed)
  Q_PROPERTY(QVariantList waypoints READ waypoints NOTIFY changed)
  Q_PROPERTY(int trackCount READ trackCount NOTIFY changed)

public:
  explicit RouteListViewModel(NavDataProvider* provider,
                              QObject* parent = nullptr);

  QVariantList routes() const { return m_routes; }
  QVariantList waypoints() const { return m_waypoints; }
  int trackCount() const { return m_track_count; }

Q_SIGNALS:
  void changed();

private:
  void refresh();

  NavDataProvider* m_provider;
  QVariantList m_routes;
  QVariantList m_waypoints;
  int m_track_count = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_ROUTE_LIST_VIEW_MODEL_H_
