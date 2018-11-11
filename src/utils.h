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

#ifndef IBMULATOR_UTILS_H
#define IBMULATOR_UTILS_H

/* Converts a int into a string, to be used in string concatenations */
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

template <typename _to_check, std::size_t _expected, std::size_t _real = sizeof(_to_check)>
void size_check()
{
	static_assert(_expected == _real, "Incorrect size!");
}

void str_replace_all(std::string &_str, const std::string &_search, const std::string &_replace);
std::string bitfield_to_string(uint8_t _bitfield,
		const std::array<std::string, 8> &_set_names);
std::string bitfield_to_string(uint8_t _bitfield,
		const std::array<std::string, 8> &_set_names,
		const std::array<std::string, 8> &_clear_names);

template<class T>
inline T clamp(T _value, T _lower, T _upper)
{
	return std::max(_lower, std::min(_value, _upper));
}

template<class T>
	T lerp(T _v0, T _v1, T _t)
{
	// does not guarantee v = v1 when t = 1, due to floating-point arithmetic error.
	// return _v0 + _t*(_v1-_v0);

	// guarantees v = v1 when t = 1.
	return (T(1)-_t)*_v0 + _t*_v1;
}

#include <functional>
#include <chrono>
#include <future>

template <class callable, class... arguments>
	void timed_event(int _after_ms, callable&& _func, arguments&&... _args)
{
	std::function<typename std::result_of<callable(arguments...)>::type()>
		event(std::bind(std::forward<callable>(_func), std::forward<arguments>(_args)...));

	std::thread([_after_ms, event]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(_after_ms));
		event();
	}).detach();
}

inline uint16_t read_16bit(const uint8_t* buf)
{
	return (buf[0] << 8) | buf[1];
}

inline uint32_t read_32bit(const uint8_t* buf)
{
	return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

inline uint8_t packet_field(uint8_t *packet, unsigned byte, unsigned start, unsigned num_bits)
{
	return ((packet[byte] >> start) & ((1 << num_bits) - 1));
}

inline uint16_t packet_word(uint8_t *packet, unsigned byte)
{
	return ( (uint16_t(packet[byte]) << 8) | packet[byte+1] );
}

#endif
