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
 * Implement OcpnConfig.
 */

#include "model/ocpn_config.h"

OcpnConfig::OcpnConfig(const QString& filePath)
    : m_settings(filePath, QSettings::IniFormat) {}

OcpnConfig::~OcpnConfig() { m_settings.sync(); }

QVariant OcpnConfig::value(const QString& key,
                           const QVariant& defaultValue) const {
  return m_settings.value(key, defaultValue);
}

void OcpnConfig::setValue(const QString& key, const QVariant& value) {
  m_settings.setValue(key, value);
}

bool OcpnConfig::contains(const QString& key) const {
  return m_settings.contains(key);
}

void OcpnConfig::remove(const QString& key) { m_settings.remove(key); }

void OcpnConfig::beginGroup(const QString& prefix) {
  m_settings.beginGroup(prefix);
  ++m_group_depth;
}

void OcpnConfig::endGroup() {
  if (m_group_depth <= 0) return;
  m_settings.endGroup();
  --m_group_depth;
}

void OcpnConfig::endAllGroups() {
  while (m_group_depth > 0) {
    m_settings.endGroup();
    --m_group_depth;
  }
}

QString OcpnConfig::group() const { return m_settings.group(); }

QStringList OcpnConfig::childKeys() const { return m_settings.childKeys(); }

QStringList OcpnConfig::childGroups() const {
  return m_settings.childGroups();
}

void OcpnConfig::sync() { m_settings.sync(); }
