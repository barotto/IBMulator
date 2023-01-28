#version 330 core

#pragma stage vertex

layout(location = 0) in vec4 Position;
uniform mat4 MVP;

void main()
{
   gl_Position = MVP * Position;
}


#pragma stage fragment

out vec4 FragColor;

void main()
{
   FragColor = vec4(0.0,0.0,0.0, 1.0);
}
