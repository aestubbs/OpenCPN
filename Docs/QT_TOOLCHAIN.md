# opencpn-qt toolchain (per target)

Companion to [`QT_MIGRATION.md`](./QT_MIGRATION.md); closes the P0.2
"toolchain doc per target" TODO. macOS is the primary development +
verification platform (decision 2026-06-10); Linux/Windows are a pre-P3.11
CI gate (P0.5).

## macOS (primary)

- **Qt 6.11** via Homebrew, keg-only: `brew install qt` →
  `/opt/homebrew/opt/qt`. The top-level `CMakeLists.txt` carries the
  keg-only hint; no `CMAKE_PREFIX_PATH` needed.
- **wxWidgets 3.2.10** via `brew install wxwidgets@3.2` (the parallel wx
  build is still in the tree until P3.11; opencpn-qt also links wx for the
  not-yet-swept model boundaries). Configure with the explicit
  `wx-config`: see `docs/` build notes — the wrapper at
  `/opt/homebrew/opt/wxwidgets@3.2/bin/wx-config`.
- **Generator**: Unix Makefiles (`cmake -S . -B build`).
- **Build**: `cmake --build build --target opencpn-qt -j 8`.
- **Run**: execute the binary directly (not `open`):
  `build/gui/qt/opencpn-qt.app/Contents/MacOS/opencpn-qt`.
- **Deployment target**: 10.15+ on the model (Qt 6 file-I/O headers need
  libc++ 10.15 attributes; set per-target in P1.10).
- **Tunables**: `OCPN_QT_MSAA` (1/2/4/8 scene-graph samples),
  `OCPN_QT_TEST_ENC`, `OCPN_QT_NMEA_LOG`, `OCPN_QT_TIDE_TEST`,
  `OCPN_QT_OESU_TEST` (dev one-shots; see `gui/qt/main.cpp`).
- **i18n**: `cmake --build build --target update_translations` re-scans
  sources into `gui/qt/translations/*.ts` (P3.10).

### Known macOS gotcha — stale AOT QML cache

After adding/removing `Q_INVOKABLE`s or properties on QML-exposed types,
an incremental build can leave the AOT-compiled QML cache referencing the
old metaobject layout → instant SIGSEGV at startup
(`QQmlPrivate::callQObjectMethod` on a null method). Fix: clean-rebuild
the target (`rm -rf build/gui/qt/.rcc build/gui/qt/CMakeFiles/opencpn-qt.dir
build/gui/qt/opencpn-qt_autogen`, reconfigure, rebuild). Diagnose with
`QV4_FORCE_INTERPRETER=1` (runs fine → stale cache).

## Linux (pre-P3.11 CI gate, P0.5)

Expected packages (Ubuntu 24.04+): `qt6-base-dev qt6-declarative-dev
qt6-serialport-dev qt6-multimedia-dev qt6-svg-dev qt6-shadertools-dev
qt6-l10n-tools libwxgtk3.2-dev` plus the existing OpenCPN deps
(`ci/control` has the wx-era list). Qt ≥ 6.5 required
(`qt_standard_project_setup(REQUIRES 6.5)`); the QML module + shaders
need `qml6-module-*` runtime packages for QtQuick.Controls/Layouts/
Dialogs/Window. Untested — first CI run will shake this out.

## Windows (pre-P3.11 CI gate, P0.5)

Expected: Qt 6.5+ via aqtinstall (`windows desktop win64_msvc2019_64` or
newer), MSVC toolchain as the wx build already uses (`buildwin/`),
`-DCMAKE_PREFIX_PATH=<Qt>/msvc2019_64`. The serial enumeration
(`QSerialPortInfo`) and file paths are already cross-platform from
Phase 1. Untested — first CI run will shake this out.

## Android / embedded

Android is **dropped for the migration** (P1.5f; `QT_MIGRATION.md` §1,
X.4) — mobile returns natively via QtQuick post-migration. Embedded
(Phase 5) targets boot-to-app eglfs/Wayland with RHI Vulkan/GLES;
untouched until Phase 5 opens.
