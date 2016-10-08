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

#ifndef IBMULATOR_SYSLOG_H
#define IBMULATOR_SYSLOG_H

#include <cstdarg>
#include <iostream>
#include <fstream>
#include <map>
#include <list>
#include <thread>
#include <atomic>
#include <mutex>
#include "shared_deque.h"

class Syslog;
extern Syslog g_syslog;

#define LOG_BUFFER_SIZE 500

#define LOG g_syslog.log

#define PINFO(verb,format,...)	LOG(LOG_INFO,LOG_PROGRAM,verb,format, ## __VA_ARGS__)
#define PWARN(format,...)		LOG(LOG_WARNING,LOG_PROGRAM,LOG_V1,format, ## __VA_ARGS__)
#define PERR(format,...)		LOG(LOG_ERROR,LOG_PROGRAM,LOG_V0,format, ## __VA_ARGS__)

#define PINFOF(verb,fac,format,...)	LOG(LOG_INFO,fac,verb,format, ## __VA_ARGS__)
#define PWARNF(fac,format,...)		LOG(LOG_WARNING,fac,LOG_V1,format, ## __VA_ARGS__)
#define PERRF(fac,format,...)		LOG(LOG_ERROR,fac,LOG_V0,format, ## __VA_ARGS__)

#define PERRFEX(fac,format,...) { LOG(LOG_ERROR,fac,LOG_V0,"%s:%d " format, __FILE__,__LINE__, ## __VA_ARGS__) }

#define PERR_ABORT(format,...) 		{ LOG(LOG_ERROR,LOG_PROGRAM,LOG_V0,format, ## __VA_ARGS__) ; exit(1); }
#define PERRF_ABORT(fac,format,...) { LOG(LOG_ERROR,fac,LOG_V0,format, ## __VA_ARGS__) ; exit(1); }
#define PERRFEX_ABORT(fac,format,...) { LOG(LOG_ERROR,fac,LOG_V0,"%s:%d " format, __FILE__,__LINE__, ## __VA_ARGS__) ; exit(1); }

#define ICONV(from,to,str,len) g_syslog.convert(from,to,str,len)

#if LOG_DEBUG_MESSAGES
	//#define PDEBUG(format,...)	LOG_FUNC(__FILE__,__LINE__,LOG_DEBUG,LOG_GENERAL,format, ## __VA_ARGS__)
	#define PDEBUGEX(verb,format,...)	LOG(LOG_DEBUG,LOG_PROGRAM,verb,"%s:%d " format, __FILE__,__LINE__, ## __VA_ARGS__)
	#define PDEBUG(verb,format,...)		LOG(LOG_DEBUG,LOG_PROGRAM,verb,format, ## __VA_ARGS__)
	#define PDEBUGEXF(verb,fac,format,...)	LOG(LOG_DEBUG,fac,verb,"%s:%d " format, __FILE__,__LINE__, ## __VA_ARGS__)
	#define PDEBUGF(verb,fac,format,...)	LOG(LOG_DEBUG,fac,verb,format, ## __VA_ARGS__)
#else
	#define PDEBUGEX(verb,format,...)
	#define PDEBUG(verb,format,...)
	#define PDEBUGEXF(verb,fac,format,...)
	#define PDEBUGF(verb,fac,format,...)
#endif

enum e_Syslog_Facilities {
	LOG_PROGRAM,
	LOG_FS,
	LOG_GFX,
	LOG_INPUT,
	LOG_GUI,
	LOG_MACHINE,
	LOG_MIXER,
	LOG_MEM,
	LOG_CPU,
	LOG_PIT,
	LOG_PIC,
	LOG_DMA,
	LOG_KEYB,
	LOG_VGA,
	LOG_CMOS,
	LOG_FDC,
	LOG_HDD,
	LOG_AUDIO,
	LOG_LPT,
	LOG_COM,

	LOG_FACMAX
};

#define LOG_ALL_FACILITIES LOG_FACMAX

enum e_Syslog_Priorities {
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARNING,
	LOG_ERROR,

	LOG_PRIMAX
};

#define LOG_ALL_PRIORITIES LOG_PRIMAX

enum _Syslog_Verbosity {
	LOG_VERBOSITY_0 = 0, // informazioni di base, errori critici, eccezioni, ...
	LOG_VERBOSITY_1,     // informazioni estese
	LOG_VERBOSITY_2,     // flusso dettagliato delle operazioni svolte (eplosivo)

	LOG_VERBOSITY_MAX
};

#define LOG_V0 LOG_VERBOSITY_0
#define LOG_V1 LOG_VERBOSITY_1
#define LOG_V2 LOG_VERBOSITY_2

class Logdev;


class Syslog
{
private:
	int m_id_max;
	Logdev* m_default;

	std::list<Logdev*> m_devices;
	std::list<Logdev*> m_mapped_devices[LOG_PRIMAX][LOG_FACMAX];
	uint8_t m_linefeed[LOG_PRIMAX][LOG_FACMAX];

	static const char* m_pri_prefixes[LOG_VERBOSITY_MAX][LOG_PRIMAX];
	static const char* m_fac_prefixes[LOG_FACMAX];
	char m_buf[LOG_BUFFER_SIZE];
	char m_iconvbuf[LOG_BUFFER_SIZE];

	uint m_verbosity[LOG_FACMAX];

	std::string m_repeat_str;
	uint m_repeat_cnt;

	/* Multi-threading extension to resolve the blocking behaviour of I/O stream
	 * writing.
	 */
	std::thread m_thread;
	bool m_stop;
	std::atomic<bool> m_paused;
	std::mutex m_log_mutex;
	std::mutex m_pause_mutex;
	std::condition_variable m_pause_cond;
	shared_deque<std::function<void()>> m_cmd_queue;

public:
	Syslog();
	~Syslog();

	void add_device(int _priority, int _facility, Logdev* _device);
	void del_device(int _priority, int _facility, Logdev* _device);
	void clear_queue(int _priority, int _facility);
	bool log(int _priority, int _facility, int _verbosity, const char* _format, ...);
	void remove(Logdev* _dev, bool _erase = false);
	void set_verbosity(uint _level, uint facility = LOG_FACMAX);

	void cmd_pause_and_signal(std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_resume();

private:
	bool p_log(int _priority, int _facility, int _verbosity, const char* _format, va_list _va);
	void put_all(std::list<Logdev*>& _devlist, std::string _prefix, std::string _mex);
	void main_loop();
};



//------------------------------------------------------------------------------



typedef std::list< std::list<Logdev*>* > LOGREFSLIST;

class Logdev
{
	friend class Syslog;

private:
	bool m_log_dispose;
	LOGREFSLIST m_log_refs;

protected:

	Logdev(bool _syslog_dispose = true);
	bool syslog_dispose();

public:
	virtual ~Logdev();
	virtual	void log_put(const std::string &_prefix, const std::string &_message) = 0;
	virtual void log_flush() {}

	void log_add(int _pri, int _fac);
	void log_del(int _pri, int _fac);
	void log_remove();
};



//------------------------------------------------------------------------------



class LogStream : public Logdev
{
protected:
	std::ostream* m_stream;
	std::ofstream m_file;

public:
	LogStream(const char* _path, std::ios_base::openmode _mode=std::ios_base::out, bool _syslog_dispose = true);
	LogStream(std::ostream& _stream, bool _syslog_dispose = true);
	~LogStream();

	void log_put(const std::string &_prefix, const std::string &_message);
	void log_flush();
};




#endif
