# OpenCPN → Qt/QtQuick Migration — Task Tracker

> Companion to [`QT_MIGRATION.md`](./QT_MIGRATION.md). That doc holds the
> design rationale; this one tracks execution. Update checkboxes and the
> **Current position** line as work proceeds.

**Current position:** P1.5a, P1.5e, P1.5f, P1.5i, P1.5b, P1.5d done.
`n0183_net` (TCP client + UDP) and `n2k_serial` are ported onto the comms
framework (`QT_MIGRATION_COMMS_ARCH.md`). Next: P1.5c/g.
**Last updated:** 2026-05-18.

Status legend: `[ ]` not started · `[~]` in progress · `[x]` done · `[!]` blocked.
Task IDs (`P1.2`) are stable — never renumber; add `Pn.x` for new work.

---

## Phase 0 — Fork & scaffold  (est. 2–3 wks)

- [ ] **P0.1** Create the fork repo; record upstream remote for future cherry-picks.
- [x] **P0.2** Install/pin Qt 6. Installed Qt **6.11.0** via Homebrew at
      `/opt/homebrew/opt/qt` (keg-only). Toolchain doc per target still TODO.
- [x] **P0.3** Qt discovery centralised in top-level `CMakeLists.txt`
      (`find_package(Qt6 Core)` + Homebrew keg-only hint, guarded `NOT QT_ANDROID`).
      Added `OCPN_USE_QT_GUI` option (OFF — placeholder for the Phase 3 GUI switch).
      Temporary hint removed from `libs/observable`. Full configure verified:
      exit 0, finds Qt 6.11.0, wx build still works.
- [ ] **P0.4** Scaffold an empty Qt app — `qt_add_executable` + `qt_add_qml_module`
      — that shows a blank `QQuickWindow`.
- [ ] **P0.5** Get the empty Qt app building in CI for desktop (Win/macOS/Linux).
- [ ] **P0.6** Decide repo layout for new code (`core/`, `render/`, `ui/`, `plugins-qt/`).
- [ ] **P0.7** Stand up an image-diff test harness skeleton (needed later for
      `s52plib` regression testing — set up early).

## Phase 1 — De-wx the core  (est. 7–9 wks)

Core stays buildable/testable against the **existing wx GUI** throughout.

- [x] **P1.1** Convert `libs/observable/` to `QObject` signals/slots — parallel
      Qt mechanism delivered (strangler; wx path untouched for the plugin ABI).
  - [x] **P1.1a** Qt added to `libs/observable` CMakeLists (`AUTOMOC`, `Qt6::Core`).
  - [x] **P1.1b** `ObsData` wx-free payload — `libs/observable/include/observable_data.h`.
  - [x] **P1.1c** `ObsNotifier` (`QObject`, `Notified(ObsData)` signal) + `ObsRegistry`
        keyed map + `EventVarQt`; `Qt::QueuedConnection` for cross-thread async.
        `libs/observable/include/observable_qt.h`, `src/observable_qt.cpp`.
  - [x] **P1.1d** `ObsConnection` — RAII connect/disconnect, movable, non-copyable.
  - [x] **P1.1e** wx `Observable`/`ObservedEvt` left intact for the plugin boundary.
  - [x] **P1.1f** 6 unit tests — `libs/observable/test/`; all pass against Qt 6.11.
        Build: `cmake -S libs/observable/test -B build/observable_test && cmake --build …`
- [x] **P1.16** wx↔Qt event-loop bridge — `QtEventBridge` creates a
      `QCoreApplication` and pumps `processEvents()` from a wx timer, so the Qt
      observable mechanism can deliver queued/cross-thread notifications while
      the app is still wx-based. **Hard prerequisite of P1.2–P1.5** (queued
      signals/slots are inert without a Qt event loop). Temporary — removed when
      the app runs a native Qt loop (Phase 3). Files: `gui/{src,include/gui}/
      qt_event_bridge.{cpp,h}`; wired into `MyApp::OnInit/OnExit`.
