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
 * LayerCompositor -- owns two ordered Layer stacks and maps them onto the
 * ChartCanvas's two top-level transform-node roots (P2.3).
 *
 * Per docs/QT_MIGRATION.md §6: each visual contributor (core or plugin) is
 * a Layer. The compositor:
 *   - Owns the World-anchored and Display-anchored Layer lists.
 *   - Per frame, syncs each transform root's children to match the
 *     visible Layers in z-order, wrapped in an opacity node if
 *     opacity < 1.
 *   - Listens for `Layer::dirty()` and notifies the canvas.
 *   - Provides the lookup the per-Layer config persistence (P2.10) hangs
 *     off of: stable `Layer::id()` -> visible / zOrder / opacity.
 *
 * Plugin layers (Phase 4) plug in here by the same API: construct a Layer
 * subclass, hand it to the compositor with `addLayer()`, done.
 */

#ifndef OCPN_QT_LAYER_COMPOSITOR_H_
#define OCPN_QT_LAYER_COMPOSITOR_H_

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "layer.h"

class OcpnConfig;

QT_BEGIN_NAMESPACE
class QQuickWindow;
class QSGOpacityNode;
class QSGTransformNode;
QT_END_NAMESPACE

namespace ocpn::qtui {

class LayerCompositor : public QObject {
  Q_OBJECT

public:
  explicit LayerCompositor(QObject* parent = nullptr);
  ~LayerCompositor() override;

  /** Add a Layer. Takes ownership; the compositor deletes it on shutdown
   *  or on `removeLayer`. The Layer's `id()` must be unique. */
  void addLayer(Layer* layer);

  /** Remove and delete a Layer by id. The scene-graph subtree is detached
   *  on the next sync and freed. No-op if `id` is unknown. */
  void removeLayer(const QString& id);

  /** Find a Layer by id, or nullptr if not registered. */
  Layer* layer(const QString& id) const;

  /** Layers in registration order (not z-order). */
  QList<Layer*> layers() const { return m_layers; }

  /** Layers attached to a given anchor, in registration order. */
  QList<Layer*> layersFor(Layer::Anchor anchor) const;

  // --- Per-layer state persistence (P2.10) -----------------------------
  /**
   * Attach a config store backing per-Layer visible / zOrder / opacity.
   * Non-owning; must outlive the compositor. Once set, addLayer() restores
   * a persistable Layer's saved state on registration (so async-added
   * Layers still pick up their preferences), and saveState() writes the
   * current state back. Layers with persistState() == false are ignored.
   */
  void setConfig(OcpnConfig* config) { m_config = config; }

  /** Write every persistable Layer's visible / zOrder / opacity to the
   *  configured store (no-op without setConfig). Call on shutdown. */
  void saveState() const;

  /**
   * Synchronise the scene graph: rebuild the children of each transform
   * root from the current Layer state. Called from ChartCanvas::update
   * PaintNode every frame (cheap when nothing is dirty).
   *
   * For each anchor in turn:
   *   1. Pick visible Layers attached to that anchor, sort by zOrder.
   *   2. For each, call updateSubtree() if dirty; else reuse cached node.
   *   3. Detach all current children from the root, re-attach in z-order
   *      (wrapped in an opacity node where opacity < 1).
   */
  void syncToScene(QSGTransformNode* world_root,
                   QSGTransformNode* display_root,
                   QQuickWindow* window);

Q_SIGNALS:
  /** Emitted when any Layer dirties or composition changes; ChartCanvas
   *  connects this to QQuickItem::update(). */
  void changed();

private:
  struct Entry {
    Layer* layer = nullptr;            // owned by us
    QSGNode* subtree = nullptr;        // owned by Qt SG once parented
    QSGOpacityNode* wrapper = nullptr; // owned by Qt SG once parented
    bool dirty = true;                 // updateSubtree needed this frame
  };

  void onLayerDirty(Layer* l);
  void syncOneRoot(QSGTransformNode* root, Layer::Anchor anchor,
                   QQuickWindow* window);
  // Apply a persistable Layer's saved state from m_config (no-op if there's
  // no config or no stored entry for this Layer).
  void restoreLayerState(Layer* l) const;

  QList<Layer*> m_layers;                  // registration order
  QHash<QString, Entry> m_entries_by_id;
  OcpnConfig* m_config = nullptr;          // non-owning; per-layer persistence
  // The composition (set of visible layers, their z-order, opacity wrappers,
  // or a layer's subtree node identity) changed, so a root's children must be
  // re-attached. A pure pan/zoom (only the world-root matrix moves) or an
  // in-place subtree update (same node pointer) leaves this false, so
  // syncOneRoot skips the detach/re-attach that would otherwise force the Qt
  // scene-graph renderer to rebuild all batches every frame. Starts true so
  // the first sync attaches.
  bool m_structure_dirty = true;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_LAYER_COMPOSITOR_H_
