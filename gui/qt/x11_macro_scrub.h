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
 * Undefine the X11/Xlib object-like macros that break Qt headers on
 * GTK/Linux (qtextstream's Status guard, qmetatype's Bool, qnamespace
 * enumerators...). wx GL/GTK headers (wx/glcanvas.h, anything pulling
 * gdk) define them.
 *
 * Deliberately NO include guard -- include it again after EVERY header
 * block that drags in Xlib, before the next Qt header (the Qt fixx11h.h
 * pattern). On non-X11 platforms it is a no-op.
 */

// NOLINT(build/header_guard)

#ifdef Status
#undef Status
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef None
#undef None
#endif
#ifdef CursorShape
#undef CursorShape
#endif
#ifdef KeyPress
#undef KeyPress
#endif
#ifdef KeyRelease
#undef KeyRelease
#endif
#ifdef FocusIn
#undef FocusIn
#endif
#ifdef FocusOut
#undef FocusOut
#endif
#ifdef FontChange
#undef FontChange
#endif
#ifdef Expose
#undef Expose
#endif
#ifdef Unsorted
#undef Unsorted
#endif
#ifdef GrayScale
#undef GrayScale
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
