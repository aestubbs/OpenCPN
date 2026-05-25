/*************************************************************************
 *
 *
 * Copyright (C) 2022 - 2025 Alec Leamas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.
 **************************************************************************/

/**
 *\file
 *
 * Implement observable.h
 */


#include <algorithm>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <wx/log.h>

#include "observable.h"

std::string ptr_key(const void* ptr) {
  std::ostringstream oss;
  oss << ptr;
  return oss.str();
}

/* ListenersByKey implementation. */

ListenersByKey& ListenersByKey::GetInstance(const std::string& key) {
  static std::unordered_map<std::string, ListenersByKey> instances;
  static std::mutex s_mutex;

  std::lock_guard<std::mutex> lock(s_mutex);
  if (instances.find(key) == instances.end()) {
    instances[key] = ListenersByKey();
  }
  return instances[key];
}

/* Observable implementation. */

using ev_pair = std::pair<wxEvtHandler*, wxEventType>;

void Observable::Listen(wxEvtHandler* listener, wxEventType ev_type) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto& listeners = m_list.listeners;

  ev_pair key_pair(listener, ev_type);
  auto found = std::find(listeners.begin(), listeners.end(), key_pair);
  assert((found == listeners.end()) && "Duplicate listener");
  m_list.listeners.push_back(key_pair);
}

bool Observable::Unlisten(wxEvtHandler* listener, wxEventType ev_type) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto& listeners = m_list.listeners;

  ev_pair key_pair(listener, ev_type);
  auto found = std::find(listeners.begin(), listeners.end(), key_pair);
  if (found == listeners.end()) return false;
  listeners.erase(found);
  return true;
}

void Observable::Notify(const std::shared_ptr<const void>& ptr,
                              const std::string& s, int num,
                              void* client_data) {
  // Fan out through the Qt notifier (queued on the Qt event loop). The
  // matching ObservableListener turns this back into an ObservedEvt and
  // dispatches it synchronously to its wxEvtHandler -- so the whole path runs
  // with no wx event loop.
  ObsData data;
  data.shared_ptr = ptr;
  data.string = s;
  data.num = num;
  data.client_data = client_data;
  ObsNotifyByKey(key, data);
}

void Observable::Notify() { Notify("", nullptr); }

/* ObservableListener implementation. */

void ObservableListener::Listen(const std::string& k, wxEvtHandler* l,
                                wxEventType e) {
  if (!key.empty()) Unlisten();
  key = k;
  listener = l;
  ev_type = e;
  Listen();
}

void ObservableListener::Listen() {
  if (key.empty()) return;
  assert(listener);
  // Subscribe on the Qt notifier; on each notification (delivered on this
  // thread's Qt event loop) rebuild the ObservedEvt and dispatch it
  // synchronously to the wxEvtHandler -- ProcessEvent() needs no event loop.
  wxEvtHandler* l = listener;
  wxEventType e = ev_type;
  m_conn.Listen(key, [l, e](const ObsData& d) {
    ObservedEvt evt(e);
    evt.SetSharedPtr(d.shared_ptr);
    evt.SetClientData(d.client_data);
    evt.SetString(wxString::FromUTF8(d.string.c_str()));
    evt.SetInt(d.num);
    l->ProcessEvent(evt);
  });
}

void ObservableListener::Unlisten() {
  m_conn.Reset();
  key = "";
}
