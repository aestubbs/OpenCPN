# OpenCPN → Qt/QtQuick Migration — Task Tracker

> Companion to [`QT_MIGRATION.md`](./QT_MIGRATION.md). That doc holds the
> design rationale; this one tracks execution. Update checkboxes and the
> **Current position** line as work proceeds.

**Current position:** P1.5 comms migration done (P1.5a/b/d/e/f/h/i, P1.5j-1);
the comms pipeline is on the framework and, as of P1.6a, wx-free behind the
`ConnectionParams` facade. `n2k_net` stays the standalone P1.5a driver;
SignalK/SocketCAN parked (P1.5m). **Phase 1 complete** (P1.6–P1.15) —
the model's `wxString` / `wxDateTime` / wx-container / `wxConfig` /
file-I/O / threading-and-timer / JSON / networking / route-mark-UI-
types sweeps are all done; build + tests green.
`QStringList` / `QList<T*>` / `QHash` / `QSet` are the container
vocabulary; `model/wx_qt_string.h` (UTF-8) centralizes wx⇄Qt string
conversions; `QDateTime` / `qint64`-seconds is the time/duration
vocabulary; `OcpnConfig` (wraps `QSettings`) is the settings store;
`QFile` / `QDir` / `QFileInfo` / `QStandardPaths` is the file-I/O
vocabulary; `QThread` / `QMutex` / `QSemaphore` / `QTimer` / `QObject`
with Qt signals/slots is the threading + event-loop vocabulary;
`QJsonDocument` / `QJsonObject` / `QJsonArray` / `QJsonValue` is the
JSON vocabulary; `QNetworkAccessManager` / `QNetworkReply` is the HTTP
vocabulary, and `libs/wxcurl` has been deleted. Remaining wx
references are deliberate boundaries — the frozen plugin ABI (incl.
the `GetSignalkPayload` `wxJSONValue` shim), the chart-reader
`wxInputStream`/`wxOutputStream` streams, `wxStandardPaths` on macOS
bundle paths, wx-widget plumbing (Phase 3), deferred-ownership
container types, `libs/wxservdisc` (mDNS — service discovery, separate
concern), and the post-P1.9 config call-site helpers.

**Phase 2 in progress.** Done so far: P2.1–P2.3 (canvas + Layer +
LayerCompositor), P2.8 (S-52 vector pipeline), plus async chart load /
quilting. This session added **P2.4** (materials catalog +
`sg_helpers` factories), **P2.5** (`TextureCacheNode` subtree-scoped
texture cache + per-name symbol dedup), **P2.6** (`SgBuilder` scene-graph
primitive builder — the ocpnDC core-primitive replacement, renamed from
the misleading `SgDc`), **P2.9** (soundings/text viewing-group toggles —
the display-category slice; full Base/Standard/Other/Mariner + per-class
viewing groups still TODO), **P2.10** (per-layer visible/zOrder/opacity
persistence via `OcpnConfig` + `Layer::persistState()`), **P2.11a/b**
(pluggable `NavDataProvider` + demo mode; retained AIS + own-ship
overlays, static route/track/waypoint overlays — live model adapter is
the remaining seam), and **P2.12** (perf profiling notes + split
dynamic/static nav signals). **P2.13** stays deferred (no deficit found).

A renderer-parity audit vs the legacy wx S-52 renderer (2026-05-30, every
finding adversarially re-verified against code) added **P2.14–P2.19** — see
"Renderer parity gaps vs wx" below. Headline: the Qt renderer is **at parity
on S-52 primitive emission** (AC/AP fills, LS/LC lines, SY symbols, CARC arcs,
TX/TE text, depth shading incl. 2-/4-shade + shallow/safety/deep thresholds,
conditional-symbology recolour incl. DEPCNT02/UDWHAZ03/SNDFRM02, and full
day/dusk/night palette switching), and the **composite quilt is a deliberate,
working divergence** from wx's reference-scale tiers (it fixes the wx
"coarse chart loses its soundings in one zoom step" symptom). Genuine open
gaps are narrower than first thought: SCAMIN on **AC solid fills, LS simple
lines and AP pattern fills** (the `Prim`/`PatternFill` families — SY/TX/LC/
soundings already honour it) — **P2.14**; area **boundary lines** not emitted —
**P2.15**; **7 of 15** built-but-inert chart-dialog vector options —
**P2.16**; and raster/CM93 chart **types** — **P2.7/P2.19**.

Progress (2026-05-30): **P2.14 DONE** (SCAMIN-cull the `Prim`/`PatternFill`
families — AC/LS/AP now honour SCAMIN per-frame). **P2.16 MOSTLY DONE**: the
chart-dialog toggles were inert because the *scene-graph emit never consulted*
the s52plib flags — fixed by adding the gates to the SG emit (6 of 8 work now:
chart-info, buoy/light labels, light descriptions, important-text-only,
national text, extended light sectors), the dialog was de-duplicated to match
wx (removed Qt-invented Lights/Buoys/Text live toggles), and de-clutter +
super-SCAMIN are split to **P2.23** (both default-off, so common case matches).
Both build clean. **P2.23a DONE** (de-clutter text gated; default off = wx).
**P2.15 OSENC DONE** (area boundary LS/LC lines on the o-charts/OSENC path).
Two follow-ups surfaced during that work: **P2.23b** (super-SCAMIN — needs the
cell native scale plumbed into `chart_context`, currently 0) and **P2.24**
(OGR/.000 path: it never decodes per-object SCAMIN onto `obj->Scamin`, which
also makes P2.14's cull inert on NOAA charts; + OGR area boundary lines from
polygon rings). **Completed since (committed, 2026-05-31):** P2.24 + P2.15 (OGR/.000 area
boundary lines; the "SCAMIN inert on NOAA" concern was retracted) and P2.23b
(super-SCAMIN) all landed. Beyond the parity audit, four features the older
roadmap had queued as "next" are now done: a **CPA/TCPA engine** for AIS
targets (`ais_cpa.cpp`, wx parity + danger display); **chart orientation**
(North-Up / Course-Up / Head-Up + look-ahead, click-the-compass-rose toggle,
real scene-graph rotation); **native o-charts** decrypt end-to-end (oexserverd
FIFO + OSENC decode + SENC disk cache); and the **live `NavDataProvider`
adapter** (`model_nav_data_provider` reading real ais_decoder / own_ship /
routeman / comm drivers; demo off by default). Pan/zoom is now GPU-real
(40+fps).

**Direction (2026-05-31):** Phase 2's S-52 vector renderer is effectively at
parity. Remaining Phase-2 items are **P2.7 (raster KAP/BSB — DEPRIORITIZED by
the user 2026-05-31; parked, do not pursue)** and the low-severity
P2.17/P2.18/P2.20 + P2.19 (CM93). Work now shifts to **Phase 3 (QtQuick UI
shell consolidation) heading toward retiring the parallel wx build (P3.11)**.
New investigation task **P2.25** captures specific chart-rendering defects to
chase (Yarmouth tiling, Poole missing areas).
See [`QT_MIGRATION_MATERIALS.md`](./QT_MIGRATION_MATERIALS.md) and
[`QT_MIGRATION_PERF.md`](./QT_MIGRATION_PERF.md).

**Phase 3 status (2026-06-10, night):** Phase 3 feature work is **done**
short of the gated retirement items. Landed today (60+ commits):
**P3.17** (Main.qml 4,681 → ~950 lines, 19 component files), **P3.18 DONE**
(all four context-menu tiers: route/mark/AIS/track menus with hit-testing,
Navigate-to, Zero XTE, Insert/Append/Split, Measure tool, AIS Target List,
chart controls, Copy/Paste-KML, undo/redo, **Send-to-Peer**,
**Send-to-GPS**), **P3.19** GPX UI, **P3.20** keyboard layer + F11,
**P3.22**, **P3.13 DONE** (edge auto-pan + waypoint snap), **P3.14** slack
markers + day labels, **P3.10** i18n infrastructure (5 seed catalogs,
QTranslator at startup), **P3.8/P3.9 closed** (audit), **P2.18/P2.20
closed** (overscale was in; grid + depth-units legend added), and **ALL
eight P3.6 prioritized gaps** (User Standard Objects checklist + Mariner's
Standard category, MMSI properties editor, HDT predictor + ring colour,
confirm-delete + advance-on-arrival-only + per-mark rings/SCAMIN, Show
Grid/Outlines, sound device; per-element fonts recommended-drop, config
templates still deferred). Plus user-driven fixes: native dialog windows
everywhere, quartered-icon + stale-position mark-dialog bugs, picker
dedup, HIG leading-label forms.
**Session update (2026-06-13):** **o-charts shop login FIXED** — the
result-5 ("plugin version obsolete") failure was a bogus version string;
now sends the wx platform-prefixed plugin version (`d./w./l.` + `2.1.17`),
verified differentially against the live API. **Translations bootstrapped
from the wx catalogs** (P3.10 — 273/265/264 finished per language).
**CM93 confirmed NOT day-1** (user, 2026-06-13): no CM93 chart set
available to verify against, so P2.19 stays parked behind the day-1
retirement gate — do not treat it as blocking. **Windows CI postponed to
post-wx-retirement** (user) — so P0.5's remaining leg is **Linux**
best-effort + the deferred Windows job, not a pre-retirement blocker.
**Raspberry Pi (aarch64) support added** (P0.8, user-requested): codebase
verified Pi-ready (GLES-baked shaders, no x86 intrinsics, no forced
desktop-GL), an `arm64` CI job on a free GitHub ARM runner, and a build
guide with **Wayland** as the headline display path (Bookworm default).
**What remains:** P2.19 CM93 (parked — plan written, decode-extraction is
the one big engineering item, but no chart set to verify), P2.17/P2.25
(chart-visual polish/defect sweep, needs charts + eyes), P3.6
config-templates (deferred) + fonts decision, the new QtQuick-only-string
translations, and the **gated retirement sequence** — Phase-4 plugin
decision (user), macOS acceptance pass, then P3.11 wx removal + P3.12
keyword cleanup, then Phases 4–6 (Windows CI folds in here).
**Last updated:** 2026-06-13.

Status legend: `[ ]` not started · `[~]` in progress · `[x]` done · `[!]` blocked
· `[—]` not applicable / dropped by decision (rationale given).
Task IDs (`P1.2`) are stable — never renumber; add `Pn.x` for new work.

---

## Phase 0 — Fork & scaffold  (est. 2–3 wks)

- [x] **P0.1** Create the fork repo; record upstream remote for future
      cherry-picks. Done: `origin` = aestubbs/OpenCPN (push), `upstream` =
      OpenCPN/OpenCPN (pull); work on branch `migrate_to_qt`. *(Marked done
      2026-06-10 — had been left unchecked.)*
- [x] **P0.2** Install/pin Qt 6. Installed Qt **6.11.0** via Homebrew at
      `/opt/homebrew/opt/qt` (keg-only). Toolchain doc per target:
      [`QT_TOOLCHAIN.md`](./QT_TOOLCHAIN.md) (2026-06-10 — incl. the
      stale-AOT-cache gotcha and the expected Linux/Windows package sets
      for the P0.5 CI gate).
- [x] **P0.3** Qt discovery centralised in top-level `CMakeLists.txt`
      (`find_package(Qt6 Core)` + Homebrew keg-only hint, guarded `NOT QT_ANDROID`).
      Added `OCPN_USE_QT_GUI` option (OFF — placeholder for the Phase 3 GUI switch).
      Temporary hint removed from `libs/observable`. Full configure verified:
      exit 0, finds Qt 6.11.0, wx build still works.
- [x] **P0.4** Scaffolded `opencpn-qt` — `qt_add_executable` + `qt_add_qml_module`
      with `Main.qml` and a `ChartCanvas` `QQuickItem`. Sibling target to the
      legacy wx `OpenCPN`; both build clean. (`gui/qt/main.cpp`,
      `gui/qt/qml/Main.qml`, `gui/qt/CMakeLists.txt`.)
- [x] **P0.5** CI for `opencpn-qt` — **GREEN on macOS (2026-06-10, run
      27304414174):** clean-runner build of the app + toolkit + all four
      plugins + a 20 s smoke-run, after a dependency-fix iteration
      (MPG123 et al.). **LINUX BUILD GREEN (2026-06-12, run
      27426653493)** — first-ever full Linux compile of the branch,
      after a five-fix ladder: libglew-dev; the QWebSocket
      errorOccurred 6.5 rename guard; Qt 6.8 via install-qt-action
      (distro 6.4 predates loadFromModule etc.); X11 macro scrubs in
      s52_sg.h and the guard-less x11_macro_scrub.h (the fixx11h
      pattern) for gui/qt's wx-mixing TUs. Still continue-on-error +
      build-only (no smoke run); promote to required after a few
      stable runs. Windows pends the toolkit export macros.
      **Deferred by decision 2026-06-10 (macOS-first):** verification
      stays on macOS; the Linux leg of the **pre-P3.11 gate** (P3.21)
      is now satisfied at build level.
      **arm64 / Raspberry Pi build leg (P0.8, 2026-06-13):** added a
      `build-linux-arm64` job on GitHub's free `ubuntu-24.04-arm` runner
      with Qt 6.8 from the official linux_arm64 binaries (aqt
      host=linux_arm64 arch=linux_gcc_arm64) — see P0.8 below.
- [~] **P0.8** **Raspberry Pi (aarch64) support** — user-requested
      (2026-06-13). A Pi 4/5 is aarch64 Linux rendering via OpenGL ES
      (Mesa V3D). **Codebase verified Pi-ready** against the tree before
      writing any port: the `.qsb` shaders already bake a `GLSL 100 es`
      variant (`qsb -d` confirms), there are **no x86 intrinsics** in
      `gui/qt`/`libs/s52plib`/`libs/s57-charts`, and `main.cpp` never
      forces a desktop-GL profile (MSAA samples only) so RHI auto-selects
      GLES. Fusion controls style on Linux; all deps in Pi OS / Ubuntu
      arm64 apt. The only prerequisite is **Qt 6.5+** (`loadFromModule`)
      vs Pi OS Bookworm's apt **Qt 6.4** — handled via aqt's prebuilt
      arm64 Qt. **Done:** `build-linux-arm64` CI job (best-effort, like
      the x86 Linux job — proves compile+link; GLES runtime needs real
      hardware) — **GREEN 2026-06-13 (run 27473991118): 552/552, linked
      `gui/qt/opencpn-qt` on aarch64 with Qt 6.11** — plus `pibuild.sh`
      (one-shot Pi build script) + `Docs/QT_RASPBERRY_PI_BUILD.md`
      (build on hardware;
      **Wayland is the headline path** since Bookworm defaults to a
      Wayland compositor — native `wayland`/qtwayland, XWayland fallback,
      eglfs kiosk; V3D MSAA tuning; ARM `oexserverd` note for o-charts).
      Remaining: runtime verification on a real Pi — GLES on V3D (needs
      hardware + eyes; the build itself is CI-green); optional
      `loadFromModule` fallback to also build on distro Qt 6.4.
- [x] **P0.6** Repo layout decided: new Qt-Quick code lives in `gui/qt/`
      (subdir of the existing `gui/` tree — closest to what it'll
      eventually replace; the legacy `gui/src/` retires in Phase 3).
- [—] **P0.7** Image-diff test harness — **dropped by decision**. The Qt
      renderer intentionally diverges in style from wx (system fonts, quilt
      model, line rendering), so pixel-diffing against the wx renderer is all
      false positives. Chart rendering is verified visually/manually instead.
      Closes P2.0 and reframes X.3 the same way.

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
  - [x] **P1.5k** TCP server-mode + GPSD — **DONE (2026-06-12), both
        supported** (GPSD is standard on Linux nav boxes; server-mode is
        a wx-parity feature): `TcpServerTransport` (`QTcpServer`;
        multi-client — reads aggregate, writes broadcast; legacy served
        one) and a connect-greeting option on `TcpClientTransport`
        carrying the legacy `?WATCH` subscription verbatim. The legacy
        `comm_drv_n0183_net.{h,cpp}` is **deleted**. Collateral fix:
        Multiplexer's echo path downcast to the legacy CommDriverN0183
        (assert/null-deref with framework drivers) — now
        ConnectionParamsProvider, like the P1.5j-1 output-path fix; the
        route-upload connect-wait polls DriverStats.available.
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
  - [x] **P1.5c** `signalk_net` on the framework — **DONE (2026-06-12).**
        `WebSocketTransport` (`QWebSocket`; frame-native messages, 30 s
        keepalive ping, self-signed-cert tolerance, ws↔wss alternation
        on error, failed-connect → Disconnected so reconnect arms) +
        `PassThroughFramer` + new `SignalKDecoder` (rapidjson
        validation, stateful self/context tracking ported from
        `HandleSkSentence`; receive-only; 7 unit tests). SignalK input
        WORKS AGAIN in opencpn-qt — the parked state produced no driver
        at all. Qt6::WebSockets linked; Linux CI dep added. The legacy
        IXWebSocket driver stays in-tree pending P1.5m deletion.
  - [~] **P1.5g** *(parked — see P1.5m)* `n2k_socketcan` on the framework —
        `CanTransport` (`QCanBusDevice`) + `PassThroughFramer` + `N2kDecoder`.
        Replaces the raw `PF_CAN` socket / `ioctl` / `Worker` thread. Backend
        by name — `socketcan` (Linux, real HW) or `virtualcan` (macOS).
  - [ ] **P1.5m** *(revisit)* ~~Un-park SignalK~~ (P1.5c landed
        2026-06-12) and SocketCAN. Remaining: delete the parked legacy
        sources — `comm_drv_signalk{,_net}.{h,cpp}` (+ the
        `ocpn::ixwebsocket` model link and the wx-app
        init/uninitIXNetSystem calls) and `comm_drv_n2k_socketcan
        .{h,cpp}` — once P1.5g lands or with the P3.11 source deletion.
  - [x] **P1.5h** `ser_ports.cpp` serial-port enumeration → `QSerialPortInfo`.
        Five platform implementations (sysfs scan, libudev, Win32 SetupAPI,
        macOS IOKit, vendored libserial) behind an `#ifdef` maze collapse to
        one ~30-line cross-platform function. `libs/serial` stays for other
        GUI callers.
  - [~] **P1.5j** Refactor the standalone P1.5a (`n2k_net`) and P1.5e
        (`n0183_serial`) onto the framework, retiring their bespoke code.
    - [x] **P1.5j-1** `n0183_serial` re-homed onto the framework; bespoke
          `CommDriverN0183Serial` deleted; Garmin host mode dropped from
          scope. Also fixed a latent P1.5b bug: the 0183 *output* path
          downcast every driver to `CommDriverN0183` for `GetParams()`,
          which a framework driver is not — 0183 transmit to a TCP/UDP
          connection was broken. New `ConnectionParamsProvider` capability
          interface (cf. `DriverStatsProvider`) replaces the concrete
          downcast.
    - [ ] **P1.5j-2** `n2k_net` onto the framework. Deferred: it already
          runs as a working Qt-native driver (P1.5a — no `wxEvtHandler`/
          `wxSocket`); re-homing it means re-expressing ~1000 lines of
          multi-format gateway parsing (YD_RAW, Actisense ASCII/binary,
          SeaSmart, MiniPlex) + fast-packet reassembly into a multi-format
          `N2kGatewayFramer`/`N2kDecoder` — high cost, zero functional gain.
          Best done alongside the P1.6 `wxString` sweep of those parsers.
  - [x] **P1.5f** Deleted the Android comm drivers (`comm_drv_n0183_android_*`,
        `INTERNAL_GPS`/`INTERNAL_BT`) and Android serial I/O
        (`android_serial_io.cpp`, deleted with P1.5e). Android is dropped for
        the migration (`QT_MIGRATION.md` §1, X.4); mobile returns natively via
        QtQuick after the core is on Qt. Desktop build green.
- [x] **P1.6** Remove `wxString` from `model/`, behind facade/adaptor
      boundaries (the wx-typed value stays at the boundary; the layer below is
      wx-free; an adaptor bridges them — reusable as each layer migrates).

      **Direction of travel — follow the data flow, not the call graph.** The
      wx-free region grows *downstream* (bytes → frames → NavMsg → nav data),
      because a facade can only sit where the layer *outside* it is still wx.
      `ConnectionParams` is the **terminal upstream facade**: it is pinned by
      the wx GUI (16 `gui/` files edit it; it even embeds a
      `ConnectionParamsPanel*`) and by config persistence
      (`Serialize`/`Deserialize`). It de-wx'es *last*, gated on Phase 3 (GUI)
      and P1.9 (config) — not next. (It is *not* in the plugin ABI, so plugins
      do not pin it.) So P1.6 advances downstream into the decode layer, with
      each new boundary at wherever that layer next hands data to wx code.
  - [x] **P1.6a** Comms pipeline de-wx'd. The framework (`CommTransport`,
        `Framer`s, `Nmea0183Decoder`/`N2kDecoder`, generic `CommDriver`,
        `N2kGatewayManager`) no longer depends on wxWidgets. New wx-free
        `SentenceFilter` value type replaces the `wxString`/`wxRegEx` input
        filter; `ConnectionParams::MakeInputFilter()` is the adaptor; the
        factory adapts `ConnectionParams` → wx-free pipeline inputs. Logging
        moved to Qt (`qWarning`/`qInfo`). `ConnectionParams` itself stays
        `wxString` — the boundary, shared with the wx GUI and plugin ABI.
  - [x] **P1.6b** De-wx the **decode layer** — the stage downstream of the
        (wx-free) `NavMsgBus` that turns the `NavMsg` stream into nav data
        (positions, AIS, HUD). This extends the clean pipeline from *bytes-in*
        through to *nav-data-out* — the core functionality.
    - [x] `comm_decoder.cpp` — wx-free internals; one `wxString` boundary to
          the still-wx `libs/nmea0183` parser (`ParseSentence`).
    - [x] `comm_bridge.cpp` — the priority-source machinery is now Qt-native:
          `PriorityMap` is `QHash<QString,int>` and `PriorityContainer`'s
          string fields are `QString`; keys are built/parsed with `QString`.
          Watchdog logging on `qInfo`, `QDateTime` timestamps. `std::string`
          survives only at the two pinned boundaries — the plugin ABI
          (`GetPriorityMaps`/`GetActivePriorityIdentifiers` in `ocpn_plugin.h`,
          adapted with `toStdString` in `ocpn_plugin_gui.cpp`) and config
          (`Load/SaveConfig`, P1.9). Other remaining wx: the `wxWindow` GUI
          lookup (`GetDataMonitor`) and a `wxString` hand-off to the still-wx
          AIS parser.
    - [x] `ais_decoder.cpp` — the largest unit (~4.7k lines). All `wxString`
          → `QString`; `wxStringTokenizer` → `QString::split` (with
          `Qt::KeepEmptyParts` — NMEA fields are positional and may be empty);
          `wxAtoi`/`wxRound`/`wxMin`/`wxMax`/`wxLogMessage` → Qt/std. The
          `DecodeSingleVDO` signature is now `QString`, so the `wxString`
          hand-off `comm_bridge` bridged to is gone (and `comm_ais` /
          `ocpn_plugin_gui` callers adapted). `wxString` remains only at
          boundary types — `MmsiProperties` (config-serialized, GUI-embedded),
          the `GetShipNameFromFile`/`UpdateMMSItoNameFile`/`GetMMSItoNameEntry`
          name-file API, and `wxString`-typed members of not-yet-migrated
          headers (`ais_target_data.h`, `meteo_points.h`). `wxDateTime`,
          `wxTimer`/`wxEvtHandler`, `wxTextFile`/`wxFileName` stay for
          P1.7/P1.11/P1.10.
  - [x] **P1.6c** Downstream/adjacent layers (routes, nav object DB).
        **Strategy shift:** the model types at this layer (`AisTargetData`,
        `Route`, `RoutePoint`, `Track`, `Routeman`, `WayPointman`, …) are
        *shared* with the wx GUI — their `wxString` surface has no wx-free
        facade; it *is* the boundary. From P1.6c these are converted
        *through*: the model type goes `QString`, and the wx GUI call sites
        are adapted now (a `wxString`⇄`QString` conversion stays local at
        each wx widget call) rather than deferred to Phase 3. The shared
        `model/wx_qt_string.h` (added during the route step) centralizes the
        conversions with an explicit UTF-8 round-trip. `ConnectionParams` +
        config and the legacy gateway parsers (`n2k_net`) are still swept
        only when their outer layer migrates (Phase 3 / P1.9) or the driver
        is revived — not pre-emptively.
    - [x] `ais_target_data` — `AisTargetData` is `QString` throughout: the 7
          display-string methods (`BuildQueryResult`, `GetFullName`, …), the 5
          free functions (`trimAISField`, `ais_get_status`, `make_hash_ERI`,
          …) and the data fields (`m_date_string`, `MSG_14_text`,
          `Ais8_001_22_SubArea::text`). ~8 wx GUI consumers adapted at the
          call site. `wxDateTime` fields and the `_()` macro stay (P1.7 /
          P3.10); a `FromWx()` helper bridges `_()` results into `QString`.
    - [x] `route` + `route_point` — the route/waypoint data model.
          `Route` and `RoutePoint` are `QString` throughout: GUIDs, names,
          descriptions, icon names, the colour-*name* field, the
          `GpxxColorNames[]` table, all method params/returns; `wxArrayString`
          → `QStringList`. ~30 consumer files adapted at the call site.
          Introduced `model/wx_qt_string.h` — shared inline UTF-8 converters
          (`wxString_to_QString`/`QString_to_wxString`); every wx⇄Qt string
          conversion in the migration now goes through an explicit UTF-8
          round-trip, never a locale-dependent default ctor. `wxDateTime`,
          the GUI-drawing types (`wxColour`/`wxBitmap`/`wxPen`/…) and
          `Route`'s `wxObject` base stay for P1.7 / P1.14 / later.
    - [x] `track` — `Track` and `TrackPoint` are `QString` throughout:
          GUIDs, names, descriptions, start/end strings, the colour-name
          field, `GetIsoDateTime`/`GetDateTime`, TrackPoint's
          timestamp-string ctor. ~17 consumer files adapted. `wxDateTime`,
          `wxTimer`/`wxEvtHandler`, `wxGenericProgressDialog` stay (P1.7 /
          P1.11 / P1.10).
    - [x] `routeman` + `waypointman` + `markicon` — Route/Track manager and
          waypoint-icon catalog. `Find{Route,Track,RoutePoint}ByGUID`,
          `CreateGUID` (returns `QString`), the `GetIcon*` accessors,
          `MarkIcon::icon_name`/`icon_description` and the
          `std::function<wxColour(QString)> get_global_colour` callback
          parameter all migrated. ~20 consumer files adapted. The drawing
          types (`wxBitmap`/`wxPen`/`wxBrush`/`wxImage`/`wxImageList`/…)
          stay for P1.14.
    - [x] `nav_object_database` + `navobj_db` — GPX I/O and SQLite
          persistence. ~99 + ~23 wxString sites converted to `QString`;
          pugixml boundary uses `toUtf8().constData()`. The stop-gap
          file-local `ws2qs`/`qs2ws` helpers earlier P1.6c steps had added
          to `nav_object_database.cpp` are deleted; everything now routes
          through `model/wx_qt_string.h`. `wxFileName` / `wxFileExists` /
          `wxRenameFile` stay for P1.10.
