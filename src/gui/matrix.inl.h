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

#include <cmath>
#include <cstring>

#define SWAP(a,b,t) { t = a; a = b; b = t; }

/*
row major to col maj
0 1 2    0 3 6
3 4 5 => 1 4 7  0=0, 1=3, 2=6, 3=1, 4=4, 5=7, 6=2, 7=5, 8=8
6 7 8    2 5 8
*/


template<class T>
mat3<T>::mat3()
{
}


template<class T>
mat3<T>::mat3(
		T e00, T e01, T e02,
		T e10, T e11, T e12,
		T e20, T e21, T e22
		)
{
	m_data[0] = e00; m_data[3] = e01; m_data[6] = e02;
	m_data[1] = e10; m_data[4] = e11; m_data[7] = e12;
	m_data[2] = e20; m_data[5] = e21; m_data[8] = e22;
}


template<class T>
mat3<T>::mat3(const mat3<T>& _copy)
{
	m_data[0] = _copy.m_data[0];
	m_data[1] = _copy.m_data[1];
	m_data[2] = _copy.m_data[2];

	m_data[3] = _copy.m_data[3];
	m_data[4] = _copy.m_data[4];
	m_data[5] = _copy.m_data[5];

	m_data[6] = _copy.m_data[6];
	m_data[7] = _copy.m_data[7];
	m_data[8] = _copy.m_data[8];
}


template<class T>
mat3<T>::~mat3()
{
}


template<class T>
void mat3<T>::load_zero()
{
	m_data[0] = m_data[1] = m_data[2] = (T)0.0;
	m_data[3] = m_data[4] = m_data[5] = (T)0.0;
	m_data[6] = m_data[7] = m_data[8] = (T)0.0;
}


template<class T>
void mat3<T>::load_identity()
{
	m_data[1] = m_data[2] = (T)0.0;
	m_data[3] = m_data[5] = (T)0.0;
	m_data[6] = m_data[7] = (T)0.0;
	m_data[0] = (T)1.0;
	m_data[4] = (T)1.0;
	m_data[8] = (T)1.0;
}


template<class T>
void mat3<T>::load_diagonal(const vec3<T>& _diag)
{
	m_data[1] = m_data[2] = (T)0.0;
	m_data[3] = m_data[5] = (T)0.0;
	m_data[6] = m_data[7] = (T)0.0;
	m_data[0] = _diag.x;
	m_data[4] = _diag.y;
	m_data[8] = _diag.z;
}


template<class T>
void mat3<T>::load_diagonal(T _v0, T _v1, T _v2)
{
	m_data[1] = m_data[2] = (T)0.0;
	m_data[3] = m_data[5] = (T)0.0;
	m_data[6] = m_data[7] = (T)0.0;
	m_data[0] = _v0;
	m_data[4] = _v1;
	m_data[8] = _v2;
}


template<class T>
const T* mat3<T>::data() const
{
	return m_data;
}


template<class T>
const T* mat3<T>::get_col(unsigned _c) const
{
	return &m_data[_c*3];
}


template<class T>
const T* mat3<T>::get_col_x() const
{
	return &m_data[0];
}


template<class T>
const T* mat3<T>::get_col_y() const
{
	return &m_data[3];
}


template<class T>
const T* mat3<T>::get_col_z() const
{
	return &m_data[6];
}


template<class T>
T& mat3<T>::operator[] (int _e)
{
	return m_data[_e];
}


template<class T>
const T& mat3<T>::operator[] (int _e) const
{
	return m_data[_e];
}


template<class T>
mat3<T> mat3<T>::operator + (const mat3<T> &_mat) const
{
	return mat3<T> (
		m_data[0]+_mat[0], m_data[3]+_mat[3], m_data[6]+_mat[6],
		m_data[1]+_mat[1], m_data[4]+_mat[4], m_data[7]+_mat[7],
		m_data[2]+_mat[2], m_data[5]+_mat[5], m_data[8]+_mat[8]
	);
}


template<class T>
mat3<T> mat3<T>::operator * (T _scalar) const
{
	return mat3<T> (
		m_data[0]*_scalar, m_data[3]*_scalar, m_data[6]*_scalar,
		m_data[1]*_scalar, m_data[4]*_scalar, m_data[7]*_scalar,
		m_data[2]*_scalar, m_data[5]*_scalar, m_data[8]*_scalar
		);
}


