#version 330 core

in vec2 UV;
out vec4 oColor;

uniform sampler2D iChannel0;
uniform float iBrightness; // range [0.0,1.0]
uniform float iContrast;   // range [0.0,1.0]

vec4 FetchTexel(sampler2D sampler, vec2 texCoords);

// Display warp.
// 0.0 = none
// 1.0/8.0 = extreme
const vec2 warp = vec2(1.0/80.0, 1.0/64.0); 

const float brightness_center = 0.7;
const vec3  brightness_tint = vec3(1.0,1.0,1.5);
const float contrast_min = 0.5;
const float contrast_max = 1.5;


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
	
	float contrast = (contrast_max - contrast_min)*iContrast + contrast_min;
	color.rgb = (color.rgb - 0.5) * contrast + 0.5;
	color.rgb += (iBrightness - brightness_center) * brightness_tint;
	
	float a = min(color.r + color.g + color.b, 1.0);
	oColor = vec4(color, a);
}