- [x] **P1.2** Migrate `comm_navmsg_bus` off `wxEvtHandler` (conservative scope).
      Dropped the `wxEvtHandler` base; `EventVar new_msg_event` → `EventVarQt`;
      `CallAfter` → `PostToMainThread()`. 3 `new_msg_event` listeners migrated in
      lockstep (`multiplexer`, `data_monitor_src`, `pluginmanager`:
      `ObsListener` → `ObsConnection`). Per-message dispatch channel
      (`Observable(*msg)`) left on wx for a later whole-channel migration.
      Build green, app runs.  *(dep: P1.1, P1.16)*
- [x] **P1.3** Migrate `comm_bridge` off `wxEvtHandler`. The base class existed
      solely for the watchdog `wxTimer`; replaced with `WatchdogTimer` (a pure
      C++17 `PeriodicTimer` subclass) which fires on a worker thread and
      marshals `OnWatchdogTimer()` back to the main thread via
      `PostToMainThread()`, preserving the wxTimer main-thread semantics. The
      19 message `ObsListener` members are untouched — they subscribe to the
      per-message dispatch channel, still on wx (see P1.2); they migrate with
      that channel later.  *(dep: P1.1, P1.16)*
- [x] **P1.4** Migrate `multiplexer` off `wxEvtHandler`. The base class was
      already vestigial — no `Bind`/`Connect`/`CallAfter`/`QueueEvent` in the
      cpp, and nothing uses `Multiplexer` as an event handler (P1.2 had moved
      its only Qt-needing listener, `m_new_msgtype_lstnr`, to `ObsConnection`).
      Dropped `: public wxEvtHandler`; header-only change, cpp untouched. The
      `m_listeners` `ObsListener` map stays on wx with the per-message channel.
      *(dep: P1.1)*
