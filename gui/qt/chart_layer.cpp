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
 * Implement chart_layer.h.
 */

#include "chart_layer.h"

#include "chart_provider.h"
#include "viewport.h"

namespace ocpn::qtui {

ChartLayer::ChartLayer(ChartProvider* provider, const Viewport* viewport,
                       QObject* parent)
    : Layer(parent), m_provider(provider), m_viewport(viewport) {
  setOwner("core.chart");
  // Provider data changes → mark the wrapping Layer dirty so the compositor
  // re-runs updateSubtree.
  if (m_provider) {
    connect(m_provider.get(), &ChartProvider::changed, this, &Layer::dirty);
  }
  // NOTE: we deliberately do NOT auto-dirty on viewport changes. Raster
  // providers don't need a re-render when the viewport pans/zooms (the
  // WorldAnchored root transform handles screen mapping). Vector
  // providers that DO need viewport-driven re-rendering (level-of-
  // detail) subscribe to the Viewport themselves and emit
  // ChartProvider::changed.
}

ChartLayer::~ChartLayer() = default;

QString ChartLayer::id() const {
  return m_provider ? m_provider->id() : QStringLiteral("chart.empty");
}

QString ChartLayer::name() const {
  return m_provider ? m_provider->name() : QStringLiteral("(empty)");
}

QSGNode* ChartLayer::updateSubtree(QSGNode* old, QQuickWindow* window) {
  if (!m_provider || !m_viewport) return nullptr;
  return m_provider->renderChart(old, *m_viewport, window);
}

}  // namespace ocpn::qtui
