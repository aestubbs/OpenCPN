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
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace ocpn::qtui {

class ChartSourceModel : public QObject {
  Q_OBJECT
  // The configured chart directories (absolute paths), for the list view.
  Q_PROPERTY(QStringList directories READ directories NOTIFY changed)
  // Named chart groups (subsets of `directories`) + the active selection.
  // Each entry: { name, dirs (QStringList), dirCount }.
  Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
  // Index into `groups` of the active group, or -1 for "All charts".
  Q_PROPERTY(int activeGroup READ activeGroup WRITE setActiveGroup
                 NOTIFY activeGroupChanged)
  // A short human status line ("N cells in M directories", or progress).
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)

public:
  explicit ChartSourceModel(QObject* parent = nullptr);

  QStringList directories() const { return m_dirs; }
  QString status() const { return m_status; }
  bool scanning() const { return m_scanning; }

  /** The directories that should actually be loaded: all of them when no group
   *  is active, else the active group's membership intersected with the live
   *  directory list. ChartCanvas iterates this, not directories(). */
  QStringList activeDirectories() const;

  QVariantList groups() const;
  int activeGroup() const { return m_active_group; }

  /** Add a chart directory (a folder of .000 ENC cells). Accepts a plain path
   *  or a file:// URL (from QML FolderDialog). Triggers a rescan. */
  Q_INVOKABLE void addDirectory(const QString& path);
  /** Remove the directory at `index` and rescan. Also drops it from any group. */
  Q_INVOKABLE void removeDirectory(int index);
  /** Re-scan all configured directories. */
  Q_INVOKABLE void rescan();

  // --- Chart groups ---
  /** Create a named group; returns its index (or -1 if the name is blank). */
  Q_INVOKABLE int addGroup(const QString& name);
  Q_INVOKABLE void removeGroup(int index);
  Q_INVOKABLE void renameGroup(int index, const QString& name);
  /** Add/remove a directory's membership in a group. Reloads if it is active. */
  Q_INVOKABLE void setDirInGroup(int groupIndex, const QString& dir, bool member);
  /** The stored fields for `index` ({ name, dirs, dirCount }). */
  Q_INVOKABLE QVariantMap groupAt(int index) const;
  /** Select the active group (-1 = All charts). Triggers a reload. */
  Q_INVOKABLE void setActiveGroup(int index);

  /** Set the status line / spinner (called by ChartCanvas during a scan). */
  void setStatus(const QString& text, bool scanning);
  /** Seed the list from a path if it is currently empty (first-run migration
   *  from the build-time OCPN_QT_TEST_ENC). Does not trigger a rescan. */
  void seedIfEmpty(const QString& path);

Q_SIGNALS:
  void changed();          // the directory list changed (persist + reload)
  void groupsChanged();    // a group's name / membership / set changed
  void activeGroupChanged(); // the active-group selection changed
  void statusChanged();    // status/scanning text changed
  void rescanRequested();  // user asked for (or a change implies) a rescan

private:
  void load();
  void save() const;

  struct Group {
    QString name;
    QStringList dirs;
  };

  QStringList m_dirs;
  QVector<Group> m_groups;
  int m_active_group = -1;  // -1 = All charts
  QString m_status;
  bool m_scanning = false;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CHART_SOURCE_MODEL_H_
