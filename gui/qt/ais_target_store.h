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
 * AisTargetStore -- the renderer's interface to live AIS target state
 * (P2.11). This is the clean boundary the discussion settled on: the chart
 * reads targets through this, decoupled from the legacy AisDecoder.
 *
 * Today the only implementation is InMemoryAisTargetStore (a thread-safe
 * MMSI-keyed hash). A SQLite-backed implementation can drop in behind the
 * same interface later if we want target history / record-replay / spatial
 * queries -- AIS rate is bounded by the VHF SOTDMA channel (a few hundred
 * reports/sec at absolute peak), so either backing scales fine.
 *
 * Staleness is timestamp-based and owned by the store: callers upsert with a
 * "now" timestamp, and prune() drops targets not seen within a max age --
 * independent of any decoder's lifecycle logic.
 *
 * Designed to be written from a decode thread and read from the GUI thread,
 * so implementations must be thread-safe.
 */

#ifndef OCPN_QT_AIS_TARGET_STORE_H_
#define OCPN_QT_AIS_TARGET_STORE_H_

#include <QList>

#include "nav_data.h"

namespace ocpn::qtui {

class AisTargetStore {
public:
  virtual ~AisTargetStore() = default;

  /** Insert or update `t` (keyed by MMSI), stamping its last-seen time with
   *  `now_ms` (monotonic-ish epoch ms). */
  virtual void upsert(const AisTarget& t, qint64 now_ms) = 0;

  /** A snapshot of the current targets, for rendering. */
  virtual QList<AisTarget> snapshot() const = 0;

  /** Drop targets whose last-seen time is older than `max_age_ms` before
   *  `now_ms` (staleness sweep). */
  virtual void prune(qint64 now_ms, qint64 max_age_ms) = 0;

  /** Number of targets currently held (diagnostics). */
  virtual int count() const = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_AIS_TARGET_STORE_H_
