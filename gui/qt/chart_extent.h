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
 * CellExtent -- the cheap, decode-free description of one ENC cell: its
 * name, its .000 path, and its geographic bounding box.
 *
 * The chart "catalog" is a list of these. It's produced by an extent-only
 * scan (S52Engine::scanCellExtents) that opens each cell and unions feature
 * envelopes WITHOUT running symbology decode / tessellation -- cheap enough
 * to do for every cell in a chart set off the main thread, so the canvas
 * can draw cell-coverage boundaries and roam the whole world before (and
 * while) the heavyweight vector content streams in on demand.
 *
 * A value type using only Qt types, declared a metatype so it (and a
 * QList of it) can cross the worker-thread -> main-thread queued-signal
 * boundary.
 */

#ifndef OCPN_QT_CHART_EXTENT_H_
#define OCPN_QT_CHART_EXTENT_H_

#include <QList>
#include <QMetaType>
#include <QString>

namespace ocpn::qtui {

struct CellExtent {
  QString name;            // cell base name, e.g. "US5CA1EJ"
  QString path;            // absolute path to the .000 file
  double north = -90.0;    // bounding box (degrees)
  double south = 90.0;
  double east = -180.0;
  double west = 180.0;
  // S-57 usage band (1 overview .. 6 berthing), the 3rd char of an S-57
  // cell name (e.g. US**5**CA1EJ). 0 if it can't be determined.
  int band = 0;
  // Native compilation scale (the DSPM:CSCL field, i.e. the 1:N the cell was
  // compiled at). Drives quilting: at a given zoom we render only the cells
  // whose native scale tier best matches, so each location is covered by one
  // cell and no coarser/finer cell overdraws it. 0 if unknown.
  int nativeScale = 0;

  /** A box is valid once it has been grown by at least one feature. */
  bool valid() const { return east > west && north > south; }

  /** Parse the S-57 usage band from a cell name (3rd character). Returns 0
   *  if the name doesn't look like a standard S-57 cell name. */
  static int bandFromName(const QString& cell_name) {
    if (cell_name.size() < 3) return 0;
    const QChar c = cell_name.at(2);
    return (c >= '1' && c <= '6') ? c.digitValue() : 0;
  }

  /** True if this cell's box overlaps the given lon/lat rectangle. */
  bool intersects(double lat_min, double lat_max, double lon_min,
                  double lon_max) const {
    return east >= lon_min && west <= lon_max && north >= lat_min &&
           south <= lat_max;
  }
};

}  // namespace ocpn::qtui

Q_DECLARE_METATYPE(ocpn::qtui::CellExtent)
Q_DECLARE_METATYPE(QList<ocpn::qtui::CellExtent>)

#endif  // OCPN_QT_CHART_EXTENT_H_
