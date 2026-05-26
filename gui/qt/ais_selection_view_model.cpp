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
 * Implement ais_selection_view_model.h.
 */

#include "ais_selection_view_model.h"

#include <QChar>

#include "display_config.h"

namespace ocpn::qtui {

void AisSelectionViewModel::select(const AisTarget& target) {
  m_target = target;
  m_valid = true;
  Q_EMIT changed();
}

void AisSelectionViewModel::clear() {
  if (!m_valid) return;
  m_valid = false;
  Q_EMIT changed();
}

QString AisSelectionViewModel::positionText() const {
  return DisplayConfig::instance().formatLatLon(m_target.lat, m_target.lon);
}

QString AisSelectionViewModel::sogText() const {
  return DisplayConfig::instance().formatSpeed(m_target.sog);
}

QString AisSelectionViewModel::cogText() const {
  return DisplayConfig::instance().formatBearing(m_target.cog);
}

QString AisSelectionViewModel::rangeText() const {
  if (m_target.rangeNm < 0.0) return QStringLiteral("--");
  return DisplayConfig::instance().formatDistance(m_target.rangeNm);
}

QString AisSelectionViewModel::bearingText() const {
  if (m_target.bearingDeg < 0.0) return QStringLiteral("--");
  return DisplayConfig::instance().formatBearing(m_target.bearingDeg);
}

QString AisSelectionViewModel::cpaText() const {
  if (!m_target.cpaValid) return QStringLiteral("--");
  return DisplayConfig::instance().formatDistance(m_target.cpaNm);
}

QString AisSelectionViewModel::tcpaText() const {
  if (!m_target.cpaValid) return QStringLiteral("--");
  // Minutes:seconds, like wx's target query (e.g. "12:30").
  const int total_s = static_cast<int>(m_target.tcpaMin * 60.0 + 0.5);
  const int m = total_s / 60;
  const int s = total_s % 60;
  return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

void AisSelectionViewModel::refresh(const QList<AisTarget>& targets) {
  if (!m_valid) return;
  for (const AisTarget& t : targets) {
    if (t.mmsi == m_target.mmsi) {
      m_target = t;
      Q_EMIT changed();
      return;
    }
  }
}

}  // namespace ocpn::qtui
