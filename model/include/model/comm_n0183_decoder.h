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
 * Comms framework -- NMEA 0183 protocol decoder.
 *
 * Turns terminator-delimited NMEA 0183 frames (from a LineFramer) into
 * Nmea0183Msg objects, applying the parsing rules of CommDriverN0183:
 * v4-tag stripping, garbage / bad-checksum classification and the
 * connection's input sentence filter. Encode() is the transmit path.
 *
 * No I/O and no Qt -- so it is unit-testable against captured sentence
 * logs with no hardware (task P1.5b, built on the P1.5i framework).
 */

#ifndef COMM_N0183_DECODER_H
#define COMM_N0183_DECODER_H

#include <memory>
#include <vector>

#include "model/comm_protocol_decoder.h"
#include "model/conn_params.h"

/**
 * ProtocolDecoder for the NMEA 0183 wire protocol.
 *
 * Holds a copy of the connection's ConnectionParams -- it needs the input
 * sentence filter and the I/O direction, both per-connection configuration
 * rather than wire-protocol state.
 */
class Nmea0183Decoder : public ProtocolDecoder {
public:
  explicit Nmea0183Decoder(const ConnectionParams& params);

  /** One 0183 frame -> exactly one Nmea0183Msg (or none for an output-only
   *  connection / an empty frame). */
  std::vector<std::shared_ptr<const NavMsg>> Decode(
      const CommFrame& frame,
      const std::shared_ptr<const NavAddr>& src) override;

  /** An Nmea0183Msg -> one wire frame, CR/LF terminated. Empty for a
   *  non-0183 message or an input-only connection. */
  std::vector<CommFrame> Encode(
      const std::shared_ptr<const NavMsg>& msg) override;

private:
  ConnectionParams m_params;
};

#endif  // COMM_N0183_DECODER_H
