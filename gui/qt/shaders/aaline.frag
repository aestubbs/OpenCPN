#version 440

// AA-line fragment shader: anti-aliases the line edges with a distance
// feather, and applies an optional dash pattern along the arc length. Output
// is premultiplied alpha (scene-graph convention).

layout(location = 0) in float v_dist;   // signed px distance from centreline
layout(location = 1) in float v_arcpx;  // arc length in device px

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 color;
    vec2 viewportPx;
    float qt_Opacity;
    float halfWidthPx;
    float featherPx;
    float dashOnPx;
    float dashOffPx;
    float pxPerWorld;
} ubuf;

void main() {
    // Edge anti-aliasing: full coverage inside halfWidth, ramping to 0 over
    // featherPx beyond it.
    float d = abs(v_dist);
    float aa = clamp((ubuf.halfWidthPx - d) / max(ubuf.featherPx, 0.001) + 0.5,
                     0.0, 1.0);

    // Dash pattern (solid when off-length is 0), with ~1px soft edges: full
    // coverage in [0, dashOnPx] of each period, fading over half a px at each
    // end, zero across the gap.
    float on = 1.0;
    float period = ubuf.dashOnPx + ubuf.dashOffPx;
    if (period > 0.0) {
        float m = mod(v_arcpx, period);
        on = clamp(m + 0.5, 0.0, 1.0) * clamp(ubuf.dashOnPx - m + 0.5, 0.0, 1.0);
    }

    float a = aa * on * ubuf.color.a * ubuf.qt_Opacity;
    fragColor = vec4(ubuf.color.rgb * a, a);
}
