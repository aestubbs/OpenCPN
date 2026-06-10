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
 * RasterChartLayer (P2.7): one KAP chart as a textured quad in world
 * coordinates -- the QImage decoded off-thread becomes a scene-graph
 * texture stretched across the chart's linear-Mercator world rectangle.
 */

#ifndef OCPN_QT_RASTER_CHART_LAYER_H_
#define OCPN_QT_RASTER_CHART_LAYER_H_

#include <QImage>
#include <QRectF>

#include "layer.h"  // opencpn_qt_toolkit

namespace ocpn::qtui {

class RasterChartLayer : public Layer {
  Q_OBJECT
public:
  RasterChartLayer(const QString& id, const QImage& image,
                   const QRectF& worldRect, QObject* parent = nullptr)
      : Layer(parent), m_id(id), m_image(image), m_rect(worldRect) {}

  QString id() const override { return m_id; }
  QString name() const override { return m_id; }
  Anchor anchor() const override { return WorldAnchored; }

  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

private:
  QString m_id;
  QImage m_image;
  QRectF m_rect;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_RASTER_CHART_LAYER_H_
