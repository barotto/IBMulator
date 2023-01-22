/*
 * Copyright (C) 2015-2023  Marco Bortolin
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

#ifndef VECTOR_H
#define VECTOR_H

#include <cstddef>

template <class T>
class vec2 {
public:

	T x;
	T y;

public:

	vec2();
	vec2(T _xy);
	vec2(T _x, T _y);

	inline void operator = (T _val);
	inline operator const T * () const;
	inline operator T * ();
	inline T dot(const vec2<T>& _v) const;
	inline bool operator == (const vec2<T> &_v);
	inline vec2<T> operator - (const vec2<T>& _v) const;
	inline vec2<T> operator + (const vec2<T>& _v) const;
	inline vec2<T> operator * (T _val) const;
	inline vec2<T> operator / (T _val) const;
	inline vec2<T> operator / (const vec2<T>& _v) const;
	inline vec2<T>& operator += (const vec2<T> &_v);
	inline vec2<T>& operator -= (const vec2<T> &_v);
	inline void sum(const vec2<T>& _v);
	inline void diff(const vec2<T>& _v);
	inline void normalize();
	inline T length();
	inline T length2();
	inline T dist(vec2<T> &_v);
	inline T dist2(vec2<T> &_v);
	inline void rotate(T _angle);

	template<class C2>
	inline void operator= (const vec2<C2> &_v2)
	{
		x = (T)(_v2.x);
		y = (T)(_v2.y);
	}
};


typedef vec2<char> vec2b;
typedef vec2<int> vec2i;
typedef vec2<float> vec2f;
typedef vec2<double> vec2d;

static_assert(offsetof(vec2<char>,y) == offsetof(vec2<char>,x)+sizeof(char));
static_assert(offsetof(vec2<int>,y) == offsetof(vec2<int>,x)+sizeof(int));
static_assert(offsetof(vec2<float>,y) == offsetof(vec2<float>,x)+sizeof(float));
static_assert(offsetof(vec2<double>,y) == offsetof(vec2<double>,x)+sizeof(double));


//------------------------------------------------------------------------------


template <class T> class mat3;
template <class T> class mat4;

/**
vec3
*/
template <class T>
class vec3
{
public:
	T x;
	T y;
	T z;

	vec3();
	vec3(T _x, T _y, T _z);
	vec3(T _val);
	vec3(const T *_vec);

	template <typename R>
	explicit vec3(const vec3<R> &_vec);

	vec3(const vec3<T> &_v1, const vec3<T> &_v2);
	inline void operator = (const T *_vec);
	inline operator const T * () const;
	inline operator T * ();
	inline T& X() const;
	inline T& Y() const;
	inline T& Z() const;
	inline void set(T _x, T _y, T _z);
	inline T dot(const vec3<T>& _v) const;
	inline vec3<T> cross(const vec3<T>& _v) const;
	inline vec3<T> normal(const vec3<T>& _v) const;
	inline vec3<T> operator - () const;
	inline vec3<T> operator - (const vec3<T> &_v) const;
	inline vec3<T> operator + (const vec3<T> &_v) const;
	inline vec3<T> operator * (T _s) const;
	inline vec3<T> operator * (const vec3<T> &_v) const;
	inline vec3<T>& operator *= (T _s);
	inline vec3<T> operator / (const T _s) const;
	inline vec3<T> operator / (const vec3<T>& _v) const;
	inline bool operator == (const vec3<T> &_v) const;
	inline bool operator == (T _v) const;
	inline vec3<T>& operator += (const vec3<T> &_v);
	inline vec3<T>& operator -= (const vec3<T> &_v);
	inline vec3<T>& operator /= (const T _s); //< scalar division
	inline vec3<T> sum(const vec3<T>& _v) const;
	inline vec3<T>& diff(const vec3<T>& _v);
	inline vec3<T>& normalize();
	inline vec3<T> normalized() const;
	void orthonormalize(vec3<T> &_v, vec3<T> &_w) const;
	inline T length() const;
	inline T length2() const;
	inline T dist(const vec3<T> &_v) const;
	inline T dist2(const vec3<T> &_v) const;
	inline vec3<T>& rotate(T _rad, const vec3<T> & _axis);
	inline vec3<T>& rotate(const mat3<T> & _rot);
	inline vec3<T>& rotate(const mat4<T> & _rot);
	inline vec3<T> rotated_by(T _rad, const vec3<T> & _axis) const;
	inline vec3<T> rotated_by(const mat3<T> & _rot) const;
	inline vec3<T> rotated_by(const mat4<T> & _rot) const;
	inline vec3<T>& invert();
	inline vec2<T> xy() const;
	inline vec2<T> xz() const;
	inline vec2<T> yz() const;
	inline vec3<T> xy3(T _z = T(0)) const;
	inline vec3<T> xz3(T _y = T(0)) const;
	inline vec3<T> yz3(T _x = T(0)) const;
	inline bool is_nan() const;
	void copy_from(const T* _data);

