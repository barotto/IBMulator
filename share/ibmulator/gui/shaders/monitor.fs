#version 330 core

in vec2 UV;
out vec4 oColor;

uniform float iAmbientLight;
uniform sampler2D iReflectionMap;

#define Luma(c) dot(vec3(0.299,0.587,0.114),c)

vec3 ToLinear(vec3 c);
vec3 ToSrgb(vec3 c);

void main()
{
	oColor.rgb = ToSrgb(ToLinear(texture(iReflectionMap, UV).rgb) * iAmbientLight);
	oColor.a = 1.0;
}