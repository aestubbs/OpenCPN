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
 * Implement qt_event_bridge.h
 */

#include "qt_event_bridge.h"

#include <QCoreApplication>

#include <wx/timer.h>

namespace {

/** Pump interval. Short enough that queued Qt slot calls feel immediate. */
constexpr int kPumpIntervalMs = 10;

// QCoreApplication keeps references to argc/argv for its whole lifetime, so
// this storage must outlive it: hence file-static. The real command line is
// still parsed by wxApp; Qt only needs a program name here.
int qt_argc = 1;
char qt_arg0[] = "OpenCPN";
char* qt_argv[] = {qt_arg0, nullptr};

}  // namespace

/** wxTimer whose tick drains the Qt event queue. */
class QtEventBridge::Impl : public wxTimer {
public:
  void Notify() override {
    if (QCoreApplication::instance()) QCoreApplication::processEvents();
  }
};

QtEventBridge::QtEventBridge() : m_impl(std::make_unique<Impl>()) {}

QtEventBridge::~QtEventBridge() { Stop(); }

void QtEventBridge::Start() {
  if (!QCoreApplication::instance()) {
    // Intentionally never deleted: it lives for the whole process so that
    // Qt objects owned by long-lived singletons stay valid until exit.
    new QCoreApplication(qt_argc, qt_argv);
  }
  m_impl->Start(kPumpIntervalMs);
}

void QtEventBridge::Stop() {
  if (m_impl) m_impl->Stop();
}
