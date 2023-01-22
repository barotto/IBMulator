#version 330 core


#pragma stage vertex

layout(location = 0) in vec2 Position;
layout(location = 1) in ivec4 Color;

out vec4 vColor;

uniform mat4 ibmu_ModelView;
uniform mat4 ibmu_Projection;

void main()
{
	vec4 eye = ibmu_ModelView * vec4(Position.x,Position.y,0.0,1.0);
    gl_Position = ibmu_Projection * eye;
	vColor = vec4(Color.r/255.0, Color.g/255.0, Color.b/255.0, Color.a/255.0);
}


#pragma stage fragment

in vec4 vColor;
out vec4 FragColor;

void main()
{
	FragColor = vColor;
}