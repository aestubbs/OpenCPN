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
 * WindDecoder -- a wx-free tap on the comm bus for wind + speed-through-water,
 * which the model doesn't otherwise decode (#39). Subscribes (via the Qt
 * ObsConnection) to NMEA-0183 MWV/VHW and NMEA-2000 130306 (wind) / 128259
 * (boat speed), decodes them, and keeps the latest apparent/true wind and STW
 * for the vessel HUD. Process-wide singleton; subscribe on the GUI thread.
 */

#ifndef OCPN_QT_WIND_DECODER_H_
#define OCPN_QT_WIND_DECODER_H_

#include <mutex>

#include "observable_qt.h"  // ObsConnection (Qt-native, no wx)

namespace ocpn::qtui {

/** Latest wind / speed-through-water. NaN angle / negative speed = no data. */
struct WindData {
  double awa = qQuietNan();  // apparent wind angle, deg relative to bow 0..360
  double aws = -1.0;         // apparent wind speed, knots
  double twa = qQuietNan();  // true wind angle, deg relative to bow
  double tws = -1.0;         // true wind speed, knots
  double stw = -1.0;         // speed through water, knots
  static double qQuietNan();
};

class WindDecoder {
public:
  static WindDecoder& instance();
  WindData data() const;  // thread-safe snapshot

private:
  WindDecoder();  // subscribes to the bus

  void onN2kWind(const ObsData& d);   // PGN 130306
  void onN2kSpeed(const ObsData& d);  // PGN 128259
  void onMwv(const ObsData& d);       // NMEA-0183 MWV
  void onVhw(const ObsData& d);       // NMEA-0183 VHW

  mutable std::mutex m_mutex;
  WindData m_data;
  ObsConnection m_c_wind, m_c_speed, m_c_mwv, m_c_vhw;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_WIND_DECODER_H_
