#version 330 core

#pragma parameter vgaReflInt     "VGA Reflection intensity"        0.5 0.0 1.0 0.1
#pragma parameter ambientFromCRT "Max. ambient light from the CRT" 0.1 0.0 1.0 0.05

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
    float vgaReflInt;
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

#include "../common/filter_bicubic.glsl"
#include "../common/color_functions.glsl"

in vec2 vTexCoord;
uniform sampler2D Original;
uniform sampler2D CRT;
uniform vec4 CRTSize;
uniform sampler2D Monitor;
uniform vec4 MonitorSize;
uniform sampler2D LEDBloom;
uniform vec4 LEDBloomSize;
uniform sampler2D Reflection;
uniform vec4 ReflectionSize;

layout(location = 0) out vec4 FragColor;

#define LED_WIDTH  (LEDBloomSize.x/MonitorSize.x)
#define LED_HEIGHT (LEDBloomSize.y/MonitorSize.y)
#define LED_UV_X0 (LED_X - (LED_WIDTH/2.0))
#define LED_UV_X1 (LED_X + (LED_WIDTH/2.0))
#define LED_UV_Y0 (LED_Y - (LED_HEIGHT/2.0))
#define LED_UV_Y1 (LED_Y + (LED_HEIGHT/2.0))

#include "functions.glsl"

void main()
{
    // === Ambient color & intensity
    vec3 ambient3 = Ambient();
    float ambient1 = Luma(ambient3);

    // === The monitor (bezel and outer frame)
    vec2 monitorUV = ScaleUV(vTexCoord, 1.0, vec2(MonitorSize.x/MonitorSize.y, 1.0));
    vec4 monitorColor = texture(Monitor, monitorUV);
    monitorColor.rgb = ToLinear(monitorColor.rgb);
    monitorColor.rgb *= ambient3; // apply the ambient tint

    float d_outer = RoundBox( vTexCoord, REFL_LINE_SIZE, REFL_LINE_RADIUS, REFL_LINE_DIST );
    vec3 reflection = texture(Reflection, vTexCoord).rgb // Reflection texture is already in linear space
            * smoothstep(REFL_LINE_SMOOTH, 0.0, d_outer)
            * monitorColor.a
            ;

    vec3 theMonitor = monitorColor.rgb * monitorColor.a;
    vec3 theReflection = reflection * params.vgaReflInt * (1.0 - ambient1);
    vec3 theLED = vec3(0.0);
    FragColor.a = monitorColor.a;
    if(monitorUV.x > LED_UV_X0 && monitorUV.x < LED_UV_X1 && monitorUV.y > LED_UV_Y0 && monitorUV.y < LED_UV_Y1) {
        if(globals.ibmu_PowerOn == 1) {
            vec2 ledUV = vec2(ScaleN(monitorUV.x, LED_UV_X0, LED_UV_X1), ScaleN(monitorUV.y, LED_UV_Y0, LED_UV_Y1));
            vec4 ledColor = texture(LEDBloom, ledUV);
            theLED  = (vec3(0.3,0.6,0.0) + vec3(0.1,0.4,0.0) * (1.0 - ambient1)) * (1.0 - monitorColor.a);
            theLED += ToLinear(ledColor.rgb) * ledColor.a * (1.0 - ambient1);
        } else {
            theLED = (vec3(0.02,0.05,0.0) * ambient1) * (1.0 - monitorColor.a);
        }
        FragColor.a = 1.0;
    }
    vec3 colorsNotTheCRT = theMonitor + theReflection + theLED;

    // boost faint reflections in complete darkness
    // (constants determined by fidgeting with wolfram alpha)
    float lowLightAdjust = pow(1.0/(ambient1+0.02), 2.0) * 0.001;
    colorsNotTheCRT *= 1.0 + lowLightAdjust;

    // === Final color with alpha blending
    vec4 crtColor = texture(CRT, vTexCoord);
    crtColor.rgb = ToLinear(crtColor.rgb) * crtColor.a * (1.0 - monitorColor.a);
    FragColor.rgb = ToSrgb(crtColor.rgb + colorsNotTheCRT);
    if(d_outer < 0.0) {
        FragColor.a = 1.0;
    }
}
