/*
 * Copyright (C) 2022  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "opengl.h"

const char * GL_get_error_string(GLenum _error_code)
{
	switch(_error_code) {
		case GL_INVALID_ENUM:
			return "GL_INVALID_ENUM: An unacceptable value is specified for an enumerated argument.";
		case GL_INVALID_VALUE:
			return "GL_INVALID_VALUE: A numeric argument is out of range.";
		case GL_INVALID_OPERATION:
			return "GL_INVALID_OPERATION: The specified operation is not allowed in the current state.";
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			return "GL_INVALID_FRAMEBUFFER_OPERATION: The framebuffer object is not complete.";
		case GL_OUT_OF_MEMORY:
			return "GL_OUT_OF_MEMORY: There is not enough memory left to execute the command.";
		case GL_STACK_UNDERFLOW:
			return "GL_STACK_UNDERFLOW: An attempt has been made to perform an operation that would cause an internal stack to underflow.";
		case GL_STACK_OVERFLOW:
			return "GL_STACK_OVERFLOW: An attempt has been made to perform an operation that would cause an internal stack to overflow.";
		default:
			break;
	}
	return "unknown error";
}

const char * GL_get_uniform_type_string(GLenum _uniform_type)
{
	switch(_uniform_type) {
	case GL_FLOAT: return "float";
	case GL_FLOAT_VEC2: return "vec2";
	case GL_FLOAT_VEC3: return "vec3";
	case GL_FLOAT_VEC4: return "vec4";
	case GL_DOUBLE: return "double";
	case GL_DOUBLE_VEC2: return "dvec2";
	case GL_DOUBLE_VEC3: return "dvec3";
	case GL_DOUBLE_VEC4: return "dvec4";
	case GL_INT: return "int";
	case GL_INT_VEC2: return "ivec2";
	case GL_INT_VEC3: return "ivec3";
	case GL_INT_VEC4: return "ivec4";
	case GL_UNSIGNED_INT: return "unsigned int";
	case GL_UNSIGNED_INT_VEC2: return "uvec2";
	case GL_UNSIGNED_INT_VEC3: return "uvec3";
	case GL_UNSIGNED_INT_VEC4: return "uvec4";
	case GL_BOOL: return "bool";
	case GL_BOOL_VEC2: return "bvec2";
	case GL_BOOL_VEC3: return "bvec3";
	case GL_BOOL_VEC4: return "bvec4";
	case GL_FLOAT_MAT2: return "mat2";
	case GL_FLOAT_MAT3: return "mat3";
	case GL_FLOAT_MAT4: return "mat4";
	case GL_FLOAT_MAT2x3: return "mat2x3";
	case GL_FLOAT_MAT2x4: return "mat2x4";
	case GL_FLOAT_MAT3x2: return "mat3x2";
	case GL_FLOAT_MAT3x4: return "mat3x4";
	case GL_FLOAT_MAT4x2: return "mat4x2";
	case GL_FLOAT_MAT4x3: return "mat4x3";
	case GL_DOUBLE_MAT2: return "dmat2";
	case GL_DOUBLE_MAT3: return "dmat3";
	case GL_DOUBLE_MAT4: return "dmat4";
	case GL_DOUBLE_MAT2x3: return "dmat2x3";
	case GL_DOUBLE_MAT2x4: return "dmat2x4";
	case GL_DOUBLE_MAT3x2: return "dmat3x2";
	case GL_DOUBLE_MAT3x4: return "dmat3x4";
	case GL_DOUBLE_MAT4x2: return "dmat4x2";
	case GL_DOUBLE_MAT4x3: return "dmat4x3";
	case GL_SAMPLER_1D: return "sampler1D";
	case GL_SAMPLER_2D: return "sampler2D";
	case GL_SAMPLER_3D: return "sampler3D";
	case GL_SAMPLER_CUBE: return "samplerCube";
	case GL_SAMPLER_1D_SHADOW: return "sampler1DShadow";
	case GL_SAMPLER_2D_SHADOW: return "sampler2DShadow";
	case GL_SAMPLER_1D_ARRAY: return "sampler1DArray";
	case GL_SAMPLER_2D_ARRAY: return "sampler2DArray";
	case GL_SAMPLER_1D_ARRAY_SHADOW: return "sampler1DArrayShadow";
	case GL_SAMPLER_2D_ARRAY_SHADOW: return "sampler2DArrayShadow";
	case GL_SAMPLER_2D_MULTISAMPLE: return "sampler2DMS";
	case GL_SAMPLER_2D_MULTISAMPLE_ARRAY: return "sampler2DMSArray";
	case GL_SAMPLER_CUBE_SHADOW: return "samplerCubeShadow";
	case GL_SAMPLER_BUFFER: return "samplerBuffer";
	case GL_SAMPLER_2D_RECT: return "sampler2DRect";
	case GL_SAMPLER_2D_RECT_SHADOW: return "sampler2DRectShadow";
	case GL_INT_SAMPLER_1D: return "isampler1D";
	case GL_INT_SAMPLER_2D: return "isampler2D";
	case GL_INT_SAMPLER_3D: return "isampler3D";
	case GL_INT_SAMPLER_CUBE: return "isamplerCube";
	case GL_INT_SAMPLER_1D_ARRAY: return "isampler1DArray";
	case GL_INT_SAMPLER_2D_ARRAY: return "isampler2DArray";
	case GL_INT_SAMPLER_2D_MULTISAMPLE: return "isampler2DMS";
	case GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY: return "isampler2DMSArray";
	case GL_INT_SAMPLER_BUFFER: return "isamplerBuffer";
	case GL_INT_SAMPLER_2D_RECT: return "isampler2DRect";
	case GL_UNSIGNED_INT_SAMPLER_1D: return "usampler1D";
	case GL_UNSIGNED_INT_SAMPLER_2D: return "usampler2D";
	case GL_UNSIGNED_INT_SAMPLER_3D: return "usampler3D";
	case GL_UNSIGNED_INT_SAMPLER_CUBE: return "usamplerCube";
	case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY: return "usampler2DArray";
	case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY: return "usampler2DArray";
	case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE: return "usampler2DMS";
	case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY: return "usampler2DMSArray";
	case GL_UNSIGNED_INT_SAMPLER_BUFFER: return "usamplerBuffer";
	case GL_UNSIGNED_INT_SAMPLER_2D_RECT: return "usampler2DRect";
	default: return "???";
	};

}