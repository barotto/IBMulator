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

#ifndef IBMULATOR_H
#define IBMULATOR_H

#define MULTITHREADED 1
#define MACHINE_HEARTBEAT 10000
#define GUI_HEARTBEAT 16666
#define MIXER_HEARTBEAT 10000
#define CPULOG 0
#define CPULOG_MAX_SIZE 200000u
#define CHRONO_RDTSC 0

#define PATHNAME_LEN 512

#define N_SERIAL_PORTS 1 // DONT'T TOUCH IT! IT MUST BE 1
#define N_PARALLEL_PORTS 1
#define USE_RAW_SERIAL 0
#define TRUE_CTLC 0

#include "config.h"
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <sys/types.h>
#include <cstdio>
#include <stdexcept>
#include <cassert>

#if !defined(_MSC_VER)
	#define GCC_ATTRIBUTE(x) __attribute__ ((x))
#else
	#define GCC_ATTRIBUTE(x)
#endif

#if defined(_MSC_VER) && (_MSC_VER>=1300)
	#define MSC_ALIGN(x) __declspec(align(x))
#else
	#define MSC_ALIGN(x)
#endif

template <typename _to_check, std::size_t _expected, std::size_t _real = sizeof(_to_check)>
void size_check()
{
	static_assert(_expected == _real, "Incorrect size!");
}


#ifndef NDEBUG
	//DEBUG
	#define ASSERT(T) assert(T)
	#define RASSERT(T) assert(T)

	#define CONFIG_PARSE 1
	#define MEMORY_TRAPS 0
	#define INT_TRAPS 0
	#define STOP_AT_EXC 0
	#define UD6_AUTO_DUMP 0

	#define DEFAULT_LOG_VERBOSITY LOG_V1

#else
	//RELEASE
	#define ASSERT(T)
	#define RASSERT(T) assert(T)

	#define CONFIG_PARSE 1
	#define MEMORY_TRAPS 0
	#define INT_TRAPS 0
	#define STOP_AT_EXC 0
	#define UD6_AUTO_DUMP 0

	#define DEFAULT_LOG_VERBOSITY LOG_V0

#endif

#include "syslog.h"




#endif
