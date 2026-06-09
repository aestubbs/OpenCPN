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
 * Implement area_pattern_material.h.
 */

#include "area_pattern_material.h"

#include <cstring>

#include <QMatrix4x4>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterialShader>
#include <QSGTexture>

namespace ocpn::qtui {

namespace {
// std140: mat4 (64) + vec2 kScale (8 @ offset 64) + float qt_Opacity (4 @ 72),
// block rounded up to a 16-byte multiple -> 80. Matches areapattern.{vert,frag}.
constexpr int kUboSize = 80;

// One vertex: world position + world offset from the cell reference. Same
// 16-byte layout as QSGGeometry::TexturedPoint2D (so the consumer can fill it
// with vertexDataAsTexturedPoint2D()), but a DEDICATED attribute set: the batch
// renderer special-cases TexturedPoint2D geometry for the built-in texture
// material and the shader rewriter then asserts when merging our custom-material
// nodes. attribute 0 is the position (transformed by qt_Matrix).
struct PatVertex {
  float x, y;    // location 0: world position
  float rx, ry;  // location 1: world offset
};
const QSGGeometry::AttributeSet& patAttributeSet() {
  static const QSGGeometry::Attribute attrs[] = {
      QSGGeometry::Attribute::create(0, 2, QSGGeometry::FloatType, true),
      QSGGeometry::Attribute::create(1, 2, QSGGeometry::FloatType, false),
  };
  static const QSGGeometry::AttributeSet set = {2, sizeof(PatVertex), attrs};
  return set;
}
}  // namespace

// ---- shader ---------------------------------------------------------------

class AreaPatternShader : public QSGMaterialShader {
 public:
  AreaPatternShader() {
    setShaderFileName(VertexStage,
                      QStringLiteral(":/shaders/areapattern.vert.qsb"));
    setShaderFileName(FragmentStage,
                      QStringLiteral(":/shaders/areapattern.frag.qsb"));
  }

  bool updateUniformData(RenderState& state, QSGMaterial* newMaterial,
                         QSGMaterial*) override {
    QByteArray* buf = state.uniformData();
    if (buf->size() < kUboSize) buf->resize(kUboSize);
    char* p = buf->data();

    const QMatrix4x4 m = state.combinedMatrix();
    std::memcpy(p + 0, m.constData(), 64);

    const auto* mat = static_cast<AreaPatternMaterial*>(newMaterial);
    const float k[2] = {mat->kScale.x(), mat->kScale.y()};
    std::memcpy(p + 64, k, 8);

    const float opacity = state.opacity();
    std::memcpy(p + 72, &opacity, 4);
    return true;
  }

  void updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                          QSGMaterial* newMaterial, QSGMaterial*) override {
    if (binding != 1) return;
    auto* mat = static_cast<AreaPatternMaterial*>(newMaterial);
    if (!mat->texture) return;
    // Nearest matches wx's pattern blit (and the GL path's GL_NEAREST); with the
    // shader's fract() wrap the sampler wrap mode is irrelevant.
    mat->texture->setFiltering(QSGTexture::Nearest);
    mat->texture->commitTextureOperations(state.rhi(),
                                          state.resourceUpdateBatch());
    *texture = mat->texture;
  }
};

// ---- material -------------------------------------------------------------

QSGMaterialType* AreaPatternMaterial::type() const {
  static QSGMaterialType t;
  return &t;
}

QSGMaterialShader* AreaPatternMaterial::createShader(
    QSGRendererInterface::RenderMode) const {
  return new AreaPatternShader;
}

int AreaPatternMaterial::compare(const QSGMaterial* other) const {
  const auto* o = static_cast<const AreaPatternMaterial*>(other);
  if (texture != o->texture) return texture < o->texture ? -1 : 1;
  // kScale differs per tile size / zoom; nodes with different kScale must not
  // batch into one draw (they would share a single uniform block).
  if (kScale.x() != o->kScale.x()) return kScale.x() < o->kScale.x() ? -1 : 1;
  if (kScale.y() != o->kScale.y()) return kScale.y() < o->kScale.y() ? -1 : 1;
  return 0;
}

// ---- builder --------------------------------------------------------------

QSGGeometryNode* makeAreaPatternNode(QSGTexture* texture, int vertex_count) {
  auto* geo = new QSGGeometry(patAttributeSet(), vertex_count);
  geo->setDrawingMode(QSGGeometry::DrawTriangles);

  auto* mat = new AreaPatternMaterial();
  mat->texture = texture;

  auto* node = new QSGGeometryNode();
  node->setGeometry(geo);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(mat);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

}  // namespace ocpn::qtui
