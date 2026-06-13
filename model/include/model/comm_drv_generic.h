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
 * Comms framework -- the generic communication driver.
 *
 * CommDriver is the lifecycle-and-glue layer: it owns a CommTransport, a
 * Framer and a ProtocolDecoder and provides, written once, the wiring
 *
 *   transport.DataReceived -> framer.Feed -> decoder.Decode -> listener
 *
 * plus reconnect, the no-data watchdog and DriverStats. A concrete driver
 * is now just a pairing built from these three parts, so the legacy NxM
 * grid of bespoke driver classes collapses. See
 * docs/QT_MIGRATION_COMMS_ARCH.md (task P1.5i).
 *
 * The build defines QT_NO_KEYWORDS (task P1.5a) so slots is used instead
 * of the slots macro.
 */

#ifndef COMM_DRV_GENERIC_H
#define COMM_DRV_GENERIC_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include <QObject>

#include "model/comm_driver.h"
#include "model/comm_drv_stats.h"
#include "model/comm_framer.h"
#include "model/comm_protocol_decoder.h"
#include "model/comm_transport.h"
#include "model/conn_params.h"

class QTimer;

/**
 * The generic communication driver: transport + framer + decoder + glue.
 *
 * Subclasses AbstractCommDriver so the device registry, DriverListener and
 * plugin-ABI boundary are unchanged; the concrete behaviour comes entirely
 * from the three parts handed to the constructor.
 */
class CommDriver : public QObject,
                   public AbstractCommDriver,
                   public DriverStatsProvider,
                   public ConnectionParamsProvider {
  Q_OBJECT

public:
  /**
   * @param bus       The driver's bus (sets the registry key).
   * @param iface     The driver's interface string (sets the registry key).
   * @param params    Connection parameters this driver was built from.
   * @param transport Media adaptor; ownership taken.
   * @param framer    Frame boundary finder; ownership taken.
   * @param decoder   Protocol conversion layer; ownership taken.
   * @param listener  Destination for decoded messages.
   * @param reconnect_interval  Delay before retrying a lost connection.
   * @param watchdog_timeout    Silence after which the driver is marked
   *                            unavailable; zero disables the watchdog.
   */
  CommDriver(NavAddr::Bus bus, const std::string& iface,
             const ConnectionParams& params,
             std::unique_ptr<CommTransport> transport,
             std::unique_ptr<Framer> framer,
             std::unique_ptr<ProtocolDecoder> decoder, DriverListener& listener,
             std::chrono::milliseconds reconnect_interval =
                 std::chrono::milliseconds(2500),
             std::chrono::milliseconds watchdog_timeout =
                 std::chrono::milliseconds(5000));

  ~CommDriver() override;

  /** Open the transport and begin streaming. */
  void Open();

  /** Close the transport and stop all timers. */
  void Close();

  bool SendMessage(std::shared_ptr<const NavMsg> msg,
                   std::shared_ptr<const NavAddr> addr) override;

  void SetListener(DriverListener& l) override { m_listener = &l; }

  DriverStats GetDriverStats() const override { return m_stats; }

  const ConnectionParams& GetConnectionParams() const override {
    return m_params;
  }

  /**
   * Install a tap called with every complete frame the framer produces,
   * before decoding. Used by side-channel logic that needs the raw frame
   * stream -- e.g. the N2K gateway manager intercepting management
   * packets. Pass an empty std::function to remove the tap.
   */
  void SetFrameObserver(std::function<void(const CommFrame&)> observer) {
    m_frame_observer = std::move(observer);
  }

  /**
   * Write raw bytes straight to the transport, bypassing the decoder. For
   * side-channel traffic such as gateway management messages that is not a
   * NavMsg. Returns false if nothing could be sent.
   */
  bool WriteRaw(const QByteArray& data);

  /**
   * Install the handler backing SetTXPGN(). Side-channel logic (the N2K
   * gateway manager) registers a callback; without one SetTXPGN is a
   * no-op, as for any non-N2K driver.
   */
  void SetTxPgnHandler(std::function<int(int)> handler) {
    m_tx_pgn_handler = std::move(handler);
  }

  int SetTXPGN(int pgn) override {
    return m_tx_pgn_handler ? m_tx_pgn_handler(pgn) : 0;
  }

signals:
  /** Emitted when the transport becomes ready (see CommTransport::Connected).
   *  Lets side-channel logic (re)run a handshake on every (re)connect. */
  void TransportConnected();

private slots:
  void OnDataReceived(const QByteArray& data);  ///< frame, decode, forward
  void OnConnected();                           ///< transport became ready
  void OnDisconnected();                        ///< transport lost; schedule retry
  void OnError(const QString& message);         ///< count and log
  void OnReconnectTimer();                      ///< retry opening the transport
  void OnWatchdogTimer();                       ///< no data -> mark unavailable

private:
  ConnectionParams m_params;
  std::unique_ptr<CommTransport> m_transport;
  std::unique_ptr<Framer> m_framer;
  std::function<void(const CommFrame&)> m_frame_observer;
  std::function<int(int)> m_tx_pgn_handler;
  std::unique_ptr<ProtocolDecoder> m_decoder;
  DriverListener* m_listener;

  /** Source address tagged onto every received message. */
  std::shared_ptr<const NavAddr> m_source_addr;

  QTimer* m_reconnect_timer;  ///< single-shot retry, owned by QObject parenting
  QTimer* m_watchdog_timer;   ///< single-shot no-data watchdog
  const std::chrono::milliseconds m_reconnect_interval;
  const std::chrono::milliseconds m_watchdog_timeout;

  StatsTimer m_stats_timer;
  DriverStats m_stats;
};

#endif  // COMM_DRV_GENERIC_H
