# Qt Quilt vs wx Quilt: Selection, Reference-Scale Gating, and M_COVR Clipping

> **STATUS (2026-05-30) — read §10 first.** This document grew in layers and
> §4–§9 describe **three different, mutually contradictory** quilt models as
> the work iterated: §5 (a wx-style reference-scale gate + outline-only + M_COVR
> clip plan), §8 (the composite model that *retired* that gate and claimed each
> cell is clipped to its M_COVR), and §9 (the underlay model that says **no
> clipping** is used). **§10 records what the code actually does today**,
> verified against source. In short: the Qt quilt is a **greedy composite**
> (no reference tier) with a **guaranteed coarser underlay** and a **per-cell
> bounding-box clip** (NOT an M_COVR clip, and NOT "no clip"). Treat §4, §5, §6
> and the clip claims in §8/§9 as **historical**; the Santa-Cruz "empty box" is
> resolved by the underlay (§10.4), not by the retired reference gate. The
> remaining real parity gaps are tracked as **P2.14–P2.19 + P2.7** in
> [`QT_MIGRATION_TASKS.md`](./QT_MIGRATION_TASKS.md).

This document explains, end-to-end, how the legacy wxWidgets OpenCPN composes an S-57/S-63 vector-chart **quilt** (the reference behaviour), how the new Qt quilt currently behaves, where they diverge, and exactly why the Santa Cruz "empty light-blue box" appears. It ends with a prioritized convergence plan. All citations are to actual source under `gui/src`, `gui/include`, and `gui/qt`.

## TL;DR

- wx anchors the quilt to ONE **reference chart** for the current zoom and renders content for that cell **and coarser cells only**. A cell **finer** than the reference is excluded from content (`b_include = false`, "scale is too large") and shown as an **outline only**. Every rendered cell is **clipped** to its real **M_COVR coverage minus all finer cells' coverage**.
- Qt has **no reference chart**, **no `Scale_ge(reference)` gate**, and **no render clip**. It picks a per-grid-point winner into a flat `m_needed` set and renders each as a **full, unclipped, opaque layer**, finer-on-top.
- At Santa Cruz mid-zoom this pulls the fine `US5CA3GB` (1:12000) into the quilt next to `US5CA50M` (1:50000); the fine cell's bbox-spanning `DEPARE` water-fill, drawn on top and unclipped, paints over the coarse cell's soundings -> the empty box. wx would render only the fine cell's outline.

---

## 1. The wx quilt pipeline (reference behaviour)

`gui/src/quilt.cpp`, `gui/include/gui/chartdbs.h`, `gui/src/chcanv.cpp`, `gui/src/gl_chart_canvas.cpp`, `gui/src/s57chart.cpp`.

### 1.1 Scale conventions

"Scale" is the native denominator `N` of a `1:N` chart, so **larger Scale number = coarser chart**. `ChartTableEntry::Scale_eq/ge/gt` compare with a rounding tolerance (`gui/include/gui/chartdbs.h:254-256`):

```cpp
bool Scale_eq(int b) const { return abs(Scale - b) <= rounding; }
bool Scale_ge(int b) const { return Scale_eq(b) || Scale > b; }   // same-or-COARSER than b
bool Scale_gt(int b) const { return Scale > b && !Scale_eq(b); }
```

So `Scale_ge(ref)` is TRUE for the reference cell and for **coarser** cells, FALSE for **finer** cells.

### 1.2 Choosing the reference / base chart for the zoom

`Quilt::AdjustRefOnZoom` (`gui/src/quilt.cpp:891`) builds, for each quiltable chart of the target family, an allowable on-screen-scale window:

- `nmax_scale = GetNomScaleMax = native / 4` (4x overzoom limit; `quilt.cpp:843`)
- `nmin_scale = GetNomScaleMin = native * 4 * mod` for vector (`quilt.cpp:877`), where `mod = pow(16, g_chart_zoom_modifier_vector/5)` clamped to `[0.2, 16]` (the detail slider; `quilt.cpp:854-862`).

Windows are stitched so ranges overlap with no holes, then the reference is the **finest** (largest-scale) cell whose window contains the display scale (`quilt.cpp:1002-1010`):

