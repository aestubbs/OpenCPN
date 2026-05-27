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
#include <QQuickStyle>
#include <QString>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>

#include <wx/init.h>

#include "app_controller.h"
#include "nav_core.h"
#include <QFileInfo>

#include "ocharts_service.h"
#include "s52_engine.h"
#if defined(Q_OS_MACOS)
#include "macos_titlebar.h"
#endif

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

  // Initialise the wx LIBRARY (string/event-table services the model still
  // uses) -- not a wx event loop. The notify/listen fan-out (NavMsgBus ->
  // AisDecoder / CommBridge / data monitor) now runs entirely on the Qt event
  // loop via the Qt notifier (libs/observable), with ObservedEvt dispatched
  // synchronously, so no wx event loop or pump is needed.
  wxInitialize();

  // Use the platform-native Qt Quick Controls style so the chrome (toolbar,
  // drawer, dialogs, switches) renders natively instead of the generic
  // "Basic" fallback. macOS / Windows get their native styles; Fusion is a
  // polished cross-platform default elsewhere. An explicit
  // QT_QUICK_CONTROLS_STYLE env var still overrides this.
  if (QQuickStyle::name().isEmpty()) {
#if defined(Q_OS_MACOS)
    QQuickStyle::setStyle(QStringLiteral("macOS"));
#elif defined(Q_OS_WIN)
    QQuickStyle::setStyle(QStringLiteral("Windows"));
#elif defined(Q_OS_IOS)
    QQuickStyle::setStyle(QStringLiteral("iOS"));
#elif defined(Q_OS_ANDROID)
    QQuickStyle::setStyle(QStringLiteral("Material"));
#else
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
#endif
  }

  // Bring up the model nav-core singletons (AIS decoder, route/waypoint
  // managers, own-ship track, navobj DB) before the QML/ChartCanvas loads,
  // in dependency order. The live overlays read these.
  ocpn::qtui::initNavCore();

  // Initialise the S-52 engine before loading the QML so the status
  // binding is current the moment Main.qml's HUD reads it.
  ocpn::qtui::S52Engine s52;
  const QString s57data = QString::fromUtf8(OCPN_QT_S57DATA_DIR);
  if (!s57data.isEmpty()) s52.init(s57data);

  // Dev one-shot: decode an OSENC/.S57 file and log feature counts, to
  // validate the native OSENC reader. Set OCPN_QT_OSENC_TEST=/path/to/cell.S57
  if (const QByteArray t = qgetenv("OCPN_QT_OSENC_TEST"); !t.isEmpty()) {
    double n = 0, s = 0, e = 0, w = 0;
    s52.loadOsencCell(QString::fromUtf8(t), &n, &s, &e, &w);
    qInfo("OSENC_TEST extent N%.4f S%.4f E%.4f W%.4f", n, s, e, w);
  }
  // Dev one-shot: decrypt an o-charts .oesu cell via oexserverd, then decode.
  // Set OCPN_QT_OESU_TEST=/path/to/charts/CELL.oesu (its dir holds the keyList).
  if (const QByteArray t = qgetenv("OCPN_QT_OESU_TEST"); !t.isEmpty()) {
    const QString cell = QString::fromUtf8(t);
    auto& oc = ocpn::qtui::OChartsService::instance();
    const int nkeys = oc.loadKeyList(QFileInfo(cell).absolutePath());
    bool ok = false;
    const QByteArray osenc = oc.decryptCell(cell, ok);
    qInfo("OESU_TEST keys=%d decrypt ok=%d bytes=%lld", nkeys, ok,
          static_cast<long long>(osenc.size()));
    if (ok && osenc.size() < 2000) {
      QFile dump(QStringLiteral("/tmp/oesu_decrypt.bin"));
      if (dump.open(QIODevice::WriteOnly)) dump.write(osenc);
      qInfo("OESU_TEST dumped %lld bytes to /tmp/oesu_decrypt.bin",
            static_cast<long long>(osenc.size()));
    }
    if (ok) {
      double n = 0, s = 0, e = 0, w = 0;
      s52.decodeOsenc(osenc, &n, &s, &e, &w);
      qInfo("OESU_TEST extent N%.4f S%.4f E%.4f W%.4f", n, s, e, w);
    }
  }

  // Shared QML<->native state (vessel-data drawer). Exposed as "app".
  ocpn::qtui::AppController appController;

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("s52", &s52);
  engine.rootContext()->setContextProperty("app", &appController);
  // The settings backends (DisplayConfig, OwnShipConfig, AisConfig,
  // RouteDefaultsConfig) are QML singletons (QML_SINGLETON) -- referenced by
  // type name in QML, so they need no context property. Singletons are
  // compile-time resolved and always available, unlike context properties,
  // which can read undefined in bindings on early-constructed objects.
  // Runtime Qt version string for the About dialog.
  engine.rootContext()->setContextProperty(
      "qtRuntimeVersion", QString::fromLatin1(qVersion()));
  // QML module URI declared in CMakeLists qt_add_qml_module(URI opencpn.qt).
  engine.loadFromModule("opencpn.qt", "Main");
  if (engine.rootObjects().isEmpty()) return -1;

#if defined(Q_OS_MACOS)
  // Put the drawer toggle in the native window title bar (right side). Defer
  // so the NSWindow exists (valid winId). If it installs, QML hides the
  // floating-toolbar fallback toggle.
  if (auto* win = qobject_cast<QWindow*>(engine.rootObjects().first())) {
    QTimer::singleShot(0, win, [win, &appController]() {
      if (ocpn::qtui::installTitlebarToggle(win, &appController))
        appController.setTitlebarToggle(true);
    });
  }
#endif

  return app.exec();
}
