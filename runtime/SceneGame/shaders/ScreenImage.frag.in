#version 450
// Fragment shader for textured quads: images, sprite frames and tiles. Screen.frag still draws
// everything else, so scenes without images never reach this one. Texels are premultiplied, see
// docs/scene-format.md. The vertex shader already narrowed `coordinate` to the quad's part of the
// texture, so this shader does not care whether it draws a whole image or one tile.
layout(set = 0, binding = 0) uniform sampler imageSampler;
layout(set = 0, binding = 1) uniform texture2D imageTexture;
layout(location = 0) in vec2 coordinate;
layout(location = 0) out vec4 outputColor;
void main() {
    outputColor = textureLod(sampler2D(imageTexture, imageSampler), coordinate, 0.0);
}
