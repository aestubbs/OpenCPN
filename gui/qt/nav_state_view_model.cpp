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

#include <cmath>

#include "nav_data_provider.h"

namespace ocpn::qtui {

namespace {
// Format a signed degree value as D°M.m' with a hemisphere suffix.
QString fmtCoord(double deg, int degWidth, QChar pos, QChar neg) {
  const QChar hemi = deg >= 0.0 ? pos : neg;
  deg = std::abs(deg);
  const int d = static_cast<int>(deg);
  const double m = (deg - d) * 60.0;
  return QStringLiteral("%1°%2'%3")
      .arg(d, degWidth, 10, QChar('0'))
      .arg(m, 5, 'f', 2, QChar('0'))
      .arg(hemi);
}
}  // namespace

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
  return fmtCoord(m_own.lat, 2, QChar('N'), QChar('S')) + QStringLiteral("  ") +
         fmtCoord(m_own.lon, 3, QChar('E'), QChar('W'));
}

QString NavStateViewModel::sogText() const {
  if (!m_own.valid) return QStringLiteral("--.- kn");
  return QStringLiteral("%1 kn").arg(m_own.sog, 0, 'f', 1);
}

QString NavStateViewModel::cogText() const {
  if (!m_own.valid) return QStringLiteral("---°");
  return QStringLiteral("%1°").arg(m_own.cog, 3, 'f', 0, QChar('0'));
}

}  // namespace ocpn::qtui
