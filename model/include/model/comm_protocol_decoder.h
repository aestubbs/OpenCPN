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
 * Comms framework -- the protocol conversion layer.
 *
 * A ProtocolDecoder turns wire frames (produced by a Framer) into decoded
 * NavMsg objects for the message bus, and -- for transmit -- NavMsg objects
 * back into wire frames. It may hold protocol state (NMEA 2000 fast-message
 * reassembly, for example).
 *
 * Like Framer, a decoder is deliberately pure -- no I/O, no Qt event loop --
 * so it is unit-testable against captured frame logs with no hardware. See
 * docs/QT_MIGRATION_COMMS_ARCH.md (task P1.5i).
 */

#ifndef COMM_PROTOCOL_DECODER_H
#define COMM_PROTOCOL_DECODER_H

#include <memory>
#include <vector>

#include "model/comm_framer.h"
#include "model/comm_navmsg.h"

/**
 * Converts between wire frames and decoded NavMsg objects.
 *
 * One subclass per wire protocol: Nmea0183Decoder, N2kDecoder,
 * SignalKDecoder. The medium is irrelevant here -- a decoder works the
 * same whether its frames arrived over serial, TCP or a gateway.
 */
class ProtocolDecoder {
public:
  virtual ~ProtocolDecoder() = default;

  /**
   * Decode one wire frame into zero or more NavMsg objects.
   *
   * Zero messages is normal -- an incomplete NMEA 2000 fast-packet
   * fragment, or a frame that fails validation. Some protocols yield
   * several messages from one frame.
   *
   * @param frame  One complete wire frame from the Framer.
   * @param src    Source address (bus + interface) the driver assigns to
   *               received messages; a decoder may refine it (e.g. an N2K
   *               decoder fills in the per-message node address).
   */
  virtual std::vector<std::shared_ptr<const NavMsg>> Decode(
      const CommFrame& frame, const std::shared_ptr<const NavAddr>& src) = 0;

  /**
   * Encode a NavMsg for transmission into zero or more wire frames.
   *
   * An empty result means this decoder cannot encode the message (wrong
   * protocol, or a receive-only protocol).
   */
  virtual std::vector<CommFrame> Encode(
      const std::shared_ptr<const NavMsg>& msg) = 0;
};

#endif  // COMM_PROTOCOL_DECODER_H
