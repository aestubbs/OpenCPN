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
 * v4-tag stripping, garbage / bad-checksum classification and the input
 * sentence filter. Encode() is the transmit path.
 *
 * wx-free (task P1.6): the decoder takes a plain dsPortType and a
 * SentenceFilter, not a wx-typed ConnectionParams -- the factory adapts.
 * So it is unit-testable against captured sentence logs with no hardware
 * and no wxWidgets.
 */

#ifndef COMM_N0183_DECODER_H
#define COMM_N0183_DECODER_H

#include <memory>

#include <QList>

#include "model/comm_protocol_decoder.h"
#include "model/ds_porttype.h"
#include "model/sentence_filter.h"

/**
 * ProtocolDecoder for the NMEA 0183 wire protocol.
 *
 * Holds the connection's I/O direction and input sentence filter -- both
 * per-connection configuration rather than wire-protocol state.
 */
class Nmea0183Decoder : public ProtocolDecoder {
public:
  Nmea0183Decoder(dsPortType io_select, SentenceFilter input_filter);

  /** One 0183 frame -> exactly one Nmea0183Msg (or none for an output-only
   *  connection / an empty frame). */
  QList<std::shared_ptr<const NavMsg>> Decode(
      const CommFrame& frame,
      const std::shared_ptr<const NavAddr>& src) override;

  /** An Nmea0183Msg -> one wire frame, CR/LF terminated. Empty for a
   *  non-0183 message or an input-only connection. NMEA 0183 has no
   *  destination address, so dest is ignored. */
  QList<CommFrame> Encode(
      const std::shared_ptr<const NavMsg>& msg,
      const std::shared_ptr<const NavAddr>& dest) override;

private:
  dsPortType m_io_select;
  SentenceFilter m_input_filter;
};

#endif  // COMM_N0183_DECODER_H
