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
 * GshhsWorldProvider -- the always-on world background chart, built from
 * OpenCPN's bundled GSHHS crude-resolution coastline data
 * (data/gshhs/poly-c-1.dat). This is the "world scale basic chart" that the
 * legacy wx app draws under everything; it gives the Qt canvas a
 * recognisable land/sea map at any zoom, so the user can roam the globe and
 * see where the detailed ENC cells sit.
 *
 * Reads the GSHHS polygon file directly (a small, self-contained binary
 * format: a 1°x1° cell grid, each cell holding level-1 land contours in
 * micro-degrees) and emits world-coordinate geometry: a sea backdrop quad,
 * tessellated land fill (libtess2), and coastline outlines. Static in world
 * space (x = lon normalised to -180..180, y = -lat), so the viewport
 * transform does all projection -- no re-decode on pan/zoom.
 *
 * Crude resolution only (poly-c-1.dat). Lakes / rivers / borders and the
 * finer GSHHS qualities are not loaded; this is a backdrop, not a chart.
 */

#ifndef OCPN_QT_GSHHS_WORLD_PROVIDER_H_
#define OCPN_QT_GSHHS_WORLD_PROVIDER_H_

#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>

#include "chart_provider.h"

namespace ocpn::qtui {

class GshhsWorldProvider : public ChartProvider {
  Q_OBJECT

public:
  /** `poly_path` is the absolute path to poly-c-1.dat. The file is read and
   *  tessellated once, here, on the calling (main) thread -- it's crude
   *  resolution (~3.7 MB) so this is quick. */
  explicit GshhsWorldProvider(const QString& poly_path,
                              QObject* parent = nullptr);

  QString id() const override { return QStringLiteral("world.gshhs"); }
  QString name() const override { return QStringLiteral("World background"); }
  double northLat() const override { return 90.0; }
  double southLat() const override { return -90.0; }
  double westLon() const override { return -180.0; }
  double eastLon() const override { return 180.0; }

  QSGNode* renderChart(QSGNode* old_subtree, const Viewport& viewport,
                       QQuickWindow* window) override;

private:
  // Read poly-c-1.dat: tessellate level-1 land contours into m_land_tris
  // (world-coord triangle list) and keep the contours in m_coastlines for
  // the outline pass.
  void load(const QString& poly_path);

  QList<QPointF> m_land_tris;            // (x=lon, y=-lat) triangle list
  QList<QList<QPointF>> m_coastlines;    // closed contours, world coords
  QColor m_sea{170, 195, 220};
  QColor m_land{225, 213, 180};
  QColor m_coast{120, 110, 90};
  bool m_loaded = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_GSHHS_WORLD_PROVIDER_H_
