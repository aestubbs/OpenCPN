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
 * ChartCatalogCache -- a persistent SQLite cache of the chart catalog (cell
 * name / extent / native scale per cell), keyed by file path + mtime. The
 * extent scan is expensive -- raw S-57 must be OGR-parsed, and o-charts cells
 * must be DECRYPTED via oexserverd (~0.5 s each, so ~minutes for a full set).
 * Caching the result means that cost is paid ONCE; later launches read the
 * catalog straight from SQLite (instant) and only re-scan cells whose file
 * changed.
 *
 * Lives in the same navobj.db as ConfigStore, in a `chart_catalog` table, on
 * its own connection (created/used on the chart-worker thread). The cell's
 * actual M_COVR coverage polygons are cached alongside the bbox (serialised
 * into a BLOB column): the quilt's per-location pick tests against the
 * coverage (CellExtent::covers), so dropping it made every warm launch fall
 * back to the bbox -- letting a small-coverage cell "win" (and blank) the rest
 * of its bounding box. A pre-coverage row (NULL blob) is treated as a miss so
 * it is re-scanned once and back-filled.
 */

#ifndef OCPN_QT_CHART_CATALOG_DB_H_
#define OCPN_QT_CHART_CATALOG_DB_H_

#include <QString>

#include "chart_extent.h"

struct sqlite3;

namespace ocpn::qtui {

class ChartCatalogCache {
public:
  ChartCatalogCache();
  ~ChartCatalogCache();
  ChartCatalogCache(const ChartCatalogCache&) = delete;
  ChartCatalogCache& operator=(const ChartCatalogCache&) = delete;

  bool isOpen() const { return m_db != nullptr; }

  /** Look up a cached extent for `path`; a hit requires the stored mtime to
   *  match `mtime`. Returns true and fills `out` on hit. */
  bool get(const QString& path, qint64 mtime, CellExtent& out) const;

  /** Insert/replace the cached extent for `ce.path` with the given mtime. */
  void put(const CellExtent& ce, qint64 mtime);

private:
  sqlite3* m_db = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_CATALOG_DB_H_
