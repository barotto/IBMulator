/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include <cmath>

template<class T>
vec2<T>::vec2()
:
x((T)0.0),
y((T)0.0)
{
}


template<class T>
vec2<T>::vec2(T _x, T _y)
:
x(_x),
y(_y)
{
}


/** Assegna ad x e y il valore dello scalare in ingresso.
@param[in] _val valore scalare.
*/
template<class T>
inline void vec2<T>::operator = (T _val)
{
	x = _val;
	y = _val;
}


template<class T>
inline vec2<T>::operator const T * () const
{
	return &x;
}


template<class T>
inline vec2<T>::operator T * ()
{
	return &x;
}


/** Calcola il prodotto scalare con il vettore in ingresso.
@param[in] _v vettore, secondo operando.
@return prodotto scalare tra questo vec2 e _v.
*/
template<class T>
inline T vec2<T>::dot(const vec2<T>& _v) const
{
	return x*_v.x + y*_v.y;
}


/** Confronto.
@param[in] _v vettore con cui controntare i valori.
@return true se i vettori hanno gli stessi valori.
*/
template<class T>
inline bool vec2<T>::operator == (const vec2<T> &_v)
{
	return (x == _v.x && y == _v.y);
}


/** Differenza.
@param[in] _v secondo operando della sottrazione.
@return vettore risultato della sottrazione tra this e _v.
*/
template<class T>
inline vec2<T> vec2<T>::operator - (const vec2<T>& _v) const
{
	return vec2<T>(x-_v.x, y-_v.y);
}


/** Somma.
@param[in] _v secondo operando dell'addizione.
@return vettore risultato dell'addizione tra this e _v.
*/
template<class T>
inline vec2<T> vec2<T>::operator + (const vec2<T>& _v) const
{
	return vec2<T>(x+_v.x, y+_v.y);
}


/** Proddotto con uno scalare.
@param[in] _val scalare.
@return vettore risultato del prodotto tra this e _val.
*/
template<class T>
inline vec2<T> vec2<T>::operator * (T _val) const
{
	return vec2<T>(x*_val, y*_val);
}


/** Divisione con uno scalare.
@param[in] _val scalare.
@return vettore risultato della divisione tra this e _val.
*/
template<class T>
inline vec2<T> vec2<T>::operator / (T _val) const
{
	return vec2<T>(x/_val, y/_val);
}


/** Somma.
@param[in] _v secondo operando della somma.
@return *this
*/
template<class T>
inline vec2<T>& vec2<T>::operator += (const vec2<T> &_v)
{
	x += _v.x;
	y += _v.y;
	return *this;
}


/** Differenza.
@param[in] _v secondo operando della sottrazione.
@return *this
*/
template<class T>
inline vec2<T>& vec2<T>::operator -= (const vec2<T> &_v)
{
	x -= _v.x;
	y -= _v.y;
	return *this;
}


/** Somma.
@param[in] _v vettore da sommare a questo oggetto.
*/
template<class T>
inline void vec2<T>::sum(const vec2<T>& _v)
{
	x += _v.x;
	y += _v.y;
}


/** Differenza.
@param[in] _v vettore da sottrarre a questo oggetto.
*/
template<class T>
inline void vec2<T>::diff(const vec2<T>& _v)
{
	x -= _v.x;
	y -= _v.y;
}


/** Normalizzazione.
Rende il vettore di lunghezza unitaria.
*/
template<class T>
inline void vec2<T>::normalize()
{
	T len = length();
	x = (T)(x/len);
	y = (T)(y/len);
}


template<class T>
inline T vec2<T>::length()
{
	return sqrt(x*x + y*y);
}


template<class T>
inline T vec2<T>::length2()
{
	return x*x + y*y;
}


template<class T>
inline T vec2<T>::dist(vec2<T> &_v)
{
	vec2<T> d(*this);
	d.diff(_v);
	return d.length();
}


template<class T>
inline T vec2<T>::dist2(vec2<T> &_v)
{
	vec2<T> d(*this);
	d.diff(_v);
	return d.length2();
}


