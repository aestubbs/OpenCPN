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
 * TideLayer -- world-anchored Layer drawing tide & current stations from the
 * prediction engine (global `ptcmgr`, P3.14 A/B), sampled at the shared display
 * time (`TimeController` -> `gTimeSource`). Tide stations show a marker + the
 * height of tide at the selected time; current stations a set-direction arrow
 * scaled by velocity. Rebuilds on viewport zoom (NavLayer), on the display time
 * crossing a minute boundary, and on the Show-Tides toggle. P3.14 phase D.
 *
 * "Active tides" seam: the per-station height query here is what a later
 * sounding-adjustment pass (mesh interpolation across stations) reuses.
 */

#ifndef OCPN_QT_TIDE_LAYER_H_
#define OCPN_QT_TIDE_LAYER_H_

#include <QtGlobal>

#include "nav_layer.h"

namespace ocpn::qtui {

class TideLayer : public StaticNavLayer {
  Q_OBJECT

public:
  explicit TideLayer(const Viewport* viewport, QObject* parent = nullptr);

  QString id() const override { return QStringLiteral("core.tides"); }
  QString name() const override { return QStringLiteral("Tides & currents"); }
  bool persistState() const override { return false; }  // driven by showTides

protected:
  void draw(SgBuilder& b, double world_per_px) override;

private:
  void onTimeChanged();  // rebuild at most once per displayed minute

  const Viewport* m_vp;
  qint64 m_last_minute = -1;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_TIDE_LAYER_H_
