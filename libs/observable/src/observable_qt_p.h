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
 * Private (Qt-facing) internals of observable_qt.h.
 *
 * This header includes Qt headers and must NOT be included by any
 * translation unit that also includes wxWidgets headers. Only
 * observable_qt.cpp and the observable_qt unit tests include it.
 */

#ifndef OBSERVABLE_QT_P_H
#define OBSERVABLE_QT_P_H

#include <string>

#include <QMetaType>
#include <QObject>

#include "observable_data.h"

Q_DECLARE_METATYPE(ObsData)

/**
 * Per-key notifier. One instance exists for each key in use, obtained
 * through ObsRegistry. Emits Notified() to every connected listener.
 */
class ObsNotifier : public QObject {
  Q_OBJECT

public:
  explicit ObsNotifier(QObject* parent = nullptr);

  /** Emit Notified() to every connected listener. */
  void Notify(const ObsData& data);

signals:
  /** Raised once per Notify() call. */
  void Notified(const ObsData& data);
};

/**
 * Process-wide registry mapping string keys to ObsNotifier instances.
 *
 * Notifiers are created lazily on first use and live for the process
 * lifetime, mirroring the wx ListenersByKey singleton.
 */
class ObsRegistry {
public:
  /** Return the notifier for key, creating it on first use. Thread-safe. */
  static ObsNotifier& Notifier(const std::string& key);

  ObsRegistry(const ObsRegistry&) = delete;
  ObsRegistry& operator=(const ObsRegistry&) = delete;

private:
  ObsRegistry() = default;
};

#endif  // OBSERVABLE_QT_P_H
