/*
 * Copyright (C) 2015-2021  Marco Bortolin
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

#include <string>
#include <vector>
#include <array>

#ifdef __GNUC__
#define bswap16 __builtin_bswap16
#define bswap32 __builtin_bswap32
#else
#error TODO define byteswap functions
#endif

inline uint32_t to_bigendian_32(uint32_t _hostval)
{
#ifdef WORDS_BIGENDIAN
	return _hostval;
#else
	return bswap32(_hostval);
#endif
}

inline uint16_t to_bigendian_16(uint16_t _hostval)
{
#ifdef WORDS_BIGENDIAN
	return _hostval;
#else
	return bswap16(_hostval);
#endif
}

/* Converts a int into a string, to be used in string concatenations */
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

template <typename _to_check, std::size_t _expected, std::size_t _real = sizeof(_to_check)>
void size_check()
{
	static_assert(_expected == _real, "Incorrect size!");
}

std::string str_implode(const std::vector<std::string> &_list, const std::string &_delim = ", ");
void str_replace_all(std::string &_str, const std::string &_search, const std::string &_replace);
std::string str_to_lower(std::string _str);
std::string str_to_upper(std::string _str);
std::string str_trim(std::string _str);
std::string str_compress_spaces(std::string _str);
std::vector<std::string> str_parse_tokens(std::string _str, std::string _regex_sep);

template<typename ... Args>
std::string str_format(const char *_format, Args ... _args)
{
	// TODO reconsider for C++20?
	// lol no, std::format is incompatible with printf format
	// I have better things to do than convert every single format string to std::format

	int size = std::snprintf(nullptr, 0, _format, _args ...);
	if(size < 0) {
		throw std::runtime_error("Error during string formatting.");
	}
	std::string buf;
	if(size) {
		buf.resize(size); // size in number of characters (null term excluded)
		snprintf(&buf[0], size+1, _format, _args ...); // size in char+null!
	}
	return buf;
}

template<typename ... Args>
std::string & str_format(std::string &_result, const char *_format, Args ... _args)
{
	int size = std::snprintf(nullptr, 0, _format, _args ...);
	if(size < 0) {
		throw std::runtime_error("Error during string formatting.");
	}
	_result.resize(size);
	if(size) {
		snprintf(_result.data(), _result.size()+1, _format, _args ...);
	}
	return _result;
}

template<typename ... Args>
std::string & str_format_sized(std::string &_result, const char *_format, Args ... _args)
{
	if(_result.size()) {
		snprintf(_result.data(), _result.size()+1, _format, _args ...);
	}
	return _result;
}

std::string bitfield_to_string(uint8_t _bitfield,
		const std::array<std::string, 8> &_set_names);
std::string bitfield_to_string(uint8_t _bitfield,
		const std::array<std::string, 8> &_set_names,
		const std::array<std::string, 8> &_clear_names);

const char* register_to_string(uint8_t _register,
	const std::vector<std::pair<int, std::string> > &_fields);

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

template<class T>
	T smoothstep(T _edge0, T _edge1, T _x)
{
	_x = clamp((_x - _edge0) / (_edge1 - _edge0), T(0.0), T(1.0)); 
	return _x * _x * (3 - 2 * _x);
}

template<class T>
	T smootherstep(T _edge0, T _edge1, T _x)
{
	_x = clamp((_x - _edge0) / (_edge1 - _edge0), T(0.0), T(1.0));
	return _x * _x * _x * (_x * (_x * 6 - 15) + 10);
}

#include <functional>
#include <chrono>
#include <future>
#include <thread>

inline uint64_t get_curtime_ms()
{
	return std::chrono::time_point_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now()
	).time_since_epoch().count();
}

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

// Converts a strongly typed enum class to integer
template <typename E>
constexpr auto ec_to_i(E e) noexcept
{
	return static_cast<std::underlying_type_t<E>>(e);
}

inline size_t round_to_dword(size_t _bits)
{
	return (((_bits + 31) & ~31) >> 3);
}

#endif
