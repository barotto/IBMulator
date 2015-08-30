#version 330 core

/*
	Bicubic filtering.

	The texture sampler must have bilinear filtering enabled (GL_LINEAR). 

	This shader relies on the fact that bilinear filtering gives us a weighted 
	average of texels in a 2x2 area. By sampling 4 bilinear samples with 
	carefully modified offsets and how we combine these 4 samples, we can get 
	the correct weights for all 16 texels we need. The result is an extremely 
	fast bicubic filtering function that only requires 4 texture samples.
	
	This shader uses texture() instead of texelFetch() so it works with texture 
	coordinate wrapping and clamping.
	
	http://http.developer.nvidia.com/GPUGems2/gpugems2_chapter20.html
	http://vec3.ca/bicubic-filtering-in-fewer-taps/
	
	Credit to:
	http://www.java-gaming.org/index.php?topic=35123.0
*/

vec4 cubic(float v)
{
	vec4 n = vec4(1.0, 2.0, 3.0, 4.0) - v;
	vec4 s = n * n * n;
	float x = s.x;
	float y = s.y - 4.0 * s.x;
	float z = s.z - 4.0 * s.y + 6.0 * s.x;
	float w = 6.0 - x - y - z;
	return vec4(x, y, z, w) * (1.0/6.0);
}

vec4 FetchTexel(sampler2D sampler, vec2 texCoords)
{
	vec2 texSize = textureSize(sampler, 0);
	vec2 invTexSize = 1.0 / texSize;

	texCoords = texCoords * texSize - 0.5;

	vec2 fxy = fract(texCoords);
	texCoords -= fxy;

	vec4 xcubic = cubic(fxy.x);
	vec4 ycubic = cubic(fxy.y);
	
	vec4 c = texCoords.xxyy + vec2(-0.5, +1.5).xyxy;
	
	vec4 s = vec4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
	vec4 offset = c + vec4(xcubic.yw, ycubic.yw) / s;
	
	offset *= invTexSize.xxyy;
	
	vec4 sample0 = texture(sampler, offset.xz);
	vec4 sample1 = texture(sampler, offset.yz);
	vec4 sample2 = texture(sampler, offset.xw);
	vec4 sample3 = texture(sampler, offset.yw);
	
	float sx = s.x / (s.x + s.y);
	float sy = s.z / (s.z + s.w);

	return mix(mix(sample3, sample2, sx), mix(sample1, sample0, sx), sy);
}