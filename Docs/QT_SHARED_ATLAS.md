# Qt chart engine: shared symbol atlas & off-thread scene-graph build

Status: B1 implemented as the **shared library-sheet texture** (the symbols
draw from one uploaded `rasterSymbols` sheet via per-symbol `sourceRect`). B2
(shared pattern textures) and A (worker-side triangle expansion) are scoped
here as follow-ups.

> **Important correction (2026-06-22), read this first.** When this doc was
> first written the atlas was *assumed* to be a dominant chart-load cost. It is
> NOT. Profiling `renderChart` (gated `OCPN_QT_SG_STATS` timers) showed the
> per-cell first-build at a dense fine-zoom view split as: `prims+fills` ~12–124
> ms, **per-cell atlas pack ~3–5 ms**, and **`recomputeDeclutter` 1000–8000 ms**.
> The real fine-scale pan/zoom freeze was a label-declutter bug, not the atlas:
> `layoutLine` returned `layout.boundingRect()` whose width was the `1e6`
> no-wrap line width, so every name label's declutter bbox was ~1,000,000 px and
> the corner-placement occupancy loop iterated ~50k cells/label (~22 M cell-ops,
> seconds). Fixed by reporting `line.naturalTextWidth()` (commit `a35656342`):
> declutter ~4264 ms → ~2 ms. **The atlas is a real but modest win** (~3–5 ms/
> cell + better cross-cell batching), worth keeping, but it was never the
> bottleneck. Lesson: profile before optimising.
>
> Separately, a **pre-existing high-zoom panning OOM** (kernel-kills the process
> after heavy dragging; resident scene-graph memory + no decode backpressure,
> NOT the atlas) is deferred — see the project memory note
> `ocpn-qt-panning-oom-resident-scenegraph`.

## The problem: chart-load stutter

Panning into a new chart area, or zooming into an area covered by finer
charts, produces a visible freeze. The chart *decode* is correctly off the
GUI thread (`ChartWorker::loadCell`, `chart_worker.cpp`, on `m_worker_thread`,
emitting an `s52sg::Buffer` via `cellLoaded`). But turning that buffer into a
Qt Scene Graph subtree is **deferred to the first paint** and runs on the
**render thread** inside `S52VectorChartProvider::renderChart`
(`s52_vector_chart_provider.cpp`), reached from `updatePaintNode` →
`LayerCompositor::syncToScene` → `ChartLayer::updateSubtree`.

In Qt's threaded render loop the GUI thread is **blocked for the whole sync
phase** (the window where `updatePaintNode` runs). So a slow first-build
`renderChart` stalls both threads at once: the render thread is busy (no frame
presented) and the GUI thread is blocked waiting on sync (input/pan freeze).

The dominant first-build costs were:

1. **Vertex expansion** — `expandToTriangles` (fan/strip → independent
   triangles) plus `QSGGeometry::allocate` + memcpy, over every area-fill /
   line primitive.
2. **The per-cell symbol atlas** — `QPainter`-compositing each point symbol
   into 2048² `QImage` pages, then uploading those pages as `QSGTexture`s.

A comment at `s52_vector_chart_provider.cpp` (the scale-gate block) names it:
"~1.5 s build = the OOM and the pan judder."

## What was actually in the atlas

After feature-name and sounding text moved to `QSGTextNode`s (Qt's shared
glyph cache, no bitmap), the per-cell billboard atlas had exactly **one**
remaining feeder: **raster point symbols** (`m_buffer.symbols[].image`,
appended via the single `addBillboard` call site in the symbol loop). For
reference, the other categories are already off the atlas:

| Source | Buffer field | Where it goes |
|---|---|---|
| Raster point symbols | `symbols[].image` | **was** the per-cell atlas → now shared textures (B1) |
| Vector / HPGL symbols, CARC arcs | `vectorSymbols` | direct `QSGGeometryNode` trees |
| Soundings, feature names | `labels` | `QSGTextNode` glyphs |
| Area pattern fills (AP) | `patternFills[].pattern` | own per-cell `QSGTexture` (B2 target) |

