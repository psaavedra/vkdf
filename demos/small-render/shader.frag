#version 430

layout (location = 0) out vec4 out_color;

layout (location = 1) in flat vec4 my_color;

void main()
{
   out_color = my_color;
}
