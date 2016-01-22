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
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iconv.h>
#include "machine.h"
#include "hardware/cpu/core.h"

Syslog g_syslog;

const char* Syslog::m_pri_prefixes[LOG_VERBOSITY_MAX][LOG_PRIMAX] = {
	{ "[DBG0]", "[INF0]", "[WRN0]", "[ERR0]" },
	{ "[DBG1]", "[INF1]", "[WRN1]", "[ERR1]" },
	{ "[DBG2]", "[INF2]", "[WRN2]", "[ERR2]" }
};

const char* Syslog::m_fac_prefixes[] = {
	" prg | ",
	" fs  | ",
	" gfx | ",
	" inp | ",
	" gui | ",
	" mch | ",
	" mix | ",
	" mem | ",
	" cpu | ",
	" pit | ",
	" pic | ",
	" dma | ",
	" kbd | ",
	" vga | ",
	" cmos| ",
	" flp | ",
	" hdd | ",
	" aud | ",
	" lpt | ",
	" com | "
};


/** Registra il dispositivo Logstream(cerr) in ogni coda di priorità e facility.
*/
Syslog::Syslog()
:
m_default(new LogStream(cerr)),
m_repeat_cnt(0)
{
	for(int pri=0; pri<LOG_PRIMAX; pri++) {
		for(int fac=0; fac<LOG_FACMAX; fac++) {
			add_device(pri,fac,m_default);
		}
	}

	memset(m_linefeed, 1, LOG_PRIMAX*LOG_FACMAX);
	memset(m_verbosity, 0, sizeof(m_verbosity));
}


/** Distrugge tutti i dispositivi per cui è responsabile.
*/
Syslog::~Syslog()
{
	auto it = m_devices.begin();
	while(it != m_devices.end()) {
		if((*it)->syslog_dispose())
			delete *it;
		it++;
	}
}


/** Aggiunge un device alla coda di priorità e facility.
@param _priority priorità (vedi _Syslog_Priorities)
@param _facility facility (vedi _Syslog_Facilities)
@param _device log device
*/
void Syslog::add_device(int _priority, int _facility, Logdev* _device)
{
	assert(_device);

	if(_priority==LOG_PRIMAX) {
		if(_facility==LOG_FACMAX) {
			for(int pri=0; pri<LOG_PRIMAX; pri++) {
				for(int fac=0; fac<LOG_FACMAX; fac++) {
					add_device(pri,fac,_device);
				}
			}
		} else {
			for(int pri=0; pri<LOG_PRIMAX; pri++) {
				add_device(pri,_facility,_device);
			}
		}
		return;
	}
	if(_facility==LOG_FACMAX) {
		for(int fac=0; fac<LOG_FACMAX; fac++) {
			add_device(_priority,fac,_device);
		}
		return;
	}

	list<Logdev*>& devlist = m_mapped_devices[_priority][_facility];

	// se il dispositivo è già presente in questa lista pri x fac esci
	if(find(_device->m_log_refs.begin(),_device->m_log_refs.end(),&devlist) != _device->m_log_refs.end())
		return;

	// aggiungi il dispositivo alla lista pri x fac
	devlist.push_back(_device);

	// aggiungi la lista ai riferimenti del dispositivo.
	_device->m_log_refs.push_back(&devlist);

	// e alla lista globale dei dispositivi (a meno che non sia già presente)
	if(find(m_devices.begin(),m_devices.end(),_device) == m_devices.end()) {
		m_devices.push_back(_device);
	}
}


/** Rimuove un device da una coda di priorità e facility
@param _priority priorità (vedi _Syslog_Priorities)
@param _facility facility (vedi _Syslog_Facilities)
@param _device dispositivo
*/
void Syslog::del_device(int _priority, int _facility, Logdev* _device)
{
	assert(_device);

	list<Logdev*>& devlist = m_mapped_devices[_priority][_facility];

	list<Logdev*>::iterator dev = find(devlist.begin(), devlist.end(), _device);

	// se il dispositivo non è presente in questa lista pri x fac esci
	if(dev == devlist.end()) return;

	// elimina la lista dai riferimenti del dispositivo.
	_device->m_log_refs.remove(&devlist);

	// elimina il dipositivo dalla lista.
	devlist.erase(dev);

	// non eliminare il device dalla lista dei device, è compito di remove().
}


