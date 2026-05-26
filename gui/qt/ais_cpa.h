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
 * CPA / TCPA solver for AIS targets vs own ship -- a wx-free port of the
 * legacy AisDecoder::UpdateOneCPA + UpdateAllAlarms (model/src/ais_decoder.cpp).
 * Same algorithm: Mercator-reduced relative position + relative-velocity
 * projection for TCPA, great-circle forward propagation for CPA, and the same
 * dangerous-target gating (CPA warn distance, max range, max TCPA, moored
 * suppression). Functional parity with wx; the thresholds come from AisConfig.
 */

#ifndef OCPN_QT_AIS_CPA_H_
#define OCPN_QT_AIS_CPA_H_

#include "nav_data.h"

namespace ocpn::qtui {

class AisConfig;

/**
 * Fill t.rangeNm / bearingDeg / cpaNm / tcpaMin / cpaValid / dangerous from the
 * own-ship state and the AIS config thresholds. Leaves cpaValid = false (and
 * dangerous = false) when no solution exists (own/target stationary, target
 * already passed, invalid fixes).
 */
void computeCpa(const OwnShipState& own, AisTarget& t, const AisConfig& cfg);

}  // namespace ocpn::qtui

#endif  // OCPN_QT_AIS_CPA_H_
