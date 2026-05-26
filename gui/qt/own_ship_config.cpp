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
  ConfigStore& c = ConfigStore::instance();
  m_name = c.getString("ownship/name");
  m_mmsi = c.getString("ownship/mmsi");
  m_icon_type = c.getInt("ownship/iconType", m_icon_type);
  m_loa = c.getDouble("ownship/loa", m_loa);
  m_beam = c.getDouble("ownship/beam", m_beam);
  m_gps_dx = c.getDouble("ownship/gpsOffsetX", m_gps_dx);
  m_gps_dy = c.getDouble("ownship/gpsOffsetY", m_gps_dy);
  m_min_screen = c.getDouble("ownship/minScreenSize", m_min_screen);
  m_show_wp_dir = c.getBool("ownship/showWaypointDir", m_show_wp_dir);
  m_show_rings = c.getBool("ownship/showRangeRings", m_show_rings);
  m_ring_count = c.getInt("ownship/ringCount", m_ring_count);
  m_ring_spacing = c.getDouble("ownship/ringSpacing", m_ring_spacing);
  m_ring_unit = c.getInt("ownship/ringUnit", m_ring_unit);
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

// Display-attribute setters: guard, store, persist, notify.
#define OCPN_OS_SET(member, value, key, putter) \
  if ((member) == (value)) return;              \
  (member) = (value);                           \
  ConfigStore::instance().putter(key, value);   \
  Q_EMIT changed();

void OwnShipConfig::setIconType(int v) {
  OCPN_OS_SET(m_icon_type, v, "ownship/iconType", setInt)
}
void OwnShipConfig::setLoa(double v) {
  OCPN_OS_SET(m_loa, v, "ownship/loa", setDouble)
}
void OwnShipConfig::setBeam(double v) {
  OCPN_OS_SET(m_beam, v, "ownship/beam", setDouble)
}
void OwnShipConfig::setGpsOffsetX(double v) {
  OCPN_OS_SET(m_gps_dx, v, "ownship/gpsOffsetX", setDouble)
}
void OwnShipConfig::setGpsOffsetY(double v) {
  OCPN_OS_SET(m_gps_dy, v, "ownship/gpsOffsetY", setDouble)
}
void OwnShipConfig::setMinScreenSize(double v) {
  OCPN_OS_SET(m_min_screen, v, "ownship/minScreenSize", setDouble)
}
void OwnShipConfig::setShowWaypointDirection(bool v) {
  OCPN_OS_SET(m_show_wp_dir, v, "ownship/showWaypointDir", setBool)
}
void OwnShipConfig::setShowRangeRings(bool v) {
  OCPN_OS_SET(m_show_rings, v, "ownship/showRangeRings", setBool)
}
void OwnShipConfig::setRingCount(int v) {
  OCPN_OS_SET(m_ring_count, v, "ownship/ringCount", setInt)
}
void OwnShipConfig::setRingSpacing(double v) {
  OCPN_OS_SET(m_ring_spacing, v, "ownship/ringSpacing", setDouble)
}
void OwnShipConfig::setRingUnit(int v) {
  OCPN_OS_SET(m_ring_unit, v, "ownship/ringUnit", setInt)
}

#undef OCPN_OS_SET

}  // namespace ocpn::qtui
