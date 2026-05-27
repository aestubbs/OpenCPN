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
#include <QUrl>

#include "config_store.h"

namespace {
constexpr char kConfigKey[] = "chartDirs";
}

namespace ocpn::qtui {

ChartSourceModel::ChartSourceModel(QObject* parent) : QObject(parent) {
  load();
}

void ChartSourceModel::load() {
  const QString json = ConfigStore::instance().getString(kConfigKey);
  m_dirs.clear();
  const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
  for (const QJsonValue& v : arr) {
    const QString p = v.toString();
    if (!p.isEmpty()) m_dirs << p;
  }
}

void ChartSourceModel::save() const {
  QJsonArray arr;
  for (const QString& d : m_dirs) arr.append(d);
  ConfigStore::instance().setString(
      kConfigKey, QString::fromUtf8(QJsonDocument(arr).toJson(
                      QJsonDocument::Compact)));
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
  m_dirs.removeAt(index);
  save();
  Q_EMIT changed();
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
