# OpenCPN → Qt / QtQuick Migration Plan

> Status: **planning / design** — no code changes made yet.
> This is a living document. It describes a **hard fork** of OpenCPN that
> replaces the wxWidgets UI with a Qt / QtQuick frontend.

## 1. Purpose & objectives

Modernise the OpenCPN GUI as a hard fork built on Qt:

- Touch-friendly UI, fluid animation.
- GPU-accelerated rendering.
- Wider target coverage — desktop, mobile, and **embedded / device builds
  closer to the metal** (boot-to-app, no desktop environment).
- Eliminate the per-platform `#ifdef` sprawl
  (`__WXMSW__` / `__WXGTK__` / `__WXMAC__` / `__OCPN__ANDROID__`).

This is explicitly a fork: it will diverge permanently from upstream and
breaks the existing wxWidgets plugin ABI.

**Android / mobile during the migration.** The existing Android build (wxQt)
is **dropped** for the duration of the migration — it is not kept alive in
parallel. Android- and wxQt-specific code (`__OCPN__ANDROID__`, `QT_ANDROID`,
the `comm_drv_*_android_*` drivers, `wxQt` paths) is **removed** as each area
is migrated, rather than ported or stubbed; keeping the legacy mobile build
working through the transition is not worth the friction. Mobile remains a
target — it is re-introduced *natively* once the core is on Qt, via QtQuick,
which delivers a far better Android/iOS experience than wxQt ever did. So
mobile is **deferred, not abandoned**.

## 2. Starting point (investigation findings)

- Desktop builds use **wxWidgets**. The Android build uses **wxQt** — wxWidgets
  with a Qt *backend* — not a Qt-native UI. Qt today appears in ~10 files,
  only as a JNI bridge. **There is no QtQuick / QML anywhere.**
- Coupling: ~584 files reference wxWidgets; the `/gui` layer (278 files) is
  100% wx-dependent.
- **Good news:** the comm / navigation core is ~53% already wx-free. The
  refactored I/O layer (`comm_buffers.h`, `N2KParser`, `nmea0183`) is
  essentially pure C++.
- **Good news:** `ocpnDC` (`gui/src/ocpndc.cpp`) is a single rendering seam —
  every draw call already funnels through one abstraction.
- Hard parts: the event system (`libs/observable`, ~47 dependents) and
  `s52plib` (the 12,550-line S52 vector-chart renderer).

## 3. Qt dependency policy

**Qt is a hard dependency at every layer**, including the core. The core may
use `QtCore` *and* `QtGui` value types. Consequences:

- Event system → `QObject` signals/slots.
- `wxString`→`QString`, `wxDateTime`→`QDateTime`, `wxConfig`→`QSettings`,
  `wxFileName`/`wxDir`→`QFile`/`QDir`, `wxThread`→`QThread`,
  `wxColour`/`wxPen`/`wxBitmap`→`QColor`/`QPen`/`QImage`.
- Vendored libs are **deleted, not ported**:
  `libs/wxsvg` (~261 files) → `QtSvg`; `libs/wxcurl` → `QNetworkAccessManager`;
  `libs/wxJSON` → `QJsonDocument`; `libs/observable` → signals/slots.
- i18n moves from wx catalogs to **Qt Linguist** (`.ts` / `tr()`).

## 4. Target architecture

```
┌──────────────────────────────────────────────────────────┐
│  QML / QtQuick UI shell        (replaces the 278 gui files) │
├──────────────────────────────────────────────────────────┤
│  Rendering: Qt Quick Scene Graph + LayerCompositor          │
│             (selective raw QRhi only for hotspots)          │
├──────────────────────────────────────────────────────────┤
│  Core: nav / comms / S57 / routes / charts  (de-wx'd, reused)│
├──────────────────────────────────────────────────────────┤
│  Qt plugin host  (registerLayer + QML HUD contributions)    │
└──────────────────────────────────────────────────────────┘
```

### 4.1 Why the scene graph, not raw RHI

RHI (Rendering Hardware Interface) is Qt's GPU abstraction — it retargets
Metal / Vulkan / D3D / GLES from one codebase. But raw RHI means hand-written
pipelines and render passes — too low-level for structured 2D cartography.

The **Qt Quick Scene Graph** sits on top of RHI (still gets all backends) and
is retained-mode: charts are mostly static between pan/zoom, so a geometry
tree re-renders cheaply on a viewport transform change without re-tessellating.
Drop to raw `QRhi` only for specific hotspots (e.g. AA-line or symbol-
instancing materials).

Note: S57/S52 is a specialised nautical cartographic standard. No off-the-shelf
GIS engine (Qt Location, MapLibre, …) renders S52. `s52plib` **is** the chart
engine — the migration only changes what its primitive output is fed into.

## 5. Scene graph structure

Two scene-graph **top nodes**, plus the QML HUD as a third compositing tier:

```
SceneGraph root
├── Top node 1 — WorldAnchoredRoot     (child of the viewport transform)
│      pan/zoom updates one transform → whole subtree follows
│
└── Top node 2 — DisplayAnchoredRoot   (child of a fixed screen transform)
       immune to chart pan/zoom

QML HUD  — layered above the chart QQuickItem, NOT a scene-graph node.
           Declarative, data-bound, animatable.
```

