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
 * S52VectorChartProvider -- a ChartProvider backed by s52plib's
 * scene-graph emit path (P2.8c).
 *
 * Holds a decoded s52sg::Buffer (world-coordinate geometry produced by
 * s52plib::RenderObjectToSG) and builds a QSGGeometryNode subtree from it:
 * one node per primitive, vertices placed in world space (x = lon,
 * y = -lat), coloured by a flat-colour material. The geometry is static
 * in world coordinates -- the ChartCanvas viewport transform on the
 * World-anchored root does all the projection, so pan/zoom never
 * re-decodes and the chart stays vector-sharp at any scale.
 *
 * Contrast with RasterChartProvider, which uploads a single QImage as a
 * textured quad. Both share the ChartProvider boundary; the renderer
 * doesn't care which kind it composites.
 */

#ifndef OCPN_QT_S52_VECTOR_CHART_PROVIDER_H_
#define OCPN_QT_S52_VECTOR_CHART_PROVIDER_H_

#include <QList>
#include <QPointF>
#include <QString>

#include "chart_provider.h"
#include "s52_sg.h"

QT_BEGIN_NAMESPACE
class QSGTransformNode;
QT_END_NAMESPACE

namespace ocpn::qtui {

class S52VectorChartProvider : public ChartProvider {
  Q_OBJECT

public:
  // `viewport` is used only to billboard point symbols / text labels
  // (world-positioned but screen-fixed size): the provider watches it and
  // re-applies the counter-scale on pan/zoom. Static fills/lines ignore it.
  S52VectorChartProvider(QString id, s52sg::Buffer buffer, double north,
                         double south, double west, double east,
                         const Viewport* viewport, QObject* parent = nullptr);

  QString id() const override { return m_id; }
  QString name() const override { return QStringLiteral("S-52 chart"); }

  double northLat() const override { return m_north; }
  double southLat() const override { return m_south; }
  double westLon() const override { return m_west; }
  double eastLon() const override { return m_east; }

  QSGNode* renderChart(QSGNode* old_subtree, const Viewport& viewport,
                       QQuickWindow* window) override;

private:
  // One billboarded point item (symbol or text): a transform node placed
  // at the world anchor whose scale counters the viewport scale so the
  // content stays screen-pixel-sized.
  struct Billboard {
    QSGTransformNode* xform = nullptr;
    QPointF worldPos;  // (x=lon, y=-lat)
  };

  void updateBillboards(const Viewport& viewport);

  QString m_id;
  s52sg::Buffer m_buffer;
  double m_north, m_south, m_west, m_east;
  const Viewport* m_viewport;
  QList<Billboard> m_billboards;
  bool m_built = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_S52_VECTOR_CHART_PROVIDER_H_
