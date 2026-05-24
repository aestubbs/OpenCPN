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
 * NavStateViewModel -- a QObject view-model exposing live navigation state to
 * QML via Q_PROPERTY (P3.2). It observes a NavDataProvider (demo or live) and
 * republishes own-ship position / SOG / COG and the AIS target count as bound
 * properties, so the QML HUD (P3.4) updates declaratively -- no imperative
 * UI plumbing. Refreshes on the provider's dynamicChanged() (coalesced).
 */

#ifndef OCPN_QT_NAV_STATE_VIEW_MODEL_H_
#define OCPN_QT_NAV_STATE_VIEW_MODEL_H_

#include <QObject>
#include <QString>

#include "nav_data.h"

namespace ocpn::qtui {

class NavDataProvider;

class NavStateViewModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool ownShipValid READ ownShipValid NOTIFY changed)
  Q_PROPERTY(double sog READ sog NOTIFY changed)
  Q_PROPERTY(double cog READ cog NOTIFY changed)
  Q_PROPERTY(QString positionText READ positionText NOTIFY changed)
  Q_PROPERTY(QString sogText READ sogText NOTIFY changed)
  Q_PROPERTY(QString cogText READ cogText NOTIFY changed)
  Q_PROPERTY(int aisTargetCount READ aisTargetCount NOTIFY changed)

public:
  explicit NavStateViewModel(NavDataProvider* provider,
                             QObject* parent = nullptr);

  bool ownShipValid() const { return m_own.valid; }
  double sog() const { return m_own.sog; }
  double cog() const { return m_own.cog; }
  QString positionText() const;
  QString sogText() const;
  QString cogText() const;
  int aisTargetCount() const { return m_ais_count; }

Q_SIGNALS:
  void changed();

private:
  void refresh();  // pull from the provider; emit changed()

  NavDataProvider* m_provider;
  OwnShipState m_own;
  int m_ais_count = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NAV_STATE_VIEW_MODEL_H_
