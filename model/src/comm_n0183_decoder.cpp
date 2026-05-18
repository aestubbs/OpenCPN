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
 * legacy path is retired when the serial driver moves onto the framework
 * (P1.5j), at which point this becomes the single 0183 decode site.
 */

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/string.h>

#include "model/comm_n0183_decoder.h"

/** Return true iff the checksum in a 0183 sentence is correct. */
static bool Is0183ChecksumOk(const std::string& sentence) {
  const size_t cs_start = sentence.find('*');
  if (cs_start == std::string::npos || cs_start > sentence.size() - 3)
    return false;  // Not found, or fewer than two characters following it.

  const std::string cs_str = sentence.substr(cs_start + 1, 2);
  const unsigned long checksum = strtol(cs_str.c_str(), nullptr, 16);
  if (checksum == 0L && cs_str != "00") return false;

  unsigned char calculated_checksum = 0;
  for (const char c : sentence.substr(1, cs_start - 1))
    calculated_checksum ^= static_cast<unsigned char>(c);
  return calculated_checksum == checksum;
}

/**
 * Return the part of the frame starting with '$' or '!', stripping any v4
 * tag prefix. Returns "" if no sentence at least six chars long is found.
 */
static std::string GetPayloadSentence(const std::string& sentence) {
  size_t start_pos = sentence.find('$');
  if (start_pos == std::string::npos) start_pos = sentence.find('!');
  if (start_pos == std::string::npos) return "";
  if (sentence.size() < start_pos + 6) return "";
  return sentence.substr(start_pos);
}

Nmea0183Decoder::Nmea0183Decoder(const ConnectionParams& params)
    : m_params(params) {}

std::vector<std::shared_ptr<const NavMsg>> Nmea0183Decoder::Decode(
    const CommFrame& frame, const std::shared_ptr<const NavAddr>& src) {
  // An output-only connection ignores anything that arrives.
  if (m_params.IOSelect == DS_TYPE_OUTPUT) return {};

  const std::string payload(frame.begin(), frame.end());
  const std::string sentence = GetPayloadSentence(payload);
  if (sentence.empty()) return {};

  const bool is_garbage =
      sentence.size() > 128 ||
      std::any_of(sentence.begin(), sentence.end(), [](char c) {
        return !isprint(static_cast<unsigned char>(c)) && c != '\n' &&
               c != '\r';
      });
  const bool has_checksum =
      sentence.find('*', sentence.size() - 6) != std::string::npos;

  NavMsg::State state;
  if (is_garbage)
    state = NavMsg::State::kCannotParse;
  else if (!m_params.SentencePassesFilter(sentence, FILTER_INPUT))
    state = NavMsg::State::kFiltered;
  else if (has_checksum && !Is0183ChecksumOk(sentence))
    state = NavMsg::State::kBadChecksum;
  else
    state = NavMsg::State::kOk;

  std::shared_ptr<const Nmea0183Msg> msg;
  if (is_garbage) {
    msg = std::make_shared<const Nmea0183Msg>("TRASH", payload, src, state);
  } else {
    // Notify based on the full message id, including the talker.
    const std::string id = sentence.substr(1, 5);
    msg = std::make_shared<const Nmea0183Msg>(id, sentence, src, state);
  }
  return {std::move(msg)};
}

std::vector<CommFrame> Nmea0183Decoder::Encode(
    const std::shared_ptr<const NavMsg>& msg) {
  // An input-only connection cannot transmit.
  if (m_params.IOSelect == DS_TYPE_INPUT) return {};

  auto msg_0183 = std::dynamic_pointer_cast<const Nmea0183Msg>(msg);
  if (!msg_0183) return {};

  std::string payload = msg_0183->payload;
  if (payload.size() < 2 ||
      payload.compare(payload.size() - 2, 2, "\r\n") != 0)
    payload += "\r\n";
  return {CommFrame(payload.begin(), payload.end())};
}