```cpp
if ((proposed_scale_onscreen < scales[i].min * 1.05) &&   // 5% roundoff leeway
    (proposed_scale_onscreen > scales[i].max)) {
  new_ref_dbIndex = scales[i].index;  // scales[] is finest-first
  break;
}
```

`Compose` stores `m_reference_scale = cte_ref.GetScale()`. At Santa Cruz mid-zoom the display scale lands in the **1:50000** window, so `US5CA50M` becomes the reference; the 1:12000 cell's window is too fine for that zoom.

### 1.3 Candidate gathering

`Quilt::BuildExtendedChartStackAndCandidateArray` (`gui/src/quilt.cpp:1307`) collects every on-screen cell of matching family/projection/skew into `m_pcandidate_array`, sorted **ascending by scale (finest first)**, dropping excessively underzoomed cells. A finer-than-reference cell may still enter the candidate array (so it can appear in the chart bar) -- candidacy is **not** rendering.

### 1.4 The content-inclusion gate (the crux)

In `Quilt::Compose` the reference cell is placed first and its region subtracted from the unclaimed view (`quilt.cpp:1975-1999`). For every other candidate, the decisive test (`quilt.cpp:2025`):

```cpp
if (cte.Scale_ge(m_reference_scale)) {
  ... // include if it has true overlap; vp_region.Subtract(chart_region)
} else {
  pqc->b_include = false;  // skip this chart, scale is too large   (quilt.cpp:2062-2063)
}
```

So only the reference cell and **coarser** cells contribute content; a **finer** cell fails `Scale_ge` and is excluded. Included cells intersect the still-unclaimed view and subtract their region (`quilt.cpp:2047-2054`); the walk stops when the view is fully claimed.

### 1.5 Coverage = M_COVR, not the bbox

`QuiltCandidate::GetCandidateRegion` (`quilt.cpp:99-206`) and `Quilt::GetChartQuiltRegion` (`quilt.cpp:489`) build each cell's region from its **AuxPly (per-M_COVR) polygons minus NoCovr**, so a cell only ever claims/renders its real charted area.

### 1.6 Per-patch ActiveRegion clip

Proceeding **largest scale (finest) to smallest** (`quilt.cpp:2344-2364`):

```cpp
piqp->ActiveRegion = piqp->quilt_region;          // = M_COVR coverage region
piqp->ActiveRegion.Subtract(m_covered_region);    // carve out all finer cells already laid down
piqp->ActiveRegion.Intersect(cvp_region);
if (piqp->ActiveRegion.Empty() && (piqp->dbIndex != m_refchart_dbIndex))
  piqp->b_eclipsed = true;
...
if (!piqp->b_overlay) m_covered_region.Union(piqp->quilt_region);
```

Finer cells carve their footprint out of coarser cells -> no double-paint. A cell fully covered becomes `b_eclipsed` and is dropped.

### 1.7 Clipped render

`glChartCanvas::RenderQuiltViewGL` / `DoRenderQuiltRegionViewOnDC` clips each chart to its `ActiveRegion` (`quilt.cpp:2772-2775`; `gl_chart_canvas.cpp` `SetClipRegion` via stencil/scissor), with a second clip inside `s57chart::DoRenderRegionViewOnGL`. A fill physically cannot escape its patch.

### 1.8 Outlines, independent of membership

`ChartCanvas::RenderAllChartOutlines` (`gui/src/chcanv.cpp:11337`) returns early only if `!m_bShowOutlines`, then loops **every** `ChartTableEntry` in the DB (`chcanv.cpp:11342-11360`) and draws each cell's boundary from its M_COVR/AuxPly polygons -- including finer-than-reference and eclipsed cells whose content was never rendered. So a finer harbour cell at mid zoom shows **only its outline**.

**Net wx behaviour:** exactly one reference scale tier (plus coarser tiers in the gaps) renders content; finer cells are outline-only; every rendered cell is clipped to its M_COVR minus all finer coverage.

---

## 2. The Qt quilt pipeline (current behaviour)

`gui/qt/chart_canvas.cpp` (`ChartCanvas::updateVisibleCells`, `:606`), `gui/qt/chart_extent.h`, `gui/qt/s52_vector_chart_provider.cpp`, `gui/qt/layer_compositor.cpp`.

