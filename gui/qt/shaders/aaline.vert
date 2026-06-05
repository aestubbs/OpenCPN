#version 440

// AA-line vertex shader (P2.4 #5 / P2.13). Expands a polyline into a quad
// strip in SCREEN space: the centreline is transformed by the scene-graph
// MVP, then offset perpendicular by a pixel half-width, so the on-screen
// width is constant regardless of zoom -- no per-zoom geometry rebuild.

layout(location = 0) in vec2 center;   // centreline position (world coords)
layout(location = 1) in vec2 normal;   // unit perpendicular (world direction)
layout(location = 2) in float side;    // +1 / -1 : which edge of the quad
layout(location = 3) in float arclen;  // cumulative arc length (world units)
layout(location = 4) in vec2 tangent;  // unit along-segment (world direction)
layout(location = 5) in float cap;     // -1 start / +1 end : square-cap extend

layout(location = 0) out float v_dist;   // signed px distance from centreline
layout(location = 1) out float v_arcpx;  // arc length in device px (for dashes)

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;     // combined model-view-projection (world -> clip)
    vec4 color;
    vec2 viewportPx;    // device-pixel viewport size
    float qt_Opacity;
    float halfWidthPx;  // half line width, device px
    float featherPx;    // AA ramp width, device px
    float dashOnPx;     // dash on length, device px (0 = solid)
    float dashOffPx;    // dash gap length, device px
    float pxPerWorld;   // device px per world unit (for arc-length -> px)
    float pencil;       // 0 = clean AA line, 1 = pencil (used in .frag)
} ubuf;

void main() {
    // Project the centreline and a point one world-normal away, take the
    // difference in screen pixels to get the screen-space normal direction.
    vec4 c0 = ubuf.qt_Matrix * vec4(center, 0.0, 1.0);
    vec4 cn = ubuf.qt_Matrix * vec4(center + normal, 0.0, 1.0);
    vec2 p0 = c0.xy / c0.w;
    vec2 pn = cn.xy / cn.w;
    vec2 dn = (pn - p0) * ubuf.viewportPx;
    float ln = length(dn);
    vec2 ndir = (ln > 0.0) ? dn / ln : vec2(0.0, 1.0);

    // Screen-space tangent direction (same projection trick) for the square
    // cap: each segment is its own quad, extended halfWidth along its own
    // direction at both ends so consecutive segments overlap at the joints --
    // no miter pinch at acute angles. Guard against a zero tangent so a
    // degenerate segment can't produce a NaN offset (cap * NaN).
    vec4 ct = ubuf.qt_Matrix * vec4(center + tangent, 0.0, 1.0);
    vec2 pt = ct.xy / ct.w;
    vec2 dt = (pt - p0) * ubuf.viewportPx;
    float lt = length(dt);
    vec2 tdir = (lt > 0.0) ? dt / lt : vec2(0.0, 0.0);

    float ext = ubuf.halfWidthPx + ubuf.featherPx;  // extend for the AA ramp
    vec2 offPx = ndir * ext * side + tdir * ubuf.halfWidthPx * cap;
    vec2 offNdc = offPx / ubuf.viewportPx * 2.0;

    gl_Position = vec4(c0.xy + offNdc * c0.w, c0.z, c0.w);
    v_dist = side * ext;
    v_arcpx = arclen * ubuf.pxPerWorld;
}
