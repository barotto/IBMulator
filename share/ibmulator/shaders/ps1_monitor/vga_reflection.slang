#version 330 core

// inspired by https://www.shadertoy.com/view/XdtfzX

#pragma parameter vgaScale     "VGA image scale" 0.85 0.1 1.0 0.01
#pragma parameter crtCurvature "CRT Curve Radius" 8.0 0.0 10.0 0.1
#pragma parameter blurSize     "VGA Reflection Blur" 5.0 0.0 5.0

#define REFLECTION_BLUR_ITERATIONS 5
#define REFLECTION_BLUR_SIZE 0.04 * params.blurSize

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
    float vgaScale;
    float crtCurvature;
    float blurSize;
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

#include "../color_functions.glsl"

in vec2 vTexCoord;
uniform sampler2D Original;
out vec4 FragColor;

#undef VGA_REFLECTION
#define VGA_REFLECTION 0
#include "functions.glsl"


float RoundSquare(vec2 p, vec2 b, float r)
{
    return length(max(abs(p)-b, 0.0)) - r;
}

// Calculate normal to distance function and move along
// normal with distance to get point of reflection
vec2 BorderReflect(vec2 p, float r)
{
    float eps = 0.0001;
    vec2 epsx = vec2(eps, 0.0);
    vec2 epsy = vec2(0.0, eps);
    vec2 b = (1. + vec2(r,r)) * 0.5;
    r /= 3.0;
    
    p -= 0.5;
    vec2 normal = vec2(RoundSquare(p-epsx,b,r)-RoundSquare(p+epsx,b,r),
                       RoundSquare(p-epsy,b,r)-RoundSquare(p+epsy,b,r))/eps;
    float d = RoundSquare(p, b, r);
    p += 0.5;
    p = vec2(1. - p.x, p.y);
    return p + d*normal;
}

void main()
{
    vec3 reflCol = vec3(0.0);
    vec2 uvC = VGACurv(vTexCoord, params.vgaScale, params.crtCurvature);

    for(int i = 0; i < REFLECTION_BLUR_ITERATIONS; i++)
    {
        vec2 uvR = BorderReflect(
            uvC
                + (vec2( Rand(uvC + float(i)), Rand(uvC + float(i) + 0.1) ) - 0.5) 
                * REFLECTION_BLUR_SIZE
            , 0.05
        );
        
        vec3 vgaCol = ToLinear(texture(Original, vec2(1.-uvR.x, uvR.y)).rgb);
        vgaCol = BrightnessSaturationContrast(vec4(vgaCol,1.0),
            globals.ibmu_Brightness, BRT_TINT * globals.ibmu_PowerOn, globals.ibmu_Saturation, globals.ibmu_Contrast);

        reflCol += vgaCol.rgb / float(REFLECTION_BLUR_ITERATIONS);
    }

    // output is in linear space
    FragColor = vec4(reflCol, 1.0);
}
