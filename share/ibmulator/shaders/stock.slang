#version 330 core

layout(std140) uniform Globals
{
	mat4 MVP;
} global;


#pragma stage vertex

layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
out vec2 vTexCoord;

void main()
{
   gl_Position = global.MVP * Position;
   vTexCoord = TexCoord;
}


#pragma stage fragment

#ifdef BICUBIC_FILTER
#include "filter_bicubic.glsl"
#define FetchTexel FetchTexelBicubic
#else
#define FetchTexel texture
#endif

in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D Original;

void main()
{
    FragColor = vec4(FetchTexel(Original, vTexCoord).rgb, 1.0);
}