template<class T>
vec3<T> mat3<T>::operator * (const vec3<T> &_vec) const
{
	return vec3<T> (
		m_data[0]*_vec.x + m_data[3]*_vec.y + m_data[6]*_vec.z,
		m_data[1]*_vec.x + m_data[4]*_vec.y + m_data[7]*_vec.z,
		m_data[2]*_vec.x + m_data[5]*_vec.y + m_data[8]*_vec.z
		);
}


template<class T>
mat3<T> mat3<T>::operator * (const mat3<T> &_mat) const
{
	return mat3<T> (
		m_data[0]*_mat[0]+m_data[3]*_mat[1]+m_data[6]*_mat[2],
		m_data[0]*_mat[3]+m_data[3]*_mat[4]+m_data[6]*_mat[5],
		m_data[0]*_mat[6]+m_data[3]*_mat[7]+m_data[6]*_mat[8],

		m_data[1]*_mat[0]+m_data[4]*_mat[1]+m_data[7]*_mat[2],
		m_data[1]*_mat[3]+m_data[4]*_mat[4]+m_data[7]*_mat[5],
		m_data[1]*_mat[6]+m_data[4]*_mat[7]+m_data[7]*_mat[8],

		m_data[2]*_mat[0]+m_data[5]*_mat[1]+m_data[8]*_mat[2],
		m_data[2]*_mat[3]+m_data[5]*_mat[4]+m_data[8]*_mat[5],
		m_data[2]*_mat[6]+m_data[5]*_mat[7]+m_data[8]*_mat[8]
		);
}

template<class T>
void mat3<T>::load_rotation(float _rad, const vec3<T> &_vec)
{
	T sinSave, cosSave, oneMinusCos;
	T xx, yy, zz, xy, yz, zx, xs, ys, zs;

	// If NULL vector passed in, this will blow up...

	sinSave = (T)sin(_rad);
	cosSave = (T)cos(_rad);
	oneMinusCos = (T)1 - cosSave;

	xx = _vec.x * _vec.x;
	yy = _vec.y * _vec.y;
	zz = _vec.z * _vec.z;
	xy = _vec.x * _vec.y;
	yz = _vec.y * _vec.z;
	zx = _vec.z * _vec.x;
	xs = _vec.x * sinSave;
	ys = _vec.y * sinSave;
	zs = _vec.z * sinSave;

	m_data[0] = (T)((oneMinusCos * xx) + cosSave);
	m_data[3] = (T)((oneMinusCos * xy) - zs);
	m_data[6] = (T)((oneMinusCos * zx) + ys);

	m_data[1] = (T)((oneMinusCos * xy) + zs);
	m_data[4] = (T)((oneMinusCos * yy) + cosSave);
	m_data[7] = (T)((oneMinusCos * yz) - xs);

	m_data[2] = (T)((oneMinusCos * zx) - ys);
	m_data[5] = (T)((oneMinusCos * yz) + xs);
	m_data[8] = (T)((oneMinusCos * zz) + cosSave);
}

template<class T>
mat3<T>::operator T*()
{
	return m_data;
}

template<class T>
void mat3<T>::operator=(const mat3<T>& _mat)
{
	for(uint i=0; i<9; i++)
		m_data[i] = _mat.m_data[i];
}

template<class T>
bool mat3<T>::is_identity()
{
	bool diag = (m_data[0] == (T)1 && m_data[4] == (T)1 && m_data[8]);
	bool zero = (m_data[1] == (T)0 && m_data[2] == (T)0 && m_data[3] == (T)0
		&& m_data[5] == (T)0 &&	m_data[6] == (T)0 && m_data[7] == (T)0);
	return diag && zero;
}


template<class T>
void mat3<T>::copy_from(const T* _data)
{
	assert(_data != nullptr);
	Memcopy(m_data, _data, sizeof(T)*9);
}


template<class T>
void mat3<T>::transpose()
{
	T temp;
	SWAP(m_data[1],m_data[3],temp);
	SWAP(m_data[2],m_data[6],temp);
	SWAP(m_data[5],m_data[7],temp);
}


