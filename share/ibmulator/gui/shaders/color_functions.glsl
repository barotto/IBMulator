#version 330 core


vec3 BrightnessSaturationContrast(vec3 color, float brightness, float saturation, float contrast)
{
	const vec3 LumCoeff = vec3(0.2125, 0.7154, 0.0721);

	/* On CRTs the control labeled Contrast is actually gain or the slope of 
	 * the output curve with increasing input voltage, and the control labeled 
	 * Brightness is actually offset or the overall level of all the points on 
	 * the curve, with the whole curve shifted up or down with increasing or 
	 * decreasing control adjustements.
	 */
	color += (brightness-1.0);
	vec3 intensity = vec3(dot(color, LumCoeff));
	color = mix(intensity, color, saturation);
	color *= contrast;
	return color;
}

