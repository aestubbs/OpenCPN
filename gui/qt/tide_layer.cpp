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
#include <QTimer>

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

// A small screen-fixed text label (sized in world units via wpp), offset to the
// right of the station marker.
void drawLabel(SgBuilder& b, const QString& txt, const QColor& color,
               const QPointF& w, double wpp) {
  const QImage img = SgBuilder::renderText(txt, color, 9.0f);
  if (img.isNull()) return;
  const qreal dpr = img.devicePixelRatio() > 0 ? img.devicePixelRatio() : 1.0;
  const double tw = img.width() / dpr * wpp;
  const double th = img.height() / dpr * wpp;
  b.drawImage(QRectF(w.x() + 8.0 * wpp, w.y() - th / 2.0, tw, th), img);
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
  // Re-query + redraw as the timeline scrubs / plays, so tides and currents
  // animate. Coalesce bursts (drag rate, the 1 Hz live tick) into ~16 rebuilds
  // a second so a fast drag stays smooth.
  connect(&TimeController::instance(), &TimeController::timeChanged, this,
          &TideLayer::onTimeChanged);
}

void TideLayer::onTimeChanged() {
  if (m_rebuild_pending) return;
  m_rebuild_pending = true;
  QTimer::singleShot(60, this, [this]() {
    m_rebuild_pending = false;
    Q_EMIT dirty();
  });
}

void TideLayer::draw(SgBuilder& b, double wpp) {
  if (!ptcmgr || !ptcmgr->IsReady() || !m_vp) return;

  const QRectF vb = m_vp->visibleWorldBounds(60.0);  // world coords (+margin)
  // Labels (height / speed) are only legible -- and cheap enough to re-render
  // every animation frame -- when zoomed in; at wide views draw markers/arrows
  // only (keeps a fast drag smooth even with hundreds of stations in view).
  const bool show_labels = vb.width() < 6.0;
  const time_t t = displaySecs();
  DisplayConfig& dc = DisplayConfig::instance();
  const bool feet = dc.heightUnit() == 1;

  int shown = 0;
  constexpr int kMax = 300;  // declutter cap per frame
  for (int i = 0; i <= ptcmgr->Get_max_IDX() && shown < kMax; ++i) {
    const IDX_entry* e = ptcmgr->GetIDX_entry(i);
    if (!e || !e->IDX_Useable) continue;
    const char ty = e->IDX_type;
    const bool is_tide = (ty == 't' || ty == 'T');
    const bool is_current = (ty == 'c' || ty == 'C');
    if (!is_tide && !is_current) continue;
    // Multi-depth current stations report several records at one lat/lon (NOAA
    // names them "... (depth NN ft)"). ScrubCurrentDepths() keeps the shallowest,
    // most-usable record and marks the deeper ones b_skipTooDeep; honour that so
    // we don't stack a marker per depth. Matches the legacy wx renderer
    // (chcanv.cpp DrawAllCurrentsInBBox / RebuildCurrentSelectList).
    if (is_current && e->b_skipTooDeep) continue;

    const QPointF w = world(e->IDX_lat, e->IDX_lon);  // Mercator world point
    if (!vb.contains(w)) continue;

    if (is_tide) {
      float val = 0.0f, dir = 0.0f;  // metres (height of tide)
      if (!ptcmgr->GetTideOrCurrentMeters(t, i, val, dir)) continue;
      b.setBrush(QColor(60, 130, 220));
      b.setPen(QColor(255, 255, 255), 1.2f);
      b.drawCircle(w, static_cast<float>(5.0 * wpp));
      if (show_labels) {
        const double h = feet ? val / 0.3048 : val;
        const QString txt =
            QString::number(h, 'f', 1) + (feet ? QStringLiteral(" ft")
                                               : QStringLiteral(" m"));
        drawLabel(b, txt, QColor(15, 45, 90), w, wpp);
      }
    } else {  // current station: value in knots (station-native)
      float val = 0.0f, dir = 0.0f;
      if (!ptcmgr->GetTideOrCurrent(t, i, val, dir)) continue;
      const QPointF u = headingVec(dir);  // world unit vector for the set
      // Drift vector: the distance the current carries you in the configured
      // time (DisplayConfig.currentVectorMinutes), drawn at chart scale. The
      // world frame is conformal Mercator, so 1 world unit = 60*cos(lat) nm;
      // hence world length = drift_nm / (60*cos lat). It grows/shrinks with zoom
      // like a real set-and-drift vector. Capped so a long period at a large
      // scale can't run off-screen.
      const double drift_nm =
          std::fabs(val) * (dc.currentVectorMinutes() / 60.0);  // kn * hours
      const double clat =
          std::max(0.20, std::cos(e->IDX_lat * 0.017453292519943295));
      const double len = std::min(drift_nm / (60.0 * clat), 160.0 * wpp);
      if (len > 1.5 * wpp) {  // skip the shaft at/near slack (no drift to show)
        const QPointF tip(w.x() + u.x() * len, w.y() + u.y() * len);
        b.setPen(QColor(240, 140, 30), 2.0f);
        b.noBrush();
        b.drawLine(w, tip);
        const QPointF n(-u.y(), u.x());
        const double hb = std::max(4.0 * wpp, len * 0.28);  // arrowhead length
        const double hw = std::max(2.5 * wpp, len * 0.16);  // arrowhead half-width
        const QPointF base(tip.x() - u.x() * hb, tip.y() - u.y() * hb);
        b.drawLine(tip, QPointF(base.x() + n.x() * hw, base.y() + n.y() * hw));
        b.drawLine(tip, QPointF(base.x() - n.x() * hw, base.y() - n.y() * hw));
      }
      b.setBrush(QColor(240, 140, 30));
      b.setPen(QColor(120, 70, 10), 1.0f);
      b.drawCircle(w, static_cast<float>(2.5 * wpp));
      if (show_labels)  // speed in the user's chosen units, beside the station
        drawLabel(b, dc.formatSpeed(std::fabs(val)), QColor(120, 60, 10), w, wpp);
    }
    ++shown;
  }
}

}  // namespace ocpn::qtui