- [x] **P1.7** Sweep `wxDateTime`/`wxTimeSpan` → `QDateTime`/`qint64`-seconds.
      `wxTimeSpan` has no Qt equivalent; durations are `qint64` (seconds or
      milliseconds, commented where ambiguous). Done in 6 steps:
  - [x] **P1.7-1** Foundation: `datetime` (`getUsrDateTimeFormat`,
        `toUsrDateTimeFormat`) and `navutil_base` (`ParseGPXDateTime`, the
        three `formatTimeDelta` overloads collapsed to one). New
        `StrftimeToQtFormat` bridges strftime codes to Qt's; `QLocale`
        replaces `wxUILocale`.
  - [x] **P1.7-2** AIS path: `ais_target_data` (`Ais8_001_22::start/expiry_time`,
        `m_ack_time`), `ais_decoder` (~36 sites), `comm_ais`, AIS GUI
        dialogs. Drops wx's `+1` month adjustment (Qt is 1-based).
  - [x] **P1.7-3** Route/waypoint/track family: `RoutePoint` (`m_seg_etd/eta`
        `QDateTime`, `m_seg_ete` `qint64`-seconds, ETD parser), `Route`
        (`m_PlannedDeparture`), `Track`/`TrackPoint` (closes the
        `AddNewPoint(...,wxDateTime)` boundary the AIS step left).
        Persistence formats preserved (SQLite Unix seconds, GPX ISO 8601 +
        `Z`, KML same). Plugin ABI `PlugIn_*::m_CreateTime`/`m_ETD`/
        `m_PlannedDeparture` stays `wxDateTime`; bridged with
        `QDateTimeToWxDateTimeUtc`/`WxDateTimeToQDateTimeUtc` helpers.
  - [x] **P1.7-4** Misc model + comms: `comm_can_util`, `autopilot_output`,
        `garmin_protocol_mgr`, `comm_drv_n2k_net`, `gui_vars`
        (`g_loglast_time`/`g_start_time`/`gTimeSource` → `QDateTime`),
        `notification_manager`(`_gui`).
  - [x] **P1.7-5** GUI shell: `ocpn_frame` (+ header), `ocpn_app`, `chcanv`
        (+ header), `concanv`, `navutil`, `pluginmanager`, `ocpn_plugin_gui`,
        `route_prop_dlg_impl` leftovers.
  - [x] **P1.7-6** Tides/charts + final mop-up: `tc_win`/`tcmgr` (dropped
        the wx 3.0.2 ToGMT-with-DST workaround; Qt's `offsetFromUtc()` /
        `isDaylightTime()` are correct), `chartdbs`/`chartdb`/`chartimg`/
        `cm93`/`s57chart`/`o_senc` (chart edition / last-modified dates,
        `ChartBase::m_EdDate` → `QDateTime`), `time_textbox` kept hybrid
        (wxTimePickerCtrl-compat shim — pickers go in P1.10), wx file-time
        boundary call sites adapted.
- [x] **P1.8** Replace wx containers with Qt — Qt throughout, STL only at
      facade boundaries where Qt isn't viable over wx. Done in 3 steps:
  - [x] **P1.8-1** Model layer + plugin-ABI bridging. Header typedef
        changes (`ArrayOfMmsiProperties` / `ArrayOfPlugIns` /
        `ArrayOfMarkIcon` → `QList<T*>`, `AIS_Target_Name_Hash` →
        `QHash<int, wxString>`); `GetRouteArrayContaining` returns
        `QList<Route*>*` (strongly typed); `EnumerateSerialPorts` returns
        `QStringList*`. `model/plugin_api.cpp` tokenizer bridged.
        `ConnectionParams::Input/OutputSentenceList` deliberately kept
        `wxArrayString` (the GUI write/read boundary).
  - [x] **P1.8-2** Top GUI consumers. `m_pSerialArray` /
        `m_font_element_array` / `m_plugin_order` /
        `m_missing_name_array` / `pMessageOnceArray` /
        `ConfigGUIDs` retyped to `QStringList`; `priority_gui` /
        `pluginmanager` / `navutil` / `options` tokenizers and array
        ops migrated. Pure widget-fill `wxArrayString` locals
        (`filter_dlg`, parts of `options`/`connection_edit`) kept wx
        per the GUI-boundary rule.
  - [x] **P1.8-3** GUI mop-up. Header typedefs across `cm93` /
        `styles` / `font_mgr` / `o_senc` / `pluginmanager` /
        `chartdbs` / `chartdb` / `chcanv` / `canvas_config` /
        `mark_info` / `config_mgr` (vestigial `WX_DECLARE_OBJARRAY`
        decls removed; remainder → `using = QList<T*>` / `QStringList`).
        `compress_target` arrays in `ocpn_frame` / `gl_texture_mgr`
        rewritten from heap-pointer wxObjArray to `QList` by value.
        Total: 168 remaining wx-container hits, all categorized
        boundaries (frozen plugin ABI, `wxDir`/`wxFileName`, wx-widget
        plumbing, `wxExecute`, `wxLocale::AddCatalog`, S52PLIB-owned,
        `wxAUI`/`wxListCtrl` API). The `cm93` covr-desc OBJARRAY +
        `wxList`-node iteration, `station_data` / `tc_data_source` /
        `idx_entry` ownership-by-add arrays, `PatchList`, and
        `SortedArrayOfMarkIcon` are deferred — their contained types
        own raw resources in their destructors with no copy ctor, so
        the conversion needs a dedicated ownership pass.
- [x] **P1.9** Replace `wxConfig`/`wxFileConfig` with `QSettings`; abstract
      `config_vars`. Done in 3 steps. End state: no `wxConfig`/`wxFileConfig`
      types remain; `OcpnConfig` (wraps `QSettings(IniFormat)`, same file path
      so existing user configs load unchanged) has a pure Qt-idiomatic
      surface — `value`/`setValue`/`beginGroup`/`endGroup`/`endAllGroups`/
      `group`/`contains`/`remove`/`childKeys`/`childGroups`/`sync`. The 870-ish
      caller sites in `navutil`/`ocpn_platform`/`ocpn_frame`/`pluginmanager`/
      `wiz_ui`/etc. went through `OcpnConfig` plus a `gui/config_compat_
      helpers.h` (header-only `ocpn_cfg::Cfg{Read,ReadIf,ReadStr,Write,
      HasEntry,HasGroup,Delete}` free functions) that keeps the conversion
      one-liner-per-site and centralises the wxString⇄QString plumbing — to
      be retired piecemeal in later cleanup. Plugin ABI:
      `GetOCPNConfigObject()` (declared `wxFileConfig*` in `ocpn_plugin.h`)
      keeps its frozen signature but returns `nullptr` for now, with a TODO
      noting the two paths (build a `wxFileConfig` adapter delegating to
      `OcpnConfig`, or sunset the accessor). The one internal caller is
      rewired through `TheBaseConfig()`.
  - [x] **P1.9-1** OcpnConfig + wiring — `MyConfig : public OcpnConfig`,
        `TheBaseConfig() -> OcpnConfig*`, `wx-compat shim methods` provided
        on OcpnConfig so the ~820 navutil call sites kept working unchanged
        during the transition. `ConfigVar<T>` impl relocated
        `libs/observable/src` → `model/src` so its OcpnConfig instantiation
        stays in the right layer.
  - [x] **P1.9-2** Small callers (`comm_bridge`, `rest_server`,
        `plugin_api`, `canvas_config`, `config_mgr`, `mbtiles`,
        `chartimg`) converted to the Qt-idiomatic OcpnConfig API; ~260
        wx-shim calls eliminated.
  - [x] **P1.9-3** Big sweep — navutil.cpp + observable_confvar.cpp + 7
        other GUI consumers swept (~870 sites). OcpnConfig's wx-compat
        shim methods deleted (`Read`/`Write`/`SetPath`/`GetPath`/`Flush`/
        `HasGroup`/`HasEntry`/`DeleteEntry`/`DeleteGroup`/
        `GetNumberOfEntries`/`GetNumberOfGroups`/`GetFirst*`/`GetNext*`);
        `ocpn_config.cpp` shrank ~347 → 75 lines. Path-balance handled
        via the new `endAllGroups()` helper (pops the QSettings group
        stack to root), so wxConfig's `SetPath("/X")` absolute-jump
        idiom translates to `endAllGroups(); beginGroup("X");` —
        structurally robust against early-returns in long load/save
        functions.
- [x] **P1.10** Replace file I/O (`wxFileName`/`wxDir`/`wxFile`/`wxFFile`/
      `wxTextFile`/`wxStandardPaths`) with `QFile`/`QDir`/`QFileInfo`/
      `QStandardPaths`/`QTextStream`. Done in 3 steps. Path/file/dir
      manipulation is Qt throughout; the chart-reader `wxInputStream`/
      `wxOutputStream` streams (chartimg/chartdbs/cm93/o_senc/s57chart binary
      formats) are deliberately deferred — that's a chart-reader stream
      refactor that ripples into the binary parsers and is out of P1.10 scope.
      Inside `chartdata_input_stream` the internal `wxFFile *` is
      `QFile *`, but the `wxInputStream` base stays. macOS deployment target
      bumped from 10.13 → 10.15 (per-target on `_model_src` and the top-level
      OpenCPN target) because Qt 6's file-I/O headers expose
      `std::filesystem::path` overloads guarded by libc++ 10.15+ attributes.
      `wxStandardPaths` calls in `base_platform` that resolve to macOS
      bundle paths stay wx — Qt's `QStandardPaths::ConfigLocation` resolves
      to `~/Library/Application Support` on macOS where wx uses
      `~/Library/Preferences`, so existing user configs would move.
  - [x] **P1.10-1** Model layer: base_platform / plugin_handler /
        plugin_cache / plugin_loader / navobj_db / notification_manager /
        svg_utils / chartdata_input_stream internals.
  - [x] **P1.10-2** Chart subsystem: s57chart / cm93 / chartdbs /
        chartimg / o_senc / chartdb / quilt / gl_tex_cache /
        waypointman_gui. wxInputStream/wxOutputStream consumer interfaces
        deliberately preserved.
  - [x] **P1.10-3** GUI shell + final audit: navutil / ocpn_platform /
        styles / options / pluginmanager / ocpn_app / ocpn_frame /
        notification_manager_gui / routemanagerdialog / s57 dialogs /
        update_mgr / initwiz / etc. wxTempFile + Commit() atomic-replace
        → QFile write to *.tmp + remove(orig) + rename(tmp, orig).
        wxFileName::CreateTempFileName(prefix) → unique path under
        QStandardPaths::TempLocation. Plugin-ABI shims keep wxString
        returns, Qt-typed internally.
- [x] **P1.11** Replace threading primitives + the standalone (non-
      `wxWindow`) `wxTimer`/`wxEvtHandler`/event-table use with Qt.
      Strategy: Qt throughout — `QThread` / `QMutex` / `QSemaphore` /
      `QWaitCondition` / `QTimer` / `QElapsedTimer` / `QObject` with
      `Q_OBJECT` and `Q_SLOTS:` / `Q_SIGNALS:` / `Q_EMIT` (project uses
      `QT_NO_KEYWORDS`). `wxStopWatch` → `QElapsedTimer`. Done in 4 sub-
      steps. End state: every threading primitive and standalone event-
      loop use in the model layer is Qt; in `gui/`, the remaining
      `wxTimer`/`wxEvtHandler` is intrinsic to `wxWindow`/`wxFrame`/
      `wxDialog` subclasses and is deferred to Phase 3 (the QtQuick
      port). `libs/wxcurl` + `libs/wxservdisc` are deferred to P1.13
      (whole-library replacement).
  - [x] **P1.11-1** Model centerpiece: `AisDecoder` dropped its
        `wxEvtHandler` base for `QObject` (Q_OBJECT) + 3 × `QTimer` with
        slot connections; its 17 `ObservableListener`-via-event-table
        listeners upgraded to `ObsListener::Init(KeyProvider, lambda)`
        (the same pattern `comm_bridge` uses). Also: `GarminProtocol
        Handler` (`wxThread` → `QThread`, event table → connect),
        `RestServer` (`wxSemaphore` → `QSemaphore`; IO-thread → main
        handoff via `Qt::DirectConnection` to avoid the deadlock that
        `QueuedConnection` would cause with the CV-wait pattern),
        `CommDriverN0183Net` (parked legacy), `comm_drv_n2k_socketcan`
        (parked) all converted.
  - [x] **P1.11-2** Chart subsystem: `gl_texture_mgr` (texture-compress
        workers), `chartdb_thread` (`PoolWorkerThread` /
        `ChartTableEntryPoolThread` `wxThread`→`QThread` with
        `connect(&QThread::finished, &QObject::deleteLater)` to
        replicate `wxTHREAD_DETACHED` self-cleanup), `chartdb`,
        `chartimg`, `s57chart`'s GDAL critical section → `QMutex`.
  - [x] **P1.11-3** Mop-up + `wxStopWatch` → `QElapsedTimer`:
        `senc_manager` (`SENCBuildThread` → `QThread`),
        `gl_texture_mgr.h` (`CompressionPoolThread` → `QThread`,
        `OCPNStopWatch` rewrapped on `QElapsedTimer`),
        `gshhs`/`ocpn_frame`/`ocpn_platform`/`o_senc`/the
        `wxStopWatch` GUI sites.
  - [x] **P1.11-3b** Model holdouts the step-3 audit found:
        `NotificationManager` (added `QObject`+`Q_OBJECT`, `wxTimer` →
        `QTimer`), `ActiveTrack` (dropped `wxEvtHandler`, became
        `: public QObject, public Track` with `Q_OBJECT`, `wxTimer` →
        `QTimer`), `CommDriverSignalKNet` (parked — dropped
        `wxEvtHandler` + the custom `wxEvent` subclass; cross-thread
        post replaced with `QMetaObject::invokeMethod(... Qt::Queued
        Connection, Q_ARG(QString, ...))`), `DataMonitorSrc`
        (`wxEvtHandler` base was vestigial — just dropped).
- [x] **P1.12** Move JSON use to `QJsonDocument`; `libs/wxJSON` minimised.
      Done in 2 steps. All internal use of `wxJSONReader`/`wxJSONWriter`/
      `wxJSONValue` is converted to `QJsonDocument`/`QJsonObject`/
      `QJsonArray`/`QJsonValue`. `libs/wxJSON` **stays in the build**
      (still added via `add_subdirectory`) — required for the single
      plugin-ABI shim `GetSignalkPayload` in `model/src/plugin_api.cpp`,
      which contracts via `include/ocpn_plugin.h:6320-6326` to return a
      `std::shared_ptr<void>` pointing at a `wxJSONValue`. The body
      builds a `QJsonObject` internally and double-encodes (Qt
      `toJson(Compact)` → `wxJSONReader::Parse`) to produce the
      `wxJSONValue` for the plugin. Deleting `libs/wxJSON` outright
      would require a plugin ABI break and is deferred. Behaviour
      delta visible to plugins: Qt's parser has no warnings (only
      errors), so the payload's `WarningCount` is always 0 / `Warnings`
      empty, and `ErrorCount` is 0 or 1.
  - [x] **P1.12-1** Model + small GUI: `plugin_comm`/`routeman`/
        `navmsg_filter`/`plugin_api`/`peer_client`/`comm_drv_loopback`/
        `catalog_handler`/`track`/`ais_decoder`/`comm_n0183_output` +
        `peer_client_dlg`/`gl_chart_canvas`/`canvas_menu`/`wiz_ui`/
        `chcanv`/`ocpn_platform`. `SendJSONMessageToAllPlugins`
        internal signature → `(QString, QJsonObject)`. `routeman::
        json_msg` payload changed `shared_ptr<wxJSONValue>` →
        `shared_ptr<QJsonObject>`. `NavmsgFilter::Parse` retries with
        up to 4 appended `}` because legacy `wxJSONReader` tolerated
        the shipped filter files' missing trailing brace.
  - [x] **P1.12-2** GUI hold-outs + final audit: `ocpn_frame` (26
        sites — POST_JSON, MOB/Track/AISMOB, WMM, GRIB_TIMELINE,
        OCPN_TRACK/ROUTE/ROUTELIST/ACTIVE_ROUTELEG_REQUEST),
        `pluginmanager` (11 sites), `track_prop_dlg`, `ocpn_app`
        stray include. Transitional `wxJSONValue` overload of
        `SendJSONMessageToAllPlugins` removed; added a
        `(QString, QJsonArray)` overload because OCPN_ROUTELIST/
        ACTIVE_ROUTELEG response payloads are array-rooted (and the
        legacy wxJSON code relied on auto-promotion). On-wire
        preservation: OCPN_ROUTELIST_RESPONSE's index-from-1 slot-0-
        null quirk reproduced with `arr.append(QJsonValue())` before
        the loop.
- [x] **P1.13** Delete `libs/wxcurl`; move networking to
      `QNetworkAccessManager`. Done in 2 steps. `libs/wxcurl` removed
      entirely from the tree (33 files); CMake plumbing
      (`add_subdirectory`, `pkg_search_module SYS_WXCURL`,
      `use_bundled_lib USE_BUNDLED_WXCURL`, the bundled libcurl
      headers, the Android `libcurl.a` wiring, the `s57` and `cli`
      link entries) deleted. The `OCPN_USE_CURL` macro stays — it
      still gates the plugin-API download surface
      (`OCPN_downloadFile*` / `OCPN_postDataHttp` / `OCPN_isOnline`),
      which is a frozen ABI; renaming would break plugins.
  - [x] **P1.13-1** Model direct-libcurl (`downloader`, `mdns_cache`,
        `peer_client`) → `QNetworkAccessManager` + `QEventLoop`
        synchronous wait for the existing blocking-API shape;
        `QTimer::singleShot` watchdog for timeouts; `QSslConfiguration
        ::PeerVerifyMode::VerifyNone` to match the curl
        `SSL_VERIFYPEER=0` setting (with the same "FIXME proper certs"
        comment). User-Agent flips from `curl/<ver>` to `Qt/<ver>`.
        `QNetworkRequest::NoLessSafeRedirectPolicy` preserves curl's
        `FOLLOWLOCATION`.
  - [x] **P1.13-2** GUI `wxcurl` (`pluginmanager`) → QNAM, then
        `libs/wxcurl` deleted. `PlugInManager` became
        `: public QObject, public wxEvtHandler` (QObject first per the
        established multi-inherit pattern) with `Q_OBJECT`; AUTOMOC
        enabled on the OpenCPN target. The threaded
        `wxCurlDownloadThread` + `wxCurlDownloadDialog` modal +
        `wxCurlHTTP` sync mix collapsed onto a single QNAM with
        `QNetworkReply` slots for finished/progress. The modal-progress
        UI uses `wxProgressDialog` driven by a `QTimer`-pumped wx-yield
        loop alongside a `QEventLoop` on the QNetworkReply's `finished`
        signal — `QProgressDialog` would pull in `QtWidgets` for one
        dialog, disproportionate. `send_to_peer_dlg`'s
        `curl_easy_strerror` mapper now decodes the negative
        `QNetworkReply::NetworkError` sentinel `peer_client` sets via
        `QMetaEnum::valueToKey`.
