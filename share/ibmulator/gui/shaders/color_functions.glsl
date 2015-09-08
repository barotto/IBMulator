#version 330 core


vec3 BrightnessSaturationContrast(vec3 color, float brightness, vec3 brighness_color, 
	float saturation, float contrast)
{
	const vec3 lumaCoeff = vec3(0.299,0.587,0.114); //vec3(0.2125, 0.7154, 0.0721);

	/* On CRTs the control labeled Contrast is actually gain or the slope of 
	 * the output curve with increasing input voltage, and the control labeled 
	 * Brightness is actually offset or the overall level of all the points on 
	 * the curve, with the whole curve shifted up or down with increasing or 
	 * decreasing control adjustements.
	 */
	float brt = brightness-1.0;
	if(brt<0.0) {
		brighness_color = vec3(1.0);
	}
	color += (brightness-1.0)*brighness_color;
	vec3 intensity = vec3(dot(color, lumaCoeff));
	color = mix(intensity, color, saturation);
	color *= contrast;
	return max(vec3(0.0), color);
}

