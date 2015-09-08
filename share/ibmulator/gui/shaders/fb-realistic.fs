#version 330 core

in vec2 UV;
in vec2 ReflectionUV;
out vec4 oColor;

uniform sampler2D iVGAMap;
uniform sampler2D iReflectionMap;
uniform float iBrightness;
uniform float iContrast;
uniform float iSaturation; 

vec4 FetchTexel(sampler2D sampler, vec2 texCoords);
vec3 BrightnessSaturationContrast(vec3 color, float brt, vec3 brtcol, float sat, float con);

// Display warp.
// 0.0 = none
// 1.0/8.0 = extreme
const vec2 warp = vec2(1.0/80.0, 1.0/64.0); 


////////////////////////////////////////////////////////////////////////////////

#define BlendAdd(base, blend) min(base + blend, vec3(1.0))

vec2 Warp(vec2 pos, vec2 warp_amount)
{
	pos = pos*2.0 - 1.0;    
	pos *= vec2(1.0+(pos.y*pos.y)*warp_amount.x, 1.0+(pos.x*pos.x)*warp_amount.y);
	return pos*0.5 + 0.5;
}

void main()
{
	vec2 pos = Warp(UV, warp);
	vec3 color = FetchTexel(iVGAMap, pos).rgb;

	color = BrightnessSaturationContrast(color, iBrightness, vec3(1.0,1.0,1.5), iSaturation, iContrast);
	vec3 reflection = texture(iReflectionMap, ReflectionUV).rgb;
	float luma = dot(vec3(0.299,0.587,0.114),color);
	reflection *= max(0.0, 1.0 - luma*2.2);
	color = BlendAdd(color, reflection);
		
	oColor = vec4(color, 1.0);
}