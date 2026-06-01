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
 * DisplayConfig -- the user's chart-display preferences, mirroring the wx
 * Options > Display page (General / Units / Advanced sub-pages) and the
 * "Quick Display" canvas-options drawer. A process-wide singleton, persisted
 * in the SQLite config (ConfigStore), exposed to QML as the context property
 * "display". Both the Options Display page and the right-edge Canvas Options
 * drawer bind to this one object, so the two surfaces stay in sync.
 *
 * Settings divide into three kinds:
 *   - consumed live      -- compass visibility, wheel-zoom sensitivity, COG
 *                           predictor length, units / lat-lon format / bearing
 *                           mode (these change the renderer or readouts now);
 *   - persisted, pending -- course-up, look-ahead, screen-mm calibration etc.
 *                           are stored but await renderer features (see the
 *                           P3.6 inventory in docs/QT_MIGRATION_TASKS.md);
 *   - not applicable     -- options that the Qt architecture makes moot (e.g.
 *                           the quilting on/off toggle, OpenGL tuning) are
 *                           NOT modelled here; the inventory records why.
 *
 * The format* helpers are the single place unit choices take effect: the nav
 * readout view-models call them instead of hard-coding knots / degrees.
 */

#ifndef OCPN_QT_DISPLAY_CONFIG_H_
#define OCPN_QT_DISPLAY_CONFIG_H_

#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace ocpn::qtui {

