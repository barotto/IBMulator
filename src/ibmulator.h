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

#define MULTITHREADED        true
#define MACHINE_HEARTBEAT    10000
#define GUI_HEARTBEAT        16666
#define MIXER_HEARTBEAT      10000
#define CHRONO_RDTSC         false
#define USE_PREFETCH_QUEUE   true
#define PIT_CNT1_AUTO_UPDATE false

/////////////
//CPU logging
#define CPULOG               false   // activate CPU logging?
#define CPULOG_MAX_SIZE      400000u // number of instruction to log
#define CPULOG_WRITE_TIME    false   // write instruction machine time?
#define CPULOG_WRITE_CSIP    true    // write instruction address as CS:IP?
#define CPULOG_WRITE_HEX     true    // write instruction as hex codes?
#define CPULOG_WRITE_CORE    false   // write the CPU registers?
#define CPULOG_WRITE_PQ      true    // write the prefetch queue?
#define CPULOG_WRITE_TIMINGS true    // write various timing values?
#define CPULOG_START_ADDR    0x500   // lower bound, instr. before this address are not logged
#define CPULOG_END_ADDR      0x9FFFF // upper bound, instr. after this address are not logged
#define CPULOG_LOG_INTS      false   // log INTs' instructions?
#define CPULOG_INT21_EXIT_IP 0x7782  // the IP of the last instr. of INT 21/4B
                                     // OS dependent, for PC-DOS 4.0 is 0x7782
                                     // use -1 to disable

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

	#define CONFIG_PARSE  true
	#define MEMORY_TRAPS  false
	#define INT_TRAPS     true
	#define STOP_AT_EXC   false
	#define UD6_AUTO_DUMP false
	#define HDD_TIMING    false //used to speed up the HDD and ease the debugging

	#define DEFAULT_LOG_VERBOSITY LOG_V1

#else
	//RELEASE
	#define ASSERT(T)
	#define RASSERT(T) assert(T)

	#define CONFIG_PARSE  true
	#define MEMORY_TRAPS  false
	#define INT_TRAPS     false
	#define STOP_AT_EXC   false
	#define UD6_AUTO_DUMP false
	#define HDD_TIMING    true

	#define DEFAULT_LOG_VERBOSITY LOG_V0

#endif

#include "syslog.h"




#endif
