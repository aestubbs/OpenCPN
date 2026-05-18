/***************************************************************************
 *   Copyright (C) 2022 David Register                                     *
 *   Copyright (C) 2022 Alec Leamas                                        *
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
 *  Implement comm_drv_n0183_serial.h -- Qt-native NMEA 0183 serial driver.
 */

#include <memory>
#include <string>
#include <vector>

// Qt headers first -- parsed before wx/system headers (see P1.5a).
#include <QByteArray>
#include <QSerialPort>
#include <QString>
#include <QTimer>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/log.h>
#include <wx/string.h>

#include "model/comm_drv_n0183_serial.h"
#include "model/comm_buffers.h"
#include "model/comm_drv_stats.h"
#include "model/logger.h"

using namespace std::literals::chrono_literals;

/** Strip a leading "Serial:" prefix from a connection-string port name. */
static std::string NormalizePort(const std::string& port) {
  static const std::string kPrefix = "Serial:";
  return port.rfind(kPrefix, 0) == 0 ? port.substr(kPrefix.size()) : port;
}

CommDriverN0183Serial::CommDriverN0183Serial(const ConnectionParams* params,
                                             DriverListener& listener)
    : CommDriverN0183(NavAddr::Bus::N0183, params->GetStrippedDSPort()),
      m_portstring(NormalizePort(params->GetDSPort().ToStdString())),
      m_baudrate(params->Baudrate),
      m_serial(nullptr),
      m_reconnect_timer(nullptr),
      m_garmin_handler(nullptr),
      m_params(*params),
      m_listener(listener),
      m_stats_timer(*this, 2s) {
  m_stats.driver_bus = NavAddr::Bus::N0183;
  m_stats.driver_iface = params->GetStrippedDSPort();
  m_stats.available = false;

  this->attributes["commPort"] = params->Port.ToStdString();
  this->attributes["userComment"] = params->UserComment.ToStdString();
  this->attributes["ioDirection"] = DsPortTypeToString(params->IOSelect);

  // Single-shot retry timer; restarted on each failed open (replaces the old
  // worker-thread reconnection loop).
  m_reconnect_timer = new QTimer(this);
  m_reconnect_timer->setSingleShot(true);
  connect(m_reconnect_timer, &QTimer::timeout, this,
          &CommDriverN0183Serial::OnReconnectTimer);

  Open();
}

CommDriverN0183Serial::~CommDriverN0183Serial() { Close(); }

bool CommDriverN0183Serial::Open() {
  wxString comx = m_params.GetDSPort().AfterFirst(':');  // strip "Serial:"
  if (comx.IsEmpty()) return false;

  wxString port_uc = m_params.GetDSPort().Upper();
  auto send_func = [this](const std::vector<unsigned char>& v) {
    SendMessage(v);
  };

  if ((port_uc.Find("USB") != wxNOT_FOUND) &&
      (port_uc.Find("GARMIN") != wxNOT_FOUND)) {
    m_garmin_handler = new GarminProtocolHandler(comx, send_func, true);
  } else if (m_params.Garmin) {
    m_garmin_handler = new GarminProtocolHandler(comx, send_func, false);
  } else {
    OpenSerialPort();
  }
  return true;
}

bool CommDriverN0183Serial::OpenSerialPort() {
  if (!m_serial) {
    m_serial = new QSerialPort(this);
    connect(m_serial, &QSerialPort::readyRead, this,
            &CommDriverN0183Serial::OnReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this,
            &CommDriverN0183Serial::OnSerialError);
  }
  m_serial->setPortName(QString::fromStdString(m_portstring));
  m_serial->setBaudRate(static_cast<qint32>(m_baudrate));
  m_serial->setDataBits(QSerialPort::Data8);
  m_serial->setParity(QSerialPort::NoParity);
  m_serial->setStopBits(QSerialPort::OneStop);
  m_serial->setFlowControl(QSerialPort::NoFlowControl);

  bool ok = m_serial->open(QIODevice::ReadWrite);
  m_stats.available = ok;
  if (!ok) {
    wxLogMessage(wxString::Format("NMEA input device open failed: %s",
                                  m_portstring.c_str()));
    if (!m_reconnect_timer->isActive())
      m_reconnect_timer->start(2500);  // retry every 2.5 s
  }
  return ok;
}

