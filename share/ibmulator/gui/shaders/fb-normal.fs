#version 330 core

in vec2 UV;
out vec4 oColor;

uniform sampler2D iVGAMap;
uniform ivec2 iDisplaySize;
uniform float iBrightness;
uniform float iContrast;
uniform float iSaturation;

vec4 FetchTexel(sampler2D sampler, vec2 texCoords);
vec3 BrightnessSaturationContrast(vec3 color, float brt, vec3 brtcol, float sat, float con);

void main()
{
	oColor = FetchTexel(iVGAMap, UV);
	oColor.rgb = BrightnessSaturationContrast(oColor.rgb, iBrightness, vec3(1.0), iSaturation, iContrast);
	oColor.a = 1.0;
}