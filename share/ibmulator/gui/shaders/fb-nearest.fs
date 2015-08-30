#version 330 core

in vec2 UV;
out vec4 oColor;

uniform sampler2D iChannel0;
uniform ivec2 iDisplaySize;
uniform float iBrightness; // range [0.0,1.0]
uniform float iContrast;   // range [0.0,1.0]

#define BRIGHTNESS_CENTER 0.7
#define CONTRAST_MIN 0.5
#define CONTRAST_MAX 1.5

vec4 FetchTexel(sampler2D sampler, vec2 texCoords);

void main()
{
	vec2 uv = UV;
	uv.t = 1.0-uv.t;
	oColor = FetchTexel(iChannel0, uv);
	oColor.a = 1.0;
	float contrast = (CONTRAST_MAX - CONTRAST_MIN)*iContrast + CONTRAST_MIN;
	oColor.rgb = (oColor.rgb - 0.5) * contrast + 0.5;
	oColor.rgb += iBrightness - BRIGHTNESS_CENTER;
}