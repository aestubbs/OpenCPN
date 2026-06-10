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
 * Implement sg_helpers.h.
 */

#include "sg_helpers.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>

namespace ocpn::qtui::sg {

QSGGeometryNode* makeFlatColorNode(const QColor& color,
                                   QSGGeometry::DrawingMode mode,
                                   int vertex_count, float line_width) {
  auto* geo =
      new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), vertex_count);
  geo->setDrawingMode(mode);
  geo->setLineWidth(line_width);

  auto* mat = new QSGFlatColorMaterial();
  mat->setColor(color);

  auto* node = new QSGGeometryNode();
  node->setGeometry(geo);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(mat);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

QSGGeometryNode* makeTextureNode(QSGTexture* texture,
                                 QSGGeometry::DrawingMode mode,
                                 int vertex_count, bool blending) {
  auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(),
                              vertex_count);
  geo->setDrawingMode(mode);

  auto* mat = new QSGTextureMaterial();
  mat->setTexture(texture);
  if (blending) mat->setFlag(QSGMaterial::Blending);

  auto* node = new QSGGeometryNode();
  node->setGeometry(geo);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(mat);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

}  // namespace ocpn::qtui::sg
