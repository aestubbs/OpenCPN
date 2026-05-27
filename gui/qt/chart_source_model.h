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
 * ChartSourceModel -- the user's chart directories, exposed to the Options >
 * Charts > Chart Files page. Replaces the build-time OCPN_QT_TEST_ENC path
 * with a runtime, persisted list (ConfigStore key "chartDirs"). ChartCanvas
 * owns one and re-scans the configured directories when the list changes or a
 * rescan is requested. The scan itself (cataloguing cells, decoding on demand)
 * stays in ChartCanvas/ChartWorker; this model is just the source list + the
 * UI-facing status/actions.
 */

#ifndef OCPN_QT_CHART_SOURCE_MODEL_H_
#define OCPN_QT_CHART_SOURCE_MODEL_H_

#include <QObject>
#include <QString>
#include <QStringList>

namespace ocpn::qtui {

class ChartSourceModel : public QObject {
  Q_OBJECT
  // The configured chart directories (absolute paths), for the list view.
  Q_PROPERTY(QStringList directories READ directories NOTIFY changed)
  // A short human status line ("N cells in M directories", or progress).
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)

public:
  explicit ChartSourceModel(QObject* parent = nullptr);

  QStringList directories() const { return m_dirs; }
  QString status() const { return m_status; }
  bool scanning() const { return m_scanning; }

  /** Add a chart directory (a folder of .000 ENC cells). Accepts a plain path
   *  or a file:// URL (from QML FolderDialog). Triggers a rescan. */
  Q_INVOKABLE void addDirectory(const QString& path);
  /** Remove the directory at `index` and rescan. */
  Q_INVOKABLE void removeDirectory(int index);
  /** Re-scan all configured directories. */
  Q_INVOKABLE void rescan();

  /** Set the status line / spinner (called by ChartCanvas during a scan). */
  void setStatus(const QString& text, bool scanning);
  /** Seed the list from a path if it is currently empty (first-run migration
   *  from the build-time OCPN_QT_TEST_ENC). Does not trigger a rescan. */
  void seedIfEmpty(const QString& path);

Q_SIGNALS:
  void changed();         // the directory list changed (persist + reload)
  void statusChanged();   // status/scanning text changed
  void rescanRequested(); // user asked for (or a change implies) a rescan

private:
  void load();
  void save() const;

  QStringList m_dirs;
  QString m_status;
  bool m_scanning = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_SOURCE_MODEL_H_