- [x] **P1.14** Abstract route/mark UI types (`wxColour`→`QColor`,
      `wxPen`→`QPen`, `wxBrush`→`QBrush`, `wxBitmap`→`QImage`,
      `wxFont`→`QFont`) in the model surface. The wider GUI drawing
      code stays wx — Phase 3 / QtQuick port territory. Added
      `model/include/model/wx_qt_ui_types.h` (header-only) with the
      inline `QColorToWxColour` / `WxColourToQColor` /
      `QImageToWxImage` / `QImageToWxBitmap` / `WxImageToQImage` /
      `WxBitmapToQImage` / `QPenToWxPen` / `QBrushToWxBrush` /
      `QFontToWxFont` bridges for the model⇄wx-GUI boundary. `Route
      Point` font is now a `QFont` value member with a parallel
      `m_MarkFontInitialized` flag (preserves the legacy "null-pointer
      means-not-yet-loaded" semantic); `MarkIcon::piconBitmap` stays a
      heap pointer (`QImage*`, owned by `WayPointman`) to keep the
      lazy-build semantic. Routeman's icon API
      (`GetIconBitmap`/`Get{Icon,X,F}ImageListIndex`) returns
      `const QImage*`; `GetIconImageListIndex` still produces a wx
      image list (consumers are wx widgets) but builds it from a
      `QImage` via the bridge. CMake adds `Qt6::Gui` to the model.
      `QColor::name()` replaces `wxColour::GetAsString(wxC2S_HTML_SYNTAX)`
      for config I/O (bytewise-equivalent for opaque colors).
- [x] **P1.15** Phase 1 verification — core sweeps complete; remaining
      `wx` references all in documented deliberate-boundary buckets;
      build + tests green. Strict "core is wx-free" wasn't the achievable
      end state because the **plugin ABI is frozen** (`include/
      ocpn_plugin.h` and its 200+ exported `extern "C"` / virtual surface
      uses `wxString` / `wxArrayString` / `wxDateTime` / `wxJSONValue` /
      `wxBitmap` / `wxColour` / `wxEvtHandler*` widely) — breaking it
      would orphan every plugin. Phase 1's substantive goal — *the model
      is Qt-typed throughout, with explicit, documented wx-typed
      boundaries* — is met. Build: `OpenCPN` + `tests` clean, 58/59 pass
      (the single `DateTimeFormatTest.LocalTimezoneCETSwedish` failure
      is the pre-existing locale-test issue tracked from before P1.6,
      unrelated to any migration step).

      **Remaining wx in `model/` — categorized boundaries** (none of
      these can be removed inside Phase 1):
      1. **Plugin ABI** — `plugin_loader` / `plugin_handler` /
         `plugin_comm` / `plugin_api` / `ocpn_plugin` (and their GUI
         consumer `pluginmanager` + `ocpn_plugin_gui`): the entire
         `extern "C" DECL_EXP` surface in `ocpn_plugin.h` is frozen
         until a plugin ABI break.
      2. **Boundary types kept wx** by earlier phases: `ConnectionParams`
         sentence lists (P1.6a), `MmsiProperties` (P1.6c), AIS name-file
         API (P1.6c), `wxStandardPaths` for macOS bundle paths in
         `base_platform` (P1.10 — Qt resolves to different dirs).
      3. **Library-API boundaries**: `wxInputStream`/`wxOutputStream`
         chart-reader interfaces (`chartdbs`/`chartimg`/`cm93`/`o_senc`/
         `s57chart`) — a chart-reader-stream refactor is its own
         multi-week sub-project. `libs/wxJSON` retained only for the
         `GetSignalkPayload` plugin-ABI shim (`model/src/plugin_api.cpp`).
         `libs/wxservdisc` for mDNS service discovery (could route
         through `libs/mdns` later but separate concern from "HTTP
         networking" P1.13).
      4. **Logging**: `wxLogMessage` / `wxLogWarning` / `wxLogDebug` /
         `wxLog` infrastructure in `logger.{h,cpp}` and 67 calls in
         `plugin_loader`, 23 in `garmin_protocol_mgr`, 10 each in
         `rest_server` / `navobj_db` / `comm_n0183_output` /
         `comm_drv_signalk_net` / `comm_drv_n2k_socketcan`. These are
         single-line conversions to `qInfo`/`qWarning`/`qDebug` —
         straightforward but cosmetic; tracked for a future polish pass
         (no functional impact; both wx and Qt logging share a sink).
      5. **GUI-bridge code** in model files: `wxScreenDC` text-extent
         calls (`route_point.cpp::CalculateNameExtents`), `wxImageList`
         construction inside `routeman::GetIconImageListIndex` (consumers
         are wx widgets), `wxFileName::GetModificationTime` callers
         (small — wxFileName is P1.10-deferred for plugin-API
         compatibility). Each bridges via the `model/wx_qt_*` helpers.
      6. **Comments and `#if 0` blocks** referencing the historical wx
         idioms — non-code.

      **What Phase 1 delivered, in vocabulary**: the *model layer's
      programming model* is now `QString` / `QStringList` / `QList<T*>`
      / `QHash` / `QSet` / `QDateTime` / `qint64`-seconds / `OcpnConfig`
      (over `QSettings`) / `QFile` / `QDir` / `QFileInfo` /
      `QStandardPaths` / `QThread` / `QMutex` / `QSemaphore` / `QTimer`
      / `QElapsedTimer` / `QObject` with signals/slots /
      `QJsonDocument` / `QNetworkAccessManager` / `QColor` / `QPen` /
      `QBrush` / `QImage` / `QFont`. Bridge headers (`model/wx_qt_
      string.h` for strings, `model/wx_qt_ui_types.h` for UI types,
      `gui/config_compat_helpers.h` for the wxConfig-style call sites)
      centralize the small surface area of conversions remaining at the
      wx-GUI boundary. Phase 1 closed. Next: Phase 2 (scene graph +
      LayerCompositor — the chart-rendering port).

## Phase 2 — Scene graph + LayerCompositor  (est. 10–16 wks)

**Revised 2026-05-21 after Phase 1 completion.** Phase 1 delivered a model
layer that is Qt-typed end to end (`QColor`/`QPen`/`QImage`/`QFont`,
`QObject` with signals/slots, `QString`/`QDateTime`/`QStringList`,
`OcpnConfig`/`QSettings`, `QFile`/`QDir`, `QNetworkAccessManager`,
`QJsonDocument`, `QThread`/`QTimer`/`QMutex`). Several Phase 2 tasks
shrink as a consequence — what was originally "build the infrastructure"
becomes "wire up existing primitives".

### Architecture — three rendering tiers

Baseline is the high-level QtQuick scene-graph approach (per
[`QT_MIGRATION.md`](./QT_MIGRATION.md) §5). One chart-canvas `QQuickItem`
plus declarative QML on top:

| Tier | Anchored to | Implementation | Example contents |
|---|---|---|---|
| **World-anchored scene** | chart lat/lon | `QSGTransformNode` (viewport matrix) → per-Layer subtrees | raw chart tiles, S-52 vector objects, AIS targets, routes, tracks |
| **Display-anchored scene** | screen pixels | identity `QSGTransformNode` → per-Layer subtrees | radar/PPI overlay, range rings, compass rose, mini-map |
| **QML HUD** | screen pixels (declarative) | QML items *above* the chart `QQuickItem` (not a scene-graph node) | depth, SOG/COG, wind, alarms — `Q_PROPERTY` + binding, animation for free |

Plugins (future) will register their own subtree at the World or Display
tier, or contribute QML items to the HUD — same `Layer` abstraction.
That's the simplification the user is after.

For composition (no custom code): `QSGTransformNode` (viewport),
`QSGOpacityNode` (per-Layer opacity), `QSGClipNode` (clip to viewport).
For most leaf nodes (no shader code): `QSGImageNode`, `QSGSimpleRectNode`,
`QSGFlatColorMaterial`, `QSGVertexColorMaterial`, `QSGOpaqueTextureMaterial`,
`QSGTextureMaterial`. The original "port 6 shaders" estimate (old P2.4)
is probably ~2–3 custom shaders once we account for how many old programs
map to built-in materials.

**Raw RHI is an escape hatch, not a baseline.** The scene graph already
runs on RHI underneath (Metal / Vulkan / D3D / GL automatic by backend).
Drop into raw RHI **only** for a specific hotspot (e.g. AA-line shader,
sounding-symbol instancing) via `QQuickWindow::beforeRendering` /
`afterRendering` hooks — without leaving the scene graph. `QQuickRhiItem`
and `QQuickFramebufferObject` are *not* used as the chart-canvas type.

### Glossary

- **`s52plib`** — the vendored library in `libs/s52plib/` implementing
  the **IHO S-52 presentation specification** for vector charts. S-57 is
  the chart-data file format; S-52 is the strict cartographic spec for
  *how to render them* (symbol catalogue, color tables, line styles, text
  placement, depth-area shading, …). It's a domain-specific marine
  cartography engine — Qt has no equivalent and no off-the-shelf GIS
  engine (Qt Location, MapLibre, …) renders S-52. The migration keeps
  using `s52plib`; only its *output stage* changes from "draw via
  OpenGL or wxDC" to "emit Qt scene-graph nodes" (P2.8).
- **`ocpnDC`** — OpenCPN's drawing abstraction (`gui/src/ocpndc.cpp`)
  wrapping either a `wxDC` (CPU path) or direct OpenGL calls (GPU path).
  P2.6 ports it; plugin compat is deferred.
- **RHI** — Qt's Rendering Hardware Interface; Qt 6's cross-backend
  graphics abstraction underneath the scene graph (Metal / Vulkan / D3D /
  GL chosen at runtime per platform).

### Phase 1 deliverables that change Phase 2 scope

| Phase 1 delivery | Phase 2 impact |
|---|---|
| `AisDecoder` / `comm_bridge` / many model classes are `QObject` with signals | Layer subscriptions are one-line `connect(...)`; no event-table porting |
| Model uses `QColor`/`QPen`/`QBrush`/`QImage`/`QFont` (P1.14) | Renderer consumes Qt types directly; no wxBitmap→QImage step inside Layers |
| `QImage` everywhere | `QQuickWindow::createTextureFromImage(QImage)` → `QSGTexture` is the texture pipeline |
| `OcpnConfig` (`QSettings`) | Per-layer persistence is `cfg->value(key, def)` — trivial, no new infrastructure |
| `QThread`/`QTimer`/`QMutex` everywhere | Chart-loading workers already use `QueuedConnection`; integrates with scene-graph update phase naturally |
| `model/wx_qt_ui_types.h` bridge | Already handles `wxBitmap`⇄`QImage` for the plugin-rendering ABI boundary |
| `QJsonDocument` | S52 lookup files / layer config / chart prefs use Qt JSON |

### Revised task list

- [—] **P2.0** Image-diff regression harness — **dropped by decision** (see
      P0.7): intentional style divergence from the wx renderer makes pixel
      diffs all false positives; visual/manual verification is the method.
      In practice the Phase-2 ports shipped without it (the 2026-05-30 parity
      audit was code-reading + visual checks).
- [x] **P2.1** Chart canvas `QQuickItem` subclass with the two top
      `QSGTransformNode`s — `m_world_anchored_root` (viewport transform —
      pan/zoom mutates one matrix, whole subtree follows) and
      `m_display_anchored_root` (identity transform). QML HUD lives in
      `Main.qml` layered above the canvas in the QML tree — no scene-graph
      node, declarative `Q_PROPERTY` bindings will hook to the (already
      QObject) Phase 1 model classes. Skeleton ships with two placeholder
      rects (red world-anchored slowly rotating to demonstrate the
      transform path; fixed blue display-anchored) — removed when Layer /
      LayerCompositor populate the subtrees.
- [x] **P2.2** `Layer` abstraction (`gui/qt/layer.h`). `QObject` base
      with `Anchor` enum (`WorldAnchored`/`DisplayAnchored`), `id()`/
      `name()`/`anchor()`/`updateSubtree(QSGNode* old, QQuickWindow*)`,
      and `Q_PROPERTY`s for `visible`/`zOrder`/`opacity` + their change
      signals. `owner` tag for UI/debug. `dirty()` signal flows from
      setters and Layer subclasses; the compositor connects.
- [x] **P2.3** `LayerCompositor` (`gui/qt/layer_compositor.{h,cpp}`).
      Owns the registered `Layer*`s; `syncToScene(world_root,
      display_root, window)` runs per frame: for each anchor, collect
      visible Layers, sort by `zOrder`, update each dirty Layer's
      subtree, detach the root's current children, re-append in order
      (wrapped in `QSGOpacityNode` where opacity < 1). Re-parenting
      hygiene: explicit detach from previous parent before re-attach
      so `QSGNode`'s no-double-parenting invariant holds. `changed()`
      signal aggregates per-Layer dirty + property changes and drives
      `QQuickItem::update()`. Two demo Layers
      (`DisplayRectLayer` + `WorldRotatingRectLayer` in
      `gui/qt/demo_layers.{h,cpp}`) replace the scaffold's inline
      rects and validate the wiring end-to-end.
- [x] **P2.4** Materials catalog — enumerate the ~6 current GL shader
      programs, map each to a built-in `QSGMaterial` where possible,
      identify the ones that genuinely need a custom `QSGMaterialShader`
      (`QShader` compiled from GLSL via `qsb`). Expected residual: ~2–3
      custom shaders (pattern fills, AA-line caps).
- [x] **P2.5** Texture pipeline — `gl_tex_cache`/`gl_texture_mgr` already
      run on Qt threads (P1.11) and produce `QImage` (P1.14). Wrap with
      `QQuickWindow::createTextureFromImage(QImage,
      QQuickWindow::TextureCanUseAtlas)` to produce `QSGTexture`s. Keep
      LZ-compressed on-disk format; decompress to `QImage` at load.
- [x] **P2.6** Reimplement `ocpnDC` primitives (core uses only — the
      plugin-API surface and existing wx-plugin compat is deferred to
      Phase 4 alongside the new plugin host).
      Non-GL path → `QPainter` (now HW-accelerated via the RHI backend).
      GL path → `QSGGeometryNode` with built-in materials.
- [~] **P2.7** Raster chart (KAP/BSB) — **v1 landed (2026-06-10),
      un-parked:** pure-Qt KAP reader (header parse, BSB RLE decode,
      least-squares linear-Mercator georeferencing with residual-based
      rejection of skewed/other projections), RasterChartLayer textured
      quad, full pipeline routing (scan extents → off-thread decode →
      quilt with tier z-ordering under same-tier vectors). Remaining:
      rendering verification with a real KAP file (none in the repo),
      then skew/polyconic support + day/dusk/night palette switching if
      wanted.
- [x] **P2.8** Vector-chart pipeline through `libs/s52plib`. **DONE** for
      the major S-52 feature classes (areas, lines, text, symbols)
      rendering from a real NOAA ENC cell through a scene-graph emit path.
      The plan changed in flight: rather than a bitmap fallback (old
      P2.8b) we went straight to a world-coordinate geometry emit, and
      added real-ENC loading via the OGR S-57 driver. Sub-phases:
  - [x] **P2.8.0** `libs/s52plib` standalone-link refactor — the library
        referenced symbols defined in `gui/src/`; brought them in so it
        links into `opencpn-qt` with no `gui/src/` dep. *No functional
        change to legacy `OpenCPN`.*
    - [x] **P2.8.0a** `vGetLengthOfNormal` (+ the vector2D helpers)
          relocated from `model` to `libs/geoprim` (fixed a layering
          inversion).
    - [x] **P2.8.0b** `S57Obj` implementation moved
          `gui/src/s57obj.cpp` → `libs/s52plib/src/s57obj.cpp`; dead
          includes dropped.
    - [x] **P2.8.0c** `render_canvas_parms` ctor/dtor →
          `libs/s52plib/src/render_canvas_parms.cpp`.
    - [x] **P2.8.0d** Host callbacks replaced with injection hooks:
          `SetGlobalColorResolver` / `SetFontColourResolver` /
          `SetFontFactory` / `SetChartScaleFactorResolver`; the s57data
          dir is derived from the PLib path. Also inverted the
          `model`↔`s52plib` dependency (s52plib now depends on model;
          `ColorScheme` enum extracted to `model/color_scheme.h`).
    - [x] **P2.8.0e** Verified both targets build/link. *(Legacy
          `OpenCPN` is slated for retirement, so future s52plib changes
          may break it without further note.)*
  - [x] **P2.8a** `s52_engine.{h,cpp}` wired into `opencpn-qt`; loads
        `S52RAZDS.RLE` + `chartsymbols.xml`, reports status in the HUD.
  - [x] **P2.8c** Scene-graph emit (no bitmap). `s52plib` gains a third
        output target beside `RenderObjectToDC`/`GL`: `RenderAreaToSG` /
        `RenderLineToSG` / `RenderTextToSG` / `RenderPointSymbolToSG`
        resolve S-52 symbology (incl. the conditional-symbology
        procedures) and append **world-coordinate** primitives to a
        Qt-idiomatic `s52sg::Buffer` (`QList<QPointF>` + `QColor` +
        `QImage`). `gui/qt` turns it into `QSGGeometryNode`s (fills/lines,
        triangle fans expanded for the Metal RHI) + billboarded
        `QSGImageNode`s (symbols/text). The Qt scene-graph transform does
        world→screen on the GPU — vector-sharp, no per-frame re-decode.
        Text uses a **system font** via `QPainter`/`QFont` (not the
        proprietary `TexFont`/`DepthFont`).
  - [x] **P2.8d** Real ENC load. `S52Engine::loadEncCell` drives the
        standalone OGR S-57 driver (`libs/s57-charts`) directly —
        `OGRS57DataSource` + `S57Reader::ReadNextFeature` (the
        `OGRS57Layer::GetNextFeature` filter is disabled in the vendored
        driver) — builds `S57Obj`s with copied attributes, looks up the
        LUP, and emits via the P2.8c path. No `Osenc`/SENC, no
        `s57chart.h`. Areas tessellate via `PolyTessGeo(OGRPolygon)`;
        lines/points use the OGR-assembled lon/lat geometry directly.
  - [x] **P2.8e** Point features: raster symbols (buoys/beacons from the
        S-52 atlas, billboarded) + soundings/labels (system font). SCAMIN
        + screen-density decluttering on zoom.
  - [x] **P2.8 follow-ups** — mostly done:
    - [x] Wide S-52 line pens: parallel 1px strips (Qt RHI caps width 1)
          + 4x MSAA; DPI-aware physical widths (0.32mm pen unit).
    - [x] Shallowest-sounding-per-cell density declutter.
    - [x] Real per-monitor DPI (logicalDotsPerInch) for the 1:N scale
          denominator + line/pattern sizing.
    - [x] AP area-pattern fills (raster patterns tiled screen-fixed via
          QSGTextureMaterial Repeat; UVs rebuilt on zoom).
    - [x] LC complex lines — **fallback only**: plain line in the LC
          colour. True symbol-along-line needs the vector path.
    - [x] Multi-cell loading: merge adjacent ENC cells into one surface
          (S52Engine::loadEncCells; ChartCanvas scans a directory).
    - [ ] **Still deferred:** vector (HPGL/SVG) symbols + true LC symbol
          patterns (need an HPGL→geometry translator; the S-52 vector
          symbols already exist in chartsymbols.xml — no external assets);
          GPU line shader (cosmeticStroke-style) for top-tier fidelity;
          async/background chart load + chart-DB (load is synchronous on
          the main thread — fine for a handful of cells).
- [x] **P2.9** Expose S52 display categories (Base / Standard / Other /
      Mariner) and viewing groups as chart sub-layers in the same
      compositor — surfacing what `s52plib` already tracks.
- [x] **P2.10** Per-layer `visible`/`zOrder`/`opacity` persistence via
      `OcpnConfig` (P1.9 — already `QSettings`-backed) — single
      `LayerCompositor::SaveState()/LoadState()` pair, ~30 LOC.
- [x] **P2.11** AIS / route / track / waypoint Layers — reactive `QObject`
      Layer subclasses that `connect(...)` to `g_pAIS->info_update`,
      `g_pRouteMan` signals, etc. (all already QObjects after P1.11).
      Subtree rebuilds on signal. *Originally part of Phase 3 (P3.3); the
      QObject-emitting model means these are cheaper to do alongside the
      scene-graph work and validate the LayerCompositor with non-trivial
      reactive layers before s52plib lands.*
- [x] **P2.12** Performance profiling vs the current GL path; close gaps.
      `QSG_VISUALIZE=overdraw|batches|changes` for free Qt SG profiling.
- [ ] **P2.13** *Optional* — drop to raw RHI via `beforeRendering`/
      `afterRendering` for any hotspot that needs it (sounding-symbol
      instancing, AA-line shader). Only if P2.12 finds genuine deficits.

### Renderer parity gaps vs wx — audit 2026-05-30

A multi-agent audit (Qt vs the legacy wx S-52 renderer) read the actual
`gui/qt`, `libs/s52plib/src` and `gui/src` code; every finding below was
adversarially re-verified. **Verdict: not yet full parity, but closer than the
older docs implied — and several suspected gaps were proven already-done.**

**Confirmed at parity / NOT gaps** (verified — do not raise as tasks): the
composite quilt vs wx's reference-scale gate is a *deliberate* divergence
(QT_QUILT_VS_WX §8/§10) that fixes the wx "lost soundings on zoom" symptom and
resolves the Santa-Cruz empty box via the coarser **underlay**; conditional-
symbology **recolour** (UDWHAZ03 / SNDFRM02 / **DEPCNT02**) *is* applied on the
SG path; **day/dusk/night** colour-scheme switching *does* rebuild the scene
with the swapped palette (`reloadResidentCells`); the `display/overzoomFactor`
*is* consumed by `updateVisibleCells`; and SCAMIN *is* honoured for SY symbols,
TX/TE labels, LC complex lines and soundings. The genuine remaining gaps:

- [x] **P2.14** SCAMIN for **AC solid fills, LS simple lines, AP pattern
      fills** (the `Prim` / `PatternFill` families). **Done 2026-05-30.**
      Emit side (`libs/s52plib/src`): added a `scamin` field to `s52sg::Prim`
      (`s52_sg.h`); `RenderToSGAC` now sets it from `obj->Scamin`, `RenderToSGLS`
      takes a `scamin` param threaded from `RenderLineToSG` (and its LC-fallback
      dashed `Prim`), and all three (AC/LS/AP) gate the value on the global
      `m_bUseSCAMIN` toggle — when Use-SCAMIN is off they emit the "unset"
      sentinel so everything shows (re-decode on toggle via `applyChartConfig`
      → `reloadResidentCells`). Consumer side (`s52_vector_chart_provider`):
      `renderChart` wraps **only** fills/lines/pattern-fills that carry a *real*
      SCAMIN in a `QSGOpacityNode` (tracked in `m_scamin_nodes`); un-SCAMIN'd
      fills append directly and always draw (so an area fill never vanishes from
      the composite underlay — no empty-box regression). New
      `updateScaminNodes(chart_scale_n)` runs in the per-frame `updateBillboards`
      pass (scale-gated, like the LC/billboard cull) and hides a node once
      `chart_scale_n > scamin`. Build: `opencpn-qt` links clean, 0 errors / 0
      warnings. *(was: severity high — most visible overview defect)*
- [x] **P2.15** Emit **area boundary lines** (RUL_SIM_LN / RUL_COM_LN).
      **Done 2026-05-30, both paths.** `RenderAreaToSG` emits only the AC/AP
      fill; the area's S-52 boundary line rules (depth-area edges, DRGARE/RESARE
      borders, ...) were dropped, so each area is fed to `RenderLineToSG` with
      its own `rz` (priority-sorted, so the border draws over the fill —
      mirrors wx's second `RenderObjectToGL` pass).
      - **OSENC / o-charts** (`decodeOsenc` GEO_AREA branch): walks the area's
        boundary edge-triples (the same `m_lsindex_array` the LNDARE
        coast-shade uses).
      - **OGR / .000 (NOAA ENC)** (`EmitAreaPoly`): the OGR path has no
        `m_lsindex` edge list, so the boundary comes from the `OGRPolygon`
        rings — `getExteriorRing` + `getInteriorRing(k)` (geographic lon/lat)
        → `RenderLineToSG`. Verified rendering at Santa Barbara US5.
- [x] **P2.16** Wire the **built-but-inert chart-dialog vector options** to the
      renderer + **de-duplicate the dialog vs wx**. **Mostly done 2026-05-30.**
      First pass set the s52plib flags via `applyDisplaySettings` but they had
      **no visible effect** — the *scene-graph emit path never consulted them*
      (the legacy gates live in `RenderText`/`ObjectRenderCheckCat`, not in the
      CS procedures). Fixed by adding the gates to the SG emit:
      - **Working now** (6): `chartInfoObjects`→ skip `M_*` objects in the
        `s52_engine` per-object loop (`m_bShowMeta`); `buoyLightLabels`→
        `RenderTextToSG` suppresses TX/TE on `BOY*`/`BCN*` when
        `!m_bShowAtonText`; `lightDescriptions`→ suppresses text on `LIGHTS`
        when `!m_bShowLdisText`; `importantTextOnly`→ `EmitTextC` skips
        `text->dis >= 20`; `nationalText`→ already honoured inside
        `S52_PL_parseTX` (NOBJNM↔OBJNAM), which the SG path calls;
        `extendedLightSectors`→ `emitCARC` shortens the sector legs to a stub
        when off. All gated on a live re-decode (`reloadResidentCells`).
      - **Still deferred** (2): `declutterText` — the Qt provider *always*
        runs label-overlap declutter (wx defaults it OFF), so the toggle needs
        a consumer-side gate on the occupancy grid in
        `S52VectorChartProvider::updateBillboards` (per-frame, not decode-time);
        `superScamin` — needs the legacy `ObjectRenderCheckCat` SuperScamin
        synthesis (`chart_scale × 2/4` with class exemptions) ported into the
        `s52_engine` object loop. Both default OFF in wx, so the common case
        already matches. Tracked as **P2.23**.
      - **Dialog de-dup (wx-match):** the Qt panel had a Qt-invented "Detail
        (live)" section (Soundings / **Text labels** / **Lights** / **Buoys &
        beacons**) that duplicated/conflicted with the wx-style cartography
        flags — e.g. the live "Text labels" master hid buoy names that the
        "Buoy/light labels" flag was *also* meant to control, and wx has no
        Lights/Buoys *symbol* toggles in its Vector Chart Display panel (symbol
        visibility follows the Display Category). Restructured `Main.qml` to one
        flat list matching wx; removed the invented Text/Lights/Buoys live
        toggles (kept Soundings live); the stale `display/{text,lights,buoys}`
        config keys are no longer loaded (`chart_canvas.cpp`); the Qt-specific
        over-zoom + un-SCAMIN'd-detail spinboxes moved under a "Quilt
        (Qt-specific)" sub-heading. Build: `opencpn-qt` links clean, 0 errors.
      The 3 *new* display controls once bundled here are split to **P2.20**
      (Show Grid + Show Depth Units — new render paths; Smooth Pan/Zoom N/A).
      ~~Remaining for [x]: P2.23 (declutter + super-SCAMIN)~~ — both landed
      (P2.23a 2026-05-31, P2.23b 2026-05-31); closed in the 2026-06-12
      tracker audit. *(was: severity high)*
- [x] **P2.17** **M_COVR polygon render clip** — **implemented
      (2026-06-10):** the per-cell `QSGClipNode` geometry is now the
      tessellated M_COVR coverage union (libtess2, NONZERO winding —
      concave rings + holes honoured), with the bbox quad as the fallback
      for coverage-less cells. wx `ActiveRegion` parity for the
      edge-sliver symptom; the `− union(finer coverage)` subtraction
      remains covered by the finest-owner suppression + draw order.
      Visual confirmation of known sliver sites folds into the P2.25
      sweep.
- [x] **P2.18** **Overscale / overzoom indication.** Was already implemented
      but left unmarked (audit 2026-06-10): the provider draws a per-cell
      vertical overscale hatch when the display is zoomed finer than the
      cell's native scale × threshold (`rebuildOverscaleHatch`,
      `s52_vector_chart_provider.cpp`; threshold = max(4, overzoom k) from
      Options), and the HUD shows "⚠ OVERSCALE ×N" via
      `ChartCanvas.overscaleFactor`.
- [~] **P2.19** **CM93 / CM93COMP** vector chart support. **Step (a) in
      progress (2026-06-10):** `cm93_dictionary.{h,cpp}` (pure-Qt
      dictionary loader) AND `cm93_cell_reader.{h,cpp}` (descramble +
      header/table readers + Ingest, verbatim extraction with mechanical
      renames) compile in opencpn-qt, and **`cm93_transcoder.{h,cpp}`** now carries
      the FULL decode chain — **step (a) COMPLETE (2026-06-10)**:
      `buildGeom` (geometry reconstruction), `transformPoint`, the
      packed-attribute walker, and `createS57Obj` (the ~650-line semantic
      core: class substitutions, attribute transcoding incl. COLMAR/QUASOU
      fixups, ATON label optimization, per-geometry S57Obj assembly with
      deferred tessellation, WGS84 offsets via a lightweight `Cm93Covr`
      capture replacing the wx covr_set; user offsets 0 until the offset
      dialog ports). Every stage compiles in opencpn-qt. **Step (b) extent
      scanner DONE (2026-06-10):** `cm93_scanner.{h,cpp}` walks a set into
      the same `CellExtent` records the S-57 scan emits (header-only bbox
      read, per-tier native scale + band; isCm93Root detection). **Step (c)
      cell→buffer loader DONE (2026-06-10):** `cm93_loader.{h,cpp}` —
      ingest → transcode → the same LUP/rules/RenderToSG emit per
      primitive (areas via deferred PolyTessGeo, lines via the cell
      transform, points, sounding clusters as depth labels). **Step (d)
      pipeline wiring DONE (2026-06-10):** a configured chart directory
      that is a CM93 root routes straight through — reloadCharts passes
      the root, the worker's scan expands it via `Cm93Scanner`,
      `kindOf()` recognises cell filenames, and `loadCell` decodes via a
      per-root dictionary cache + `S52Engine::loadCm93Cell`; the buffer
      flows into the existing provider/quilt unchanged. **Remaining:**
      RENDERING VERIFICATION against a real CM93 set (none in the repo —
      add the set's root folder under Options > Charts > Chart Files and
      look; coordinate-transform bugs would show as misplaced geometry),
      **Detail slider live (step e, 2026-06-10):** Options > Vector's CM93
      detail biases tier eligibility (3^(detail/2.5), wx
      g_cm93_zoom_factor parity); offset UI still pends verification.
      v1 divergences tracked: base cells only (no update-cell merging),
      user offsets 0, no queryable-feature capture, .xz cells skipped.
      **Implementation plan (scoped 2026-06-10):** CM93 decode already
      produces s52plib-compatible `S57Obj`s (`cm93chart::CreateS57Obj`,
      `gui/src/cm93.cpp:3163` — ~650 lines of attribute/class transcoding,
      the genuinely hard part to extract; geometry via `BuildGeom`,
      :2526). So the SG emit needs NOTHING new — the port is: (a) extract
      the binary reader + transcoder into a wx-light layer (drop the
      `wxFileOutputStream` staging + ViewPort-typed signatures);
      (b) a `Cm93ExtentScanner` (cell-header + M_COVR read → `CellExtent`,
      easy); (c) a worker decode case piping the S57Objs through
      `RenderAreaToSG` etc. exactly like the .000 path; (d) per-scale
      selection (A–G tiers, `GetCMScaleFromVP` logic) mapped onto the Qt
      quilt's tiering; (e) the offset dialog + detail slider QML
      (`ChartConfig` already has the properties). Effort: decode
      extraction HIGH, everything else LOW/MEDIUM. Original note:
      Qt's `S52Engine`
      loads only `.000` / `.S57` / `.oesu` / `.oesenc`; wx renders CM93
      worldwide vector (`cm93chart` / `cm93compchart`, incl. next-smaller-cell
      dashed outlines). The Qt CM93 detail / offset controls already exist but
      drive nothing. Add a CM93 decode path feeding the s52plib SG emit.
      *(severity: low — large effort)*
