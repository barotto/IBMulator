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

#ifndef MATRIX_H
#define MATRIX_H

#include "vector.h"



/*
OpenGL compatible
*/
template<class T>
class mat3
{

protected:

	T m_data[3*3];

public:

	mat3();
	mat3(
		T e00, T e01, T e02,
		T e10, T e11, T e12,
		T e20, T e21, T e22
		);
	mat3(const mat3<T>& _copy);
	~mat3();

	void load_zero();
	void load_identity();
	void load_diagonal(const vec3<T> &_vec);
	void load_diagonal(T _v0, T _v1, T _v2);
	void load_rotation(float _rad, const vec3<T> &_vec);
	inline bool is_identity();
	inline const T* data() const;
	inline const T* get_col(unsigned _c) const;
	inline const T* get_col_x() const;
	inline const T* get_col_y() const;
	inline const T* get_col_z() const;
	inline T& operator[] (int _e);
	inline const T& operator[] (int _e) const;
	inline mat3<T> operator + (const mat3<T> &_mat) const;
	inline mat3<T> operator * (T _scalar) const;
	inline vec3<T> operator * (const vec3<T> &_vec) const;
	inline mat3<T> operator * (const mat3<T> &_mat) const;
	inline operator T*();
	inline void operator=(const mat3<T>& _mat);

	void transpose();
	bool invert();

	void copy_from(const T* _data);

	static const mat3<T> I;
	static const mat3<T> Z;
};


typedef mat3<int> mat3i;
typedef mat3<float> mat3f;
typedef mat3<double> mat3d;



//-----------------------------------------------------------------------------



/*
OpenGL compatible
*/
template<class T>
class mat4
{

protected:

	T m_data[4*4];

public:

	mat4();
	mat4(const mat3<T> &_rot, const vec3<T> &_trans);
	mat4(const mat3<T> &_m3);
	mat4(
		T e00, T e01, T e02, T e03,
		T e10, T e11, T e12, T e13,
		T e20, T e21, T e22, T e23,
		T e30, T e31, T e32, T e33
		);
	mat4(const mat4<T>& _copy);
	mat4(const T* _copy);
	mat4(T _s);
	~mat4();

	const mat4<T> & operator = (const mat4<T> &_mat);

	inline void load(const T* _copy);
	inline void load_zero();
	inline void load_identity();
	inline void load_diagonal(const vec4<T>& _v);
	inline bool is_identity();
	inline T* data();
	inline const T* data() const;
	inline const T* get_col_x() const;
	inline const T* get_col_y() const;
	inline const T* get_col_z() const;
	inline const T* get_col_w() const;
	inline T& operator [] (int _e);
	inline const T& operator [] (int _e) const;
	inline T& element(int _row, int _col);
	inline const T& element(int _row, int _col) const;
	void load_rotation(const mat3<T> &_rot);
	void load_rotation(float _rad, const vec3<T> &_vec);
	void load_translation(const vec3<T> &_tra);
	void load_translation(T _x, T _y, T _z);
	void load_scale(T _x, T _y, T _z);
	void multiply(const T* _mat);
	inline mat4<T> operator * (T _scalar) const;
	inline vec4<T> operator * (const vec4<T> &_vec) const;
	inline mat4<T> operator * (const mat4<T> &_mat) const;
	inline mat4<T> operator + (const mat4<T> &_mat) const;
	inline operator T * ();
	inline operator const T * () const;
	inline vec3<T> get_translation() const;
	inline vec3<T> trans() const; //alias of get_translation()
	inline mat3<T> get_rotation() const;
	inline mat3<T> rot() const; //alias of get_rotation()

	void copy_from(const T* _data);
	void transpose();
	bool invert();


	static const mat4<T> I;
	static const mat4<T> Z;
};


template <class T>
inline mat4<T> operator * (T _scalar, const mat4<T>& _mat);


typedef mat4<int> mat4i;
typedef mat4<float> mat4f;
typedef mat4<double> mat4d;

template <class T>
inline mat4<T> mat4_frustum(T _l, T _r, T _b, T _t, T _n, T _f)
{
	return mat4<T>(
		((T)2*_n)/(_r-_l), (T)0,              (_r+_l)/(_r-_l),    (T)0,
		(T)0,              ((T)2*_n)/(_t-_b), (_t+_b)/(_t-_b),    (T)0,
		(T)0,              (T)0,              -(_f+_n)/(_f-_n),   -((T)2*_f*_n)/(_f-_n),
		(T)0,              (T)0,              (T)-1,              (T)0
	);
}


template <class T>
inline mat4<T> mat4_ortho(T _l, T _r, T _b, T _t, T _n, T _f)
{
	return mat4<T>(
		(T)2/(_r-_l), (T)0,         (T)0,          -(_r+_l)/(_r-_l),
		(T)0,         (T)2/(_t-_b), (T)0,          -(_t+_b)/(_t-_b),
		(T)0,         (T)0,         (T)-2/(_f-_n), -(_f+_n)/(_f-_n),
		(T)0,         (T)0,         (T)0,          (T)1
	);
}

//-----------------------------------------------------------------------------



#include "matrix.inl.h"



#endif
