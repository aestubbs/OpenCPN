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
 * Implement object_query_view_model.h.
 */

#include "object_query_view_model.h"

#include <algorithm>

#include <QSet>

namespace ocpn::qtui {

namespace {
// S-57 / OGR bookkeeping attributes -- record ids, version, agency, feature
// ids, relationship indices. Not cartographic info, so hidden from the popup.
bool isInternalAttr(const QString& name) {
  static const QSet<QString> kSkip = {
      QStringLiteral("RCID"), QStringLiteral("PRIM"), QStringLiteral("GRUP"),
      QStringLiteral("OBJL"), QStringLiteral("RVER"), QStringLiteral("AGEN"),
      QStringLiteral("FIDN"), QStringLiteral("FIDS"), QStringLiteral("LNAM"),
      QStringLiteral("LNAM_REFS")};
  return kSkip.contains(name) || name.endsWith(QLatin1String("_RIND"));
}

// Specificity rank for ordering: points are the most specific thing you can
// click, then lines, then areas (and smaller areas before larger -- the
// cell-coverage polygon sorts last, the "root").
int geomRank(s52sg::QueryGeom g) {
  switch (g) {
    case s52sg::QueryGeom::Point: return 0;
    case s52sg::QueryGeom::Line: return 1;
    default: return 2;  // Area
  }
}
double bboxArea(const s52sg::QueryObject& o) {
  return (o.maxLon - o.minLon) * (o.maxLat - o.minLat);
}
}  // namespace

void ObjectQueryViewModel::setObjects(const QList<s52sg::QueryObject>& objs) {
  QList<s52sg::QueryObject> sorted = objs;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const s52sg::QueryObject& a, const s52sg::QueryObject& b) {
                     const int ra = geomRank(a.geom), rb = geomRank(b.geom);
                     if (ra != rb) return ra < rb;
                     return bboxArea(a) < bboxArea(b);  // smaller = more specific
                   });

  m_items.clear();
  for (const s52sg::QueryObject& o : sorted) {
    Item it;
    it.className = o.className;
    for (const s52sg::QueryAttr& a : o.attrs) {
      if (isInternalAttr(a.name)) continue;
      it.attrs += QStringLiteral("%1: %2\n").arg(a.name, a.value);
    }
    it.attrs = it.attrs.trimmed();
    m_items.append(it);
  }
  m_index = 0;
  Q_EMIT changed();
}

QString ObjectQueryViewModel::className() const {
  return (m_index >= 0 && m_index < m_items.size()) ? m_items[m_index].className
                                                    : QString();
}

QString ObjectQueryViewModel::text() const {
  return (m_index >= 0 && m_index < m_items.size()) ? m_items[m_index].attrs
                                                    : QString();
}

void ObjectQueryViewModel::next() {
  if (m_index + 1 < m_items.size()) {
    ++m_index;
    Q_EMIT changed();
  }
}

void ObjectQueryViewModel::prev() {
  if (m_index > 0) {
    --m_index;
    Q_EMIT changed();
  }
}

void ObjectQueryViewModel::clear() {
  if (m_items.isEmpty()) return;
  m_items.clear();
  m_index = 0;
  Q_EMIT changed();
}

}  // namespace ocpn::qtui
