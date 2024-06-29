#version 330 core


#pragma stage vertex

// Input vertex data
layout(location = 0) in vec2 Position;
layout(location = 1) in ivec4 Color;
layout(location = 2) in vec2 TexCoord;

out vec2 vTexCoord;
out vec4 vColor;

uniform mat4 ibmu_ModelView;
uniform mat4 ibmu_Projection;

void main()
{
	vec4 eye = ibmu_ModelView * vec4(Position.x,Position.y,0.0,1.0);
	gl_Position = ibmu_Projection * eye;
	vTexCoord = TexCoord;
	vColor = vec4(Color.r/255.0, Color.g/255.0, Color.b/255.0, Color.a/255.0);
}


#pragma stage fragment

in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D Source;
uniform int uMultAlpha;

out vec4 FragColor;

void main()
{
	vec4 texcol = texture( Source, vTexCoord );
	if(uMultAlpha > 0) {
		texcol.rgb = texcol.rgb * texcol.a;
	}
	FragColor = vec4(vColor.rgb * texcol.rgb, vColor.a * texcol.a);
}