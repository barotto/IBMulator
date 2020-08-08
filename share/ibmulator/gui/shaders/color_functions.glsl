#version 330 core

// sRGB to Linear.
// Assuming using sRGB typed textures this should not be needed.
float ToLinear1(float c) 
{
	if(c<=0.04045) {
		return c/12.92;
	} else {
		return pow((c+0.055)/1.055, 2.4);
	}
}

vec3 ToLinear(vec3 c) 
{
	return vec3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b));
}

// Linear to sRGB.
// Assuming using sRGB typed textures this should not be needed.
float ToSrgb1(float c)
{
	if(c<0.0031308) { 
		return c*12.92;
	} else { 
		return 1.055*pow(c,0.41666) - 0.055;
	}
}

vec3 ToSrgb(vec3 c)
{
	return vec3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
}

vec3 BrightnessSaturationContrast(vec4 color, float brightness, vec3 brightness_color, 
	float saturation, float contrast)
{
	const vec3 lumaCoeff = vec3(0.299,0.587,0.114); //vec3(0.2125, 0.7154, 0.0721);

	/* On CRTs the control labeled Contrast is actually gain or the slope of 
	 * the output curve with increasing input voltage, and the control labeled 
	 * Brightness is actually offset or the overall level of all the points on 
	 * the curve, with the whole curve shifted up or down with increasing or 
	 * decreasing control adjustements.
	 */
	brightness -= 1.0;
	if(brightness < 0.0) {
		brightness_color = vec3(1.0);
	}
	brightness_color = mix(vec3(0.0), brightness_color, color.a);
	color.rgb += brightness * brightness_color;
	vec3 intensity = vec3(dot(color.rgb, lumaCoeff));
	color.rgb = mix(intensity, color.rgb, saturation);
	color.rgb *= contrast;
	return max(vec3(0.0), color.rgb);
}