- [x] **P2.20** **Show Grid + Show Depth Units** display controls (split from
      P2.16). **Show Grid DONE (2026-06-10):** `GridLayer` — labelled
      meridians/parallels at a scale-picked interval, world-anchored,
      rebuilt per view change, gated by `DisplayConfig.showGrid`
      (Options > Display). **Show Depth Units DONE (2026-06-10):** a HUD
      pill beside the scale bar ("Depths: m/ft/fm"), gated by the persisted
      `DisplayConfig.showDepthUnits` (default on, wx parity). Original note:
      Show Depth Units is an on-chart legend showing the sounding unit
      (m / ft / fm) the active cell uses. Add the `DisplayConfig`/`ChartConfig`
      property + QML control alongside the render path. **Smooth Pan/Zoom is
      intentionally NOT added** — it is `[—]` N/A under the Qt scene graph
      (pan/zoom is GPU-smooth and zoom already tracks the cursor; see the
      Display → General notes in P3.6). *(severity: low)*
- [x] **P2.23** The last 2 chart-dialog flags (split from P2.16; both
      default-OFF in wx, so the common case already matched). **Done 2026-05-30.**
      - [x] **P2.23a De-cluttered text** (`m_bDeClutterText`). The Qt provider
        *always* ran the label bounding-box declutter in `updateBillboards`;
        gated it on a new `S52VectorChartProvider::setDeclutter` flag (default
        false = wx default, all labels shown). `ChartCanvas::applyDisplaySettings`
        pushes `ChartConfig.declutterText()`; toggling re-runs the per-frame
        cull (no re-decode).
      - [x] **P2.23b Super-SCAMIN** (`m_bUseSUPER_SCAMIN`). Implemented as a
        decode-time **SCAMIN synthesis**: a free `ApplySuperScamin(obj,
        nativeScale, useSuper)` helper in `s52_engine` writes `native × 2` onto
        `obj->Scamin` for objects with no real SCAMIN (wx `>9e6` = unset), with
        wx's LNDARE/DEPARE/SWPARE/RECTRK/TSS/TSEZNE/DRGARE/COALNE exemptions, so
        the existing **P2.14 per-frame cull** enforces it — no new per-frame
        machinery, no SuperScamin field. (Net wx behaviour for the undefined
        default is `×2`; we fold the `×4`-then-`×2` cascade into the single `×2`
        result. LNDARE fully exempt vs wx's RUL_ARE_CO-only — safe-direction.)
        Called per-object on **both paths**: OGR/.000 uses
        `reader->GetCSCL()` (also stored on `ctx->chart_scale`); OSENC threads
        `cell.nativeScale` through new `native_scale` params on `decodeOsenc` /
        `loadOsencCell`, supplied by `ChartWorker::loadCell`. Build clean; runs.
        *(was: severity low)*
- [x] **P2.24** OGR/.000 (NOAA ENC) area boundary lines — **done under P2.15**
      (the OGR half: `EmitAreaPoly` now emits the `OGRPolygon` exterior +
      interior rings through `RenderLineToSG`). **SCAMIN-decode concern
      RETRACTED 2026-05-30:** a first instrumented pass suggested the OGR driver
      didn't surface SCAMIN, but a corrected probe in `CopyFeatureAttributes`
      showed the opposite — the vendored OGR S-57 driver **does** emit a
      `SCAMIN` integer field (`name=SCAMIN type=OFTInteger set=1`), and the
      existing `AddIntegerAttribute` path already routes it onto `obj->Scamin`
      (`s57obj.cpp:196`). So per-object SCAMIN **is** decoded on NOAA charts and
      P2.14's cull is **not** inert there (the earlier "inert on NOAA" note was
      a measurement error — the first probe's `%50` counter never reached its
      print threshold in the small harbour cell, not an absence of SCAMIN).
      Nothing to do here; closed.

- [~] **P2.25** **Chart-rendering defect sweep (specific cells).** Investigate
      **2026-06-12 (user repros, both FIXED):** (a) blank rectangle in
      the Needles quilt at 1:50k — oeSENC cells carried NO M_COVR in the
      catalog (OSENC header scan ignored CELL_COVR_RECORD), so quilt
      selection over-claimed each cell's bbox and suppressed the coarser
      chart beneath; the scanner now parses COVR records (and the P2.17
      clip uses them). Likely the same root cause as the parked "Poole
      missing areas" note. (b) Guernsey only overview shapes at 1:167k —
      the underzoom admit was tied to the OVERZOOM k; now its own
      kUnderzoomAdmit=4.0 (wx Quilt parity, SCAMIN thins the detail).
      Post-fix verification at four user repros (Needles 1:50k, Guernsey
      1:167k + St Peter Port 1:4.7k, Hamble 1:12k): correct cells
      selected, full detail, no spurious overscale. NOTE the coverage
      fix forces a ONE-TIME rescan of every o-charts cell (FIFO decrypt
      each, ~5 min for 826 cells) — mid-scan the quilt is sparse; the
      back-filled coverage persists in the catalog DB so it never
      recurs. The afternoon's "charts not brought in" reports were this
      transitional state, repeatedly restarted by short test launches.
      and fix concrete rendering issues on the user's local UK South-Coast
      charts:
      - **Yarmouth (Isle of Wight / Solent)** — **DONE 2026-06-06.** The
        symptoms (overlapping/duplicate text, light-sector arcs sliced at the
        cell edge) were all the composite quilt rendering each overlapping cell's
        full content clipped to its own bbox, with no cross-cell de-dup. Root
        cause + fix verified against the wx pipeline (`gui/src/quilt.cpp`:
        `ActiveRegion = quilt_region - m_covered_region`; no cross-cell feature
        ID exists — `S57Obj` has no LNAM/FOID, only a cell-local `Index` — so wx
        uses spatial region-subtraction, not identity de-dup). Three fixes in
        `gui/qt/s52_vector_chart_provider.{h,cpp}` + `gui/qt/chart_canvas.{h,cpp}`:
        (1) **billboards unclipped** — point annotations (symbols, text labels,
        CARC light-sector arcs) now append to the unclipped `root`, not the
        per-cell bbox `QSGClipNode`, so an arc/name sweeping past the cell edge
        is never sliced (fills/lines keep the bbox clip). (2) **Finest-owner
        suppression** — `ChartCanvas::updateFinerCoverage()` hands each cell the
        coverage of every finer overlapping cell (`setFinerCoverage`); a
        billboard whose anchor a finer cell owns is culled in
        `recomputeDeclutter` (`coveredByFiner`) — the scene-graph analogue of
        `m_covered_region.Subtract`. Cells with no captured M_COVR (`cov=0`,
        e.g. these o-charts cells) fall back to the finer cell's bbox. (3)
        **Within-cell same-name de-dup** — a label whose text + screen rect
        duplicates one already placed in the SAME cell is dropped (always on;
        narrower than the `m_declutter` toggle). Net: harbour overlap gone; arcs
        complete; the remaining spread-out "Isle of Wight"/"River Yar" labels
        are genuine per-polygon `OBJNAM`s in the o-charts data (released wx
        renders them too, *worse* — heavily stacked), so this is **cleaner than
        wx** and accepted as done.
      - **Poole (Dorset)** — **missing areas** (chart area not rendering /
        coverage gap). STILL OPEN.
      Method: reproduce each, then isolate the cause — quilt composite vs
      per-cell bounding-box clip (cf. **P2.17** M_COVR), area emit /
      tessellation, or the decode path (OGR `.000` vs OSENC) — and fix.
      Likely overlaps P2.17. *(severity: medium — user-visible on home waters)*

> **P2.7 (re-confirmed open, high):** `RasterChartProvider` is still a
> placeholder with no decoder, so Qt cannot render **raster KAP/BSB** charts at
> all — a hard parity gap for raster-only regions. Extend its scope to cover
> **MBTiles** raster/overlay and raster **de-skew** reprojection (wx:
> `ChartKAP` / `ChartGEO` / `ChartMBTiles`). See the P2.7 entry above.
> **DEPRIORITIZED 2026-05-31 (user decision):** raster support is parked — do
> not pursue it as a next step. Vector (NOAA ENC `.000` + native o-charts/OSENC)
> covers current use.

> **Doc upkeep:** `Docs/QT_QUILT_VS_WX.md` carried three contradictory quilt
> models (§5 reference-gate plan, §8 composite-with-M_COVR-clip, §9 no-clip
> underlay). Reconciled against verified code on 2026-05-30 via a new §10
> ("Current implementation — verified ground truth"); the true model is
> **composite + coarser underlay + per-cell bounding-box clip, no reference
> gate, no M_COVR clip**. §4/§5/§6 and the clip claims in §8/§9 are now flagged
> historical. No task needed.

### Future / post-Phase-2 follow-ups (capture, not scheduled)

- **Vector S-52 symbols (research → P2.x or P4.x)** — the shipped
  `data/s57data/rastersymbols-{day,dusk,dark}.png` symbol sheets are
  pre-rendered PNG atlases (one per color scheme). Vector replacements
  (SVG, or a path-based catalogue rendered through `QQuickShape`/
  `QPainterPath`/`QSGGeometryNode`) would give clean scaling at any
  DPI, recolouring without three separate atlases, and a smaller
  on-disk footprint. Drop-in initially via raster; revisit after the
  scene-graph port is working. User intends to research before
  scheduling.
- **Chart-colour editing UI (Phase 3)** — add an "S-52 colour-table
  editor" pane to the ENC settings, letting users override individual
  S-52 colour tokens (`DEPVS`, `LANDA`, `CHBLK`, …) per scheme. The
  three DAY / DUSK / NIGHT tables are loaded from
  `data/s57data/chartsymbols.xml` as plain RGB triples — a user-
  overlay layer that persists overrides via `OcpnConfig` (P1.9) and
  re-pushes via `s52plib::SetPLIBColorScheme(...)` would let users
  tune colours without editing the data file. Add to the Phase 3 UI
  scope once the new chart canvas is running and the standard ENC
  settings panel is ported.

### Deferred to Phase 4 (new Qt plugin host)

Phase 2 deliberately does **not** carry the wx-plugin rendering ABI —
the plan is to land a Qt-native plugin host first (in Phase 4) and
migrate plugins onto it then, rather than maintain a wx-compat
rendering bridge in the new renderer. Specifically deferred:

- Plugin `RenderOverlay(wxMemoryDC*)` / `RenderGLOverlay(wxGLContext*)` /
  `RenderOverlayMultiCanvas(...)` ABI bridging.
- Chart plugin `PlugInChartBase::RenderRegionView()` (returns `wxBitmap&`)
  bridging.
- `GetThumbnail` / `GetPlugInBitmap` `wxBitmap` returns.

Implication: during Phase 2 / 3 the wx GUI keeps running the wx plugin
path against the *old* GL renderer; the new Qt renderer renders core
content only. The Qt plugin host (Phase 4) lets each plugin contribute
its own scene-graph subtree (registered at the World or Display tier)
or its own QML HUD items — same `Layer` abstraction core uses, no wx
bridging.

The **plugin-management GUI** (catalog browser, install / uninstall /
update flows, settings dialogs) is in scope for Phase 3 — it doesn't
render anything onto the chart, only manages the plugin lifecycle.

### P2.27 Rendering performance programme (added 2026-06-10)

From the end-to-end pipeline investigation (full report in the session
log). The pipeline's fundamentals are sound — pure pan is transform-only
for all NavLayer overlays, per-cell content uses opacity-based culling —
and these six items are the funded improvement ladder, in order. **User
direction: all six are wanted eventually.**

- [x] **PERF-1** Stop per-pan-frame layer rebuilds — **DONE 2026-06-10.**
      Investigation found NavLayer overlays already scale-only-dirty; the
      one real offender was GridLayer, whose cost was QPainter label
      rasterization per frame. Labels now cache by text (bounded), pan
      rebuild collapses to line quads + cached textures
      (`grid_layer.{h,cpp}`).
- [x] **PERF-2** Zoom-settle declutter tuning — **DONE 2026-06-10.** The
      S-52 declutter debounce (pattern UVs, complex-line glyph walk,
      label/sounding grids) drops 110 ms → 60 ms
      (`s52_vector_chart_provider.cpp` m_zoom_timer). If large cells
      stutter at 60 ms, consider the preview-declutter variant (SCAMIN
      only during gesture, full pass on settle).
- [x] **PERF-3** Merge same-colour chart prims into one geometry node —
      **DONE (2026-06-12).** Implemented as a zero-reorder RUN merge:
      consecutive prims sharing (tile parent, SCAMIN, colour) for fills
      — plus (width, dash) for AA lines — emit one node per run. Prims
      are already stable-sorted by S-52 priority and a run never
      crosses a key change, so draw order is bit-identical to the
      per-prim path. aa_line gained a multi-polyline makeAaLineNode
      (independent segment quads concatenate exactly; dash phase
      restarts per strip). Measured (OCPN_QT_SG_STATS=1): 30,703 fill
      prims → 2,531 nodes / 15,207 → 834 (up to 18× fewer nodes);
      lines 820 → 332.
