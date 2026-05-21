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
 * ChartLayer -- adapts any `ChartProvider` to the `Layer` interface so it
 * plugs into the LayerCompositor. World-anchored, always.
 *
 * One ChartLayer wraps one ChartProvider. Multiple charts = multiple
 * ChartLayers; quilting / multi-chart composition is a future concern (a
 * higher-level Layer that owns a set of ChartProviders and renders them
 * in priority order, or a tile-managing provider that owns sub-providers).
 */

#ifndef OCPN_QT_CHART_LAYER_H_
#define OCPN_QT_CHART_LAYER_H_

#include <memory>

#include "layer.h"

namespace ocpn::qtui {

class ChartProvider;
class Viewport;

class ChartLayer : public Layer {
  Q_OBJECT

public:
  /** Takes ownership of `provider`. The `viewport` reference must outlive
   *  this ChartLayer; typically owned by ChartCanvas. */
  ChartLayer(ChartProvider* provider, const Viewport* viewport,
             QObject* parent = nullptr);
  ~ChartLayer() override;

  QString id() const override;
  QString name() const override;
  Anchor anchor() const override { return WorldAnchored; }
  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

  ChartProvider* provider() const { return m_provider.get(); }

private:
  std::unique_ptr<ChartProvider> m_provider;
  const Viewport* m_viewport;  // non-owning
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_LAYER_H_
