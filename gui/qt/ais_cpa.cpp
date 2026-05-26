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
 * Implement ais_cpa.h -- ported from AisDecoder::UpdateOneCPA /
 * UpdateAllAlarms (model/src/ais_decoder.cpp).
 */

#include "ais_cpa.h"

#include <cmath>

#include "ais_config.h"
#include "model/georef.h"

namespace ocpn::qtui {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
// SOG sentinel: AIS reports 102.2 kn as "not available".
constexpr double kSogMax = 102.2;

void invalidate(AisTarget& t) {
  t.cpaNm = -1.0;
  t.tcpaMin = -1.0;
  t.cpaValid = false;
  t.dangerous = false;
}
}  // namespace

void computeCpa(const OwnShipState& own, AisTarget& t, const AisConfig& cfg) {
  // Range + bearing own -> target (always set when own fix valid).
  if (own.valid) {
    double brg = -1.0, range = -1.0;
    DistanceBearingMercator(t.lat, t.lon, own.lat, own.lon, &brg, &range);
    t.rangeNm = range;
    t.bearingDeg = (range <= 1e-5) ? -1.0 : brg;
  } else {
    t.rangeNm = -1.0;
    t.bearingDeg = -1.0;
  }

  // --- Validity gates (mirror UpdateOneCPA) ---
  if (!own.valid) { invalidate(t); return; }

  double own_cog = own.cog, own_sog = own.sog;
  double tgt_cog = t.cog, tgt_sog = t.sog;

  // Own ship: implausible SOG, or invalid COG while moving, voids the solution.
  if (std::isnan(own_sog) || own_sog > kSogMax) { invalidate(t); return; }
  if (std::isnan(own_cog) || own_cog >= 360.0) {
    if (own_sog < 0.01)
      own_cog = 0.0;  // stationary: heading is irrelevant
    else { invalidate(t); return; }
  }
  // Target: same treatment.
  if (std::isnan(tgt_sog) || tgt_sog > kSogMax) { invalidate(t); return; }
  if (std::isnan(tgt_cog) || tgt_cog >= 360.0) {
    if (tgt_sog < 0.01)
      tgt_cog = 0.0;
    else { invalidate(t); return; }
  }
  // Both essentially stationary -> no meaningful CPA.
  if (own_sog < 1e-6 && tgt_sog < 1e-6) { invalidate(t); return; }

  // --- Relative geometry (Mercator-reduced, flat-earth, metres) ---
  const double east1 = (t.lon - own.lon) * 60.0 * 1852.0;
  const double north1 = (t.lat - own.lat) * 60.0 * 1852.0;
  const double east = east1 * std::cos(own.lat * kDeg2Rad);
  const double north = north1;

  // Velocity components (m/h) in unit-circle frame (90 - compass bearing).
  const double v_own = own_sog * 1852.0;
  const double v_tgt = tgt_sog * 1852.0;
  const double cosa = std::cos((90.0 - own_cog) * kDeg2Rad);
  const double sina = std::sin((90.0 - own_cog) * kDeg2Rad);
  const double cosb = std::cos((90.0 - tgt_cog) * kDeg2Rad);
  const double sinb = std::sin((90.0 - tgt_cog) * kDeg2Rad);
  const double fc = (v_own * cosa) - (v_tgt * cosb);
  const double fs = (v_own * sina) - (v_tgt * sinb);
  const double d = (fc * fc) + (fs * fs);

  double tcpa_hours = 0.0;
  if (d > 1e-9) tcpa_hours = ((fc * east) + (fs * north)) / d;
  if (tcpa_hours < 0.0) { invalidate(t); return; }  // target already passed

  // CPA: propagate both ships great-circle to TCPA, measure separation.
  double olat, olon, tlat, tlon;
  ll_gc_ll(own.lat, own.lon, own_cog, own_sog * tcpa_hours, &olat, &olon);
  ll_gc_ll(t.lat, t.lon, tgt_cog, tgt_sog * tcpa_hours, &tlat, &tlon);
  t.cpaNm = DistGreatCircle(olat, olon, tlat, tlon);
  t.tcpaMin = tcpa_hours * 60.0;
  t.cpaValid = true;

  // --- Dangerous-target gating (mirror UpdateAllAlarms) ---
  t.dangerous = false;
  const bool moored = tgt_sog <= cfg.suppressAnchoredSpeedMax();
  if (cfg.suppressMooredAlerts() && moored) return;
  if (t.rangeNm > cfg.cpaMaxRangeNm()) return;        // out of alert range
  if (t.cpaNm >= cfg.cpaWarnNm()) return;             // CPA not close enough
  if (t.tcpaMin > cfg.tcpaWarnMin()) return;          // CPA too far off in time
  t.dangerous = true;
}

}  // namespace ocpn::qtui
