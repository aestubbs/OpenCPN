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
 * ChartSpatialIndex -- a coarse uniform-grid index of catalog cells by
 * geographic bounding box, so the quilt's per-pan candidate gather is
 * O(cells-in-view) instead of O(whole-catalog).
 *
 * The catalog can hold thousands of cells (a full US ENC set is ~8000). The
 * quilt re-runs candidate selection on every pan/zoom settle, and scanning all
 * of them with a bbox test each time is wasted work on the UI thread -- felt as
 * jank on a Raspberry Pi exactly in the dense regions (Florida, the Gulf).
 *
 * Buckets are 1-degree lon/lat tiles. A cell is filed in every bucket its bbox
 * overlaps, EXCEPT very large or antimeridian-crossing cells (a handful of
 * overview tiles) which would explode the bucket count -- those go in a small
 * always-checked list. query() returns a coarse superset (deduplicated); the
 * caller still refines with CellExtent::intersects()/covers() exactly as
 * before, so selection results are unchanged -- only faster.
 *
 * Header-only, no Qt object semantics; built on the UI thread from m_catalog
 * after each scan, holding pointers into that QHash (rebuild on catalog change,
 * like the candidate list it replaces).
 */

#ifndef OCPN_QT_CHART_SPATIAL_INDEX_H_
#define OCPN_QT_CHART_SPATIAL_INDEX_H_

#include <cmath>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "chart_extent.h"

namespace ocpn::qtui {

class ChartSpatialIndex {
public:
  void clear() {
    m_buckets.clear();
    m_large.clear();
  }

  /** (Re)build from the catalog. Pointers reference `catalog`'s values, so the
   *  index is valid only until that QHash is next modified. */
  void build(const QHash<QString, CellExtent>& catalog) {
    clear();
    m_buckets.reserve(catalog.size());
    for (auto it = catalog.cbegin(); it != catalog.cend(); ++it) {
      const CellExtent& c = it.value();
      if (!c.valid()) continue;
      // Antimeridian-crossing or large-footprint (overview) cells: file once in
      // the always-checked list rather than across hundreds of buckets.
      if (c.east < c.west || (c.east - c.west) > kLargeDeg ||
          (c.north - c.south) > kLargeDeg) {
        m_large.append(&c);
        continue;
      }
      const int ix0 = bucket(c.west), ix1 = bucket(c.east);
      const int iy0 = bucket(c.south), iy1 = bucket(c.north);
      for (int iy = iy0; iy <= iy1; ++iy)
        for (int ix = ix0; ix <= ix1; ++ix)
          m_buckets[key(ix, iy)].append(&c);
    }
  }

  /** Coarse candidate query: append every cell that MIGHT overlap the rect to
   *  `out` (deduplicated). The caller refines with a precise test. */
  void query(double lat0, double lat1, double lon0, double lon1,
             QList<const CellExtent*>& out) const {
    QSet<const CellExtent*> seen;
    const int ix0 = bucket(lon0), ix1 = bucket(lon1);
    const int iy0 = bucket(lat0), iy1 = bucket(lat1);
    for (int iy = iy0; iy <= iy1; ++iy) {
      for (int ix = ix0; ix <= ix1; ++ix) {
        auto it = m_buckets.constFind(key(ix, iy));
        if (it == m_buckets.constEnd()) continue;
        for (const CellExtent* c : it.value())
          if (!seen.contains(c)) {
            seen.insert(c);
            out.append(c);
          }
      }
    }
    for (const CellExtent* c : m_large)
      if (!seen.contains(c)) {
        seen.insert(c);
        out.append(c);
      }
  }

  bool empty() const { return m_buckets.isEmpty() && m_large.isEmpty(); }

private:
  static constexpr double kBucketDeg = 1.0;  // bucket size
  static constexpr double kLargeDeg = 20.0;  // "file in m_large" footprint
  static int bucket(double deg) {
    return static_cast<int>(std::floor(deg / kBucketDeg));
  }
  static qint64 key(int ix, int iy) {
    return (static_cast<qint64>(ix) << 32) ^ (static_cast<quint32>(iy));
  }

  QHash<qint64, QList<const CellExtent*>> m_buckets;
  QList<const CellExtent*> m_large;  // overview / dateline-crossing cells
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_SPATIAL_INDEX_H_
