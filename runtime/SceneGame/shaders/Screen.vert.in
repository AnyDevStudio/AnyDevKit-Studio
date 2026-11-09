#version 450
struct Frame {
    vec4 rectangle;
    vec4 color;
    vec4 viewport; // xy: extent, z: mode (0 gradient, 1 glyph, 2 solid)
    uvec4 glyph;
};
layout(set = 0, binding = 0, std430) readonly buffer Frames {
    Frame values[];
} frames;
layout(location = 0) out vec2 coordinate;
layout(location = 1) flat out vec4 color;
layout(location = 2) flat out uvec4 glyph;
layout(location = 3) flat out uint mode;
void main() {
    const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    Frame frame = frames.values[gl_VertexIndex / 6];
    vec2 corner = corners[gl_VertexIndex % 6];
    mode = uint(frame.viewport.z);
    coordinate = corner * (mode == 1u ? vec2(5, 7) : vec2(1));
    color = frame.color;
    glyph = frame.glyph;
    vec2 position = frame.rectangle.xy + corner * frame.rectangle.zw;
    gl_Position = vec4(position / frame.viewport.xy * 2.0 - 1.0, 0.0, 1.0);
}
