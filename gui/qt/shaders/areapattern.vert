#version 440

// Area-pattern (AP) fill vertex shader.
//
// Position (location 0) is the world coord (lon, Mercator-Y) used for the MVP.
// The second attribute (location 1) is the world offset from a per-cell
// reference point, kept SMALL so it interpolates at full precision. The
// fragment derives the tile coordinate from it -- so the pattern tiles
// correctly however finely the area is tessellated. (Per-vertex UV tiling
// collapsed each sub-tile polygon to a single texel; see areapattern.frag.)

layout(location = 0) in vec2 worldPos;
layout(location = 1) in vec2 relPos;

layout(location = 0) out vec2 v_rel;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec2 kScale;       // world units -> tile repeats (scale / tilePx), per axis
    float qt_Opacity;
} ubuf;

void main() {
    gl_Position = ubuf.qt_Matrix * vec4(worldPos, 0.0, 1.0);
    v_rel = relPos;
}