template<class T>
bool mat3<T>::invert()
{
	/* http://en.wikipedia.org/wiki/Invertible_matrix
	A computationally efficient 3x3 matrix inversion is given by

		   [ a b c ]       1 [ A B C ]
	A^-1 = | d e f | ^-1 = - | D E F |
		   [ g h k ]       Z [ G H K ]

	where

	Z = a(ek − fh) + b(fg − dk) + c(dh − eg)

	which is the determinant of the matrix.
	If Z is finite (non-zero), the matrix is invertible,
	with the elements of the above matrix on the right side given by

	A = (ek - fh) B = (ch - bk) C = (bf - ce)
	D = (fg - dk) E = (ak - cg) F = (cd - af)
	G = (dh - eg) H = (bg - ah) K = (ae - bd)
	*/
	/*
	T a=m_data[0], b=m_data[3], c=m_data[6];
	T d=m_data[1], e=m_data[4], f=m_data[7];
	T g=m_data[2], h=m_data[5], k=m_data[8];

	T Z = (a*(e*k - f*h) + b*(f*g - d*k) + c*(d*h - e*g));
	if(Z == T(0)) {
		return false;
	}
	T iZ = T(1)/Z;
	T A=(e*k - f*h), B=(c*h - b*k), C=(b*f - c*e);
	T D=(f*g - d*k), E=(a*k - c*g), F=(c*d - a*f);
	T G=(d*h - e*g), H=(b*g - a*h), K=(a*e - b*d);

	m_data[0]=A*iZ; m_data[3]=B*iZ; m_data[6]=C*iZ;
	m_data[1]=D*iZ; m_data[4]=E*iZ; m_data[7]=F*iZ;
	m_data[2]=G*iZ; m_data[5]=H*iZ; m_data[8]=K*iZ;
	*/

    // Invert a 3x3 using cofactors.  This is faster than using a generic
    // Gaussian elimination because of the loop overhead of such a method.

    mat3<T> inverse;

	// Compute the adjoint.
	inverse.m_data[0] = m_data[4]*m_data[8] - m_data[7]*m_data[5];
	inverse.m_data[1] = m_data[7]*m_data[2] - m_data[1]*m_data[8];
	inverse.m_data[2] = m_data[1]*m_data[5] - m_data[4]*m_data[2];
	inverse.m_data[3] = m_data[6]*m_data[5] - m_data[3]*m_data[8];
	inverse.m_data[4] = m_data[0]*m_data[8] - m_data[6]*m_data[2];
	inverse.m_data[5] = m_data[3]*m_data[2] - m_data[0]*m_data[5];
	inverse.m_data[6] = m_data[3]*m_data[7] - m_data[6]*m_data[4];
	inverse.m_data[7] = m_data[6]*m_data[1] - m_data[0]*m_data[7];
	inverse.m_data[8] = m_data[0]*m_data[4] - m_data[3]*m_data[1];

    T det = m_data[0]*inverse.m_data[0] + m_data[3]*inverse.m_data[1] + m_data[6]*inverse.m_data[2];

    //should compare the fabs(abs) to an epsilon!
    if(det == T(0))
    {
    	return false;
    }

	T invDet = (T(1))/det;
	m_data[0] = inverse.m_data[0] * invDet;
	m_data[1] = inverse.m_data[1] * invDet;
	m_data[2] = inverse.m_data[2] * invDet;
	m_data[3] = inverse.m_data[3] * invDet;
	m_data[4] = inverse.m_data[4] * invDet;
	m_data[5] = inverse.m_data[5] * invDet;
	m_data[6] = inverse.m_data[6] * invDet;
	m_data[7] = inverse.m_data[7] * invDet;
	m_data[8] = inverse.m_data[8] * invDet;

	return true;
}


//------------------------------------------------------------------------------


/* row major to column major
 0   1   2   3    0   4   8  12
 4   5   6   7    1   5   9  13
 8   9  10  11    2   6  10  14   1=4, 2=8, 3=12, 4=1, 6=9, 7=13, 8=2, 9=6, 11=14, 12=3, 13=7, 14=11
12  13  14  15    3   7  11  15
*/



template<class T>
mat4<T>::mat4()
{
}


template<class T>
mat4<T>::mat4(const mat3<T> &_rot, const vec3<T> &_trans)
{
	m_data[0] = _rot[0];
	m_data[1] = _rot[1];
	m_data[2] = _rot[2];
	m_data[3] = 0;

	m_data[4] = _rot[3];
	m_data[5] = _rot[4];
	m_data[6] = _rot[5];
	m_data[7] = 0;

	m_data[8] = _rot[6];
	m_data[9] = _rot[7];
	m_data[10] = _rot[8];
	m_data[11] = 0;

	m_data[12] = _trans.x;
	m_data[13] = _trans.y;
	m_data[14] = _trans.z;
	m_data[15] = 1;
}


