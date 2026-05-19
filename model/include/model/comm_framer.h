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
 * Comms framework -- frame boundary finding.
 *
 * A Framer turns the raw byte stream delivered by a CommTransport into
 * discrete protocol frames. Byte-stream media (serial, TCP, UDP) need real
 * framing -- finding sentence/packet boundaries in a buffered stream;
 * frame-native media (CAN, WebSocket) hand over frames pre-built and use
 * PassThroughFramer. See docs/QT_MIGRATION_COMMS_ARCH.md.
 *
 * Wire data is carried as QByteArray throughout the comms pipeline.
 */

#ifndef COMM_FRAMER_H
#define COMM_FRAMER_H

#include <cstdint>

#include <QByteArray>
#include <QList>

#include "model/comm_buffers.h"

/** One complete protocol frame. */
using CommFrame = QByteArray;

/**
 * Turns a raw byte stream into discrete frames.
 *
 * Feed() is given successive chunks as they arrive from the transport. It
 * buffers any partial frame internally and returns whatever complete frames
 * are available after the chunk -- possibly none.
 */
class Framer {
public:
  virtual ~Framer() = default;

  virtual QList<CommFrame> Feed(const QByteArray& bytes) = 0;
};

/**
 * NMEA 0183 line framer -- frames are terminator-delimited sentences. Wraps
 * LineBuffer, the existing line-assembly logic.
 */
class LineFramer : public Framer {
public:
  QList<CommFrame> Feed(const QByteArray& bytes) override {
    QList<CommFrame> frames;
    for (char b : bytes) m_buffer.Put(static_cast<uint8_t>(b));
    while (m_buffer.HasLine()) {
      const std::vector<uint8_t> line = m_buffer.GetLine();
      frames.append(QByteArray(reinterpret_cast<const char*>(line.data()),
                               static_cast<qsizetype>(line.size())));
    }
    return frames;
  }

private:
  LineBuffer m_buffer;
};

/**
 * Identity framer for frame-native media (CAN, WebSocket): each chunk the
 * transport hands in already is exactly one frame.
 */
class PassThroughFramer : public Framer {
public:
  QList<CommFrame> Feed(const QByteArray& bytes) override {
    if (bytes.isEmpty()) return {};
    return {bytes};
  }
};

// Control bytes of the Actisense serial paketizing format:
//   <ESC><STX> <application data, ESC-escaped> <CRC> <ESC><ETX>
constexpr char kN2kEscape = 0x10;       ///< DLE
constexpr char kN2kStartOfText = 0x02;  ///< STX
constexpr char kN2kEndOfText = 0x03;    ///< ETX

/**
 * Framer for NMEA 2000 gateways speaking the Actisense binary serial format
 * (NGT-1, Yacht Devices YDNU-02, ...). It finds <ESC><STX> ... <ESC><ETX>
 * packet boundaries and un-escapes the doubled ESC bytes, emitting the raw
 * application data (data code, length, PGN, payload, trailing CRC) as one
 * frame. CRC validation is left to the decoder.
 *
 * Stateful across chunks; see comm_framer.cpp.
 */
class N2kGatewayFramer : public Framer {
public:
  QList<CommFrame> Feed(const QByteArray& bytes) override;

private:
  CommFrame m_frame;       ///< application data of the packet in progress
  bool m_in_msg = false;   ///< between <ESC><STX> and <ESC><ETX>
  bool m_got_esc = false;  ///< previous byte was an unescaped ESC
  bool m_got_sot = false;  ///< an <ESC><STX> opener was just seen
};

#endif  // COMM_FRAMER_H
