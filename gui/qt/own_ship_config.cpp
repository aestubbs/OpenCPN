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
 * Implement own_ship_config.h.
 */

#include "own_ship_config.h"

#include "config_store.h"

namespace ocpn::qtui {

OwnShipConfig& OwnShipConfig::instance() {
  static OwnShipConfig s;
  return s;
}

OwnShipConfig::OwnShipConfig() {
  m_name = ConfigStore::instance().getString("ownship/name");
  m_mmsi = ConfigStore::instance().getString("ownship/mmsi");
}

void OwnShipConfig::setVesselName(const QString& name) {
  if (name == m_name) return;
  m_name = name;
  ConfigStore::instance().setString("ownship/name", name);
  Q_EMIT changed();
}

void OwnShipConfig::setMmsi(const QString& mmsi) {
  const QString trimmed = mmsi.trimmed();
  if (trimmed == m_mmsi) return;
  m_mmsi = trimmed;
  ConfigStore::instance().setString("ownship/mmsi", trimmed);
  Q_EMIT changed();
}

int OwnShipConfig::mmsiValue() const {
  bool ok = false;
  const int v = m_mmsi.toInt(&ok);
  return ok ? v : 0;
}

}  // namespace ocpn::qtui