class DisplayConfig : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  // --- General -------------------------------------------------------------
  // Chart orientation: 0 = North-Up, 1 = Course-Up. (Course-Up is persisted
  // but the viewport does not rotate yet -- see the P3.6 inventory.)
  Q_PROPERTY(int navMode READ navMode WRITE setNavMode NOTIFY changed)
  // Shift the viewport ahead of own ship along COG while following. Persisted;
  // pending look-ahead support in the follow logic.
  Q_PROPERTY(bool lookAhead READ lookAhead WRITE setLookAhead NOTIFY changed)
  // Keep the current zoom when the reference chart changes, rather than
  // snapping to the new chart's native scale.
  Q_PROPERTY(bool preserveScaleOnSwitch READ preserveScaleOnSwitch WRITE
                 setPreserveScaleOnSwitch NOTIFY changed)
  // Mouse-wheel zoom step multiplier per notch (1.1 = gentle .. 2.0 = brisk).
  Q_PROPERTY(double wheelZoomFactor READ wheelZoomFactor WRITE
                 setWheelZoomFactor NOTIFY changed)
  // Show the on-chart compass rose overlay (top-right).
  Q_PROPERTY(bool showCompass READ showCompass WRITE setShowCompass NOTIFY
                 changed)
  // Show tide stations (and the bottom timeline). Mirrors wx g_bShowTide.
  // The tide/current scene-graph Layer (P3.14 phase D) will also gate on this.
  Q_PROPERTY(bool showTides READ showTides WRITE setShowTides NOTIFY changed)
  // ECDIS NODATA fill: paint the world backdrop the S-52 no-coverage grey so
  // areas with no ENC read as "unsurveyed" rather than the roamable world map.
  // Off by default (keeps the world basemap). Mirrors the ECDIS no-data look.
  Q_PROPERTY(bool showNoData READ showNoData WRITE setShowNoData NOTIFY changed)
  // Time display: 0 = UTC, 1 = local (OS) time zone.
  Q_PROPERTY(int timeZone READ timeZone WRITE setTimeZone NOTIFY changed)
  // Own-ship COG/SOG predictor vector length, in minutes of run.
  Q_PROPERTY(double cogPredictorMinutes READ cogPredictorMinutes WRITE
                 setCogPredictorMinutes NOTIFY changed)
  // First-order low-pass damping (seconds) on SOG/COG. Persisted; applied by
  // the nav pipeline once filtering lands.
  Q_PROPERTY(double sogCogDampingSeconds READ sogCogDampingSeconds WRITE
                 setSogCogDampingSeconds NOTIFY changed)
  // Default boat speed (knots) for ETA when no live SOG is available.
  Q_PROPERTY(double defaultBoatSpeed READ defaultBoatSpeed WRITE
                 setDefaultBoatSpeed NOTIFY changed)

  // --- Units ---------------------------------------------------------------
  // 0 = NM, 1 = km, 2 = statute miles.
  Q_PROPERTY(int distanceUnit READ distanceUnit WRITE setDistanceUnit NOTIFY
                 changed)
  // 0 = kn, 1 = km/h, 2 = mph.
  Q_PROPERTY(int speedUnit READ speedUnit WRITE setSpeedUnit NOTIFY changed)
  // 0 = kn, 1 = m/s, 2 = km/h, 3 = mph.
  Q_PROPERTY(int windUnit READ windUnit WRITE setWindUnit NOTIFY changed)
  // 0 = m, 1 = ft, 2 = fathoms.
  Q_PROPERTY(int depthUnit READ depthUnit WRITE setDepthUnit NOTIFY changed)
  // 0 = m, 1 = ft.
  Q_PROPERTY(int heightUnit READ heightUnit WRITE setHeightUnit NOTIFY changed)
  // 0 = Celsius, 1 = Fahrenheit.
  Q_PROPERTY(int tempUnit READ tempUnit WRITE setTempUnit NOTIFY changed)
  // Lat/lon format: 0 = DDD MM.mmm', 1 = DDD MM' SS.s", 2 = DDD.dddddd.
  Q_PROPERTY(int latLonFormat READ latLonFormat WRITE setLatLonFormat NOTIFY
                 changed)
  // Show bearings as magnetic (applying variation) rather than true.
  Q_PROPERTY(bool showMagneticBearings READ showMagneticBearings WRITE
                 setShowMagneticBearings NOTIFY changed)
  // Use a user-supplied magnetic variation instead of a computed WMM value.
  Q_PROPERTY(bool useUserMagVar READ useUserMagVar WRITE setUseUserMagVar
                 NOTIFY changed)
  // User magnetic variation in degrees (east +, west -).
  Q_PROPERTY(double userMagVar READ userMagVar WRITE setUserMagVar NOTIFY
                 changed)

  // --- Advanced (all persisted, pending renderer support) ------------------
  // Re-project ("de-skew") raster charts to a north-up grid.
  Q_PROPERTY(bool deskewRaster READ deskewRaster WRITE setDeskewRaster NOTIFY
                 changed)
  // Course-up heading averaging window, seconds.
  Q_PROPERTY(double chartRotationAveraging READ chartRotationAveraging WRITE
                 setChartRotationAveraging NOTIFY changed)
  // Physical screen width in mm, for true-scale / 1:N display calibration.
  Q_PROPERTY(double screenMmWidth READ screenMmWidth WRITE setScreenMmWidth
                 NOTIFY changed)
  // Enlarge controls for touch / high-DPI ("responsive") use.
  Q_PROPERTY(bool responsiveSizing READ responsiveSizing WRITE
                 setResponsiveSizing NOTIFY changed)

