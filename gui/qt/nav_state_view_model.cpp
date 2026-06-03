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
 * Implement nav_state_view_model.h.
 */

#include "nav_state_view_model.h"

#include "display_config.h"
#include "nav_data_provider.h"

namespace ocpn::qtui {

NavStateViewModel::NavStateViewModel(NavDataProvider* provider,
                                     QObject* parent)
    : QObject(parent), m_provider(provider) {
  if (m_provider) {
    connect(m_provider, &NavDataProvider::dynamicChanged, this,
            &NavStateViewModel::refresh);
    refresh();
  }
  // Re-emit when units / lat-lon format / bearing mode change so the HUD and
  // status bar re-read the formatted text without a fresh nav fix.
  connect(&DisplayConfig::instance(), &DisplayConfig::changed, this,
          &NavStateViewModel::changed);
}

void NavStateViewModel::refresh() {
  if (!m_provider) return;
  m_own = m_provider->ownShip();
  Q_EMIT changed();
}

QString NavStateViewModel::positionText() const {
  if (!m_own.valid) return QStringLiteral("---");
  return DisplayConfig::instance().formatLatLon(m_own.lat, m_own.lon);
}

QString NavStateViewModel::sogText() const {
  if (!m_own.valid) return QStringLiteral("--.- kn");
  return DisplayConfig::instance().formatSpeed(m_own.sog);
}

QString NavStateViewModel::cogText() const {
  if (!m_own.valid) return QStringLiteral("---°");
  return DisplayConfig::instance().formatBearing(m_own.cog);
}

QString NavStateViewModel::hdgText() const {
  if (!m_own.valid || m_own.hdg >= 360.0) return QStringLiteral("---°");
  return DisplayConfig::instance().formatBearing(m_own.hdg);
}

}  // namespace ocpn::qtui
