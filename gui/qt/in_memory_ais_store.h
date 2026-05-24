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
 * InMemoryAisTargetStore -- a thread-safe, MMSI-keyed in-memory AisTargetStore
 * (P2.11). A QHash guarded by a QReadWriteLock: the decode side upserts
 * (write lock), the GUI side snapshots (read lock). Each entry carries a
 * last-seen timestamp so prune() can drop stale targets.
 */

#ifndef OCPN_QT_IN_MEMORY_AIS_STORE_H_
#define OCPN_QT_IN_MEMORY_AIS_STORE_H_

#include <QHash>
#include <QReadWriteLock>

#include "ais_target_store.h"

namespace ocpn::qtui {

class InMemoryAisTargetStore : public AisTargetStore {
public:
  void upsert(const AisTarget& t, qint64 now_ms) override;
  QList<AisTarget> snapshot() const override;
  void prune(qint64 now_ms, qint64 max_age_ms) override;
  int count() const override;

private:
  struct Entry {
    AisTarget target;
    qint64 last_seen = 0;
  };
  QHash<int, Entry> m_targets;       // by MMSI
  mutable QReadWriteLock m_lock;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_IN_MEMORY_AIS_STORE_H_
