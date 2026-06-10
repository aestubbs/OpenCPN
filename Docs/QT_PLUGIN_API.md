# OpenCPN-Qt Plugin API (draft 1.0)

Status: **interface drafted (P4.1), loader live (P4.2)** — awaiting the
Phase-4 "day-1 plugins" decision before the first module ports
(P4.3–P4.5). IID: `org.opencpn.qt.plugin/1.0` — any breaking change bumps
it.

This is a **clean break** from the frozen wx plugin ABI
(`include/ocpn_plugin.h`). Qt plugins are ordinary `QPluginLoader`
modules; no wx types appear anywhere in the surface.

## Where plugins live

`<AppDataLocation>/plugins-qt/` (e.g.
`~/Library/Application Support/opencpn-qt/plugins-qt/` on macOS). One
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
- **`host.navMsgTap`** — reserved (nullptr today): the decoded
  NMEA-0183/2000 message stream lands here when the comm tap is exposed
  to plugins.

## What the wx ABI surface maps to

| wx ABI concept | Qt equivalent |
|---|---|
| `RenderOverlay` / `RenderGLOverlay` | a registered `Layer` (scene-graph, no paint events) |
| `SetPositionFixEx` callbacks | `host.navData` snapshots + `dynamicChanged()` |
| plugin message strings (JSON) | direct `Q_PROPERTY`/signal surface on your context object |
| `AddCanvasContextMenuItem` | not yet exposed (planned with the first port that needs it) |
| toolbar tools | register a HUD component (or planned toolbar seam) |
| `wxAuiManager` panes / dialogs | QML components (HUD or settings page) |

## Porting guidance (Phase 4)

The first-party candidates (P4.3–P4.5: dashboard, chartdldr, grib)
port as **built-in Qt modules** first — same interface, statically
registered — and validate the API before it is frozen for third
parties (P4.6 closes when the API survives those three ports
unchanged).
