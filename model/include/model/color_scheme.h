/***************************************************************************
 *   Copyright (C) 2025 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

/**
 * \file
 *
 * The display-wide colour scheme enum (DAY / DUSK / NIGHT / RGB).
 *
 * Previously lived in libs/s52plib/src/color_types.h alongside the S-52
 * engine's internal colour types (S52color, colorHashMap, ...). That
 * coupled `ocpn::model` to `ocpn::s52plib` only because of this enum --
 * everything else in color_types.h is s52plib-internal. Extracted here
 * to break that backwards link dependency so libs/s52plib can in turn
 * depend on ocpn::model for `toSM`/`fromSM` / `g_b_EnableVBO` (needed
 * by the relocated S57Obj implementation; see P2.8.0 in
 * docs/QT_MIGRATION_TASKS.md).
 *
 * Kept C-style (`typedef enum ... _ColorScheme;`) so existing legacy
 * call sites compile unchanged.
 */

#ifndef OCPN_MODEL_COLOR_SCHEME_H_
#define OCPN_MODEL_COLOR_SCHEME_H_

typedef enum ColorScheme {
  GLOBAL_COLOR_SCHEME_RGB,
  GLOBAL_COLOR_SCHEME_DAY,
  GLOBAL_COLOR_SCHEME_DUSK,
  GLOBAL_COLOR_SCHEME_NIGHT,
  N_COLOR_SCHEMES
} _ColorScheme;

#endif  // OCPN_MODEL_COLOR_SCHEME_H_
