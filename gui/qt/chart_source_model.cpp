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
 * Implement chart_source_model.h.
 */

#include "chart_source_model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "config_store.h"

namespace {
constexpr char kConfigKey[] = "chartDirs";
constexpr char kGroupsKey[] = "chartGroups";
constexpr char kActiveGroupKey[] = "chartActiveGroup";
}

namespace ocpn::qtui {

ChartSourceModel::ChartSourceModel(QObject* parent) : QObject(parent) {
  load();
}

void ChartSourceModel::load() {
  ConfigStore& cfg = ConfigStore::instance();
  m_dirs.clear();
  const QJsonArray arr =
      QJsonDocument::fromJson(cfg.getString(kConfigKey).toUtf8()).array();
  for (const QJsonValue& v : arr) {
    const QString p = v.toString();
    if (!p.isEmpty()) m_dirs << p;
  }

  m_groups.clear();
  const QJsonArray garr =
      QJsonDocument::fromJson(cfg.getString(kGroupsKey).toUtf8()).array();
  for (const QJsonValue& v : garr) {
    const QJsonObject o = v.toObject();
    Group g;
    g.name = o.value("name").toString();
    if (g.name.isEmpty()) continue;
    for (const QJsonValue& d : o.value("dirs").toArray()) {
      const QString p = d.toString();
      if (!p.isEmpty()) g.dirs << p;
    }
    m_groups.append(g);
  }
  bool ok = false;
  const int ag = cfg.getString(kActiveGroupKey).toInt(&ok);
  m_active_group = (ok && ag >= 0 && ag < m_groups.size()) ? ag : -1;
}

void ChartSourceModel::save() const {
  ConfigStore& cfg = ConfigStore::instance();
  QJsonArray arr;
  for (const QString& d : m_dirs) arr.append(d);
  cfg.setString(kConfigKey, QString::fromUtf8(QJsonDocument(arr).toJson(
                                QJsonDocument::Compact)));

  QJsonArray garr;
  for (const Group& g : m_groups) {
    QJsonObject o;
    o["name"] = g.name;
    o["dirs"] = QJsonArray::fromStringList(g.dirs);
    garr.append(o);
  }
  cfg.setString(kGroupsKey, QString::fromUtf8(QJsonDocument(garr).toJson(
                                QJsonDocument::Compact)));
  cfg.setString(kActiveGroupKey, QString::number(m_active_group));
}

QStringList ChartSourceModel::activeDirectories() const {
  if (m_active_group < 0 || m_active_group >= m_groups.size()) return m_dirs;
  // Intersect the group's membership with the live dir list so a directory
  // removed from Chart Files automatically drops out of every group.
  QStringList out;
  for (const QString& d : m_groups[m_active_group].dirs)
    if (m_dirs.contains(d)) out << d;
  return out;
}

QVariantList ChartSourceModel::groups() const {
  QVariantList out;
  for (const Group& g : m_groups) {
    QVariantMap m;
    m["name"] = g.name;
    m["dirs"] = g.dirs;
    m["dirCount"] = g.dirs.size();
    out.append(m);
  }
  return out;
}

QVariantMap ChartSourceModel::groupAt(int index) const {
  if (index < 0 || index >= m_groups.size()) return {};
  const Group& g = m_groups[index];
  QVariantMap m;
  m["name"] = g.name;
  m["dirs"] = g.dirs;
  m["dirCount"] = g.dirs.size();
  return m;
}

int ChartSourceModel::addGroup(const QString& name) {
  const QString n = name.trimmed();
  if (n.isEmpty()) return -1;
  Group g;
  g.name = n;
  m_groups.append(g);
  save();
  Q_EMIT groupsChanged();
  return m_groups.size() - 1;
}

void ChartSourceModel::removeGroup(int index) {
  if (index < 0 || index >= m_groups.size()) return;
  m_groups.removeAt(index);
  bool active_changed = false;
  if (m_active_group == index) {
    m_active_group = -1;
    active_changed = true;
  } else if (m_active_group > index) {
    --m_active_group;
  }
  save();
  Q_EMIT groupsChanged();
  if (active_changed) {
    Q_EMIT activeGroupChanged();
    Q_EMIT rescanRequested();
  }
}

void ChartSourceModel::renameGroup(int index, const QString& name) {
  if (index < 0 || index >= m_groups.size()) return;
  const QString n = name.trimmed();
  if (n.isEmpty() || n == m_groups[index].name) return;
  m_groups[index].name = n;
  save();
  Q_EMIT groupsChanged();
}

void ChartSourceModel::setDirInGroup(int groupIndex, const QString& dir,
                                     bool member) {
  if (groupIndex < 0 || groupIndex >= m_groups.size()) return;
  QStringList& dirs = m_groups[groupIndex].dirs;
  const bool has = dirs.contains(dir);
  if (member && !has)
    dirs << dir;
  else if (!member && has)
    dirs.removeAll(dir);
  else
    return;  // no change
  save();
  Q_EMIT groupsChanged();
  if (groupIndex == m_active_group) Q_EMIT rescanRequested();
}

void ChartSourceModel::setActiveGroup(int index) {
  const int n = (index >= 0 && index < m_groups.size()) ? index : -1;
  if (n == m_active_group) return;
  m_active_group = n;
  save();
  Q_EMIT activeGroupChanged();
  Q_EMIT rescanRequested();
}

void ChartSourceModel::addDirectory(const QString& path) {
  // Accept a file:// URL (QML FolderDialog) or a plain path.
  QString p = path;
  if (p.startsWith(QStringLiteral("file:")))
    p = QUrl(p).toLocalFile();
  p = p.trimmed();
  if (p.isEmpty() || m_dirs.contains(p)) return;
  m_dirs << p;
  save();
  Q_EMIT changed();
  Q_EMIT rescanRequested();
}

void ChartSourceModel::removeDirectory(int index) {
  if (index < 0 || index >= m_dirs.size()) return;
  const QString removed = m_dirs.takeAt(index);
  bool groups_touched = false;
  for (Group& g : m_groups)
    if (g.dirs.removeAll(removed) > 0) groups_touched = true;
  save();
  Q_EMIT changed();
  if (groups_touched) Q_EMIT groupsChanged();
  Q_EMIT rescanRequested();
}

void ChartSourceModel::rescan() { Q_EMIT rescanRequested(); }

void ChartSourceModel::setStatus(const QString& text, bool scanning) {
  if (m_status == text && m_scanning == scanning) return;
  m_status = text;
  m_scanning = scanning;
  Q_EMIT statusChanged();
}

void ChartSourceModel::seedIfEmpty(const QString& path) {
  if (!m_dirs.isEmpty() || path.isEmpty()) return;
  m_dirs << path;
  save();
  Q_EMIT changed();
}

}  // namespace ocpn::qtui
