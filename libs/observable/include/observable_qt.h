/*************************************************************************
 *
 * Copyright (C) 2026 OpenCPN Developers
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
 * \file
 *
 * Qt signals/slots implementation of the Notify()/Listen() pattern.
 *
 * This is the wx-free replacement for observable.h / observable_evtvar.h,
 * added alongside the wx implementation (strangler migration). See
 * docs/QT_MIGRATION.md and QT_MIGRATION_TASKS.md task P1.1.
 *
 * IMPORTANT: this header deliberately pulls in **no Qt headers**. All Qt
 * usage is encapsulated in observable_qt.cpp / observable_qt_p.h. This keeps
 * the header safe to include from translation units that also include
 * wxWidgets headers, which otherwise clash with Qt headers via macros.
 */

#ifndef OBSERVABLE_QT_H
#define OBSERVABLE_QT_H

#include <functional>
#include <memory>
#include <string>

#include "observable_data.h"

/**
 * RAII listening handle.
 *
 * Connects an action to a key on construction (or Listen()) and disconnects
 * on destruction. Movable, non-copyable.
 *
 * The action runs in the thread that created the ObsConnection, regardless
 * of which thread triggers the notification: delivery is queued
 * (asynchronous), preserving the previous wxQueueEvent() semantics. The
 * creating thread must therefore run a Qt event loop.
 */
class ObsConnection {
public:
  using Action = std::function<void(const ObsData&)>;

  /** Construct an idle handle that listens to nothing until Listen(). */
  ObsConnection();

  /** Construct and immediately listen to key. */
  ObsConnection(const std::string& key, Action action);

  ~ObsConnection();

  ObsConnection(ObsConnection&& other) noexcept;
  ObsConnection& operator=(ObsConnection&& other) noexcept;
  ObsConnection(const ObsConnection&) = delete;
  ObsConnection& operator=(const ObsConnection&) = delete;

  /** (Re)connect to key; drops any previous connection. */
  void Listen(const std::string& key, Action action);

  /** Drop the current connection, if any. */
  void Reset();

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

/**
 * Producer-side handle for an anonymous event variable.
 *
 * Owns a unique key and forwards Notify() calls to the matching notifier.
 * Mirrors the wx EventVar API surface.
 */
class EventVarQt {
public:
  EventVarQt();

  /** The key a listener must use to listen to this variable. */
  std::string Key() const { return m_key; }

  /** Notify all listeners, no payload. */
  void Notify();

  /** Notify all listeners with an explicit payload. */
  void Notify(const ObsData& data);

  /** Notify all listeners with a string payload. */
  void Notify(const std::string& s);

  /** Notify all listeners with an int and a string payload. */
  void Notify(int num, const std::string& s);

  /** Notify all listeners with a shared_ptr, string and optional int. */
  void Notify(const std::shared_ptr<const void>& p, const std::string& s = "",
              int num = 0);

private:
  std::string m_key;
};

/**
 * Run action asynchronously on the Qt main event loop.
 *
 * Used to defer work, typically off a worker thread. If no Qt event loop
 * exists yet the action is run immediately and synchronously.
 */
void PostToMainThread(std::function<void()> action);

/**
 * Notify every listener on `key` with `data`, through the Qt notifier
 * registry. The wx-side Observable::Notify is implemented on top of this so
 * the notify/listen machinery runs entirely on the Qt event loop -- no wx
 * event loop. Qt-header-free, so it is callable from wx-including code.
 */
void ObsNotifyByKey(const std::string& key, const ObsData& data);

/**
 * Recover the typed shared payload an ObsData carries (the wx-free
 * equivalent of UnpackEvtPointer for ObservedEvt). Returns a
 * shared_ptr<const T> aliasing data.shared_ptr.
 */
template <typename T>
std::shared_ptr<const T> UnpackObsData(const ObsData& data) {
  return std::static_pointer_cast<const T>(data.shared_ptr);
}

#endif  // OBSERVABLE_QT_H
