# OpenCPN → Qt/QtQuick Migration — Task Tracker

> Companion to [`QT_MIGRATION.md`](./QT_MIGRATION.md). That doc holds the
> design rationale; this one tracks execution. Update checkboxes and the
> **Current position** line as work proceeds.

**Current position:** P1.5 comms migration done (P1.5a/b/d/e/f/h/i, P1.5j-1);
the comms pipeline is on the framework and, as of P1.6a, wx-free behind the
`ConnectionParams` facade. `n2k_net` stays the standalone P1.5a driver;
SignalK/SocketCAN parked (P1.5m). P1.6, P1.7 and P1.8 done — the model's
`wxString`, `wxDateTime`/`wxTimeSpan` and wx-container sweeps are all
complete. `QStringList`, `QList<T*>`, `QHash`/`QSet` are the container
vocabulary; `model/wx_qt_string.h` (UTF-8) centralizes wx⇄Qt string
conversions; `QDateTime`/`qint64`-seconds is the time/duration vocabulary.
Remaining wx-typed references are deliberate boundaries — the frozen
plugin ABI, `wxDir`/`wxFileName` callers (P1.10), wx-widget plumbing,
`wxList`-node container types owning raw resources (deferred ownership
pass), and a handful of external library boundaries (S52PLIB color/ATON
arrays, GLU tesselator). Next: P1.9 (`wxConfig`/`wxFileConfig` →
`QSettings`).
**Last updated:** 2026-05-20.

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
  - [~] **P1.5c** *(parked — see P1.5m)* `signalk_net` on the framework —
        `WebSocketTransport` (`QWebSocket`) + `PassThroughFramer` +
        `SignalKDecoder`; retires the vendored `IXWebSocket` for this driver.
  - [~] **P1.5g** *(parked — see P1.5m)* `n2k_socketcan` on the framework —
        `CanTransport` (`QCanBusDevice`) + `PassThroughFramer` + `N2kDecoder`.
        Replaces the raw `PF_CAN` socket / `ioctl` / `Worker` thread. Backend
        by name — `socketcan` (Linux, real HW) or `virtualcan` (macOS).
  - [ ] **P1.5m** *(revisit)* Un-park SignalK and SocketCAN. Both legacy
        drivers (`comm_drv_signalk{,_net}.{h,cpp}`, `comm_drv_n2k_socketcan
        .{h,cpp}`) are kept in the tree but **excluded from the build** and
        unreachable from the factory — NMEA 0183 + NMEA 2000 (serial &
        net) cover current needs. Revisit deletes the parked sources once
        P1.5c and P1.5g land, or sooner if either protocol is needed.
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
