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

#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGTransformNode>

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
  // Property changes already dirty the Layer via setters' Q_EMIT dirty();
  // but z-order changes also need composition re-sort, so trigger an
  // overall composition change too.
  connect(layer, &Layer::zOrderChanged, this, &LayerCompositor::changed);

  Q_EMIT changed();
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

  Q_EMIT changed();
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
  Q_EMIT changed();
}

void LayerCompositor::syncToScene(QSGTransformNode* world_root,
                                  QSGTransformNode* display_root,
                                  QQuickWindow* window) {
  if (world_root) syncOneRoot(world_root, Layer::WorldAnchored, window);
  if (display_root) syncOneRoot(display_root, Layer::DisplayAnchored, window);
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
  for (Layer* l : to_render) {
    Entry& e = m_entries_by_id[l->id()];
    if (e.dirty || !e.subtree) {
      QSGNode* updated = l->updateSubtree(e.subtree, window);
      if (updated != e.subtree && e.subtree) {
        // Layer returned a different node; the old one is detached from
        // any wrapper next, so we can safely delete it here.
        delete e.subtree;
      }
      e.subtree = updated;
      e.dirty = false;
    }
  }

  // Detach all current children of the root so we can re-attach in the
  // new order. Detaches do not delete; the nodes stay alive via our
  // Entry pointers (subtree and wrapper).
  root->removeAllChildNodes();

  for (Layer* l : to_render) {
    Entry& e = m_entries_by_id[l->id()];
    if (!e.subtree) continue;

    // Move subtree out of any previous parent (the wrapper from the prior
    // frame, if any) before re-parenting now.
    if (e.subtree->parent()) e.subtree->parent()->removeChildNode(e.subtree);

    if (l->opacity() < 1.0) {
      if (!e.wrapper) e.wrapper = new QSGOpacityNode();
      e.wrapper->setOpacity(l->opacity());
      // If wrapper was previously parented elsewhere, detach.
      if (e.wrapper->parent())
        e.wrapper->parent()->removeChildNode(e.wrapper);
      e.wrapper->removeAllChildNodes();
      e.wrapper->appendChildNode(e.subtree);
      root->appendChildNode(e.wrapper);
    } else {
      // Full opacity: skip the wrapper for slightly cheaper traversal.
      root->appendChildNode(e.subtree);
    }
  }
}

}  // namespace ocpn::qtui
