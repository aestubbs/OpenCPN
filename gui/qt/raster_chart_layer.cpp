/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "raster_chart_layer.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

namespace ocpn::qtui {

QSGNode* RasterChartLayer::updateSubtree(QSGNode* old, QQuickWindow* window) {
  auto* node = static_cast<QSGSimpleTextureNode*>(old);
  if (!node) {
    if (!window || m_image.isNull()) return nullptr;
    node = new QSGSimpleTextureNode();
    QSGTexture* tex = window->createTextureFromImage(
        m_image, QQuickWindow::TextureHasAlphaChannel);
    tex->setFiltering(QSGTexture::Linear);
    tex->setMipmapFiltering(QSGTexture::Linear);
    node->setTexture(tex);
    node->setOwnsTexture(true);
    node->setFiltering(QSGTexture::Linear);
    // Free the CPU copy once the GPU owns it.
    m_image = QImage();
  }
  node->setRect(m_rect);
  return node;
}

}  // namespace ocpn::qtui
