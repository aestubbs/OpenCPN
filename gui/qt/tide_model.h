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
 * TideModel -- the user's tide/current harmonic data sets (Options > Charts >
 * Tides) and the bridge to the prediction engine. Holds a persisted list of
 * data files (.tcd binary harmonics, or a HARMONIC .IDX ascii set) and loads
 * them into the global TCMgr (`ptcmgr`) that the tide/current Layer queries.
 * P3.14 phase B. The model-touching code (TCMgr) lives in the .cpp so the
 * engine headers stay out of this header.
 */

#ifndef OCPN_QT_TIDE_MODEL_H_
#define OCPN_QT_TIDE_MODEL_H_

#include <QObject>
#include <QString>
#include <QStringList>

namespace ocpn::qtui {

class TideModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList dataSources READ dataSources NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(int stationCount READ stationCount NOTIFY changed)

public:
  explicit TideModel(QObject* parent = nullptr);

  QStringList dataSources() const { return m_sources; }
  QString status() const { return m_status; }
  bool ready() const { return m_ready; }
  int stationCount() const { return m_count; }

  /** Add a harmonic data file (.tcd / .IDX). Accepts a path or a file:// URL. */
  Q_INVOKABLE void addSource(const QString& path);
  Q_INVOKABLE void removeSource(int index);
  /** Re-create the engine from the current source list. */
  Q_INVOKABLE void reload();

  /** Seed the list from `path` if it is empty and the file exists (first-run
   *  bundled data). Does not load -- call reload() after. */
  void seedIfEmpty(const QString& path);

signals:
  void changed();

private:
  void load();        // read the persisted list
  void save() const;  // write the list
  void rebuild();     // (re)create TCMgr + LoadDataSources + set ptcmgr

  QStringList m_sources;
  QString m_status;
  bool m_ready = false;
  int m_count = 0;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_TIDE_MODEL_H_