- [~] **P1.5** Migrate the comm drivers (`comm_drv_*`) to **Qt-native
      transport classes**. P1.5a and P1.5e revealed a common shape; the rest
      are built on a planned **comms framework** — `CommTransport` + `Framer`
      + `ProtocolDecoder` + a generic `CommDriver` — see
      [`QT_MIGRATION_COMMS_ARCH.md`](./QT_MIGRATION_COMMS_ARCH.md) and
      [`QT_MIGRATION_COMMS_PLAN.md`](./QT_MIGRATION_COMMS_PLAN.md).
      *(dep: P1.2–1.4)*
  - [x] **P1.5a** `CommDriverN2KNet` (N2K over TCP/UDP) → `QtNetwork`. Done as
        a standalone driver — see
        [`QT_MIGRATION_N2K_NET_PLAN.md`](./QT_MIGRATION_N2K_NET_PLAN.md);
        refactored onto the framework by P1.5j.
  - [x] **P1.5i** Built the comms framework — `CommTransport`
        (`SerialTransport`, `TcpClientTransport`, `UdpTransport`), `Framer`
        (`LineFramer`, `PassThroughFramer`), the `ProtocolDecoder` interface,
        and the generic `CommDriver` (lifecycle, reconnect, watchdog, stats).
        `CanTransport`/`WebSocketTransport` and the concrete decoders are
        added with their driver tasks (P1.5g/c, P1.5b/d). The remaining
        drivers are built on this framework.
  - [x] **P1.5e** `CommDriverN0183Serial` → `QSerialPort`. *Primary reference
        pattern* (byte-stream serial). Native `QObject` owning a `QSerialPort`;
        RX is event-loop driven (`readyRead` → `LineBuffer` framing). The
        `SerialIo`/`ThreadCtrl` worker-thread abstraction is gone — deleted
        `serial_io.h`, `std_serial_io.cpp`, `android_serial_io.cpp`. The
        vendored `libs/serial` is no longer used here (it survives for
        `ser_ports.cpp` port enumeration — P1.5h). Build green, 58/59
        tests pass.
  - [x] **P1.5b** `n0183_net` on the framework — TCP-client and UDP
        connections run on the generic `CommDriver` (`TcpClientTransport` /
        `UdpTransport` + `LineFramer` + new `Nmea0183Decoder`, 10 unit
        tests). The factory builds the triple. **Out of scope:** TCP
        server-mode (a 0.0.0.0 listen address) and GPSD — treated as edge
        cases; the factory creates no driver for them (logs a message).
        The legacy `CommDriverN0183Net` is kept in the tree but is no
        longer reachable. See **P1.5k** to review whether to restore them.
  - [ ] **P1.5k** *(review / deferred)* Decide whether NMEA 0183 network
        **TCP server-mode** and **GPSD** are worth supporting. If yes:
        server-mode is a `TcpServerTransport` (`QTcpServer`) and GPSD is a
        TCP transport with a `?WATCH` connect-greeting option — both at the
        transport layer, then the legacy `CommDriverN0183Net` can be
        deleted. If no, delete `comm_drv_n0183_net.{h,cpp}` outright.
  - [x] **P1.5d** `n2k_serial` on the framework — N2K serial-gateway
        connections run on the generic `CommDriver` (`SerialTransport` +
        new `N2kGatewayFramer` + new `N2kDecoder`, 13 unit tests). The
        worker thread / `wxEvtHandler` / vendored `serial::Serial` are
        gone; the legacy `comm_drv_n2k_serial.{h,cpp}` is deleted. The
        gateway management handshake (NGT-1 startup, mfg-code probe,
        TX-PGN enable/commit/activate, YDNU-02 mode) is a full async
        rewrite — `N2kGatewayManager`, a QObject state machine on
        QTimers, replacing the legacy blocking `wxMilliSleep`/`wxYield`
        loops. `SetTXPGN` moved up to `AbstractCommDriver` so callers
        (`plugin_api`, `autopilot_output`) need no driver-type downcast.
        `libs/serial` stays — still used by `ser_ports.cpp` et al (P1.5h).
  - [ ] **P1.5c** `signalk_net` on the framework — `WebSocketTransport`
        (`QWebSocket`) + `PassThroughFramer` + `SignalKDecoder`; retires the
        vendored `IXWebSocket` for this driver.
  - [ ] **P1.5g** `n2k_socketcan` on the framework — `CanTransport`
        (`QCanBusDevice`) + `PassThroughFramer` + `N2kDecoder`. Replaces the
        raw `PF_CAN` socket / `ioctl` / `Worker` thread. Backend by name —
        `socketcan` (Linux, real HW) or `virtualcan` (macOS dev/test).
  - [ ] **P1.5h** `ser_ports.cpp` serial-port enumeration → `QSerialPortInfo`,
        retiring the platform `#ifdef` branches.
  - [ ] **P1.5j** Refactor the standalone P1.5a (`n2k_net`) and P1.5e
        (`n0183_serial`) onto the framework, retiring their bespoke code.
  - [x] **P1.5f** Deleted the Android comm drivers (`comm_drv_n0183_android_*`,
        `INTERNAL_GPS`/`INTERNAL_BT`) and Android serial I/O
        (`android_serial_io.cpp`, deleted with P1.5e). Android is dropped for
        the migration (`QT_MIGRATION.md` §1, X.4); mobile returns natively via
        QtQuick after the core is on Qt. Desktop build green.
