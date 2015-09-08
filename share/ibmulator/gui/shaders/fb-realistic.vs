#version 330 core

layout(location = 0) in vec3 iVertex;

out vec2 UV;
out vec2 ReflectionUV;

uniform vec2 iVGAScale;
uniform vec2 iReflectionScale;
uniform mat4 iModelView;

void main()
{
	gl_Position = iModelView * vec4(iVertex,1);
	UV = (iVertex.xy*iVGAScale + vec2(1,1)) / 2.0;
	UV.y = 1.0 - UV.y;

	ReflectionUV = (iVertex.xy*iReflectionScale + vec2(1,1)) / 2.0;
	ReflectionUV.y = 1.0 - ReflectionUV.y;
}