#version 330 core

in vec2 UV;
out vec3 oColor;

uniform sampler2D iChannel0;
uniform ivec2 iCh0Size;
uniform ivec2 iResolution;

void main()
{
	vec2 uv = UV;
	uv.s *= float(iResolution.x)/float(iCh0Size.x);
	uv.t = 1.0-uv.t;
	uv.t *= float(iResolution.y)/float(iCh0Size.y);
	oColor = texture( iChannel0, uv ).xyz;
}