- [ ] **P1.6** Sweep `wxString` → `QString` across `model/` and core `libs/`.
- [ ] **P1.7** Sweep `wxDateTime`/`wxTimeSpan` → `QDateTime`/`QTimeSpan` equivalents.
- [ ] **P1.8** Replace wx containers (`wxArrayString` etc.) with Qt/STL.
- [ ] **P1.9** Replace `wxConfig`/`wxFileConfig` with `QSettings`; abstract `config_vars`.
- [ ] **P1.10** Replace file I/O (`wxFileName`/`wxDir`/`chartdata_input_stream`) with `QFile`/`QDir`.
- [ ] **P1.11** Replace threading primitives (`wxThread`/`wxMutex`/`wxSemaphore`).
- [ ] **P1.12** Delete `libs/wxJSON`; move JSON use to `QJsonDocument`.
- [ ] **P1.13** Delete `libs/wxcurl`; move networking to `QNetworkAccessManager`.
- [ ] **P1.14** Abstract route/mark UI types (`wxColour`/`wxPen`/`wxBitmap`) → `QColor`/`QPen`/`QImage`.
- [ ] **P1.15** Verify: core compiles wx-free; unit tests pass.

## Phase 2 — Scene graph + LayerCompositor  (est. 10–16 wks)

- [ ] **P2.1** Build the chart host `QQuickItem` with the two top nodes:
      `WorldAnchoredRoot` (viewport transform) + `DisplayAnchoredRoot` (fixed transform).
- [ ] **P2.2** Implement the `Layer` abstraction (anchor, visible, zOrder, opacity, owner, id).
- [ ] **P2.3** Implement the `LayerCompositor` (two ordered stacks → the two top nodes).
- [ ] **P2.4** Port the 6 GLSL shaders via `qsb` to `QShader`; build scene-graph materials.
- [ ] **P2.5** Port the texture pipeline (`gl_tex_cache`/`gl_texture_mgr`) to `QSGTexture`.
- [ ] **P2.6** Reimplement `ocpnDC` primitives — non-GL path on `QPainter`,
      GL path on scene-graph geometry nodes.
- [ ] **P2.7** Raster chart (KAP/BSB) Layer — textured quads.
- [ ] **P2.8** Port `s52plib` vector rendering output to scene-graph geometry nodes.  *(dep: P2.4–2.6)*
- [ ] **P2.9** Expose S52 display categories / viewing groups as chart sub-layers.
- [ ] **P2.10** Per-layer `visible`/`zOrder`/`opacity` persistence via `QSettings`.
- [ ] **P2.11** Image-diff regression suite: new renderer vs current GL renderer.  *(dep: P0.7)*
- [ ] **P2.12** Performance profiling vs the current GL path; close gaps.

## Phase 3 — QtQuick UI shell  (est. 16–24 wks)

- [ ] **P3.1** App shell in QML — main window, chart view embedding the Phase 2 `QQuickItem`.
- [ ] **P3.2** `QObject` view-models exposing core data via `Q_PROPERTY` to QML.
- [ ] **P3.3** Core world-anchored Layers — own-ship, routes, tracks, AIS targets.
- [ ] **P3.4** QML HUD tier — depth, SOG/COG, wind readouts bound to view-models.
- [ ] **P3.5** Toolbar / main controls in QML (touch-friendly).
- [ ] **P3.6** Settings / preferences UI in QML.
- [ ] **P3.7** Route & mark manager UI in QML.
- [ ] **P3.8** Chart selection / quilting UI.
- [ ] **P3.9** Dialogs (AIS target info, object query, alarms) in QML.
- [ ] **P3.10** i18n via Qt Linguist (`.ts`/`tr()`); migrate translatable strings.
- [ ] **P3.11** Remove the parallel wx build path; fork is now Qt-only.
- [ ] **P3.12** Remove `QT_NO_KEYWORDS`; restore the plain `signals` /
      `slots` / `emit` keywords now that no wx/system headers remain to clash
      with. Touches the QObject classes added during Phase 1 (`observable_qt`,
      `comm_drv_*`). Introduced by P1.5a.

## Phase 4 — Qt plugin host  (est. 6–8 wks)

