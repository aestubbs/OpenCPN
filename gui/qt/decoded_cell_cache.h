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
 * DecodedCellCache -- a bounded, in-memory LRU of fully-decoded chart cells
 * (an s52sg::Buffer plus its geographic extent), keyed by source-file path and
 * validated by mtime.
 *
 * Why: decoding a cell (OGR/OSENC read + libtess2 tessellation + the full
 * s52plib symbology pass) is the dominant CPU cost of the chart pipeline, and
 * the quilt re-selects the same cells constantly while panning -- pan back,
 * zoom across a tier boundary, or just oscillate around a harbour and every
 * cell that left the view is re-decoded from scratch when it returns. This
 * cache keeps the last cells' decoded buffers so a re-selection is a refcount
 * bump, not a re-decode. It is the single biggest lever for the Florida
 * panhandle / Gulf "processor exhaustion while panning" symptom.
 *
 * Lives on the chart-worker thread; single-threaded access, so no locking.
 *
 * IMPORTANT: a decoded buffer bakes in the s52plib display settings and colour
 * scheme that were live at decode time (SCAMIN, depth shading + contours,
 * symbol/boundary style, palette, height/depth units). When ANY of those
 * change the entire cache is stale -- the worker MUST call clear() (it does, in
 * setColorScheme()/applyDisplaySettings()).
 *
 * s52sg::Buffer holds only Qt copy-on-write containers, so get()/put() copies
 * are O(1) refcount bumps until a consumer mutates its own copy; the cache
 * therefore shares storage with the live provider rather than duplicating it.
 */

#ifndef OCPN_QT_DECODED_CELL_CACHE_H_
#define OCPN_QT_DECODED_CELL_CACHE_H_

#include <QHash>
#include <QList>
#include <QString>

#include "s52_sg.h"

namespace ocpn::qtui {

class DecodedCellCache {
public:
  /** A decoded cell: the geometry buffer plus the cell's geographic extent
   *  (the worker emits these together to onCellLoaded). */
  struct Entry {
    s52sg::Buffer buffer;
    double north = 0, south = 0, east = 0, west = 0;
  };

  /** Default footprint budget. Overridable per process via the
   *  OCPN_QT_CHART_CACHE_MB environment variable (e.g. lower it on a Pi). */
  static constexpr qint64 kDefaultBudgetBytes = 128LL * 1024 * 1024;

  explicit DecodedCellCache(qint64 budget_bytes = 0);

  /** On a hit (path present and `mtime` matches the stored mtime) copy the
   *  decoded entry into *out, mark it most-recently-used, and return true.
   *  A stale (mtime-mismatched) entry is dropped and reported as a miss. */
  bool get(const QString& path, qint64 mtime, Entry* out);

  /** Insert or replace `path`'s decoded entry, then evict least-recently-used
   *  entries until the total approximate footprint is within budget. */
  void put(const QString& path, qint64 mtime, const Entry& entry);

  /** Drop every entry (after a display-setting / colour-scheme change). */
  void clear();

  qint64 bytes() const { return m_bytes; }
  int count() const { return m_map.size(); }
  qint64 budget() const { return m_budget; }

private:
  struct Node {
    qint64 mtime = 0;
    Entry entry;
    qint64 bytes = 0;  // cached approxBytes(entry.buffer) at insert time
  };
  void touch(const QString& path);  // move to MRU front of m_lru
  void evictToBudget();

  QHash<QString, Node> m_map;
  QList<QString> m_lru;  // front = most recently used, back = LRU victim
  qint64 m_bytes = 0;
  qint64 m_budget;
};

/** Rough in-memory footprint of a decoded buffer, for the cache budget. An
 *  over-estimate when Qt COW containers are shared with a live provider, which
 *  only makes eviction more conservative. Exposed for tests. */
qint64 approxDecodedBytes(const s52sg::Buffer& buf);

}  // namespace ocpn::qtui

#endif  // OCPN_QT_DECODED_CELL_CACHE_H_