/** Cancella la coda di device alle coordinate _priority x _facility */
void Syslog::clear_queue(int _priority, int _facility)
{
	m_mapped_devices[_priority][_facility].clear();
}


/** Scrive una stringa sui dispositivi registrati per la priorità e facility in ingresso.
@param _priority priorità
@param _facility facility
@param _verbosity livello entro il quale stampare il messaggio
@param _format stringa
*/
bool Syslog::log(int _priority, int _facility, int _verbosity, const char* _format, ...)
{
	va_list ap;

	if(_verbosity > int(m_verbosity[_facility])) {
		return false;
	}

	va_start(ap, _format);
	bool res = log(_priority,_facility,_verbosity,_format,ap);
	va_end(ap);
	return res;
}


/** Scrive una stringa sui dispositivi registrati per la priorità e facility in ingresso.
@param _priority priorità
@param _facility facility
@param _verbosity livello entro il quale stampare il messaggio
@param _format stringa
@param _va lista variabile di parametri.

Chiamata da log(int, int, int, const char*, ...).
*/
bool Syslog::log(int _priority, int _facility, int _verbosity, const char* _format, va_list _va)
{
	assert(_format);

	if(_verbosity > int(m_verbosity[_facility])) {
		return false;
	}

	if(_format[0] == 0) return false;

	list<Logdev*>& devlist = m_mapped_devices[_priority][_facility];
	if(devlist.empty()) return false;

	std::lock_guard<std::mutex> lock(m_lock);

	if(_format[0] == '\n' && _format[1] == 0) {
		put_all(devlist,"","\n");
		m_linefeed[_priority][_facility] = 1;
		return true;
	}

	int len = vsnprintf(m_buf, LOG_BUFFER_SIZE, _format, _va);
	if(len > LOG_BUFFER_SIZE) {
		m_buf[LOG_BUFFER_SIZE-2] = '\n';
		len = LOG_BUFFER_SIZE-1;
	}
	string prefix, tocompare;
	if(m_linefeed[_priority][_facility]) {
		//this 'if' should be based on the output device, not the pri-fac combo!
		std::stringstream temp;
		if(LOG_MACHINE_TIME) {
			temp << setfill('0') << setw(10) << g_machine.get_virt_time_us_mt() << " ";
		}
		if(LOG_CSIP) {
			temp << std::hex << std::uppercase << internal << setfill('0');
			temp << setw(4) << REG_CS.sel.value << ":" << setw(4) << REG_IP << " ";
			temp << setw(2) << (uint)(g_machine.get_POST_code()) << " ";
		}
		prefix += temp.str();
		prefix += m_pri_prefixes[_verbosity][_priority];
		prefix += m_fac_prefixes[_facility];
		tocompare += m_pri_prefixes[_verbosity][_priority];
		tocompare += m_fac_prefixes[_facility];
	}
	tocompare += m_buf;

	if(m_linefeed[_priority][_facility] && tocompare.compare(m_repeat_str) == 0) {
		m_linefeed[_priority][_facility] = 1;
		m_repeat_cnt++;
	} else {
		if(m_repeat_cnt>0) {
			std::stringstream ss;
			ss << "last message repeated " << m_repeat_cnt << " more times" << std::endl;
			put_all(devlist, "", ss.str().c_str());
		}
		m_repeat_cnt = 0;
		m_repeat_str = tocompare;
		m_linefeed[_priority][_facility] = (int)(m_buf[len-1] == '\n');
		put_all(devlist, prefix.c_str(), m_buf);
	}
	return true;
}