1. **Candidates** (`chart_canvas.cpp:629-635`): every catalog `CellExtent` with `nativeScale>0`, `navFeatures>0`, intersecting the 1.3x pan-padded view rect.
2. **Per-grid pick** (`chart_canvas.cpp:667-707`): sample a 24x24 grid. At each point gather `cov[]` = candidates whose `CellExtent::covers(lat,lon)` is true (`chart_extent.h:72` -- point-in-M_COVR, bbox fallback). A cell is "eligible" iff `nativeScale >= threshold = displayScaleN(scale,centerLat) / kMaxUnderzoom` with `kMaxUnderzoom = 8` (`chart_canvas.cpp:658-662`). Among eligible cells within `kComparable = 4x` of the finest, pick the **densest** (`navFeatures / bboxArea`), tie-break finer; if none eligible, pick coarsest. Insert `pick->name` into the flat set `m_needed`.
3. **Load / evict** (`chart_canvas.cpp:713-733`): decode each new needed cell; evict loaded cells no longer needed.
4. **Full layer, finer-on-top** (`chart_canvas.cpp:545-581`): `onCellLoaded` wraps each decoded buffer in an `S52VectorChartProvider` + `ChartLayer` at `z = zOrderForScale(nativeScale)`. `zOrderForScale` (`chart_canvas.cpp:596-604`) gives finer cells a **higher z**, so finer draws **on top**.
5. **No render clip** (`s52_vector_chart_provider.cpp:511-546`): `renderChart` emits **every** area-fill prim as a `DrawTriangles` node appended directly to `root`, spanning the cell's full tessellated geometry. There is **no** `QSGClipNode` / scissor / stencil / M_COVR mask anywhere in `gui/qt` (grep finds zero). `CellExtent::coverage` is used only for the `covers()` selection test and the click highlight (`chart_boundary_provider.cpp`), never to clip content.

**Net Qt behaviour:** no reference chart, no `Scale_ge` gate, no inter-cell subtraction, no M_COVR clip. A finer and a coarser cell over the same ground can both be in `m_needed` and both render as full opaque layers, finer on top.

---

## 3. Differences table

| Aspect | wx | Qt | Impact |
|---|---|---|---|
| Reference / base chart | Single reference per ViewPort = finest cell whose `[native/4 .. native*4*mod]` window contains the display scale (`quilt.cpp:891,1002-1010`) | None; each of 24x24 grid points picks its own winner, unioned into `m_needed` (`chart_canvas.cpp:687-705`) | Qt has no tier to gate finer cells against |
| Content-inclusion gate | `cte.Scale_ge(m_reference_scale)` -> reference-or-coarser only; else `b_include=false` "scale is too large" (`quilt.cpp:2025,2062-2063`) | None; eligibility is "within 8x finer than display" + density (`chart_canvas.cpp:658-703`) | **Primary cause** -- the fine `US5CA3GB` is not excluded in Qt |
| Per-cell render region | `ActiveRegion = M_COVR - union(finer regions) ∩ viewport` (`quilt.cpp:2344-2364`) | None; full bbox tessellation (`s52_vector_chart_provider.cpp:511-546`) | Fine cell's fill extends past its M_COVR over the coarse cell |
| Render clip primitive | Stencil/scissor to ActiveRegion (`quilt.cpp:2772-2775`; `gl_chart_canvas` `SetClipRegion`) | None (no `QSGClipNode`/`setClip`/scissor in `gui/qt`) | Nothing bounds a fill to its coverage |
| Z-order vs overlap | Coarse->fine, but finer regions already subtracted -> no overlap paint | Finer higher z, drawn on top, no subtraction (`chart_canvas.cpp:596-604`, `layer_compositor.cpp:140`) | Finer-on-top => OVER-paint |
| Outline-only cells | `RenderAllChartOutlines` loops every DB cell, gated on `m_bShowOutlines` (`chcanv.cpp:11337-11360`) | No render-vs-outline split; only the always-on boundary grid + click highlight (`chart_boundary_provider.cpp`) | Qt cannot express "outline only, no content" |
| Coverage semantics | M_COVR used for selection AND render/clip region (`quilt.cpp:99-206`) | M_COVR used for selection only (`chart_extent.h:72`) | Clip data is captured but never applied |

---

## 4. Root cause of the Santa Cruz empty box

Primary cause: the **missing reference-scale content gate**, compounded by the **missing M_COVR render clip**.