### Key fact: symbols are a fixed library, already deduped

`s52plib_sg.cpp:cachedAtlasImage` fetches each symbol via
`ChartSymbols::GetImage(name)` and memoises it in a **process-wide static
`QHash<QString,QImage>` keyed by the 8-char S-52 symbol name** (`SYNM`). The
bitmap itself is cropped from one library sheet (`rasterSymbols`, see
`chartsymbols.cpp:GetImage`). So:

* the symbol set is finite and identical across every chart in the world;
* every feature using the same symbol shares **one implicitly-shared
  `QImage`** — i.e. a single stable `QImage::cacheKey()` process-wide.

The per-cell atlas therefore re-packed and re-uploaded the *same* handful of
library symbols for every cell, and again for the second split-view pane.
That redundant copy is what B1 removes.

## B1 — one shared library-sheet texture (implemented)

The S-52 raster symbol library is **already** one pre-packed atlas:
`ChartSymbols::rasterSymbols` is a single sheet bitmap (rastersymbols-*.png for
the current colour scheme) and `GetGLTextureRect(name)` gives each symbol's rect
within it. So we upload that sheet **once** and draw every symbol from it via a
per-symbol `sourceRect`:

* `s52sg::Symbol` gains `atlasRect` (the symbol's rect in the sheet — the same
  region `GetImage` already cropped its `image` from); `s52sg::Buffer` gains
  `symbolSheet` (the whole sheet, one implicitly-shared `QImage` set once at
  emit). Wired in `s52plib_sg.cpp` (`cachedSymbolSheet` + `RenderPointSymbolToSG`).
* `gui/qt/shared_symbol_atlas.{h,cpp}` — `SharedSymbolTextures`, a render-thread
  singleton (`TextureCacheNode` promoted to **app lifetime**) that uploads the
  sheet once per `cacheKey()` and hands the same `QSGTexture` to every cell and
  both panes. Entries are **immutable** and the cache only ever grows.
* `addBillboard` (in `renderChart`) sets that one sheet texture on the symbol's
  `QSGImageNode` plus `setSourceRect(atlasRect)`. Symbol **geometry**
  (size/pivot/dpr) still derives from `image`, so retina/HiDPI sizing is
  byte-identical to before. The per-cell atlas-pack remains only as a fallback
  for a null sheet (processes an empty list — cheap).

### Why one shared sheet — and what failed first

This is fundamentally about **draw-call batching**, not just upload cost. Qt's
batch renderer merges consecutive `QSGImageNode`s that share a texture; the
break happens whenever the texture changes between adjacent nodes.

* **Per-cell atlas pages** (the prior design): all symbol *types* in a cell sat
  on a few shared pages, so a cell drew its symbols in a handful of batches —
  but different cells had different page textures, so symbols never batched
  *across* cells, and each cell paid a QPainter pack + page upload on the render
  thread (the chart-load stutter).
* **One texture per symbol type** (tried, and it made dragging *much worse* —
  reverted): symbols are emitted in mixed type order, so consecutive nodes
  rarely share a texture and the batch breaks on nearly every node. A
  2000+-symbol cell went from a few draw calls to hundreds/thousands **per
  frame**; during a pan (continuous re-render) that is a severe regression. The
  "batches better across cells" intuition was wrong: cross-type batching within
  a frame matters far more, and per-symbol textures destroy it.
* **One shared sheet** (current): every symbol — every type, every cell, both
  panes — references one texture, so they all batch into ~one draw call. Strictly
  better than the per-cell atlas for per-frame cost, and zero per-cell copy or
  re-upload.

### Why immutable-and-grow

A shared atlas that we *packed into* at runtime would have to re-upload (replace)
a page's `QSGTexture` as it fills — dangling every already-built cell node that
still points at the old pointer. The sheet sidesteps this completely: it is
pre-packed, uploaded whole exactly once per scheme, and never mutated. A
scheme change just adds a new sheet entry (new cacheKey); the old one lingers
until `sceneGraphInvalidated` (a few MB per scheme — the price of zero dangling
risk).

### Lifetime / threading (the only subtle part)