/** Rimuove un device dal sistema di log.
@param _device dispositivo da rimuovere da ogni lista di priorità e facility del sistema.
@param _erase se true allora il device viene "dimenticato" dal syslog e la sua
cancellazione fisica è a carico del chiamante; se false allora il syslog resta
il responsabile della sua gestione (nel caso in cui sia vero anche _device->syslog_dispose()).
*/
void Syslog::remove(Logdev* _device, bool _erase)
{
	assert(_device);

	LOGREFSLIST::iterator refit = _device->m_log_refs.begin();
	for(refit = _device->m_log_refs.begin(); refit != _device->m_log_refs.end(); refit++) {
		list<Logdev*>::iterator devit = find((*refit)->begin(), (*refit)->end(), _device);
		(*refit)->erase(devit);
	}

	if(_erase) {
		list<Logdev*>::iterator dev = find(m_devices.begin(),m_devices.end(),_device);
		if(dev == m_devices.end())
			return;
		m_devices.erase(dev);
	}
}


/** Scrive una stringa su tutti i device di una lista.
@param _devlist lista di dispositivi.
@param _prefix the prefix to the message
@param _mex the message to print
*/
void Syslog::put_all(list<Logdev*>& _devlist, const char* _prefix, const char* _mex)
{
	for(list<Logdev*>::iterator dit=_devlist.begin(); dit!=_devlist.end(); dit++) {
		(*dit)->log_put(_prefix, _mex);
		(*dit)->log_flush();
	}
}


/** Imposta il livello di verbosità
@param[in] _level livello tra uno di enum _Syslog_Verbosity
*/
void Syslog::set_verbosity(uint _level, uint _facility)
{
	if(_facility >= LOG_FACMAX) {
		//sets the same verbosity for all facilities
		for(uint i=0; i<LOG_FACMAX; i++) {
			m_verbosity[i] = _level;
		}
		return;
	}
	m_verbosity[_facility] = _level;
}


const char* Syslog::convert(const char *from_charset, const char *to_charset,
		char *instr, size_t inlen)
{
	std::lock_guard<std::mutex> lock(m_lock);

	size_t inleft = inlen;
	size_t outleft = LOG_BUFFER_SIZE;
	char * pIn = instr;
	char * pOut = m_iconvbuf;

	memset(m_iconvbuf, 0, LOG_BUFFER_SIZE);

	iconv_t cd = iconv_open(to_charset, from_charset);
	if(cd == iconv_t(-1)) {
		return "?";
	}

	size_t rc = iconv(cd, &pIn, &inleft, &pOut, &outleft);
	if(rc == size_t(-1)) {
		iconv_close(cd);
		return "?";
	}
	iconv_close(cd);
	return m_iconvbuf;
}


//------------------------------------------------------------------------------



Logdev::Logdev(bool _syslog_dispose)
:
m_log_dispose(_syslog_dispose)
{
}


Logdev::~Logdev()
{
	if(!m_log_dispose) {
		g_syslog.remove(this,true);
	}
}


bool Logdev::syslog_dispose()
{
	return m_log_dispose;
}


void Logdev::log_add(int _pri, int _fac)
{
	g_syslog.add_device(_pri, _fac, this);
}


void Logdev::log_del(int _pri, int _fac)
{
	g_syslog.del_device(_pri, _fac, this);
}


void Logdev::log_remove()
{
	g_syslog.remove(this,!m_log_dispose);
}



//------------------------------------------------------------------------------



LogStream::LogStream(ostream& _stream, bool _disp)
:
Logdev(_disp),
m_stream(&_stream)
{
}


LogStream::LogStream(const char* _fn, ios_base::openmode _mode, bool _disp)
:
Logdev(_disp),
m_stream(nullptr),
m_file(_fn,_mode)
{
}


LogStream::~LogStream()
{
	if(m_file.is_open())
		m_file.close();
}


void LogStream::log_put(const char* _prefix, const char* _message)
{
	if(m_file.is_open()) {
		m_file << _prefix << _message;
	} else {
		*m_stream << _prefix << _message;
	}
}


void LogStream::log_flush()
{
	if(m_file.is_open())
		m_file.flush();
	else
		m_stream->flush();
}

