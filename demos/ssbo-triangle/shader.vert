#version 450 core

layout(std140, set = 1, binding = 0) uniform ubo {
    mat4 mvp;
} UBO;

layout (location = 0) in vec4 pos;

layout (location = 1) out float color;

void main() {
   color = 1.0f;

   gl_Position = UBO.mvp * pos;
}