- [ ] **P4.1** Define the Qt plugin interface (`QPluginLoader` + Layer/HUD contribution API).
- [ ] **P4.2** Expose `registerLayer()` and QML HUD contribution points to plugins.
- [ ] **P4.3** Port `dashboard` as a built-in Qt module (display-anchored / HUD).
- [ ] **P4.4** Port `chartdldr` (chart downloader) as a built-in Qt module.
- [ ] **P4.5** Port `grib` as a built-in Qt module (world-anchored Layer).
- [ ] **P4.6** Document the plugin API for future third-party / open use.

## Phase 5 — Embedded / device targets  (est. 4–8 wks)

- [ ] **P5.1** Cross-compile config for an embedded reference board.
- [ ] **P5.2** eglfs / Wayland boot-to-app (no desktop environment).
- [ ] **P5.3** Validate RHI backend selection (Vulkan/GLES) on the target.
- [ ] **P5.4** Touch / input tuning for embedded hardware.
- [ ] **P5.5** Qt for Device Creation packaging / image build.

---

## Cross-cutting / ongoing

- [ ] **X.1** Keep Phase 1 changes mechanical (not redesign) to preserve upstream cherry-pick ability.
- [ ] **X.2** Update [`QT_MIGRATION.md`](./QT_MIGRATION.md) when design decisions change.
- [ ] **X.3** Maintain the image-diff regression suite as the chart renderer evolves.
- [ ] **X.4** As each area is migrated, **remove** its Android / wxQt-specific
      code (`__OCPN__ANDROID__`, `QT_ANDROID`, `wxQt` paths, `*_android_*`
      files) rather than porting it. Android is dropped for the migration
      (`QT_MIGRATION.md` §1); mobile is reintroduced natively via QtQuick once
      the core is on Qt.

## Notes / decisions log

- 2026-05-17 — Tracker created. Plan and rationale: `QT_MIGRATION.md`.
- 2026-05-17 — P0.3 done: Qt discovery in top-level CMake; `OCPN_USE_QT_GUI`
  option added (OFF). Configure verified end-to-end with wx still building.
- 2026-05-17 — Full project build verified green: `OpenCPN.app` builds and
  launches normally; AUTOMOC runs on `observable_qt.h`; `Qt6::Core` links into
  the binary. No behaviour change (new Qt code not yet called).
- 2026-05-17 — `observable_qt.h` refactored to be **Qt-header-free**: `ObsConnection`
  is pimpl'd, Qt internals (`ObsNotifier`/`ObsRegistry`) moved to `observable_qt_p.h`,
  and a `PostToMainThread()` helper added. Reason: wxWidgets + Qt headers in the
  same TU clash via macros (`qscopeguard.h` broke). All Qt headers are now
  confined to `observable_qt.cpp`. Rule going forward: keep Qt out of any header
  that wx-including code consumes.
- 2026-05-17 — New task P1.16 discovered while scoping P1.2: the Qt observable
  mechanism uses `Qt::QueuedConnection`, which needs a running Qt event loop;
  the app is still a `wxApp` with none. Added `QtEventBridge` as a hard
  prerequisite of P1.2–P1.5.
- 2026-05-17 — P1.1 delivered: Qt observable mechanism (`ObsData`, `ObsNotifier`,
  `ObsRegistry`, `ObsConnection`, `EventVarQt`) + 6 unit tests, all passing on
  Qt 6.11.0. wx observable path untouched. Consumer migration is P1.2–P1.5.
- 2026-05-17 — P1.1 re-scoped: `ObservedEvt` is part of the wx plugin ABI
  (`ocpn_plugin.h`), so observable conversion must be strangler-style — add a
  parallel Qt mechanism, keep the wx path for the plugin boundary until Phase 4.
  P0.3 (Qt in CMake) promoted to a hard prerequisite of P1.1.
