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

#include "nav_data_provider.h"
#include "nav_format.h"

namespace ocpn::qtui {

NavStateViewModel::NavStateViewModel(NavDataProvider* provider,
                                     QObject* parent)
    : QObject(parent), m_provider(provider) {
  if (m_provider) {
    connect(m_provider, &NavDataProvider::dynamicChanged, this,
            &NavStateViewModel::refresh);
    refresh();
  }
}

void NavStateViewModel::refresh() {
  if (!m_provider) return;
  m_own = m_provider->ownShip();
  m_ais_count = static_cast<int>(m_provider->aisTargets().size());
  Q_EMIT changed();
}

QString NavStateViewModel::positionText() const {
  if (!m_own.valid) return QStringLiteral("---");
  return navfmt::latLon(m_own.lat, m_own.lon);
}

QString NavStateViewModel::sogText() const {
  if (!m_own.valid) return QStringLiteral("--.- kn");
  return navfmt::sog(m_own.sog);
}

QString NavStateViewModel::cogText() const {
  if (!m_own.valid) return QStringLiteral("---°");
  return navfmt::cog(m_own.cog);
}

}  // namespace ocpn::qtui
