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

#include <QDir>

#include "model/base_platform.h"  // g_BasePlatform

namespace ocpn::qtui {

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
               "scale INTEGER, band INTEGER, navfeatures INTEGER)",
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
                         "navfeatures FROM chart_catalog WHERE path=?1",
                         -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW &&
        sqlite3_column_int64(st, 0) == mtime) {
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
      hit = true;
    }
  }
  sqlite3_finalize(st);
  return hit;
}

void ChartCatalogCache::put(const CellExtent& ce, qint64 mtime) {
  if (!m_db) return;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(
          m_db,
          "INSERT INTO chart_catalog"
          "(path,mtime,name,north,south,east,west,scale,band,navfeatures) "
          "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) "
          "ON CONFLICT(path) DO UPDATE SET mtime=?2,name=?3,north=?4,south=?5,"
          "east=?6,west=?7,scale=?8,band=?9,navfeatures=?10",
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
    sqlite3_step(st);
  }
  sqlite3_finalize(st);
}

}  // namespace ocpn::qtui
