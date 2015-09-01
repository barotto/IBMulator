#version 330 core

in vec2 UV;
out vec4 oColor;

uniform sampler2D iChannel0;
uniform float iBrightness;
uniform float iContrast;
uniform float iSaturation; 

vec4 FetchTexel(sampler2D sampler, vec2 texCoords);
vec3 BrightnessSaturationContrast(vec3 color, float brt, float sat, float con);

// Display warp.
// 0.0 = none
// 1.0/8.0 = extreme
const vec2 warp = vec2(1.0/80.0, 1.0/64.0); 


////////////////////////////////////////////////////////////////////////////////


vec2 Warp(vec2 pos, vec2 warp_amount)
{
	pos = pos*2.0 - 1.0;    
	pos *= vec2(1.0+(pos.y*pos.y)*warp_amount.x, 1.0+(pos.x*pos.x)*warp_amount.y);
	return pos*0.5 + 0.5;
}

void main()
{
	vec2 uv = UV;
	uv.t = 1.0 - uv.t;
	vec2 pos = Warp(uv, warp);
	vec3 color = FetchTexel(iChannel0, pos).rgb;

	color = BrightnessSaturationContrast(color, iBrightness, 1.0, iContrast);
		
	float a = min(color.r + color.g + color.b, 1.0);
	oColor = vec4(color, a);
}