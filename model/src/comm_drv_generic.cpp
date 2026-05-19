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

#include <utility>

#include <QByteArray>
#include <QList>
#include <QString>
#include <QTimer>
#include <QtGlobal>  // qWarning

#include "model/comm_drv_generic.h"

using namespace std::literals::chrono_literals;

CommDriver::CommDriver(NavAddr::Bus bus, const std::string& iface,
                       const ConnectionParams& params,
                       std::unique_ptr<CommTransport> transport,
                       std::unique_ptr<Framer> framer,
                       std::unique_ptr<ProtocolDecoder> decoder,
                       DriverListener& listener,
                       std::chrono::milliseconds reconnect_interval,
                       std::chrono::milliseconds watchdog_timeout)
    : AbstractCommDriver(bus, iface),
      m_params(params),
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
  m_stats.rx_count += data.size();
  m_stats.available = true;

  // Data is flowing -- (re)arm the no-data watchdog.
  if (m_watchdog_timeout.count() > 0)
    m_watchdog_timer->start(static_cast<int>(m_watchdog_timeout.count()));

  for (const CommFrame& frame : m_framer->Feed(data)) {
    // The raw-frame tap sees every frame before decoding.
    if (m_frame_observer) m_frame_observer(frame);
    for (auto& msg : m_decoder->Decode(frame, m_source_addr))
      if (m_listener) m_listener->Notify(std::move(msg));
  }
}

bool CommDriver::WriteRaw(const QByteArray& data) {
  if (!m_transport->Write(data)) return false;
  m_stats.tx_count += data.size();
  return true;
}

void CommDriver::OnConnected() {
  m_reconnect_timer->stop();
  m_stats.available = true;
  if (m_watchdog_timeout.count() > 0)
    m_watchdog_timer->start(static_cast<int>(m_watchdog_timeout.count()));
  if (m_listener) m_listener->Notify(*this);
  Q_EMIT TransportConnected();
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
  qWarning("CommDriver %s: %s", iface.c_str(),
           message.toStdString().c_str());
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
  const QList<CommFrame> frames = m_decoder->Encode(msg, addr);
  if (frames.isEmpty()) return false;

  bool ok = true;
  for (const CommFrame& frame : frames) {
    if (m_transport->Write(frame))
      m_stats.tx_count += frame.size();
    else
      ok = false;
  }
  return ok;
}
