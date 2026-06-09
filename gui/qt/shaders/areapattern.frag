#version 440

// Area-pattern (AP) fill fragment shader.
//
// Computes the tile coordinate per-fragment from the (small) world offset and
// wraps it with fract() -- a manual Repeat. This matters for two reasons:
//   1. QSGTexture::Repeat was not honoured for these tiles (every fragment
//      sampled the clamped edge texel, so a finely tessellated area collapsed
//      to one flat colour and the sparse stipple vanished). fract() makes the
//      wrap explicit and sampler-independent.
//   2. The world->tile multiply happens HERE, on a small interpolated offset,
//      not as a large interpolated UV varying -- so the fractional part (which
//      texel) keeps full precision even far from the cell origin.
// Nearest sampling (set on the texture) matches wx's pattern blit. The tile is
// straight-alpha; output premultiplied for the scene graph's blending.

layout(location = 0) in vec2 v_rel;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D src;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec2 kScale;
    float qt_Opacity;
} ubuf;

void main() {
    vec2 uv = fract(v_rel * ubuf.kScale);
    vec4 c = texture(src, uv);
    float a = c.a * ubuf.qt_Opacity;
    fragColor = vec4(c.rgb * a, a);
}
