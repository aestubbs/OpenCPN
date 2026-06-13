/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "gps_upload_controller.h"

#include <QCoreApplication>

#include "model/comm_n0183_output.h"
#include "model/gui_vars.h"  // g_b_legacy_input_filter_behaviour
#include "model/multiplexer.h"
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"  // pRouteList, pWayPointMan
#include "model/wx_qt_string.h"

namespace ocpn::qtui {

GpsUploadController::GpsUploadController(QObject* parent) : QObject(parent) {}

void GpsUploadController::ensureMultiplexer() {
  // The upload logs its sentences through the app Multiplexer; the Qt app
  // has no other use for one yet, so create it lazily (no-op log callbacks).
  if (!g_pMUX)
    g_pMUX = new Multiplexer(MuxLogCallbacks(),
                             g_b_legacy_input_filter_behaviour);
}

void GpsUploadController::finish(int result) {
  switch (result) {
    case 0:
      m_status = tr("Transmitted ✓");
      break;
    case ERR_GARMIN_INITIALIZE:
      m_status = tr("Upload failed — Garmin GPS not connected");
      break;
    case ERR_GPS_DRIVER_NOT_AVAILAIBLE:
      m_status = tr("Upload failed — GPS driver not available");
      break;
    default:
      m_status = tr("Upload failed (code %1) — check the logfile").arg(result);
      break;
  }
  m_sending = false;
  emit sendingChanged();
  emit statusChanged();
}

bool GpsUploadController::sendRoute(int routeIndex, const QString& port,
                                    bool sendWaypoints) {
  if (m_sending || port.isEmpty() || !pRouteList || routeIndex < 0 ||
      routeIndex >= static_cast<int>(pRouteList->size()))
    return false;
  Route* r = (*pRouteList)[routeIndex];
  if (!r) return false;
  ensureMultiplexer();
  m_sending = true;
  m_progress = 0;
  m_status = tr("Sending…");
  emit sendingChanged();
  emit statusChanged();

  N0183DlgCtx ctx;
  ctx.set_range = [this](int range) {
    m_progress_range = range;  // see note below
  };
  // (set_range stores the denominator; set_value converts to 0-100.)
  ctx.set_value = [this](int v) {
    m_progress = m_progress_range > 0 ? (100 * v) / m_progress_range : 0;
    emit progressChanged();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  };
  ctx.confirm_overwrite = []() { return true; };

  const wxString com =
      QString_to_wxString(QStringLiteral("Serial:") + port);
  const int result = SendRouteToGPS_N0183(r, com, sendWaypoints, *g_pMUX, ctx);
  finish(result);
  return result == 0;
}

bool GpsUploadController::sendMark(const QString& guid, const QString& port) {
  if (m_sending || port.isEmpty()) return false;
  RoutePoint* wp =
      pWayPointMan ? pWayPointMan->FindRoutePointByGUID(guid) : nullptr;
  if (!wp) return false;
  ensureMultiplexer();
  m_sending = true;
  m_progress = 0;
  m_status = tr("Sending…");
  emit sendingChanged();
  emit statusChanged();

  N0183DlgCtx ctx;
  ctx.confirm_overwrite = []() { return true; };
  const wxString com =
      QString_to_wxString(QStringLiteral("Serial:") + port);
  const int result = SendWaypointToGPS_N0183(wp, com, *g_pMUX, ctx);
  finish(result);
  return result == 0;
}

}  // namespace ocpn::qtui
