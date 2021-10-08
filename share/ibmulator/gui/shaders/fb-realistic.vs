#version 330 core

layout(location = 0) in vec3 iVertex;

out vec2 UV;
out vec2 ReflectionUV;

uniform vec2 iVGAScale;
uniform vec2 iReflectionScale;
uniform mat4 iProjection;
uniform mat4 iModelView;

void main()
{
	gl_Position = iProjection * (iModelView * vec4(iVertex,1));
	UV = iVertex.xy * iVGAScale;
	ReflectionUV = iVertex.xy*iReflectionScale - vec2((iReflectionScale - 1.0) / 2.0);
}