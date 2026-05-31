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
 * Implement tide_model.h.
 */

#include "tide_model.h"

#include <string>
#include <vector>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include "config_store.h"
#include "tcmgr.h"  // TCMgr + the global ptcmgr (libs/tides)

namespace {
constexpr char kConfigKey[] = "tideDataSources";
}

namespace ocpn::qtui {

TideModel::TideModel(QObject* parent) : QObject(parent) { load(); }

void TideModel::load() {
  const QString json = ConfigStore::instance().getString(kConfigKey);
  m_sources.clear();
  const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
  for (const QJsonValue& v : arr) {
    const QString p = v.toString();
    if (!p.isEmpty()) m_sources << p;
  }
}

void TideModel::save() const {
  QJsonArray arr;
  for (const QString& s : m_sources) arr.append(s);
  ConfigStore::instance().setString(
      kConfigKey,
      QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void TideModel::seedIfEmpty(const QString& path) {
  if (!m_sources.isEmpty() || path.isEmpty()) return;
  if (!QFileInfo::exists(path)) return;  // only seed if the bundled file is there
  m_sources << path;
  save();
}

void TideModel::addSource(const QString& path) {
  QString p = path;
  if (p.startsWith(QStringLiteral("file:"))) p = QUrl(p).toLocalFile();
  p = p.trimmed();
  if (p.isEmpty() || m_sources.contains(p)) return;
  m_sources << p;
  save();
  rebuild();
}

void TideModel::removeSource(int index) {
  if (index < 0 || index >= m_sources.size()) return;
  m_sources.removeAt(index);
  save();
  rebuild();
}

void TideModel::reload() { rebuild(); }

void TideModel::rebuild() {
  // Recreate the engine fresh on every change so the station set always matches
  // the current source list (LoadDataSources accumulates across files).
  delete ptcmgr;
  ptcmgr = new TCMgr;
  std::vector<std::string> sources;
  for (const QString& s : m_sources) sources.push_back(s.toStdString());
  if (!sources.empty()) ptcmgr->LoadDataSources(sources);

  m_ready = ptcmgr->IsReady();
  m_count = m_ready ? ptcmgr->Get_max_IDX() + 1 : 0;
  if (m_sources.isEmpty())
    m_status = tr("No tide data sets");
  else if (m_ready)
    m_status = tr("%1 stations · %2 data set(s)")
                   .arg(m_count)
                   .arg(m_sources.size());
  else
    m_status = tr("No stations loaded");
  Q_EMIT changed();
}

}  // namespace ocpn::qtui
