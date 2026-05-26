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

#include <QObject>
#include <QString>

namespace ocpn::qtui {

class OwnShipConfig : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString vesselName READ vesselName WRITE setVesselName NOTIFY
                 changed)
  // MMSI as a 9-digit string ("" if unset) -- string so QML keeps leading
  // zeros and an empty field; mmsiValue() gives the int for filtering.
  Q_PROPERTY(QString mmsi READ mmsi WRITE setMmsi NOTIFY changed)

public:
  static OwnShipConfig& instance();

  QString vesselName() const { return m_name; }
  void setVesselName(const QString& name);
  QString mmsi() const { return m_mmsi; }
  void setMmsi(const QString& mmsi);

  /** The MMSI as an int for AIS self-exclusion; 0 if unset/invalid. */
  int mmsiValue() const;

Q_SIGNALS:
  void changed();

private:
  OwnShipConfig();  // loads from the config store
  QString m_name;
  QString m_mmsi;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_OWN_SHIP_CONFIG_H_
