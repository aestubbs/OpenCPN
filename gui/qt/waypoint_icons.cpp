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
 * Implement waypoint_icons.h.
 */

#include "waypoint_icons.h"

#include <QImage>
#include <QPainter>
#include <QString>
#include <QSvgRenderer>

#include "model/routeman.h"  // pWayPointMan

#ifndef OCPN_QT_MARKICONS_DIR
#define OCPN_QT_MARKICONS_DIR ""
#endif

namespace ocpn::qtui {

namespace {
// The default mark icons (key -> SVG file), ported from the wx
// WayPointmanGui::ProcessDefaultIcons curated list so the key vocabulary
// (triangle, diamond, anchor, ...) matches persisted data + the config
// defaults.
struct IconDef {
  const char* key;
  const char* file;
};
constexpr IconDef kIcons[] = {
    {"empty", "Symbol-Empty.svg"},
    {"triangle", "Symbol-Triangle.svg"},
    {"activepoint", "1st-Active-Waypoint.svg"},
    {"boarding", "Marks-Boarding-Location.svg"},
    {"airplane", "Hazard-Airplane.svg"},
    {"anchorage", "1st-Anchorage.svg"},
    {"anchor", "Symbol-Anchor2.svg"},
    {"boundary", "Marks-Boundary.svg"},
    {"buoy1", "Marks-Buoy-TypeA.svg"},
    {"buoy2", "Marks-Buoy-TypeB.svg"},
    {"campfire", "Activity-Campfire.svg"},
    {"camping", "Activity-Camping.svg"},
    {"coral", "Sea-Floor-Coral.svg"},
    {"fishhaven", "Activity-Fishing.svg"},
    {"fishing", "Activity-Fishing.svg"},
    {"fish", "Activity-Fishing.svg"},
    {"float", "Marks-Mooring-Buoy.svg"},
    {"food", "Service-Food.svg"},
    {"greenlite", "Marks-Light-Green.svg"},
    {"kelp", "Sea-Floor-Sea-Weed.svg"},
    {"light", "Marks-Light-TypeA.svg"},
    {"light1", "Marks-Light-TypeB.svg"},
    {"litevessel", "Marks-Light-Vessel.svg"},
    {"mob", "1st-Man-Overboard.svg"},
    {"mooring", "Marks-Mooring-Buoy.svg"},
    {"oilbuoy", "Marks-Mooring-Buoy-Super.svg"},
    {"platform", "Hazard-Oil-Platform.svg"},
    {"redgreenlite", "Marks-Light-Red-Green.svg"},
    {"redlite", "Marks-Light-Red.svg"},
    {"rock1", "Hazard-Rock-Exposed.svg"},
    {"rock2", "Hazard-Rock-Awash.svg"},
    {"sand", "Hazard-Sandbar.svg"},
    {"scuba", "Activity-Diving-Scuba-Flag.svg"},
    {"shoal", "Hazard-Sandbar.svg"},
    {"snag", "Hazard-Snag.svg"},
    {"square", "Symbol-Square.svg"},
    {"diamond", "1st-Diamond.svg"},
    {"circle", "Symbol-Circle.svg"},
    {"wreck1", "Hazard-Wreck1.svg"},
    {"wreck2", "Hazard-Wreck2.svg"},
    {"xmblue", "Symbol-X-Small-Blue.svg"},
    {"xmgreen", "Symbol-X-Small-Green.svg"},
    {"xmred", "Symbol-X-Small-Red.svg"},
};

// Rasterise an SVG to a transparent RGBA image, longest side `px` logical
// pixels, at device-pixel-ratio 2 for crispness (consumers divide by the dpr).
QImage renderSvg(const QString& path, int px) {
  QSvgRenderer r(path);
  if (!r.isValid()) return QImage();
  QSize sz = r.defaultSize();
  if (sz.isEmpty()) sz = QSize(px, px);
  const double s = static_cast<double>(px) / qMax(sz.width(), sz.height());
  constexpr qreal kDpr = 2.0;
  QImage img(QSize(qRound(sz.width() * s * kDpr), qRound(sz.height() * s * kDpr)),
             QImage::Format_RGBA8888_Premultiplied);
  img.setDevicePixelRatio(kDpr);
  img.fill(Qt::transparent);
  QPainter p(&img);
  r.render(&p);
  p.end();
  return img;
}
}  // namespace

void loadDefaultWaypointIcons() {
  if (!pWayPointMan) return;
  const QString dir = QStringLiteral(OCPN_QT_MARKICONS_DIR);
  if (dir.isEmpty()) return;
  for (const IconDef& d : kIcons) {
    const QImage img = renderSvg(dir + "/" + QString::fromLatin1(d.file), 36);
    if (!img.isNull())
      pWayPointMan->AddMarkIcon(QString::fromLatin1(d.key),
                                QString::fromLatin1(d.key), img);
  }
}

}  // namespace ocpn::qtui