- 2026-05-18 — P1.3 done: `CommBridge` no longer inherits `wxEvtHandler`. The
  base class was needed only for the watchdog `wxTimer`; it is replaced by
  `WatchdogTimer`, a `PeriodicTimer` subclass. `PeriodicTimer` runs `Notify()`
  on a worker thread, so the tick is marshalled to the main thread with
  `PostToMainThread()` to keep `OnWatchdogTimer()` (touches global nav state,
  AppMsgBus) on the main thread as the old wxTimer did. The message
  `ObsListener` members stay on wx — their publisher (the per-message
  dispatch channel) is still wx. Build green; 58/59 unit tests pass (the one
  failure, `DateTimeFormatTest.LocalTimezoneCETSwedish`, is a pre-existing
  locale-dependent test unrelated to this change).
- 2026-05-18 — P1.4 done: `Multiplexer` no longer inherits `wxEvtHandler`. The
  base was entirely vestigial — P1.2 had already migrated the one listener
  needing Qt, and the remaining `ObsListener` map needs no event-handler base
  (confirmed by P1.3). Header-only change. Build green, 58/59 tests pass
  (same unrelated locale test).
- 2026-05-18 — P1.5 found to be deeper than expected and split into per-driver
  sub-tasks P1.5a–f. The two net drivers (`n2k_net`, `n0183_net`) are
  `wxSocket`-bound — dropping `wxEvtHandler` means a full socket-layer rewrite
  to Qt, not a base-class swap.
