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
 * Implement layer_compositor.h.
 */

#include "layer_compositor.h"

#include <algorithm>

#include <QSet>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGTransformNode>
#include <QVarLengthArray>

#include "model/ocpn_config.h"

namespace ocpn::qtui {

LayerCompositor::LayerCompositor(QObject* parent) : QObject(parent) {}

LayerCompositor::~LayerCompositor() {
  // Qt SG owns parented nodes; we only need to delete the Layer objects.
  for (Layer* l : m_layers) delete l;
}

void LayerCompositor::addLayer(Layer* layer) {
  if (!layer) return;
  const QString id = layer->id();
  if (m_entries_by_id.contains(id)) {
    // Duplicate id -- drop the new one to avoid clobbering, but take
    // ownership so the caller's `new Foo()` doesn't leak.
    qWarning("LayerCompositor: duplicate Layer id '%s' rejected",
             qUtf8Printable(id));
    delete layer;
    return;
  }

  Entry e;
  e.layer = layer;
  m_entries_by_id.insert(id, e);
  m_layers.append(layer);

  // Restore any persisted visible / zOrder / opacity before wiring signals
  // and the first sync, so the Layer appears in its saved state.
  restoreLayerState(layer);

  connect(layer, &Layer::dirty, this, [this, layer]() {
    onLayerDirty(layer);
  });
  // Visibility / z-order / opacity changes alter the *structure* of a root's
  // child list (which nodes attach, in what order, whether wrapped in an
  // opacity node), so they must trigger the re-attach path -- unlike a plain
  // data dirty(), which only re-runs updateSubtree in place. setVisible /
  // setOpacity also emit dirty() (handled above for the subtree refresh); the
  // structural flag is what makes syncOneRoot actually re-attach.
  auto markStructural = [this]() {
    m_structure_dirty = true;
    emit changed();
  };
  connect(layer, &Layer::visibleChanged, this, markStructural);
  connect(layer, &Layer::zOrderChanged, this, markStructural);
  connect(layer, &Layer::opacityChanged, this, markStructural);

  m_structure_dirty = true;  // a newly added layer must be attached next sync
  emit changed();
}

void LayerCompositor::removeLayer(const QString& id) {
  auto it = m_entries_by_id.find(id);
  if (it == m_entries_by_id.end()) return;

  Layer* l = it.value().layer;
  m_layers.removeAll(l);
  // Detaching from Qt SG happens at next syncToScene: removeAllChildNodes
  // is called per anchor root and the layer's subtree just won't be
  // re-added. The wrapper/subtree nodes are reachable until then via
  // their parent root; deleting them here would race the render thread.
  // For now leak them through to next sync, then forget. Cleaner once we
  // run sync as part of removeLayer.
  m_entries_by_id.erase(it);
  delete l;

  m_structure_dirty = true;  // the removed layer's subtree must be detached
  emit changed();
}

Layer* LayerCompositor::layer(const QString& id) const {
  auto it = m_entries_by_id.constFind(id);
  return it == m_entries_by_id.cend() ? nullptr : it.value().layer;
}

QList<Layer*> LayerCompositor::layersFor(Layer::Anchor anchor) const {
  QList<Layer*> out;
  out.reserve(m_layers.size());
  for (Layer* l : m_layers)
    if (l->anchor() == anchor) out.append(l);
  return out;
}

void LayerCompositor::saveState() const {
  if (!m_config) return;
  m_config->beginGroup(QStringLiteral("layers"));
  for (Layer* l : m_layers) {
    if (!l->persistState()) continue;
    m_config->beginGroup(l->id());
    m_config->setValue(QStringLiteral("visible"), l->visible());
    m_config->setValue(QStringLiteral("zOrder"), l->zOrder());
    m_config->setValue(QStringLiteral("opacity"), l->opacity());
    m_config->endGroup();
  }
  m_config->endGroup();
  m_config->sync();
}

void LayerCompositor::restoreLayerState(Layer* l) const {
  if (!m_config || !l || !l->persistState()) return;
  m_config->beginGroup(QStringLiteral("layers"));
  m_config->beginGroup(l->id());
  if (m_config->contains(QStringLiteral("visible")))
    l->setVisible(m_config->value(QStringLiteral("visible")).toBool());
  if (m_config->contains(QStringLiteral("zOrder")))
    l->setZOrder(m_config->value(QStringLiteral("zOrder")).toInt());
  if (m_config->contains(QStringLiteral("opacity")))
    l->setOpacity(m_config->value(QStringLiteral("opacity")).toReal());
  m_config->endGroup();
  m_config->endGroup();
}

void LayerCompositor::onLayerDirty(Layer* l) {
  auto it = m_entries_by_id.find(l->id());
  if (it != m_entries_by_id.end()) it.value().dirty = true;
  emit changed();
}

void LayerCompositor::syncToScene(QSGTransformNode* world_root,
                                  QSGTransformNode* display_root,
                                  QQuickWindow* window) {
  if (world_root) syncOneRoot(world_root, Layer::WorldAnchored, window);
  if (display_root) syncOneRoot(display_root, Layer::DisplayAnchored, window);
  // Any structural change has now been applied to both roots.
  m_structure_dirty = false;
}

void LayerCompositor::syncOneRoot(QSGTransformNode* root,
                                  Layer::Anchor anchor,
                                  QQuickWindow* window) {
  // Pick visible Layers for this anchor, sorted by zOrder ascending
  // (smaller z is drawn first / underneath).
  QList<Layer*> to_render;
  for (Layer* l : m_layers)
    if (l->anchor() == anchor && l->visible()) to_render.append(l);
  std::sort(to_render.begin(), to_render.end(),
            [](Layer* a, Layer* b) { return a->zOrder() < b->zOrder(); });

  // Update each dirty Layer's subtree. The compositor takes ownership of
  // whatever updateSubtree returns; if a new node, the old one is freed
  // when its parent (the wrapper or root) drops it below.
  //
  // `structural` tracks whether the root's child list must be rebuilt this
  // frame. It starts from the composition flag (add/remove/visible/zorder/
  // opacity) and additionally trips if any dirty layer hands back a *new*
  // node pointer -- the only cases that change the attached node set or order.
  // A layer that mutates its subtree in place (returns the same pointer) does
  // NOT trip it, so the renderer keeps its batches.
  bool structural = m_structure_dirty;
  for (Layer* l : to_render) {
    Entry& e = m_entries_by_id[l->id()];
    if (e.dirty || !e.subtree) {
      QSGNode* updated = l->updateSubtree(e.subtree, window);
      if (updated != e.subtree) {
        // Layer returned a different node; the old one is detached from
        // any wrapper next, so we can safely delete it here.
        if (e.subtree) delete e.subtree;
        structural = true;
      }
      e.subtree = updated;
      e.dirty = false;
    }
  }

  // Pure pan/zoom (no dirty layer) and in-place subtree updates leave the
  // root's children exactly as attached last frame. Re-attaching them every
  // frame is what forced the Qt scene-graph renderer to rebuild all batches
  // and made pan/zoom/route-drag "step": skip it unless something structural
  // actually changed. When nothing changed, only the world-root matrix (set
  // by ChartCanvas) moves, and the whole subtree follows on the GPU.
  if (!structural) return;

  // Build the desired ordered list of nodes to attach (each layer's subtree,
  // or its opacity wrapper when opacity < 1). Node IDENTITY is kept stable
  // across frames -- the same subtree/wrapper pointer is reused -- so the
  // reconcile below can detect "already in place" and leave it untouched.
  QVarLengthArray<QSGNode*, 32> desired;
  desired.reserve(to_render.size());
  for (Layer* l : to_render) {
    Entry& e = m_entries_by_id[l->id()];
    if (!e.subtree) continue;
    QSGNode* node;
    if (l->opacity() < 1.0) {
      if (!e.wrapper) e.wrapper = new QSGOpacityNode();
      e.wrapper->setOpacity(l->opacity());
      // Ensure the subtree is the wrapper's sole child.
      if (e.subtree->parent() != e.wrapper) {
        if (e.subtree->parent()) e.subtree->parent()->removeChildNode(e.subtree);
        e.wrapper->removeAllChildNodes();
        e.wrapper->appendChildNode(e.subtree);
      }
      node = e.wrapper;
    } else {
      // Full opacity: attach the subtree directly. Unwrap if it was wrapped.
      if (e.wrapper && e.subtree->parent() == e.wrapper)
        e.wrapper->removeChildNode(e.subtree);
      node = e.subtree;
    }
    desired.append(node);
  }

  // Reconcile the root's children to `desired` with the MINIMAL set of
  // attach/detach/move operations. Adding or evicting one cell then touches
  // only that one node -- every other layer's subtree stays attached, so the
  // Qt scene-graph renderer keeps its batches for them instead of re-batching
  // the whole ~14k-node scene (the 600-800ms cell-load hitch while panning).
  //
  // 1) Detach any current child that's no longer wanted (e.g. an evicted cell).
  QSet<QSGNode*> wanted;
  wanted.reserve(desired.size());
  for (QSGNode* n : desired) wanted.insert(n);
  QVarLengthArray<QSGNode*, 16> stale;
  for (QSGNode* c = root->firstChild(); c; c = c->nextSibling())
    if (!wanted.contains(c)) stale.append(c);
  for (QSGNode* c : stale) root->removeChildNode(c);

  // 2) Walk `desired` in order; only move/insert a node that isn't already in
  // its correct slot. A node already in place costs nothing (no dirty flag).
  QSGNode* prev = nullptr;
  for (QSGNode* d : desired) {
    QSGNode* expected = prev ? prev->nextSibling() : root->firstChild();
    if (expected != d) {
      if (d->parent() == root) root->removeChildNode(d);
      if (prev)
        root->insertChildNodeAfter(d, prev);
      else if (root->firstChild())
        root->insertChildNodeBefore(d, root->firstChild());
      else
        root->appendChildNode(d);
    }
    prev = d;
  }
}

}  // namespace ocpn::qtui