At the reported mid zoom the display scale lands in the 1:50000 window, so in wx the reference is `US5CA50M` (`m_reference_scale = 50000`). `US5CA3GB` (1:12000) is finer than the reference, so `cte.Scale_ge(50000)` is FALSE -> `b_include = false` ("scale is too large", `quilt.cpp:2025/2062-2063`): its content is never turned into a patch; it shows only an outline via `RenderAllChartOutlines` (`chcanv.cpp:11337`).

Qt's `updateVisibleCells` has no equivalent gate. `US5CA3GB` is "eligible" because `12000 >= displayScaleN/8` (`kMaxUnderzoom=8`, `chart_canvas.cpp:658-662`), and because density `= navFeatures/bboxArea` favours a small dense cell, it **wins** the grid points it M_COVR-covers and enters `m_needed` (`chart_canvas.cpp:687-705`) alongside `US5CA50M`. Both become full `ChartLayer`s.

The clip defect makes that bad selection visible: `US5CA3GB` is given a higher z by `zOrderForScale` (finer-on-top; `chart_canvas.cpp:596-604`), and `S52VectorChartProvider::renderChart` emits its **full-bbox opaque `DEPARE` water-fill** with no clip (`s52_vector_chart_provider.cpp:511-546`). That fill spans the cell's whole bounding box -- beyond its real M_COVR -- and, drawn on top, paints over `US5CA50M`'s soundings, leaving the empty light-blue rectangle exactly at the fine cell's bbox.

The **selection** defect (no `Scale_ge(reference)` gate) is the one wx would have prevented and is the change that most directly matches the user's expectation: *"only show the boundary, don't render the finer cell's content at that zoom."* The **clip** defect is the secondary, general guard.

---

## 5. Convergence plan (prioritized)

### Priority 1 -- Reference-scale tier + `Scale_ge`-style content gate (fixes Santa Cruz; matches the user's expectation)

In `ChartCanvas::updateVisibleCells` (`gui/qt/chart_canvas.cpp:606`):

- **(a)** Port `AdjustRefOnZoom`'s band logic. For each candidate compute `[nmax = nativeScale/4, nmin = nativeScale*4*mod]`, `mod = pow(16, zoomModifierVector/5)` clamped `[0.2,16]` (wx `quilt.cpp:843,877,854-862`). Pick `referenceScaleN` = the finest candidate whose window contains `displayScaleN(scale,centerLat)`: `displayN < nmin*1.05 && displayN > nmax` (wx `quilt.cpp:1002-1010`).
- **(b)** Gate content on `nativeScale >= referenceScaleN` within a rounding tolerance (the Qt analogue of `cte.Scale_ge(m_reference_scale)`). Reference + coarser fill each location; finer cells are excluded from `m_needed`.
- **(c)** Replace the global `threshold = displayScaleN/8` + density winner (`chart_canvas.cpp:658-705`) with a reference-anchored rule: use the reference for points it covers, fall back to the next coarser covering cell where the reference has no coverage. Keep density only as a same-tier tie-break.

This single change excludes `US5CA3GB` from `m_needed` at the reported zoom, so it is never rendered -- only outlined.

### Priority 2 -- Outline-only representation of non-rendered cells

The chart bar already lists all available cells (`chartBarCells`, `chart_canvas.cpp:740`). Extend `ChartBoundaryProvider` (`gui/qt/chart_boundary_provider.cpp`) to draw the M_COVR boundary (`CellExtent::coverage`) of every catalog cell over the view, gated on a Show-Outlines toggle (mirrors `RenderAllChartOutlines`, `chcanv.cpp:11337-11360`), independent of `m_needed`. Restores wx's "finer cell shows only its outline".

### Priority 3 -- Per-cell M_COVR render clip (robust wx-parity guard)

Give each rendered cell an `ActiveRegion` equivalent. Build a `QSGClipNode` (or scissor) from `CellExtent::coverage` (already captured) and install it in `ChartLayer::updateSubtree` / `S52VectorChartProvider::renderChart` (`s52_vector_chart_provider.cpp:481-546`) so a cell's fills cannot paint beyond its M_COVR. For full parity, subtract the union of all finer included cells' coverage from each cell's clip region before drawing (port `quilt.cpp:2344-2364`, `ActiveRegion = quilt_region - covered_region`), so finer cells carve their footprint out of coarser cells and nothing double-paints.

