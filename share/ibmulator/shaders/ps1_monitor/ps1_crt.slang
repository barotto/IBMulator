#version 330 core

#pragma parameter vgaScale         "VGA image scale"                  0.85 0.1 1.0 0.01
#pragma parameter vgaConvergence   "VGA Convergence"                  0.5 0.0 1.0 0.1
#pragma parameter crtCurvature     "CRT Curve Radius"                 8.0 0.0 10.0 0.1
#pragma parameter crtEnvMapEnabled "CRT Environment map enabled"      1.0 0.0 1.0 1.0
#pragma parameter ambientFromCRT   "Max. ambient light from the CRT"  0.1 0.0 1.0 0.05


layout(std140) uniform Globals
{
    mat4  MVP;
    vec4  OutputSize;
    float ibmu_Brightness;
    float ibmu_Contrast;
    float ibmu_Saturation;
    float ibmu_Ambient;
    int   ibmu_Monochrome;
    int   ibmu_PowerOn;
} globals;

layout(std140) uniform Params
{
    float vgaConvergence;
    float vgaScale;
    float crtCurvature;
    float crtEnvMapEnabled;
    float ambientFromCRT;
} params;


#pragma stage vertex

layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
out vec2 vTexCoord;

void main()
{
    gl_Position = globals.MVP * Position;
    vTexCoord = TexCoord.xy;
}


#pragma stage fragment

#include "../filter_bicubic.glsl"
#include "../color_functions.glsl"

in vec2 vTexCoord;

uniform sampler2D Original;
uniform sampler2D CRTEnv;
uniform vec4 CRTEnvSize;
#if VGA_REFLECTION
uniform sampler2D Reflection;
uniform vec4 ReflectionSize;
#endif

out vec4 FragColor;

#define VGA_SCALE   (params.vgaScale * CRT_SCALE)
#define CONVERGENCE (params.vgaConvergence * 0.001)

#include "functions.glsl"

void main()
{
    vec2 crtUV = ScaleUV(vTexCoord, CRT_SCALE, vec2(CRTEnvSize.x/CRTEnvSize.y, 1.0));

    // === VGA color
    float convergence = CONVERGENCE - CONVERGENCE * globals.ibmu_Monochrome;
    vec2 vgaUV = VGACurv(vTexCoord * (convergence*2.0 + 1.0) - convergence, VGA_SCALE, params.crtCurvature);
    
    float chabx = clamp(abs(vgaUV.x*2.0 - 1.0) + 0.5, 0.0, 1.0) * convergence;
    vec2 rOffset = vec2(-1.0*chabx, 0.0 );
    vec2 gOffset = vec2( 0.0*chabx, 0.0 );
    vec2 bOffset = vec2( 1.0*chabx, 0.0 );

    float rValue = ToLinear(FetchTexelBicubic(Original, vgaUV - rOffset).rgb).r;
    float gValue = ToLinear(FetchTexelBicubic(Original, vgaUV - gOffset).rgb).g;
    float bValue = ToLinear(FetchTexelBicubic(Original, vgaUV - bOffset).rgb).b;

    vec4 vgaColor = vec4(rValue, gValue, bValue, 1.0);
    vgaColor.rgb = BrightnessSaturationContrast(vgaColor,
        globals.ibmu_Brightness, BRT_TINT * globals.ibmu_PowerOn, globals.ibmu_Saturation, globals.ibmu_Contrast);

    if(globals.ibmu_Brightness > 1.0) {
        vgaColor.rgb *= min(Vignette(crtUV, 200.0, 0.2), 1.0);
    }

    // === Ambient color & intensity
    vec3 ambient3 = Ambient();
    float ambient1 = Luma(ambient3);

    // === CRT glass effects
    vec3 crtEnvColor = ToLinear(texture(CRTEnv, crtUV).rgb);
    crtEnvColor *= ambient1 * params.crtEnvMapEnabled; // the higher the ambient light, the grater the reflections are

    // This will act as a crude substitute for proper tone mapping.
    // VGA image will be prioritized for usability reasons.
    float vgaLuma = Luma(vgaColor.rgb);
    const float luma_w = 1.0;
    crtEnvColor *= max(0.0, 1.0 - vgaLuma*luma_w);

    vec3 crtColor = BlendAdd(vgaColor.rgb, crtEnvColor);
    
    // === Final color with alpha blending
    FragColor.rgb = ToSrgb(crtColor);
    if(crtUV.x >= 0.0 && crtUV.x <= 1.0 && crtUV.y >= 0.0 && crtUV.y <= 1.0) {
        FragColor.a = 1.0;
    } else {
        FragColor.a = 0.0;
    }
}