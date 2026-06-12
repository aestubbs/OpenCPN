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
 * Comms framework -- the SignalK protocol decoder (task P1.5c).
 *
 * One frame = one JSON document from the server's websocket stream. The
 * decoder validates the JSON, tracks the stream's "self" vessel identity
 * and current context (stateful -- the server announces them once in its
 * hello message), and wraps each document in a SignalkMsg for the bus.
 * Ported from the legacy CommDriverSignalKNet::HandleSkSentence.
 *
 * SignalK is receive-only here, as in the legacy driver: Encode() yields
 * nothing.
 */

#ifndef COMM_SIGNALK_DECODER_H
#define COMM_SIGNALK_DECODER_H

#include <string>

#include "model/comm_protocol_decoder.h"

class SignalKDecoder : public ProtocolDecoder {
public:
  /**
   * @param iface The connection's interface string ("host:port"), tagged
   *              onto every produced SignalkMsg.
   */
  explicit SignalKDecoder(std::string iface) : m_iface(std::move(iface)) {}

  QList<std::shared_ptr<const NavMsg>> Decode(
      const CommFrame& frame,
      const std::shared_ptr<const NavAddr>& src) override;

  QList<CommFrame> Encode(const std::shared_ptr<const NavMsg>&,
                          const std::shared_ptr<const NavAddr>&) override {
    return {};  // receive-only protocol
  }

  /** The stream's announced self vessel ("vessels.<id>"); test seam. */
  const std::string& self() const { return m_self; }

  /** The stream's current context; test seam. */
  const std::string& context() const { return m_context; }

private:
  const std::string m_iface;
  std::string m_self;
  std::string m_context;
};

#endif  // COMM_SIGNALK_DECODER_H
