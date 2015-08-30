#version 330 core

vec4 FetchTexel(sampler2D sampler, vec2 texCoords)
{
	return texture(sampler, texCoords);
}