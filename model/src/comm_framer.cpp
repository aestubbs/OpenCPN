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
 * Implement the non-trivial Framer methods declared in comm_framer.h.
 */

#include "model/comm_framer.h"

std::vector<CommFrame> N2kGatewayFramer::Feed(
    const std::vector<uint8_t>& bytes) {
  std::vector<CommFrame> frames;
  for (uint8_t b : bytes) {
    if (m_in_msg) {
      if (m_got_esc) {
        // The byte after an ESC decides the escape sequence.
        m_got_esc = false;
        if (b == kN2kEscape) {
          m_frame.push_back(b);  // <ESC><ESC> -> a literal ESC
        } else if (b == kN2kEndOfText) {
          frames.push_back(m_frame);  // <ESC><ETX> -> packet complete
          m_frame.clear();
          m_in_msg = false;
        } else if (b == kN2kStartOfText) {
          m_frame.clear();  // <ESC><STX> mid-packet -> restart
        } else {
          m_frame.clear();  // any other <ESC><x> -> abort, resync
          m_in_msg = false;
        }
      } else {
        m_got_esc = (b == kN2kEscape);
        if (!m_got_esc) m_frame.push_back(b);
      }
    } else if (b == kN2kStartOfText) {
      // Opens a packet only when preceded by an ESC; m_got_esc is
      // recomputed from the next non-STX byte before it is used again.
      m_got_sot = m_got_esc;
    } else {
      m_got_esc = (b == kN2kEscape);
      if (m_got_sot) {
        m_got_sot = false;
        m_in_msg = true;
        m_frame.push_back(b);
      }
    }
  }
  return frames;
}
