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
 * Implement tide_graph_view_model.h.
 */

#include "tide_graph_view_model.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include <QDateTime>
#include <QVariantMap>

#include "display_config.h"
#include "idx_entry.h"
#include "tcmgr.h"  // TCMgr + global ptcmgr (libs/tides)
#include "time_controller.h"

namespace ocpn::qtui {

namespace {
constexpr double kKnotToKmh = 1.852;
constexpr double kKnotToMph = 1.150779448;

bool sampleRaw(int idx, bool is_current, time_t t, float& val) {
  float dir = 0.0f;
  return is_current ? ptcmgr->GetTideOrCurrent(t, idx, val, dir)
                    : ptcmgr->GetTideOrCurrentMeters(t, idx, val, dir);
}
}  // namespace

TideGraphViewModel::TideGraphViewModel(QObject* parent) : QObject(parent) {
  // The fixed marker reads the value at the shared display time.
  connect(&TimeController::instance(), &TimeController::timeChanged, this,
          &TideGraphViewModel::recomputeMarker);
}

double TideGraphViewModel::toUser(float raw) const {
  DisplayConfig& dc = DisplayConfig::instance();
  if (m_is_current) {
    switch (dc.speedUnit()) {
      case 1: return raw * kKnotToKmh;  // km/h
      case 2: return raw * kKnotToMph;  // mph
      default: return raw;              // knots
    }
  }
  return dc.heightUnit() == 1 ? raw / 0.3048 : raw;  // ft : m
}

void TideGraphViewModel::clear() {
  if (m_idx < 0) return;
  m_idx = -1;
  Q_EMIT changed();
  Q_EMIT markerChanged();
}

void TideGraphViewModel::select(int idx) {
  if (!ptcmgr || !ptcmgr->IsReady()) return;
  const IDX_entry* e = ptcmgr->GetIDX_entry(idx);
  if (!e) return;
  m_idx = idx;
  m_name = QString::fromUtf8(e->IDX_station_name);
  const char ty = e->IDX_type;
  m_is_current = (ty == 'c' || ty == 'C');

  DisplayConfig& dc = DisplayConfig::instance();
  if (m_is_current) {
    switch (dc.speedUnit()) {
      case 1: m_unit_label = QStringLiteral("km/h"); break;
      case 2: m_unit_label = QStringLiteral("mph"); break;
      default: m_unit_label = QStringLiteral("kn"); break;
    }
  } else {
    m_unit_label =
        dc.heightUnit() == 1 ? QStringLiteral("ft") : QStringLiteral("m");
  }
  recomputeRange();
  recomputeMarker();
  Q_EMIT changed();
}

void TideGraphViewModel::recomputeRange() {
  // Scan ±18h around "now" at 15-min steps so the y-axis stays stable as the
  // user pans (rather than rescaling to each window).
  if (m_idx < 0 || !ptcmgr) {
    m_min = 0.0;
    m_max = 1.0;
    return;
  }
  const time_t now = time(nullptr);
  double lo = 1e9, hi = -1e9;
  for (int k = -18 * 4; k <= 18 * 4; ++k) {
    float val = 0.0f;
    if (!sampleRaw(m_idx, m_is_current, now + k * 15 * 60, val)) continue;
    const double u = toUser(val);
    lo = std::min(lo, u);
    hi = std::max(hi, u);
  }
  if (lo > hi) {
    lo = 0.0;
    hi = 1.0;
  }
  if (m_is_current) {  // currents straddle zero -> symmetric range
    const double m = std::max(std::fabs(lo), std::fabs(hi));
    lo = -m;
    hi = m;
  }
  const double margin = std::max(0.2, (hi - lo) * 0.1);
  m_min = lo - margin;
  m_max = hi + margin;
}

void TideGraphViewModel::recomputeMarker() {
  if (m_idx < 0 || !ptcmgr) {
    m_marker_value = 0.0;
    m_marker_text.clear();
    Q_EMIT markerChanged();
    return;
  }
  const time_t t = static_cast<time_t>(
      TimeController::instance().displayTime().toSecsSinceEpoch());
  float val = 0.0f;
  if (!sampleRaw(m_idx, m_is_current, t, val)) {
    m_marker_value = 0.0;
    m_marker_text.clear();
    Q_EMIT markerChanged();
    return;
  }
  m_marker_value = toUser(val);
  m_marker_text =
      m_is_current
          ? DisplayConfig::instance().formatSpeed(std::fabs(val))
          : QString::number(m_marker_value, 'f', 1) + QStringLiteral(" ") +
                m_unit_label;
  Q_EMIT markerChanged();
}

QVariantList TideGraphViewModel::samples(double startMs, double endMs,
                                         int n) const {
  QVariantList out;
  if (m_idx < 0 || !ptcmgr || n < 2 || endMs <= startMs) return out;
  out.reserve(n);
  const double startS = startMs / 1000.0, endS = endMs / 1000.0;
  for (int i = 0; i < n; ++i) {
    const time_t t =
        static_cast<time_t>(startS + (endS - startS) * i / (n - 1));
    float val = 0.0f;
    out.append(sampleRaw(m_idx, m_is_current, t, val) ? toUser(val) : 0.0);
  }
  return out;
}

QVariantList TideGraphViewModel::events(double startMs, double endMs) const {
  QVariantList out;
  if (m_idx < 0 || !ptcmgr) return out;
  const time_t start = static_cast<time_t>(startMs / 1000.0);
  const time_t end = static_cast<time_t>(endMs / 1000.0);
  time_t tm = start;
  for (int guard = 0; tm < end && guard < 200; ++guard) {
    const time_t prev = tm;
    const int flags = ptcmgr->GetNextBigEvent(&tm, m_idx);
    if (flags == 0) break;
    if (tm <= prev) {  // never stall
      tm = prev + 60;
      continue;
    }
    if (tm >= end) break;
    if (tm < start) continue;
    float val = 0.0f;
    const bool ok = sampleRaw(m_idx, m_is_current, tm, val);
    QVariantMap ev;
    ev[QStringLiteral("t")] = static_cast<double>(tm) * 1000.0;
    ev[QStringLiteral("v")] = ok ? toUser(val) : 0.0;
    if (m_is_current)
      ev[QStringLiteral("type")] =
          (ok && val >= 0.0f) ? QStringLiteral("Flood") : QStringLiteral("Ebb");
    else
      ev[QStringLiteral("type")] =
          (flags & 2) ? QStringLiteral("HW") : QStringLiteral("LW");
    out.append(ev);
  }
  return out;
}

}  // namespace ocpn::qtui
