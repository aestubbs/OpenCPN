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
 * Layer -- abstract base for a single visual contributor in the new Qt-Quick
 * chart renderer (P2.2).
 *
 * Every visual element -- core or plugin -- is a uniform Layer. Each Layer
 * owns one scene-graph subtree, builds/updates it from its own data on
 * demand, and emits dirty() when the subtree needs re-syncing. The Layer
 * knows nothing about other Layers; the LayerCompositor (P2.3) assembles
 * them.
 *
 * See docs/QT_MIGRATION.md §6 and docs/QT_MIGRATION_TASKS.md Phase 2 for
 * the surrounding architecture (three-tier scene graph: World-anchored,
 * Display-anchored, QML HUD above).
 */

#ifndef OCPN_QT_LAYER_H_
#define OCPN_QT_LAYER_H_

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QQuickWindow;
class QSGNode;
QT_END_NAMESPACE

namespace ocpn::qtui {

class Layer : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
  Q_PROPERTY(int zOrder READ zOrder WRITE setZOrder NOTIFY zOrderChanged)
  Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

public:
  /** Which top-level transform the Layer's subtree attaches under. */
  enum Anchor {
    WorldAnchored,    ///< chart coordinates -- pan/zoom moves the subtree
    DisplayAnchored,  ///< screen pixels -- immune to pan/zoom
  };
  Q_ENUM(Anchor)

  explicit Layer(QObject* parent = nullptr) : QObject(parent) {}
  ~Layer() override = default;

  /** Stable identifier (used by `OcpnConfig` to persist visible/zOrder/
   *  opacity per Layer at P2.10). Must be unique within the compositor. */
  virtual QString id() const = 0;

  /** Human-readable name (UI / debug). */
  virtual QString name() const = 0;

  /** Which tier the Layer belongs to. Constant for the Layer's lifetime. */
  virtual Anchor anchor() const = 0;

  /**
   * Build or update the Layer's scene-graph subtree.
   *
   * Called by the LayerCompositor when the Layer is dirty or first attached.
   * The `old_subtree` argument is the previously-returned subtree (nullptr
   * on first call). The Layer may mutate it in place and return it, or
   * delete it and return a fresh node. The compositor takes ownership of
   * the returned node and parents it under the appropriate transform root.
   * Returning nullptr removes the Layer's subtree from the scene.
   *
   * @param old_subtree the Layer's previous subtree, or nullptr
   * @param window      the QQuickWindow (for createTextureFromImage etc.)
   * @return            the subtree to attach this frame (may be == old)
   */
  virtual QSGNode* updateSubtree(QSGNode* old_subtree,
                                 QQuickWindow* window) = 0;

  // --- Composition properties (compositor honours these per frame) ---

  bool visible() const { return m_visible; }
  void setVisible(bool v) {
    if (m_visible == v) return;
    m_visible = v;
    Q_EMIT visibleChanged();
    Q_EMIT dirty();
  }

  int zOrder() const { return m_z_order; }
  void setZOrder(int z) {
    if (m_z_order == z) return;
    m_z_order = z;
    Q_EMIT zOrderChanged();
    Q_EMIT dirty();
  }

  qreal opacity() const { return m_opacity; }
  void setOpacity(qreal o) {
    if (m_opacity == o) return;
    m_opacity = o;
    Q_EMIT opacityChanged();
    Q_EMIT dirty();
  }

  /** Owner tag -- core subsystem name (e.g. "core.ais", "core.routes") or
   *  plugin id. Informational; surfaced in UI / config. */
  QString owner() const { return m_owner; }
  void setOwner(const QString& o) { m_owner = o; }

  /** Whether the compositor should persist this Layer's visible / zOrder /
   *  opacity across runs (P2.10). True for stable, user-arranged Layers
   *  (AIS, routes, plugin overlays). Transient or data-driven Layers whose
   *  id / z-order is computed (e.g. dynamically loaded chart cells) override
   *  to false so they don't clutter config or fight their computed state. */
  virtual bool persistState() const { return true; }

Q_SIGNALS:
  /** Emit when the subtree needs rebuilding (data changed, properties
   *  changed). The compositor connects and schedules an update. */
  void dirty();

  void visibleChanged();
  void zOrderChanged();
  void opacityChanged();

private:
  bool m_visible = true;
  int m_z_order = 0;
  qreal m_opacity = 1.0;
  QString m_owner;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_LAYER_H_
