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
 * ObjectQueryViewModel -- the S-57 features found under a click, exposed to
 * QML for the object-query popup (P3.9).
 *
 * A click stacks every feature whose geometry is hit: the specific object
 * (a buoy / sounding) plus every AREA containing the point (depth area, sea
 * area, ... up to the cell coverage). Rather than dump them all, the popup
 * shows ONE at a time, ordered most-specific -> most-general, with prev/next
 * to step "up" toward the containing objects -- so the thing you clicked is
 * front and centre.
 */

#ifndef OCPN_QT_OBJECT_QUERY_VIEW_MODEL_H_
#define OCPN_QT_OBJECT_QUERY_VIEW_MODEL_H_

#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "s52_sg.h"

namespace ocpn::qtui {

class ObjectQueryViewModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool valid READ valid NOTIFY changed)
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(int index READ index NOTIFY changed)        // 0-based current
  Q_PROPERTY(QString className READ className NOTIFY changed)  // current
  Q_PROPERTY(QString text READ text NOTIFY changed)      // current attrs

public:
  using QObject::QObject;

  bool valid() const { return !m_items.isEmpty(); }
  int count() const { return static_cast<int>(m_items.size()); }
  int index() const { return m_index; }
  QString className() const;
  QString text() const;

  /** Geometry of the currently-shown feature (lon/lat shape + geom kind), for
   *  the on-chart pick highlight. Empty shape when nothing is selected. */
  QList<QPointF> currentShape() const;
  s52sg::QueryGeom currentGeom() const;

  /** Set the queried features (ordered specific->general; current = first). */
  void setObjects(const QList<s52sg::QueryObject>& objs);
  /** Step toward the more-general (containing) object. */
  Q_INVOKABLE void next();
  /** Step toward the more-specific object. */
  Q_INVOKABLE void prev();
  /** Clear the result (close the popup). */
  Q_INVOKABLE void clear();

signals:
  void changed();

private:
  struct Item {
    QString className;
    QString attrs;  // pre-formatted attribute lines
    QList<QPointF> shape;  // (lon, lat) geometry, for the pick highlight
    s52sg::QueryGeom geom = s52sg::QueryGeom::Area;
  };
  QList<Item> m_items;  // ordered most-specific -> most-general
  int m_index = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_OBJECT_QUERY_VIEW_MODEL_H_