public:
  static DisplayConfig& instance();
  // QML singleton factory -- returns the shared C++ instance. CppOwnership so
  // the engine never deletes our function-static singleton on teardown.
  static DisplayConfig* create(QQmlEngine*, QJSEngine*) {
    DisplayConfig* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  int navMode() const { return m_nav_mode; }
  void setNavMode(int v);
  bool lookAhead() const { return m_look_ahead; }
  void setLookAhead(bool v);
  bool preserveScaleOnSwitch() const { return m_preserve_scale; }
  void setPreserveScaleOnSwitch(bool v);
  double wheelZoomFactor() const { return m_wheel_zoom; }
  void setWheelZoomFactor(double v);
  bool showCompass() const { return m_show_compass; }
  void setShowCompass(bool v);
  bool showTides() const { return m_show_tides; }
  void setShowTides(bool v);
  bool showNoData() const { return m_show_nodata; }
  void setShowNoData(bool v);
  int timeZone() const { return m_time_zone; }
  void setTimeZone(int v);
  double cogPredictorMinutes() const { return m_cog_predict_min; }
  void setCogPredictorMinutes(double v);
  double sogCogDampingSeconds() const { return m_sogcog_damping; }
  void setSogCogDampingSeconds(double v);
  double defaultBoatSpeed() const { return m_default_speed; }
  void setDefaultBoatSpeed(double v);

  int distanceUnit() const { return m_distance_unit; }
  void setDistanceUnit(int v);
  int speedUnit() const { return m_speed_unit; }
  void setSpeedUnit(int v);
  int windUnit() const { return m_wind_unit; }
  void setWindUnit(int v);
  int depthUnit() const { return m_depth_unit; }
  void setDepthUnit(int v);
  int heightUnit() const { return m_height_unit; }
  void setHeightUnit(int v);
  int tempUnit() const { return m_temp_unit; }
  void setTempUnit(int v);
  int latLonFormat() const { return m_latlon_format; }
  void setLatLonFormat(int v);
  bool showMagneticBearings() const { return m_magnetic_bearings; }
  void setShowMagneticBearings(bool v);
  bool useUserMagVar() const { return m_use_user_magvar; }
  void setUseUserMagVar(bool v);
  double userMagVar() const { return m_user_magvar; }
  void setUserMagVar(double v);

  bool deskewRaster() const { return m_deskew_raster; }
  void setDeskewRaster(bool v);
  double chartRotationAveraging() const { return m_rotation_averaging; }
  void setChartRotationAveraging(double v);
  double screenMmWidth() const { return m_screen_mm; }
  void setScreenMmWidth(double v);
  bool responsiveSizing() const { return m_responsive; }
  void setResponsiveSizing(bool v);

  // --- Unit-aware formatters: the single place unit choices take effect. ---
  // Speed in knots -> the selected speed unit, e.g. "6.4 kn" / "11.8 km/h".
  Q_INVOKABLE QString formatSpeed(double knots) const;
  // Distance in NM -> the selected distance unit.
  Q_INVOKABLE QString formatDistance(double nm) const;
  // A lat/lon pair in the selected coordinate format.
  Q_INVOKABLE QString formatLatLon(double lat, double lon) const;
  // A true bearing (deg) -> "123°T", or "118°M" when magnetic bearings are on
  // (applying the user/auto variation). Auto variation is the supplied value.
  Q_INVOKABLE QString formatBearing(double true_deg,
                                    double auto_variation = 0.0) const;
  // Depth/draught unit conversion for editable fields (ECDIS: a depth-unit
  // choice governs every depth entry + display). The canonical store stays in
  // metres; these convert for the UI. `toUserDepth` metres -> selected unit,
  // `fromUserDepth` selected unit -> metres, `depthUnitLabel` the short suffix.
  Q_INVOKABLE double toUserDepth(double metres) const;
  Q_INVOKABLE double fromUserDepth(double value) const;
  Q_INVOKABLE QString depthUnitLabel() const;

Q_SIGNALS:
  void changed();

private:
  DisplayConfig();  // loads from the config store

  int m_nav_mode = 0;
  bool m_look_ahead = false;
  bool m_preserve_scale = false;
  double m_wheel_zoom = 1.3;
  bool m_show_compass = true;
  bool m_show_tides = false;
  bool m_show_nodata = false;
  int m_time_zone = 0;
  double m_cog_predict_min = 6.0;
  double m_sogcog_damping = 0.0;
  double m_default_speed = 6.0;

  int m_distance_unit = 0;
  int m_speed_unit = 0;
  int m_wind_unit = 0;
  int m_depth_unit = 0;
  int m_height_unit = 0;
  int m_temp_unit = 0;
  int m_latlon_format = 0;
  bool m_magnetic_bearings = false;
  bool m_use_user_magvar = false;
  double m_user_magvar = 0.0;

  bool m_deskew_raster = true;
  double m_rotation_averaging = 0.0;
  double m_screen_mm = 0.0;
  bool m_responsive = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_DISPLAY_CONFIG_H_