* **Created** lazily on the **render thread** (first `renderChart` that needs a
  symbol), via the one window's `createTextureFromImage`. The app has a single
  `QQuickWindow` — both `ChartCanvas` panes (`Main.qml`: `chart` and
  `splitPane`) are children of one `ApplicationWindow`, so there is one render
  context and one render thread. A single shared texture is valid for both
  panes. (`AA_ShareOpenGLContexts` is not set and is not needed.)
* **Never** built in `main()` — `createTextureFromImage` needs the window's SG
  render context, which initialises lazily on the render thread after expose.
* **Freed** only on `QQuickWindow::sceneGraphInvalidated` (a `DirectConnection`
  so the slot runs on the render thread, with `window` as the context object).
  At that point the context is going away and the whole scene graph is being
  torn down, so freeing every texture is safe — nothing is mid-render.
* **Not** freed via a C++ static destructor: that would run at process exit on
  the main thread after the render context is gone. The singleton is an
  intentionally-leaked heap object; GPU resources are reclaimed by
  `sceneGraphInvalidated` / process teardown.
* **Colour-scheme / style changes** are *not* special-cased. A scheme change
  re-decodes cells and produces new symbol `QImage`s with new cache keys; the
  old textures simply become unreferenced and linger until the next
  `sceneGraphInvalidated`. The bound is tiny (~all library symbols × a few
  schemes, each a ~30² RGBA texture ≈ a couple of MB), and *not* freeing
  mid-session is what keeps the design free of dangling-pointer hazards.

### Double-chart-display (split view) — verified safe

* One `QQuickWindow`, one render context, one render thread (above).
* Both panes consume the *same* `s52sg::Buffer` but each builds its **own**
  provider / subtree (`chart_canvas.cpp:onCellLoaded`), so each pane's symbol
  nodes independently reference the shared textures — exactly the shared-read
  the singleton is designed for.
* The split pane mirrors the primary's colour scheme
  (`Main.qml`: `colorScheme: chart.colorScheme`), so one shared texture set
  serves both with no per-pane keying.
* Even though the split pane uses a separate `S52Engine`, the symbol bitmaps
  are identical (same library) and `cachedAtlasImage` is a name-keyed global,
  so cache keys are stable across both engines.

## B2 — shared pattern-tile textures (follow-up)

Area pattern fills (`patternFills[].pattern`) still take a **per-cell**
`root->texture(pf.pattern)`. Patterns are tiled (`Repeat` wrap) so they cannot
co-pack into a symbol atlas page, but they are a fixed library too. Replace the
per-cell texture with a process-wide cache (same singleton style as B1) keyed
by **`(pattern name + colour-table index + baked fill colour)`** — vector/HPGL
patterns (CATZOC `DQUAL*`, dredged-area stipple, …) bake a per-colour-table
fill, so the name alone is not a sufficient key. One upload per unique pattern
for the whole app instead of per cell.

## A — worker-side triangle expansion (follow-up)

With B1+B2 done, the residual render-thread CPU is geometry:

* Pattern-fill triangles are **already** tessellated on the worker
  (`PatternFill.tris` is a ready triangle list) — nothing to do.
* Area-fill / line `Prim`s are stored as fans/strips and converted on the
  render thread by `expandToTriangles`. Move that expansion into
  `ChartWorker::loadCell` (store the expanded independent-triangle list in the
  buffer) so `renderChart` only does `allocate` + memcpy. `QSGGeometry` is
  plain CPU memory, safe to fill off-thread; only node *attachment* stays on the
  render thread. Respect the existing SCAMIN build gate so underzoomed sub-pixel
  prims are not expanded (memory).

## Net effect

After B1, a symbol-heavy cell's first paint no longer does any `QPainter`
compositing or per-cell texture upload for symbols — it reuses already-resident
shared textures. B2 removes the same for patterns. A removes the residual
vertex expansion. The render-thread sync window shrinks toward a near-atomic
node-attach, which is what removes the freeze. (The separate GUI-thread
`updateVisibleCells` selection cost is tracked elsewhere.)
