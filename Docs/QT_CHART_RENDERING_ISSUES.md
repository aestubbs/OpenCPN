# Qt chart rendering — open issues & investigation handoff

Status as of 2026-06-20, branch `migrate_to_qt`. Two **distinct** problems are
conflated under the symptom "charts stop rendering / only the basemap shows".
Keep them separate — they have different causes and fixes.

Repro/observe on the Pi: launch with `OCPN_INSTR=1`, output to `~/ocpn_run.log`.
Read the **live** viewport from the SQLite config (auto-saved every 2 s):

```
python3 - <<'EOF'
import sqlite3
db=sqlite3.connect('file:/home/pi/.opencpn-qt/navobj.db?mode=ro',uri=True)
for k,v in db.execute("SELECT key,value FROM qt_config WHERE key LIKE 'view/%'"): print(k,'=',v)
EOF
```

Key log lines: `quilt: displayN=… cells:`, `VIS … cands= needed= loaded=`,
`INSTR centreCover lat … lon … -> …`, `INSTR malloc_trim: RSS …`,
`onCellLoaded: SKIP … OOM guard`, `INSTR memPressure …`.

---

## Problem 1 — Longitude un-normalisation breaks chart selection (REGRESSION) — FIXED

**FIXED in commit `23bf8f8b4`** (see "Fix direction" below for what was done).
Kept here as the record of the bug. It was the cause of the reported "zoom out
then back in and the cells don't reappear / chart bar is empty" — a *selection*
bug, not a memory bug.

### Evidence
- Live view when stuck: `view/lon = 283.52`, `view/lat = 38.23`, `view/scale =
  1992` (≈ 1:166k — genuinely zoomed IN). `283.52 − 360 = −76.48` ≈ the NC/VA
  coast the user started from.
- `VIS … needed=0 loaded=12 cands=0` with `MemAvailable ≈ 3 GB`, `SKIP=0` — i.e.
  **not** memory pressure. `cands=0` = the candidate query found no cells.
- Basemap renders the correct coastline (it wraps longitude); only the ENC
  charts are missing.