- 2026-05-18 — P1.5a done: `CommDriverN2KNet` rewritten as a native `QObject`
  using `QTcpSocket`/`QTcpServer`/`QUdpSocket` + `QTimer`; the custom `wxEvent`
  payload channel became a queued signal/slot. Decision: use **native QObject**
  in driver headers (the fork's intent) rather than pimpl, and define
  **`QT_NO_KEYWORDS`** project-wide so the `signals`/`slots`/`emit` macros
  cannot collide with wx/system headers — Qt code uses `Q_SIGNALS`/`Q_SLOTS`/
  `Q_EMIT`. `observable_qt` converted to match. Transitional; P3.12 records
  removing `QT_NO_KEYWORDS` after the wx path is gone. The protocol parsers
  were left as-is (their `wxString` use is the P1.6 string sweep). Build
  green, 58/59 tests pass; on-water/simulator testing of a real N2K gateway
  still required.
- 2026-05-18 — Android dropped for the migration (`QT_MIGRATION.md` §1, X.4):
  the legacy wxQt Android build is not kept alive; Android/wxQt-specific code
  is deleted as each area is migrated. Mobile returns natively via QtQuick
  after the core is on Qt.
- 2026-05-18 — P1.5e done: `CommDriverN0183Serial` is a native `QObject`
  owning a `QSerialPort`; the `SerialIo`/`ThreadCtrl` worker-thread layer and
  its desktop + Android implementations were deleted (`serial_io.h`,
  `std_serial_io.cpp`, `android_serial_io.cpp`). RX is event-loop driven via
  `readyRead`; reconnect is a `QTimer`. This is the byte-stream reference
  pattern; `n2k_serial` (P1.5d) and `n0183_net` (P1.5b) adapt from it. Build
  green, 58/59 tests pass; functional verification needs a serial device.
- 2026-05-18 — P1.5 architecture agreed (`QT_MIGRATION_COMMS_ARCH.md`): the
  two completed drivers (P1.5a, P1.5e) revealed a common shape, and the legacy
  drivers fuse medium + protocol + lifecycle per N×M combination with
  reconnect/watchdog/stats re-derived each time. Planned framework: a pipeline
  of `CommTransport` (media I/O) → `Framer` (boundary finding; pass-through for
  frame-native media) → `ProtocolDecoder` (frame → NavMsg) → generic
  `CommDriver` (lifecycle/reconnect/watchdog/stats, written once). Key point:
  it is *all frames* — byte-stream media just need a Framer to find boundaries;
  frame-native media (CAN, WebSocket) deliver them pre-built. Remaining drivers
  build on the framework (P1.5i); P1.5a/e refactor onto it (P1.5j). Decoders/
  framers are pure → unit-testable without hardware.
- 2026-05-18 — P1.5 reframed (`QT_MIGRATION_COMMS_PLAN.md`): rather than just
  dropping `wxEvtHandler`, each driver adopts the correct Qt transport class
  (`QSerialPort`, `QCanBus`, `QtNetwork`, `QWebSocket`), which lets vendored
  libs (`libs/serial`, `IXWebSocket`) and the `ser_ports.cpp` `#ifdef` maze be
  deleted. Two reference drivers go first — P1.5g (`socketcan` → `QCanBus`,
  frame-oriented) and P1.5e (`n0183_serial` → `QSerialPort`, byte-stream); the
  others adapt. Note: P1.5g is Linux-only and cannot be built on the macOS
  dev box. Key distinction: only `socketcan` is real CAN — the N2K *gateway*
  drivers (`n2k_serial`, `n2k_net`) are byte-stream transports, not `QCanBus`.
- 2026-05-18 — P1.5i done: the comms framework is built. `CommTransport`
  (abstract `QObject` media adaptor) with `SerialTransport`/`TcpClientTransport`/
  `UdpTransport`; `Framer` with `LineFramer`/`PassThroughFramer`; the pure
  `ProtocolDecoder` interface; and the generic `CommDriver` (`QObject` +
  `AbstractCommDriver`) that owns a transport+framer+decoder triple and
  provides reconnect, the no-data watchdog and `DriverStats` once. The NxM
  driver grid now collapses to N transports + M decoders + a few framers + 1
  driver. `CanTransport`/`WebSocketTransport` and the concrete decoders land
  with their driver tasks (P1.5g/c, P1.5b/d). Build green.
- 2026-05-18 — P1.5b done: `n0183_net` is the first driver on the comms
  framework. TCP-client and UDP connections now run as a generic `CommDriver`
  + `TcpClientTransport`/`UdpTransport` + `LineFramer` + a new pure
  `Nmea0183Decoder` (v4-tag stripping, garbage/checksum classification, input
  sentence filter; 10 gtest cases, no hardware needed). The factory
  (`MakeN0183NetDriver`) builds the triple. The legacy `CommDriverN0183Net`
  is retained only for TCP server-mode and GPSD — server-mode would be a
  `TcpServerTransport` (transport layer, deferred); GPSD is out of scope
  pending review. `UdpTransport` now binds shareable (REUSEADDR), matching
  the legacy socket. Build green; framer+decoder tests pass.
- 2026-05-18 — P1.5d done: `n2k_serial` ported onto the comms framework.
  N2K serial-gateway connections are now a generic `CommDriver` +
  `SerialTransport` + `N2kGatewayFramer` (the <ESC><STX>..<ESC><ETX>
  un-escaping framer) + `N2kDecoder` (Actisense frame <-> Nmea2000Msg). The
  legacy `comm_drv_n2k_serial.{h,cpp}` -- a `wxThread` + `wxEvtHandler`
  driver using the vendored `serial::Serial` -- is deleted. Per the agreed
  scope (full async port) the gateway management handshake was rewritten as
  `N2kGatewayManager`, a QObject state machine: it sends fire-and-forget
  init (NGT-1 startup / YDNU-02 mode), runs response-correlated probes
  (mfg code, TX-PGN enable/commit/activate) on QTimers, and is fed 0xA0
  management frames through a new general CommDriver frame-tap; the legacy
  blocking `wxMilliSleep`/`wxYield` loops are gone. `SetTXPGN` moved up to
  `AbstractCommDriver` (no-op default) so `plugin_api`/`autopilot_output`
  reach it without a driver-type downcast. 13 framer+decoder gtest cases;
  build green (app + tests link), 27/27 comms-framework tests pass.
