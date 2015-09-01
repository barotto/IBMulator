#version 330 core

in vec2 UV;
out vec4 oColor;

uniform sampler2D iChannel0;
uniform ivec2 iDisplaySize;
uniform float iBrightness;
uniform float iContrast;

vec4 FetchTexel(sampler2D sampler, vec2 texCoords);
vec3 BrightnessSaturationContrast(vec3 color, float brt, float sat, float con);

void main()
{
	vec2 uv = UV;
	uv.t = 1.0-uv.t;
	oColor = FetchTexel(iChannel0, uv);
	oColor.a = 1.0;
	oColor.rgb = BrightnessSaturationContrast(oColor.rgb, iBrightness, 1.0, iContrast);
}