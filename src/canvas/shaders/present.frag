#version 440

layout(location = 0) out vec4 color;

layout(std140, binding = 0) uniform Blend {
  vec2 size;
  float opacity;
  int mode;
};
layout(binding = 1) uniform sampler2D composite;

void main() {
  color = texture(composite, gl_FragCoord.xy / size);
}
