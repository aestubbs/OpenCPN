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
 * Implement comm_n2k_gateway_mgr.h -- the async N2K gateway handshake.
 */

#include <cstdint>
#include <utility>

#include <QByteArray>
#include <QTimer>
#include <QtGlobal>  // qWarning / qInfo

#include "model/comm_n2k_gateway_mgr.h"
#include "model/comm_drv_generic.h"

/** Actisense data code prefixing a gateway management packet (RX and TX). */
static constexpr uint8_t kMgmtCode = 0xA1;
/** Data code of a received management packet. */
static constexpr uint8_t kMgmtReply = 0xA0;

/** Response timeout for a correlated management request. */
static constexpr int kResponseTimeoutMs = 2000;
/** Re-sends attempted before a request is abandoned. */
static constexpr int kMaxRetries = 2;

// Tells an NGT-1 to clear its RX filter (forward every PGN) and switches a
// Yacht Devices YDNU-02 into N2K mode -- the same three payload bytes serve
// both. Sent fire-and-forget; the gateways do not acknowledge it.
static const QByteArray kInitSequence = QByteArray::fromHex("110200");
// Probes the gateway for its device NAME / manufacturer code.
static const QByteArray kMfgProbe = QByteArray::fromHex("42");

N2kGatewayManager::N2kGatewayManager(CommDriver& driver, QObject* parent)
    : QObject(parent), m_driver(driver), m_timeout_timer(new QTimer(this)),
      m_mfg_code(0) {
  m_timeout_timer->setSingleShot(true);
  connect(m_timeout_timer, &QTimer::timeout, this,
          &N2kGatewayManager::OnRequestTimeout);
  connect(&m_driver, &CommDriver::TransportConnected, this,
          &N2kGatewayManager::OnTransportConnected);

  // Sit beside the data pipeline: see every frame, send raw, back SetTXPGN.
  m_driver.SetFrameObserver([this](const CommFrame& f) { OnFrame(f); });
  m_driver.SetTxPgnHandler([this](int pgn) { return RequestTxPgn(pgn); });

  // A serial transport opens synchronously inside the CommDriver
  // constructor, so the TransportConnected signal may already have fired
  // before this manager existed -- run the handshake now if so.
  if (m_driver.GetDriverStats().available) OnTransportConnected();
}

void N2kGatewayManager::OnTransportConnected() {
  // A (re)connected gateway has lost any prior configuration: discard a
  // stale in-flight request and replay the whole handshake.
  m_timeout_timer->stop();
  m_queue.clear();

  Enqueue(kInitSequence, /*wants_response=*/false);
  Enqueue(kMfgProbe, /*wants_response=*/true);
  for (int pgn : m_tx_pgns) EnqueueTxPgn(pgn);

  ProcessNext();
}

int N2kGatewayManager::RequestTxPgn(int pgn) {
  if (m_tx_pgns.contains(pgn)) return 0;  // already registered
  m_tx_pgns.insert(pgn);
  if (m_driver.GetDriverStats().available) {
    EnqueueTxPgn(pgn);
    ProcessNext();
  }
  // Else: recorded -- OnTransportConnected replays it once the gateway is up.
  return 0;
}

void N2kGatewayManager::Enqueue(QByteArray payload, bool wants_response) {
  m_queue.append(
      {std::move(payload), wants_response, wants_response ? kMaxRetries : 0});
}

void N2kGatewayManager::EnqueueTxPgn(int pgn) {
  // Enable, commit and activate the PGN in the gateway's TX whitelist.
  QByteArray enable;
  enable.append(static_cast<char>(0x47));
  enable.append(static_cast<char>(pgn & 0xff));
  enable.append(static_cast<char>((pgn >> 8) & 0xff));
  enable.append(static_cast<char>((pgn >> 16) & 0xff));
  enable.append(static_cast<char>(0x00));
  enable.append(static_cast<char>(0x01));
  enable.append(4, static_cast<char>(0xFF));
  Enqueue(enable, /*wants_response=*/true);
  Enqueue(QByteArray(1, 0x01), /*wants_response=*/true);  // commit
  Enqueue(QByteArray(1, 0x4B), /*wants_response=*/true);  // activate
}

void N2kGatewayManager::ProcessNext() {
  // Busy while a correlated request awaits its reply.
  if (m_timeout_timer->isActive() || m_queue.isEmpty()) return;
  SendCurrent();
}

void N2kGatewayManager::SendCurrent() {
  const MgmtRequest& req = m_queue.first();
  const bool wants_response = req.wants_response;
  m_driver.WriteRaw(BuildMgmtMessage(req.payload));
  if (wants_response) {
    m_timeout_timer->start(kResponseTimeoutMs);
  } else {
    m_queue.removeFirst();
    ProcessNext();  // chain through consecutive fire-and-forget requests
  }
}

void N2kGatewayManager::OnFrame(const CommFrame& frame) {
  // Only management replies are of interest here.
  if (frame.size() < 3 || static_cast<uint8_t>(frame.at(0)) != kMgmtReply)
    return;
  ExtractInfo(frame);

  if (m_queue.isEmpty()) return;
  const MgmtRequest& req = m_queue.first();
  // The reply sub-code (frame[2]) echoes the request's first payload byte.
  if (req.wants_response && !req.payload.isEmpty() &&
      frame.at(2) == req.payload.at(0)) {
    m_timeout_timer->stop();
    m_queue.removeFirst();
    ProcessNext();
  }
}

void N2kGatewayManager::OnRequestTimeout() {
  if (m_queue.isEmpty()) return;
  MgmtRequest& req = m_queue.first();
  if (req.retries_left > 0) {
    req.retries_left--;
    SendCurrent();
    return;
  }
  // Give up on this request -- non-fatal (a YDNU-02 ignores some of these)
  // -- and move on so the rest of the handshake still runs.
  qWarning("N2K gateway: no response to management request 0x%02X",
           req.payload.isEmpty() ? 0 : static_cast<uint8_t>(req.payload.at(0)));
  m_queue.removeFirst();
  ProcessNext();
}

void N2kGatewayManager::ExtractInfo(const CommFrame& frame) {
  // Sub-code 0x42 reply carries the 8-byte device NAME at offset 15; the
  // manufacturer code is bits 21+ of its low word.
  if (frame.at(2) == 0x42 && frame.size() >= 19) {
    const uint32_t low =
        static_cast<uint8_t>(frame.at(15)) |
        (static_cast<uint32_t>(static_cast<uint8_t>(frame.at(16))) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(frame.at(17))) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(frame.at(18))) << 24);
    m_mfg_code = static_cast<int>(low) >> 21;
    qInfo("N2K gateway manufacturer code: %d", m_mfg_code);
  }
}

QByteArray N2kGatewayManager::BuildMgmtMessage(const QByteArray& payload) {
  QByteArray msg;
  msg.append(kN2kEscape);
  msg.append(kN2kStartOfText);
  msg.append(static_cast<char>(kMgmtCode));
  int byte_sum = kMgmtCode;

  const uint8_t len = static_cast<uint8_t>(payload.size());
  msg.append(static_cast<char>(len));
  byte_sum += len;
  for (char c : payload) {
    const uint8_t b = static_cast<uint8_t>(c);
    if (b == static_cast<uint8_t>(kN2kEscape))
      msg.append(kN2kEscape);  // escape payload bytes
    msg.append(c);
    byte_sum += b;
  }

  byte_sum %= 256;
  msg.append(static_cast<char>(byte_sum == 0 ? 0 : 256 - byte_sum));
  msg.append(kN2kEscape);
  msg.append(kN2kEndOfText);
  return msg;
}
