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
 * Implement in_memory_ais_store.h.
 */

#include "in_memory_ais_store.h"

namespace ocpn::qtui {

void InMemoryAisTargetStore::upsert(const AisTarget& t, qint64 now_ms) {
  QWriteLocker lock(&m_lock);
  Entry& e = m_targets[t.mmsi];
  e.target = t;
  e.last_seen = now_ms;
}

QList<AisTarget> InMemoryAisTargetStore::snapshot() const {
  QReadLocker lock(&m_lock);
  QList<AisTarget> out;
  out.reserve(m_targets.size());
  for (const Entry& e : m_targets) out.append(e.target);
  return out;
}

void InMemoryAisTargetStore::prune(qint64 now_ms, qint64 max_age_ms) {
  QWriteLocker lock(&m_lock);
  for (auto it = m_targets.begin(); it != m_targets.end();) {
    if (now_ms - it.value().last_seen > max_age_ms)
      it = m_targets.erase(it);
    else
      ++it;
  }
}

int InMemoryAisTargetStore::count() const {
  QReadLocker lock(&m_lock);
  return static_cast<int>(m_targets.size());
}

}  // namespace ocpn::qtui
