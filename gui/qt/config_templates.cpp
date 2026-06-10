/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Development Team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "config_templates.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "config_store.h"

namespace ocpn::qtui {

QString ConfigTemplates::dirPath() const {
  const QString d =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      QStringLiteral("/templates");
  QDir().mkpath(d);
  return d;
}

QStringList ConfigTemplates::templates() const {
  QStringList out;
  for (const QString& f : QDir(dirPath()).entryList(
           {QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    out << QFileInfo(f).completeBaseName();
  return out;
}

bool ConfigTemplates::saveCurrent(const QString& name) {
  const QString clean = QString(name).replace('/', '-').trimmed();
  if (clean.isEmpty()) return false;
  const QVariantMap entries = ConfigStore::instance().allEntries();
  QFile f(dirPath() + QDir::separator() + clean + QStringLiteral(".json"));
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(QJsonDocument(QJsonObject::fromVariantMap(entries)).toJson());
  Q_EMIT changed();
  return true;
}

bool ConfigTemplates::apply(const QString& name) {
  QFile f(dirPath() + QDir::separator() + name + QStringLiteral(".json"));
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject()) return false;
  ConfigStore::instance().setEntries(doc.object().toVariantMap());
  Q_EMIT changed();
  return true;
}

void ConfigTemplates::remove(const QString& name) {
  QFile::remove(dirPath() + QDir::separator() + name +
                QStringLiteral(".json"));
  Q_EMIT changed();
}

}  // namespace ocpn::qtui
