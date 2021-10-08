#version 330 core

// Input vertex data
layout(location = 0) in vec3 iVertex;

// Output data; will be interpolated for each fragment.
out vec2 UV;

uniform mat4 iModelView;
uniform mat4 iProjection;

void main()
{
	gl_Position = iProjection * (iModelView * vec4(iVertex,1));
	UV = iVertex.xy;
}