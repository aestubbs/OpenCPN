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
 * Implement comm_signalk_decoder.h -- SignalK JSON stream decoding.
 */

#include <cstring>

#include "rapidjson/document.h"

#include "model/comm_signalk_decoder.h"

QList<std::shared_ptr<const NavMsg>> SignalKDecoder::Decode(
    const CommFrame& frame, const std::shared_ptr<const NavAddr>& /*src*/) {
  const std::string msg = frame.toStdString();

  rapidjson::Document root;
  root.Parse(msg);
  if (root.HasParseError()) {
    qWarning("SignalKDecoder: JSON not well-formed: error %d",
             static_cast<int>(root.GetParseError()));
    return {};
  }
  if (!root.IsObject()) {
    qWarning("SignalKDecoder: message is not a JSON object");
    return {};
  }

  // The server hello announces its version and the stream's "self" vessel;
  // later deltas may switch context. Track both (legacy HandleSkSentence).
  if (root.HasMember("version") && root["version"].IsString())
    qInfo("SignalKDecoder: connected to server version %s",
          root["version"].GetString());
  if (root.HasMember("self") && root["self"].IsString()) {
    const char* self = root["self"].GetString();
    if (strncmp(self, "vessels.", 8) == 0)
      m_self = self;  // Java server / OpenPlotter node.js >= 1.20
    else
      m_self = std::string("vessels.").append(self);  // node.js server
  }
  if (root.HasMember("context") && root["context"].IsString())
    m_context = root["context"].GetString();

  return {std::make_shared<const SignalkMsg>(m_self, m_context, msg,
                                             m_iface)};
}
