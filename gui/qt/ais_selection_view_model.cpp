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

#include "nav_format.h"

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
  return navfmt::latLon(m_target.lat, m_target.lon);
}

QString AisSelectionViewModel::sogText() const {
  return navfmt::sog(m_target.sog);
}

QString AisSelectionViewModel::cogText() const {
  return navfmt::cog(m_target.cog);
}

}  // namespace ocpn::qtui
