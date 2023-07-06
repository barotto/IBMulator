#version 330 core

#pragma parameter curvature "Curve Radius" 5.0 0.0 10.0 0.1
#pragma parameter vga_scale "VGA scale" 0.8 0.1 1.0 0.01
#pragma parameter convergence "Horiz. chromatic aberrations" 0.0006 0.0 0.001 0.0001


layout(std140) uniform Push
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    float Brightness;
    float Contrast;
    float Saturation;
    float Ambient;

} params;

layout(std140) uniform UBO
{
    mat4 MVP;
    float curvature;
    float vga_scale;
    float convergence;
} global;

#pragma stage vertex

layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord.xy;
}


#pragma stage fragment

#include "../common/filter_bicubic.glsl"
#include "../common/color_functions.glsl"



in vec2 vTexCoord;
uniform sampler2D Original;
uniform sampler2D Reflection;
uniform vec4 OutputSize;
out vec4 FragColor;
const vec3 brt_tint = vec3(1.0,1.0,1.5);

vec2 CurvedSurface(vec2 uv, float r)
{
    return r * uv/sqrt(r * r - dot(uv, uv));
}

vec2 VGACurv(vec2 uv) 
{
    uv = (uv - 0.5) * 2.0; // move to [-1,1]
    uv /= vec2(params.OutputSize.y/params.OutputSize.x, 1.0); // make it square
    uv /= global.vga_scale; // resize
    uv = CurvedSurface(uv, global.curvature); // apply curvature
    uv /= vec2(4.0/3.0, 1.0); // apply aspect ratio
    uv = (uv / 2.0) + 0.5; // move back to [0,1]
	return uv;
}

void main()
{
    float convergence = global.convergence - global.convergence * params.Monochrome;
	
    vec2 pos = VGACurv(vTexCoord * (convergence*2.0 + 1.0) - convergence);
	float chabx = clamp(abs(pos.x*2.0 - 1.0) + 0.5, 0.0, 1.0) * convergence;
	vec2 rOffset = vec2(-1.0*chabx, 0.0);
	vec2 gOffset = vec2( 0.0*chabx, 0.0);
	vec2 bOffset = vec2( 1.0*chabx, 0.0);
	
	vec4 red = FetchTexelBicubic(Original, pos - rOffset);
	float rValue = ToLinear(red.rgb).r;
	float gValue = ToLinear(FetchTexelBicubic(Original, pos - gOffset).rgb).g;
	float bValue = ToLinear(FetchTexelBicubic(Original, pos - bOffset).rgb).b;

	vec4 vga_color = vec4(rValue, gValue, bValue, red.a);
	vga_color.rgb = BrightnessSaturationContrast(vga_color, params.Brightness, brt_tint, params.Saturation, params.Contrast);

	vec3 reflection = ToLinear(texture(Reflection, vTexCoord).rgb) * params.Ambient;

	// This will act as a crude substitute for proper tone mapping.
	// VGA image will be prioritized for usability reasons.
	float vga_luma = Luma(vga_color.rgb);
	const float luma_w = 1.0;
	reflection *= max(0.0, 1.0 - vga_luma*luma_w);
	
	FragColor.rgb = ToSrgb(BlendAdd(vga_color.rgb, reflection));
	FragColor.a = 1.0;

    //vec2 vgaUV = VGACurv(vTexCoord);
    //vec4 vgaColor = FetchTexelBicubic(Original, vgaUV);
    //vec4 reflectionColor = texture(Reflection, vTexCoord);
    
    //FragColor = vec4(((vgaColor + reflectionColor)/ 2.0).rgb, 1.0);
}