void CommDriverN0183Serial::Close() {
  wxLogMessage(
      wxString::Format("Closing NMEA Driver %s", m_portstring.c_str()));

  m_stats_timer.Stop();
  if (m_reconnect_timer) m_reconnect_timer->stop();
  if (m_serial && m_serial->isOpen()) m_serial->close();
  m_stats.available = false;

  //  Kill off the Garmin handler, if alive
  if (m_garmin_handler) {
    m_garmin_handler->Close();
    delete m_garmin_handler;
    m_garmin_handler = nullptr;
  }
}

void CommDriverN0183Serial::OnReadyRead() {
  if (!m_serial) return;
  const QByteArray chunk = m_serial->readAll();
  for (char b : chunk) m_line_buffer.Put(static_cast<uint8_t>(b));

  while (m_line_buffer.HasLine()) {
    std::vector<uint8_t> line = m_line_buffer.GetLine();
    m_stats.rx_count += line.size();
    SendMessage(line);
  }
}

void CommDriverN0183Serial::OnSerialError() {
  if (!m_serial) return;
  const QSerialPort::SerialPortError err = m_serial->error();
  if (err == QSerialPort::NoError) return;

  // A device that disappears (USB adaptor unplugged, etc.) -- close it and
  // let the reconnect timer retry, mirroring the old worker-thread retry loop.
  if (err == QSerialPort::ResourceError ||
      err == QSerialPort::PermissionError ||
      err == QSerialPort::DeviceNotFoundError ||
      err == QSerialPort::OpenError) {
    m_stats.available = false;
    if (m_serial->isOpen()) m_serial->close();
    if (!m_reconnect_timer->isActive()) m_reconnect_timer->start(2500);
  }
  m_serial->clearError();
}

void CommDriverN0183Serial::OnReconnectTimer() {
  if (m_serial && m_serial->isOpen()) return;
  OpenSerialPort();
}

bool CommDriverN0183Serial::IsSecThreadActive() const {
  return m_serial && m_serial->isOpen();
}

bool CommDriverN0183Serial::IsGarminThreadActive() const {
  if (m_garmin_handler) {
    // TODO expand for serial
#ifdef __WXMSW__
    return m_garmin_handler->m_usb_handle != INVALID_HANDLE_VALUE;
#endif
  }
  return false;
}

void CommDriverN0183Serial::StopGarminUSBIOThread(bool b_pause) const {
  if (m_garmin_handler) m_garmin_handler->StopIOThread(b_pause);
}

bool CommDriverN0183Serial::SendMessage(std::shared_ptr<const NavMsg> msg,
                                        std::shared_ptr<const NavAddr> addr) {
  auto msg_0183 = std::dynamic_pointer_cast<const Nmea0183Msg>(msg);
  if (!msg_0183) return false;
  std::string sentence = msg_0183->payload;

  // Same guard the old SerialIo::SetOutMsg applied.
  if (sentence.size() < 6 || (sentence[0] != '$' && sentence[0] != '!'))
    return false;
  if (!m_serial || !m_serial->isOpen()) return false;

  if (sentence.size() < 2 ||
      sentence.compare(sentence.size() - 2, 2, "\r\n") != 0)
    sentence += "\r\n";

  qint64 written =
      m_serial->write(sentence.data(), static_cast<qint64>(sentence.size()));
  if (written < 0) return false;
  m_serial->flush();
  m_stats.tx_count += sentence.size();
  return true;
}

void CommDriverN0183Serial::SendMessage(const std::vector<unsigned char>& msg) {
  // Output-only ports (commonly the "Send to GPS" function) do not feed the
  // listener.
  if (m_params.IOSelect == DS_TYPE_OUTPUT) return;

  SendToListener({msg.begin(), msg.end()}, m_listener, m_params);
}
