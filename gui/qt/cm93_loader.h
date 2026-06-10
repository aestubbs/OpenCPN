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
 * Cm93Loader (P2.19 step (c)): decode one CM93 cell end-to-end into an
 * s52sg::Buffer -- ingest (cm93_cell_reader) -> geometry + S57Obj
 * transcode (cm93_transcoder) -> the same per-primitive LUP/rules/
 * RenderToSG emit the S-57 OGR path uses. Update-cell merging and user
 * offsets are not yet ported (base cells only) -- noted divergences.
 */

#ifndef OCPN_QT_CM93_LOADER_H_
#define OCPN_QT_CM93_LOADER_H_

#include <QString>

#include "s52_sg.h"

class s52plib;

namespace ocpn::qtui {

class Cm93Dictionary;

class Cm93Loader {
public:
  /** Decode `path` through `plib` into `out`. Bounds return the cell's
   *  geographic extent. False on ingest failure. */
  static bool loadCell(s52plib* plib, const QString& path,
                       const Cm93Dictionary* dict, s52sg::Buffer* out,
                       double* north, double* south, double* east,
                       double* west);
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CM93_LOADER_H_
