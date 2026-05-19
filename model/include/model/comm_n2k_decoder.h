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
 * Comms framework -- NMEA 2000 gateway protocol decoder.
 *
 * Converts between Actisense application-data frames (produced by a
 * N2kGatewayFramer) and Nmea2000Msg objects. Decode() reads the data code,
 * PGN and node NAME out of a received frame; Encode() builds the escaped
 * Actisense TX packet for transmission.
 *
 * Management packets (data code 0xA0) are not NavMsg objects -- Decode()
 * ignores them; the gateway manager picks them up via the driver's raw
 * frame tap. No I/O, no Qt and no wxWidgets -- unit-testable (task P1.6).
 */

#ifndef COMM_N2K_DECODER_H
#define COMM_N2K_DECODER_H

#include <memory>

#include <QList>

#include "model/comm_protocol_decoder.h"
#include "model/ds_porttype.h"

/** ProtocolDecoder for NMEA 2000 over an Actisense-format serial gateway. */
class N2kDecoder : public ProtocolDecoder {
public:
  explicit N2kDecoder(dsPortType io_select);

  /** One Actisense application-data frame -> one Nmea2000Msg. Empty for a
   *  management packet, an output-only connection or a too-short frame. */
  QList<std::shared_ptr<const NavMsg>> Decode(
      const CommFrame& frame,
      const std::shared_ptr<const NavAddr>& src) override;

  /** An Nmea2000Msg -> one escaped Actisense TX packet. Empty for a
   *  non-N2K message or an input-only connection. dest, when a NavAddr2000,
   *  supplies the N2K destination node address (else broadcast, 255). */
  QList<CommFrame> Encode(
      const std::shared_ptr<const NavMsg>& msg,
      const std::shared_ptr<const NavAddr>& dest) override;

private:
  dsPortType m_io_select;
};

#endif  // COMM_N2K_DECODER_H
