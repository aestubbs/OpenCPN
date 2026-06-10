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
 * GridLayer -- the lat/lon graticule overlay (wx "Show Grid", P2.20):
 * labelled meridians and parallels at a "nice" interval chosen from the
 * current scale, spanning the visible view. World-anchored, rebuilt on
 * every viewport change (the lines track the view, unlike the nav
 * overlays which only rebuild on zoom); gated by DisplayConfig::showGrid.
 */

#ifndef OCPN_QT_GRID_LAYER_H_
#define OCPN_QT_GRID_LAYER_H_

#include <QHash>
#include <QImage>

#include "nav_layer.h"

namespace ocpn::qtui {

class GridLayer : public NavLayer {
  Q_OBJECT
public:
  GridLayer(NavDataProvider* provider, const Viewport* viewport,
            QObject* parent = nullptr);
  QString id() const override { return QStringLiteral("core.grid"); }
  QString name() const override { return QStringLiteral("Grid"); }

  QSGNode* updateSubtree(QSGNode* old, QQuickWindow* window) override;

private:
  const Viewport* m_vp;
  QSGNode* m_root = nullptr;
  // Label raster cache: text -> rendered image. QPainter text
  // rasterization dominated the per-pan rebuild (PERF option 1); the
  // texture upload below is already cached by TextureCacheNode.
  QHash<QString, QImage> m_label_cache;  // stable root (StaticNavLayer retention pattern)
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_GRID_LAYER_H_
