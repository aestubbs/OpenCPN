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
 * Implement wind_decoder.h.
 */

#include "wind_decoder.h"

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QList>
#include <QString>

#include "N2KParser.h"
#include "model/comm_navmsg.h"

namespace ocpn::qtui {

namespace {
constexpr double kMsToKnots = 1.943844;
constexpr double kRadToDeg = 57.29577951308232;

// Cast the observable payload back to the message it carried.
std::shared_ptr<const NavMsg> asNavMsg(const ObsData& d) {
  return std::static_pointer_cast<const NavMsg>(d.shared_ptr);
}

// Split an NMEA-0183 sentence into comma fields, dropping any "*xx" checksum.
QList<QString> nmeaFields(const std::string& payload) {
  QString s = QString::fromStdString(payload);
  const int star = s.indexOf('*');
  if (star >= 0) s = s.left(star);
  return s.split(',');
}

double normAngle(double deg) {
  while (deg < 0) deg += 360.0;
  while (deg >= 360.0) deg -= 360.0;
  return deg;
}
}  // namespace

double WindData::qQuietNan() {
  return std::numeric_limits<double>::quiet_NaN();
}

WindDecoder& WindDecoder::instance() {
  static WindDecoder s;
  return s;
}

WindDecoder::WindDecoder() {
  m_c_wind.Listen(Nmea2000Msg(static_cast<uint64_t>(130306)).key(),
                  [this](const ObsData& d) { onN2kWind(d); });
  m_c_speed.Listen(Nmea2000Msg(static_cast<uint64_t>(128259)).key(),
                   [this](const ObsData& d) { onN2kSpeed(d); });
  m_c_mwv.Listen(Nmea0183Msg::MessageKey("MWV"),
                 [this](const ObsData& d) { onMwv(d); });
  m_c_vhw.Listen(Nmea0183Msg::MessageKey("VHW"),
                 [this](const ObsData& d) { onVhw(d); });
}

WindData WindDecoder::data() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_data;
}

void WindDecoder::onN2kWind(const ObsData& d) {
  auto msg = std::dynamic_pointer_cast<const Nmea2000Msg>(asNavMsg(d));
  if (!msg) return;
  std::vector<unsigned char> v = msg->payload;
  unsigned char sid = 0;
  double speed_ms = 0, angle_rad = 0;
  tN2kWindReference ref;
  if (!ParseN2kPGN130306(v, sid, speed_ms, angle_rad, ref)) return;
  const double kt = speed_ms * kMsToKnots;
  const double deg = normAngle(angle_rad * kRadToDeg);
  std::lock_guard<std::mutex> lock(m_mutex);
  if (ref == N2kWind_Apparent) {
    m_data.aws = kt;
    m_data.awa = deg;
  } else {  // true (boat / water / north referenced)
    m_data.tws = kt;
    m_data.twa = deg;
  }
}

void WindDecoder::onN2kSpeed(const ObsData& d) {
  auto msg = std::dynamic_pointer_cast<const Nmea2000Msg>(asNavMsg(d));
  if (!msg) return;
  std::vector<unsigned char> v = msg->payload;
  unsigned char sid = 0;
  double water = 0, ground = 0;
  tN2kSpeedWaterReferenceType swrt;
  if (!ParseN2kPGN128259(v, sid, water, ground, swrt)) return;
  if (std::isnan(water)) return;
  std::lock_guard<std::mutex> lock(m_mutex);
  m_data.stw = water * kMsToKnots;
}

void WindDecoder::onMwv(const ObsData& d) {
  auto msg = std::dynamic_pointer_cast<const Nmea0183Msg>(asNavMsg(d));
  if (!msg) return;
  // $--MWV,angle,reference(R/T),speed,units(N/K/M),status(A/V)
  const QList<QString> f = nmeaFields(msg->payload);
  if (f.size() < 6) return;
  if (f[5].trimmed().startsWith('V')) return;  // void
  bool ok1 = false, ok2 = false;
  const double angle = normAngle(f[1].toDouble(&ok1));
  double speed = f[3].toDouble(&ok2);
  if (!ok1 || !ok2) return;
  const QString units = f[4].trimmed().toUpper();
  if (units == "K")
    speed *= 0.539957;  // km/h -> kt
  else if (units == "M")
    speed *= kMsToKnots;  // m/s -> kt
  const bool apparent = f[2].trimmed().startsWith('R');
  std::lock_guard<std::mutex> lock(m_mutex);
  if (apparent) {
    m_data.awa = angle;
    m_data.aws = speed;
  } else {
    m_data.twa = angle;
    m_data.tws = speed;
  }
}

void WindDecoder::onVhw(const ObsData& d) {
  auto msg = std::dynamic_pointer_cast<const Nmea0183Msg>(asNavMsg(d));
  if (!msg) return;
  // $--VHW,headT,T,headM,M,stw_kn,N,stw_kmh,K  -- field 5 is STW in knots.
  const QList<QString> f = nmeaFields(msg->payload);
  if (f.size() < 6) return;
  bool ok = false;
  const double stw = f[5].toDouble(&ok);
  if (!ok) return;
  std::lock_guard<std::mutex> lock(m_mutex);
  m_data.stw = stw;
}

}  // namespace ocpn::qtui
