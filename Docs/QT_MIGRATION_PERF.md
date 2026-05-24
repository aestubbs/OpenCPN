# P2.12 — Qt scene-graph performance profiling

> Companion to [`QT_MIGRATION_TASKS.md`](./QT_MIGRATION_TASKS.md) task
> **P2.12**. Records how to profile the Qt renderer, the performance levers
> already designed into it, the gaps found so far and which are closed, and
> what a rigorous head-to-head vs the legacy GL path still needs.

## How to profile (free Qt SG tools)

Run `opencpn-qt` with these environment variables (on-screen — see the
headless caveat below):

| Variable | What it shows |
|---|---|
| `QSG_RENDER_TIMING=1` | per-frame polish / sync / render / swap times (ms) |
| `QT_LOGGING_RULES="qt.scenegraph.time.*=true"` | the same, via logging categories (Qt 6 RHI) |
| `QSG_VISUALIZE=overdraw` | tints overdrawn regions — spot wasteful blending/fills |
| `QSG_VISUALIZE=batches` | colours per batch — spot draw-call fragmentation |
| `QSG_VISUALIZE=changes` | flashes nodes that changed — spot needless rebuilds |
| `QSG_RHI_PROFILE` / Metal frame capture | GPU-side timing on the chosen backend |

**Headless caveat:** under `QT_QPA_PLATFORM=offscreen` the scene graph does
not drive the GPU render loop, so `QSG_RENDER_TIMING` emits nothing. Frame
timings and a side-by-side comparison against the legacy wx/GL `OpenCPN`
must be captured from an **on-screen** run on the same chart set; that
manual benchmark is still to be done.

## Performance levers already in the design

These were built in as the renderer was written, not bolted on:

- **Pan costs zero CPU.** World-anchored Layers emit world-coordinate
  geometry once; pan/zoom only mutates the single `QSGTransformNode` matrix
  on the world root, so the GPU reprojects everything for free. Providers
  do **not** dirty on pan (`ChartLayer` deliberately doesn't subscribe to
  the viewport; vector providers re-fire only on scale change).
- **Zoom rebuild is debounced** (`S52VectorChartProvider`, ~110 ms): the
  cached subtree keeps being GPU-transformed during the gesture; the CPU
  re-layout (declutter + billboard counter-scale) runs once it settles.
- **Texture upload is deduplicated and subtree-scoped** (P2.5): each cell
  uploads each distinct S-52 symbol / pattern to the GPU exactly once
  (`TextureCacheNode` keyed on `QImage::cacheKey()`, with per-name
  memoisation in `s52plib_sg.cpp`), and frees them on the render thread with
  the subtree.
- **Retained dynamic overlays** (P2.11): AIS targets / own ship build their
  node group once and update only transform matrices per tick — O(targets)
  matrix writes, not a geometry rebuild.
- **Screen-density declutter + SCAMIN** thin out soundings / labels as you
  zoom out, so off-scale detail isn't drawn (opacity-0 → no draw call).
- **MSAA (4×)** gives anti-aliased edges without a custom AA-line shader
  (the residual custom-shader candidate from P2.4 / P2.13).

## Gaps found & closed

- **Static overlays rebuilt at the AIS tick rate.** RouteLayer / TrackLayer /
  WaypointLayer originally shared the provider's single `changed()` signal,
  so in demo mode they rebuilt (re-tessellating polylines, re-rendering text
  labels via `QPainter`, re-uploading label textures) every 200 ms despite
  being static. **Closed:** `NavDataProvider` now emits `dynamicChanged()`
  (AIS/own-ship) vs `staticChanged()` (routes/tracks/waypoints); static
  layers subscribe only to the latter and so build once.

## Remaining / candidate items

- **Draw-call batching.** Each `SgBuilder` primitive and each S-52 line strip
  is its own `QSGGeometryNode`. `QSG_VISUALIZE=batches` on-screen will show
  how much the SG auto-batches; merging same-material geometry into shared
  buffers is the optimisation if it doesn't. (The API surface won't change.)
- **AA-line shader** (P2.4 #5 / P2.13): only if on-screen profiling shows the
  MSAA + parallel-strip lines are insufficient or too costly.
- **Sub-pixel line widths.** Overlay pen widths are world units (`px * world
  /px`); below 1 they fall to a 1-px line strip — fine visually, but worth a
  look under overdraw.
- **Head-to-head vs legacy GL** on an identical chart/AIS load — the
  acceptance check for "close gaps"; needs the on-screen benchmark above.