/** Copia una mat3 partendo da (0,0) e mettendo a zero il resto.
*/
template<class T>
mat4<T>::mat4(const mat3<T> &_m3)
{
	m_data[0] = _m3[0];
	m_data[1] = _m3[1];
	m_data[2] = _m3[2];
	m_data[3] = 0;

	m_data[4] = _m3[3];
	m_data[5] = _m3[4];
	m_data[6] = _m3[5];
	m_data[7] = 0;

	m_data[8] = _m3[6];
	m_data[9] = _m3[7];
	m_data[10] = _m3[8];
	m_data[11] = 0;

	m_data[12] = 0;
	m_data[13] = 0;
	m_data[14] = 0;
	m_data[15] = 0;
}


template<class T>
mat4<T>::mat4(
		T e00, T e01, T e02, T e03,
		T e10, T e11, T e12, T e13,
		T e20, T e21, T e22, T e23,
		T e30, T e31, T e32, T e33
		)
{
	m_data[0] = e00; m_data[4] = e01;  m_data[8] = e02; m_data[12] = e03;
	m_data[1] = e10; m_data[5] = e11;  m_data[9] = e12; m_data[13] = e13;
	m_data[2] = e20; m_data[6] = e21; m_data[10] = e22; m_data[14] = e23;
	m_data[3] = e30; m_data[7] = e31; m_data[11] = e32; m_data[15] = e33;
}


template<class T>
mat4<T>::mat4(const mat4<T>& _copy)
{
	m_data[0] = _copy.m_data[0];
	m_data[1] = _copy.m_data[1];
	m_data[2] = _copy.m_data[2];
	m_data[3] = _copy.m_data[3];

	m_data[4] = _copy.m_data[4];
	m_data[5] = _copy.m_data[5];
	m_data[6] = _copy.m_data[6];
	m_data[7] = _copy.m_data[7];

	m_data[8] = _copy.m_data[8];
	m_data[9] = _copy.m_data[9];
	m_data[10] = _copy.m_data[10];
	m_data[11] = _copy.m_data[11];

	m_data[12] = _copy.m_data[12];
	m_data[13] = _copy.m_data[13];
	m_data[14] = _copy.m_data[14];
	m_data[15] = _copy.m_data[15];
}


template<class T>
mat4<T>::mat4(const T* _copy)
{
	load(_copy);
}


template<class T>
mat4<T>::mat4(T _s)
{
	m_data[0] = _s;
	m_data[1] = _s;
	m_data[2] = _s;
	m_data[3] = _s;

	m_data[4] = _s;
	m_data[5] = _s;
	m_data[6] = _s;
	m_data[7] = _s;

	m_data[8] = _s;
	m_data[9] = _s;
	m_data[10] = _s;
	m_data[11] = _s;

	m_data[12] = _s;
	m_data[13] = _s;
	m_data[14] = _s;
	m_data[15] = _s;
}


template<class T>
mat4<T>::~mat4()
{
}


template<class T>
void mat4<T>::load(const T* _copy)
{
	m_data[0] = _copy[0];
	m_data[1] = _copy[1];
	m_data[2] = _copy[2];
	m_data[3] = _copy[3];

	m_data[4] = _copy[4];
	m_data[5] = _copy[5];
	m_data[6] = _copy[6];
	m_data[7] = _copy[7];

	m_data[8] = _copy[8];
	m_data[9] = _copy[9];
	m_data[10] = _copy[10];
	m_data[11] = _copy[11];

	m_data[12] = _copy[12];
	m_data[13] = _copy[13];
	m_data[14] = _copy[14];
	m_data[15] = _copy[15];
}


template<class T>
void mat4<T>::load_zero()
{
	memset(m_data,0,sizeof(T)*16);
}


template<class T>
void mat4<T>::load_identity()
{
	load_zero();
	m_data[0] = (T)1;
	m_data[5] = (T)1;
	m_data[10] = (T)1;
	m_data[15] = (T)1;
}


template<class T>
const T* mat4<T>::data() const
{
	return m_data;
}


template<class T>
T* mat4<T>::data()
{
	return m_data;
}


template<class T>
T& mat4<T>::operator [] (int _e)
{
	return m_data[_e];
}


template<class T>
const T& mat4<T>::operator [] (int _e) const
{
	return m_data[_e];
}


