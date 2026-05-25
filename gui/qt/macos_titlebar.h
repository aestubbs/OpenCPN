/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * macOS-only: install a native title-bar accessory button (right side) that
 * toggles AppController::hudExpanded -- the vessel-data drawer. Uses
 * NSTitlebarAccessoryViewController, which has no cross-platform Qt API, so
 * this is implemented in Obj-C++ (macos_titlebar.mm) and built only on Apple.
 * Returns true if the button was installed.
 */

#ifndef OCPN_QT_MACOS_TITLEBAR_H_
#define OCPN_QT_MACOS_TITLEBAR_H_

class QWindow;

namespace ocpn::qtui {

class AppController;

bool installTitlebarToggle(QWindow* window, AppController* app);

}  // namespace ocpn::qtui

#endif  // OCPN_QT_MACOS_TITLEBAR_H_