	static const vec3 ZERO;
	static const vec3 UNIT;
	static const vec3 UNIT_X;
	static const vec3 UNIT_Y;
	static const vec3 UNIT_Z;


	//--------------------------------------------------------------------------
	// friends & statics

	static inline vec3<T> normal(const vec3<T>& _v0, const vec3<T>& _v1, const vec3<T>& _v2)
	{
		//[v0 - v1] × [v1 - v2]
		return (_v0 - _v1).cross(_v1 - _v2);
	}
	static inline vec3<T> normal(const vec3<T>& _v0, const vec3<T>& _v1)
	{
		//v0 × v1
		return _v0.cross(_v1);
	}
	inline friend vec3<T> operator * (T _s, const vec3<T> &_v)
	{
		return vec3<T> ( _v.x*_s, _v.y*_s, _v.z*_s );
	}
	inline friend vec3<T> operator / (T _s, const vec3<T> &_v)
	{
		return vec3<T> ( _s/_v.x, _s/_v.y, _s/_v.z );
	}
};

typedef vec3<char> vec3b;
typedef vec3<int> vec3i;
typedef vec3<float> vec3f;
typedef vec3<double> vec3d;

static_assert(offsetof(vec3<char>,y) == offsetof(vec3<char>,x)+sizeof(char));
static_assert(offsetof(vec3<char>,z) == offsetof(vec3<char>,y)+sizeof(char));
static_assert(offsetof(vec3<int>,y) == offsetof(vec3<int>,x)+sizeof(int));
static_assert(offsetof(vec3<int>,z) == offsetof(vec3<int>,y)+sizeof(int));
static_assert(offsetof(vec3<float>,y) == offsetof(vec3<float>,x)+sizeof(float));
static_assert(offsetof(vec3<float>,z) == offsetof(vec3<float>,y)+sizeof(float));
static_assert(offsetof(vec3<double>,y) == offsetof(vec3<double>,x)+sizeof(double));
static_assert(offsetof(vec3<double>,z) == offsetof(vec3<double>,y)+sizeof(double));


//-----------------------------------------------------------------------------


/**
vec4
*/
template <class T>
class vec4
{
public:
	T x;
	T y;
	T z;
	T w;

	vec4();
	vec4(T _x, T _y, T _z, T _w);
	vec4(T _val);
	vec4(const vec3<T>& _v, T _w = (T)1);
	vec4(const T *_vec);
	inline operator T * ();
	inline operator const T * () const;
	inline vec4<T> operator - () const;
	inline vec4<T> operator + (const vec4<T> &_v) const;
	inline vec4<T> operator - (const vec4<T> &_v) const;
	inline void operator = (const vec3<T> &_v);
	inline vec3<T> xyz();
	inline vec2<T> xy();
	vec4<T>& normalize();
	void copy_from(const T* _data);

	static const vec4 ZERO;
	static const vec4 UNIT;
	static const vec4 UNIT_X;
	static const vec4 UNIT_Y;
	static const vec4 UNIT_Z;
	static const vec4 UNIT_W;
};

typedef vec4<int> vec4i;
typedef vec4<float> vec4f;
typedef vec4<double> vec4d;

static_assert(offsetof(vec4<int>,y) == offsetof(vec4<int>,x)+sizeof(int));
static_assert(offsetof(vec4<int>,z) == offsetof(vec4<int>,y)+sizeof(int));
static_assert(offsetof(vec4<int>,w) == offsetof(vec4<int>,z)+sizeof(int));
static_assert(offsetof(vec4<float>,y) == offsetof(vec4<float>,x)+sizeof(float));
static_assert(offsetof(vec4<float>,z) == offsetof(vec4<float>,y)+sizeof(float));
static_assert(offsetof(vec4<float>,w) == offsetof(vec4<float>,z)+sizeof(float));
static_assert(offsetof(vec4<double>,y) == offsetof(vec4<double>,x)+sizeof(double));
static_assert(offsetof(vec4<double>,z) == offsetof(vec4<double>,y)+sizeof(double));
static_assert(offsetof(vec4<double>,w) == offsetof(vec4<double>,z)+sizeof(double));

//------------------------------------------------------------------------------

#include "vector.inl.h"



#endif
