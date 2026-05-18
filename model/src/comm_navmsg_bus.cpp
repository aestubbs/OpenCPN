/**************************************************************************
 *   Copyright (C) 2022 by David Register                                  *
 *   Copyright (C) 2022 Alec Leamas                                        *
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
 * Implement comm_navmsg_bus.h i. e., NavMsgBus.
 */

#include "model/comm_navmsg_bus.h"

#include "observable.h"

void NavMsgBus::Notify(std::shared_ptr<const NavMsg> msg) {
  if (!msg) return;
  std::string key = NavAddr::BusToString(msg->bus) + "::" + msg->GetKey();
  auto deferred = [msg] { Observable(*msg).Notify(msg); };
  if (RegisterKey(key))
    // Leave some time for listeners to register before the message is sent.
    // Deferred onto the Qt event loop, pumped by QtEventBridge (task P1.16).
    PostToMainThread(deferred);
  else
    deferred();
}

bool NavMsgBus::RegisterKey(const std::string& key) {
  std::lock_guard lock(m_mutex);
  auto rv = m_active_messages.insert(key);
  if (rv.second) new_msg_event.Notify();
  return rv.second;
}

NavMsgBus& NavMsgBus::GetInstance() {
  static NavMsgBus instance;
  return instance;
}

/** Handle changes in driver list. */
void NavMsgBus::Notify(AbstractCommDriver const&) {}
