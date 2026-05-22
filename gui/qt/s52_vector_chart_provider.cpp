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
 * Implement s52_vector_chart_provider.h.
 */

#include "s52_vector_chart_provider.h"

#include <QColor>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>

#include <vector>

#include "viewport.h"

namespace ocpn::qtui {

namespace {
// World convention: x = lon, y = -lat (see viewport.h).
QSGGeometry::Point2D worldPoint(const s52sg::Vertex& v) {
  QSGGeometry::Point2D p;
  p.set(static_cast<float>(v.lon), static_cast<float>(-v.lat));
  return p;
}

// Expand a fill primitive into an independent triangle list. The Qt RHI
// backends (Metal/Vulkan/D3D) do not all support triangle fans -- Metal
// in particular rejects them ("Primitive topology 0x6 not supported") --
// and GLU tessellation emits fans and strips freely. Converting to a
// DrawTriangles list here keeps the s52plib emit GPU-agnostic and works
// on every backend.
std::vector<QSGGeometry::Point2D> expandToTriangles(const s52sg::Prim& prim) {
  const auto& v = prim.verts;
  std::vector<QSGGeometry::Point2D> out;
  if (v.size() < 3) return out;

  switch (prim.type) {
    case s52sg::PrimType::TriangleFan:
      // (v0, vi, vi+1) for i in 1..n-2
      out.reserve((v.size() - 2) * 3);
      for (size_t i = 1; i + 1 < v.size(); ++i) {
        out.push_back(worldPoint(v[0]));
        out.push_back(worldPoint(v[i]));
        out.push_back(worldPoint(v[i + 1]));
      }
      break;
    case s52sg::PrimType::TriangleStrip:
      // (vi, vi+1, vi+2) with winding alternation
      out.reserve((v.size() - 2) * 3);
      for (size_t i = 0; i + 2 < v.size(); ++i) {
        if (i & 1) {
          out.push_back(worldPoint(v[i + 1]));
          out.push_back(worldPoint(v[i]));
          out.push_back(worldPoint(v[i + 2]));
        } else {
          out.push_back(worldPoint(v[i]));
          out.push_back(worldPoint(v[i + 1]));
          out.push_back(worldPoint(v[i + 2]));
        }
      }
      break;
    case s52sg::PrimType::Triangles:
    default:
      out.reserve(v.size());
      for (const auto& vert : v) out.push_back(worldPoint(vert));
      break;
  }
  return out;
}
}  // namespace

S52VectorChartProvider::S52VectorChartProvider(QString id,
                                               s52sg::Buffer buffer,
                                               double north, double south,
                                               double west, double east,
                                               QObject* parent)
    : ChartProvider(parent),
      m_id(std::move(id)),
      m_buffer(std::move(buffer)),
      m_north(north),
      m_south(south),
      m_west(west),
      m_east(east) {}

QSGNode* S52VectorChartProvider::renderChart(QSGNode* old_subtree,
                                             const Viewport& /*viewport*/,
                                             QQuickWindow* /*window*/) {
  // Geometry is static in world coordinates -- build the node tree once
  // and reuse it. Pan/zoom is handled entirely by the World-anchored
  // root's transform, so there is nothing to rebuild per frame.
  if (old_subtree) return old_subtree;

  auto* root = new QSGNode();

  for (const s52sg::Prim& prim : m_buffer.prims) {
    if (prim.verts.empty()) continue;

    // Line features keep their strip topology (supported everywhere);
    // fills expand to an independent triangle list (fans aren't portable).
    if (prim.type == s52sg::PrimType::LineStrip) {
      auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),
                                  static_cast<int>(prim.verts.size()));
      geo->setDrawingMode(QSGGeometry::DrawLineStrip);
      geo->setLineWidth(prim.width);
      QSGGeometry::Point2D* v = geo->vertexDataAsPoint2D();
      for (size_t i = 0; i < prim.verts.size(); ++i)
        v[i] = worldPoint(prim.verts[i]);

      auto* mat = new QSGFlatColorMaterial();
      mat->setColor(QColor(prim.r, prim.g, prim.b, prim.a));
      auto* node = new QSGGeometryNode();
      node->setGeometry(geo);
      node->setFlag(QSGNode::OwnsGeometry);
      node->setMaterial(mat);
      node->setFlag(QSGNode::OwnsMaterial);
      root->appendChildNode(node);
      continue;
    }

    std::vector<QSGGeometry::Point2D> tris = expandToTriangles(prim);
    if (tris.empty()) continue;

    auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),
                                static_cast<int>(tris.size()));
    geo->setDrawingMode(QSGGeometry::DrawTriangles);
    QSGGeometry::Point2D* v = geo->vertexDataAsPoint2D();
    for (size_t i = 0; i < tris.size(); ++i) v[i] = tris[i];

    auto* mat = new QSGFlatColorMaterial();
    mat->setColor(QColor(prim.r, prim.g, prim.b, prim.a));

    auto* node = new QSGGeometryNode();
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    root->appendChildNode(node);
  }

  return root;
}

}  // namespace ocpn::qtui
