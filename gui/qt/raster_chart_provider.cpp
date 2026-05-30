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
 * Implement raster_chart_provider.h.
 */

#include "raster_chart_provider.h"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>

#include "viewport.h"

namespace ocpn::qtui {

RasterChartProvider::RasterChartProvider(QString id, QImage image,
                                         double north_lat, double south_lat,
                                         double west_lon, double east_lon,
                                         QObject* parent)
    : ChartProvider(parent),
      m_id(std::move(id)),
      m_image(std::move(image)),
      m_north(north_lat),
      m_south(south_lat),
      m_west(west_lon),
      m_east(east_lon) {
  // World-space rect: x = lon, y = Mercator-lat (Y-down). Top-left is the
  // chart's north-west corner; height is the Mercator span north->south.
  const double yt = Viewport::latToWorldY(north_lat);  // top (north)
  const double yb = Viewport::latToWorldY(south_lat);  // bottom (south)
  m_world_rect = QRectF(west_lon, yt, east_lon - west_lon, yb - yt);
}

RasterChartProvider::~RasterChartProvider() {
  delete m_texture;
}

QSGNode* RasterChartProvider::renderChart(QSGNode* old_subtree,
                                          const Viewport& /*viewport*/,
                                          QQuickWindow* window) {
  // Raster doesn't care about the viewport -- the QSGImageNode rect is
  // in world coords; the WorldAnchored root transform handles screen
  // mapping.

  QSGImageNode* node = static_cast<QSGImageNode*>(old_subtree);
  if (!node) {
    if (!window) return nullptr;
    node = window->createImageNode();
    if (!node) return nullptr;
  }

  // Lazily upload the QImage -> QSGTexture (needs the window, hence not
  // in the constructor).
  if (!m_texture && window) {
    m_texture = window->createTextureFromImage(
        m_image,
        QQuickWindow::CreateTextureOptions(
            QQuickWindow::TextureCanUseAtlas |
            QQuickWindow::TextureHasAlphaChannel));
    if (m_texture) {
      node->setTexture(m_texture);
      // Bilinear filtering -- charts look bad with point sampling at
      // anything other than 1:1.
      node->setFiltering(QSGTexture::Linear);
      node->setMipmapFiltering(QSGTexture::Linear);
      node->setOwnsTexture(false);  // dtor owns m_texture
    }
  }

  // Destination rect in WORLD coordinates; the parent QSGTransformNode
  // (set by ChartCanvas from Viewport::transformMatrix) converts to
  // screen pixels.
  node->setRect(m_world_rect);

  return node;
}

}  // namespace ocpn::qtui
