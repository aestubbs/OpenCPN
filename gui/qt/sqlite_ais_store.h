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
 * SqliteAisTargetStore -- an AisTargetStore that persists targets to an
 * `ais_targets` table in the SQLite navobj DB (#40), so previously-seen
 * vessels reappear instantly on restart (subject to the display staleness
 * window) without waiting to rebuild from the feed.
 *
 * An in-memory cache backs render reads (fast); writes are batched to the DB
 * on the prune tick to avoid one write per report. The table grows to capture
 * every vessel seen, capped at a fixed row count (oldest last_seen evicted).
 * On construction it loads the recently-seen rows into the cache; prune()
 * trims the cache to the caller's display window while the DB keeps history.
 */

#ifndef OCPN_QT_SQLITE_AIS_STORE_H_
#define OCPN_QT_SQLITE_AIS_STORE_H_

#include <QHash>
#include <QMutex>
#include <QPointF>
#include <QSet>
#include <QVector>

#include "ais_target_store.h"

struct sqlite3;

namespace ocpn::qtui {

class SqliteAisTargetStore : public AisTargetStore {
public:
  SqliteAisTargetStore();
  ~SqliteAisTargetStore() override;

  void upsert(const AisTarget& t, qint64 now_ms) override;
  QList<AisTarget> snapshot() const override;
  void prune(qint64 now_ms, qint64 max_age_ms) override;
  int count() const override;
  QVector<AisTrackPoint> trackSince(int mmsi, qint64 since_ms) const override;

private:
  void flushDirtyLocked();   // write dirty cache rows to the DB
  void enforceCapLocked();   // drop oldest rows beyond the cap
  void flushTracksLocked();  // append buffered trail points to ais_track
  void purgeTracksLocked(qint64 now_ms);  // drop trail rows past retention

  struct Entry {
    AisTarget target;
    qint64 last_seen = 0;
  };
  // A buffered trail point awaiting a batched DB write.
  struct TrackRow {
    int mmsi = 0;
    qint64 t = 0;
    double lat = 0.0;
    double lon = 0.0;
    double cog = -1.0;
    double sog = -1.0;
    double hdg = 511.0;
  };
  QHash<int, Entry> m_targets;  // by MMSI (render cache)
  QSet<int> m_dirty;            // MMSIs needing a DB write
  QVector<TrackRow> m_track_pending;     // trail points not yet flushed
  QHash<int, QPointF> m_track_last;      // last recorded pos per MMSI (dedup)
  mutable QMutex m_mutex;
  sqlite3* m_db = nullptr;
  int m_writes_since_cap = 0;
  int m_flushes_since_purge = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_SQLITE_AIS_STORE_H_
