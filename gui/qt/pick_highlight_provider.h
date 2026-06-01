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
 * PickHighlightProvider -- draws a transient highlight around the feature
 * currently shown in the object-query popup (F). The S-57 object query
 * already returns each hit feature's geometry (lon/lat shape + kind); this
 * provider outlines the CURRENT one in the S-52 highlight colour so the
 * mariner can see which object the popup describes, and the outline follows
 * the prev/next stepping through the stacked features.
 *
 * World-anchored (vertices in world coords x = lon, y = -lat, so it pans/
 * zooms with the chart) but drawn at a constant pixel width like the chart
 * boundary grid. A point feature gets a small ring; a line its polyline; an
 * area its closed outline. Empty shape -> nothing drawn.
 */

#ifndef OCPN_QT_PICK_HIGHLIGHT_PROVIDER_H_
#define OCPN_QT_PICK_HIGHLIGHT_PROVIDER_H_

#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>

#include "chart_provider.h"
#include "s52_sg.h"  // s52sg::QueryGeom

namespace ocpn::qtui {

class PickHighlightProvider : public ChartProvider {
  Q_OBJECT

public:
  // `viewport` (optional) is watched so the point-feature ring stays a fixed
  // pixel size as the chart zooms (line/area outlines are world-anchored and
  // need no rebuild). Pass nullptr to skip the subscription.
  explicit PickHighlightProvider(QString id, const Viewport* viewport = nullptr,
                                 QObject* parent = nullptr);

  QString id() const override { return m_id; }
  QString name() const override { return QStringLiteral("Pick highlight"); }

  // The highlight is screen-anchored content over the whole view; report
  // world-spanning bounds so it is never culled by extent.
  double northLat() const override { return 90.0; }
  double southLat() const override { return -90.0; }
  double westLon() const override { return -180.0; }
  double eastLon() const override { return 180.0; }

  /** Set the highlighted feature's geometry (lon/lat shape + kind). An empty
   *  shape clears the highlight. Emits changed(). */
  void setShape(const QList<QPointF>& shape, s52sg::QueryGeom geom);

  QSGNode* renderChart(QSGNode* old_subtree, const Viewport& viewport,
                       QQuickWindow* window) override;

private:
  QString m_id;
  const Viewport* m_viewport = nullptr;  // watched for zoom (ring pixel-size)
  QList<QPointF> m_shape;  // (lon, lat)
  s52sg::QueryGeom m_geom = s52sg::QueryGeom::Area;
  // S-52 highlight: a bright magenta-ish outline (the ECDIS "cursor pick"
  // accent). Opaque so it reads over any chart colour.
  QColor m_color{255, 0, 200, 255};
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_PICK_HIGHLIGHT_PROVIDER_H_
