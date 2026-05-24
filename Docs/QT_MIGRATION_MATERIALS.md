# P2.4 — Materials catalog (legacy GL shaders → Qt scene-graph materials)

> Companion to [`QT_MIGRATION_TASKS.md`](./QT_MIGRATION_TASKS.md) task **P2.4**.
> Decision record: which built-in `QSGMaterial` each legacy GL shader program
> maps to, and the residual custom-shader candidates. The shared factories
> that realise the "built-in" rows live in
> [`gui/qt/sg_helpers.{h,cpp}`](../gui/qt/sg_helpers.h).

## The legacy programs

The old GL renderer compiled **six** shader programs (`gui/src/shaders.cpp`,
the `p*_shader_program` table around lines 283–342):

| # | Legacy program | What it does | Used for |
|---|---|---|---|
| 1 | `color_tri_shader_program` | transform position, flat/interpolated colour | solid fills, lines |
| 2 | `texture_2D_shader_program` | transform position + UV, sample texture | textured quads/tiles |
| 3 | `circle_filled_shader_program` | distance-field filled circle + border | filled circles, dots |
| 4 | `texture_2DA_shader_program` | sample texture, modulate by a colour/alpha | tinted glyphs / symbols |
| 5 | `AALine_shader_program` | distance-based anti-aliased line | high-quality lines |
| 6 | `ring_shader_program` | concentric ring with sector clipping | range/bearing rings, compass |

## Mapping to the Qt scene graph

The scene graph runs on Qt RHI (Metal / Vulkan / D3D / GL chosen per platform).
Qt ships built-in materials that cover the bulk of the catalogue with **no GLSL
and no `qsb` step**:

| # | Legacy program | Qt replacement | Custom shader? |
|---|---|---|---|
| 1 | `color_tri` | **`QSGFlatColorMaterial`** (single colour) / `QSGVertexColorMaterial` (per-vertex) | no — built-in (`sg::makeFlatColorNode`) |
| 2 | `texture_2D` | **`QSGTextureMaterial`** / `QSGOpaqueTextureMaterial`; `QSGImageNode` for a single quad | no — built-in (`sg::makeTextureNode`, `QQuickWindow::createImageNode`) |
| 3 | `circle_filled` | **tessellated triangle-fan geometry** + `QSGFlatColorMaterial`, edges smoothed by 4× MSAA | no for the core path; optional DF shader later |
| 4 | `texture_2DA` | **pre-modulate the `QImage` on the CPU** (`QPainter`), then `QSGTextureMaterial`; pure-alpha fades via `QSGOpacityNode` | no in practice — the label/symbol path already paints colour into the image |
| 5 | `AALine` | **custom `QSGMaterialShader`** — `aa_line.{h,cpp}` + `shaders/aaline.{vert,frag}` (baked via qsb). Screen-space quad expansion (width as a px uniform, zoom-invariant), distance-feather AA, dash via arc length | **DONE** — this is now the single shared line path for S-52 chart lines + all overlays, replacing the parallel-strip workaround |
| 6 | `ring` | **line-loop / annulus-triangle geometry** + `QSGFlatColorMaterial` (display-anchored), built by the P2.6 scene-graph DC | no for the core path; optional DF shader later |

## Conclusion

- **Two** programs (1, 2) map directly onto built-in materials and are now the
  shared `sg::makeFlatColorNode` / `sg::makeTextureNode` factories — already
  adopted by every current provider (`s52_vector_chart_provider`,
  `gshhs_world_provider`, `chart_boundary_provider`, and `RasterChartProvider`
  via `QSGImageNode`).
- **One** (4) is avoided entirely by doing the colour modulation on the CPU
  when the source `QImage` is built — which the text-label / symbol pipeline
  already does.
- **Three** (3, 5, 6) are drawable today with plain geometry + MSAA. Of these,
  only the **AA-line** shader (5) is a realistic custom-`QSGMaterialShader`
  candidate, and only as a fidelity upgrade if **P2.12** profiling shows the
  MSAA + parallel-strip approach is insufficient. Circles/rings (3, 6) only
  need a custom shader for crisp distance-field anti-aliasing at extreme
  zoom — not a baseline requirement.

So the residual **genuinely-required** custom shader count was **0–1**
(AA-line), below the original "~2–3" estimate. The **AA-line shader has now
been implemented** (`aa_line.{h,cpp}`) — the parallel-strip technique didn't
give clean lines at arbitrary widths, so it was replaced with one shared
custom `QSGMaterialShader` used by both the S-52 chart-line path and every
overlay. Raw RHI (`beforeRendering`/`afterRendering`, **P2.13**) remains an
unused escape hatch.
