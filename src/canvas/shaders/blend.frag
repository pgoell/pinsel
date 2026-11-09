#version 440

layout(location = 0) out vec4 color;

layout(std140, binding = 0) uniform Blend {
  vec2 size;
  float opacity;
  // TileStore::BlendMode
  int mode;
};
layout(binding = 1) uniform sampler2D layer;
layout(binding = 2) uniform sampler2D backdrop;

// The term each mode adds to plain source-over, in premultiplied colors, as
// QPainter computes its composition modes of the same names; the CPU flatten
// uses those. https://www.w3.org/TR/compositing-1/#blendingseparable
vec3 blended(vec4 s, vec4 d) {
  if (mode == 1) {
    return s.rgb * d.rgb;
  }
  if (mode == 2) {
    return s.rgb * d.a + d.rgb * s.a - s.rgb * d.rgb;
  }
  if (mode == 3) {
    vec3 dark = 2.0 * s.rgb * d.rgb;
    vec3 light = vec3(s.a * d.a) - 2.0 * (d.a - d.rgb) * (s.a - s.rgb);
    return mix(dark, light, step(vec3(d.a), 2.0 * d.rgb));
  }
  return s.rgb * d.a;
}

void main() {
  // Every target has the size of the widget, so a pixel's position finds it in
  // each, whichever way up the backend stores them.
  vec2 at = gl_FragCoord.xy / size;
  vec4 s = texture(layer, at) * opacity;
  vec4 d = texture(backdrop, at);
  color = vec4(s.rgb * (1.0 - d.a) + d.rgb * (1.0 - s.a) + blended(s, d),
               s.a + d.a - s.a * d.a);
}
