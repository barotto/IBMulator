#version 330 core

#include "filter_bicubic.glsl"

vec4 FetchTexel(sampler2D sampler, vec2 texCoords)
{
	return FetchTexelBicubic(sampler, texCoords);
}