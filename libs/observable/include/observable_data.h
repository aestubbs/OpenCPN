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
 * Toolkit-free payload delivered by the Qt observable mechanism.
 *
 * Replaces the wxWidgets-based ObservedEvt (a wxCommandEvent subclass) for
 * the new signals/slots path. Plain enough to be used by non-Qt core code.
 * See QT_MIGRATION_TASKS.md task P1.1b.
 */

#ifndef OBSERVABLE_DATA_H
#define OBSERVABLE_DATA_H

#include <memory>
#include <string>

/** Payload carried by an observable notification. All fields optional. */
struct ObsData {
  std::shared_ptr<const void> shared_ptr;  ///< Optional shared payload.
  std::string string;                      ///< Optional string payload.
  int num = 0;                             ///< Optional integer payload.
  void* client_data = nullptr;             ///< Optional opaque pointer.
};

#endif  // OBSERVABLE_DATA_H
