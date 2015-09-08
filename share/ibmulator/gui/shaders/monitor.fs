#version 330 core

in vec2 UV;
out vec4 oColor;

uniform sampler2D iReflectionMap;

void main()
{
	//if(iSpecular) {

		oColor = texture(iReflectionMap, UV);
	//} else {
		//oColor = vec4(0.0,0.0,0.0, 1.0);
	//}
}