template<class T>
inline void vec2<T>::rotate(T _angle)
{
	T c = cos(_angle);
	T s = sin(_angle);
	T x_ = x;
	x = x*c + y*s;
	y = y*c - x_*s;
}


//------------------------------------------------------------------------------


template<class T>
vec3<T>::vec3()
:
x((T)0.0),
y((T)0.0),
z((T)0.0)
{
}


template<class T>
vec3<T>::vec3(T _x, T _y, T _z)
:
x(_x),
y(_y),
z(_z)
{
}


template<class T>
vec3<T>::vec3(T _val)
:
x(_val),
y(_val),
z(_val)
{
}


template<class T>
vec3<T>::vec3(const T *_vec)
{
	x = _vec[0];
	y = _vec[1];
	z = _vec[2];
}

template <typename T>
template <typename R>
vec3<T>::vec3(const vec3<R> &_vec)
{
	x = (T)_vec.x;
	y = (T)_vec.y;
	z = (T)_vec.z;
}

template<class T>
vec3<T>::vec3(const vec3<T>& _v1, const vec3<T>& _v2)
{
	x = _v2.x - _v1.x;
	y = _v2.y - _v1.y;
	z = _v2.z - _v1.z;
}


template<class T>
inline void vec3<T>::operator = (const T *_vec)
{
	x = _vec[0];
	y = _vec[1];
	z = _vec[2];
}


template<class T>
inline vec3<T>::operator const T * () const
{
	return &x;
}


template<class T>
inline vec3<T>::operator T * ()
{
	return &x;
}


template<class T>
inline T& vec3<T>::X() const
{
	return x;
}


template<class T>
inline T& vec3<T>::Y() const
{
	return y;
}


template<class T>
inline T& vec3<T>::Z() const
{
	return z;
}


template<class T>
inline void vec3<T>::set(T _x, T _y, T _z)
{
	x = _x;
	y = _y;
	z = _z;
}


template<class T>
inline T vec3<T>::dot(const vec3<T>& _v) const
{
	return x*_v.x + y*_v.y + z*_v.z;
}


template<class T>
inline vec3<T> vec3<T>::cross(const vec3<T>& _v) const
{
	return vec3<T>(
		y*_v.z - z*_v.y, //x
		z*_v.x - x*_v.z, //y
		x*_v.y - y*_v.x  //z
		);
}


template<class T>
inline vec3<T> vec3<T>::normal(const vec3<T>& _v) const
{
	//v1 × v2
	return cross(_v);
}


template<class T>
inline vec3<T> vec3<T>::operator - () const
{
	return vec3<T>(-x,-y,-z);
}


template<class T>
inline vec3<T> vec3<T>::operator - (const vec3<T> &_v) const
{
	return vec3<T>(x-_v.x, y-_v.y, z-_v.z);
}


template<class T>
inline vec3<T> vec3<T>::operator + (const vec3<T> &_v) const
{
	return vec3<T>(x+_v.x, y+_v.y, z+_v.z);
}


template<class T>
inline vec3<T> vec3<T>::operator * (T _s) const
{
	return vec3<T> ( x*_s, y*_s, z*_s );
}


template<class T>
inline vec3<T> vec3<T>::operator * (const vec3<T> &_v) const
{
	return vec3<T>( x*_v.x, y*_v.y, z*_v.z );
}


template<class T>
inline vec3<T>& vec3<T>::operator *= (T _s)
{
	x *= _s; y *= _s; z *= _s;
	return *this;
}


template<class T>
inline vec3<T> vec3<T>::operator / (const T _s) const
{
	return vec3<T>(x/_s, y/_s, z/_s);
}


template<class T>
inline vec3<T> vec3<T>::operator / (const vec3<T>& _v) const
{
	return vec3<T>(x/_v.x, y/_v.y, z/_v.z);
}


template<class T>
inline bool vec3<T>::operator == (const vec3<T> &_v) const
{
	return (x == _v.x && y == _v.y && z == _v.z);
}


template<class T>
inline bool vec3<T>::operator == (T _v) const
{
	return (x == _v && y == _v && z == _v);
}