### Sequencing

Implement **P1 first** (resolves the reported bug, matches the user's request), then **P2** so finer cells remain discoverable as outlines, then **P3** as the structural guarantee. P1 is the single most direct change to the user's stated expectation.

---

## 6. Runtime evidence (Santa Cruz, NOAA ENC, measured 2026-05-29)

Captured from the Qt app with per-cell load logging added to `ChartWorker::loadCell`
and `ChartCanvas::onCellLoaded`/`updateVisibleCells`. The configured chart set for
this region is **NOAA `.000` (OGR path)**; the o-charts `.oesu` cells are UK-only.

At the mid zoom where the box is empty:

```
quilt: 2 cells needed: US5CA50M,US5CA3GB
loadCell: name=US5CA3GB kind=0 scale=12000  cov=1 prims=1383  labels=270  soundings=222   query=693
loadCell: name=US5CA50M kind=0 scale=50000  cov=1 prims=17288 labels=2029 soundings=1802  query=2571
loadCell: name=US3CA52M kind=0 scale=210668 cov=1 prims=37241 labels=3080 soundings=2563  query=4513
onCellLoaded: ADD  US5CA3GB scale=12000           ← finer, drawn ON TOP, unclipped
onCellLoaded: ADD  US5CA50M scale=50000           ← richer, underneath, overpainted
```

Cells covering the harbour (`navobj.db` `chart_catalog`):

| cell | native scale | band | soundings | note |
|------|-------------:|:----:|----------:|------|
| US5CA3GB | 1:12000  | 5 | 222  | the box; sparse; `GetNormalScaleMax = 12000×4 = 48000` |
| US5CA50M | 1:50000  | 5 | 1802 | the surrounding soundings |
| US3CA52M | 1:210668 | 3 | 2563 | coarse fill |

`US5CA3GB`'s `NormalScaleMax` is **1:48000**, so wx would stop rendering its content
once zoomed out past 1:48000 and show it as an outline only — exactly the user's
expectation. The Qt `kMaxUnderzoom = 8` keeps it renderable to 1:96000, a full zoom
band too far, and the absence of an M_COVR clip lets its fill overpaint `US5CA50M`.

The zoom-dependence the user observed (soundings present zoomed-out, gone at mid-zoom,
present again zoomed-in) is the interaction of this with per-feature SCAMIN on the
fine cell's own soundings.

## 7. Status of fixes already applied (this session)

- **Object query (separate bug):** `S52Engine::decodeOsenc` never populated
  `buf.queryObjects`, so the right-click "Object query" popup was always blank for
  **o-charts/OSENC** cells (the NOAA/OGR `loadOneCell` path always populated them —
  confirmed `query=` counts above). Fixed: `decodeOsenc` now emits a `QueryObject`
  per point/line/area feature plus one per sounding. `gui/qt/s52_engine.cpp`.
- **Coverage cache (prerequisite for P3):** `chart_catalog_db` did not persist the
  M_COVR coverage polygons, so on every warm launch `CellExtent::covers()` fell back
  to the bounding box (the quilt's `covers()` test then behaved as if every cell
  covered its whole bbox). Fixed: coverage is serialised into a `coverage BLOB`
  column; a pre-coverage row (NULL) is treated as a cache miss and re-scanned once.
  `gui/qt/chart_catalog_db.{h,cpp}`. Runtime now shows `cov=1` per cell.

Remaining: the quilt convergence plan in §5 (P1 reference-scale gate is the primary
fix for the empty box; P2 outline-only finer cells; P3 M_COVR render clip).

---

## 8. Display rules — DEFINITIVE (agreed 2026-05-29)

After several fragile iterations of a *reference-scale, pick-one-chart-per-location*
quilt (which kept producing empty boxes / offshore holes whenever the single
"winning" cell's clip had a gap and nothing was drawn underneath), the model was
replaced with an explicit, ECDIS-style **composite** quilt. These are the rules
the Qt quilt now implements:

1. **Availability grid (always on).** Basemap, plus the **bounding-box rectangle**
   of every chart in the DB — a simple "a chart exists here" indicator. Drawn by
   `ChartBoundaryProvider` from each `CellExtent`'s bbox (not M_COVR). A cell's
   rendered *content* is separately clipped to its M_COVR, so its data may fill
   less than its rectangle; the rectangle still marks finer charts not yet drawn.

2. **Content render threshold.** A chart's content renders once the view is
   zoomed in to within **k×** of its natural (compilation) scale:
   `displayScaleN ≤ nativeScale × k`. **k is configurable** in the chart-settings
   dialog, **default 2, range 1–5** (1 = strictly at native scale; 5 ≈ wx's
   `native×4` range). Stored at `display/overzoomFactor`; plumbed into
   `ChartCanvas::updateVisibleCells` via `m_overzoom_k`.

3. **Composite + M_COVR clip (ECDIS-style), by overpaint.** Render the finest
   eligible chart **plus coarser charts beneath it**, finer on top (by
   `zOrderForScale`), each **clipped to its M_COVR coverage**. Selection is a
   greedy walk finest→coarsest over a 24×24 sample grid: a chart joins the
   rendered set when it covers a still-uncovered grid point (wx's "reference +
   coarser until the viewport is covered"), which bounds the set to the
   appropriate band + just enough coarser charts to fill its gaps. Because a
   coarser chart is *always* underneath, a gap in a finer chart's M_COVR is
   filled by the coarser chart — **no holes, no single-winner overpaint of a
   coarser cell's soundings**.

4. **Basemap** is the bottom layer, so it shows only where **no chart's M_COVR
   content** covers — satisfying "basemap only outside alternative coverage" for
   free.

5. **Overscale indication:** not implemented (wx draws it; deferred).

### Consistency requirement (the bug that motivated this)

The selection test (`CellExtent::covers`, `Qt::OddEvenFill`) and the per-cell
render clip (`S52VectorChartProvider` `QSGClipNode`, libtess2 `TESS_WINDING_ODD`)
**must use the same fill rule** — even-odd. They are the same rule (winding
parity == crossing parity), so a point selected as "covered by chart X" is
exactly a point chart X's clip paints. A mismatch (the earlier `NONZERO` clip
vs even-odd `covers`) made a self-intersecting M_COVR polygon select a cell for
a sub-area its clip then left blank → the basemap showed through. Even-odd on
both sides keeps them identical. (The composite model is additionally robust:
even a residual clip gap is backfilled by the coarser chart underneath.)

### What this retired

The `kUnderzoom`/reference-scale gate, the single-winner per-point pick, the
`density` tie-break, and the M_COVR boundary outlines (P2) — all replaced by the
greedy composite selection + bbox availability rectangles above.

### Performance note (overpaint vs region-subtract)

We **overpaint** (coarser drawn fully under finer) rather than subtracting finer
coverage from coarser (wx's `ActiveRegion`). On the Qt GPU scene graph this is a
few layers of hidden overdraw on a few-million-pixel window — sub-millisecond —
and produces the identical image (finer fills are opaque and on top). wx subtracts
mainly because of its CPU/DC raster path; it is unnecessary here.

---

## 9. Selection-vs-render gap + the underlay fix (DEFINITIVE, 2026-05-29)

A full pipeline trace found why basemap "holes" persisted through every clip
variant: **selection and rendering used two different definitions of "where the
chart is," and were never reconciled.**

- **Selection** owned a point via `CellExtent::covers()` = point-in-**M_COVR**
  exterior ring (and it discarded M_COVR *interior* rings, so it over-claimed
  coverage holes). `chart_extent.h:72`, `s52_engine.cpp:1356`.
- **Rendering** paints only the **actually-decoded** DEPARE/SOUNDG triangles --
  a cell emits a fill *only where an OGR polygon exists*. `s52_vector_chart_provider.cpp`.

M_COVR is a **superset** of the painted footprint, so a point could be *selected*
(owned by a finer cell) yet *unpainted* (the cell has no feature there). The old
greedy marked that point "covered" on the first owner and selected **no coarser
cell beneath it**, so the basemap (z=-1000) was the only thing left. Confirmed by
pixel colour: the offshore was basemap sea RGB(170,195,220), not the pale DEPDW
fill RGB(212,234,238) -- a *true hole*, not a clip artefact. (Clipping was a red
herring; the bbox clip is larger than M_COVR and removes nothing.)

### The fix (no clipping; uniform for NOAA/OSENC/raster)

1. **Guaranteed coarser underlay.** `updateVisibleCells` now renders **every**
   content-eligible chart that covers the view, composited finer-on-top
   (`zOrderForScale`). A finer chart's **opaque** area-fills hide the coarser
   exactly where the finer *has* data; where the finer has none (open water it
   doesn't chart), the coarser chart beneath paints instead. A coarser chart is
   therefore *always* genuinely rendered under a finer one, so an unpainted spot
   is filled by a coarser chart -- never the basemap. Selection and render are
   reconciled by the **underlay**, not by clipping selection down to render. This
   is the practical form of Rule 3; it needs no M_COVR clip (whose incomplete
   tessellation caused the earlier holes and clipped edge symbols).

2. **Soundings follow the chart, not a separate cliff.** Soundings are no longer
   SCAMIN-culled per cell (`effScamin` returns "always" for `BbKind::Sounding`);
   while a cell is rendered (content-eligible) its soundings stay visible and the
   shallowest-per-46px declutter thins them progressively -- fixing the "coarse
   chart loses all its soundings in one zoom step" symptom. (Nav-aid SCAMIN is
   unchanged.)

### Known residuals / follow-ups

- **Finer-paints-too-early:** governed by the over-zoom factor `k` (chart
  settings, default 2). Lower `k` keeps the coarser chart primary longer.
- **Coastline sliver mismatch:** with no clip, a coarser chart's slightly
  different coastline can show a thin sliver at a finer chart's edge. wx hides
  this with M_COVR ActiveRegion subtraction; revisit only if it's distracting.
- **Cross-tier sounding deconfliction:** declutter is per-cell; a composite-wide
  declutter (thin all tiers against one grid) would remove any residual
  stacked/offset soundings where two tiers both show in a gap.
- **M_COVR interior rings** are still discarded in the catalog scan; capturing
  them would make `covers()` exact (no over-claim), but the underlay already
  prevents the hole so it is no longer load-bearing.

---

## 10. Current implementation — verified ground truth (2026-05-30)

§4–§9 were written across several design iterations and disagree with each
other. This section is the **authoritative** description of what the Qt code
does **today**, verified by reading source (a fresh code audit on 2026-05-30,
each claim adversarially re-checked). When earlier sections conflict with this,
§10 wins.

### 10.1 Quilt selection — greedy composite, no reference tier

`ChartCanvas::updateVisibleCells` (`gui/qt/chart_canvas.cpp`):

1. **Candidates** = every catalog `CellExtent` with `nativeScale > 0`,
   `navFeatures > 0`, intersecting the view rect.
2. **Eligibility** is a single over-zoom test: `displayN ≤ nativeScale × k`,
   where `k = m_overzoom_k` (config `display/overzoomFactor`, default **2**,
   range 1–5; consumed in `updateVisibleCells`, re-runs selection on change).
   There is **no `Scale_ge` reference-scale tier** — wx's single-reference
   model (§1.2, §1.4) is **not** implemented and was deliberately retired (§8).
3. **Pass 1 (greedy, finest-first):** over a 24×24 sample grid, each eligible
   cell (smallest `nativeScale` first) claims the still-uncovered grid points
   its `CellExtent::covers()` returns true for; if it claims any, it joins the
   rendered set.
4. **Pass 2 (underlay):** for any grid point still uncovered, add the coarsest
   covering cell *regardless of eligibility* — the "guaranteed coarser underlay"
   that backfills gaps so the basemap never shows through a charted area (§9).
5. **Composite render:** every selected cell renders as a full opaque layer,
   **finer on top** via `zOrderForScale` (finer ⇒ higher z), so finer fills
   hide the coarser where the finer has data.

### 10.2 Clipping — per-cell bounding box, not M_COVR

`gui/qt/s52_vector_chart_provider.cpp` builds a per-cell **`QSGClipNode`** whose
rectangle is the cell's geographic **bounding box** (`m_north/m_south/m_west/
m_east` projected to screen, recomputed each frame). It is **not** an M_COVR
polygon clip. So §8's "clipped to M_COVR" is **false**, and §9's "no clipping"
is **stale** — the accurate statement is **"per-cell bbox clip, no M_COVR
clip."** Because the bbox is larger than the M_COVR coverage, the clip removes
little real content; a coarser cell can still show a thin coastline **sliver**
at a finer cell's edge (wx hides this with `ActiveRegion` M_COVR subtraction).
Adding an M_COVR clip is the optional parity item **P2.17**.

### 10.3 SCAMIN — now honoured by all primitive kinds (P2.14 done 2026-05-30)

SCAMIN takes BOTH an emit-side population and a consumer-side cull. Before
P2.14 the two sides were inconsistent: the billboard family worked but the
`Prim`/`PatternFill` families did not, so coarse-cell fills/lines streamed
across every zoom ("webbing"). **P2.14 closed that gap.** Current state
(verified against `libs/s52plib/src/s52plib_sg.cpp` and
`gui/qt/s52_vector_chart_provider.cpp`):

| Primitive | scamin set at emit? | culled by consumer? | net: SCAMIN works? |
|---|---|---|---|
| SY point symbol | ✅ `:311/:324` | ✅ (billboard) | ✅ |
| TX/TE text label | ✅ `:211` | ✅ (billboard) | ✅ |
| LC complex line | ✅ `:505` | ✅ (`ComplexLine`) | ✅ |
| SOUNDG sounding | ✅ (OGR SCAMIN) | density declutter | ✅ |
| **AC solid area fill** | ✅ `RenderToSGAC` (P2.14) | ✅ `m_scamin_nodes` (P2.14) | ✅ |
| **LS simple/styled line** | ✅ `RenderToSGLS` param (P2.14) | ✅ `m_scamin_nodes` (P2.14) | ✅ |
| **AP area-pattern fill** | ✅ `:608` | ✅ `m_scamin_nodes` (P2.14) | ✅ |

**P2.14 design (regression-safe):** the consumer (`renderChart`) wraps a
fill/line/pattern-fill in a `QSGOpacityNode` **only when it carries a real
SCAMIN**; un-SCAMIN'd fills append directly and always draw — so an
un-SCAMIN'd area fill never vanishes (it must persist as the composite
underlay, §10.4 — never apply the nav-aid `m_unset_scamin_n` default to area
fills). `updateScaminNodes(chart_scale_n)` hides a wrapped node once
`chart_scale_n > scamin`, run in the scale-gated `updateBillboards` pass
(same timing as the LC/billboard cull). Emit gates the value on the global
`m_bUseSCAMIN` toggle, so turning Use-SCAMIN off (re-decode via
`applyChartConfig`→`reloadResidentCells`) shows everything.

### 10.4 The Santa-Cruz "empty box" is resolved

The §4/§6 root-cause analysis (missing reference gate + missing M_COVR clip)
is **historical**. The composite **underlay** (§10.1 pass 2) means a coarser
chart is always rendered beneath a finer one, so where the finer cell has no
data the coarser cell paints — no empty box, no basemap hole, *independent* of
any gate or clip. **Do not re-introduce the reference-scale gate to "fix" this;
it is already solved.**

### 10.5 Parity scorecard

| Aspect | Qt today | wx | Parity? |
|---|---|---|---|
| Quilt selection | greedy composite + underlay | single reference tier | **divergent by design** (Qt fixes the wx "lost soundings on zoom" symptom) |
| Render clip | per-cell **bbox** | M_COVR `ActiveRegion` | partial — bbox present; M_COVR clip is **P2.17** (polish; underlay covers holes) |
| SCAMIN: SY/TX/LC/SOUNDG | yes | yes | ✅ |
| SCAMIN: AC fills, LS lines, AP fills | yes (P2.14, 2026-05-30) | yes | ✅ |
| Area boundary LS/LC lines | not emitted (fill only) | yes | **gap (P2.15)** |
| AC/AP fills, LS lines, SY symbols, CARC arcs, TX/TE text, depth shading (2/4-shade, shallow/safety/deep), CS recolour (UDWHAZ03/SNDFRM02/DEPCNT02), day/dusk/night palette | yes | yes | ✅ |
| Chart-options dialog → renderer | all 15 vector toggles wired (P2.16, 2026-05-30) | full | ✅ |
| Overscale indication | no | yes | **gap (P2.18)** |
| Chart types: S-57/OSENC/o-charts | yes | yes | ✅ |
| Chart types: raster KAP/BSB, MBTiles | no | yes | **gap (P2.7)** |
| Chart types: CM93 | no | yes | **gap (P2.19)** |
