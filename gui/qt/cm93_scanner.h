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
 * Cm93Scanner (P2.19 step (b)): walk a CM93 chart-set root and produce one
 * CellExtent per cell file -- name, path, geographic bbox (cheap
 * header-only read) and the scale tier's native 1:N -- the same catalog
 * records the S-57 scan emits, so the quilt can tier CM93 cells alongside
 * ENCs. Layout walked: <root>/<llll llll>/<scale char Z,A..G>/?lllnnnn.X
 * (case-insensitive scale dirs; .xz-compressed cells are skipped for now).
 */

#ifndef OCPN_QT_CM93_SCANNER_H_
#define OCPN_QT_CM93_SCANNER_H_

#include <QList>
#include <QString>

#include "chart_extent.h"

namespace ocpn::qtui {

class Cm93Scanner {
public:
  /** True if `dir` looks like a CM93 set: the dictionary loads from it (or
   *  a CM93SYS/ subdir) and it contains 8-digit lat/lon root folders. */
  static bool isCm93Root(const QString& dir);

  /** Enumerate every cell under `root` across all scale tiers. */
  static QList<CellExtent> scan(const QString& root);

  /** Native 1:N for a scale letter (Z,A..G); 0 if unknown. */
  static int scaleForChar(QChar c);
  /** S-57-style usage band approximation for a scale letter (1..6). */
  static int bandForChar(QChar c);
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CM93_SCANNER_H_
