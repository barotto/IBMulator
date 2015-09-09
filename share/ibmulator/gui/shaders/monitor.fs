#version 330 core

in vec2 UV;
out vec4 oColor;

uniform float iAmbientLight;
uniform sampler2D iReflectionMap;

#define Luma(c) dot(vec3(0.299,0.587,0.114),c)

void main()
{
	oColor.rgb = vec3(Luma(texture(iReflectionMap, UV).rgb)) * iAmbientLight;
	oColor.a = 1.0;
}