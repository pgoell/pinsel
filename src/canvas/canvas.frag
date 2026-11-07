#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 color;

layout(binding = 1) uniform sampler2D tile;
layout(binding = 2) uniform sampler2D mask;

// Premultiplied, like the tiles: red at half strength.
const vec4 kMaskTint = vec4(0.5, 0.0, 0.0, 0.5);

void main() {
  vec4 image = texture(tile, v_texcoord);
  float covered = texture(mask, v_texcoord).r;
  color = kMaskTint * covered + image * (1.0 - kMaskTint.a * covered);
}
