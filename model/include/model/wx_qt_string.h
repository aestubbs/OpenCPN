/***************************************************************************
 *   Copyright (C) 2025 by the OpenCPN Development Team                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 * \file
 *
 * Helpers for converting between wxString and QString during the ongoing
 * wxWidgets -> Qt migration. Always uses an explicit UTF-8 round-trip so
 * non-ASCII text is preserved regardless of the current locale.
 */

#ifndef OCPN_WX_QT_STRING_H_
#define OCPN_WX_QT_STRING_H_

#include <QString>
#include <wx/string.h>

/** Convert a wxString to a QString using an explicit UTF-8 round-trip. */
inline QString wxString_to_QString(const wxString &ws) {
  return QString::fromStdString(ws.utf8_string());
}

/** Convert a QString to a wxString using an explicit UTF-8 round-trip. */
inline wxString QString_to_wxString(const QString &qs) {
  return wxString::FromUTF8(qs.toStdString());
}

#endif  // OCPN_WX_QT_STRING_H_
