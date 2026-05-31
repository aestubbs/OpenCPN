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
 * Implement tide_layer.h.
 */

#include "tide_layer.h"

#include <cmath>
#include <ctime>

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QRectF>

#include "display_config.h"
#include "idx_entry.h"
#include "model/gui_vars.h"  // gTimeSource (shared display time)
#include "tcmgr.h"
#include "time_controller.h"

namespace ocpn::qtui {

namespace {
// The display time the engine samples at: gTimeSource when set, else "now".
time_t displaySecs() {
  return gTimeSource.isValid()
             ? static_cast<time_t>(gTimeSource.toSecsSinceEpoch())
             : static_cast<time_t>(
                   QDateTime::currentDateTime().toSecsSinceEpoch());
}
}  // namespace

TideLayer::TideLayer(const Viewport* viewport, QObject* parent)
    : StaticNavLayer(nullptr, viewport, parent), m_vp(viewport) {
  setOwner(QStringLiteral("core.tides"));
  setVisible(DisplayConfig::instance().showTides());
  // Show/hide with the MUIBar tides toggle.
  connect(&DisplayConfig::instance(), &DisplayConfig::changed, this, [this]() {
    setVisible(DisplayConfig::instance().showTides());
  });
  // Rebuild when the timeline scrubs (throttled to once per displayed minute --
  // tides move slowly and re-querying + re-rendering labels is not free).
  connect(&TimeController::instance(), &TimeController::timeChanged, this,
          &TideLayer::onTimeChanged);
}

void TideLayer::onTimeChanged() {
  const qint64 minute = static_cast<qint64>(displaySecs()) / 60;
  if (minute == m_last_minute) return;
  m_last_minute = minute;
  Q_EMIT dirty();
}

void TideLayer::draw(SgBuilder& b, double wpp) {
  if (!ptcmgr || !ptcmgr->IsReady() || !m_vp) return;

  const QRectF vb = m_vp->visibleWorldBounds(60.0);  // world coords (+margin)
  const time_t t = displaySecs();

  int shown = 0;
  constexpr int kMax = 300;  // declutter cap per frame
  for (int i = 0; i <= ptcmgr->Get_max_IDX() && shown < kMax; ++i) {
    const IDX_entry* e = ptcmgr->GetIDX_entry(i);
    if (!e || !e->IDX_Useable) continue;
    const char ty = e->IDX_type;
    const bool is_tide = (ty == 't' || ty == 'T');
    const bool is_current = (ty == 'c' || ty == 'C');
    if (!is_tide && !is_current) continue;

    const QPointF w = world(e->IDX_lat, e->IDX_lon);  // Mercator world point
    if (!vb.contains(w)) continue;

    float val = 0.0f, dir = 0.0f;
    if (!ptcmgr->GetTideOrCurrentMeters(t, i, val, dir)) continue;

    if (is_tide) {
      b.setBrush(QColor(60, 130, 220));
      b.setPen(QColor(255, 255, 255), 1.2f);
      b.drawCircle(w, static_cast<float>(5.0 * wpp));

      const QString txt = QString::asprintf("%.1f m", val);
      const QImage img = SgBuilder::renderText(txt, QColor(15, 45, 90), 9.0f);
      if (!img.isNull()) {
        const qreal dpr =
            img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
        const double tw = img.width() / dpr * wpp;
        const double th = img.height() / dpr * wpp;
        b.drawImage(QRectF(w.x() + 8.0 * wpp, w.y() - th / 2.0, tw, th), img);
      }
    } else {  // current station: set-direction arrow, length ~ velocity
      const QPointF u = headingVec(dir);  // world unit vector for the set
      const double len = (14.0 + std::min(60.0, std::fabs(val) * 18.0)) * wpp;
      const QPointF tip(w.x() + u.x() * len, w.y() + u.y() * len);
      b.setPen(QColor(240, 140, 30), 2.0f);
      b.noBrush();
      b.drawLine(w, tip);
      // arrowhead
      const QPointF n(-u.y(), u.x());
      const QPointF base(tip.x() - u.x() * 5.0 * wpp, tip.y() - u.y() * 5.0 * wpp);
      b.drawLine(tip, QPointF(base.x() + n.x() * 3.0 * wpp,
                              base.y() + n.y() * 3.0 * wpp));
      b.drawLine(tip, QPointF(base.x() - n.x() * 3.0 * wpp,
                              base.y() - n.y() * 3.0 * wpp));
      b.setBrush(QColor(240, 140, 30));
      b.setPen(QColor(120, 70, 10), 1.0f);
      b.drawCircle(w, static_cast<float>(2.5 * wpp));
    }
    ++shown;
  }
}

}  // namespace ocpn::qtui
