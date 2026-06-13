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
 * Implement display_config.h.
 */

#include "display_config.h"

#include <cmath>

#include <QChar>

#include "config_store.h"

namespace ocpn::qtui {

namespace {
// Conversion factors from the canonical units (knots / nautical miles).
constexpr double kKnotToKmh = 1.852;
constexpr double kKnotToMph = 1.150779448;
constexpr double kKnotToMs = 0.514444;
}  // namespace

DisplayConfig& DisplayConfig::instance() {
  static DisplayConfig s;
  return s;
}

DisplayConfig::DisplayConfig() {
  ConfigStore& c = ConfigStore::instance();
  m_nav_mode = c.getInt("display/navMode", m_nav_mode);
  m_show_grid = c.getBool("display/showGrid", m_show_grid);
  m_show_depth_units =
      c.getBool("display/showDepthUnits", m_show_depth_units);
  m_show_outlines = c.getBool("display/showChartOutlines", m_show_outlines);
  m_look_ahead = c.getBool("display/lookAhead", m_look_ahead);
  m_preserve_scale = c.getBool("display/preserveScale", m_preserve_scale);
  m_wheel_zoom = c.getDouble("display/wheelZoom", m_wheel_zoom);
  m_show_compass = c.getBool("display/showCompass", m_show_compass);
  m_show_tides = c.getBool("display/showTides", m_show_tides);
  m_show_nodata = c.getBool("display/showNoData", m_show_nodata);
  m_current_vector_min =
      c.getDouble("display/currentVectorMinutes", m_current_vector_min);
  m_time_zone = c.getInt("display/timeZone", m_time_zone);
  m_cog_predict_min = c.getDouble("display/cogPredictMin", m_cog_predict_min);
  m_sogcog_damping = c.getDouble("display/sogCogDamping", m_sogcog_damping);
  m_default_speed = c.getDouble("display/defaultSpeed", m_default_speed);

  m_distance_unit = c.getInt("units/distance", m_distance_unit);
  m_speed_unit = c.getInt("units/speed", m_speed_unit);
  m_wind_unit = c.getInt("units/wind", m_wind_unit);
  m_depth_unit = c.getInt("units/depth", m_depth_unit);
  m_height_unit = c.getInt("units/height", m_height_unit);
  m_temp_unit = c.getInt("units/temp", m_temp_unit);
  m_latlon_format = c.getInt("units/latLonFormat", m_latlon_format);
  m_magnetic_bearings = c.getBool("units/magneticBearings", m_magnetic_bearings);
  m_use_user_magvar = c.getBool("units/useUserMagVar", m_use_user_magvar);
  m_user_magvar = c.getDouble("units/userMagVar", m_user_magvar);

  m_deskew_raster = c.getBool("display/deskewRaster", m_deskew_raster);
  m_rotation_averaging =
      c.getDouble("display/rotationAveraging", m_rotation_averaging);
  m_screen_mm = c.getDouble("display/screenMmWidth", m_screen_mm);
  m_responsive = c.getBool("display/responsiveSizing", m_responsive);
}

// Setters: guard against no-op, store the member, persist, notify. The single
// `changed()` signal drives both display surfaces and the readout refresh.
#define OCPN_SET(member, value, persist)         \
  if ((member) == (value)) return;               \
  (member) = (value);                            \
  persist;                                       \
  emit changed();

void DisplayConfig::setNavMode(int v) {
  OCPN_SET(m_nav_mode, v, ConfigStore::instance().setInt("display/navMode", v))
}
void DisplayConfig::setShowGrid(bool v) {
  OCPN_SET(m_show_grid, v,
           ConfigStore::instance().setBool("display/showGrid", v))
}
void DisplayConfig::setShowDepthUnits(bool v) {
  OCPN_SET(m_show_depth_units, v,
           ConfigStore::instance().setBool("display/showDepthUnits", v))
}
void DisplayConfig::setShowChartOutlines(bool v) {
  OCPN_SET(m_show_outlines, v,
           ConfigStore::instance().setBool("display/showChartOutlines", v))
}
void DisplayConfig::setLookAhead(bool v) {
  OCPN_SET(m_look_ahead, v,
           ConfigStore::instance().setBool("display/lookAhead", v))
}
void DisplayConfig::setPreserveScaleOnSwitch(bool v) {
  OCPN_SET(m_preserve_scale, v,
           ConfigStore::instance().setBool("display/preserveScale", v))
}
void DisplayConfig::setWheelZoomFactor(double v) {
  OCPN_SET(m_wheel_zoom, v,
           ConfigStore::instance().setDouble("display/wheelZoom", v))
}
void DisplayConfig::setShowCompass(bool v) {
  OCPN_SET(m_show_compass, v,
           ConfigStore::instance().setBool("display/showCompass", v))
}
void DisplayConfig::setShowTides(bool v) {
  OCPN_SET(m_show_tides, v,
           ConfigStore::instance().setBool("display/showTides", v))
}
void DisplayConfig::setShowNoData(bool v) {
  OCPN_SET(m_show_nodata, v,
           ConfigStore::instance().setBool("display/showNoData", v))
}
void DisplayConfig::setCurrentVectorMinutes(double v) {
  OCPN_SET(m_current_vector_min, v,
           ConfigStore::instance().setDouble("display/currentVectorMinutes", v))
}
void DisplayConfig::setTimeZone(int v) {
  OCPN_SET(m_time_zone, v, ConfigStore::instance().setInt("display/timeZone", v))
}
void DisplayConfig::setCogPredictorMinutes(double v) {
  OCPN_SET(m_cog_predict_min, v,
           ConfigStore::instance().setDouble("display/cogPredictMin", v))
}
void DisplayConfig::setSogCogDampingSeconds(double v) {
  OCPN_SET(m_sogcog_damping, v,
           ConfigStore::instance().setDouble("display/sogCogDamping", v))
}
void DisplayConfig::setDefaultBoatSpeed(double v) {
  OCPN_SET(m_default_speed, v,
           ConfigStore::instance().setDouble("display/defaultSpeed", v))
}

void DisplayConfig::setDistanceUnit(int v) {
  OCPN_SET(m_distance_unit, v, ConfigStore::instance().setInt("units/distance", v))
}
void DisplayConfig::setSpeedUnit(int v) {
  OCPN_SET(m_speed_unit, v, ConfigStore::instance().setInt("units/speed", v))
}
void DisplayConfig::setWindUnit(int v) {
  OCPN_SET(m_wind_unit, v, ConfigStore::instance().setInt("units/wind", v))
}
void DisplayConfig::setDepthUnit(int v) {
  OCPN_SET(m_depth_unit, v, ConfigStore::instance().setInt("units/depth", v))
}
void DisplayConfig::setHeightUnit(int v) {
  OCPN_SET(m_height_unit, v, ConfigStore::instance().setInt("units/height", v))
}
void DisplayConfig::setTempUnit(int v) {
  OCPN_SET(m_temp_unit, v, ConfigStore::instance().setInt("units/temp", v))
}
void DisplayConfig::setLatLonFormat(int v) {
  OCPN_SET(m_latlon_format, v,
           ConfigStore::instance().setInt("units/latLonFormat", v))
}
void DisplayConfig::setShowMagneticBearings(bool v) {
  OCPN_SET(m_magnetic_bearings, v,
           ConfigStore::instance().setBool("units/magneticBearings", v))
}
void DisplayConfig::setUseUserMagVar(bool v) {
  OCPN_SET(m_use_user_magvar, v,
           ConfigStore::instance().setBool("units/useUserMagVar", v))
}
void DisplayConfig::setUserMagVar(double v) {
  OCPN_SET(m_user_magvar, v,
           ConfigStore::instance().setDouble("units/userMagVar", v))
}

void DisplayConfig::setDeskewRaster(bool v) {
  OCPN_SET(m_deskew_raster, v,
           ConfigStore::instance().setBool("display/deskewRaster", v))
}
void DisplayConfig::setChartRotationAveraging(double v) {
  OCPN_SET(m_rotation_averaging, v,
           ConfigStore::instance().setDouble("display/rotationAveraging", v))
}
void DisplayConfig::setScreenMmWidth(double v) {
  OCPN_SET(m_screen_mm, v,
           ConfigStore::instance().setDouble("display/screenMmWidth", v))
}
void DisplayConfig::setResponsiveSizing(bool v) {
  OCPN_SET(m_responsive, v,
           ConfigStore::instance().setBool("display/responsiveSizing", v))
}

#undef OCPN_SET

QString DisplayConfig::formatSpeed(double knots) const {
  switch (m_speed_unit) {
    case 1:
      return QStringLiteral("%1 km/h").arg(knots * kKnotToKmh, 0, 'f', 1);
    case 2:
      return QStringLiteral("%1 mph").arg(knots * kKnotToMph, 0, 'f', 1);
    default:
      return QStringLiteral("%1 kn").arg(knots, 0, 'f', 1);
  }
}

QString DisplayConfig::formatDistance(double nm) const {
  switch (m_distance_unit) {
    case 1:
      return QStringLiteral("%1 km").arg(nm * kKnotToKmh, 0, 'f', 2);
    case 2:
      return QStringLiteral("%1 mi").arg(nm * kKnotToMph, 0, 'f', 2);
    default:
      return QStringLiteral("%1 NM").arg(nm, 0, 'f', 2);
  }
}

double DisplayConfig::toUserDepth(double metres) const {
  switch (m_depth_unit) {
    case 1: return metres / 0.3048;        // feet
    case 2: return metres / 0.3048 / 6.0;  // fathoms
    default: return metres;                // metres
  }
}

double DisplayConfig::fromUserDepth(double value) const {
  switch (m_depth_unit) {
    case 1: return value * 0.3048;        // feet
    case 2: return value * 0.3048 * 6.0;  // fathoms
    default: return value;                // metres
  }
}

QString DisplayConfig::depthUnitLabel() const {
  switch (m_depth_unit) {
    case 1: return QStringLiteral("ft");
    case 2: return QStringLiteral("fm");
    default: return QStringLiteral("m");
  }
}

QString DisplayConfig::formatLatLon(double lat, double lon) const {
  auto one = [this](double deg, int deg_width, QChar pos, QChar neg) {
    const QChar hemi = deg >= 0.0 ? pos : neg;
    deg = std::abs(deg);
    const int d = static_cast<int>(deg);
    switch (m_latlon_format) {
      case 1: {  // DDD MM' SS.s"
        const double m_full = (deg - d) * 60.0;
        const int m = static_cast<int>(m_full);
        const double s = (m_full - m) * 60.0;
        return QStringLiteral("%1°%2'%3\"%4")
            .arg(d, deg_width, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 4, 'f', 1, QChar('0'))
            .arg(hemi);
      }
      case 2:  // DDD.dddddd
        return QStringLiteral("%1°%2")
            .arg(deg, deg_width + 7, 'f', 6, QChar('0'))
            .arg(hemi);
      default: {  // DDD MM.mmm'
        const double m = (deg - d) * 60.0;
        return QStringLiteral("%1°%2'%3")
            .arg(d, deg_width, 10, QChar('0'))
            .arg(m, 6, 'f', 3, QChar('0'))
            .arg(hemi);
      }
    }
  };
  return one(lat, 2, QChar('N'), QChar('S')) + QStringLiteral("  ") +
         one(lon, 3, QChar('E'), QChar('W'));
}

QString DisplayConfig::formatBearing(double true_deg,
                                     double auto_variation) const {
  double deg = true_deg;
  QChar suffix('T');
  if (m_magnetic_bearings) {
    const double var = m_use_user_magvar ? m_user_magvar : auto_variation;
    // East variation positive: magnetic = true - variation.
    deg = std::fmod(true_deg - var + 360.0, 360.0);
    suffix = QChar('M');
  }
  return QStringLiteral("%1°%2").arg(deg, 3, 'f', 0, QChar('0')).arg(suffix);
}

}  // namespace ocpn::qtui
