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
 * RouteDefaultsConfig -- defaults for new routes / waypoints / tracks,
 * mirroring the wx Options > Ships > Routes/Points sub-panel. A process-wide
 * singleton, persisted via ConfigStore, exposed to QML as "routeDefaults".
 *
 * Persisted today; the route/track creation paths (RouteLayer, track
 * recording) will read these as they gain styling -- see the P3.6 inventory.
 */

#ifndef OCPN_QT_ROUTE_DEFAULTS_CONFIG_H_
#define OCPN_QT_ROUTE_DEFAULTS_CONFIG_H_

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace ocpn::qtui {

class RouteDefaultsConfig : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  // New-route line: colour (hex "#rrggbb") + style (0 solid .. 4 dash-dot).
  Q_PROPERTY(QColor routeColor READ routeColor WRITE setRouteColor NOTIFY
                 changed)
  Q_PROPERTY(int routeStyle READ routeStyle WRITE setRouteStyle NOTIFY changed)
  Q_PROPERTY(bool persistActiveRoute READ persistActiveRoute WRITE
                 setPersistActiveRoute NOTIFY changed)
  Q_PROPERTY(QString waypointIcon READ waypointIcon WRITE setWaypointIcon
                 NOTIFY changed)
  Q_PROPERTY(QString routepointIcon READ routepointIcon WRITE setRoutepointIcon
                 NOTIFY changed)
  Q_PROPERTY(double arrivalCircleNm READ arrivalCircleNm WRITE
                 setArrivalCircleNm NOTIFY changed)
  Q_PROPERTY(bool autoAnchorMark READ autoAnchorMark WRITE setAutoAnchorMark
                 NOTIFY changed)
  Q_PROPERTY(int scaminMin READ scaminMin WRITE setScaminMin NOTIFY changed)
  Q_PROPERTY(int scaminMax READ scaminMax WRITE setScaminMax NOTIFY changed)

  // Track auto-create daily at midnight: 0 off, 1 computer, 2 UTC, 3 LMT.
  Q_PROPERTY(int trackAutoDaily READ trackAutoDaily WRITE setTrackAutoDaily
                 NOTIFY changed)
  Q_PROPERTY(bool trackHighlight READ trackHighlight WRITE setTrackHighlight
                 NOTIFY changed)
  Q_PROPERTY(QColor trackColor READ trackColor WRITE setTrackColor NOTIFY
                 changed)
  // 0 = high (every fix), 1 = medium, 2 = low.
  Q_PROPERTY(int trackingPrecision READ trackingPrecision WRITE
                 setTrackingPrecision NOTIFY changed)
  // Ask before deleting a route / track / mark (wx g_bConfirmObjectDelete).
  Q_PROPERTY(bool confirmObjectDelete READ confirmObjectDelete WRITE
                 setConfirmObjectDelete NOTIFY changed)
  // Only advance the active waypoint INSIDE the arrival circle -- never by
  // passing abeam (wx g_bAdvanceRouteWaypointOnArrivalOnly).
  Q_PROPERTY(bool advanceOnArrivalOnly READ advanceOnArrivalOnly WRITE
                 setAdvanceOnArrivalOnly NOTIFY changed)

public:
  static RouteDefaultsConfig& instance();
  static RouteDefaultsConfig* create(QQmlEngine*, QJSEngine*) {
    RouteDefaultsConfig* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  QColor routeColor() const { return m_route_color; }
  void setRouteColor(const QColor& v);
  int routeStyle() const { return m_route_style; }
  void setRouteStyle(int v);
  bool persistActiveRoute() const { return m_persist_active; }
  void setPersistActiveRoute(bool v);
  bool autoAnchorMark() const { return m_auto_anchor_mark; }
  void setAutoAnchorMark(bool v);
  QString waypointIcon() const { return m_waypoint_icon; }
  void setWaypointIcon(const QString& v);
  QString routepointIcon() const { return m_routepoint_icon; }
  void setRoutepointIcon(const QString& v);
  double arrivalCircleNm() const { return m_arrival_nm; }
  void setArrivalCircleNm(double v);
  int scaminMin() const { return m_scamin_min; }
  void setScaminMin(int v);
  int scaminMax() const { return m_scamin_max; }
  void setScaminMax(int v);

  int trackAutoDaily() const { return m_track_auto_daily; }
  void setTrackAutoDaily(int v);
  bool trackHighlight() const { return m_track_highlight; }
  void setTrackHighlight(bool v);
  QColor trackColor() const { return m_track_color; }
  void setTrackColor(const QColor& v);
  int trackingPrecision() const { return m_tracking_precision; }
  void setTrackingPrecision(int v);
  bool confirmObjectDelete() const { return m_confirm_delete; }
  void setConfirmObjectDelete(bool v);
  bool advanceOnArrivalOnly() const { return m_advance_arrival_only; }
  void setAdvanceOnArrivalOnly(bool v);

signals:
  void changed();

private:
  // Constructor is PRIVATE so the QML engine cannot default-
  // construct a second instance: Qt picks the Constructor mode
  // over the create() factory whenever the type is default-
  // constructible (qqmlprivate.h singletonConstructionMode),
  // which split every singleton into a C++ brain and a QML
  // brain. Private ctor => Factory mode => create() => the
  // ONE shared instance().
  RouteDefaultsConfig();  // loads from the config store


  QColor m_route_color{58, 64, 70};  // graphite (pencil-on-chart default)
  int m_route_style = 0;
  bool m_auto_anchor_mark = false;
  bool m_persist_active = true;
  QString m_waypoint_icon{QStringLiteral("diamond")};
  QString m_routepoint_icon{QStringLiteral("xmblue")};
  double m_arrival_nm = 0.05;
  int m_scamin_min = 0;
  int m_scamin_max = 0;

  int m_track_auto_daily = 0;
  bool m_track_highlight = false;
  QColor m_track_color{120, 72, 40};  // brown (dashed breadcrumb default)
  int m_tracking_precision = 0;
  bool m_confirm_delete = true;  // wx default
  bool m_advance_arrival_only = false;  // wx default
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_ROUTE_DEFAULTS_CONFIG_H_
