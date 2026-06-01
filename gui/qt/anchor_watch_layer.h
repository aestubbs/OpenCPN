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
 * AnchorWatchLayer -- world-anchored Layer drawing the anchor-watch circle
 * (P3.15). A single circle centred on the dropped-anchor position with radius
 * AlertEngine::anchorRadiusM; amber while inside, red on breach. State (and
 * the breach flag) is read from the AlertEngine, so the chart circle and the
 * alarm stay in lock-step. The circle is world-anchored: its radius is in
 * world (degree-equivalent) units, so the world transform scales it with zoom
 * like a real geographic circle.
 */

#ifndef OCPN_QT_ANCHOR_WATCH_LAYER_H_
#define OCPN_QT_ANCHOR_WATCH_LAYER_H_

#include "layer.h"

QT_BEGIN_NAMESPACE
class QSGNode;
QT_END_NAMESPACE

namespace ocpn::qtui {

class AlertEngine;

class AnchorWatchLayer : public Layer {
  Q_OBJECT

public:
  explicit AnchorWatchLayer(const AlertEngine* engine,
                            QObject* parent = nullptr);

  QString id() const override { return QStringLiteral("core.anchorwatch"); }
  QString name() const override { return QStringLiteral("Anchor watch"); }
  Anchor anchor() const override { return WorldAnchored; }
  bool persistState() const override { return false; }  // data-driven

  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

private:
  const AlertEngine* m_engine;
  QSGNode* m_root = nullptr;
  bool m_dirty = true;  // anchor position / radius / breach changed
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_ANCHOR_WATCH_LAYER_H_
