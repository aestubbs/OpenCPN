# OpenCPN-Qt Plugin API (draft 1.0)

Status: **interface drafted (P4.1), loader live and VERIFIED (P4.2 — the
example plugin loads, its HUD clock renders and its settings page
appears)** — awaiting the
Phase-4 "day-1 plugins" decision before the first module ports
(P4.3–P4.5). IID: `org.opencpn.qt.plugin/1.0` — any breaking change bumps
it.

This is a **clean break** from the frozen wx plugin ABI
(`include/ocpn_plugin.h`). Qt plugins are ordinary `QPluginLoader`
modules; no wx types appear anywhere in the surface.

## Where plugins live

`<AppDataLocation>/plugins-qt/` (verified on macOS:
`~/Library/Application Support/OpenCPN/opencpn-qt/plugins-qt/`). One
shared library per plugin (`.dylib`/`.so`/`.dll`). The catalogue, load
errors and per-plugin enable switches show under **Options > Plugins**
(enable changes apply on restart).

## The interface

Implement `ocpn::qtui::OcpnQtPlugin`
(`gui/qt/plugin/ocpn_qt_plugin.h`):

```cpp
class MyPlugin : public QObject, public ocpn::qtui::OcpnQtPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID OcpnQtPlugin_iid)
  Q_INTERFACES(ocpn::qtui::OcpnQtPlugin)
public:
  QString name() const override { return "MyPlugin"; }
  QString version() const override { return "1.0"; }
  QString description() const override { return "Example"; }
  bool init(const ocpn::qtui::OcpnQtPluginHost& host) override;
  void deinit() override;
};
```

`init(host)` runs once on the GUI thread; register contributions there
and return `true` (false aborts the load). `deinit()` runs before
unload.

## Contribution points (the host seams)

- **`host.registerLayer(Layer*)`** — hand over a
  `gui/qt/layer.h` `Layer` subclass (world- or display-anchored
  scene-graph subtree). It joins the `LayerCompositor` exactly like the
  built-in overlays: per-layer visible/zOrder/opacity with persistence,
  `dirty()`-driven rebuilds, `updateSubtree()` on the render-sync phase.
  For data-driven overlays, the `StaticNavLayer` / `NavLayer` bases (and
  `SgBuilder` for geometry) are the same toolkit the core uses — see
  `route_overlay_layers.cpp` for the canonical patterns.
- **`host.registerHud(componentUrl, context)`** — a QML component the
  shell instantiates above the chart. The plugin's `context` QObject is
  assigned to the item's `pluginContext` property — expose your data as
  `Q_PROPERTY`s on it and bind normally.
- **`host.registerSettingsPage(title, componentUrl, context)`** — a QML
  component stacked under **Options > Plugins**, same `pluginContext`
  convention.
- **`host.navData`** — the `NavDataProvider` snapshot interface (own
  ship, AIS targets, routes/tracks/waypoints + the dynamic/static change
  signals). Read-only by design.
- **`host.navMsgTap`** — **live**: a QObject emitting
  `lineReceived(QString line, QString source)` per decoded NMEA-0183
  sentence / N2K PGN / SignalK message (`"HH:mm:ss  <payload>"` + the
  connection tag). Connect with the string-based `SIGNAL()` form — the
  emitter's concrete type is not part of the API. The Dashboard plugin's
  depth/water-temp instruments are the reference consumer.

## wx ABI capability audit (2026-06-11)

| wx capability flag / API | Qt seam | Status |
|---|---|---|
| WANTS_OVERLAY/OPENGL_OVERLAY (RenderOverlay) | `registerLayer` (scene-graph Layer) | ✅ (GRIB) |
| WANTS_NMEA/AIS_SENTENCES | `host.navMsgTap` (decoded stream) | ✅ (dashboard) |
| WANTS_NMEA_EVENTS / SetPositionFixEx | `host.navData` snapshots | ✅ (dashboard) |
| INSTALLS_TOOLBOX_PAGE (options tabs) | `registerOptionsPane(section,…)` | ✅ (o-charts, chartdldr) |
| WANTS_PREFERENCES | `registerSettingsPage` → Preferences… dialog | ✅ |
| INSTALLS_TOOLBAR_TOOL | `registerToolbarAction` | ✅ (example) |
| INSTALLS_CONTEXTMENU_ITEMS | `registerContextMenuItem(label, cb(lat,lon))` | ✅ (example) |
| WANTS_CURSOR_LATLON | HUD components bind `chart.cursorLat/Lon` | ✅ pattern (GRIB readout) |
| WANTS_CONFIG | plugin-side QSettings | ✅ convention |
| INSTALLS_PLUGIN_CHART(_GL) | chart formats are core-native (o-charts/CM93/KAP) | ✅ by design |
| USES_AUI_MANAGER (panes/dialogs) | QML HUD components / native dialogs | ✅ |
| WANTS_PLUGIN_MESSAGING (JSON strings) | — | ❌ gap (inter-plugin bus) |
| Route/waypoint CRUD from plugins | — | ❌ gap (navData is read-only) |
| WANTS_MOUSE/KEYBOARD_EVENTS | — | ❌ gap (input hooks) |
| WANTS_TIDECURRENT_CLICK | — | ❌ gap |
| WANTS_PRESHUTDOWN_HOOK / LATE_INIT | deinit() exists; late-init unneeded | ◐ |
| WANTS_VECTOR_CHART_OBJECT_INFO | — | ❌ gap (object-query hook) |
| Colour-scheme notification | — | ❌ gap (layers can poll; no signal) |

The Plugins pane is MANAGEMENT ONLY (wx parity): catalogue, enable
switches, per-plugin Preferences… button. Plugin UI lives in its own
options panes, toolbar actions, HUD components and dialogs.

## Porting guidance (Phase 4)

The first-party candidates (P4.3–P4.5: dashboard, chartdldr, grib)
port as **built-in Qt modules** first — same interface, statically
registered — and validate the API before it is frozen for third
parties (P4.6 closes when the API survives those three ports
unchanged).
