#version 450 core

#extension GL_AMD_gpu_shader_half_float: enable

#define WIDTH 1024 //FIXME: hardcoded (perhaps an uniform)

layout (location = 0) out ${vector_type_name} outColor;

layout (set = 0, binding = 3) buffer ssbo {
   ${scalar_type_name} out_value[];
} SSBO;

layout (location = 1) in float color;

void main() {
  ${scalar_type_name} value = ${scalar_type_name}(color);
  ivec2 pos = ivec2(gl_FragCoord).xy;

  outColor = ${vector_type_name}(value);
  SSBO.out_value[pos.y * WIDTH + pos.x] = value;
}
