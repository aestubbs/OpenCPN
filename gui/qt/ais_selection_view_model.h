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
 * AisSelectionViewModel -- the currently-selected AIS target, exposed to QML
 * for an info popup (P3.9). ChartCanvas hit-tests a click against the
 * AisTargetStore (Qt-native, replacing the legacy Select registry) and calls
 * select()/clear(); QML binds the info popup to `valid` and the detail
 * properties.
 */

#ifndef OCPN_QT_AIS_SELECTION_VIEW_MODEL_H_
#define OCPN_QT_AIS_SELECTION_VIEW_MODEL_H_

#include <QObject>
#include <QString>

#include "nav_data.h"

namespace ocpn::qtui {

class AisSelectionViewModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool valid READ valid NOTIFY changed)
  Q_PROPERTY(int mmsi READ mmsi NOTIFY changed)
  Q_PROPERTY(QString name READ name NOTIFY changed)
  Q_PROPERTY(QString positionText READ positionText NOTIFY changed)
  Q_PROPERTY(QString sogText READ sogText NOTIFY changed)
  Q_PROPERTY(QString cogText READ cogText NOTIFY changed)

public:
  using QObject::QObject;

  bool valid() const { return m_valid; }
  int mmsi() const { return m_target.mmsi; }
  QString name() const { return m_target.name; }
  QString positionText() const;
  QString sogText() const;
  QString cogText() const;

  /** Set the selected target (from a hit-test) and notify QML. */
  void select(const AisTarget& target);
  /** Clear the selection (close the popup). Invokable from QML. */
  Q_INVOKABLE void clear();

Q_SIGNALS:
  void changed();

private:
  AisTarget m_target;
  bool m_valid = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_AIS_SELECTION_VIEW_MODEL_H_
