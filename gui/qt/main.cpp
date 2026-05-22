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
 * Entry point for opencpn-qt -- the new Qt-Quick chart renderer.
 *
 * Minimal scaffolding for P0.4 / P2.1: opens a QQmlApplicationEngine that
 * loads Main.qml; Main.qml hosts a ChartCanvas QQuickItem with the three-tier
 * scene-graph (World-anchored, Display-anchored) plus QML HUD layered above.
 *
 * Now also initialises the S-52 vector-chart engine (P2.8a). The data
 * directory containing S52RAZDS.RLE / chartsymbols.xml /
 * rastersymbols-*.png is injected via the OCPN_QT_S57DATA_DIR compile
 * definition (set in gui/qt/CMakeLists.txt to the source-tree
 * data/s57data/ for development). Production installs will discover via
 * QStandardPaths::AppDataLocation -- not implemented yet.
 *
 * Builds as a sibling to the legacy wx-based OpenCPN executable; replaces it
 * in Phase 3.
 */

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QSurfaceFormat>

#include "s52_engine.h"

#ifndef OCPN_QT_S57DATA_DIR
#define OCPN_QT_S57DATA_DIR ""
#endif

int main(int argc, char* argv[]) {
  // Enable 4x MSAA for the scene graph so chart geometry edges -- line
  // quads and area-fill boundaries alike -- are anti-aliased by the GPU.
  // Must be set before the QQuickWindow is created. Default to the
  // platform's preferred surface otherwise (Metal via RHI on macOS,
  // D3D11 on Windows, Vulkan/OpenGL on Linux).
  QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
  fmt.setSamples(4);
  QSurfaceFormat::setDefaultFormat(fmt);

  QGuiApplication app(argc, argv);
  app.setOrganizationName("OpenCPN");
  app.setApplicationName("opencpn-qt");

  // Initialise the S-52 engine before loading the QML so the status
  // binding is current the moment Main.qml's HUD reads it.
  ocpn::qtui::S52Engine s52;
  const QString s57data = QString::fromUtf8(OCPN_QT_S57DATA_DIR);
  if (!s57data.isEmpty()) s52.init(s57data);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("s52", &s52);
  // QML module URI declared in CMakeLists qt_add_qml_module(URI opencpn.qt).
  engine.loadFromModule("opencpn.qt", "Main");
  if (engine.rootObjects().isEmpty()) return -1;

  return app.exec();
}
