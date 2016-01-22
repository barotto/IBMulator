/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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

#include "vector.h"


template<> const vec3<char> vec3<char>::ZERO(0,0,0);
template<> const vec3<char> vec3<char>::UNIT(1,1,1);
template<> const vec3<char> vec3<char>::UNIT_X(1,0,0);
template<> const vec3<char> vec3<char>::UNIT_Y(0,1,0);
template<> const vec3<char> vec3<char>::UNIT_Z(0,0,1);

template<> const vec3<int> vec3<int>::ZERO(0,0,0);
template<> const vec3<int> vec3<int>::UNIT(1,1,1);
template<> const vec3<int> vec3<int>::UNIT_X(1,0,0);
template<> const vec3<int> vec3<int>::UNIT_Y(0,1,0);
template<> const vec3<int> vec3<int>::UNIT_Z(0,0,1);

template<> const vec3<float> vec3<float>::ZERO(0.0f,0.0f,0.0f);
template<> const vec3<float> vec3<float>::UNIT(1.0f,1.0f,1.0f);
template<> const vec3<float> vec3<float>::UNIT_X(1.0f,0.0f,0.0f);
template<> const vec3<float> vec3<float>::UNIT_Y(0.0f,1.0f,0.0f);
template<> const vec3<float> vec3<float>::UNIT_Z(0.0f,0.0f,1.0f);

template<> const vec3<double> vec3<double>::ZERO(0.0,0.0,0.0);
template<> const vec3<double> vec3<double>::UNIT(1.0,1.0,1.0);
template<> const vec3<double> vec3<double>::UNIT_X(1.0,0.0,0.0);
template<> const vec3<double> vec3<double>::UNIT_Y(0.0,1.0,0.0);
template<> const vec3<double> vec3<double>::UNIT_Z(0.0,0.0,1.0);


template<> const vec4<int> vec4<int>::ZERO(0,0,0,0);
template<> const vec4<int> vec4<int>::UNIT(1,1,1,1);
template<> const vec4<int> vec4<int>::UNIT_X(1,0,0,0);
template<> const vec4<int> vec4<int>::UNIT_Y(0,1,0,0);
template<> const vec4<int> vec4<int>::UNIT_Z(0,0,1,0);
template<> const vec4<int> vec4<int>::UNIT_W(0,0,0,1);

template<> const vec4<float> vec4<float>::ZERO(0.0f,0.0f,0.0f,0.0f);
template<> const vec4<float> vec4<float>::UNIT(1.0f,1.0f,1.0f,1.0f);
template<> const vec4<float> vec4<float>::UNIT_X(1.0f,0.0f,0.0f,0.0f);
template<> const vec4<float> vec4<float>::UNIT_Y(0.0f,1.0f,0.0f,0.0f);
template<> const vec4<float> vec4<float>::UNIT_Z(0.0f,0.0f,1.0f,0.0f);
template<> const vec4<float> vec4<float>::UNIT_W(0.0f,0.0f,0.0f,1.0f);

template<> const vec4<double> vec4<double>::ZERO(0.0,0.0,0.0,0.0);
template<> const vec4<double> vec4<double>::UNIT(1.0,1.0,1.0,1.0);
template<> const vec4<double> vec4<double>::UNIT_X(1.0,0.0,0.0,0.0);
template<> const vec4<double> vec4<double>::UNIT_Y(0.0,1.0,0.0,0.0);
template<> const vec4<double> vec4<double>::UNIT_Z(0.0,0.0,1.0,0.0);
template<> const vec4<double> vec4<double>::UNIT_W(0.0,0.0,0.0,1.0);


