# OSENC / S-57 → Qt scene-graph rendering pipeline

Status: **investigation + design**, 2026-05-27. Driving issue: o-charts (OESU)
vector charts decode and mostly render, but **line geometry of several feature
types webs across the view** (grey/magenta/tan streaks in the Solent and North
Sea), and the **coastline land-shade** does not fire. NOAA `.000` (OGR) charts
render correctly. This document maps what the wx renderer does, what the Qt port
does, where they diverge, and the plan to converge every geometry path onto one
scene-graph pipeline.

Primary code:
- Incoming decode (Qt): `gui/qt/s52_engine.cpp` (`decodeOsenc`, `loadEncCells`,
  `buildOsencPolyTessGeo`), `gui/qt/osenc_reader.*`, `gui/qt/chart_worker.cpp`.
- Reference decode (wx): `gui/src/o_senc.cpp` (`Osenc::ingest200`,
  `BuildPolyTessGeo`), `gui/include/gui/o_senc.h` (record structs).
- Reference render (wx): `gui/src/s57chart.cpp` (`BuildLineVBO` / `m_ls_list`),
  s52plib `RenderAreaToGL` / `RenderToGLAC` etc.
- SG emit (shared): `libs/s52plib/src/s52plib_sg.cpp`
  (`RenderPointSymbolToSG`, `RenderLineToSG`, `RenderAreaToSG`, `RenderToSGAC`).
- SG consume / quilt: `gui/qt/s52_vector_chart_provider.cpp`,
  `gui/qt/chart_canvas.cpp` (quilt selection + eviction), `gui/qt/coast_shade.cpp`.

---

## 1. Incoming formats

### 1.1 OSENC record stream
Both NOAA-via-OGR and o-charts ultimately become a stream of **S57Obj**. The
o-charts path arrives as an **OSENC byte stream** (decrypted by `oexserverd`),
a flat sequence of records:

```
record = uint16 type | uint32 length(incl. 6-byte base) | payload[length-6]
```

Record types (`osenc_reader.h` `OsencRecordType`):

| value | record | payload |
|-------|--------|---------|
| 1..8  | HEADER_* | version, cell name, scale, dates |
| 64 | FEATURE_ID_RECORD | feature type code (→ acronym via s57 registrar) |
| 65 | FEATURE_ATTRIBUTE_RECORD | attr type code, value-type, value |
| 80 | FEATURE_GEOMETRY_RECORD_POINT | lat, lon (2 doubles) |
| 81 | FEATURE_GEOMETRY_RECORD_LINE | extent(4 dbl) + edgeVector_count + index table |
| 82 | FEATURE_GEOMETRY_RECORD_AREA | extent + counts + triangles **+ boundary edge index table** |
| 83 | FEATURE_GEOMETRY_RECORD_MULTIPOINT | soundings (x,y,z per point) |
| 96 | VECTOR_EDGE_NODE_TABLE_RECORD | per-edge: index, point-count, points |
| 97 | VECTOR_CONNECTED_NODE_TABLE_RECORD | per-node: index, point |
| 98/99 | CELL_COVR / NOCOVR | coverage polygons |
| 100 | CELL_EXTENT_RECORD | 4 corners (sw,nw,ne,se) as lat/lon → 8 doubles |

### 1.2 Coordinates are SM (simple mercator) metres, not lon/lat
**Critical.** Point geometry (80) carries lat/lon **doubles** directly. But the
**VE/VC tables and area triangle vertices are stored as `float` SM
easting/northing** relative to the cell reference: `o_senc.cpp` runs
`toSM(lat, lon, m_ref_lat, m_ref_lon, &e, &n)` on every node/vertex when writing
(lines ~2034, 2129, 2675, 2812).

The reference point is the **extent centroid**:
`m_ref_lat = (NLAT+SLAT)/2`, `m_ref_lon = (ELON+WLON)/2` (`o_senc.cpp:686`). The
absolute value is irrelevant as long as decode uses the **same** ref as encode —
`fromSM(SM, ref)` round-trips exactly. So everything must be inverted with
`fromSM_plib(e, n, ref_lat, ref_lon, …)` before it reaches the lon/lat scene
graph.

### 1.3 Line/area geometry = edge-index triples over shared edge tables
Both LINE (81) and the AREA boundary (inside 82) are an array of **triples**:

```
[ startVC_index, ±edgeVE_index, endVC_index ]   (3 × int32 per segment)
```

- `startVC/endVC` index into the **connected-node** table (VC, record 97) — the
  topological endpoints.
