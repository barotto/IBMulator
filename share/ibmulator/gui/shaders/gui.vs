#version 330 core

// Input vertex data
layout(location = 0) in vec2 _vertex;
layout(location = 1) in ivec4 _color;
layout(location = 2) in vec2 _uv;

uniform mat4 MV;
uniform mat4 P;

centroid out vec2 UV_;
centroid out vec4 color_;

void main()
{
	vec4 eye = MV * vec4(_vertex.x,_vertex.y,0.0,1.0);
    gl_Position = P * eye;
	UV_ = _uv;
	color_ = vec4(_color.r/255.0, _color.g/255.0, _color.b/255.0, _color.a/255.0);
}