template<class T>
void mat4<T>::load_rotation(const mat3<T> &_rot)
{
	m_data[0] = _rot[0]; m_data[4] = _rot[3]; m_data[8] = _rot[6];
	m_data[1] = _rot[1]; m_data[5] = _rot[4]; m_data[9] = _rot[7];
	m_data[2] = _rot[2]; m_data[6] = _rot[5]; m_data[10] = _rot[8];
}


template<class T>
void mat4<T>::load_translation(const vec3<T> &_tra)
{
	m_data[12] = _tra.x;
	m_data[13] = _tra.y;
	m_data[14] = _tra.z;
}

template<class T>
void mat4<T>::load_translation(T _x, T _y, T _z)
{
	m_data[12] = _x;
	m_data[13] = _y;
	m_data[14] = _z;
}

template<class T>
void mat4<T>::load_scale(T _x, T _y, T _z)
{
	m_data[0] = _x;
	m_data[5] = _y;
	m_data[10] = _z;
}

template<class T>
void mat4<T>::load_rotation(float _rad, const vec3<T> &_vec)
{
	T sinSave, cosSave, oneMinusCos;
	T xx, yy, zz, xy, yz, zx, xs, ys, zs;

	sinSave = (T)sin(_rad);
	cosSave = (T)cos(_rad);
	oneMinusCos = (T)1 - cosSave;

	xx = _vec.x * _vec.x;
	yy = _vec.y * _vec.y;
	zz = _vec.z * _vec.z;
	xy = _vec.x * _vec.y;
	yz = _vec.y * _vec.z;
	zx = _vec.z * _vec.x;
	xs = _vec.x * sinSave;
	ys = _vec.y * sinSave;
	zs = _vec.z * sinSave;

	m_data[0] = (T)((oneMinusCos * xx) + cosSave);
	m_data[4] = (T)((oneMinusCos * xy) - zs);
	m_data[8] = (T)((oneMinusCos * zx) + ys);

	m_data[1] = (T)((oneMinusCos * xy) + zs);
	m_data[5] = (T)((oneMinusCos * yy) + cosSave);
	m_data[9] = (T)((oneMinusCos * yz) - xs);

	m_data[2] = (T)((oneMinusCos * zx) - ys);
	m_data[6] = (T)((oneMinusCos * yz) + xs);
	m_data[10] = (T)((oneMinusCos * zz) + cosSave);
}


template<class T>
void mat4<T>::load_diagonal(const vec4<T>& _v)
{
	m_data[0] = _v.x;
	m_data[5] = _v.y;
	m_data[10] = _v.z;
	m_data[15] = _v.w;
}


template<class T>
mat4<T>::operator T*()
{
	return m_data;
}


template<class T>
mat4<T>::operator const T*() const
{
	return m_data;
}


template<class T>
vec3<T> mat4<T>::get_translation() const
{
	return vec3<T>(&m_data[12]);
}


template<class T>
vec3<T> mat4<T>::trans() const
{
	return vec3<T>(&m_data[12]);
}


template<class T>
mat3<T> mat4<T>::get_rotation() const
{
	return mat3<T>(
		m_data[0], m_data[4], m_data[8],
		m_data[1], m_data[5], m_data[9],
		m_data[2], m_data[6], m_data[10]
		);
}


template<class T>
mat3<T> mat4<T>::rot() const
{
	return mat3<T>(
		m_data[0], m_data[4], m_data[8],
		m_data[1], m_data[5], m_data[9],
		m_data[2], m_data[6], m_data[10]
		);
}


template<class T>
bool mat4<T>::is_identity()
{
	bool diag = (m_data[0] == (T)1 && m_data[5] == (T)1 && m_data[10] == (T)1 && m_data[15] == (T)1);
	bool zero = (m_data[1] == (T)0 && m_data[2] == (T)0 && m_data[3] == (T)0 && m_data[4] == (T)0
		&& m_data[6] == (T)0 && m_data[7] == (T)0 && m_data[8] == (T)0 && m_data[9] == (T)0
		&& m_data[11] == (T)0 && m_data[12] == (T)0 && m_data[13] == (T)0 && m_data[14] == (T)0);
	return diag && zero;
}


template<class T>
mat4<T> mat4<T>::operator * (T _scalar) const
{
	return mat4<T> (
		m_data[0]*_scalar, m_data[4]*_scalar, m_data[8]*_scalar, m_data[12]*_scalar,
		m_data[1]*_scalar, m_data[5]*_scalar, m_data[9]*_scalar, m_data[13]*_scalar,
		m_data[2]*_scalar, m_data[6]*_scalar, m_data[10]*_scalar, m_data[14]*_scalar,
		m_data[3]*_scalar, m_data[7]*_scalar, m_data[11]*_scalar, m_data[15]*_scalar
		);
}


