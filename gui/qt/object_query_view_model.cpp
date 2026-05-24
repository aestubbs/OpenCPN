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

namespace ocpn::qtui {

void ObjectQueryViewModel::setObjects(
    const QList<s52sg::QueryObject>& objs) {
  m_count = static_cast<int>(objs.size());
  QString t;
  for (const s52sg::QueryObject& o : objs) {
    if (!t.isEmpty()) t += QChar('\n');
    t += QStringLiteral("● ") + o.className + QChar('\n');
    for (const s52sg::QueryAttr& a : o.attrs)
      t += QStringLiteral("    %1: %2\n").arg(a.name, a.value);
  }
  m_text = t.trimmed();
  Q_EMIT changed();
}

void ObjectQueryViewModel::clear() {
  if (m_count == 0) return;
  m_count = 0;
  m_text.clear();
  Q_EMIT changed();
}

}  // namespace ocpn::qtui
