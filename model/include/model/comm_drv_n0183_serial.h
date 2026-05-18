/***************************************************************************
 *   Copyright (C) 2022 by David Register                                  *
 *   Copyright (C) 2022 by Alec Leamas                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 *  \file
 *
 *  NMEA 0183 serial driver.
 *
 *  Qt-native rewrite (Qt/QtQuick migration, task P1.5e) -- the driver is a
 *  QObject owning a QSerialPort directly; the worker-thread `SerialIo`
 *  abstraction and the vendored libs/serial are retired. RX is event-loop
 *  driven (`QSerialPort::readyRead`). See docs/QT_MIGRATION_COMMS_PLAN.md.
 */

#ifndef COMMDRIVERN0183SERIAL_H
#define COMMDRIVERN0183SERIAL_H

#include <memory>
#include <string>
#include <vector>

#include <QObject>

#include "model/comm_buffers.h"
#include "model/comm_drv_n0183.h"
#include "model/comm_drv_stats.h"
#include "model/conn_params.h"
#include "model/garmin_protocol_mgr.h"

class QSerialPort;
class QTimer;

class CommDriverN0183Serial : public QObject,
                              public CommDriverN0183,
                              public DriverStatsProvider {
  Q_OBJECT

public:
  CommDriverN0183Serial(const ConnectionParams* params, DriverListener& l);

  ~CommDriverN0183Serial() override;

  bool Open();
  void Close();

  /** True while the serial port is open (legacy "secondary thread" query). */
  bool IsSecThreadActive() const;

  bool IsGarminThreadActive() const;
  void StopGarminUSBIOThread(bool bPause) const;

  const ConnectionParams& GetParams() const override { return m_params; }

  bool SendMessage(std::shared_ptr<const NavMsg> msg,
                   std::shared_ptr<const NavAddr> addr) override;

  DriverStats GetDriverStats() const override { return m_stats; }

  // QT_NO_KEYWORDS (task P1.5a) -- Q_SLOTS, reverted by P3.12.
private Q_SLOTS:
  void OnReadyRead();       ///< QSerialPort readyRead -- frame & forward
  void OnSerialError();     ///< QSerialPort errorOccurred -- close & retry
  void OnReconnectTimer();  ///< retry opening the port

private:
  /** (Re)open the serial port and wire its signals. */
  bool OpenSerialPort();

  /** Forward a received NMEA 0183 line to listeners after filtering. */
  void SendMessage(const std::vector<unsigned char>& msg);

  std::string m_portstring;  ///< bare device name (no "Serial:" prefix)
  unsigned m_baudrate;

  QSerialPort* m_serial;      ///< owned via QObject parenting to this driver
  QTimer* m_reconnect_timer;  ///< single-shot retry timer
  LineBuffer m_line_buffer;   ///< NMEA 0183 sentence framing

  GarminProtocolHandler* m_garmin_handler;

  ConnectionParams m_params;
  DriverListener& m_listener;

  StatsTimer m_stats_timer;
  DriverStats m_stats;
};

#endif  //  COMMDRIVERN0183SERIAL_H