template<class T>
inline vec3<T>& vec3<T>::operator += (const vec3<T> &_v)
{
	x += _v.x;
	y += _v.y;
	z += _v.z;
	return *this;
}


template<class T>
inline vec3<T>& vec3<T>::operator -= (const vec3<T> &_v)
{
	x -= _v.x;
	y -= _v.y;
	z -= _v.z;
	return *this;
}


//scalar division
template<class T>
inline vec3<T>& vec3<T>::operator /= (const T _s)
{
	x /= _s;
	y /= _s;
	z /= _s;
	return *this;
}


template<class T>
inline vec3<T> vec3<T>::sum(const vec3<T>& _v) const
{
	return vec3<T>(x+_v.x, y+_v.y, z+_v.z);
}


template<class T>
inline vec3<T>& vec3<T>::diff(const vec3<T>& _v)
{
	x -= _v.x;
	y -= _v.y;
	z -= _v.z;

	return *this;
}


/**
You should check for NaN in case of division by zero.
*/
template<class T>
inline vec3<T>& vec3<T>::normalize()
{
	T invlen = T(1)/length();
	x *= invlen;
	y *= invlen;
	z *= invlen;

	return *this;
}


/**
You should check for NaN in case of division by zero.
*/
template<class T>
inline vec3<T> vec3<T>::normalized() const
{
	T invlen = T(1)/length();
	return vec3<T>(x*invlen, y*invlen, z*invlen);
}


template <class T>
void vec3<T>::orthonormalize(vec3<T> &_v, vec3 &_w) const
{
	// If the input vectors are v0, v1, and v2, then the Gram-Schmidt
	// orthonormalization produces vectors u0, u1, and u2 as follows,
	//
	//   u0 = v0/|v0|
	//   u1 = (v1-(u0*v1)u0)/|v1-(u0*v1)u0|
	//   u2 = (v2-(u0*v2)u0-(u1*v2)u1)/|v2-(u0*v2)u0-(u1*v2)u1|
	//
	// where |A| indicates length of vector A and A*B indicates dot
	// product of vectors A and B.
	//
	// u0 is this vector.
	// it is assumed to be already normalized:
	// this->normalize();

	// compute u1
	T dot0 = this->dot(_v);
	_v = _v - (*this)*dot0;
	_v.normalize();

	// compute u2
	T dot1 = _v.dot(_w);
	dot0 = this->dot(_w);
	_w -= (*this)*dot0 + _v*dot1;
	_w.normalize();
}


template<class T>
inline T vec3<T>::length2() const
{
	return x*x + y*y + z*z;
}


template<class T>
inline T vec3<T>::length() const
{
	return Sqrt(length2());
}


template<class T>
inline T vec3<T>::dist(const vec3<T> &_v) const
{
	vec3<T> d(*this);
	d.diff(_v);
	return d.length();
}


template<class T>
inline T vec3<T>::dist2(const vec3<T> &_v) const
{
	vec3<T> d(*this);
	d.diff(_v);
	return d.length2();
}


template<class T>
inline vec3<T>& vec3<T>::rotate(T _rad, const vec3<T> & _axis)
{
	mat3<T> matrix;
	matrix.load_rotation(_rad,_axis);
	rotate(matrix);

	return *this;
}


template<class T>
inline vec3<T>& vec3<T>::rotate(const mat3<T> & _rot)
{
	T nx = _rot[0] * x + _rot[3] * y + _rot[6] * z;
	T ny = _rot[1] * x + _rot[4] * y + _rot[7] * z;
	T nz = _rot[2] * x + _rot[5] * y + _rot[8] * z;
	x = nx; y = ny; z = nz;

	return *this;
}


// La colonna di traslazione è ignorata
template<class T>
inline vec3<T>& vec3<T>::rotate(const mat4<T> & _rot)
{
	T nx = _rot[0] * x + _rot[4] * y + _rot[8]  * z;
	T ny = _rot[1] * x + _rot[5] * y + _rot[9]  * z;
	T nz = _rot[2] * x + _rot[6] * y + _rot[10] * z;
	x = nx; y = ny; z = nz;

	return *this;
}


