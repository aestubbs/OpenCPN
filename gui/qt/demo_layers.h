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
 * Two trivial Layer subclasses used by the Phase 2 scaffold to validate the
 * LayerCompositor wiring. Removed when real Layers (raster chart, S52,
 * AIS, routes) replace them in P2.7 / P2.8 / P2.11.
 */

#ifndef OCPN_QT_DEMO_LAYERS_H_
#define OCPN_QT_DEMO_LAYERS_H_

#include <QColor>
#include <QRectF>

#include "layer.h"

namespace ocpn::qtui {

/** A single coloured rectangle, fixed in screen coordinates. */
class DisplayRectLayer : public Layer {
  Q_OBJECT
public:
  DisplayRectLayer(QString id, const QRectF& rect, QColor colour,
                   QObject* parent = nullptr);

  QString id() const override { return m_id; }
  QString name() const override { return m_id; }
  Anchor anchor() const override { return DisplayAnchored; }
  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

private:
  QString m_id;
  QRectF m_rect;
  QColor m_colour;
};

/** A single coloured rectangle in "world" coordinates, used here to
 *  demonstrate the WorldAnchored transform path. In a real layer the
 *  geometry would be lat/lon-derived and the transform would be the
 *  viewport matrix; for the scaffold we rotate the rect to make the
 *  transform path visually obvious. */
class WorldRotatingRectLayer : public Layer {
  Q_OBJECT
public:
  WorldRotatingRectLayer(QString id, const QRectF& rect, QColor colour,
                         QObject* parent = nullptr);

  QString id() const override { return m_id; }
  QString name() const override { return m_id; }
  Anchor anchor() const override { return WorldAnchored; }
  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

  /** Set the rotation in degrees. Marks the Layer dirty. */
  void setRotationDeg(qreal deg);

private:
  QString m_id;
  QRectF m_rect;
  QColor m_colour;
  qreal m_rotation_deg = 0.0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_DEMO_LAYERS_H_
