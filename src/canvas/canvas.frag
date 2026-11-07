#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 color;

layout(binding = 1) uniform sampler2D tile;

void main() {
  color = texture(tile, v_texcoord);
}
