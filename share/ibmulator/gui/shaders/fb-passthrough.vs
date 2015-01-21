#version 330 core

// Input vertex data
layout(location = 0) in vec3 iVertex;

// Output data; will be interpolated for each fragment.
out vec2 UV;

uniform mat4 iModelView;

void main()
{
	gl_Position = iModelView * vec4(iVertex,1);
	UV = (iVertex.xy + vec2(1,1)) / 2.0;
}