template<class T>
vec4<T> mat4<T>::operator * (const vec4<T> &_vec) const
{
	return vec4<T> (
		m_data[0]*_vec.x + m_data[4]*_vec.y + m_data[8]*_vec.z + m_data[12]*_vec.w,
		m_data[1]*_vec.x + m_data[5]*_vec.y + m_data[9]*_vec.z + m_data[13]*_vec.w,
		m_data[2]*_vec.x + m_data[6]*_vec.y + m_data[10]*_vec.z + m_data[14]*_vec.w,
		m_data[3]*_vec.x + m_data[7]*_vec.y + m_data[11]*_vec.z + m_data[15]*_vec.w
		);
}



template<class T>
mat4<T> mat4<T>::operator * (const mat4<T> &_mat) const
{
	return mat4<T> (
		m_data[0]*_mat[0]+m_data[4]*_mat[1]+m_data[8]*_mat[2]+m_data[12]*_mat[3],
		m_data[0]*_mat[4]+m_data[4]*_mat[5]+m_data[8]*_mat[6]+m_data[12]*_mat[7],
		m_data[0]*_mat[8]+m_data[4]*_mat[9]+m_data[8]*_mat[10]+m_data[12]*_mat[11],
		m_data[0]*_mat[12]+m_data[4]*_mat[13]+m_data[8]*_mat[14]+m_data[12]*_mat[15],

		m_data[1]*_mat[0]+m_data[5]*_mat[1]+m_data[9]*_mat[2]+m_data[13]*_mat[3],
		m_data[1]*_mat[4]+m_data[5]*_mat[5]+m_data[9]*_mat[6]+m_data[13]*_mat[7],
		m_data[1]*_mat[8]+m_data[5]*_mat[9]+m_data[9]*_mat[10]+m_data[13]*_mat[11],
		m_data[1]*_mat[12]+m_data[5]*_mat[13]+m_data[9]*_mat[14]+m_data[13]*_mat[15],

		m_data[2]*_mat[0]+m_data[6]*_mat[1]+m_data[10]*_mat[2]+m_data[14]*_mat[3],
		m_data[2]*_mat[4]+m_data[6]*_mat[5]+m_data[10]*_mat[6]+m_data[14]*_mat[7],
		m_data[2]*_mat[8]+m_data[6]*_mat[9]+m_data[10]*_mat[10]+m_data[14]*_mat[11],
		m_data[2]*_mat[12]+m_data[6]*_mat[13]+m_data[10]*_mat[14]+m_data[14]*_mat[15],

		m_data[3]*_mat[0]+m_data[7]*_mat[1]+m_data[11]*_mat[2]+m_data[15]*_mat[3],
		m_data[3]*_mat[4]+m_data[7]*_mat[5]+m_data[11]*_mat[6]+m_data[15]*_mat[7],
		m_data[3]*_mat[8]+m_data[7]*_mat[9]+m_data[11]*_mat[10]+m_data[15]*_mat[11],
		m_data[3]*_mat[12]+m_data[7]*_mat[13]+m_data[11]*_mat[14]+m_data[15]*_mat[15]
	);
}


template<class T>
mat4<T> mat4<T>::operator + (const mat4<T> &_mat) const
{
	return mat4<T> (
		m_data[0]+_mat[0], m_data[4]+_mat[4], m_data[8]+_mat[8], m_data[12]+_mat[12],
		m_data[1]+_mat[1], m_data[5]+_mat[5], m_data[9]+_mat[9], m_data[13]+_mat[13],
		m_data[2]+_mat[2], m_data[6]+_mat[6], m_data[10]+_mat[10], m_data[14]+_mat[14],
		m_data[3]+_mat[3], m_data[7]+_mat[7], m_data[11]+_mat[11], m_data[15]+_mat[15]
	);
}


template<class T>
const mat4<T> & mat4<T>::operator = (const mat4<T> &_mat)
{
	for(uint i=0; i<16; i++)
		m_data[i] = _mat.m_data[i];
	return *this;
}


