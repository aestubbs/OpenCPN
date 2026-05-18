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
#include <vector>

// Qt headers first -- parsed before wx/system headers (see P1.5a).
#include <QByteArray>
#include <QTimer>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/log.h>

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
static const std::vector<uint8_t> kInitSequence = {0x11, 0x02, 0x00};
// Probes the gateway for its device NAME / manufacturer code.
static const std::vector<uint8_t> kMfgProbe = {0x42};

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
  if (!m_tx_pgns.insert(pgn).second) return 0;  // already registered
  if (m_driver.GetDriverStats().available) {
    EnqueueTxPgn(pgn);
    ProcessNext();
  }
  // Else: recorded -- OnTransportConnected replays it once the gateway is up.
  return 0;
}

void N2kGatewayManager::Enqueue(std::vector<uint8_t> payload,
                                bool wants_response) {
  m_queue.push_back(
      {std::move(payload), wants_response, wants_response ? kMaxRetries : 0});
}

void N2kGatewayManager::EnqueueTxPgn(int pgn) {
  // Enable, commit and activate the PGN in the gateway's TX whitelist.
  const std::vector<uint8_t> enable = {
      0x47, static_cast<uint8_t>(pgn & 0xff),
      static_cast<uint8_t>((pgn >> 8) & 0xff),
      static_cast<uint8_t>((pgn >> 16) & 0xff),
      0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};
  Enqueue(enable, /*wants_response=*/true);
  Enqueue({0x01}, /*wants_response=*/true);  // commit
  Enqueue({0x4B}, /*wants_response=*/true);  // activate
}

void N2kGatewayManager::ProcessNext() {
  // Busy while a correlated request awaits its reply.
  if (m_timeout_timer->isActive() || m_queue.empty()) return;
  SendCurrent();
}

void N2kGatewayManager::SendCurrent() {
  const MgmtRequest& req = m_queue.front();
  const std::vector<uint8_t> wire = BuildMgmtMessage(req.payload);
  m_driver.WriteRaw(QByteArray(reinterpret_cast<const char*>(wire.data()),
                               static_cast<qsizetype>(wire.size())));
  if (req.wants_response) {
    m_timeout_timer->start(kResponseTimeoutMs);
  } else {
    m_queue.pop_front();
    ProcessNext();  // chain through consecutive fire-and-forget requests
  }
}

void N2kGatewayManager::OnFrame(const CommFrame& frame) {
  // Only management replies are of interest here.
  if (frame.size() < 3 || frame[0] != kMgmtReply) return;
  ExtractInfo(frame);

  if (m_queue.empty()) return;
  const MgmtRequest& req = m_queue.front();
  // The reply sub-code (frame[2]) echoes the request's first payload byte.
  if (req.wants_response && frame[2] == req.payload.front()) {
    m_timeout_timer->stop();
    m_queue.pop_front();
    ProcessNext();
  }
}

void N2kGatewayManager::OnRequestTimeout() {
  if (m_queue.empty()) return;
  MgmtRequest& req = m_queue.front();
  if (req.retries_left > 0) {
    req.retries_left--;
    SendCurrent();
    return;
  }
  // Give up on this request -- non-fatal (a YDNU-02 ignores some of these)
  // -- and move on so the rest of the handshake still runs.
  wxLogMessage("N2K gateway: no response to management request 0x%02X",
               req.payload.front());
  m_queue.pop_front();
  ProcessNext();
}

void N2kGatewayManager::ExtractInfo(const CommFrame& frame) {
  // Sub-code 0x42 reply carries the 8-byte device NAME at offset 15; the
  // manufacturer code is bits 21+ of its low word.
  if (frame[2] == 0x42 && frame.size() >= 19) {
    const uint32_t low = frame[15] | (frame[16] << 8) | (frame[17] << 16) |
                         (static_cast<uint32_t>(frame[18]) << 24);
    m_mfg_code = static_cast<int>(low) >> 21;
    wxLogMessage("N2K gateway manufacturer code: %d", m_mfg_code);
  }
}

std::vector<uint8_t> N2kGatewayManager::BuildMgmtMessage(
    const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> msg = {kN2kEscape, kN2kStartOfText, kMgmtCode};
  int byte_sum = kMgmtCode;

  const uint8_t len = static_cast<uint8_t>(payload.size());
  msg.push_back(len);
  byte_sum += len;
  for (uint8_t b : payload) {
    if (b == kN2kEscape) msg.push_back(kN2kEscape);  // escape payload bytes
    msg.push_back(b);
    byte_sum += b;
  }

  byte_sum %= 256;
  msg.push_back(static_cast<uint8_t>(byte_sum == 0 ? 0 : 256 - byte_sum));
  msg.push_back(kN2kEscape);
  msg.push_back(kN2kEndOfText);
  return msg;
}
