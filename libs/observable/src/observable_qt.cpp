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
 * Implement observable_qt.h. This is the single translation unit that sees
 * Qt headers; everything else uses the Qt-free public header.
 */

#include "observable_qt.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include <QCoreApplication>

#include "observable_qt_p.h"

namespace {

/** Register ObsData as a Qt metatype exactly once (needed for queued calls). */
void EnsureMetaTypeRegistered() {
  static std::once_flag flag;
  std::call_once(flag, [] { qRegisterMetaType<ObsData>("ObsData"); });
}

/** Produce a unique key for an anonymous EventVarQt. */
std::string AutoKey() {
  static std::atomic<unsigned long> counter{0};
  return std::string("!@%/+qt/") + std::to_string(counter++);
}

}  // namespace

/* ObsNotifier ------------------------------------------------------------ */

ObsNotifier::ObsNotifier(QObject* parent) : QObject(parent) {}

void ObsNotifier::Notify(const ObsData& data) { Q_EMIT Notified(data); }

/* ObsRegistry ------------------------------------------------------------ */

ObsNotifier& ObsRegistry::Notifier(const std::string& key) {
  static std::unordered_map<std::string, std::unique_ptr<ObsNotifier>>
      notifiers;
  static std::mutex mutex;

  std::lock_guard<std::mutex> lock(mutex);
  auto found = notifiers.find(key);
  if (found == notifiers.end()) {
    found = notifiers.emplace(key, std::make_unique<ObsNotifier>()).first;
  }
  return *found->second;
}

/* ObsConnection ---------------------------------------------------------- */

/** Holds the Qt connection state behind ObsConnection's pimpl. */
class ObsConnection::Impl {
public:
  std::unique_ptr<QObject> context;
  QMetaObject::Connection connection;
};

ObsConnection::ObsConnection() = default;

ObsConnection::ObsConnection(const std::string& key, Action action) {
  Listen(key, std::move(action));
}

ObsConnection::~ObsConnection() { Reset(); }

ObsConnection::ObsConnection(ObsConnection&&) noexcept = default;

ObsConnection& ObsConnection::operator=(ObsConnection&& other) noexcept {
  if (this != &other) {
    Reset();
    m_impl = std::move(other.m_impl);
  }
  return *this;
}

void ObsConnection::Listen(const std::string& key, Action action) {
  Reset();
  EnsureMetaTypeRegistered();
  if (!m_impl) m_impl = std::make_unique<Impl>();

  // The context object lives in the calling thread; with a queued connection
  // the action therefore runs in that thread no matter who triggers it.
  m_impl->context = std::make_unique<QObject>();
  ObsNotifier& notifier = ObsRegistry::Notifier(key);
  m_impl->connection = QObject::connect(
      &notifier, &ObsNotifier::Notified, m_impl->context.get(),
      [action = std::move(action)](const ObsData& data) { action(data); },
      Qt::QueuedConnection);
}

void ObsConnection::Reset() {
  if (!m_impl) return;
  if (m_impl->connection) {
    QObject::disconnect(m_impl->connection);
    m_impl->connection = QMetaObject::Connection();
  }
  m_impl->context.reset();
}

/* EventVarQt ------------------------------------------------------------- */

EventVarQt::EventVarQt() : m_key(AutoKey()) {}

void EventVarQt::Notify() { Notify(ObsData{}); }

void EventVarQt::Notify(const ObsData& data) {
  ObsRegistry::Notifier(m_key).Notify(data);
}

void EventVarQt::Notify(const std::string& s) {
  ObsData data;
  data.string = s;
  Notify(data);
}

void EventVarQt::Notify(int num, const std::string& s) {
  ObsData data;
  data.num = num;
  data.string = s;
  Notify(data);
}

void EventVarQt::Notify(const std::shared_ptr<const void>& p,
                        const std::string& s, int num) {
  ObsData data;
  data.shared_ptr = p;
  data.string = s;
  data.num = num;
  Notify(data);
}

/* Free helpers ----------------------------------------------------------- */

void PostToMainThread(std::function<void()> action) {
  if (auto* app = QCoreApplication::instance()) {
    QMetaObject::invokeMethod(app, std::move(action), Qt::QueuedConnection);
  } else {
    action();
  }
}
