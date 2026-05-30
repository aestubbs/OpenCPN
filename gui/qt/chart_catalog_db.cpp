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
 * Implement chart_catalog_db.h.
 */

#include "chart_catalog_db.h"

#include <sqlite3.h>

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QIODevice>

#include "model/base_platform.h"  // g_BasePlatform

namespace ocpn::qtui {

namespace {
// Serialise the cell's M_COVR coverage polygons (QList<QPolygonF>, lon/lat)
// into a compact blob for the cache, and back. QDataStream handles the nested
// QList<QPolygonF> directly; the version is pinned so a future Qt can still
// read old rows.
QByteArray serializeCoverage(const QList<QPolygonF>& cov) {
  QByteArray out;
  QDataStream ds(&out, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << cov;
  return out;
}
QList<QPolygonF> deserializeCoverage(const void* data, int len) {
  QList<QPolygonF> cov;
  if (!data || len <= 0) return cov;
  const QByteArray buf(static_cast<const char*>(data), len);
  QDataStream ds(buf);
  ds.setVersion(QDataStream::Qt_5_15);
  ds >> cov;
  return cov;
}
}  // namespace

ChartCatalogCache::ChartCatalogCache() {
  if (!g_BasePlatform) return;
  const QString dir =
      QString::fromStdString(g_BasePlatform->GetPrivateDataDir().ToStdString());
  const QString path = dir + QDir::separator() + QStringLiteral("navobj.db");
  if (sqlite3_open_v2(path.toUtf8().constData(), &m_db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    m_db = nullptr;
    return;
  }
  sqlite3_busy_timeout(m_db, 5000);
  sqlite3_exec(m_db,
               "CREATE TABLE IF NOT EXISTS chart_catalog ("
               "path TEXT PRIMARY KEY, mtime INTEGER, name TEXT, "
               "north REAL, south REAL, east REAL, west REAL, "
               "scale INTEGER, band INTEGER, navfeatures INTEGER, "
               "coverage BLOB)",
               nullptr, nullptr, nullptr);
  // Migrate an existing (pre-coverage) table: add the column if missing. The
  // ALTER errors harmlessly when the column already exists -- ignore it. Old
  // rows get a NULL coverage, which get() treats as a miss so they re-scan
  // once and back-fill their real M_COVR polygons.
  sqlite3_exec(m_db, "ALTER TABLE chart_catalog ADD COLUMN coverage BLOB",
               nullptr, nullptr, nullptr);
}

ChartCatalogCache::~ChartCatalogCache() {
  if (m_db) sqlite3_close(m_db);
}

bool ChartCatalogCache::get(const QString& path, qint64 mtime,
                            CellExtent& out) const {
  if (!m_db) return false;
  sqlite3_stmt* st = nullptr;
  bool hit = false;
  if (sqlite3_prepare_v2(m_db,
                         "SELECT mtime,name,north,south,east,west,scale,band,"
                         "navfeatures,coverage FROM chart_catalog WHERE path=?1",
                         -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    // A NULL coverage column is a pre-coverage row: treat it as a miss so the
    // caller re-scans the cell and back-fills its M_COVR polygons (without
    // them the quilt falls back to the bbox -- the cause of fine cells blanking
    // out the rest of their bounding box).
    if (sqlite3_step(st) == SQLITE_ROW &&
        sqlite3_column_int64(st, 0) == mtime &&
        sqlite3_column_type(st, 9) != SQLITE_NULL) {
      out.path = path;
      out.name = QString::fromUtf8(
          reinterpret_cast<const char*>(sqlite3_column_text(st, 1)));
      out.north = sqlite3_column_double(st, 2);
      out.south = sqlite3_column_double(st, 3);
      out.east = sqlite3_column_double(st, 4);
      out.west = sqlite3_column_double(st, 5);
      out.nativeScale = sqlite3_column_int(st, 6);
      out.band = sqlite3_column_int(st, 7);
      out.navFeatures = sqlite3_column_int(st, 8);
      out.coverage = deserializeCoverage(sqlite3_column_blob(st, 9),
                                         sqlite3_column_bytes(st, 9));
      hit = true;
    }
  }
  sqlite3_finalize(st);
  return hit;
}

void ChartCatalogCache::put(const CellExtent& ce, qint64 mtime) {
  if (!m_db) return;
  sqlite3_stmt* st = nullptr;
  const QByteArray cov = serializeCoverage(ce.coverage);
  if (sqlite3_prepare_v2(
          m_db,
          "INSERT INTO chart_catalog"
          "(path,mtime,name,north,south,east,west,scale,band,navfeatures,"
          "coverage) "
          "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11) "
          "ON CONFLICT(path) DO UPDATE SET mtime=?2,name=?3,north=?4,south=?5,"
          "east=?6,west=?7,scale=?8,band=?9,navfeatures=?10,coverage=?11",
          -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, ce.path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, mtime);
    sqlite3_bind_text(st, 3, ce.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 4, ce.north);
    sqlite3_bind_double(st, 5, ce.south);
    sqlite3_bind_double(st, 6, ce.east);
    sqlite3_bind_double(st, 7, ce.west);
    sqlite3_bind_int(st, 8, ce.nativeScale);
    sqlite3_bind_int(st, 9, ce.band);
    sqlite3_bind_int(st, 10, ce.navFeatures);
    // Stored even when empty (o-charts headers carry no M_COVR) so the row is
    // non-NULL and not re-scanned every launch; empty -> covers() uses bbox.
    sqlite3_bind_blob(st, 11, cov.constData(), cov.size(), SQLITE_TRANSIENT);
    sqlite3_step(st);
  }
  sqlite3_finalize(st);
}

}  // namespace ocpn::qtui
