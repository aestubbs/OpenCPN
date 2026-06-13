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
 * GpsUploadController -- the Qt front end for "Send to GPS" (P3.18 tier 4,
 * wx SendToGpsDlg): upload a route or mark to a chartplotter / GPS over a
 * serial port via the model's NMEA-0183 output path
 * (SendRouteToGPS_N0183 / SendWaypointToGPS_N0183). The model upload is
 * blocking (it opens a temporary output driver); progress/status callbacks
 * surface through Q_PROPERTYs. Constructs the app's Multiplexer on first
 * use (the upload logs its output messages through it; the Qt app
 * otherwise has no instance).
 */

#ifndef OCPN_QT_GPS_UPLOAD_CONTROLLER_H_
#define OCPN_QT_GPS_UPLOAD_CONTROLLER_H_

#include <QObject>
#include <QString>

namespace ocpn::qtui {

class GpsUploadController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool sending READ sending NOTIFY sendingChanged)
  Q_PROPERTY(int progress READ progress NOTIFY progressChanged)  // 0-100
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
  explicit GpsUploadController(QObject* parent = nullptr);

  bool sending() const { return m_sending; }
  int progress() const { return m_progress; }
  QString status() const { return m_status; }

  /** Upload to the serial device at `port` (system location, e.g.
   *  /dev/cu.usbserial-0001). Blocking; returns true on success. */
  Q_INVOKABLE bool sendRoute(int routeIndex, const QString& port,
                             bool sendWaypoints);
  Q_INVOKABLE bool sendMark(const QString& guid, const QString& port);

signals:
  void sendingChanged();
  void progressChanged();
  void statusChanged();

private:
  void ensureMultiplexer();
  void finish(int result);

  bool m_sending = false;
  int m_progress = 0;
  int m_progress_range = 0;  // set_range denominator for the 0-100 mapping
  QString m_status;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_GPS_UPLOAD_CONTROLLER_H_
