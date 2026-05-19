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
 * Implement comm_n2k_decoder.h -- the NMEA 2000 gateway protocol decoder.
 */

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "model/comm_n2k_decoder.h"

/** Actisense data code for a PC-to-gateway N2K transmit message. */
static constexpr uint8_t kMsgTypeN2kTx = 0x94;
/** Actisense data code prefixing a gateway management packet. */
static constexpr uint8_t kMsgTypeMgmt = 0xA0;

/** Read frame byte i as an unsigned value (QByteArray indexes as char). */
static uint8_t Byte(const CommFrame& frame, int i) {
  return static_cast<uint8_t>(frame.at(i));
}

/** Reinterpret the first eight frame bytes as an N2K node NAME, as the
 *  legacy driver did -- used only to key the NavAddr2000. */
static uint64_t FrameToName(const CommFrame& frame) {
  uint64_t name = 0;
  std::memcpy(&name, frame.constData(), sizeof(name));
  return name;
}

N2kDecoder::N2kDecoder(dsPortType io_select) : m_io_select(io_select) {}

QList<std::shared_ptr<const NavMsg>> N2kDecoder::Decode(
    const CommFrame& frame, const std::shared_ptr<const NavAddr>& src) {
  // An output-only connection ignores anything that arrives.
  if (m_io_select == DS_TYPE_OUTPUT) return {};
  // Need at least the data code, length, priority, the 3-byte PGN and
  // enough bytes for the NAME reinterpretation.
  if (frame.size() < 8) return {};
  // Management packets are handled by the gateway manager via the raw
  // frame tap, not turned into bus messages.
  if (Byte(frame, 0) == kMsgTypeMgmt) return {};

  const uint64_t pgn = static_cast<uint64_t>(Byte(frame, 3)) |
                       (static_cast<uint64_t>(Byte(frame, 4)) << 8) |
                       (static_cast<uint64_t>(Byte(frame, 5)) << 16);

  const std::string iface = src ? src->iface : std::string();
  auto addr = std::make_shared<NavAddr2000>(iface, N2kName(FrameToName(frame)));

  // Nmea2000Msg carries its payload as std::vector<unsigned char> -- the
  // message-bus type, converted here at the NavMsg boundary.
  const auto* begin = reinterpret_cast<const unsigned char*>(frame.constData());
  std::vector<unsigned char> payload(begin, begin + frame.size());
  auto msg = std::make_shared<const Nmea2000Msg>(pgn, payload, addr);
  return {std::move(msg)};
}

QList<CommFrame> N2kDecoder::Encode(
    const std::shared_ptr<const NavMsg>& msg,
    const std::shared_ptr<const NavAddr>& dest) {
  // An input-only connection cannot transmit.
  if (m_io_select == DS_TYPE_INPUT) return {};

  auto n2k = std::dynamic_pointer_cast<const Nmea2000Msg>(msg);
  if (!n2k) return {};

  uint8_t destination = 255;  // N2K broadcast
  if (auto addr = std::dynamic_pointer_cast<const NavAddr2000>(dest))
    destination = addr->address;

  const std::vector<unsigned char>& data = n2k->payload;
  uint64_t pgn = n2k->PGN.pgn;

  CommFrame buf;
  int byte_sum = 0;
  // Append a byte to the packet body: ESC-escape it and add to the
  // running checksum sum.
  auto add = [&](uint8_t b) {
    if (b == static_cast<uint8_t>(kN2kEscape)) buf.append(kN2kEscape);
    buf.append(static_cast<char>(b));
    byte_sum += b;
  };

  buf.append(kN2kEscape);
  buf.append(kN2kStartOfText);
  add(kMsgTypeN2kTx);
  add(static_cast<uint8_t>(data.size() + 6));  // length excludes escaped bytes
  add(static_cast<uint8_t>(n2k->priority));
  add(static_cast<uint8_t>(pgn & 0xff));
  add(static_cast<uint8_t>((pgn >> 8) & 0xff));
  add(static_cast<uint8_t>((pgn >> 16) & 0xff));
  add(destination);
  add(static_cast<uint8_t>(data.size()));
  for (unsigned char b : data) add(b);

  byte_sum %= 256;
  const uint8_t checksum =
      static_cast<uint8_t>(byte_sum == 0 ? 0 : 256 - byte_sum);
  buf.append(static_cast<char>(checksum));
  if (checksum == static_cast<uint8_t>(kN2kEscape))
    buf.append(kN2kEscape);  // escape the checksum
  buf.append(kN2kEscape);
  buf.append(kN2kEndOfText);

  return {buf};
}
