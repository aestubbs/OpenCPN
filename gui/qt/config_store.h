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
 * ConfigStore -- a tiny key/value settings store kept in a `qt_config` table
 * inside the SAME SQLite file as the model's navobj DB (navobj.db, in the
 * platform private-data dir). Keeping app config there means routes, tracks
 * and settings all live in one file (#35). Its own sqlite3 connection (with a
 * busy timeout) is used so it doesn't touch NavObj_dB's handle; config writes
 * are infrequent so contention is negligible.
 */

#ifndef OCPN_QT_CONFIG_STORE_H_
#define OCPN_QT_CONFIG_STORE_H_

#include <QString>

struct sqlite3;

namespace ocpn::qtui {

class ConfigStore {
public:
  static ConfigStore& instance();

  QString getString(const QString& key, const QString& def = QString()) const;
  void setString(const QString& key, const QString& value);
  int getInt(const QString& key, int def) const;
  void setInt(const QString& key, int value);
  bool getBool(const QString& key, bool def) const;
  void setBool(const QString& key, bool value);
  double getDouble(const QString& key, double def) const;
  void setDouble(const QString& key, double value);

private:
  ConfigStore();
  ~ConfigStore();
  ConfigStore(const ConfigStore&) = delete;
  ConfigStore& operator=(const ConfigStore&) = delete;

  sqlite3* m_db = nullptr;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CONFIG_STORE_H_
