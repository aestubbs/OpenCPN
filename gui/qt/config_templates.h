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
 * ConfigTemplates (P3.6, wx configuration-templates parity, v1): named
 * snapshots of the entire ConfigStore saved as JSON files under
 * <AppData>/templates/. Apply bulk-writes the snapshot back and takes
 * full effect on the NEXT START (the config singletons read at startup)
 * -- stated in the UI. List/save/apply/delete via a QML singleton.
 */

#ifndef OCPN_QT_CONFIG_TEMPLATES_H_
#define OCPN_QT_CONFIG_TEMPLATES_H_

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace ocpn::qtui {

class ConfigTemplates : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QStringList templates READ templates NOTIFY changed)

public:
  static ConfigTemplates& instance() {
    static ConfigTemplates inst;
    return inst;
  }
  static ConfigTemplates* create(QQmlEngine*, QJSEngine*) {
    ConfigTemplates* p = &instance();
    QJSEngine::setObjectOwnership(p, QJSEngine::CppOwnership);
    return p;
  }

  QStringList templates() const;
  Q_INVOKABLE bool saveCurrent(const QString& name);
  Q_INVOKABLE bool apply(const QString& name);
  Q_INVOKABLE void remove(const QString& name);

signals:
  void changed();

private:
  explicit ConfigTemplates(QObject* parent = nullptr) : QObject(parent) {}
  QString dirPath() const;
};

}  // namespace ocpn::qtui

#endif  // OCPN_QT_CONFIG_TEMPLATES_H_
