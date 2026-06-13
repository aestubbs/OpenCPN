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
 * NMEA 2000 serial-gateway management.
 *
 * N2kGatewayManager drives the side-channel handshake with an Actisense
 * NGT-1 / Yacht Devices YDNU-02 gateway box: it tells the box to forward
 * every PGN, probes its manufacturer code and registers the PGNs OpenCPN
 * intends to transmit.
 *
 * It is a pure event-loop state machine -- no threads, no blocking waits.
 * The legacy driver did this with wxMilliSleep/wxYield busy-loops; here
 * each response-correlated request is sent, a QTimer arms a timeout, and a
 * matching reply (delivered through the CommDriver's raw frame tap)
 * advances the queue. See docs/QT_MIGRATION_COMMS_ARCH.md (task P1.5d).
 *
 * The build defines QT_NO_KEYWORDS (task P1.5a) so slots is used.
 */

#ifndef COMM_N2K_GATEWAY_MGR_H
#define COMM_N2K_GATEWAY_MGR_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSet>

#include "model/comm_framer.h"  // CommFrame

class CommDriver;
class QTimer;

/**
 * Async management state machine for an Actisense-format N2K gateway.
 *
 * Attaches to a generic CommDriver: it sends management messages via
 * CommDriver::WriteRaw, is fed every framed frame via the driver's frame
 * tap, and (re)runs its handshake on the driver's TransportConnected
 * signal. Owned (QObject-parented) by the driver.
 */
class N2kGatewayManager : public QObject {
  Q_OBJECT

public:
  explicit N2kGatewayManager(CommDriver& driver, QObject* parent = nullptr);

  /**
   * Fed every framed frame by the driver's frame tap. Acts on 0xA0
   * management packets; everything else is ignored.
   */
  void OnFrame(const CommFrame& frame);

  /**
   * Register intent to transmit a PGN -- enqueues the gateway
   * enable/commit/activate handshake. Idempotent per PGN. Returns 0: the
   * registration is asynchronous and retried internally, so there is no
   * synchronous failure to report.
   */
  int RequestTxPgn(int pgn);

private slots:
  void OnTransportConnected();  ///< (re)run the init handshake
  void OnRequestTimeout();      ///< no response -- retry or give up

private:
  /** One queued gateway management request. */
  struct MgmtRequest {
    QByteArray payload;   ///< bytes following the 0xA1 command code
    bool wants_response;  ///< await a 0xA0 ack keyed on payload[0]
    int retries_left;
  };

  void Enqueue(QByteArray payload, bool wants_response);
  void EnqueueTxPgn(int pgn);
  void ProcessNext();
  void SendCurrent();
  void ExtractInfo(const CommFrame& frame);

  /** Wrap a management payload in the Actisense <ESC><STX>0xA1...<ESC><ETX>
   *  packet with its checksum. */
  static QByteArray BuildMgmtMessage(const QByteArray& payload);

  CommDriver& m_driver;
  QTimer* m_timeout_timer;
  QList<MgmtRequest> m_queue;
  QSet<int> m_tx_pgns;  ///< PGNs registered for TX, re-sent on reconnect
  int m_mfg_code;           ///< gateway manufacturer code, 0 until probed
};

#endif  // COMM_N2K_GATEWAY_MGR_H
