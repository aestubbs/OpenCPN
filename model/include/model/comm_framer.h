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
 * PassThroughFramer. See Docs/QT_MIGRATION_COMMS_ARCH.md (task P1.5i).
 *
 * Framers are deliberately pure -- no Qt, no I/O -- so they are
 * unit-testable against captured byte logs with no hardware.
 */

#ifndef COMM_FRAMER_H
#define COMM_FRAMER_H

#include <cstdint>
#include <vector>

#include "model/comm_buffers.h"

/** One complete protocol frame. */
using CommFrame = std::vector<uint8_t>;

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

  virtual std::vector<CommFrame> Feed(const std::vector<uint8_t>& bytes) = 0;
};

/**
 * NMEA 0183 line framer -- frames are terminator-delimited sentences. Wraps
 * LineBuffer, the existing line-assembly logic.
 */
class LineFramer : public Framer {
public:
  std::vector<CommFrame> Feed(const std::vector<uint8_t>& bytes) override {
    std::vector<CommFrame> frames;
    for (uint8_t b : bytes) m_buffer.Put(b);
    while (m_buffer.HasLine()) frames.push_back(m_buffer.GetLine());
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
  std::vector<CommFrame> Feed(const std::vector<uint8_t>& bytes) override {
    if (bytes.empty()) return {};
    return {bytes};
  }
};

#endif  // COMM_FRAMER_H
