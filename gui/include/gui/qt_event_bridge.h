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
 * Runs Qt event processing inside the wxWidgets main loop.
 *
 * During the Qt migration the application is still a wxApp, so there is no
 * native Qt event loop. The Qt observable mechanism (queued signals/slots,
 * Qt timers) needs one to deliver cross-thread notifications. This bridge
 * creates a QCoreApplication and pumps QCoreApplication::processEvents()
 * from a wx timer.
 *
 * Temporary: remove once the application runs a native Qt event loop
 * (migration Phase 3). See docs/QT_MIGRATION_TASKS.md task P1.16.
 */

#ifndef QT_EVENT_BRIDGE_H
#define QT_EVENT_BRIDGE_H

#include <memory>

/** Pumps a QCoreApplication from the wx main loop. */
class QtEventBridge {
public:
  QtEventBridge();
  ~QtEventBridge();

  /** Create the QCoreApplication (once) and start pumping its event queue. */
  void Start();

  /** Stop pumping. The QCoreApplication itself lives until process exit. */
  void Stop();

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

#endif  // QT_EVENT_BRIDGE_H
