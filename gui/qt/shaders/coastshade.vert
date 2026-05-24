#version 440

// Coastline land-shade vertex shader. Expands each coastline vertex into the
// LAND side by a screen-fixed pixel width: t=0 sits on the coast, t=1 is
// `widthPx` pixels inland (the inward direction is supplied per vertex from
// the contour winding). Screen-space expansion like the AA-line shader, so
// the shade band is a constant pixel width at any zoom.

layout(location = 0) in vec2 center;   // coastline vertex (world coords)
layout(location = 1) in vec2 inward;   // unit inward (landward) dir, world
layout(location = 2) in float t;       // 0 at coast .. 1 inland

layout(location = 0) out float v_t;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 color;
    vec2 viewportPx;
    float qt_Opacity;
    float widthPx;
    float maxAlpha;
} ubuf;

void main() {
    vec4 c0 = ubuf.qt_Matrix * vec4(center, 0.0, 1.0);
    vec4 c1 = ubuf.qt_Matrix * vec4(center + inward, 0.0, 1.0);
    vec2 p0 = c0.xy / c0.w;
    vec2 p1 = c1.xy / c1.w;
    vec2 dir = (p1 - p0) * ubuf.viewportPx;
    float len = length(dir);
    vec2 ndir = (len > 0.0) ? dir / len : vec2(0.0, 0.0);

    vec2 offPx = ndir * ubuf.widthPx * t;
    vec2 offNdc = offPx / ubuf.viewportPx * 2.0;
    gl_Position = vec4(c0.xy + offNdc * c0.w, c0.z, c0.w);
    v_t = t;
}