- [x] **PERF-4** Label/sounding texture atlas — **DONE (2026-06-12).**
      Billboard texture assignment is deferred at build: images
      shelf-pack into shared 2048² atlas pages (2 px gutter vs linear
      bleed, cacheKey dedup, oversized→dedicated fallback); image
      nodes sample via setSourceRect. Declutter/counter-scale touch
      only transforms — unaffected (no re-atlas needed). Measured:
      1,339 images → 1 page / 1,276 → 2 pages (~1,300 GPU textures per
      dense cell → 1–2). Verified visually via the new
      `OCPN_QT_GRAB=<png>[:delay]` window-grab hook (passive,
      window-only — the headless verify loop's eyes). **Regression
      found by the user same-day and FIXED:** the atlas blit used the
      drawImage POINT overload, which honours the labels'
      devicePixelRatio — every billboarded label rendered at HALF size
      (gotcha: explicit target rect required when packing DPR-tagged
      images by device-pixel coordinates).
- [x] **PERF-5** ~~Dirty-flag split~~ — **CLOSED OBSOLETE by
      measurement (2026-06-12).** With PERF-3/4 landed, the new
      OCPN_QT_PAN_TEST exerciser + QSG_RENDER_TIMING shows a steady pan
      over the dense Solent quilt locked at 60 fps with sync=0 ms,
      preprocess=0 ms, polish=0 ms — no per-pan layer cost remains to
      split.
- [ ] **PERF-6** Pan-into-new-area hitch — **RE-SCOPED by measurement
      (2026-06-12).** The hitch is real (~1.06 s frames as cells join
      mid-pan) but NOT where assumed: the provider build measures only
      15–29 ms (atlas 1–4 ms, declutter ~1 ms; per-phase timing under
      OCPN_QT_SG_STATS) and the stall is render=1064 ms on the RENDER
      thread inside the RHI — batch rebuild / texture upload / Metal
      pipeline compilation as the new cell's nodes enter the scene.
      Chunking OUR build (the old plan) would not touch it.
      **Investigation log (2026-06-12):** QSG_RENDERER_DEBUG exposed the
      world basemap as single 1.86M + 1.18M-vertex nodes re-copied on
      every 'rebuild: full' → the basemap now tiles into a 24x12 grid
      and submits only view-intersecting tiles (landed; seamless at all
      zooms; big scene-size win). The hitch PERSISTS though
      (render=1021 ms, preprocess=0, updates=0, no node >10k verts
      left): the second is inside the RHI batch build/record phase with
      ~15k small elements. **RESOLVED in substance (2026-06-12):** a
      `sample` profile pinned it — QSGBatchRenderer::prepareAlphaBatches
      scans translucent elements quasi-quadratically calling material
      compare(), and AaLineMaterial::compare()'s four un-inlined
      QColor::rgba() calls were ~half the render-thread time. Fixed:
      compare() reads a cached POD rgbaKey, and chart LS lines skip the
      cull tiles so the PERF-3 run-merge collapses the alpha-element
      count (thin lines cost ~nothing off-screen). Measured: worst pan
      frame 1047 ms → ~217 ms typical / 480 ms once; steady pan still
      60 fps; visuals identical. The remaining ~0.2 s tail is genuine
      batch building — chase only if it still registers in real use.
      Exerciser: OCPN_QT_PAN_TEST=<s>.
- [ ] **PERF-0** (cross-cutting) Instrumentation before each step:
      per-layer updateSubtree timing, declutter phase timing,
      QSG_RENDER_TIMING, texture-cache hit rate — measure, don't assume.


## Phase 3 — QtQuick UI shell  (est. 16–24 wks)

- [x] **P3.1** App shell in QML — main window, chart view embedding the Phase 2
      `QQuickItem`. Delivered incrementally (frameless `Main.qml` shell +
      `ChartCanvas` + `macos_titlebar.mm`); marked done 2026-06-10.
- [x] **P3.2** `QObject` view-models exposing core data via `Q_PROPERTY` to
      QML. Delivered: `nav_state_view_model`, `route_list_view_model`,
      `ais_selection_view_model`, `connections_view_model`,
      `object_query_view_model`, `tide_graph_view_model`,
      `nmea_monitor_model`, `route_follower` + the `*Config` QML singletons.
- [x] **P3.3** Core world-anchored Layers — own-ship, routes, tracks, AIS
      targets. Delivered via P2.11 + `ais_layer`, `own_ship_layer`,
      `route_overlay_layers`, `tide_layer`, `anchor_watch_layer`.
- [x] **P3.4** QML HUD tier — readouts bound to view-models. Delivered: status
      bar (position / SOG / COG / cursor / range-bearing / scale), alert
      banner, time bar, route-follow console (P3.16). Depth/wind readouts
      follow when their feeds are surfaced (`wind_decoder` exists).
- [x] **P3.5** Toolbar / main controls in QML (touch-friendly). **DONE
      (2026-06-10)** per the agreed 2026-06-09 spec, in the P3.17 components
      (`FloatToolbar.qml` + `MuiBar.qml`): Tides + Anchor moved to the main
      toolbar, MOB icon fixed (⚓→🛟) + wired to `dropMob()`, Follow is a
      jump+follow split button with the look-ahead flyout, the status-bar
      scale is a clickable free-form entry (`ScaleDialog`), and the Data
      Monitor slot moved to Connections (P3.22). **Print stays a stub**
      (inert button + "not yet implemented" tooltip) — explicitly acceptable
      per the P3.21 gate; full chart printing is a separate feature.
- [x] **P3.6** Settings / preferences UI in QML — **all sub-items closed
      (2026-06-10);** see the breakdown below. The only open row in the
      breakdown is the per-element-fonts USER DECISION (drop
      recommended); every built/behaviour item is done.
- [x] **P3.7** **Route / mark / track manager UI in QML. DONE (2026-06-02).**
      Left-edge `Drawer` with a Routes | Marks | Tracks tab selector; visibility
      and selection are independent throughout (per-item "eye" toggles replaced
      the old master layer switches). Routes: tiles (name + length + legs), tap
      selects + zooms, explicit Edit mode (node drag/insert/delete) behind the ⋯
      menu (+ Rename/Duplicate/Reverse/Delete). Marks (free `m_bIsolatedMark`
      RoutePoints): drop via right-click → New Mark dialog (name + comment +
      icon picker), `WaypointIconProvider` (`image://wpicon/`), Recent/Nearest
      sort. Tracks (own-vessel, dated, `#n` same-day suffix): Start/Stop/Reset +
      rename/delete, newest-first. All persist via `NavObj_dB`.
      **Schema convergence (2026-06-02):** own-vessel `trk_points` converged
      toward `ais_track` — `timestamp` is now integer epoch-ms (was ISO-8601
      TEXT) and `cog`/`sog`/`hdg` REAL columns added (`TrackPoint::m_cog/m_sog/
      m_hdg`, captured from `gCog`/`gSog`/`gHdt` in `Track::AddNewPoint`). The
      two tables stay separate (different identity/lifecycle/retention/volume).
      Existing DBs upgraded in place by a one-shot `MigrateTrkPointsSchema()`
      table-rebuild (transaction-wrapped, FK off; ISO→epoch via `strftime`);
      verified against the live `navobj.db` (schema changed, routes survived).
- [x] **P3.8** Chart selection / quilting UI — **covered (2026-06-10
      audit):** the Qt canvas always quilts (deliberate divergence — the
      composite quilt replaced wx's reference-chart selection, see the
      P2 renderer notes), the **chart bar** shows the in-view cells, and
      selection-adjacent controls live in Options > Charts (directory
      manager, chart groups, detail/over-zoom sliders). No further
      manual-selection UI is planned.
- [x] **P3.9** Dialogs (AIS target info, object query, alarms) in QML —
      **DONE (2026-06-10 audit):** AIS info card + the AIS **Target List
      window** (P3.18 tier 2), the **object query** window (stacked-feature
      stepper), and the alert surface (banner + sounds via AlertEngine /
      P3.15, anchor-watch card). All follow the native-dialog convention
      where they are dialogs.
- [~] **P3.10** i18n via Qt Linguist — **infrastructure DONE (2026-06-10):**
      `qt_add_translations` with de/fr/es/nl/it seed catalogs (841 messages
      from the existing `tr()`/`qsTr()` surface after the AppMenuBar/etc.
      lupdate re-scan), `update_translations` lupdate target, `.qm` embedded
      at `:/i18n`, `QTranslator` installed at startup from the persisted
      Options language choice (restart applies).
      **Bootstrap DONE (2026-06-13):** harvested the legacy wx gettext
      catalogs (`po/opencpn_<lang>.po`) into the 5 `.ts` files — two-tier
      match (verbatim msgid + normalized: case-/mnemonic-/trailing-punctuation-
      insensitive), written as FINISHED so lrelease compiles them.
      Result: **273** finished (de/es/fr), **265** (it), **264** (nl)
      (verified via `lconvert` round-trip + clean startup under a `de`
      locale). The ~568 still-untranslated per catalog are the new
      QtQuick-only strings (N-Up, "Move test ship here", the Qt-edition
      About box) that never existed in wx.
      Remaining: those new-string translations (community/lupdate workflow)
      and any hard-coded strings still missing `qsTr()`.
- [~] **P3.11** Remove the parallel wx build path —
      **increment 1 (2026-06-10):** OCPN_BUILD_WX_APP (default OFF) makes the
      wx app EXCLUDE_FROM_ALL and excludes the wx-era opencpn-cmd.
      **increment 2 — wx GUI DELETED (2026-06-13, gate now satisfied via
      P3.21 sign-off):** removed **gui/src (135) + gui/include (146) = 281
      files**, the entire legacy wxWidgets GUI. The OpenCPN target is kept
      DEFINED but compiles only `cmake/wx_app_retired_stub.cpp` when
      OCPN_BUILD_WX_APP=OFF (the ~50 scattered
      `target_link_libraries(${PACKAGE_NAME} ...)` + the `_opencpn` alias
      still reference it); the `OPENGL_FOUND` gl_* `target_sources` block was
      gated; the wx `add_subdirectory(plugins)` was dropped. Verified: clean
      reconfigure (generate done) + `make opencpn-qt` links + app runs/renders.
      **Remaining:**
      (a) `plugins/` is NOT yet deletable — the Qt plugins (gui/qt/plugin/)
          reuse legacy plugin *computation* sources (e.g.
          `plugins/grib_pi/src/GribRecord.cpp`); needs the Qt plugins to
          vendor their own copies first.
      (b) dead `GUI_SRC`/`GUI_HDRS` list-assembly (CMakeLists ~810–1102) now
          references deleted files — harmless (unused when OFF) but should be
          removed for clarity.
      (c) final cleanup: remove the stub `OpenCPN` target + all its scattered
          refs + the `OCPN_BUILD_WX_APP` option once (a)/(b) are done.
      **Dependency rework (2026-06-13, follow-on):** with the wx GUI gone,
      gated the build deps that only existed to link the wx target behind
      `OCPN_BUILD_WX_APP`, so the default (Qt) build no longer requires them:
      **GTK** (+ dropped `libgtk-3-dev`), **Gettext** (the wx .po/.mo build;
      Qt uses lrelease — + dropped `gettext`), Pango, X11, BZip2, TinyXML,
      ZSTD-on-mac, and the old-wx wxSVG font stack (Freetype/Fontconfig/PNG/
      Pixman — already inert on wx ≥ 3.1.6, matching the "no per-element
      fonts" decision). **KEPT** (genuine Qt deps): **ZLIB** (Qt grib plugin
      links `ZLIB::ZLIB` — an over-gate here broke the grib build, caught +
      fixed) and **GLEW** (`libs/s52plib` uses it). `libmpg123`/`libmp3lame`/
      `libexif`/`libzstd`/`libusb` left in place — genuine deps of kept libs
      (`o_sound`, `garmin`, `libarchive`). All verified green on macOS +
      Linux + arm64 CI.
- [ ] **P3.12** Remove `QT_NO_KEYWORDS`; restore the plain `signals` /
      `slots` / `emit` keywords. **Blocked (not by P3.11 directly):** the
      `model/` layer still links wxWidgets at its deliberate Phase-1
      boundaries and hosts the Phase-1 QObject classes (`observable_qt`,
      `comm_drv_*`), so wx and Qt headers still co-exist in model TUs and the
      keyword macros could still clash. Safe only once the residual model wx
      usage is gone. Introduced by P1.5a.
- [x] **P3.13** **Route-creation interaction parity (pan/zoom while routing).**
      **Drag-to-pan + line rendering done (2026-06-01); keyboard / edge-pan /
      snap pending.** Interaction model chosen: **drag = pan, click = place
      vertex, wheel = zoom** — reusing the canvas's existing 6 px click/drag
      threshold (consistent with select/insert elsewhere), rather than a special
      hold-to-place gesture (inconsistent + slower per node). In route-build
      mode `mousePressEvent` now starts a *potential pan* on left-press and
      `mouseReleaseEvent` decides: barely moved → `addRoutePoint`, else it was a
      pan (already applied live). Right-click still finishes. Route **line
      rendering** also fixed: the shared AA-line (`aa_line.cpp` + `aaline.vert`)
      pinched to invisibility at acute angles because it extruded an averaged,
      re-normalised **bisector** normal by exactly halfWidth. Rebuilt as
      **per-segment quads using each segment's own normal + square caps** (a new
      tangent+cap vertex attribute extends each segment halfWidth along its own
      direction at both ends, so consecutive segments overlap at the joints — no
      pinch). Benefits every AA-line consumer (routes/tracks/AIS vectors/own-ship
      /rings/anchor/tide arrows/trails). Route line is now 2 px graphite. Builds
      clean (qsb regenerates all backends); runs with no shader/Metal errors.
      **Keyboard nav landed via P3.20 (2026-06-10):** arrows pan, +/- zoom,
      Esc cancels the build, Enter finishes it. **Edge auto-pan +
      nearby-waypoint snap landed (2026-06-10):** 5%-band / 2%-per-200ms
      edge pan during build / measure / node-drag (wx CheckEdgePan), and
      build clicks snap to an existing mark within pick range (positional
      snap; wx's shared-RoutePoint reuse is a possible follow-up).
      Still pending: touch affordances and round caps/joins (square
      accepted for now) -- both cosmetic.
      Remaining detail (original analysis):
      Route *rendering* and the manager (P3.7) are good, but the *entry* UX is
      hard to use: **you cannot pan the chart while creating a route.** In
      `ChartCanvas::mousePressEvent` (`gui/qt/chart_canvas.cpp:1156`) route-build
      mode consumes every left-click as "add vertex" and returns early, so
      left-drag never pans; there is no edge auto-pan and no `keyPressEvent`
      override at all. (Wheel **zoom** already works — `wheelEvent` is not gated
      by route mode.)
      - **wx reference** (`gui/src/chcanv.cpp`): while `m_routeState >= 2`
        (rubber-banding) the chart stays fully navigable —
        - **Edge auto-pan**: `CheckEdgePan(x,y,dragging,5,2)` on mouse-move
          (`chcanv.cpp:8040`); cursor inside a 5%-margin edge band pans the
          viewport 2%/tick via a 200 ms one-shot `pPanTimer`, so a route extends
          past the current view without stopping.
        - Wheel **zoom** and **keyboard** pan (arrows) / zoom stay active.
        - Vertices placed on discrete **left-down** (`chcanv.cpp:8616`) with
          **nearby-waypoint reuse** (`GetNearbyWaypoint` snaps to an existing
          mark within the select radius).
        - Right-click / Esc / double-click / kill-focus **finish** the route
          (`FinishRoute`, `m_FinishRouteOnKillFocus`).
      - **Qt scope:**
        1. **Edge auto-pan** during build — port `CheckEdgePan` + a one-shot
           `QTimer` pan loop; keep the rubber-band leg world-anchored so it
           follows the pan.
        2. **Keyboard nav while building** — add a `keyPressEvent` override
           (none today): arrows pan, +/-/= zoom, **Esc cancels** the route,
           **Backspace/Delete removes the last vertex**, Enter **finishes**.
        3. *(beyond wx)* **Drag-to-pan** during build — distinguish a click
           (place vertex) from a drag (pan) by a small move threshold before
           release; gives a modern/touch feel edge-pan alone doesn't.
        4. **Nearby-waypoint reuse** — snap a placed vertex to an existing
           waypoint within the select radius (mirror `GetNearbyWaypoint`).
        5. **Touch parity** — drag-to-pan + tap-to-place + on-screen
           Finish/Cancel/Undo affordances (a route can't be built with
           right-click on touch).
      - **Files:** `gui/qt/chart_canvas.{cpp,h}` (input, edge-pan timer,
        `keyPressEvent`), `gui/qt/qml/Main.qml` (build-mode affordances),
        `NavDataProvider` route API (`addRoutePoint`/`setRouteRubberband`/
        `finishRoute` exist; add `removeLastRoutePoint`/`cancelRoute`).
        Relates to **P3.7**. *(severity: medium-high — route creation is a core
        nav workflow and is currently frustrating to use)*
- [~] **P3.14** **Tides & currents port.** Plan agreed 2026-05-31 after a
      full audit of the wx subsystem (engine `tcmgr`/`idx_entry`/
      `tc_data_source`/`tcds_*_harmonic` + vendored `libtcd`; on-chart render in
      `chcanv.cpp`; graph `tc_win.cpp`). **Key enabler:** the display-time spine
      already exists in the de-wx'd core — `gTimeSource` (a `QDateTime` in
      `model/gui_vars.cpp:105`) is what GRIB sets and the tide/current render
      reads (invalid == live/now). The engine is already mostly `std`/`time_t`/
      `QDateTime`-typed (~35 wx refs) and the math is correct. Engine is **not
      reentrant** (static epoch state + libtcd single-DB-open) → sample on one
      mutex-guarded worker thread.
      **Decisions (user, 2026-05-31):** one **unified timeline** (tides +
      currents + future GRIB all follow `gTimeSource`); the tide/current graph
      is a **docked bottom panel** (scrub moves the marker, click-curve sets the
      time); slider supports **scrub + Play** (animate flood/ebb).
      **Phases:** (A) port the engine into a `libs/tides` lib `gui/qt` links —
      de-wx (wxString→QString at the edge, `WX_DECLARE_OBJARRAY`→`QVector`,
      wxLog/tokenizer/wxConvUTF8/wxHashMap), keep the harmonic math, expose a
      mutex-guarded `TideCurrentEngine` facade; (B) Charts→Tides data-set list
      (persisted, feeds `LoadDataSources` — mirrors `ChartSourceModel`); (C)
      **`TimeController`** QML singleton wrapping `gTimeSource` (live/scrub/play
      via QTimer); (D) world-anchored **tide/current scene-graph Layer** (icons
      + log-scaled current arrows + labels, sampled at displayTime on a worker,
      rebuilt on time/viewport change — reuses SgBuilder/billboards); (E)
      **bottom timeline slider HUD** (scrub/Now/Play/readout); (F) docked
      **tide/current graph panel** replacing `TCWin`, bound to the same
      `TimeController`. **Done so far:** C + E (TimeController + timeline HUD);
      **A** — the engine is ported into `libs/tides` and **builds, links and
      calculates** in opencpn-qt (verified via `OCPN_QT_TIDE_TEST`: loaded 8,184
      stations from harmonics-dwf .tcd and predicted Honolulu / Nawiliwili / etc.
      for 'now'). **A.2** — the engine is now **pure Qt/std** (wxString→QString,
      wxObjArray→an `ObjArray<T>` shim, wxHashMap→QHash, wxStringTokenizer→
      `QString::split`, wxConvUTF8→`fromUtf8`, wxLog→qInfo/qWarning; no wx in the
      code, wx dependency dropped from the lib CMake). Re-verified still
      calculating identically (same 8,184 stations, correct names). **B DONE** —
      `TideModel` (context property `tides`) persists a harmonic-file list and
      (re)builds the global `TCMgr` via `LoadDataSources`; bundled
      `harmonics-dwf` seeded on first run; Options→Charts→Tides is a real
      add/remove/list picker. **D DONE** — `TideLayer` (world-anchored,
      `StaticNavLayer`) queries the engine per station in the viewport bbox at
      the timeline's display time (`gTimeSource`), drawing tide markers + height
      labels and current set-arrows; gated on `DisplayConfig.showTides`
      (MUIBar ≋), rebuilt on zoom / minute-tick / toggle. `TimeController` is
      now an `instance()`+`create()` singleton so the C++ Layer and the QML
      timeline share one clock. **F DONE** — a docked **bottom tide-graph
      drawer** + a full-width window-bottom **time bar** that rides the drawer
      and uses a **fixed read-marker with a pannable, infinite axis** (drag pans
      time; chart tides + graph animate under the marker; ▶ animates; Now
      re-snaps). `TimeController` rewritten to the pan/fixed-marker window
      (8h back + 24h ahead, marker ¼; panPixels/setDisplayFraction). Clicking a
      tide/current station (`ChartCanvas::pickTideStationAt`) selects it into a
      new `TideGraphViewModel` (curve `samples()` + HW/LW or flood/ebb
      `events()` + `valueAtMarker`, all in the user's units); the QML graph is a
      `Canvas` aligned to the bar's shared time→x mapping. Current-speed labels +
      height-unit formatting also landed (the animation-on-drag polish too).
      **Remaining (updated 2026-06-10):** ~~slack markers~~ + ~~DST tick
      labels~~ DONE — slack zero-crossings ring the graph's zero line with
      "Slack hh:mm", and the time bar marks each local midnight with a
      day-name divider (hour labels were already DST-correct). Still open:
      scale-gated decluttering and the **active-tides** sounding adjustment
      (the per-(lat,lon,time) seam lives in `TideLayer`/the engine).
      *(severity: medium — large effort)*
- [x] **P3.15** **Alert engine (sound triggering). DONE (2026-06-01).** All
      four trigger sources are wired through one `AlertEngine`
      (`gui/qt/alert_engine.{h,cpp}`, owned by `ChartCanvas`, exposed to QML as
      `chart.alerts`). `ChartCanvas` feeds it the CPA-enriched `aisTargets()`
      (computed once, shared with the AIS info popup) and the own-ship fix on
      each dynamic tick. A pulsing **alert banner** in `Main.qml` shows the
      highest-priority alert and emits `soundRequested(file)` →
      `SoundPlayer.play()`; **Acknowledge** silences it.
      - **AIS CPA/TCPA** — per-MMSI state (active / acked-with-hold-off /
        already-sounded). Banner = lowest-TCPA target; gated on
        `AisConfig.cpaAlert` (banner) + `cpaAlertSound` + `UIConfig.aisSoundFile`
        (sound); ack holds off `AisConfig.ackTimeoutMin`; leaving the danger
        zone re-arms.
      - **SART / DSC distress** — `AisTarget` gained `isSart`/`isDsc`, populated
        in `ModelNavDataProvider::mirrorTargets` from `AisTargetData::Class ==
        AIS_SART/AIS_DSC` (+ `b_isDSCtarget`). Distress always banners
        (independent of the CPA gate) and outranks a CPA alert; sound gated on
        `UIConfig.sart/dscAlertSound` + the matching file.
      - **Anchor watch** — `dropAnchor()` pins the watch at the last own-ship
        fix (persisted in ConfigStore: `anchor/{set,lat,lon,radiusM}`, survives
        restart); a fix outside `anchorRadiusM` raises the (red) anchor alarm
        (`UIConfig.anchorAlarmSound` + file), held off on ack until the boat
        returns inside. `AnchorWatchLayer` (new world-anchored Layer) draws the
        cos(lat)-scaled circle, amber→red on breach. On-chart control: a ⚓
        MUIBar button opens a popup (drop / raise / radius spinbox).
      - **Ship's bells** — a re-arming half-hour `QTimer`; on the boundary, if
        `UIConfig.playShipsBells`, strikes 1–8 bells (the 4-hour-watch count)
        using the bundled `data/sounds/{1,2}bells.wav` (new `OCPN_QT_SOUNDS_DIR`
        compile def), sequenced via `QTimer::singleShot`.
      All build clean; app launches with no QML errors. **Polish deferred:**
      distinctive SART/DSC chart rendering (they currently draw as normal
      targets — only the banner/sound flags them), and an anchor-watch
      panel in Options (today it's the MUIBar popup only).
- [x] **P3.16** **Activate & follow a route + test ship. DONE** (commit
      `48cfdf234`; entry retro-added 2026-06-10 — the ID was used in the
      commit but never written here). Route activate/deactivate;
      `RouteFollower` (`gui/qt/route_follower.{h,cpp}`) exposes BTW / DTW /
      XTE-with-steer-side / VMG / ETA as `Q_PROPERTY`s bound into the HUD —
      the wx `concanv` active-route console parity — advances waypoints on
      arrival and emits the autopilot NMEA via `g_pRouteMan`;
      `SimShipController` provides the test ship to exercise it.

### P3.6 — Options / Settings dialog breakdown

The legacy wx Options dialog (`gui/src/options.cpp`, `gui/include/gui/
options.h`) is a `wxListbook` of six top-level pages, each a `wxNotebook`
of sub-panels (`CreatePanel_*`). This is the authoritative feature
inventory for the QML port. The Qt build (`gui/qt/qml/Main.qml`,
`optionsWindow`) implements the six-page shell (Display / Charts / Connections
/ Ships / User Interface / Plugins); most pages are built out against this
inventory and backed by persisted settings singletons, with the checklist
marking what is wired live vs. persisted-pending vs. still to build.

Status legend: `[x]` wired & live · `[p]` control present, setting persisted
but pending renderer support · `[~]` page exists, control still a placeholder
· `[ ]` not yet present · `[—]` not applicable to the Qt build (rationale
given).

The Display page is now built out as **General / Units / Advanced** sub-tabs
in `optionsWindow` (Main.qml), all bound to the shared `DisplayConfig` backend
(`gui/qt/display_config.{h,cpp}`, context property `display`), persisted via
`ConfigStore`. The right-edge **Canvas Options drawer** ("Quick display")
shares the same backend, so the two surfaces stay in sync. Changes apply live
(no Apply button). The wx "Templates" sub-panel and the unported items below
remain.

**Display page → General** (wx sub-panel: General)
- [x] Auto-follow own ship (`chart.followOwnShip`). Plus the Qt-only demo-mode
      and debug-overlay toggles.
- [x] Show compass / GPS window — toggles the on-chart compass rose overlay
      (`display.showCompass`; also in the Quick-display drawer).
- [x] Mouse-wheel zoom sensitivity — slider 1.1×–2.0× per notch, consumed by
      `ChartCanvas::wheelEvent` (`display.wheelZoomFactor`).
- [x] Own-ship COG/SOG predictor length (minutes) — consumed by
      `OwnShipLayer` (`display.cogPredictorMinutes`).
- [x] Navigation Mode North-Up / Course-Up / Head-Up + look-ahead — DONE
      (2026-05-31). The viewport now rotates via real scene-graph rotation;
      persisted (`display.navMode`, `display.lookAhead`), and clicking the
      on-chart compass rose cycles the three modes.
- [p] Preserve scale on chart switch — persisted (`display.preserveScaleOnSwitch`);
      the Piano-click autoscale will read it once wired.
- [p] Time display UTC vs local, SOG/COG damping, default boat speed (ETA) —
      controls present and persisted (`display.timeZone`,
      `sogCogDampingSeconds`, `defaultBoatSpeed`); no readout consumes them yet
      (no time field / nav filter / ETA panel in the Qt HUD so far).
- [—] Enable/disable quilting — the Qt canvas **always** quilts (the only
      chart model is the scene-graph quilt + Piano bar); a global "turn
      quilting off" mode has no Qt equivalent, so it is intentionally omitted.
- [—] Smooth pan/zoom, zoom-to-cursor, 10 Hz screen update — moot under Qt:
      pan/zoom is GPU scene-graph and inherently smooth, the wheel already
      zooms about the cursor, and redraw is driven by the scene-graph vsync
      (no fixed-Hz repaint timer to expose).
- [x] Auto-anchor mark — **landed 2026-06-10**
      (RouteDefaultsConfig.autoAnchorMark + dropAnchorMark; checkbox on
      Routes & Marks; undo-able). *(HDT predictor length landed
      2026-06-10 — it lives on Ships > Own ship.)*

**Display page → Units** (wx sub-panel: Units) — **[x] built**
- [x] Distance, speed, wind-speed, depth, height, temperature unit choices
      (`display.{distance,speed,wind,depth,height,temp}Unit`).
- [x] Lat/Lon format — decimal-minutes / DMS / decimal-degrees
      (`display.latLonFormat`).
- [x] Show true / magnetic bearings + user magnetic variation
      (`display.showMagneticBearings`, `useUserMagVar`, `userMagVar`).
- Note: speed / lat-lon / bearing settings take effect live through
  `DisplayConfig::format{Speed,Distance,LatLon,Bearing}`, which the HUD, AIS
  info and status-bar view-models now call. Depth/height/temp/wind units are
  stored and await the S-52 sounding pipeline + wind/depth readouts that will
  consume them.

**Display page → Advanced** (wx sub-panel: Advanced)
- [p] De-skew raster charts, course-up heading-averaging time, screen-size
      (mm) calibration, responsive/touch sizing — controls present and
      persisted (`display.{deskewRaster,chartRotationAveraging,screenMmWidth,
      responsiveSizing}`), pending the raster/rotation/true-scale/touch render
      paths.
- [—] OpenGL on/off + the "OpenGL Options…" sub-dialog (accelerated panning,
      texture compression + caching, polygon/line smoothing, software GL,
      texture-memory size, rebuild/clear texture cache) — not applicable: the
      Qt build renders exclusively through the Qt Quick scene graph (GPU
      always on, RHI manages texture upload/caching), so there is no GL on/off
      switch or texcache to tune. A note in the Advanced tab states this.
- [—] Chart-display update period — **covered by design (2026-06-10):** the
      Qt scene graph is render-on-demand (a frame draws only when something
      changes); there is no repaint timer to configure.

**Display page → Templates** (wx sub-panel: Templates)
- [x] Configuration templates — **landed 2026-06-10** (ConfigTemplates
      singleton: JSON snapshots of the whole ConfigStore; save / apply /
      delete on the User Interface page; apply effective on next start).
      The multi-canvas screen-config selector rides P6.1.

**Charts page** (wx sub-panels: Chart Files, Vector Chart Display, Chart
Groups, Tides & Currents) — now four sub-tabs in `optionsWindow`. The extended
vector options bind a new `ChartConfig` QML-singleton (persisted); the live
toggles stay on `chart` (ChartCanvas).
- [~] **Chart Files** — chart directory list (add / remove / compress /
      migrate), scan-and-update DB, force full rebuild, "Prepare all ENC
      charts" (PARSE_ENC), rebuild chart database. **Updated 2026-06-09:** the
      runtime chart-directory manager is now **live** (`chart.chartSource`
      add / remove / rescan with status + busy indicator, `Main.qml`:1462-1528,
      `chart_source_model.*`); still missing: compress, "Prepare all ENC",
      explicit force-rebuild.
- [x] **Vector Chart Display** → Display Category (Base / Standard / All;
      wx also has Mariner's Standard). Qt has Base/Standard/All wired.
- [x] Vector → detail toggles (live): soundings, text, lights, buoys/beacons.
- [x] Vector → detail/cartography & style — **live** (corrected 2026-06-01;
      the earlier "persisted-pending" note was stale). `ChartConfig.*` →
      `ChartCanvas::applyChartConfig` → `S52Engine::applyDisplaySettings` sets
      the s52plib flags and re-decodes resident cells (run at startup + on any
      change, debounced): chart-info objects, buoy/light labels, light
      descriptions, extended light sectors, national text, important-text-only,
      de-cluttered text, reduced detail at small scale (SCAMIN), super-SCAMIN,
      **graphics style (paper/simplified → `m_nSymbolStyle`), boundaries
      (plain/symbolised → `m_nBoundaryStyle`), 2-/4-colour (`S52_MAR_TWO_SHADES`),
      shallow/safety/deep depth contours (`S52_MAR_*_CONTOUR`)**.
- [~] Vector → CM93 detail-level slider — **live (2026-06-10)**; offset
      entry still pends rendering verification (needs the CM93
      decode path, **P2.19**).
- [x] ~~"User Standard Objects" checklist~~ — DONE 2026-06-10 (P3.6 gap 1,
      Mariner's Standard category). Still open here only: CM93 offset
      (P2.19). Original line: Vector → CM93 offset, USO checklist (select-all /
      clear-all / reset-to-standard), ECDIS help (not surfaced).
- [x] **Chart Groups (2026-05-31).** Qt-native named-group editor in
      `ChartSourceModel`: create / rename / remove groups, toggle each chart
      folder's membership via a checklist, and an **Active group** selector
      ("All charts" + each group). Persisted as JSON (`chartGroups` /
      `chartActiveGroup`). The active group filters what loads —
      `ChartSourceModel::activeDirectories()` (group membership ∩ live dir
      list), which `ChartCanvas::reloadCharts` now iterates; changing the
      active group triggers a reload. (`chart_source_model.*`, `Main.qml`
      Groups tab.)
- [x] **Tides & Currents** — **Updated 2026-06-09:** the data-set list is now
      **live** (add / remove data sets via `tides.addSource`, `Main.qml`:1839-1893);
      on-chart tide/current stations are predicted by the built-in engine
      (**P3.14**, the `gui/src/tcmgr.cpp` port).

**Connections page** (wx sub-panel: NMEA / data connections)
- [x] Connection list (enable/disable, summary, remove) backed by
      `chart.connections` (`ConnectionsViewModel`).
- [x] **Connections editor parity (2026-05-31).** The add/edit form now covers
      every transport/protocol the wx-free comm framework actually wires:
      **Serial** (port via `QSerialPortInfo` enumeration + rescan, baud) and
      **Network** TCP/UDP, each carrying **NMEA 0183 / NMEA 2000**; plus **I/O
      direction** (Input / Output / Both → `dsPortType`), **input & output
      sentence filters** (Accept/Ignore = whitelist/blacklist; input filter is
      live via `ConnectionParams::MakeInputFilter`), a **user comment**, and
      full **edit-in-place** of an existing connection (re-keys the running
      driver). Persisted as JSON via `ConfigStore`. (`connections_view_model.*`,
      `Main.qml` Connections tab.)
- [x] ~~GPSD, SignalK, TCP-server deliberately not in the editor~~ —
      **exposed (2026-06-12)** the same day P1.5c/P1.5k un-parked them:
      transport combo gains GPSD + Signal K (wire protocol/direction
      fixed, NMEA filters hidden for Signal K, auth-token field), and a
      blank TCP address is listen mode ("TCP listen :port" summary).
      Still out: Garmin (dropped from scope, P1.5j-1) and SocketCAN
      (returns with P1.5g).
- [x] Per-connection **priorities** — **landed 2026-06-10**
      (CommPrioritiesModel + PrioritiesDialog over CommBridge's five
      priority maps; re-rank with Move up/down, active source marked,
      Clear All; Connections > Priorities…). The data-monitor launcher
      moved to this page earlier (P3.22). **P3.6 has no open sub-items
      left** beyond the user-decision rows (per-element fonts) and
      explicitly-deferred ones (config templates).

**Ships page** (wx sub-panels: Own ship, AIS Targets, MMSI Properties,
Routes/Points) — now built as four sub-tabs in `optionsWindow`, backed by the
QML-singleton settings objects `OwnShipConfig`, `AisConfig` and
`RouteDefaultsConfig` (+ the existing identity in `OwnShipConfig`), persisted
via `ConfigStore`. (These, and `DisplayConfig`, are exposed as
`QML_SINGLETON`s rather than context properties — a context property reads
`undefined` in bindings on early-constructed objects, which left bool toggles
stuck; singletons are compile-time resolved and always available.)

**Ships → Own ship** (wx sub-panel: Own ship)
- [x] Identity: vessel name + own MMSI (drives AIS self-exclusion).
- [x] Display: range rings (show, count, spacing, unit) — consumed live by
      `OwnShipLayer` (2026-06-01). Draws `ringCount` concentric world-anchored
      circles at `ringSpacing` (NM / km / statute-mile), sized by the
      cos(latitude) Mercator scale so they're true geographic circles; resized
      on an `OwnShipConfig` change or noticeable N/S drift.
- [x] Display: real-scale ship icon (icon type, LOA, beam, GPS offsets, minimum
      screen size) — **live** (2026-06-01). `OwnShipLayer` draws a to-scale hull
      pentagon in world units (cos(lat)-scaled, oriented by heading, GPS-antenna
      offset baked in) when the icon type is real-scale and the dimensions are
      set; it falls back to the fixed marker when the hull would be below
      `minScreenSize` (≈4 px/mm) on screen. Both real-scale-bitmap and
      real-scale-vector map to the vector hull (no bitmap asset).
- [p] Display: show direction to active waypoint — present and persisted
      (`OwnShipConfig.showWaypointDirection`); pending an active-waypoint
      accessor on the NavDataProvider. (COG predictor length lives on Display →
      General, consumed live.)
- [x] Range-ring colour + HDT (separate from COG) predictor length
      (2026-06-10): `OwnShipConfig.ringColor` / `hdtPredictorNm`, consumed
      live by the own-ship layer.

**Ships → AIS Targets** (wx sub-panel: AIS Targets) — controls built;
all persisted (`AisConfig.*`); the **CPA/TCPA engine is now implemented**
(`ais_cpa.cpp`, wx parity + danger display) and consumes the warn thresholds —
filtering and the alert sound/dialog engine are still pending
- [x] CPA/TCPA: max target range, CPA warn distance, TCPA warn time — consumed
      live by the CPA/TCPA engine (`ais_cpa.cpp`); dangerous targets flagged.
- [x] Lost targets: mark-lost / remove-lost timeouts — **live** (2026-06-01).
      `ChartCanvas::syncAisModelGlobals` pushes `AisConfig.markLostMin` /
      `removeLostMin` (+ `suppressAnchoredSpeedMax` → moored kts) into the reused
      model decoder's globals (`g_bMarkLost`/`g_MarkLost_Mins`/`g_bRemoveLost`/
      `g_RemoveLost_Mins`/`g_ShowMoored_Kts`), which set `b_lost`/`b_removed`;
      `mirrorTargets` skips those, so targets age out per the user's timeouts.
- [x] Display: COG-predictor length (+ "sync with own ship"), show names —
      consumed live by `AisLayer` (2026-06-01): the predictor reach reads
      `AisConfig.predictorMinutes` or, when sync is on, `DisplayConfig`'s
      own-ship predictor length; the name label is suppressed when Show names is
      off; an `AisConfig`/`DisplayConfig` change rebuilds the retained target
      nodes.
- [x] AIS trails (target tracks) — **live** (2026-06-01). Global position
      history is recorded to a new append-only `ais_track(mmsi,t,lat,lon,cog,
      sog,hdg)` table in `SqliteAisTargetStore` (deduped per moved fix, batched
      on the prune tick), purged past `AisConfig.trackRetentionDays` (default 7,
      configurable in Options → Ships → AIS Targets). A trail is drawn only for
      vessels the user *selects + toggles* ("Show trail" in the AIS info popup →
      `ChartCanvas::setAisTrail` → `AisLayer`): a 2px light-grey AA-line seeded
      from SQLite (`NavDataProvider::aisTrack`) over the last **5× the predictor
      reach**, then slid live each tick. COG/SOG/HDG are stored (not derived) for
      faithful historical replay.
- [p] Display (still pending): suppress-anchored speed max, attenuation
      threshold, show area notices, show real size, WPL handling.
- [p] Rollover info block toggles: class/type/status, SOG/COG, CPA/TCPA.
- [x] Alerts: alert dialog + alert sound — wired (2026-06-01) via the
      `AlertEngine` (P3.15): `cpaAlert` shows the banner, `cpaAlertSound` +
      `UIConfig.aisSoundFile` play it, `suppressMooredAlerts` gates inside
      `ais_cpa`, and `ackTimeoutMin` is the Acknowledge hold-off.
- [—] Realtime-prediction speed min — **not applicable yet (2026-06-10):**
      the Qt AIS layer has no realtime-prediction render path, so the
      threshold would be a dead control; surface it with that feature.

**Ships → MMSI Properties** (wx sub-panel: MMSI Properties)
- [x] Per-MMSI list + editor (track mode default/always/never, persist track,
      ignore, MOB, VDM follower, ship name) — **DONE (2026-06-10)**: full
      editor on Ships > MMSI bound to the live `g_MMSI_Props_Array`,
      persisted via ConfigStore (`ais/mmsiProps`).

**Ships → Routes/Points** (wx sub-panel: Routes/Points) — controls built;
persisted (`RouteDefaultsConfig.*`), pending styled route/track creation
- [p] New route: line colour (ColorDialog swatch) + style, persist-active-
      route.
- [p] Waypoints: default mark + route-point icon names, arrival-circle radius,
      SCAMIN min/max.
- [p] Tracks: auto-daily mode (Off / Computer / UTC / LMT), tracking
      precision, highlight + highlight colour.

**User Interface page** (wx sub-panels: General Options, Sounds) — now a page
in `optionsWindow` (between Ships and Plugins), backed by the `UIConfig`
QML-singleton (persisted). Several controls are wired live to the shell.

**UI → General Options**
- [x] Show status bar / chart bar / compass window / zoom buttons — toggle the
      footer ToolBar, chart bar, compass overlay (shared with Display via
      `DisplayConfig.showCompass`) and the MUIBar zoom keys live.
- [x] Toolbar transparency (floating-toolbar opacity) and auto-hide (+ timeout)
      — wired: a hover-reset Timer collapses the toolbar after the timeout.
- [x] UI scale factor — drives the touch-target size (`root.touchSize`) live.
- [p] Touchscreen interface, Inland ECDIS, play ship's bells, and the chart-
      object / ship / ENC-text / ENC-sounding scale factors — present and
      persisted; await the touch layout / renderer scaling / sound + bells.
- [x] Language choice — persisted AND honoured at startup via QTranslator
      (P3.10, 2026-06-10); applies on restart.
- [x] Per-element fonts — **DECIDED 2026-06-10: dropped** (user
      confirmed: platform system fonts throughout; chart text sizing
      stays on the S-52 controls). Toolbar/window style,
      menu bar (the frameless Qt shell has no menu bar to toggle).
- [—] Scaled-graphics interface — folded into Display → Advanced
      "Responsive / touch sizing" (`DisplayConfig.responsiveSizing`); not
      duplicated here.
- [x] Mouse-wheel zoom sensitivity — lives on Display → General
      (`DisplayConfig.wheelZoomFactor`), consumed live.
- [x] Inland ECDIS mode — **landed 2026-06-10** as a settings preset
      toggle on Charts > Vector (km + km/h units, Standard category, AIS
      real-size off; wx SwitchInlandEcdisMode parity).
- [—] Show compass window / mouse-wheel sensitivity are not duplicated as
      separate UIConfig keys: the UI page binds the existing DisplayConfig
      properties so the two pages stay in sync.

**UI → Sounds**
- [x] **Qt sound engine (2026-05-31).** `SoundPlayer` (QML singleton,
      `QMediaPlayer` + `QAudioOutput`, `Qt6::Multimedia`) plays any Qt-supported
      audio file. The per-event **Test** buttons (anchor / AIS / SART / DSC) are
      now live (`SoundPlayer.play(file)`); files + enables persist via
      `UIConfig`. (`sound_player.{h,cpp}`, `Main.qml` Sounds page.)
- [x] **Alert-engine triggering** (the part that *fires* each sound) — DONE
      (2026-06-01, **P3.15**): the `AlertEngine` fires AIS CPA/TCPA, SART/DSC
      distress, anchor-watch and ship's-bells, each gated on its `UIConfig`
      enable + file and routed to `SoundPlayer.play` via `soundRequested`.
- [x] Sound-output device selection — DONE (2026-06-10); custom play
      command deliberately unported (Qt Multimedia covers the formats).

**Plugins page**
- [~] Qt shows a placeholder. To build: plugin list/enable, catalog
      manager (browse / install / uninstall / update), add-plugin panel,
      per-plugin settings. Lifecycle-only UI; rendering is the Phase 4
      Qt plugin host. *(Tracked jointly with Phase 4.)*

**Dialog framework / cross-cutting**
- [—] OK / Cancel / Apply semantics and the wx change-bitmask — **covered
      by design (2026-06-10):** the Qt Options window is modeless and
      live-apply (macOS System Settings convention); every control commits
      immediately and persists, so there is no pending-changes bitmask to
      flush or roll back. ~~Original:~~ OK / Cancel / Apply semantics and the wx change-bitmask
      (`S52_CHANGED`, `GROUPS_CHANGED`, `TIDES_CHANGED`, `GL_CHANGED`,
      `LOCALE_CHANGED`, `REBUILD_RASTER_CACHE`, …) — the Qt port needs an
      equivalent "what changed → what to refresh" dispatch. The current
      QML pane applies changes live (no Apply button).
- [x] Initial-page deep-linking (`openAt(page)`, SetInitialPage parity)
      + persisted window position — **landed 2026-06-10**. Size is fixed
      by design (macOS settings convention); colour scheme follows the
      app-wide scheme already.

### UI parity audit — toolbars / context-menus / settings (2026-06-09)

A three-part parity audit (Qt `gui/qt` vs the wx app `gui/src`) over the
toolbars, the canvas right-click menus, and the Options dialog, taken before
building out the rest of the UI. Findings are cited to wx + Qt source.
**Headline:** the toolbars and the settings dialog are close to parity; the
**canvas context menus are the biggest gap**. `Main.qml` (4,765 lines) should
be split into a component module *before* this build-out — **P3.17**.

**Toolbar gaps** (most tools ported — see P3.5). Remaining:
- **MOB marker** (`floatToolbar`, `Main.qml`:4738) and **Print** (4721) are
  present but **inert** (no `onClicked`) — wire to MOB-drop / a print path
  (wx `ActivateMOB()` / `DoPrint()`, `ocpn_frame.cpp`:2651/2580).
- **iENC toolbar absent**: Range +/− steppers + range annunciator
  (`ienc_toolbar.cpp`:79-80) have no Qt equivalent; Density exists as the
  Base/Standard/All radios but lacks the 4th **Mariner's Standard** level.
- MUI **Set-Scale** (click the scale annunciator to type a scale,
  `mui_bar.cpp`:942) — Qt shows scale text only.
- Follow-ship is 2-state; wx has a 3rd **look-ahead** state (`mui_bar.cpp`:1015).

- [~] **P3.17** **Restructure `Main.qml` into a QML component module.** The
  single 4,765-line file is the app shell + both toolbars + drawers + context
  menus + popups + HUD + the whole 6-page Options dialog (~half the file). Split
  into per-feature `.qml` components, each added to `QML_FILES` in
  `gui/qt/CMakeLists.txt` (same module ⇒ types visible by name; `QML_SINGLETON`
  configs + root context properties stay in scope). **Pure refactor, no
  behaviour change** — extract one component at a time, rebuild + run between
  each; the only hazards are cross-boundary `id` refs and inline functions that
  touched sibling `id`s (→ become component `property`/`signal` interfaces). Do
  this **first** so the P3.5/P3.18/P3.6 build-out lands in proper files.
  **Progress (2026-06-10):** done in five commits — `FloatToolbar.qml` +
  `MuiBar.qml` (acf7e363d), `OptionsWindow.qml` + `VectorDetailList.qml`,
  `ScaleDialog` / `MarkEditorDialog` / `RouteDetailsWindow` /
  `ObjectQueryWindow` / `AboutWindow` / `DataMonitorWindow`,
  `CanvasOptionsDrawer` + `RouteManagerDrawer`, and `VesselHud` +
  `TideTimeBar` + `TideGraphPanel`. Main.qml: 4,681 → 953 lines.
  **Layout decision:** files live flat in `gui/qt/qml/` (not the subdir
  scheme first sketched) — same-directory implicit type resolution, matching
  the toolbar extraction. **Conventions:** extracted components reference
  `chart` + the config singletons via the QML context chain; sibling
  windows/drawers are reached via signals wired in Main.qml; shell state /
  window sizing passes as properties (`appWindow`); sibling-id anchors stay
  at the instantiation site in Main.qml (the shell owns layout topology).
  **Dialog convention (user feedback 2026-06-10):** every dialog is a real
  `Window { flags: Qt.Dialog; modality: Qt.ApplicationModal }` with native
  title bar/controls, 18–20 px content margins and a `DialogButtonBox` —
  never an in-scene QtQuick `Dialog`/sheet (MarkEditor / ScaleDialog /
  ConfirmDialog were converted 2026-06-10). Corner-anchored chart-state
  cards (anchor watch, AIS info, look-ahead flyout) remain `Popup`
  popovers by design.
  **Remaining (by design):** the two small canvas context menus extract as
  part of the P3.18 rebuild (no point moving them twice); the display-
  anchored canvas overlays (alert banner, compass, nav-HUD pills, chart bar,
  scale bar, sim panel, AIS info + anchor popups) stay inside the
  ChartCanvas subtree in Main.qml — the 953-line shell is manageable and
  they are genuinely chart-coupled.

- [x] **P3.18** **Canvas context-menu parity. DONE (2026-06-10)** — all
  four audit tiers implemented, including **Send-to-Peer**
  (PeerSendController + native SendToPeerDialog: mDNS discovery, manual
  host[:port], PIN pairing, activate-route option) and **Send-to-GPS**
  (GpsUploadController + native SendToGpsDialog: serial NMEA-0183
  RTE/WPL upload with progress, over the model comm_n0183_output path).
  Deliberately-omitted odds: per-mark anchor watch (the global
  anchor-watch popover covers the use), Chart Groups / CM93-offset menu
  entries (Chart Groups lives in Options; CM93 is P2.19).
  **Send-to-Peer plan (scoped 2026-06-10):** the model is ready —
  `FindAllOCPNServers(timeout)` (mdns_query.h) populates `MdnsCache`;
  `SendNavobjects(PeerData&)` (peer_client.h, QNAM-based since P1.13)
  takes routes/routepoints/tracks + `dest_ip_address` and drives two
  *synchronous* callbacks: `run_pincode_dlg` (must block for the user's
  PIN — needs a QEventLoop-pumped modal dialog window) and
  `run_status_dlg`, plus an `EventVar` progress. Qt shape: a
  `PeerSendController` QObject (`peers()` snapshot + `sendRoute(idx, ip,
  activate)` run on a worker with queued dialog requests), a native
  dialog window listing discovered peers + PIN entry, menu items on the
  route/mark/track menus. Send-to-GPS similarly wraps the
  `comm_n0183_output` route-upload path (wx SendToGpsDlg). Original audit: Qt had only a general
  `chartContextMenu` + a route-node menu (edit-mode only); wx builds a focused
  popup per object via `CanvasMenuHandler` (`canvas_menu.cpp`).
  **Progress (2026-06-10): Tier 1 largely DONE** — ChartCanvas now hit-tests
  right-clicks against marks + route nodes/segments outside edit mode and
  pops focused menus (all in the new `ChartContextMenus.qml`):
  **Navigate to here / to a mark** (temporary GOTO route via provider
  `createRoute`, activated at once, auto-deleted on arrival — wx
  `m_bDeleteOnArrival`); **route menu** (Activate / Deactivate / Activate
  next waypoint / Zero XTE / Insert waypoint here / Edit points / Reverse /
  Details… / Delete); **mark menu** (Navigate to / Edit / Delete);
  **Zero XTE** (`RouteFollower::zeroXte` →
  `Routeman::ZeroCurrentXTEToActivePoint`); and the **Measure tool**
  (click legs + dashed rubber-band via the new `MeasureLayer`, live
  leg brg/dist + running total readout pill, Esc/menu to end).
  **Tier 1 now fully DONE** — **Append waypoints** (provider
  `beginAppendRoute` reuses the route-build mouse flow on an existing model
  route; finish/cancel both persist) and **Split at this leg** (provider
  `splitRoute` → "<name> A"/"<name> B", original deleted, followed route
  deactivated first) landed 2026-06-10; wx's reverse "Rename waypoints?"
  prompt and delete confirmations are skipped by design (Qt convention so
  far). **Tier 2 (AIS) DONE (2026-06-10):** right-click a target →
  Target query / Center view on target / Show-Hide target track /
  Target list… / Copy MMSI; the new `AisTargetListWindow.qml` (wx
  `AISTargetListDialog`) lists live targets nearest-first
  (range/brg/SOG/COG/CPA/TCPA, danger/SART accents, click = select,
  double-click = center). Missing, by tier:
  - *Tier 1 (core nav):* **Navigate To Here / To This mark**
    (`canvas_menu.cpp`:490/923); a **route** right-click menu (Activate /
    Deactivate / Activate-Next / Insert / Append / Split / Reverse / Properties,
    :719-790); **Zero XTE** (:532); **Measure** (Qt item exists but
    `enabled:false`, `Main.qml`:4031 — no measure tool).
  - *Tier 2 (AIS):* AIS right-click → Target Query / **Target List** / per-target
    CPA toggle / **Copy MMSI** (:634-654); general **Show/Hide CPA alarm** (:694).
    (AIS query *content* is reachable today via a left-click `aisInfo` popup.
    Note the **Target List window itself doesn't exist** in Qt either — the
    menu entry needs the window, not just the gesture.)
  - *Tier 3 (objects/charts):* **largely DONE (2026-06-10)** — mark menu
    (tier 1), **track menu** (Hide / Zoom-to / Delete, new polyline
    hit-test) and **chart controls** (Scale In/Out, orientation submenu,
    full-screen) all landed on the canvas menus. Remaining tier-3 odds:
    Copy-as-KML / Send-to-GPS / Send-to-Peer interop, per-mark
    Anchor-Watch, Chart Groups + CM93 offset entries (CM93 itself is
    P2.19).
  - *Tier 4:* **Copy-as-KML + Paste-KML DONE (2026-06-10)** — route /
    mark / track menus copy a wx-parser-compatible KML document; the
    general canvas menu pastes one (Points → marks, LineStrings →
    routes; namespace-agnostic, so Google Earth exports paste).
    **Undo/Redo DONE (2026-06-10)** for the wx undo.cpp scope (mark
    create/delete; Cmd-Z / Shift-Cmd-Z + menu items, 32-deep stack;
    recreation assigns a fresh GUID — noted divergence). Still open:
    Send-to-GPS / Send-to-Peer (need the upload/peer machinery).
  Several Tier-1/3 actions exist in the **left drawer** (activate / reverse /
  delete) but not as a canvas right-click — parity needs the on-chart gesture.

**Settings (P3.6) — stale statuses corrected 2026-06-09** (flipped inline above
where top-level):
- **Charts → Chart Files `[ ]`→`[~]`** — runtime dir manager now live (see above).
- **Charts → Tides & Currents `[~]`→`[x]`** (data-set list) — see above.
- **Ships → AIS Targets** — the "still pending" controls (target-track length,
  suppress-anchored, attenuation, area notices, real size, WPL, rollover
  toggles) are all **present controls** now (`Main.qml`:2569-2651); CPA/TCPA is
  computed **live** (`ais_cpa.cpp`). The in-QML note at `Main.qml`:2688
  ("CPA/TCPA … not wired in yet") is stale and should be removed; the AIS-page
  **Test** button is still `enabled:false` (2670) while the Sounds-page Test works.
- **Routes/Points** live under **UI → "Routes & Marks"** in Qt, not under Ships.

**Settings — genuine remaining gaps, prioritized:**
1. ~~Vector → **"User Standard Objects"** per-object viewing-group
   checklist~~ — **DONE (2026-06-10):** per-buffer class table + per-primitive
   `classIdx` in the `s52sg` schema (filled at every emit site), provider
   `setHiddenClasses` post-decode cull, a 4th display category **"Mariner's
   standard"** (wx `MARINERS_STANDARD`; DISPLAYBASE always shows), and the
   checklist UI (show/hide-all + per-class checkboxes, descriptions via
   `S57Dictionary`, persisted as `display/hiddenClasses`). wx's
   "reset to STANDARD" preset is approximated by **Show all** for now.
   **The original implementation plan (kept for reference):** the SG
   emit path bypasses s52plib's `ObjectRenderCheckCat`/`nViz` entirely, and
   the emitted primitives carry `dispCat`/`scamin`/`viewGroup` but **not the
   S-57 class** — so the feature needs: (a) a per-buffer **class table**
   (encountered FeatureName acronyms) + a per-primitive class index added to
   all 6 `s52sg` primitive structs, filled at emit (`s52plib_sg.cpp`, where
   `rzRules->obj->FeatureName` is in hand alongside the existing `dispRank`
   derivation); (b) `S52VectorChartProvider::setHiddenClasses(QSet<QString>)`
   culling post-decode like the existing dispCat/SCAMIN culls; (c) a 4th
   display-category choice "Mariner's Standard" (wx `MARINERS_STANDARD`)
   gating when the per-class filter applies; (d) the QML checklist (class
   descriptions via `S57Dictionary::className`, persistence
   `objfilter/viz<ACR>` in ConfigStore, reset-to-STANDARD = the classes
   whose LUP category is STANDARD/DISPLAYBASE); pOBJLArray itself stays
   unused (it is inert on the SG path).
2. ~~**MMSI Properties** editor~~ — **DONE (2026-06-10):** the Ships > MMSI
   placeholder is a full list + editor (MMSI, name, track mode, persist
   track, ignore, MOB, VDM→VDO, follower); edits feed the live decoder's
   `g_MMSI_Props_Array` and persist via ConfigStore (`ais/mmsiProps`,
   wx-compatible serialize format), loaded at canvas startup.
3. ~~Own-ship **HDT predictor length** (separate from COG) + **range-ring
   colour**~~ — **DONE (2026-06-10):** `OwnShipConfig.hdtPredictorNm` +
   `ringColor`, drawn by the own-ship layer (dashed HDT vector), set on
   Options > Ships > Own ship.
4. **Routes & Marks** toggles — **confirm route/track/mark deletion DONE
   (2026-06-10)**: `RouteDefaultsConfig.confirmObjectDelete` + the shared
   `ConfirmDialog.qml`, wired into every drawer/menu delete path.
   **advance-on-arrival-only + per-mark range rings / override-SCAMIN also
   DONE (2026-06-10)** (`RouteDefaultsConfig.advanceOnArrivalOnly` → the
   model global; rings/SCAMIN in the mark editor). Still open: lock
   marks/waypoints — deliberately unported: Qt's explicit Edit mode
   already prevents accidental drags (the wx lock guarded always-on
   dragging). (A separate route-point icon default already exists —
   `RouteDefaultsConfig.routepointIcon`.)
5. UI → **per-element Fonts** (font + colour + reset) — needs a FontMgr
   equivalent. **Recommendation (2026-06-10, needs user confirmation):**
   drop as deliberately-divergent — the Qt app's typography is system-wide
   by design (system fonts + the UI scale factor), matching the
   modernize-appearance principle; a per-element font manager re-imports
   wx-era complexity. If specific elements need sizing control (e.g.
   sounding figures), add targeted sliders like the existing ENC
   sounding-size one instead.
6. ~~Display → **Show Grid** / **Show Chart Outlines**~~ — **DONE
   (2026-06-10)**: `GridLayer` graticule + the cell-grid toggle
   (`DisplayConfig.showGrid` / `showChartOutlines`), both on Options >
   Display. Still open: Advanced → vector/raster **chart-zoom weighting**
   sliders + **extended chart-bar info** toggle (wx-specific zoom-weighting
   may be moot under the Qt quilt — assess before porting).
7. **Configuration Templates** (Display → Templates) — whole feature; deferred.
8. ~~Sound-output **device selection**~~ — **DONE (2026-06-10)**:
   `SoundPlayer.outputDevice` (persisted by device id) on UI > Sounds.
   The custom play *command* stays unported (a shell-out; Qt Multimedia
   covers the formats the command path existed for).

Full per-control matrices live in the session audit; the above is the
actionable distillation.

### Full-migration scoping review (2026-06-10)

A docs-vs-code review checking this tracker against `gui/qt` and the wx
feature surface ahead of the wx-build retirement. Tracker realignments made:
P0.1 + P3.1–P3.4 marked done, P3.16 retro-added, the image-diff tasks
(P0.7 / P2.0 / X.3) closed as dropped-by-decision. Decisions recorded:
**macOS-first** (Linux/Windows CI = P0.5 becomes a pre-P3.11 gate);
**multi-canvas/split-screen out of scope** for the migration (parked in
Phase 6). New scope found untracked:

- [x] **P3.19** **GPX import & export UI. DONE (2026-06-10).** Provider
      `importGpx` (LoadAllGPXObjects merge + persist + duplicate-skip,
      returns added counts) + `exportGpxAll/Route/Track/Waypoint`
      (`SetRootGPXNode` + `AddGPX*` + atomic `SaveFile`); ChartCanvas
      QUrl invokables; manager drawer gets **Import GPX… / Export all…**
      buttons + a transient added-counts status line, and each route
      tile's ⋯ menu gets **Export GPX…**. Per-mark / per-track export
      can be added to their tiles the same way if wanted.
- [~] **P3.20** **App-wide keyboard shortcuts. Core layer DONE (2026-06-10):**
      `ChartCanvas::keyPressEvent` (wx `chcanv::OnKeyDown` parity) — arrow-key
      pan, `+`/`=`/`-` zoom, **M / F4** measure toggle, **Esc** cancels the
      transient mode (measure → route-build → selection), **Enter** finishes
      a route being built; the canvas claims focus on click and starts
      focused (the test-ship overlay borrows keys while simming). **F11**
      full-screen via a window-level `Shortcut`. Remaining: Ctrl-Z/Y once
      undo exists (P3.18 tier 4); any further wx hotkeys (`hotkeys_dlg`
      audit) as needed; P3.13's route-build keys fold in when both land.
- [x] **P3.23** **Application menu bar (wx RegisterGlobalMenuItems
      parity) — DONE 2026-06-12.** `AppMenuBar.qml` via Qt.labs.platform
      (the NATIVE macOS global bar): Navigate / View / AIS / Tools / Help
      with the wx items, shortcuts and check states, bound two-way onto
      the same seams as the toolbars/Options. New seams built for the
      full set: dropMarkAtCursor/AtBoat (instant dated drops,
      undo-able), scaleChartStep (Ctrl-Left/Right next finer/coarser
      native scale at centre), showEncAnchoring (the wx SetAnchorOn
      class set through the hidden-classes pipeline), and AisConfig
      showTargets/hideMoored/showTargetTracks filtering AisLayer. The
      ONLY omitted wx item is the quilting toggle (Qt provider is
      always-quilted by design). User visual pass pends.
- [ ] **P3.21** **wx-retirement acceptance checklist — gates P3.11.** P3.11
      ("remove the parallel wx build") executes only when all of these hold:
      1. ~~P3.18 canvas context menus — tiers 1–2 minimum~~ **DONE** (all
         four tiers landed);
      2. ~~P3.19 GPX import/export~~ **DONE**;
      3. ~~the P3.6 "genuine remaining gaps" items 1–4~~ **DONE** (User
         Standard Objects checklist, MMSI Properties editor, HDT
         predictor + ring colour, Routes & Marks behaviour toggles);
      4. ~~MOB + Print wired~~ **DONE** (P3.5 toolbar spec; Print stub);
      5. a recorded Phase-4 decision — which plugins must work day-1 vs
         post-retirement. **The decision input is now concrete:** the Qt
         plugin platform is live with dashboard / chart-downloader / GRIB
         at v1 (P4.1–P4.5) — what remains is the user's sign-off on
         whether that set suffices for day-1;
      6. ~~P0.5 CI green~~ **DONE (2026-06-10): macOS green on the
         clean-runner build + smoke-run (run 27304414174); Linux
         best-effort iterating**;
      7. a macOS user-acceptance pass on home waters (visual verification —
         no image-diff, per P0.7).
      **Status: items 1–4 AND 6 complete. Item 5 (Phase-4 day-1 set)
      decided in substance (2026-06-10): o-charts shop = REQUIRED
      day-1 (new P4.7); dashboard = built-in HUD; chart downloader +
      GRIB at v1. Item 7 (acceptance pass) remains; the user has
      directed wx-deprecation work to begin.**
      **Checklist authored (2026-06-13): `Docs/QT_MIGRATION_ACCEPTANCE.md`**
      — an executable per-area sign-off sheet. Its top section is
      pre-verified headlessly (clean launch, vector-chart render of the
      Solent o-charts cells, `de` catalog loads, viewport restore, arm64
      CI link).
      **SIGNED OFF (user, 2026-06-13): acceptance pass PASS + day-1 plugin
      set confirmed; any issues with either to be handled later as defects.
      Items 5 AND 7 now closed → P3.21 SATISFIED → P3.11 wx removal is
      UNBLOCKED and may proceed.**
- [x] **P3.22** **Data Monitor launcher → Connections page. DONE
      (2026-06-10).** The 📡 toolbar slot is removed; a "Data monitor…"
      button on Options > Connections opens the window
      (`DataMonitorWindow.qml`) via an `OptionsWindow` signal wired in
      Main.qml. Satisfies the "show NMEA debug window launcher" item under
      the Connections page above.

Agreed wx↔Qt aligned toolbar layout (the implementation target for P3.5; build
in the P3.17 components `FloatToolbar.qml` + `MuiBar.qml`). wx behaviour traced
to source and cited. The wx app has three toolbar surfaces (master / MUI / iENC)
+ a compass-rose widget; Qt consolidates as below.

**Main toolbar — left vertical** (`FloatToolbar.qml`), in order:
1. ☰ Collapse / expand (wx `ID_MASTERTOGGLE`)
2. ⚙ Options
3. ✚ Create route (toggle)
4. ▤ Route & mark manager
5. ⊚ Track record (toggle)
6. ≈ Tides (toggle, `DisplayConfig.showTides`) — **moved from the MUI bar**
7. ◑ Colour scheme (day / dusk / night cycle)
8. ⎙ Print — wire to a print path (wx `DoPrint`); a stub/"not yet" is acceptable
   as a first cut (full chart printing is a separate feature)
9. ≣ Data monitor (Qt addition) — **moved to the Connections page (P3.22,
   done 2026-06-10)**; the toolbar slot is gone
10. ⓘ Help / about
11. ⚓ Anchor watch (drop / raise + radius) — **moved from the MUI bar**
12. 🛟 MOB — drop MOB marker; icon **fixed ⚓→🛟** (`U+1F6DF` RING BUOY, matches
    wx's red life-buoy); wire to a MOB-drop action
- Plugin tools: deferred (Phase 4).

**MUI bar — bottom-right** (`MuiBar.qml`): Zoom in (+) · Zoom out (−) ·
Fit-to-world (⤢) · **Follow / jump-to-ship (◉)** · Canvas-options menu (☰).
Tides + Anchor removed (→ main toolbar); scale removed (→ status bar).
- **Follow = split button.** Tap = jump-to-ship (centre on own-ship) + toggle
  follow — matches wx `TogglebFollow`→`SetbFollow`→`JumpToPosition`
  (chcanv.cpp:4921/4940/4959). Long-press → a flyout extending from the bar:
  **{Follow centred · Follow + look-ahead}**; the chosen variant runs *and*
  becomes the sticky one-click default. Icon reflects 3 states (off / follow /
  follow-ahead), mirroring wx `UpdateFollowButtonState` (chcanv.cpp:4972).
  Look-ahead = own-ship offset toward the stern so more chart shows ahead (wx
  `m_bLookAhead` / `ToggleLookahead`, chcanv.cpp:3425) — orthogonal to orientation.
  Reusable split-button pattern (colour-scheme is a future candidate).

**Compass rose — top-right:** click cycles orientation North-up / Course-up /
Head-up (wx `SetUpMode`). Look-ahead lives on the Follow flyout, not here.

**Status bar — bottom:** the scale `1:N` readout becomes **clickable → type a
scale**. Free-form like wx (`OnScaleSelected`, mui_bar.cpp:942: strip a `1:`
prefix, **clamp 1:1,000–1:3,000,000**, set exactly — *no* snapping to standard
scales; `SetVPScale`→`SetViewPoint` sets `view_scale_ppm` directly,
chcanv.cpp:5470). Needs a new `chart.setScaleDenominator(n)` invokable.

**Deferred:** iENC inland bar (no inland charts at present); plugin toolbar
tools (Phase 4).

**New C++ surface required** (`ChartCanvas`): `setScaleDenominator(double)`;
a MOB-drop action; and the Follow jump + look-ahead state (verify against the
existing `followOwnShip` property before adding).

## Phase 4 — Qt plugin host  (est. 6–8 wks)

- [~] **P4.1** Define the Qt plugin interface — **DRAFT landed 2026-06-10**
      (`gui/qt/plugin/ocpn_qt_plugin.h`): QPluginLoader modules implement
      `OcpnQtPlugin` (name/version/init/deinit) against an
      `OcpnQtPluginHost` seam — registerLayer (the built-ins' Layer
      contract + compositor persistence), registerHud / registerSettingsPage
      (QML component URLs with a plugin context object), NavDataProvider
      snapshots and a future NavMsg tap. IID-versioned
      ("org.opencpn.qt.plugin/1.0"). Awaiting the Phase-4 day-1-plugins
      decision before the loader (P4.2) lands; the draft exists to inform
      that decision.
- [x] **P4.2** Expose `registerLayer()` and QML HUD contribution points —
      **DONE (2026-06-10):** `PluginRegistry` (QPluginLoader over the
      app-data `plugins-qt` dir, init/deinit lifecycle, per-plugin enable
      persisted, errors surfaced on the new Options > Plugins catalogue
      page); registered Layers join the compositor like built-ins, HUD
      components load above the chart, settings pages stack under
      Options > Plugins. **Verified end-to-end (2026-06-10)** with the
      opt-in example plugin (`gui/qt/plugin/example/`,
      -DOCPN_QT_EXAMPLE_PLUGIN=ON): it loads from the app-data dir, its
      HUD clock contribution renders and its settings page stacks under
      the catalogue. ~~Recorded constraint~~ **RESOLVED (2026-06-10):** the
      layer toolkit is now a SHARED library (`gui/qt/toolkit/`,
      `libopencpn_qt_toolkit`) the executable links and plugins can link —
      Layer contributions + NavDataProvider consumption work against real
      symbols. Windows export macros land with the P0.5 CI gate. Nothing
      architectural blocks the P4.3–P4.5 ports now.
- [x] **P4.3** Dashboard — **superseded by the built-in HUD (user
      decision 2026-06-10):** the nav pill's rows are picker-driven
      (right-click menu), bus stats (DPT/MTW) parse on the canvas, and
      the compass drags the HUD group. The plugin remains as an API
      reference, OFF by default. Original port notes: — **v1 landed (2026-06-10):** the first
      real Phase-4 plugin (`gui/qt/plugin/dashboard/`, built by default):
      NavDataProvider-fed instrument strip (SOG/COG/HDG/STW/apparent+true
      wind/position), per-instrument visibility on its settings page,
      plugin-side persistence. Verified loading + rendering. **+ comm tap
      (host.navMsgTap) live + depth/water-temp instruments (DPT/MTW)
      landed 2026-06-10, + opt-in compass dials + corner/orientation layout
      options. Parity complete for the wx dashboard's core instrument set;
      exotic instruments (clock/moon/GPS-status) on demand.**
- [x] **P4.4** Port `chartdldr` — **v1 landed (2026-06-10)**
      (`gui/qt/plugin/chartdldr/`, built by default): catalog-XML fetch +
      parse (NOAA schema, ENC product catalog default), per-chart download
      with progress, libarchive ZIP extract into a chosen folder.
      **+ catalog presets (NOAA ENC/RNC, US Inland ENC) and bulk download
      with skip-existing + cancel — 2026-06-10** (the practical core of
      update-checking). **+ per-chart date-based update detection
      and auto-add to the chart library (the new host.addChartDirectory
      seam) — 2026-06-10. Parity complete for the wx feature set short of
      its FTP-era niceties; exercise against the live NOAA catalog.**
- [~] **P4.5** Port `grib` — **wx-parity ANALYSIS complete (2026-06-11,
      from the in-tree grib_pi sources); plan below. Landed so far:**
      decode core compiled as-is; wind barbs (screen-fixed ~42 px,
      zoom-adaptive density) + 2 hPa isobars via the first
      plugin-contributed Layer; 🌬 toolbar icon + on-canvas control bar;
      the GRIB follows the chart TIME BAR (host.timeline seam); cursor
      readout; saildocs request builder; settings page.
      **The wx feature set (canonical, GribSettingsDialog.cpp):**
      13 data types — Wind, Wind Gust, Pressure, Waves, Current,
      Rainfall, Cloud Cover, Air Temp, Sea Temp, CAPE, Composite
      Reflectivity, Altitude (geopotential), Relative Humidity — each
      with per-type display modes (barbed arrows, isolines ± abbreviated
      numbers, direction arrows, colour-mapped OVERLAY raster, numbers,
      PARTICLE animation) and per-type units; wind at 4 altitudes
      (850/700/500/300 hPa); timeline play/speed + interpolation between
      timesteps; cursor data panel for every loaded type; the request
      dialog (model/resolution/days/waves selection); multi-file + zu/
      bz2; weather-routing messaging (→ P4.8 bus).
      **Parity plan, tiered:**
      1. ~~Generalize the layer to N scalar/vector fields~~ **DONE
         (2026-06-11):** waves (dir+height arrows), current (u/v
         arrows), gust/rain/cloud/air+sea temp/CAPE/reflectivity/
         humidity as numbers with unit conversion; per-type toggles on
         the control bar, shown only when the type is present in the
         file; flags persisted. Toolbar right-click + long-press open
         the GRIB preferences (wx parity).
      2. ~~Colour-mapped OVERLAY mode~~ **DONE (2026-06-11):** one field
         at a time as a translucent ramp wash (wind kn ramp / generic
         normalized), grid-res raster + GPU linear smoothing, selector
         on the control bar, persisted.
      3. ~~Interpolation between timesteps~~ **DONE (2026-06-11):**
         scalar + vector (Interpolated2DRecord) interpolation at the
         exact bar time; scrub/play renders continuously. (Play/speed
         buttons live on the app time bar already.)
      4. ~~Particle animation~~ **DONE (2026-06-11):** 600 advected
         particles, fading streaks, 33 ms timer while enabled (off by
         default; control-bar toggle).
      5. ~~Altitude selector + GRIB directory management~~ **DONE
         (2026-06-11):** Open… lists the configured folder newest-first
         (+Browse…); folder picker on the prefs page; wind at
         surface/850/700/500/300 hPa when isobaric levels are present;
         bz2/gz transparent; last-file session restore. Multi-file
         layering (several files merged) remains a refinement.
      6. ~~Request dialog matrix~~ **DONE (2026-06-11):** model/
         resolution/interval selection (GFS/ECMWF/ICON/ARPEGE/NAM),
         GFS-only params auto-limited.
      **ALL SIX TIERS LANDED (2026-06-11).** Since then (2026-06-12):
      per-type **units engine** DONE (kn/m/s/km/h/mph/Bft, °C/°F,
      m/ft, hPa/mmHg/inHg — unitOptions/setUnitFor + activeUnit applied
      to every rendered field and the cursor panel); **cursor-data HUD**
      DONE (its own panel listing every loaded type at chart.cursorLat/
      Lon, toggled from the flyout); toolbar UX to wx parity DONE
      (click = checkable weather on/off with Tides-style shading,
      hold/right-click = MUI-style perpendicular chip flyout: per-type
      toggles, particles, cursor panel, Open-from-folder menu,
      settings); **3-tab Settings dialog** DONE (Data tab reshapes per
      type, Playback, GUI + Request tab; OK/Cancel/Apply staged apply);
      GRIB 0–360 longitude normalization (US files render); timeline
      contract: forecast-step **marks** (clickable green dots), coverage
      **span band**, snap-into-range on load, beyond-forecast layer
      dimming, wheel zoom of the bar's window span.
      ~~wx colormap tables~~ + ~~exact barb conventions~~ **DONE
      (2026-06-12):** `grib_color_maps.{h,cpp}` ports all 10
      GetGraphicColor palettes verbatim (gradual interpolation,
      per-type GetMin/GetMax normalization; reflectivity gets the REFC
      radar palette wx ships but never defaults to) — barbs and the
      overlay wash both colour through it; barbs are the exact wx
      LineBuffer shapes — 14 speed buckets, calm circle, centred 26 px
      staff, integer petite/grande/pennant geometry, southern-
      hemisphere mirroring, 50 px wx spacing. ~~CursorData.cpp
      formatting port~~ **DONE (2026-06-12):** the cursor panel samples
      the time-interpolated records and carries the full wx tracking
      set (wind speed+bf+dir at altitude, gust, pressure, waves
      h-period-dir, current flow-dir, rain/cloud/temps/CAPE/refl/
      humidity, geopotential altitude aloft) at wx precisions through
      the units engine. ~~Multi-file layering~~ **DONE (2026-06-12):**
      one GribReader fed every file (the wx GRIBFile merge), fixups
      once over the merged set, date-union steps; addFile() + a '+'
      per file in the flyout's Open menu; the set persists/restores
      (lastFiles); OCPN_GRIB_FILE takes a ;-separated list. Remaining
      refinements vs wx: fixed/minimum-spacing options, isotachs/
      isotherms beyond isobars, gust-as-barbs variant. Functional
      end-state reached — verify against real forecasts and refine.
- [~] **P4.7** o-charts SHOP plugin — **v1 landed (2026-06-11),
      built by default:** login2/getlist/identifySystem/assign/request
      against the live API (parameters verified against ochartShop.cpp),
      fingerprint via the new host.ochartsService seam, ZIP install +
      keyList placement + auto-add to the chart library. oeRNC sets
      listed but unsupported. **Needs the user's o-charts account for
      end-to-end verification** (login → identify → install → charts
      render); flip to [x] after that pass.
- [ ] **P4.8** **Plugin-API gap list (wx-ABI audit, 2026-06-11; table in
      QT_PLUGIN_API.md):** inter-plugin messaging bus, route/waypoint
      WRITE API (weather-routing-class plugins need it), mouse/keyboard
      input hooks, tide-click hook, object-query hook, colour-scheme
      change signal. Build on demand as porting candidates need them.
- [~] **P4.6** Document the plugin API —
      [`QT_PLUGIN_API.md`](./QT_PLUGIN_API.md) drafted 2026-06-10
      (interface, contribution seams, wx-ABI mapping table, porting
      guidance). The API survived all three first-party ports with only
      additive changes (navMsgTap activation, addChartDirectory) — every
      seam now has a reference consumer. Freeze is a one-line decision
      once the user's Phase-4 review signs off.

## Phase 5 — Embedded / device targets  (est. 4–8 wks)

- [ ] **P5.1** Cross-compile config for an embedded reference board.
- [ ] **P5.2** eglfs / Wayland boot-to-app (no desktop environment).
- [ ] **P5.3** Validate RHI backend selection (Vulkan/GLES) on the target.
- [ ] **P5.4** Touch / input tuning for embedded hardware.
- [ ] **P5.5** Qt for Device Creation packaging / image build.
- [~] **P5.6** **Get wxWidgets out of the `opencpn-qt` link graph**
      (prerequisite for slim embedded builds). **Measured baseline
      (2026-06-10):** 13 gui/qt files touch wx directly; the app layer's
      OWN wx artifacts are now removed (cm93_transcoder de-wx'd) — every
      remaining usage is the **boundary contract** with the two wx-coupled
      dependencies: `libs/s52plib` (wxString APIs, wxPoint2DDouble in
      Extended_Geometry, chartsymbols) and the residual wx surfaces in
      `model/` (BasePlatform, comm bridges' wxString fields) plus the
      vendored GRIB decode core. The project = de-wx s52plib's public API
      (or fork a Qt-typed s52plib-qt), then sweep the bridges
      (QString_to_wxString call sites: chart_canvas 4, route_defaults 5,
      s52_engine 3, others ≤2). Multi-week; sequenced post-P3.11 so the
      wx build doesn't need dual maintenance during it.

## Phase 6 — Post-migration follow-ups (parked)

Deliberately out of scope for the migration; revisit after the wx build
retires. (The "Future / post-Phase-2 follow-ups" capture list — vector S-52
symbols, chart-colour editor — stays where it is in Phase 2.)

- [~] **P6.1** Multi-canvas / split-screen — **experimental v1 landed
      (2026-06-10):** an independent second pane (own viewport + own
      S52Engine + own decode worker; engine constructed only when the
      persisted toggle is on), sharing the chart library / nav data /
      settings. Verified live with both engines decoding concurrently.
      Inspection-only v1; **+ draggable divider with persisted
      fraction + SHARED DECODE SERVICE (one worker/engine for both panes,
      duplicate-arrival guard; live-tested dual-pane) — 2026-06-10.**
      **+ focused-canvas routing (2026-06-10): root.activeChart follows
      focus; zoom, follow/look-ahead and route-build act on the focused
      pane (track/MOB/colour-scheme deliberately global).** **+ cross-pane colour-scheme sync + PER-PANE CONTEXT MENUS AND MARK
      EDITING (targetChart retarget; verified both modes) — 2026-06-10.
      P6.1's wx-parity surface is functionally covered**; residual polish
      (per-pane measure pill placement, editing-overlay variants) as
      verification feedback arrives.

---

## Cross-cutting / ongoing

- [ ] **X.1** Keep Phase 1 changes mechanical (not redesign) to preserve upstream cherry-pick ability.
- [ ] **X.2** Update [`QT_MIGRATION.md`](./QT_MIGRATION.md) when design decisions change.
- [—] **X.3** ~~Maintain the image-diff regression suite~~ — replaced
      (2026-06-10, see P0.7): verify chart rendering visually/manually as the
      renderer evolves; no image-diff suite exists or is planned.
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
- 2026-05-18 — SignalK and SocketCAN parked. NMEA 0183 and NMEA 2000
  (serial gateway + net) cover current needs, so the two un-migrated
  drivers are dropped from the build: `comm_drv_signalk{,_net}.{h,cpp}`
  and `comm_drv_n2k_socketcan.{h,cpp}` are removed from `model/CMakeLists
  .txt` (LINUX list for SocketCAN) but kept in the tree. The factory no
  longer creates either (logs an out-of-scope message); `initIXNetSystem`
  / `uninitIXNetSystem` now call `ix::` directly rather than through the
  SignalK driver; `wiz_ui.cpp` drops its now-unused `comm_drv_signalk_net
  .h` include (its SignalK discovery uses `mdns_query` only). New tracker
  item P1.5m tracks un-parking. Build green: OpenCPN app + tests link.
- 2026-05-18 — P1.5h done: serial-port enumeration rewritten on
  QSerialPortInfo, replacing five platform-specific implementations.
- 2026-05-18 — P1.5j part 1 done: n0183_serial re-homed onto the comms
  framework, bespoke CommDriverN0183Serial deleted, Garmin host mode
  dropped from scope. Surfaced and fixed a latent P1.5b defect — the 0183
  output path (BroadcastNMEA0183Message, Send-to-GPS) downcast every
  driver to the legacy CommDriverN0183 base for GetParams(), so 0183
  transmit to a framework (TCP/UDP/serial) connection hit a null cast.
  Fixed with a ConnectionParamsProvider capability interface. P1.5j part 2
  (n2k_net) deferred: it already works as a Qt-native standalone driver
  and re-homing it is a large, zero-functional-value rewrite of its
  multi-format gateway parsing — better folded into the P1.6 string sweep.
- 2026-05-18 — P1.6a done: the comms pipeline is de-wx'd. Established the
  facade/adaptor pattern for the wxString sweep — a wx-free core (here the
  transport→framer→decoder→driver pipeline) behind a wx-typed boundary
  (ConnectionParams, kept for the GUI + plugin ABI), with an adaptor (the
  factory + ConnectionParams::MakeInputFilter) bridging. New wx-free
  SentenceFilter value type; framework logging moved to Qt. The pattern is
  reusable: P1.6b+ widens the wx-free boundary outward layer by layer.
- 2026-05-19 — P1.6 plan corrected. Investigation confirmed ConnectionParams
  is pinned wxString by the wx GUI (16 gui/ files; it embeds a
  ConnectionParamsPanel*) and config Serialize/Deserialize — not by the
  plugin ABI. So it is the terminal upstream facade and de-wx'es last (gated
  on Phase 3 + P1.9), not next. P1.6 instead advances downstream along the
  data flow: P1.6b de-wx's the decode layer (comm_decoder, comm_bridge,
  ais_decoder), each new facade boundary placed where that layer hands data
  to wx. A read-only agent audit also confirmed the comms data pipeline is
  properly Qt-native (Qt6::Core/Network/SerialPort linked and used; no
  wxSocket/wxThread/wxEvtHandler in the critical path).
- 2026-05-19 — Comms pipeline retrofitted to Qt-idiomatic types. Earlier
  framer/decoder entries describe them as "pure C++ / no Qt / unit-testable
  without hardware" — that was an assistant assumption, never a project
  rationale, and is now dropped. `CommFrame` is `QByteArray`; framers,
  decoders, `CommDriver` and `N2kGatewayManager` use `QByteArray`/`QList`/
  `QSet`; `SentenceFilter` uses `QRegularExpression`. Direction going
  forward: actively prefer Qt over std/pure-C++ wherever Qt offers a way;
  the goal is a fully Qt application. `QT_MIGRATION_COMMS_ARCH.md` §3.2-3.3
  / §4 updated accordingly.
- 2026-05-19 — P1.6b in progress: `comm_decoder` and `comm_bridge` de-wx'd.
  The decode layer's wxString sweep follows the data flow — `comm_decoder`
  (NMEA/N2K/SignalK → `NavData`) then `comm_bridge` (the `NavData` →
  global-nav-state stage). `comm_bridge` is converted Qt-first, not merely
  de-wx'd: per the post-`71721d90a` direction (actively prefer Qt over
  std/pure-C++), the priority-source machinery's representation is now Qt —
  `PriorityMap` is `QHash<QString,int>`, `PriorityContainer`'s string fields
  are `QString`. Watchdog logging is `qInfo`, timestamps `QDateTime`.
  `std::string` is kept deliberately, and only, at the two boundaries that
  pin it: the plugin ABI (`GetPriorityMaps`/`UpdateAndApplyPriorityMaps`/
  `GetActivePriorityIdentifiers`, exported via `ocpn_plugin.h` — a fixed
  signature, adapted at the call site in `ocpn_plugin_gui.cpp`) and config
  persistence (`Load/SaveConfig`, P1.9). Other remaining wx in `comm_bridge`:
  the `wxWindow` data-monitor GUI lookup and a `wxString` hand-off to the
  still-wx AIS parser. `ais_decoder` (~117 refs, and that AIS-parser
  boundary) is the remaining P1.6b file.
- 2026-05-19 — P1.6b complete: `ais_decoder` de-wx'd, closing the decode
  layer. The 4.7k-line AIS decoder had wxString woven through all the
  NMEA/N2K/SignalK sentence parsing; all of it → `QString`, with
  `wxStringTokenizer` → `QString::split`. One non-obvious call: NMEA fields
  are positional and routinely empty, and a `wxStringTokenizer` on a
  non-whitespace delimiter keeps empty tokens — so the splits use
  `Qt::KeepEmptyParts`, not `SkipEmptyParts`, to preserve field offsets.
  `DecodeSingleVDO` is now `QString`-based, so the `wxString` hand-off
  `comm_bridge` bridged to (and the `comm_ais` free-function wrapper +
  `ocpn_plugin_gui` plugin-ABI shim) is closed. `wxString` survives at the
  GUI/config boundary types (`MmsiProperties`, the AIS name-file API) and in
  `wxString`-typed members of headers not yet migrated (`ais_target_data.h`,
  `meteo_points.h`); `wxDateTime`/`wxTimer`/`wxTextFile` stay for their
  dedicated phases (P1.7/P1.11/P1.10). The decode layer now runs Qt-typed
  end to end: bytes-in → nav-data-out.
- 2026-05-19 — P1.6c starts; strategy shift recorded. The decode layer had a
  wx-free facade to hide behind; the layers below it (`AisTargetData`,
  `Route`, …) do not — these types are shared with the wx GUI and their
  `wxString` surface *is* the boundary. Decision: convert them *through* —
  the model type goes `QString`, wx GUI call sites are adapted now (local
  `wxString`⇄`QString` conversions at each wx widget call), rather than
  parking the type wx-typed until Phase 3. First file: `ais_target_data` —
  `AisTargetData` fully `QString` (7 display-string methods, 5 free
  functions, 3 data fields), ~8 wx GUI consumers adapted. wx⇄Qt string
  conversions use UTF-8 explicitly (`wxString::FromUTF8` / `utf8_string()`),
  not the locale-dependent default ctors, so translated/non-ASCII text
  round-trips correctly. `wxDateTime` and the `_()` macro stay (P1.7/P3.10);
  a `FromWx()` helper bridges `_()` results into `QString`.
- 2026-05-19 — P1.6c: the route/waypoint model (`route`, `route_point`)
  de-wx'd. `Route` and `RoutePoint` are `QString` throughout — GUIDs, names,
  descriptions, icon names, the colour-name field, the `GpxxColorNames[]`
  table, all method params/returns; `wxArrayString` → `QStringList`. ~30
  consumer files across `gui/` and `model/` adapted at the call site (the
  ripple of `m_GUID` / `GetName()` etc.). Added `model/wx_qt_string.h` —
  shared inline UTF-8 converters `wxString_to_QString`/`QString_to_wxString`;
  the whole migration's wx⇄Qt string conversions now route through these
  (explicit UTF-8, never a locale-dependent default ctor). Deferred and left
  untouched: `wxDateTime` (P1.7), the GUI-drawing types `wxColour`/
  `wxBitmap`/`wxPen`/`wxRect`/… and `Route`'s `wxObject` base (P1.14 /
  later), the `_()` macro (P3.10). `track`, `routeman` and the nav object DB
  (`nav_object_database`, `navobj_db`) were adapted only at their
  `Route`/`RoutePoint` call sites — their own de-wx is a later P1.6c step.
- 2026-05-19 — P1.6 closes: `track`, `routeman` (+ `WayPointman` /
  `MarkIcon`), and the nav-object DB (`nav_object_database`, `navobj_db`)
  de-wx'd in three steps. The route/waypoint/track data model and its
  persistence layer now run Qt-typed end to end. Highlights: `MarkIcon`
  migrated alongside `WayPointman` because `GetIconKey`/`GetIconDescription`
  hand out raw pointers into the struct, so the fields had to flip with the
  return types; pugixml in `nav_object_database` is fed via
  `qs.toUtf8().constData()`; the stop-gap file-local `ws2qs`/`qs2ws`
  helpers earlier route/route_point steps had added to other files are
  deleted, everything now routes through the shared
  `model/wx_qt_string.h`. The full P1.6 (model wxString sweep) is done;
  `wxDateTime`/`wxTimer`/`wxBitmap`/`wxFileName`/`wxJSONValue`/`_()` etc.
  remain for their dedicated phases (P1.7 / P1.11 / P1.14 / P1.10 / P1.12 /
  P3.10).
- 2026-05-20 — P1.7 done: the model `wxDateTime`/`wxTimeSpan` sweep is
  closed in 6 steps. `QDateTime` is the time vocabulary; durations are
  `qint64` (seconds unless commented). The strftime↔Qt format-code gap is
  bridged by a `StrftimeToQtFormat` helper in `model/datetime.cpp`; ISO 8601
  uses `Qt::ISODate`; locale-dependent formats go via `QLocale::system()`.
  Wire/on-disk formats preserved (SQLite Unix seconds, GPX/KML ISO 8601 +
  `Z`). The wx 3.0.2 `ToGMT()`-with-DST workaround in tides is dropped —
  Qt's `offsetFromUtc()` / `isDaylightTime()` are correct. Remaining
  `wxDateTime`/`wxTimeSpan`/`wxLongLong` references are all in deliberate
  boundary buckets: the frozen plugin ABI (`PlugIn_*` types, the
  `pluginmanager` and `api_121` / `ocpn_plugin_gui` bridges,
  `toUsrDateTimeFormat_Plugin`), `wxDatePickerCtrl`/`wxTimePickerCtrl`
  pickers (swept with the GUI pickers in P1.10), `wxFileName::Get*Time()`
  callers (P1.10), and the `time_textbox.h` `wxTimePickerCtrl`-compat shim
  (also P1.10).
- 2026-05-20 — P1.8 done: the model's wx-container sweep is closed in 3
  steps. `QStringList`, `QList<T*>`, `QHash`/`QSet` are the container
  vocabulary throughout. The strategy was Qt-first with std only at facade
  boundaries where Qt isn't viable (none arose; Qt always sufficed).
  Notable header typedef changes hit hard once and ripple cleanly:
  `ArrayOfPlugIns`, `ArrayOfMarkIcon`, `ArrayOfMmsiProperties`,
  `AIS_Target_Name_Hash`, `ChartGroupArray`, `ArrayOfCDI`,
  `arrayofCanvasPtr`, plugin-menu/toolbar/panel arrays, and the styles /
  font_mgr / cm93 / o_senc internals. `GetRouteArrayContaining` returns
  `QList<Route*>*` (strongly typed — callers were already casting). The
  `compress_target` heap-pointer wxObjArrays in `ocpn_frame` /
  `gl_texture_mgr` are rewritten to `QList<compress_target>` by value
  (ownership semantics preserved -- nothing held outside pointers).
  Remaining 168 wx-container hits are all in deliberate boundary buckets
  (frozen plugin ABI, `wxDir`/`wxFileName` callers for P1.10,
  pure-wx-widget-fill locals, `wxExecute`/`wxLocale`/Android Bluetooth
  APIs, S52PLIB-owned `wxArrayPtrVoid`, `wxAUI`/`wxListCtrl` API). Five
  container families with non-trivial ownership semantics (cm93's
  `Array_Of_M_COVR_Desc` + `List_Of_M_COVR_Desc`, `PatchList`,
  `ArrayOfStationData`, `ArrayOfTCDSources`, `ArrayOfIDXEntry`,
  `SortedArrayOfMarkIcon`) are deferred to a dedicated ownership pass --
  their contained types own raw resources without copy ctors, so naïve
  wxObjArray → QList migration would double-free.
- 2026-05-20 — P1.9 done: wxConfig/wxFileConfig is replaced with
  `OcpnConfig` (wraps `QSettings(IniFormat)` at the same on-disk path so
  user configs keep loading). The class exposes a pure Qt-idiomatic API
  (`value`/`setValue`/`beginGroup`/`endGroup`/`endAllGroups`/`group`/
  `contains`/`remove`/`childKeys`/`childGroups`/`sync`); the wx-style
  `Read`/`Write`/`SetPath` etc. shim methods used during the transition
  were deleted in step 3 once every caller had been swept. `MyConfig` in
  the GUI now inherits from `OcpnConfig`; `TheBaseConfig()` /
  `InitBaseConfig()` traffic in `OcpnConfig*`. The big GUI consumers
  (navutil, ocpn_platform, ocpn_frame, pluginmanager, wiz_ui, ...) call
  through `gui/config_compat_helpers.h` (`ocpn_cfg::Cfg{Read,Write,...}`
  free functions over the Qt API), which keeps the diff one-liner-per-
  site and centralises wxString⇄QString conversions — a future cleanup
  can drop those helpers when each file is fully QString-typed. Plugin
  ABI: `GetOCPNConfigObject()` (declared `wxFileConfig*` in
  ocpn_plugin.h) keeps its frozen signature but returns nullptr with a
  TODO; the one internal caller is rewired through `TheBaseConfig()`.
  `ConfigVar<T>` was moved out of `libs/observable/src` into
  `model/src` so its `OcpnConfig` instantiation stays in the right
  layer.
- 2026-05-20 — P1.10 done: path / dir / file I/O is Qt-typed throughout
  (`QFile` / `QDir` / `QFileInfo` / `QStandardPaths` / `QTextStream` /
  `QDirIterator`). The wx file-I/O API in the model and most of the GUI
  is gone. macOS deployment target moved 10.13 → 10.15 on the
  `_model_src` and top-level `OpenCPN` targets because Qt 6's file-I/O
  headers expose `std::filesystem::path` overloads guarded by libc++
  10.15+ availability attributes; Qt 6 itself already needs 10.15+ to
  link, so this is consistent. Two deliberate boundary buckets remain:
  (a) the chart-reader `wxInputStream`/`wxOutputStream` interfaces and
  their consumers (chartimg / chartdbs / cm93 / o_senc / s57chart and
  the `chartdata_input_stream` wrappers), which feed raw-byte parsers
  for the on-disk chart binary formats — a chart-reader stream refactor
  separate from "path/file/dir manipulation"; (b) `wxStandardPaths` in
  `base_platform.cpp` for macOS bundle paths, where Qt's
  `QStandardPaths::ConfigLocation` resolves to a different directory
  (`~/Library/Application Support` vs wx's `~/Library/Preferences`),
  so existing user configs would move — left wx-typed with a code
  comment. The wxString-typed `wxStandardPaths& GetStdPaths()` plugin-
  ABI accessor stays in the same bucket. `wxFileName::CreateTempFile
  Name` (no Qt equivalent that returns a path) was replaced everywhere
  with a `QStandardPaths::TempLocation` + pid + msec-since-epoch
  uniquification helper. `wxTempFile + Commit()` (atomic replace)
  became `QFile`-to-tmp + `QFile::remove(orig)` + `QFile::rename(tmp,
  orig)`. `wxTextFile` line-by-line readers became `QFile + QTextStream
  ::readLine` loops. Plugin-ABI shims (`GetWritableDocumentsDir`,
  `GetExePath`, `GetRoutepointGPX`, etc.) keep their `wxString` returns
  but are Qt-typed internally.
- 2026-05-20 — P1.11 done: threading primitives + standalone (non-
  `wxWindow`) `wxTimer`/`wxEvtHandler`/event-table use is Qt throughout.
  `QThread` / `QMutex` / `QSemaphore` / `QWaitCondition` / `QTimer` /
  `QElapsedTimer` / `QObject` with `Q_OBJECT` + `Q_SLOTS:` /
  `Q_SIGNALS:` / `Q_EMIT` is the threading + event-loop vocabulary.
  `wxStopWatch` → `QElapsedTimer`. Notable patterns established:
  `wxTHREAD_DETACHED` self-cleanup → `connect(this, &QThread::finished,
  this, &QObject::deleteLater)` in the QThread subclass ctor;
  cross-thread `wxPostEvent` / custom `wxEvent` subclass → either a Qt
  signal with `Qt::QueuedConnection` or `QMetaObject::invokeMethod(...
  Qt::QueuedConnection, Q_ARG(T, value))` for one-off worker→consumer
  delivery; wxConfig-style `wxTimer::SetOwner` + `EVT_TIMER` event-
  table → `connect(&timer, &QTimer::timeout, this, &Class::OnFoo)` in
  the ctor; `RestServer`'s IO-thread → main thread CV-wait handoff
  uses `Qt::DirectConnection` (the wx code ran the handler in-line on
  the IO thread via `wxTheApp->ProcessPendingEvents()`; queued
  delivery would deadlock the blocked main thread). All `Q_OBJECT`
  classes pick up moc via the model target's existing `AUTOMOC ON`.
  In `gui/` ~199 `wxTimer`/`wxEvtHandler` hits across 42 files remain
  — all are members of `wxWindow`/`wxFrame`/`wxDialog`/`wxAuiManager`
  subclasses or non-window wx event handlers
  (`pluginUtilHandler`/`CanvasMenuHandler`/etc.); these migrate with
  the QtQuick port in Phase 3. `libs/wxcurl` + `libs/wxservdisc` (21
  hits) are deferred to P1.13.
- 2026-05-20 — P1.12 done: JSON usage is `QJsonDocument` / `QJsonObject`
  / `QJsonArray` / `QJsonValue` throughout. `libs/wxJSON` stays in the
  build for one shim: `GetSignalkPayload` (model/src/plugin_api.cpp)
  contractually returns a `std::shared_ptr<void>` pointing at a
  `wxJSONValue` per the frozen plugin ABI (`include/ocpn_plugin.h`).
  The shim builds the payload as a `QJsonObject` internally and
  double-encodes via `QJsonDocument::toJson(Compact)` →
  `wxJSONReader::Parse` to produce the `wxJSONValue` plugins expect.
  All other live wxJSON code is gone. Behaviour delta: Qt's parser
  has no warnings (only errors), so the SignalK payload's
  `WarningCount` is always 0 and `ErrorCount` is 0 or 1. The on-disk
  filter files (`data/filters/all-nmea.filter.json` etc.) are missing
  their closing `}` — legacy `wxJSONReader` tolerated this; `Navmsg
  Filter::Parse` now retries with up to 4 appended `}` to keep the
  shipped data loading. `OCPN_ROUTELIST_RESPONSE` and `OCPN_ACTIVE_
  ROUTELEG_RESPONSE` are array-rooted plugin messages; a second
  `SendJSONMessageToAllPlugins(QString, QJsonArray)` overload was
  added to preserve their wire format. `OCPN_ROUTELIST_RESPONSE`'s
  index-from-1 slot-0-null quirk preserved with
  `arr.append(QJsonValue())` before the loop.
- 2026-05-20 — P1.13 done: `libs/wxcurl` is gone (33 files removed)
  and networking is `QNetworkAccessManager` end to end. The model's
  three direct-libcurl users (`downloader`, `mdns_cache`,
  `peer_client`) use QNAM with a local `QEventLoop` to preserve their
  synchronous "return-the-body" API shape (these are non-GUI worker
  paths; nesting an event loop is fine). The GUI's wxcurl machinery
  in `pluginmanager` (modal `wxCurlDownloadDialog` + threaded
  `wxCurlDownloadThread` + sync `wxCurlHTTP`) collapsed onto a single
  QNAM with `QNetworkReply` signal/slot wiring. `PlugInManager` is
  now `: public QObject, public wxEvtHandler` (QObject first per the
  established multi-inherit pattern) with `Q_OBJECT`; AUTOMOC enabled
  on the OpenCPN target. The modal-progress UI is `wxProgressDialog`
  driven by a `QTimer`-pumped wx-yield loop alongside a `QEventLoop`
  on `QNetworkReply::finished` — `QProgressDialog` would have pulled
  in `QtWidgets` for a single dialog. The `OCPN_USE_CURL` config
  macro stays (still gates the plugin-API download surface) but is
  now a misnomer for "download support enabled". CMake plumbing —
  `add_subdirectory(libs/wxcurl)`, `pkg_search_module SYS_WXCURL`,
  `use_bundled_lib USE_BUNDLED_WXCURL`, the bundled libcurl headers,
  Android `libcurl.a`, and the `s57` and `cli` link entries — all
  deleted. `libs/wxservdisc` (mDNS service discovery) stays for now —
  it's a separate concern from "HTTP networking" and would need
  routing through `libs/mdns` (we already have it vendored) or a Qt-
  native zeroconf solution. Listed in the deliberate-boundaries
  ledger.
- 2026-05-20 — P1.14 done: route/mark UI types abstracted in the model.
  `QColor` / `QPen` / `QBrush` / `QImage` / `QFont` replace `wxColour`
  / `wxPen` / `wxBrush` / `wxBitmap` / `wxFont` across `routeman` /
  `route` / `route_point` / `MarkIcon`. The wider GUI rendering code
  (chcanv, ais, options, etc.) stays wx — that's Phase 3 / QtQuick
  port territory. Added `model/include/model/wx_qt_ui_types.h` —
  header-only bridge helpers (`QColorToWxColour` / `WxColourToQColor`
  / `QImageToWxImage` / `QImageToWxBitmap` / `WxImageToQImage` /
  `WxBitmapToQImage` / `QPenToWxPen` / `QBrushToWxBrush` /
  `QFontToWxFont`) for the model⇄wx-GUI boundary, joining
  `wx_qt_string.h` and the implicit `QDateTime`↔`wxDateTime` time-t
  bridges. GUI consumers (route_point_gui, route_gui, track_gui,
  chcanv, ais, mark_info, options, navutil, ocpn_plugin_gui, api_121)
  adapt at the call site using these helpers. Two preserved legacy
  semantics worth noting: `RoutePoint::m_pMarkFont` was a `wxFont*`
  whose null value meant "not yet loaded" — replaced by a `QFont`
  value member plus a parallel `bool m_MarkFontInitialized` flag;
  `MarkIcon::piconBitmap` stays a heap pointer (`QImage*`, owned by
  `WayPointman`) so the same lazy-build semantic continues to work.
  CMake adds `Qt6::Gui` publicly to the model target (its headers now
  consume `QColor`/`QPen`/`QBrush`/`QImage`/`QFont`).
- 2026-05-20 — **Phase 1 complete (P1.15 verification).** Build clean,
  58/59 tests pass (the `DateTimeFormatTest.LocalTimezoneCETSwedish`
  failure is a pre-existing locale-test issue from before P1.6). Model
  layer programming model is `QString` / `QStringList` / `QList<T*>` /
  `QHash` / `QSet` / `QDateTime` / `qint64`-seconds / `OcpnConfig` /
  `QFile` / `QDir` / `QFileInfo` / `QStandardPaths` / `QThread` /
  `QMutex` / `QSemaphore` / `QTimer` / `QElapsedTimer` / `QObject`
  signals & slots / `QJsonDocument` / `QNetworkAccessManager` /
  `QColor` / `QPen` / `QBrush` / `QImage` / `QFont`. Remaining `wx*`
  in `model/` is entirely in documented deliberate-boundary buckets
  (plugin ABI, library-API streams, wxLog logging infrastructure,
  macOS bundle paths, GUI-bridge code in model files, comments).
  Strict "core compiles wx-free" is blocked by the frozen plugin
  ABI's `ocpn_plugin.h` (200+ exported `wxString` / `wxArrayString` /
  `wxDateTime` / `wxJSONValue` / `wxBitmap` / `wxColour` /
  `wxEvtHandler*` references) — orphaning every existing plugin to
  achieve that was never the goal. Phase 1's substantive objective is
  met: the model is Qt-typed throughout, with explicit, documented wx
  boundaries. Bridge headers (`model/wx_qt_string.h`,
  `model/wx_qt_ui_types.h`, the `QDateTime`↔`wxDateTime` epoch
  round-trips, `gui/config_compat_helpers.h`) centralize the small
  surface of conversions remaining. Next: Phase 2 — the scene graph +
  `LayerCompositor` chart-rendering port to QtQuick.
- 2026-06-01 — Started closing the persisted-only settings tail + the alert
  engine (two threads chosen after a migration-state review). Landed: (1) AIS
  predictor length / "sync with own ship" / show-names now consumed live by
  `AisLayer`, with an `AisConfig`/`DisplayConfig`-change rebuild; (2) own-ship
  **range rings** drawn by `OwnShipLayer` as cos(lat)-scaled world circles; (3)
  **P3.15 AIS CPA/TCPA alert** — new `AlertEngine` (`gui/qt/alert_engine.*`,
  `chart.alerts`) fed the CPA-enriched target list each tick, raising a pulsing
  `Main.qml` banner + the user's AIS sound, with per-MMSI ack/hold-off state.
  All build clean; `opencpn-qt` launches with no QML errors. Remaining P3.15:
  anchor watch, SART/DSC, ship's bells. Remaining tail: AIS tracks/lost-target
  timeouts/display filters, own-ship real-scale icon, vector chart style
  options (graphics/boundary/colour/contours).
- 2026-06-01 (cont.) — **P3.15 finished.** Added the three remaining alert
  sources to the `AlertEngine`: (a) **anchor watch** — drop/raise/radius +
  ConfigStore persistence, a cos(lat)-scaled `AnchorWatchLayer` circle (amber→
  red on breach), and a ⚓ MUIBar popup control; (b) **SART/DSC distress** —
  new `AisTarget.isSart/isDsc` populated from `AisTargetData::Class` in
  `mirrorTargets`, distress banners unconditionally and outranks CPA, sound via
  `UIConfig.sart/dscSoundFile`; (c) **ship's bells** — re-arming half-hour
  `QTimer` striking 1–8 bells from `data/sounds/{1,2}bells.wav` (new
  `OCPN_QT_SOUNDS_DIR`), gated on `UIConfig.playShipsBells`. Banner priority:
  SART/DSC distress > anchor breach > AIS CPA. All build clean; launches with no
  QML errors. Deferred polish: distinctive SART/DSC chart icons, an Options
  anchor-watch panel.
- 2026-06-01 (cont.) — **Thread-3 settings tail.** (1) Found the **vector chart
  style options** (graphics/boundary/2-4-colour/contours) were already live via
  `applyChartConfig` → `S52Engine::applyDisplaySettings` — the `[p]` inventory
  note was stale; corrected. (2) **AIS lost-target timeouts** wired:
  `ChartCanvas::syncAisModelGlobals` pushes `AisConfig.markLostMin`/`removeLostMin`
  /`suppressAnchoredSpeedMax` into the reused model decoder's globals
  (`g_bMarkLost`/`g_MarkLost_Mins`/`g_bRemoveLost`/`g_RemoveLost_Mins`/
  `g_ShowMoored_Kts`), declared as externs locally to avoid the wxString-laden
  `ais_state_vars.h`. (3) **Own-ship real-scale icon**: `OwnShipLayer` swaps the
  fixed marker for a to-scale, heading-oriented hull (world units, cos(lat),
  GPS-offset) when the icon type is real-scale and it's big enough on screen
  (minScreenSize floor). All build clean; launches with no QML errors. Tail
  still open: AIS display filters (names done; tracks need AIS-track rendering),
  rollover info block, waypoint-direction arrow (needs an active-waypoint
  accessor), CM93 (P2.19).
- 2026-06-01 (cont.) — **AIS trails (target tracks).** Global, persistent,
  timeline-ready. New append-only `ais_track(mmsi,t,lat,lon,cog,sog,hdg)` table
  in `SqliteAisTargetStore`: records every moved fix (deduped, batched on the
  prune flush), `trackSince(mmsi,since)` query, time-based purge past
  `AisConfig.trackRetentionDays` (default 7, configurable; indexed on `t`).
  Exposed via `NavDataProvider::aisTrack` (model forwards to the store;
  switchable delegates; demo empty). `AisLayer` draws a trail **only for
  user-selected+toggled vessels** (a mess otherwise): a 2px light-grey AA-line
  seeded from SQLite over the last 5× the predictor reach, then slid live each
  tick (new point on the front, expired points off the back). UI: a "Show
  trail" check in the AIS info popup (`ChartCanvas::setAisTrail`) + a retention
  spinbox in Options. **COG/SOG/HDG stored, not derived** (per the design
  call): heading is underivable from position and reported COG/SOG beat noisy
  deltas, so a historical target can later be redrawn with its true orientation
  + speed — the groundwork for scrubbing the timeline to "unwind" the AIS scene
  (the same `gTimeSource` spine tides/currents follow). Schema auto-migrates an
  existing 4-column table via ALTER. Builds clean; `ais_track` verified created
  + migrated in `navobj.db`; app launches with no QML errors. **Not yet done:**
  the timeline-scrub replay itself (the layer reading `gTimeSource` to query
  positions at a past time) — recording is in place to enable it next.
- 2026-06-02 — **Route manager redesign (P3.7).** Replaced the modal
  "Routes & marks" window (flat list + a clumsy centred rename dialog) with a
  **left-edge `Drawer` of route tiles**. Each tile shows the route name + stats
  (length in NM · legs), clicking the tile body selects + zooms to the route's
  extent (`ChartCanvas::showRoute(index)` = select + fitBounds), and a `⋯`
  overflow menu holds **Rename (inline TextField in the tile) / Duplicate /
  Reverse / Delete**. Backend additions: `RouteListViewModel` route maps gain
  `lengthNm` (haversine sum); `SwitchableNavDataProvider::duplicateRoute` clones
  a route's points into a new "<name> copy" (persisted); `ChartCanvas` gains
  `duplicateRoute` + `showRoute` invokables. Layer toggles (Routes/Tracks/Marks)
  + a compact marks list kept in the drawer. The toolbar ▤ button toggles the
  drawer. Builds clean; QML compiles. **Visual check blocked** (machine locked
  at the time) — built/compiled only, not eyeballed.
  Refinements (same day, after user review): layer switches restored to normal
  (default) size; an explicit **edit mode** — the tile `⋯` menu gains "Edit"
  (`ChartCanvas::editRoute` = show + `routeEditMode` on), and node-drag /
  leg-insert / node-delete are now gated behind `m_route_edit_mode` (so a plain
  tile click only views/zooms — no accidental node drags); a top-left "Editing
  route … Done" banner reflects + exits the mode (deselecting also exits).
  **Visibility model (visibility ≠ selection):** each tile has a per-route
  visibility **"eye"** (`ChartCanvas::routeVisible`/`setRouteVisible`, keyed by
  GUID in `m_visible_routes`, pushed to `RouteLayer::setVisibleRouteGuids`); a
  route draws when its eye is on **OR** it is the selected route. Default: eyes
  off, so only the selected route shows (clicking a tile selects + shows + zooms
  without changing its eye). `routeVisibilityRevision` (NOTIFY) keeps the QML
  eye bindings reactive. Replaces the earlier auto-solo. Built/ran clean (no QML
  errors). Next: execute/follow a route.
- 2026-06-02 — **Marks + tracks management (P3.7), 3 phases.** The drawer became
  a tabbed manager: **Routes | Marks | Tracks** (`TabBar`+`StackLayout`; master
  Tracks/Marks switches removed — per-item eyes replace them).
  **Marks (free waypoints):** `NavWaypoint` gained guid/comment/icon/visible/
  createTime; `SwitchableNavDataProvider` got `dropMark`/`rename`/`setComment`/
  `setIcon`/`setVisible`/`deleteWaypoint` (all via `NavObj_dB` Insert/Update/
  Delete RoutePoint, `m_bIsolatedMark`); `ChartCanvas` invokables
  (`dropMarkHere`/`showMark`/`set*`/`delete`/`markIconNames`);
  `RouteListViewModel.waypoints` enriched + a **Recent/Nearest sort** (haversine
  range from own ship). Drop is right-click "Drop mark here" → a **New Mark
  dialog** (name + comment + **visual icon picker**); the same dialog edits.
  Icons are surfaced to QML via a new `WaypointIconProvider`
  (`image://wpicon/<key>`), and `WaypointLayer` now renders the chosen icon
  (was a dot) + per-mark eye + cyan selection ring; fixed a latent
  plain-`-lat` (non-Mercator) mark positioning bug.
  **Tracks (own-vessel):** `NavTrack` gained name/guid/length/startTime/visible/
  active; reworked the recording lifecycle — `startTrack` creates a dated
  ActiveTrack (`#n` suffix for same-day) in `g_TrackList` + `InsertTrack`,
  `stopTrack` finalizes (discards <2 pts), `resetTrack` = stop+start (new tile),
  plus `rename`/`delete`/`setVisible`; `TrackLayer` honours per-track eye +
  selection; `RouteListViewModel.tracks` newest-first. Tracks tab has
  Start/Stop/Reset + tiles (name, length, ● REC badge, eye, rename, delete).
  All build + run clean (no QML errors). **Deferred:** the trk_points schema
  convergence (add cog/sog/hdg + integer-epoch time) — a model-DB change +
  migration to do as a focused, separately-verified step.
- 2026-06-10 — Full-migration scoping review (docs vs code vs wx feature
  surface). Tracker realigned: P0.1 + P3.1–P3.4 marked done, P3.16 retro-added
  (RouteFollower console etc. shipped in 48cfdf234 without a tracker entry),
  image-diff tasks P0.7/P2.0/X.3 closed as dropped-by-decision (visual
  verification — intentional style divergence makes pixel diffs all false
  positives). New tasks: P3.19 GPX import/export UI, P3.20 app-wide keyboard
  shortcuts, P3.21 wx-retirement acceptance checklist (gates P3.11), P3.22
  Data Monitor launcher → Connections page. Decisions: macOS-first (P0.5
  Linux/Windows CI is a pre-P3.11 gate); multi-canvas out of scope → new
  Phase 6 "post-migration follow-ups" (P6.1).
- 2026-06-12 — **THE GRIB-scrub saga root-caused: split-brain QML
  singletons.** Symptom: scrubbing the time bar never animated the GRIB
  layer, no green step-dots/coverage band — while every programmatic
  check passed. Root cause (proven with lldb instance-identity
  sampling): Qt's `singletonConstructionMode()` prefers
  default-construction over the `create()` factory whenever the class
  is `std::is_default_constructible`, so the QML engine silently built
  its OWN `TimeController` — the bar drove one instance, the plugins/
  layers/self-tests another. Undetectable statically: both brains load
  identical persisted state; only LIVE cross-boundary changes vanish.
  Fix: constructor moved to `private:` in all 9 QML_SINGLETON classes
  (TimeController + the Config singletons, SoundPlayer,
  CommPrioritiesModel), forcing the factory path; SoundPlayer gained
  the missing instance()/create() pair. Rule recorded in
  QT_TOOLCHAIN.md: **every QML_SINGLETON declares its ctor private**.
  Verified post-fix: a single onTick `this` address; the GRIB self-test
  (`OCPN_GRIB_SELFTEST=1`) walked the real bar through 11 pushes with
  changing interpolated-grid fingerprints (meanU 6.221→…→4.147), zero
  errors. Collateral win: every live Options→C++ flow on those
  singletons (grid toggle, tide layer, S-52 re-decode, sound device…)
  was silently dead and is now restored — re-verify in the next manual
  pass. **User-verified hands-on (2026-06-12): "Timebar is now
  working" — the scrub saga is CLOSED.** The diagnostics are retired:
  timebar qWarnings removed; the GRIB step dump + PUSH/REBUILD meanU
  fingerprints now gate behind OCPN_GRIB_SELFTEST (they are the
  self-test's assertions).
- 2026-06-12 — **CI repair + wx-workflow deletion (user direction: "don't
  need the wx builds anymore in GH").** Every push was flooding failure
  email: `test-clang-format` (an upstream wx-era check over model/gui/
  plugins) failed on all 20 pushes, and the `opencpn-qt` macOS job had
  been failing **since 2026-06-10** — the dashboard plugin was gated off
  (built-in HUD supersedes it) but the workflow still built its target.
  All wx-era workflows are deleted (clang-format-check, linux, MacOS,
  windows, doxygen, zulip); `opencpn-qt.yml` is the fork's CI, its
  plugin-target list now matches the default-ON set (+ o-charts shop),
  and the best-effort Linux job gains libglew-dev. Process note: my
  background run-watches had a wrapper bug that swallowed exit codes, so
  earlier "CI green" reports today were WRONG — verification now uses
  the bare `gh run watch --exit-status` + a `gh run list` cross-check.