### Root cause
The basemap continuous-longitude-wrap change (commit `d50dd4c0`,
`gui/qt/toolkit/viewport.h`) deliberately leaves `m_center_lon` **un-normalised**
so the world can scroll continuously across the antimeridian ("double precision
is ample for many laps"). The **basemap** copes — `ShapefileBasemapProvider`
renders the world repeated at `k*360` shifts. But **chart selection does not
wrap**:

- `ChartCanvas::updateVisibleCells` builds the query bbox straight from
  `centerLon`: `lon0 = centerLon − half_lon`, `lon1 = centerLon + half_lon`
  (`gui/qt/chart_canvas.cpp` ~line 1144).
- `ChartSpatialIndex::query()` (`gui/qt/chart_spatial_index.h`) and
  `CellExtent::covers()` / `intersects()` (`gui/qt/chart_extent.h`) use the ENC
  catalog's real longitudes, which live in `[-180, 180]`.

So once the view scrolls across the seam (centerLon leaves `[-180,180]`), the
query is at lon ≈ 283 where **no cell exists** → `cands=0` → `needed=0` → basemap
only. Zooming in/out doesn't help; only scrolling back across the seam does.

### Reproduce
Scroll east (or west) past the antimeridian seam — i.e. accumulate ±360° of
longitude pan — then look for charts at any zoom. They vanish; the chart bar
empties; basemap stays correct.

### Fix direction
Normalise longitude **for chart/catalog operations** while leaving the basemap's
continuous value alone (do NOT just re-normalise `m_center_lon` globally — the
basemap relies on continuity to avoid a visible seam jump). Options:

1. In `updateVisibleCells`, wrap the query-centre longitude into `[-180,180]`
   before computing `lon0/lon1`, and handle the antimeridian-straddle case (when
   `lon0 < -180` or `lon1 > 180`, split into two query ranges and union the
   results). Apply the same wrap to the `centerLon` used in the OVERSCALE /
   `centreCover` / finer-coverage tests.
2. Or keep a separate normalised longitude on the viewport (`normalizedCenterLon()`)
   used by every catalog consumer, with the raw lap-counting value used only by
   the basemap renderer.

Touch points: `gui/qt/chart_canvas.cpp` (`updateVisibleCells` query bbox + every
`covers(centerLat, centerLon)` call), `gui/qt/chart_spatial_index.h`,
`gui/qt/chart_extent.h`, `gui/qt/toolkit/viewport.h` (`setCenter`/`panBy`/`zoomAt`/
`clampCenter`, where `m_center_lon` is kept un-normalised). Also check the chart
bar / piano coverage list and click-to-query, which likely share the same raw
longitude.

**Applied in TWO commits** (the first was necessary but not sufficient):
- `23bf8f8b4` — catalog/selection side: a `wrapLon` into [-180,180] for all
  catalog work in `updateVisibleCells` (spatial query, candidate intersects
  prune, grid-point covers tests, centre covers for OVERSCALE/centreCover/LRU
  trim) and in `chartBarCells` (the empty chart bar). Both split the query into
  two in-range spans when the normalised view straddles +/-180. This made cells
  SELECT + the chart bar populate, but they still drew off-screen.
- `98f00dcde` — render side (the real fix): `Viewport::clampCenter` now wraps
  `m_center_lon` into [-180,180] (`-= 360*floor((lon+180)/360)`), and
  `screenToLatLon` normalises its output. The render transform + chart geometry
  live in [-180,180], so an un-normalised centre drew every chart off-screen
  (basemap only) and reported a 632 deg cursor. Now the centre stays a real
  longitude and charts render; the basemap still scrolls continuously (it draws
  its own +/-360 copies).

`updateBoundaryExtents` needed no change (whole-catalog scale-only scan).
**Remaining limitation:** a chart cell within ~half a view of +/-180 can still
glitch right at the seam -- the wx fix draws such cells at both lon and lon+/-360
(`s52plib.cpp` ~11613 region); not done here.

### Quick mitigation
Restarting recentres the view in range, so it "fixes itself" until you scroll
across the seam again. (Not a real fix.)

---

## Problem 2 — Chart GPU/atlas memory never returns to the OS

Real and separate. This is the OOM/dead-band risk at wide zoom and the target of
the planned investigation (QSGTextNode atlas + proactive GL release).

### Evidence
- RSS climbs with navigation and stays pinned (observed 2.5–3.9 GB) even after
  cells are evicted (`loaded=0` yet RSS multi-GB).
- `malloc_trim(0)` on eviction (commit `3b99684b`) reclaims only ~1–4 % (e.g.
  3610→3463 MB) — the bulk is **GL / texture-atlas memory** (Pi V3D unified
  memory) and QSG batch-renderer structures, not glibc-arena-top-free heap, so
  trim can't return it.
- When RAM is pinned low, the build-floor guard (`onCellLoaded`, env
  `OCPN_QT_BUILD_FLOOR_MB=900`) then SKIPs every build → basemap at all scales
  until restart. (Earlier this masqueraded as the symptom; Problem 1 is the
  current cause, but this remains a real failure mode at genuine continental
  load.)

### Where the memory goes (measured earlier — see project memory notes)
Soundings, labels and symbols are CPU-rasterised: `renderSoundingImage` /
`renderLabelImage` (`gui/qt/s52_vector_chart_provider.cpp`) paint each item with
`QPainter` into a `QImage`, shelf-packed into shared **2048² RGBA texture-atlas
pages**, drawn as `QSGImageNode` billboards. Plus thousands of tiny
`QSGGeometryNode`s + GL VBOs per dense cell.

### Mitigations already in place
- Fallback underzoom cap (commit `c23d8b81`, env `OCPN_QT_FALLBACK_ADMIT=6`):
  continental zoom shows basemap instead of dragging in the memory-huge
  far-underzoomed overview cells. **Do not remove** (see
  `QT_QUILT_VS_WX.md` / project memory `ocpn-qt-chart-selection-overscale`).
- LRU resident cap `OCPN_QT_MAX_RESIDENT=12`; per-pass cell cap
  `OCPN_QT_MAX_CELLS=50`; memory-pressure guard `OCPN_QT_MIN_FREE_MB=1800`;
  build-floor `OCPN_QT_BUILD_FLOOR_MB=900`.

### Investigation directions (planned for the next session)

**A. Replace the CPU text/symbol atlas with `QSGTextNode` (distance-field glyphs)
for soundings + markers.** Qt 6.7 (this Pi runs 6.7.3) exposes public
`QSGTextNode`. The scene graph keeps a shared distance-field glyph atlas, so each
glyph is stored **once** (vs each whole sounding/label string as its own packed
bitmap) — a large atlas-memory cut and crisp at any zoom (bitmaps blur when
overscaled). Caveats:
  - Soundings aren't plain text: integer + subscript tenths, drying underline,
    low-accuracy italic, safety-depth bold, swept-depth bracket → need a composed
    rich-text layout plus a small geometry node for the bracket. **Labels (plain
    names) first** (easy, high value); soundings second.
  - Each item is a world-anchored, screen-fixed billboard (per-instance transform
    + counter-scale) — watch the node/draw-call count (we already fought ~25k
    `QSGGeometryNode` overhead); batch where possible.
  - Symbols/markers: many are raster PresLib symbols — those may stay raster, but
    text-bearing markers benefit.

**B. Proactively release evicted cells' GL resources on eviction** (don't wait for
OS pressure or `malloc_trim`, which doesn't reclaim GL). When a cell leaves the
resident set (`ChartCanvas::updateVisibleCells` evict loop ~line 1408 →
`LayerCompositor::removeLayer`):
  - Ensure the provider's QSG subtree + `QSGTexture`s / VBOs are destroyed
    promptly on the **render thread** (verify the "freed N evicted subtree
    node(s)" path actually frees GPU resources, not just unlinks nodes).
  - Drop the provider's CPU-side atlas `QImage`s immediately.
  - Investigate `QQuickWindow::releaseResources()` / explicit `QSGTexture`
    deletion, and whether the V3D driver returns the memory.
  - Measure with `/proc/<pid>/status` VmRSS + `/proc/meminfo` and, if possible,
    GL driver memory; confirm RSS actually drops on eviction.

Touch points: `gui/qt/s52_vector_chart_provider.cpp` (atlas build ≈ lines
1450–1700, `renderSoundingImage`/`renderLabelImage`, `m_billboards`, the atlas
pages), the `LayerCompositor::removeLayer` / subtree-free path,
`gui/qt/chart_canvas.cpp` eviction loop.

---

## Related project-memory notes
`ocpn-qt-chart-selection-overscale`, `ocpn-qt-panning-oom-decode-leak`,
`ocpn-qt-oom-startup-quilt-storm`, `ocpn-qt-pi-v3d-64k-vertex-batch-limit`,
`ocpn-qt-instrumentation`, `ocpn-pi-build-x11-qt`.

## Recent relevant commits
- `d50dd4c0` basemap continuous longitude wrap + zoom-out world-fit (← source of Problem 1)
- `c8dad4d9` wx-style chart selection + symmetric sounding SCAMIN
- `4e02e6d6` features come in with their chart (SCAMIN margin = underzoom admit)
- `3b99684b` malloc_trim on eviction (largely ineffective — see Problem 2)
- `c23d8b81` cap fallback underzoom (continental zoom-out OOM mitigation)
