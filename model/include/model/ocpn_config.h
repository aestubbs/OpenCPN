/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 * \file
 *
 * OcpnConfig -- Qt-based application configuration object replacing the
 * historical wxFileConfig / wxConfigBase used throughout OpenCPN.
 *
 * The API is a thin wrapper around QSettings(IniFormat). The historical
 * wxConfig-compatibility shim that bridged the migration was removed at the
 * end of P1.9; all call sites now use the Qt-idiomatic surface below.
 */

#ifndef OCPN_CONFIG_H_
#define OCPN_CONFIG_H_

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

class OcpnConfig {
public:
  /** Construct backed by an INI-format QSettings file at `filePath`. */
  explicit OcpnConfig(const QString& filePath);
  virtual ~OcpnConfig();

  OcpnConfig(const OcpnConfig&) = delete;
  OcpnConfig& operator=(const OcpnConfig&) = delete;

  // ---------------------------------------------------------------------
  //  Qt-idiomatic API (mirrors QSettings)
  // ---------------------------------------------------------------------
  QVariant value(const QString& key,
                 const QVariant& defaultValue = QVariant()) const;
  void setValue(const QString& key, const QVariant& value);
  bool contains(const QString& key) const;
  void remove(const QString& key);

  void beginGroup(const QString& prefix);
  void endGroup();
  /** Pop every open group, returning to the root. Useful for re-anchoring
   *  in long load/save functions that switch between sibling group trees. */
  void endAllGroups();
  QString group() const;

  QStringList childKeys() const;
  QStringList childGroups() const;

  void sync();

private:
  QSettings m_settings;

  // Current group depth, so endAllGroups() can pop without overshooting.
  int m_group_depth = 0;
};

#endif  // OCPN_CONFIG_H_
