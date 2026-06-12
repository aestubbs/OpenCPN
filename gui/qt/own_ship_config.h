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
 * OwnShipConfig -- the user's own-vessel identity (name + MMSI), persisted in
 * the SQLite config and exposed to QML as the context property "ownShip" (set
 * on the Options > Ships page). A process-wide singleton so the live nav
 * provider can read the MMSI to exclude the own vessel from the AIS display
 * (we already have our own position).
 */

#ifndef OCPN_QT_OWN_SHIP_CONFIG_H_
#define OCPN_QT_OWN_SHIP_CONFIG_H_

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace ocpn::qtui {

class OwnShipConfig : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QString vesselName READ vesselName WRITE setVesselName NOTIFY
                 changed)
  // MMSI as a 9-digit string ("" if unset) -- string so QML keeps leading
  // zeros and an empty field; mmsiValue() gives the int for filtering.
  Q_PROPERTY(QString mmsi READ mmsi WRITE setMmsi NOTIFY changed)

  // --- Display attributes (wx Options > Ships > Own ship). All persisted;
  //     the real-scale icon, GPS offsets and range rings await the matching
  //     render paths (see the P3.6 inventory) -- the marker is a fixed symbol
  //     for now. ---
  // 0 = default symbol, 1 = real-scale bitmap, 2 = real-scale vector.
  Q_PROPERTY(int iconType READ iconType WRITE setIconType NOTIFY changed)
  Q_PROPERTY(double loa READ loa WRITE setLoa NOTIFY changed)
  Q_PROPERTY(double beam READ beam WRITE setBeam NOTIFY changed)
  Q_PROPERTY(double gpsOffsetX READ gpsOffsetX WRITE setGpsOffsetX NOTIFY
                 changed)
  Q_PROPERTY(double gpsOffsetY READ gpsOffsetY WRITE setGpsOffsetY NOTIFY
                 changed)
  // Minimum on-screen symbol size, mm (real-scale clamps to this).
  Q_PROPERTY(double minScreenSize READ minScreenSize WRITE setMinScreenSize
                 NOTIFY changed)
  // Safety depth, metres. ENC soundings at or shallower than this are drawn
  // bold/black (the S-52 SOUNDS emphasis). Vessel-related (draft + margin), so
  // it lives with the vessel settings rather than the chart options.
  Q_PROPERTY(double safetyDepth READ safetyDepth WRITE setSafetyDepth NOTIFY
                 changed)
  // Draw an arrow toward the active waypoint.
  Q_PROPERTY(bool showWaypointDirection READ showWaypointDirection WRITE
                 setShowWaypointDirection NOTIFY changed)
  // Concentric range rings centred on own ship.
  Q_PROPERTY(bool showRangeRings READ showRangeRings WRITE setShowRangeRings
                 NOTIFY changed)
  Q_PROPERTY(int ringCount READ ringCount WRITE setRingCount NOTIFY changed)
  Q_PROPERTY(double ringSpacing READ ringSpacing WRITE setRingSpacing NOTIFY
                 changed)
  // Ring spacing unit: 0 = NM, 1 = km, 2 = statute miles.
  Q_PROPERTY(int ringUnit READ ringUnit WRITE setRingUnit NOTIFY changed)
  // Range-ring colour (wx g_colourOwnshipRangeRingsColour).
  Q_PROPERTY(QColor ringColor READ ringColor WRITE setRingColor NOTIFY changed)
  // Heading (HDT) predictor length, NM; 0 disables. Separate from the
  // time-based COG/SOG predictor (wx g_ownship_HDTpredictor_miles).
  Q_PROPERTY(double hdtPredictorNm READ hdtPredictorNm WRITE setHdtPredictorNm
                 NOTIFY changed)

public:
  static OwnShipConfig& instance();
  static OwnShipConfig* create(QQmlEngine*, QJSEngine*) {
    OwnShipConfig* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  QString vesselName() const { return m_name; }
  void setVesselName(const QString& name);
  QString mmsi() const { return m_mmsi; }
  void setMmsi(const QString& mmsi);

  /** The MMSI as an int for AIS self-exclusion; 0 if unset/invalid. */
  int mmsiValue() const;

  int iconType() const { return m_icon_type; }
  void setIconType(int v);
  double loa() const { return m_loa; }
  void setLoa(double v);
  double beam() const { return m_beam; }
  void setBeam(double v);
  double gpsOffsetX() const { return m_gps_dx; }
  void setGpsOffsetX(double v);
  double gpsOffsetY() const { return m_gps_dy; }
  void setGpsOffsetY(double v);
  double minScreenSize() const { return m_min_screen; }
  void setMinScreenSize(double v);
  double safetyDepth() const { return m_safety_depth; }
  void setSafetyDepth(double v);
  bool showWaypointDirection() const { return m_show_wp_dir; }
  void setShowWaypointDirection(bool v);
  bool showRangeRings() const { return m_show_rings; }
  void setShowRangeRings(bool v);
  int ringCount() const { return m_ring_count; }
  void setRingCount(int v);
  double ringSpacing() const { return m_ring_spacing; }
  void setRingSpacing(double v);
  int ringUnit() const { return m_ring_unit; }
  void setRingUnit(int v);
  QColor ringColor() const { return m_ring_color; }
  void setRingColor(const QColor& v);
  double hdtPredictorNm() const { return m_hdt_predictor_nm; }
  void setHdtPredictorNm(double v);

Q_SIGNALS:
  void changed();

private:
  // Constructor is PRIVATE so the QML engine cannot default-
  // construct a second instance: Qt picks the Constructor mode
  // over the create() factory whenever the type is default-
  // constructible (qqmlprivate.h singletonConstructionMode),
  // which split every singleton into a C++ brain and a QML
  // brain. Private ctor => Factory mode => create() => the
  // ONE shared instance().
  OwnShipConfig();  // loads from the config store

  QString m_name;
  QString m_mmsi;

  int m_icon_type = 0;
  double m_loa = 0.0;
  double m_beam = 0.0;
  double m_gps_dx = 0.0;
  double m_gps_dy = 0.0;
  double m_min_screen = 0.0;
  double m_safety_depth = 5.0;  // metres; ENC sounding bold threshold
  bool m_show_wp_dir = true;
  bool m_show_rings = false;
  int m_ring_count = 0;
  double m_ring_spacing = 1.0;
  int m_ring_unit = 0;
  QColor m_ring_color = QColor(90, 110, 150);  // muted blue-grey (layer default)
  double m_hdt_predictor_nm = 0.0;             // 0 = no HDT predictor
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_OWN_SHIP_CONFIG_H_
