#version 440

// AA-line fragment shader: anti-aliases the line edges with a distance
// feather, and applies an optional dash pattern along the arc length. Output
// is premultiplied alpha (scene-graph convention).
//
// Pencil mode (ubuf.pencil > 0.5, routes): a graphite look that keeps the
// centreline ruler-straight -- it only modulates fragment coverage, never the
// geometry. Three ingredients: a toothed edge (the half-width jitters a little
// along the stroke), fine graphite grain (speckle across + along), and a
// modest low-frequency pressure variation (the stroke darkens/lightens slowly).
// No skips (grain never reaches zero) and no wobble (geometry untouched).

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
    float pencil;
} ubuf;

// Cheap 2D hash -> [0,1). Standard fract/dot construction; deterministic, so
// the grain is static (no shimmer between frames).
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Value noise (smoothed hash) for the slow pressure variation.
float vnoise(float x) {
    float i = floor(x);
    float f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(hash(vec2(i, 3.0)), hash(vec2(i + 1.0, 3.0)), u);
}

void main() {
    float d = abs(v_dist);
    float halfW = ubuf.halfWidthPx;
    float ink = 1.0;  // pencil darkness multiplier (1.0 = clean line)

    if (ubuf.pencil > 0.5) {
        // Toothed edge: jitter the half-width a little along the stroke so the
        // boundary has tooth instead of a clean feather. Centreline unchanged.
        float eseed = floor(v_arcpx * 0.6);
        float ej = hash(vec2(eseed, 7.0)) + hash(vec2(eseed + 1.0, 13.0));  // ~0..2
        halfW += (ej - 1.0) * 0.9;  // +/-0.9 px

        // Fine graphite grain (screen-space speckle, across + along).
        float grain = mix(0.70, 1.0, hash(vec2(v_arcpx * 1.7, v_dist * 2.3)));

        // Modest pressure: slow low-frequency darkness along the stroke.
        float pressure = mix(0.82, 1.0, vnoise(v_arcpx * 0.03));

        ink = grain * pressure;
    }

    // Edge anti-aliasing: full coverage inside halfWidth, ramping to 0 over
    // featherPx beyond it (halfW already perturbed in pencil mode).
    float aa = clamp((halfW - d) / max(ubuf.featherPx, 0.001) + 0.5, 0.0, 1.0);

    // Dash pattern (solid when off-length is 0), with ~1px soft edges.
    float on = 1.0;
    float period = ubuf.dashOnPx + ubuf.dashOffPx;
    if (period > 0.0) {
        float m = mod(v_arcpx, period);
        on = clamp(m + 0.5, 0.0, 1.0) * clamp(ubuf.dashOnPx - m + 0.5, 0.0, 1.0);
    }

    float a = aa * on * ink * ubuf.color.a * ubuf.qt_Opacity;
    fragColor = vec4(ubuf.color.rgb * a, a);
}