template<class T>
inline vec3<T> vec3<T>::rotated_by(T _rad, const vec3<T> & _axis) const
{
	vec3f v(*this);
	v.rotate(_rad,_axis);
	return v;
}


template<class T>
inline vec3<T> vec3<T>::rotated_by(const mat3<T> & _rot) const
{
	vec3f v(*this);
	v.rotate(_rot);
	return v;
}


template<class T>
inline vec3<T> vec3<T>::rotated_by(const mat4<T> & _rot) const
{
	vec3f v(*this);
	v.rotate(_rot);
	return v;
}


template<class T>
inline vec3<T>& vec3<T>::invert()
{
	x = -x;
	y = -y;
	z = -z;

	return *this;
}


template<class T>
inline vec2<T> vec3<T>::xy() const
{
	return vec2<T>(x,y);
}


template<class T>
inline vec2<T> vec3<T>::xz() const
{
	return vec2<T>(x,z);
}


template<class T>
inline vec2<T> vec3<T>::yz() const
{
	return vec2<T>(y,z);
}


template<class T>
inline vec3<T> vec3<T>::xy3(T _z) const
{
	return vec3<T>(x,y,_z);
}


template<class T>
inline vec3<T> vec3<T>::xz3(T _y) const
{
	return vec3<T>(x,_y,z);
}


template<class T>
inline vec3<T> vec3<T>::yz3(T _x) const
{
	return vec3<T>(_x,y,z);
}


template<class T>
void vec3<T>::copy_from(const T* _data)
{
	ASSERT(_data != NULL);
	x = _data[0];
	y = _data[1];
	z = _data[2];
}


//------------------------------------------------------------------------------



template<class T>
vec4<T>::vec4()
:
x((T)0),
y((T)0),
z((T)0),
w((T)0)
{
}


template<class T>
vec4<T>::vec4(T _x, T _y, T _z, T _w)
:
x(_x),
y(_y),
z(_z),
w(_w)
{
}


template<class T>
vec4<T>::vec4(T _val)
:
x(_val),
y(_val),
z(_val),
w(_val)
{
}


template<class T>
vec4<T>::vec4(const vec3<T>& _v, T _w)
{
	x = _v.x;
	y = _v.y;
	z = _v.z;
	w = _w;
}


template<class T>
vec4<T>::vec4(const T *_vec)
{
	x = _vec[0];
	y = _vec[1];
	z = _vec[2];
	w = _vec[3];
}


template<class T>
inline vec4<T>::operator T*()
{
	return &x;
}


template<class T>
inline vec4<T>::operator const T*() const
{
	return &x;
}


template<class T>
inline vec4<T> vec4<T>::operator - () const
{
	return vec4<T>(-x,-y,-z,-w);
}


template<class T>
inline vec4<T> vec4<T>::operator + (const vec4<T> &_v) const
{
	return vec4<T>(x+_v.x,y+_v.y,z+_v.z,w+_v.w);
}


template<class T>
inline vec4<T> vec4<T>::operator - (const vec4<T> &_v) const
{
	return vec4<T>(x-_v.x,y-_v.y,z-_v.z,w-_v.w);
}


template<class T>
inline void vec4<T>::operator = (const vec3<T> &_v)
{
	x = _v.x;
	y = _v.y;
	z = _v.z;
	w = (T)1;
}


template<class T>
inline vec4<T>& vec4<T>::normalize()
{
    T invlen = 1.0/(sqrt(x*x + y*y + z*z + w*w));
	x *= invlen;
	y *= invlen;
	z *= invlen;
	w *= invlen;

	return *this;
}


template<class T>
inline vec3<T> vec4<T>::xyz()
{
	return vec3<T>(x,y,z);
}


template<class T>
inline vec2<T> vec4<T>::xy()
{
	return vec2<T>(x,y);
}


template<class T>
void vec4<T>::copy_from(const T* _data)
{
	ASSERT(_data != NULL);
	x = _data[0];
	y = _data[1];
	z = _data[2];
	w = _data[3];
}

