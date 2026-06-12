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
 * CommPrioritiesModel (P3.6 Connections): the per-connection data-source
 * priority editor's QML surface over CommBridge's five priority maps
 * (position / speed-course / heading / variation / satellites). Each map
 * is an ordered '|'-separated source list; moving a source reorders the
 * map and applies + persists it (wx PriorityDlg parity).
 */

#ifndef OCPN_QT_COMM_PRIORITIES_MODEL_H_
#define OCPN_QT_COMM_PRIORITIES_MODEL_H_

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <QJSEngine>
#include <QQmlEngine>

namespace ocpn::qtui {

class CommPrioritiesModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

public:
  static CommPrioritiesModel& instance() {
    static CommPrioritiesModel inst;
    return inst;
  }
  static CommPrioritiesModel* create(QQmlEngine*, QJSEngine*) {
    CommPrioritiesModel* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  /** The five data categories, in map order. */
  Q_INVOKABLE QStringList categories() const;
  /** Priority-ordered source identifiers for a category. */
  Q_INVOKABLE QStringList sourcesFor(int category) const;
  /** The currently-active source index within a category (-1 unknown). */
  Q_INVOKABLE int activeIndex(int category) const;
  /** Move a source up (delta -1) / down (+1); applies + persists. */
  Q_INVOKABLE void move(int category, int index, int delta);
  /** Forget all learned sources/orders (wx Clear All). */
  Q_INVOKABLE void clearAll();
  /** Re-read from the bridge (sources appear as data arrives). */
  Q_INVOKABLE void refresh();

Q_SIGNALS:
  void changed();

private:
  // Private so the QML engine cannot default-construct a second
  // instance (see the singletonConstructionMode note in ui_config.h).
  explicit CommPrioritiesModel(QObject* parent = nullptr);
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_COMM_PRIORITIES_MODEL_H_
