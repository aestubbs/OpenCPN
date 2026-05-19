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
 * Implement comm_n0183_decoder.h -- the NMEA 0183 protocol decoder.
 *
 * The classification helpers mirror CommDriverN0183::SendToListener; that
 * legacy path is retired when the last CommDriverN0183-based driver is, at
 * which point this becomes the single 0183 decode site.
 */

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

#include <QByteArray>

#include "model/comm_n0183_decoder.h"

/** Return true iff the checksum in a 0183 sentence is correct. */
static bool Is0183ChecksumOk(const QByteArray& sentence) {
  const int cs_start = sentence.indexOf('*');
  if (cs_start < 0 || cs_start > sentence.size() - 3)
    return false;  // Not found, or fewer than two characters following it.

  bool ok = false;
  const uint checksum = sentence.mid(cs_start + 1, 2).toUInt(&ok, 16);
  if (!ok) return false;

  unsigned char calculated_checksum = 0;
  for (int i = 1; i < cs_start; i++)
    calculated_checksum ^= static_cast<unsigned char>(sentence[i]);
  return calculated_checksum == checksum;
}

/**
 * Return the part of the frame starting with '$' or '!', stripping any v4
 * tag prefix. Returns empty if no sentence at least six chars long is found.
 */
static QByteArray GetPayloadSentence(const QByteArray& frame) {
  int start = frame.indexOf('$');
  if (start < 0) start = frame.indexOf('!');
  if (start < 0) return {};
  if (frame.size() < start + 6) return {};
  return frame.mid(start);
}

Nmea0183Decoder::Nmea0183Decoder(dsPortType io_select,
                                 SentenceFilter input_filter)
    : m_io_select(io_select), m_input_filter(std::move(input_filter)) {}

QList<std::shared_ptr<const NavMsg>> Nmea0183Decoder::Decode(
    const CommFrame& frame, const std::shared_ptr<const NavAddr>& src) {
  // An output-only connection ignores anything that arrives.
  if (m_io_select == DS_TYPE_OUTPUT) return {};

  const QByteArray sentence = GetPayloadSentence(frame);
  if (sentence.isEmpty()) return {};

  const bool is_garbage =
      sentence.size() > 128 ||
      std::any_of(sentence.begin(), sentence.end(), [](char c) {
        return !isprint(static_cast<unsigned char>(c)) && c != '\n' &&
               c != '\r';
      });
  const bool has_checksum =
      sentence.indexOf('*', sentence.size() - 6) >= 0;

  NavMsg::State state;
  if (is_garbage)
    state = NavMsg::State::kCannotParse;
  else if (!m_input_filter.Passes(sentence))
    state = NavMsg::State::kFiltered;
  else if (has_checksum && !Is0183ChecksumOk(sentence))
    state = NavMsg::State::kBadChecksum;
  else
    state = NavMsg::State::kOk;

  std::shared_ptr<const Nmea0183Msg> msg;
  if (is_garbage) {
    msg = std::make_shared<const Nmea0183Msg>("TRASH", frame.toStdString(),
                                              src, state);
  } else {
    // Notify based on the full message id, including the talker.
    msg = std::make_shared<const Nmea0183Msg>(sentence.mid(1, 5).toStdString(),
                                              sentence.toStdString(), src,
                                              state);
  }
  return {std::move(msg)};
}

QList<CommFrame> Nmea0183Decoder::Encode(
    const std::shared_ptr<const NavMsg>& msg,
    const std::shared_ptr<const NavAddr>& /* dest */) {
  // An input-only connection cannot transmit.
  if (m_io_select == DS_TYPE_INPUT) return {};

  auto msg_0183 = std::dynamic_pointer_cast<const Nmea0183Msg>(msg);
  if (!msg_0183) return {};

  QByteArray payload = QByteArray::fromStdString(msg_0183->payload);
  if (!payload.endsWith("\r\n")) payload += "\r\n";
  return {payload};
}