template <class T>
mat4<T> operator * (T _s, const mat4<T>& _mat)
{
	return mat4<T> (
		_mat[0]*_s, _mat[4]*_s, _mat[8]*_s, _mat[12]*_s,
		_mat[1]*_s, _mat[5]*_s, _mat[9]*_s, _mat[13]*_s,
		_mat[2]*_s, _mat[6]*_s, _mat[10]*_s,_mat[14]*_s,
		_mat[3]*_s, _mat[7]*_s, _mat[11]*_s,_mat[15]*_s);
}


template<class T>
const T* mat4<T>::get_col_x() const
{
	return &m_data[0];
}


template<class T>
const T* mat4<T>::get_col_y() const
{
	return &m_data[4];
}


template<class T>
const T* mat4<T>::get_col_z() const
{
	return &m_data[8];
}


template<class T>
const T* mat4<T>::get_col_w() const
{
	return &m_data[12];
}


template<class T>
T& mat4<T>::element(int _row, int _col)
{
	assert(_row>=0 && _row<4);
	assert(_col>=0 && _col<4);

	return m_data[_row+_col*4];
}


template<class T>
const T& mat4<T>::element(int _row, int _col) const
{
	assert(_row>=0 && _row<4);
	assert(_col>=0 && _col<4);

	return m_data[_row+_col*4];
}


template<class T>
void mat4<T>::copy_from(const T* _data)
{
	assert(_data != nullptr);
	memcopy(m_data, _data, sizeof(T)*16);
}


template<class T>
void mat4<T>::transpose()
{
	T temp;
	SWAP(m_data[1],m_data[4],temp);
	SWAP(m_data[2],m_data[8],temp);
	SWAP(m_data[3],m_data[12],temp);
	SWAP(m_data[6],m_data[9],temp);
	SWAP(m_data[7],m_data[13],temp);
	SWAP(m_data[11],m_data[14],temp);
}


template<class T>
bool mat4<T>::invert()
{
	T a0 = m_data[ 0]*m_data[ 5] - m_data[ 4]*m_data[ 1];
	T a1 = m_data[ 0]*m_data[ 9] - m_data[ 8]*m_data[ 1];
	T a2 = m_data[ 0]*m_data[13] - m_data[12]*m_data[ 1];
	T a3 = m_data[ 4]*m_data[ 9] - m_data[ 8]*m_data[ 5];
	T a4 = m_data[ 4]*m_data[13] - m_data[12]*m_data[ 5];
	T a5 = m_data[ 8]*m_data[13] - m_data[12]*m_data[ 9];
	T b0 = m_data[ 2]*m_data[ 7] - m_data[ 6]*m_data[ 3];
	T b1 = m_data[ 2]*m_data[11] - m_data[10]*m_data[ 3];
	T b2 = m_data[ 2]*m_data[15] - m_data[14]*m_data[ 3];
	T b3 = m_data[ 6]*m_data[11] - m_data[10]*m_data[ 7];
	T b4 = m_data[ 6]*m_data[15] - m_data[14]*m_data[ 7];
	T b5 = m_data[10]*m_data[15] - m_data[14]*m_data[11];

	T det = a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
	//should compare the fabs(abs) to an epsilon!
	if(det == T(0))
	{
		return false;
	}

	mat4<T> inverse;
	inverse.m_data[ 0] = + m_data[ 5]*b5 - m_data[ 9]*b4 + m_data[13]*b3;
	inverse.m_data[ 1] = - m_data[ 1]*b5 + m_data[ 9]*b2 - m_data[13]*b1;
	inverse.m_data[ 2] = + m_data[ 1]*b4 - m_data[ 5]*b2 + m_data[13]*b0;
	inverse.m_data[ 3] = - m_data[ 1]*b3 + m_data[ 5]*b1 - m_data[ 9]*b0;
	inverse.m_data[ 4] = - m_data[ 4]*b5 + m_data[ 8]*b4 - m_data[12]*b3;
	inverse.m_data[ 5] = + m_data[ 0]*b5 - m_data[ 8]*b2 + m_data[12]*b1;
	inverse.m_data[ 6] = - m_data[ 0]*b4 + m_data[ 4]*b2 - m_data[12]*b0;
	inverse.m_data[ 7] = + m_data[ 0]*b3 - m_data[ 4]*b1 + m_data[ 8]*b0;
	inverse.m_data[ 8] = + m_data[ 7]*a5 - m_data[11]*a4 + m_data[15]*a3;
	inverse.m_data[ 9] = - m_data[ 3]*a5 + m_data[11]*a2 - m_data[15]*a1;
	inverse.m_data[10] = + m_data[ 3]*a4 - m_data[ 7]*a2 + m_data[15]*a0;
	inverse.m_data[11] = - m_data[ 3]*a3 + m_data[ 7]*a1 - m_data[11]*a0;
	inverse.m_data[12] = - m_data[ 6]*a5 + m_data[10]*a4 - m_data[14]*a3;
	inverse.m_data[13] = + m_data[ 2]*a5 - m_data[10]*a2 + m_data[14]*a1;
	inverse.m_data[14] = - m_data[ 2]*a4 + m_data[ 6]*a2 - m_data[14]*a0;
	inverse.m_data[15] = + m_data[ 2]*a3 - m_data[ 6]*a1 + m_data[10]*a0;

	T invDet = (T(1))/det;
	m_data[ 0] = inverse.m_data[ 0] * invDet;
	m_data[ 1] = inverse.m_data[ 1] * invDet;
	m_data[ 2] = inverse.m_data[ 2] * invDet;
	m_data[ 3] = inverse.m_data[ 3] * invDet;
	m_data[ 4] = inverse.m_data[ 4] * invDet;
	m_data[ 5] = inverse.m_data[ 5] * invDet;
	m_data[ 6] = inverse.m_data[ 6] * invDet;
	m_data[ 7] = inverse.m_data[ 7] * invDet;
	m_data[ 8] = inverse.m_data[ 8] * invDet;
	m_data[ 9] = inverse.m_data[ 9] * invDet;
	m_data[10] = inverse.m_data[10] * invDet;
	m_data[11] = inverse.m_data[11] * invDet;
	m_data[12] = inverse.m_data[12] * invDet;
	m_data[13] = inverse.m_data[13] * invDet;
	m_data[14] = inverse.m_data[14] * invDet;
	m_data[15] = inverse.m_data[15] * invDet;

	return true;
}