- `edgeVE` indexes the **edge** table (VE, record 96) — the intermediate
  vertices of that edge. **Sign = traversal direction** (negative ⇒ walk the
  edge's points in reverse). `0` ⇒ no edge (a direct node→node connector).

The full geometry of one triple is: `VC[start] → (VE points, fwd/rev) → VC[end]`.
Triples within a feature are **NOT guaranteed to be in geometrically contiguous
order**, and a feature may have multiple disjoint parts.

### 1.4 Area record carries BOTH fill and outline
`FEATURE_GEOMETRY_RECORD_AREA` (82) payload = extent + `{contour, triprim,
edge}` counts + per-contour point counts + **triangle primitives** (the fill) +
**boundary edge-index table** (the outline, same triple format as a line). wx
reads both: `BuildPolyTessGeo` for the fill, then `SetLineGeometry(GEO_AREA)`
for the boundary (`o_senc.cpp:866-890`). The boundary feeds area-border line
rules and the coastline.

---

## 2. The wx rendering pipeline (reference)

1. **Decode** → `S57Obj` per feature, with `Primitive_type`
   (POINT/LINE/AREA/MULTIPOINT), attributes, and geometry:
   - POINT: `m_lat/m_lon`.
   - AREA: `pPolyTessGeo` (fill) **and** `m_lsindex_array` (boundary triples).
   - LINE: `m_lsindex_array` (triples).
   The VE/VC tables are held per-chart (`m_ve_hash`, `m_vc_hash`) keyed by index.

2. **Build segment list** (`s57chart.cpp:1004-1233`,
   `BuildLineVBO`): for each triple build a `line_segment_element` chain of
   typed pieces:
   - `TYPE_CE`: connector `VC[start] → edge first/last point`.
   - `TYPE_EE` / `TYPE_EE_REV`: the edge's own points (fwd/rev).
   - `TYPE_EC`: connector `edge last/first point → VC[end]`.
   - `TYPE_CC`: direct `VC[start] → VC[end]` when there is no edge.
   Connectors are 2-point segments stored in a separate connector buffer. Each
   piece is an **independent** primitive — wx never joins one triple's end to
   the next triple's start. Identical endpoints make chained features *look*
   continuous; non-contiguous ones simply don't get bridged.

3. **Render** each piece from the VBO as line strips, applying the LUP's LS/LC
   line rules (colour/pattern). Areas: `RenderAreaToGL` walks AC (solid) and AP
   (pattern) rules over the tessellation, then draws the boundary via the area's
   line geometry where the symbology calls for it.

4. **Quilt / scale selection**: the chart DB assigns each cell a native scale;
   the quilt renders only cells whose scale band suits the view, and honours
   per-object **SCAMIN** (hide when zoomed out past the object's minimum scale).

---

## 3. The Qt scene-graph pipeline (current)

1. **Decode** (`decodeOsenc`, OSENC) or `loadEncCells` (OGR/NOAA) → `S57Obj`,
   exactly mirroring §2.1. VE/VC held in local `QHash`es for the cell.
2. **SM → lon/lat**: after the read pass, VC and VE tables are inverted with
   `fromSM_plib(..., ref_lat, ref_lon)` (added this session). Area triangles are
   inverted inside `RenderToSGAC` using `ppg_geo->GetChartRefPos()` — so
   `buildOsencPolyTessGeo` now sets `pPTG->m_ref_lat/m_ref_lon` (added this
   session; the default-ctor left it (0,0) → fills off Africa).
3. **Emit pass** walks objects and calls the shared SG emitters
   (`RenderPointSymbolToSG`, `RenderLineToSG`, `RenderAreaToSG`) into an
   `s52sg::Buffer` (world-coord prims, billboards, labels, pattern fills,
   `landContours`).
4. **Consume**: `S52VectorChartProvider::renderChart` turns the buffer into QSG
   nodes; the viewport projects lon/lat. `coast_shade.cpp` builds the inland
   gradient from `landContours` (assumes **closed, land-on-left rings**).
5. **Quilt**: `chart_canvas.cpp` samples the view on a grid, picks the finest
   *eligible* cell per point (`kMaxUnderzoom=8`, `kComparable=4`), unions into
   `m_needed`, loads those, and **evicts** loaded cells no longer in `m_needed`.

**Key divergence vs wx:** line geometry is emitted **per-triple** (one
`RenderLineToSG` per triple) — correct, mirrors wx's independent pieces. Earlier
the Qt code concatenated all triples into one strip → spider web (fixed). The
area boundary is now captured but **not yet drawn** and **not yet stitched into
rings** for the coast shade.

---

## 4. Findings from this investigation

What works after this session's fixes:
- o-charts cells decrypt, decode, and render areas + points + line features.
- Line **per-triple** emit removed the within-feature web for detail cells.
- Area fills correct (ptg ref set; SM inversion correct).

### 4.0 ROOT CAUSE of the line web + missing lines (FOUND & FIXED, 2026-05-27)
The o-charts **OESU edge-index table stores 4 int32 per edge entry**
(`[startVC, ±edgeVE, endVC, reserved=0]`), whereas the open-source OSENC in
`gui/src/o_senc.cpp` uses **3**. The Qt decoder read the table at stride 3, so
every entry after the first drifted — pairing edges with the wrong connected
nodes (degree-long "spider web" segments) and leaving entries unresolvable
(missing pontoon/berth lines). Diagnostic fingerprint: coherent triples landed
on a *perfect every-4th* periodicity (3 and 4 are coprime → stride-3 reads
realign every 4 entries over stride-4 data). Fix: `repackEdgeIndex()`
auto-detects the stride from the record length and repacks to a canonical
stride-3 `[in,edge,en]` table; applied to both the LINE record and the AREA
boundary table. After the fix the resolved segments chain correctly
(`seg[n].endVC == seg[n+1].startVC`) and each edge sits between its nodes.
**Implication for the convergence plan:** OESU ≠ open OSENC at the byte level —
do NOT assume `gui/src/o_senc.cpp` formats; verify each table's stride/layout
against the actual `oexserverd` output (o-charts_pi's Osenc, not the OCPN one).

### 4.1 Z-order / display priority (FIXED 2026-05-27)
`s52sg::Prim` had no S-52 display priority and the consumer drew prims in
stream/emit order, so a `DEPARE` fill emitted after the pontoon/berth lines
painted over them (lines "cut off" by the lighter deep-water area). Added
`Prim::priority` (from `LUP->DPRI - '0'`, set in `RenderToSGAC` / `RenderToSGLS`
/ `RenderLineToSG`'s LC path) and `std::stable_sort` the prim list by priority
in `decodeOsenc` + `loadEncCells`. wx achieves the same via its per-priority
render pass (`razRules[PRIO_NUM][...]`).

### 4.2 Remaining missing render features (open, by symptom)
Comparing Qt vs wx at Poole (1:6000):
- **Soundings** not shown — `FEATURE_GEOMETRY_RECORD_MULTIPOINT` (rec 83) has no
  decoder in `decodeOsenc`. NOAA path formats them as labels; add the OSENC
  equivalent (x,y,z per point → depth label).
- **Dotted/stipple area shading** (e.g. dredged/foul channel) not shown — these
  are S-52 **AP pattern fills**; `RenderToSGAP` exists but the OSENC area path
  may not be hitting the AP rules (verify the area LUP rule walk + the symbol
  atlas for the pattern).
- **Dashed magenta line** (recommended track / fairway / cable, bottom-right)
  not shown — these use **LC (complex line)** symbology; our `emitLC` draws a
  plain solid strip (no dash/symbol), and some may be skipped. Need real LC
  line-style handling (dash patterns / placed symbols).
- **Solid coastline edge line** — wx draws a crisp solid line exactly on the
  land/area boundary; we only draw the soft `coast_shade` gradient (which also
  doesn't perfectly track the edge). Should draw the area boundary (LNDARE/
  COALNE) as a proper LS/LC line, in addition to (or instead of) the gradient.

### 4.3 Caching the converted cells (open question raised by user)
We do NOT convert to ENC files. The flow is OSENC bytes → `S57Obj` →
`s52sg::Buffer` (scene-graph prims). The **SENC disk cache (`qt_senc_cache`)
stores the raw DECRYPTED OSENC bytes** only (to skip the slow `oexserverd`
round-trip); `decodeOsenc` re-runs on every load. Options to consider:
serialise the decoded `s52sg::Buffer` (fast reload, but format-coupled), or keep
the byte cache and accept the re-decode (decode is per-visible-cell and cheap).
Decide as part of converging the pipeline.

What is still wrong / open:

1. **Overview cells web at wide zoom.** The `OC-44-000xx` cells are **1:1,500,000
   overview/coverage tiles** with round-number extents (e.g. lat[30,50]
   lon[0,40]). Detail cells are 1:90,000 / 1:8,000. At Poole zoom the quilt
   correctly evicts the overview cells (image was ~clean); at Solent/North Sea
   zoom the overview cells are the chosen scale and their **coarse, degree-
   spanning** coastline/cable/pipeline/contour edges streak across the view.
   Open question: is that coarse data being decoded **correctly** (and just
   ugly because it's an overview being viewed near its limit), or is there a
   residual decode error amplified by the large coordinates? Needs a
   **single-cell side-by-side against wx OpenCPN** rendering the same cell.
   Diagnostic seen (now removed): long segments cluster on a few recurring node
   indices (e.g. node `1`) with spans of 4–16°.

2. **SCAMIN not applied to o-charts lines/areas.** wx hides objects below their
   SCAMIN. Many of the webbing overview features likely have a SCAMIN that wx
   honours and the Qt path ignores (points already carry `scamin`; lines/areas
   do not). This may be the real reason the overview lines should *not* show.

3. **Coastline land-shade.** (FIXED 2026-05-27.) Area boundary edges are
   captured (`SetLineGeometry(GEO_AREA)`). Concatenating the triples in *stream
   order* joined non-adjacent points, so `makeCoastShadeNode` (which needs
   **closed, contiguous, consistently-wound rings**) drew crossing "X" slivers
   across each polygon and put the shade on the wrong side (user images
   2026-05-27). Fix: `stitchRings()` in `decodeOsenc` chains the boundary edges
   by their **VC connected-node indices** (exact topological joins, not float
   matching), orientation-agnostic (reverse a segment if entered at its end
   node), splitting disjoint loops into separate rings, then forces each ring
   **CCW in lon/lat** to match the OGR exterior-ring convention the shader
   assumes (left normal points into land after the provider's y-flip). Holes
   are not separated from outers (forced CCW like the OGR path, which only
   shades exterior rings) -- erroneous shade inside LNDARE lakes is possible but
   rare on coastal charts. **wx draws NO inland gradient**: land = flat `LANDA`
   fill + a crisp boundary line `LS(SOLD,1,CSTLN)` (CSQUALIN01, s52cnsy.cpp:2130)
   -- **constant 1 S-52 pen unit (~0.32mm), no scale dependence** (only the
   radar-conspicuous `CONRAD=1` coast adds a 3-unit `CHMGF` underlay). The
   gradient is a Qt-only depth cue; width/zoom-ramp is our choice (provider:
   `width_px=6`, ramped 1px→6px). If the gradient still disappoints, the
   fallback is the wx-style crisp line, drawn via the existing shader AA-line
   (`makeAaLineNode`, screen-fixed width) -- NOT tessellated.

4. **Area boundary lines not drawn at all** (Qt `RenderAreaToSG` only emits AC
   fill + AP pattern, no LS/LC border). NOAA gets borders/contours from separate
   COALNE/DEPCNT *line* features, which o-charts also has — so this is lower
   priority, but S-52 PLAIN/SYMBOLISED boundary rules are not honoured for areas.

5. **MULTIPOINT (soundings) not decoded** in the OSENC path (record 83). NOAA
   path formats soundings as labels; OSENC path has no handler.

Fixed correctness nit: `CELL_EXTENT` now uses `ELON = se_lon (d[7])` to match
wx (was `ne_lon d[5]`; equal only for axis-aligned cells).

---

## 5. Conversion plan (all geometry paths → one SG pipeline)

Goal (user direction): decode every incoming format into the **same `S57Obj` +
shared SG emit** so the rendering pipeline is common as far down as possible, and
the o-charts SENC cache stores a form that rejoins that path early.

Ordered work:

1. **Verify line decode against wx for one overview + one detail cell.** Dump the
   resolved segments and compare to wx's `m_ls_list` for the same cell. Confirm
   whether the overview "web" is correct-but-coarse data or a real bug. (Use the
   `OCPN_QT_OESU_TEST` hook + targeted logging.)

2. **Apply SCAMIN to lines and areas.** Plumb each object's SCAMIN (from the
   `SCAMIN` attribute / S-52) into the SG prims and cull in the consumer, as
   already done for point symbols. Likely removes most overview webbing
   legitimately.

3. **Edge→ring stitching for area boundaries.** One reusable routine: resolve
   triples to segments, stitch by endpoint into closed rings, keep winding.
   Feed LNDARE rings to `landContours` (re-enable coast shade); reuse for area
   border line rules.

4. **Honour area boundary symbology** (LS/LC rules) in `RenderAreaToSG`, matching
   `RenderAreaToGL`.

5. **MULTIPOINT/soundings** handler in `decodeOsenc` (mirror the NOAA sounding
   label path).

6. **Converge the SENC cache** onto the common path: cache decoded geometry (or
   a canonical SENC) so o-charts cells re-enter rendering at the same point as
   `.000`/OGR cells, not via a bespoke re-decode each load.

Cross-cutting: keep the o-charts decode byte-for-byte aligned with
`Osenc::ingest200` (`gui/src/o_senc.cpp`) — it is the canonical reader and the
`oexserverd` output target.

See also: [[o-charts-native-plan]], [[qt-vector-chart-pipeline]],
[[wx-parity-method]].