| Tier | Anchored to | Implemented as | Examples |
|---|---|---|---|
| World-anchored | chart coordinates | `WorldAnchoredRoot` subtree | raw chart + S52 sub-layers, AIS, routes, tracks, history, crowd-sourced overlays |
| Display-anchored (graphical) | screen coordinates | `DisplayAnchoredRoot` subtree | radar / PPI overlay, range rings, compass rose, mini-map |
| HUD (declarative) | screen coordinates | QML items above the chart | depth readout, SOG / COG, wind, alarms |

Rule: each visual element lives at the **cheapest tier that does the job**.
Simple bound readouts are QML HUD, not scene-graph nodes.

## 6. The LayerCompositor

Every visual contributor — core or plugin — is a uniform **Layer**:

| Property | Purpose |
|---|---|
| `anchor` | `WorldAnchored` or `DisplayAnchored` — which top node it joins |
| `visible` | toggle (user or programmatic) |
| `zOrder` | composite order within its group |
| `opacity` | blend weight |
| `owner` | core subsystem or plugin id |
| `id` / `name` | stable identity, used for config persistence |

A Layer owns one **scene-graph subtree** and updates it when its own data
changes — it knows nothing about other layers. The **LayerCompositor** owns
two ordered stacks and maps them onto `WorldAnchoredRoot` /
`DisplayAnchoredRoot`.

Why this is the spine of the design:

- **Performance** — per-layer subtrees mean independent dirty-tracking; new
  AIS positions rebuild only the AIS subtree. Pan/zoom updates one transform.
- **Chart is not monolithic** — S52 display categories (Base / Standard /
  Other / Mariner) and viewing groups become chart sub-layers in the same
  compositor. The concept already exists in `s52plib`; it is just surfaced.
- **This is the plugin API** — a plugin is a *Layer provider*: it calls
  `registerLayer()` and optionally contributes QML HUD items. GRIB is a
  world-anchored layer; dashboard contributes display-anchored / HUD elements.
- **Config falls out free** — per-layer `visible` / `zOrder` / `opacity`
  serialise to `QSettings` generically via the stable `id`.

Each Layer may run its own async data pipeline (e.g. a crowd-sourced feed with
its own network source and cadence) and hand node updates to the render thread
independently.

## 7. Phased plan

| Phase | Scope | Estimate |
|---|---|---|
| 0 | Fork; unify CMake into one Qt path; `qt_add_executable` / `qt_add_qml_module`; empty Qt app builds per target in CI | 2–3 wks |
| 1 | De-wx the core | 7–9 wks |
| 2 | Scene graph + LayerCompositor + chart / S52 sub-layer Layers | ~10–16 wks |
| 3 | QtQuick UI shell; core Layers (AIS, routes, tracks); QML HUD | 16–24 wks |
| 4 | Qt plugin host — expose `registerLayer()`; port dashboard / chartdldr / grib as built-in Qt modules | 6–8 wks |
| 5 | Embedded targets — eglfs / Wayland boot-to-app, Qt for Device Creation, cross-compile configs | 4–8 wks |

**Rough total: ~10–14 months for 2–3 developers.** Phases 1 and 2 can overlap
once the observable refactor lands.

### Phase 1 ordering (de-wx the core)

1. `libs/observable/` → `QObject` signals/slots. **Do this first** — it
   unblocks ~30 downstream files.
2. `comm_bridge`, `multiplexer`, `comm_navmsg_bus` — drop `wxEvtHandler`
   inheritance.
3. Comm drivers (~15 files) — signal/callback based instead of `wxEvtHandler`.
4. Mechanical sweeps — `wxString`, `wxDateTime`, containers, config, file I/O.

The core stays buildable and testable against the *existing wx GUI*
throughout Phase 1 — a major de-risking property.

## 8. Risks

- **`s52plib` scene-graph port** — S52 is a strict cartographic standard;
  visual regressions are chart-safety bugs. Mitigate with image-diff
  regression tests against the current renderer *before* changing it.
- **Chart performance parity** — the current GL path is heavily tuned
  (texture compression, quilting, FBO caching). Budget profiling time.
- **Loss of upstream merges** — a hard fork cannot cleanly merge upstream
  chart / protocol fixes. Keep Phase 1 as mechanical swaps (not redesign) to
  preserve the ability to cherry-pick.

## 9. Where to start

**Phase 1.1 — convert `libs/observable/` to `QObject` signals/slots**, while
keeping the existing wx GUI running on top. It is a direct, well-understood
mapping, it proves the Qt-everywhere decision, it unblocks the comm layer, and
it is fully testable before any UI work begins.

## 10. Data-flow example — live depth readout

Illustrates the reuse dividend of the layered plan:

```
depth sounder → comm driver → comm core
        (emits a Qt signal — post Phase 1 de-wx)
              ↓
      C++ QObject model   (Q_PROPERTY depthMeters)
              ↓  property binding
      QML HUD:  Text { text: nav.depthMeters + " m" }
```

Once the comm core emits signals, a live sensor value reaching the screen is a
`Q_PROPERTY` plus a one-line QML binding — no renderer code, animation for free
via `Behavior` / `NumberAnimation`.
