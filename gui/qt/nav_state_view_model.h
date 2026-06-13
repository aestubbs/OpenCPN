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

#include <cmath>

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
  // Heading (deg true). hdgValid is false when no heading is received (the
  // 511 sentinel) so the HUD can fall back to COG for the boat icon.
  Q_PROPERTY(double hdg READ hdg NOTIFY changed)
  Q_PROPERTY(bool hdgValid READ hdgValid NOTIFY changed)
  Q_PROPERTY(QString positionText READ positionText NOTIFY changed)
  Q_PROPERTY(QString sogText READ sogText NOTIFY changed)
  Q_PROPERTY(QString cogText READ cogText NOTIFY changed)
  Q_PROPERTY(QString hdgText READ hdgText NOTIFY changed)
  // Wind + speed-through-water (#39). *Valid flags gate the HUD gauges.
  Q_PROPERTY(double awa READ awa NOTIFY changed)
  Q_PROPERTY(bool awaValid READ awaValid NOTIFY changed)
  Q_PROPERTY(double aws READ aws NOTIFY changed)
  Q_PROPERTY(double twa READ twa NOTIFY changed)
  Q_PROPERTY(bool twaValid READ twaValid NOTIFY changed)
  Q_PROPERTY(double tws READ tws NOTIFY changed)
  Q_PROPERTY(double stw READ stw NOTIFY changed)

public:
  explicit NavStateViewModel(NavDataProvider* provider,
                             QObject* parent = nullptr);

  bool ownShipValid() const { return m_own.valid; }
  double sog() const { return m_own.sog; }
  double cog() const { return m_own.cog; }
  double hdg() const { return m_own.hdg; }
  bool hdgValid() const { return m_own.hdg < 360.0; }
  double awa() const { return m_own.awa; }
  bool awaValid() const { return std::isfinite(m_own.awa) &&
                                 m_own.awa >= 0.0 && m_own.awa < 360.0; }
  double aws() const { return m_own.aws; }
  double twa() const { return m_own.twa; }
  bool twaValid() const { return std::isfinite(m_own.twa) &&
                                 m_own.twa >= 0.0 && m_own.twa < 360.0; }
  double tws() const { return m_own.tws; }
  double stw() const { return m_own.stw; }
  QString positionText() const;
  QString sogText() const;
  QString cogText() const;
  QString hdgText() const;

signals:
  void changed();

private:
  void refresh();  // pull from the provider; emit changed()

  NavDataProvider* m_provider;
  OwnShipState m_own;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_NAV_STATE_VIEW_MODEL_H_
