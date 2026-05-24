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
 * QML for the object-query popup (P3.9). ChartCanvas hit-tests the click
 * against the loaded chart providers and calls setObjects(); QML binds a
 * scrollable text view to the formatted result.
 */

#ifndef OCPN_QT_OBJECT_QUERY_VIEW_MODEL_H_
#define OCPN_QT_OBJECT_QUERY_VIEW_MODEL_H_

#include <QObject>
#include <QString>

#include "s52_sg.h"

namespace ocpn::qtui {

class ObjectQueryViewModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool valid READ valid NOTIFY changed)
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(QString text READ text NOTIFY changed)

public:
  using QObject::QObject;

  bool valid() const { return m_count > 0; }
  int count() const { return m_count; }
  QString text() const { return m_text; }

  /** Set the queried features (formats them into the display text). */
  void setObjects(const QList<s52sg::QueryObject>& objs);
  /** Clear the result (close the popup). Invokable from QML. */
  Q_INVOKABLE void clear();

Q_SIGNALS:
  void changed();

private:
  QString m_text;
  int m_count = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_OBJECT_QUERY_VIEW_MODEL_H_
