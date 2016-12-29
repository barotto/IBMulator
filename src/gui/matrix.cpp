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

#include "ibmulator.h"
#include "matrix.h"


//------------------------------------------------------------------------------


template<> const mat3<int> mat3<int>::I(1, 0, 0,
                                        0, 1, 0,
                                        0, 0, 1);

template<> const mat3<float> mat3<float>::I(1.f, .0f, .0f,
                                            .0f, 1.f, .0f,
                                            .0f, .0f, 1.f);

template<> const mat3<double> mat3<double>::I(1., .0, .0,
                                              .0, 1., .0,
                                              .0, .0, 1.);

template<> const mat3<int> mat3<int>::Z(0, 0, 0,
                                        0, 0, 0,
                                        0, 0, 0);

template<> const mat3<float> mat3<float>::Z(.0f, .0f, .0f,
                                            .0f, .0f, .0f,
                                            .0f, .0f, .0f);

template<> const mat3<double> mat3<double>::Z(.0, .0, .0,
                                              .0, .0, .0,
                                              .0, .0, .0);


//------------------------------------------------------------------------------



template<> const mat4<int> mat4<int>::I(1, 0, 0, 0,
                                        0, 1, 0, 0,
                                        0, 0, 1, 0,
                                        0, 0, 0, 1);

template<> const mat4<float> mat4<float>::I(1.f, .0f, .0f, .0f,
                                            .0f, 1.f, .0f, .0f,
                                            .0f, .0f, 1.f, .0f,
                                            .0f, .0f, .0f, 1.f);

template<> const mat4<double> mat4<double>::I(1., .0, .0, .0,
                                              .0, 1., .0, .0,
                                              .0, .0, 1., .0,
                                              .0, .0, .0, 1.);

template<> const mat4<int> mat4<int>::Z(0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0,
                                        0, 0, 0, 0);

template<> const mat4<float> mat4<float>::Z(.0f, .0f, .0f, .0f,
                                            .0f, .0f, .0f, .0f,
                                            .0f, .0f, .0f, .0f,
                                            .0f, .0f, .0f, .0f);

template<> const mat4<double> mat4<double>::Z(.0, .0, .0, .0,
                                              .0, .0, .0, .0,
                                              .0, .0, .0, .0,
                                              .0, .0, .0, .0);
