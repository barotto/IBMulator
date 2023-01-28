vec2 ScaleUV(vec2 uv, float scale, vec2 ratio) 
{
    uv = (uv - 0.5) * 2.0; // move to [-1,1]
    uv /= vec2(globals.OutputSize.y/globals.OutputSize.x, 1.0); // make it square
    uv /= scale; // apply scale
    uv /= ratio; // apply aspect ratio
    uv = (uv / 2.0) + 0.5; // move back to [0,1]
	return uv;
}

vec2 CurvedSurface(vec2 uv, float r)
{
    return r * uv/sqrt(r * r - dot(uv, uv));
}

vec2 VGACurv(vec2 uv, float scale, float curvature) 
{
    uv = (uv - 0.5) * 2.0; // move to [-1,1]
    uv /= vec2(globals.OutputSize.y/globals.OutputSize.x, 1.0); // make it square
    uv /= scale; // scale
    uv = CurvedSurface(uv, curvature); // apply curvature
    uv /= vec2(4.0/3.0, 1.0); // apply aspect ratio
    uv = (uv / 2.0) + 0.5; // move back to [0,1]
	return uv;
}

float RoundBox(vec2 pos, vec2 size, float radius, float dist) 
{
    pos = (pos - 0.5) * 2.0; // move to [-1,1]
    pos /= vec2(globals.OutputSize.y/globals.OutputSize.x, 1.0); // make it square
    vec2 q = abs(pos) - size + radius;
    return min(max(q.x,q.y),0.0) + length(max(q,0.0)) - radius + dist;
}

int MaxLOD(vec4 size)
{
    return int(floor(log2(max(size.x, size.y))));
}

float Rand(vec2 co)
{
    return fract(sin(dot(co, vec2(12.9898,78.233))) * 43758.5453);
}

float ScaleN(float n, float l, float h)
{
	return (n - l) / (h - l);
}

float ScaleN(float n, float l0, float h0, float l1, float h1)
{
	return (n - l0) * (h1 - l1) / (h0 - l0) + l1;
}

vec3 Ambient()
{
    // take the global ambient value
    vec3 ambient = ToLinear(vec3(globals.ibmu_Ambient, globals.ibmu_Ambient, globals.ibmu_Ambient));

#if VGA_REFLECTION
    // and add the contribution from the light that comes from the CRT
    //  (Reflection texture is already in linear space)
    vec3 crtAmbient = texelFetch(Reflection, ivec2(0,0), MaxLOD(ReflectionSize)).rgb;
    ambient += Saturation(crtAmbient, 0.3) // reduce saturation 
        * mix(0.0, params.ambientFromCRT, 1.0 - globals.ibmu_Ambient*1.2); // as global ambient increases, ambient from crt lowers
#endif

    // but don't go over 1.0
    ambient = vec3(
        min(ambient.r, 1.0),
        min(ambient.g, 1.0),
        min(ambient.b, 1.0)
    );
    return ambient;
}
