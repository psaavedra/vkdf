#version 430

layout (location = 1) out vec4 my_color;

void main()
{
   vec4 pos;
   switch (gl_VertexIndex) {
   case 0:
      pos = vec4( 0.0, 0.5, 0.0, 1.0);
      break;
   case 1:
      pos = vec4(-0.5,-0.5, 0.0, 1.0);
      break;
   case 2:
      pos = vec4(0.5, -0.5, 0.0, 1.0);
      break;
   }

   gl_Position = pos;
   my_color = vec4(0.0, 0.0, 1.0, 1.0);;
}
