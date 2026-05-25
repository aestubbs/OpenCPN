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
 * Implement sqlite_ais_store.h.
 */

#include "sqlite_ais_store.h"

#include <sqlite3.h>

#include <string>

#include <QByteArray>
#include <QDateTime>
#include <QDir>

#include "model/base_platform.h"  // g_BasePlatform

namespace ocpn::qtui {

namespace {
// Load targets seen within this window on startup (a little wider than the
// display staleness; prune() trims to the caller's exact window).
constexpr qint64 kLoadWindowMs = 20 * 60 * 1000;
// Keep at most this many rows (oldest last_seen evicted) -- the history grows
// to capture vessels seen but is bounded.
constexpr int kMaxRows = 8000;
constexpr int kCapInterval = 256;  // enforce the cap every N flushes
}  // namespace

SqliteAisTargetStore::SqliteAisTargetStore() {
  if (!g_BasePlatform) return;
  const QString path =
      QString::fromStdString(g_BasePlatform->GetPrivateDataDir().ToStdString()) +
      QDir::separator() + QStringLiteral("navobj.db");
  if (sqlite3_open_v2(path.toUtf8().constData(), &m_db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    m_db = nullptr;
    return;
  }
  sqlite3_busy_timeout(m_db, 3000);
  sqlite3_exec(m_db,
               "CREATE TABLE IF NOT EXISTS ais_targets ("
               "mmsi INTEGER PRIMARY KEY, lat REAL, lon REAL, cog REAL, "
               "sog REAL, hdg REAL, name TEXT, last_seen INTEGER, "
               "shiptype INTEGER DEFAULT 0)",
               nullptr, nullptr, nullptr);
  // Migrate older DBs (no-op error if the column already exists).
  sqlite3_exec(m_db,
               "ALTER TABLE ais_targets ADD COLUMN shiptype INTEGER DEFAULT 0",
               nullptr, nullptr, nullptr);
  sqlite3_exec(m_db,
               "CREATE INDEX IF NOT EXISTS ais_last_seen "
               "ON ais_targets(last_seen)",
               nullptr, nullptr, nullptr);

  // Load the recently-seen rows into the render cache so they show at once.
  const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kLoadWindowMs;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(
          m_db,
          "SELECT mmsi,lat,lon,cog,sog,hdg,name,last_seen,shiptype "
          "FROM ais_targets WHERE last_seen >= ?1",
          -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, cutoff);
    while (sqlite3_step(st) == SQLITE_ROW) {
      Entry e;
      e.target.mmsi = sqlite3_column_int(st, 0);
      e.target.lat = sqlite3_column_double(st, 1);
      e.target.lon = sqlite3_column_double(st, 2);
      e.target.cog = sqlite3_column_double(st, 3);
      e.target.sog = sqlite3_column_double(st, 4);
      e.target.hdg = sqlite3_column_double(st, 5);
      if (const unsigned char* n = sqlite3_column_text(st, 6))
        e.target.name = QString::fromUtf8(reinterpret_cast<const char*>(n));
      e.last_seen = sqlite3_column_int64(st, 7);
      e.target.shipType = sqlite3_column_int(st, 8);
      m_targets.insert(e.target.mmsi, e);
    }
  }
  sqlite3_finalize(st);
}

SqliteAisTargetStore::~SqliteAisTargetStore() {
  if (m_db) {
    QMutexLocker lock(&m_mutex);
    flushDirtyLocked();
    sqlite3_close(m_db);
  }
}

void SqliteAisTargetStore::upsert(const AisTarget& t, qint64 now_ms) {
  QMutexLocker lock(&m_mutex);
  Entry& e = m_targets[t.mmsi];
  e.target = t;
  e.last_seen = now_ms;
  m_dirty.insert(t.mmsi);  // written to the DB on the next prune() tick
}

QList<AisTarget> SqliteAisTargetStore::snapshot() const {
  QMutexLocker lock(&m_mutex);
  QList<AisTarget> out;
  out.reserve(m_targets.size());
  for (const Entry& e : m_targets) out.append(e.target);
  return out;
}

void SqliteAisTargetStore::prune(qint64 now_ms, qint64 max_age_ms) {
  QMutexLocker lock(&m_mutex);
  // Persist pending writes (batched) before trimming the render cache.
  flushDirtyLocked();
  // Trim the in-memory render set to the display window; the DB keeps history.
  for (auto it = m_targets.begin(); it != m_targets.end();) {
    if (now_ms - it.value().last_seen > max_age_ms)
      it = m_targets.erase(it);
    else
      ++it;
  }
}

int SqliteAisTargetStore::count() const {
  QMutexLocker lock(&m_mutex);
  return m_targets.size();
}

void SqliteAisTargetStore::flushDirtyLocked() {
  if (!m_db || m_dirty.isEmpty()) return;
  sqlite3_exec(m_db, "BEGIN", nullptr, nullptr, nullptr);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(
          m_db,
          "INSERT INTO ais_targets"
          "(mmsi,lat,lon,cog,sog,hdg,name,last_seen,shiptype) "
          "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) "
          "ON CONFLICT(mmsi) DO UPDATE SET lat=?2,lon=?3,cog=?4,sog=?5,"
          "hdg=?6,name=?7,last_seen=?8,shiptype=?9",
          -1, &st, nullptr) == SQLITE_OK) {
    for (int mmsi : m_dirty) {
      auto it = m_targets.constFind(mmsi);
      if (it == m_targets.cend()) continue;
      const AisTarget& t = it.value().target;
      sqlite3_bind_int(st, 1, t.mmsi);
      sqlite3_bind_double(st, 2, t.lat);
      sqlite3_bind_double(st, 3, t.lon);
      sqlite3_bind_double(st, 4, t.cog);
      sqlite3_bind_double(st, 5, t.sog);
      sqlite3_bind_double(st, 6, t.hdg);
      const QByteArray name = t.name.toUtf8();
      sqlite3_bind_text(st, 7, name.constData(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(st, 8, it.value().last_seen);
      sqlite3_bind_int(st, 9, t.shipType);
      sqlite3_step(st);
      sqlite3_reset(st);
    }
  }
  sqlite3_finalize(st);
  sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr);
  m_dirty.clear();
  if (++m_writes_since_cap >= kCapInterval) {
    m_writes_since_cap = 0;
    enforceCapLocked();
  }
}

void SqliteAisTargetStore::enforceCapLocked() {
  if (!m_db) return;
  // Keep the newest kMaxRows by last_seen; drop the rest.
  const std::string sql =
      "DELETE FROM ais_targets WHERE mmsi NOT IN "
      "(SELECT mmsi FROM ais_targets ORDER BY last_seen DESC LIMIT " +
      std::to_string(kMaxRows) + ")";
  sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, nullptr);
}

}  // namespace ocpn::qtui
