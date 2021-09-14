#version 330 core

centroid in vec2 UV_;
centroid in vec4 color_;
out vec4 glColor;

uniform bool textured;
uniform sampler2D guitex;


void main()
{
	if(textured) {
		vec4 texcol = texture( guitex, UV_ );
		glColor = vec4(color_.xyz*texcol.xyz, color_.a*texcol.a);
	} else {
		glColor = color_;
	}
}