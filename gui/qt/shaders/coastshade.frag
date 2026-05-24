#version 440

// Coastline land-shade fragment shader: a gradient from `maxAlpha` at the
// coast (t=0) fading to fully transparent inland (t=1), so the land edge is
// darkened and "lifts" off the water. Premultiplied-alpha output.

layout(location = 0) in float v_t;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 color;
    vec2 viewportPx;
    float qt_Opacity;
    float widthPx;
    float maxAlpha;
} ubuf;

void main() {
    float a = (1.0 - v_t) * ubuf.maxAlpha * ubuf.qt_Opacity;
    fragColor = vec4(ubuf.color.rgb * a, a);  // premultiplied (color usually black)
}
