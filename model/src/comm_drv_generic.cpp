/***************************************************************************
 *   Copyright (C) 2026 OpenCPN Developers                                  *
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
 * \file
 *
 * Implement comm_drv_generic.h -- the generic communication driver.
 */

#include <cstdint>
#include <utility>
#include <vector>

// Qt headers first -- parsed before wx/system headers (see P1.5a).
#include <QByteArray>
#include <QString>
#include <QTimer>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/log.h>

#include "model/comm_drv_generic.h"

using namespace std::literals::chrono_literals;

CommDriver::CommDriver(NavAddr::Bus bus, const std::string& iface,
                       std::unique_ptr<CommTransport> transport,
                       std::unique_ptr<Framer> framer,
                       std::unique_ptr<ProtocolDecoder> decoder,
                       DriverListener& listener,
                       std::chrono::milliseconds reconnect_interval,
                       std::chrono::milliseconds watchdog_timeout)
    : AbstractCommDriver(bus, iface),
      m_transport(std::move(transport)),
      m_framer(std::move(framer)),
      m_decoder(std::move(decoder)),
      m_listener(&listener),
      m_source_addr(std::make_shared<const NavAddr>(bus, iface)),
      m_reconnect_timer(new QTimer(this)),
      m_watchdog_timer(new QTimer(this)),
      m_reconnect_interval(reconnect_interval),
      m_watchdog_timeout(watchdog_timeout),
      m_stats_timer(*this, 2s) {
  m_stats.driver_bus = bus;
  m_stats.driver_iface = iface;
  m_stats.available = false;

  m_reconnect_timer->setSingleShot(true);
  connect(m_reconnect_timer, &QTimer::timeout, this,
          &CommDriver::OnReconnectTimer);
  m_watchdog_timer->setSingleShot(true);
  connect(m_watchdog_timer, &QTimer::timeout, this,
          &CommDriver::OnWatchdogTimer);

  // Wire the transport's media events into the driver before opening so a
  // synchronous Connected() (serial, UDP bind) is not missed.
  connect(m_transport.get(), &CommTransport::DataReceived, this,
          &CommDriver::OnDataReceived);
  connect(m_transport.get(), &CommTransport::Connected, this,
          &CommDriver::OnConnected);
  connect(m_transport.get(), &CommTransport::Disconnected, this,
          &CommDriver::OnDisconnected);
  connect(m_transport.get(), &CommTransport::ErrorOccurred, this,
          &CommDriver::OnError);

  Open();
}

CommDriver::~CommDriver() { Close(); }

void CommDriver::Open() {
  if (!m_transport->Open()) {
    // A synchronous open failure (port absent). Sockets connect
    // asynchronously and report failure later via ErrorOccurred.
    if (!m_reconnect_timer->isActive())
      m_reconnect_timer->start(static_cast<int>(m_reconnect_interval.count()));
  }
}

void CommDriver::Close() {
  m_stats_timer.Stop();
  m_reconnect_timer->stop();
  m_watchdog_timer->stop();
  m_transport->Close();
  m_stats.available = false;
}

void CommDriver::OnDataReceived(const QByteArray& data) {
  const auto* begin = reinterpret_cast<const uint8_t*>(data.constData());
  const std::vector<uint8_t> bytes(begin, begin + data.size());
  m_stats.rx_count += bytes.size();
  m_stats.available = true;

  // Data is flowing -- (re)arm the no-data watchdog.
  if (m_watchdog_timeout.count() > 0)
    m_watchdog_timer->start(static_cast<int>(m_watchdog_timeout.count()));

  for (const CommFrame& frame : m_framer->Feed(bytes)) {
    for (auto& msg : m_decoder->Decode(frame, m_source_addr))
      if (m_listener) m_listener->Notify(std::move(msg));
  }
}

void CommDriver::OnConnected() {
  m_reconnect_timer->stop();
  m_stats.available = true;
  if (m_watchdog_timeout.count() > 0)
    m_watchdog_timer->start(static_cast<int>(m_watchdog_timeout.count()));
  if (m_listener) m_listener->Notify(*this);
}

void CommDriver::OnDisconnected() {
  m_watchdog_timer->stop();
  m_stats.available = false;
  if (m_listener) m_listener->Notify(*this);
  // Schedule a single retry; OnReconnectTimer re-arms itself if it fails.
  if (!m_reconnect_timer->isActive())
    m_reconnect_timer->start(static_cast<int>(m_reconnect_interval.count()));
}

void CommDriver::OnError(const QString& message) {
  m_stats.error_count++;
  wxLogMessage(wxString::Format("CommDriver %s: %s", iface.c_str(),
                                message.toStdString().c_str()));
}

void CommDriver::OnReconnectTimer() {
  if (m_transport->IsOpen()) return;
  Open();
}

void CommDriver::OnWatchdogTimer() {
  // No data for the watchdog period -- the source is silent. The transport
  // is left open; a genuine disconnect arrives separately as Disconnected().
  m_stats.available = false;
}

bool CommDriver::SendMessage(std::shared_ptr<const NavMsg> msg,
                             std::shared_ptr<const NavAddr> addr) {
  const std::vector<CommFrame> frames = m_decoder->Encode(msg);
  if (frames.empty()) return false;

  bool ok = true;
  for (const CommFrame& frame : frames) {
    const QByteArray ba(reinterpret_cast<const char*>(frame.data()),
                        static_cast<qsizetype>(frame.size()));
    if (m_transport->Write(ba))
      m_stats.tx_count += frame.size();
    else
      ok = false;
  }
  return ok;
}
