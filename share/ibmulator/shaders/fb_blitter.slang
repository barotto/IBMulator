#version 330 core

/*
 * Used for blitting the output of the final render pass in the case the pass won't render directly to the backbuffer.
 */

#pragma stage vertex

uniform mat4 MVP;
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
out vec2 vTexCoord;

void main()
{
   gl_Position = MVP * Position;
   // Framebuffers render to textures starting from the last line
   //vTexCoord = vec2(TexCoord.x, 1.0-TexCoord.y);
   vTexCoord = TexCoord;
}


#pragma stage fragment

// the correct filtering method will be determined during linking
vec4 FetchTexel(sampler2D sampler, vec2 texCoords);

uniform sampler2D Source;
in vec2 vTexCoord;

out vec4 FragColor;

void main()
{
   FragColor = FetchTexel(Source, vTexCoord);
}