template<class T>
void mat4<T>::multiply(const T* _mat)
{
	T result[16];
	result[0] = m_data[0]*_mat[0]+m_data[4]*_mat[1]+m_data[8]*_mat[2]+m_data[12]*_mat[3];
	result[1] = m_data[1]*_mat[0]+m_data[5]*_mat[1]+m_data[9]*_mat[2]+m_data[13]*_mat[3];
	result[2] = m_data[2]*_mat[0]+m_data[6]*_mat[1]+m_data[10]*_mat[2]+m_data[14]*_mat[3];
	result[3] = m_data[3]*_mat[0]+m_data[7]*_mat[1]+m_data[11]*_mat[2]+m_data[15]*_mat[3];
	result[4] = m_data[0]*_mat[4]+m_data[4]*_mat[5]+m_data[8]*_mat[6]+m_data[12]*_mat[7];
	result[5] = m_data[1]*_mat[4]+m_data[5]*_mat[5]+m_data[9]*_mat[6]+m_data[13]*_mat[7];
	result[6] = m_data[2]*_mat[4]+m_data[6]*_mat[5]+m_data[10]*_mat[6]+m_data[14]*_mat[7];
	result[7] = m_data[3]*_mat[4]+m_data[7]*_mat[5]+m_data[11]*_mat[6]+m_data[15]*_mat[7];
	result[8] = m_data[0]*_mat[8]+m_data[4]*_mat[9]+m_data[8]*_mat[10]+m_data[12]*_mat[11];
	result[9] = m_data[1]*_mat[8]+m_data[5]*_mat[9]+m_data[9]*_mat[10]+m_data[13]*_mat[11];
	result[10] = m_data[2]*_mat[8]+m_data[6]*_mat[9]+m_data[10]*_mat[10]+m_data[14]*_mat[11];
	result[11] = m_data[3]*_mat[8]+m_data[7]*_mat[9]+m_data[11]*_mat[10]+m_data[15]*_mat[11];
	result[12] = m_data[0]*_mat[12]+m_data[4]*_mat[13]+m_data[8]*_mat[14]+m_data[12]*_mat[15];
	result[13] = m_data[1]*_mat[12]+m_data[5]*_mat[13]+m_data[9]*_mat[14]+m_data[13]*_mat[15];
	result[14] = m_data[2]*_mat[12]+m_data[6]*_mat[13]+m_data[10]*_mat[14]+m_data[14]*_mat[15];
	result[15] = m_data[3]*_mat[12]+m_data[7]*_mat[13]+m_data[11]*_mat[14]+m_data[15]*_mat[15];

	for(int i=0; i<16; i++)
		m_data[i] = result[i];
}

