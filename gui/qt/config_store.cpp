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
 * Implement config_store.h.
 */

#include "config_store.h"

#include <sqlite3.h>

#include <QDir>

#include "model/base_platform.h"  // g_BasePlatform

namespace ocpn::qtui {

ConfigStore& ConfigStore::instance() {
  static ConfigStore s;
  return s;
}

ConfigStore::ConfigStore() {
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
  sqlite3_busy_timeout(m_db, 3000);  // wait out NavObj_dB transactions
  sqlite3_exec(m_db,
               "CREATE TABLE IF NOT EXISTS qt_config ("
               "key TEXT PRIMARY KEY, value TEXT)",
               nullptr, nullptr, nullptr);
}

ConfigStore::~ConfigStore() {
  if (m_db) sqlite3_close(m_db);
}

QString ConfigStore::getString(const QString& key, const QString& def) const {
  if (!m_db) return def;
  sqlite3_stmt* st = nullptr;
  QString out = def;
  if (sqlite3_prepare_v2(m_db, "SELECT value FROM qt_config WHERE key=?1", -1,
                         &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
      if (const unsigned char* v = sqlite3_column_text(st, 0))
        out = QString::fromUtf8(reinterpret_cast<const char*>(v));
    }
  }
  sqlite3_finalize(st);
  return out;
}

void ConfigStore::setString(const QString& key, const QString& value) {
  if (!m_db) return;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(m_db,
                         "INSERT INTO qt_config(key,value) VALUES(?1,?2) "
                         "ON CONFLICT(key) DO UPDATE SET value=?2",
                         -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, value.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
  }
  sqlite3_finalize(st);
}

int ConfigStore::getInt(const QString& key, int def) const {
  const QString s = getString(key);
  if (s.isEmpty()) return def;
  bool ok = false;
  const int v = s.toInt(&ok);
  return ok ? v : def;
}

void ConfigStore::setInt(const QString& key, int value) {
  setString(key, QString::number(value));
}

}  // namespace ocpn::qtui
