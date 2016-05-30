#version 330 core

in vec2 UV;
in vec2 ReflectionUV;
out vec4 oColor;

uniform sampler2D iVGAMap;
uniform sampler2D iReflectionMap;
uniform float iAmbientLight;
uniform float iBrightness;
uniform float iContrast;
uniform float iSaturation;

vec4 FetchTexel(sampler2D sampler, vec2 texCoords);
vec3 BrightnessSaturationContrast(vec3 color, float brt, vec3 brt_tint, float sat, float con);

const vec3 brt_tint = vec3(1.0,1.0,1.5);

// Display warp.
// 0.0 = none
// 1.0/8.0 = extreme
const vec2 warp = vec2(1.0/80.0, 1.0/64.0); 

// CRT convergence (horiz. chromatic aberrations)
const float convergence = 0.0006;

////////////////////////////////////////////////////////////////////////////////

#define BlendAdd(base, blend) min(base + blend, vec3(1.0))
#define Luma(c) dot(vec3(0.299,0.587,0.114),c)

vec2 Warp(vec2 pos, vec2 warp_amount)
{
	pos = pos*2.0 - 1.0;    
	pos *= vec2(1.0+(pos.y*pos.y)*warp_amount.x, 1.0+(pos.x*pos.x)*warp_amount.y);
	return pos*0.5 + 0.5;
}

void main()
{
	vec2 pos = Warp(UV, warp);
	float chabx = clamp(abs(pos.x*2.0 - 1.0) + 0.5, 0.0, 1.0) * convergence;
	vec2 rOffset = vec2(0.0*chabx,0.0);
	vec2 gOffset = vec2(1.0*chabx,0.0);
	vec2 bOffset = vec2(2.0*chabx,0.0);
    float rValue = FetchTexel(iVGAMap, pos - rOffset).r;  
    float gValue = FetchTexel(iVGAMap, pos - gOffset).g;
    float bValue = FetchTexel(iVGAMap, pos - bOffset).b;  
	vec3 vga_color = vec3(rValue, gValue, bValue);
	vga_color = BrightnessSaturationContrast(vga_color, iBrightness, brt_tint, iSaturation, iContrast);
	vec3 reflection = vec3(Luma(texture(iReflectionMap, ReflectionUV).rgb)) * iAmbientLight;
	float vga_luma = Luma(vga_color);
	reflection *= max(0.0, 1.0 - vga_luma*4.0);
	oColor.rgb = BlendAdd(vga_color, reflection);
	oColor.a = 1.0;
}