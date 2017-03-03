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
#include "program.h"
#include "machine.h"
#include "statebuf.h"
#include "hardware/cpu.h"
#include "hardware/cpu/debugger.h"
#include "hardware/memory.h"
#include "hardware/devices.h"
#include "hardware/devices/keyboard.h"
#include "hardware/devices/floppy.h"
#include "hardware/devices/dma.h"
#include "hardware/devices/systemboard.h"

#include "gui/gui.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <unistd.h>
#include <thread>

Machine g_machine;
std::mutex Machine::ms_gui_lock;

#define US_TO_NS(us) (us*1000)

Machine::Machine()
:
m_heartbeat(MACHINE_HEARTBEAT),
m_quit(false),
m_on(false),
m_cpu_single_step(false),
m_breakpoint_cs(0),
m_breakpoint_eip(0),
m_mouse_fun(nullptr)
{
	memset(&m_s, 0, sizeof(m_s));
}

Machine::~Machine()
{

}

#define MACHINE_STATE_NAME "Machine state"
#define MACHINE_TIMERS_NAME "Machine timers"

void Machine::save_state(StateBuf &_state)
{
	/*
	 * This method should be called by Program only via cmd_save_state()
	 */

	StateHeader h;

	h.name = MACHINE_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.write(&m_s, h);

	h.name = MACHINE_TIMERS_NAME;
	h.data_size = sizeof(Timer)*MAX_TIMERS;
	_state.write(m_timers, h);

	g_cpu.save_state(_state);
	g_memory.save_state(_state);
	g_devices.save_state(_state);

	PINFOF(LOG_V0, LOG_MACHINE, "Machine state saved\n");
}

void Machine::restore_state(StateBuf &_state)
{
	/*
	 * This method should be called by Program only via cmd_restore_state()
	 */

	StateHeader h;

	//MACHINE state
	h.name = MACHINE_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.read(&m_s, h);
	m_mt_virt_time.store(m_s.virt_time);

	//timers
	h.name = MACHINE_TIMERS_NAME;
	h.data_size = sizeof(Timer) * MAX_TIMERS;
	_state.check(h);

	//for every timer in the savestate
	for(uint t=0; t<MAX_TIMERS; t++) {
		Timer *savtimer = (Timer*)_state.get_buf();
		if(savtimer->in_use) {
			uint mchtidx;
			//find the correct machine timer, which must be already registered
			for(mchtidx=0; mchtidx<MAX_TIMERS; mchtidx++) {
				if(strcmp(m_timers[mchtidx].name, savtimer->name) == 0) {
					break;
				}
			}
			if(mchtidx>=MAX_TIMERS) {
				PERRF(LOG_MACHINE, "cant find timer %s\n", savtimer->name);
				throw std::exception();
			}
			if(!m_timers[mchtidx].in_use) {
				PERRF(LOG_MACHINE, "timer %s is not in use\n", m_timers[mchtidx].name);
				throw std::exception();
			}
			m_timers[mchtidx].period = savtimer->period;
			m_timers[mchtidx].time_to_fire = savtimer->time_to_fire;
			m_timers[mchtidx].active = savtimer->active;
			m_timers[mchtidx].continuous = savtimer->continuous;
		}
		_state.advance(sizeof(Timer));
	}

	//exceptions will be thrown if the buffer size is smaller than expected
	try {
		//CPU state
		g_cpu.restore_state(_state);
	} catch(std::exception &e) {
		PERRF(LOG_MACHINE, "error restoring cpu\n");
		throw;
	}

	try {
		//MEMORY state
		g_memory.restore_state(_state);
	} catch(std::exception &e) {
		PERRF(LOG_MACHINE, "error restoring memory\n");
		throw;
	}

	try {
		//DEVICES state
		g_devices.restore_state(_state);
	} catch(std::exception &e) {
		PERRF(LOG_MACHINE, "error restoring devices\n");
		throw;
	}

	if(_state.get_bytesleft() != 0) {
		PERRF(LOG_MACHINE, "state buffer size mismatch\n");
		throw std::exception();
	}

	std::unique_lock<std::mutex> lock(ms_gui_lock);
	m_curr_prgname_changed = true;

	PINFOF(LOG_V0, LOG_MACHINE, "Machine state restored\n");
}

void Machine::calibrate(const Chrono &_c)
{
	m_main_chrono.calibrate(_c);
}

void Machine::init()
{
	/* the time keeping in IBMulator is equivalent to that of Bochs slowdown:
	 * the emulator is deterministic, and the clock/s are kept in sync with real time
	 * by slowing emulation down when virtual time gets ahead of real time.
	 */

	m_main_chrono.start();
	m_bench.init(&m_main_chrono, 1000);
	m_s.curr_prgname[0] = 0;
	m_num_timers = 0;

	g_cpu.init();
	g_cpu.set_shutdown_trap([this] () {
		reset(CPU_SOFT_RESET);
	});
	g_memory.init();
	g_devices.init(this);
}

void Machine::start()
{
	m_quit = false;
	PDEBUGF(LOG_V2, LOG_MACHINE, "Machine thread started\n");
	main_loop();
}

void Machine::reset(uint _signal)
{
	m_on = true;
	g_cpu.reset(_signal);
	switch(_signal) {
		case CPU_SOFT_RESET:
			PDEBUGF(LOG_V2, LOG_MACHINE, "CPU software reset\n");
			g_memory.set_A20_line(true);
			return;
		case MACHINE_HARD_RESET:
			PINFOF(LOG_V1, LOG_MACHINE, "Machine hardware reset\n");
			break;
		case MACHINE_POWER_ON:
			PINFOF(LOG_V0, LOG_MACHINE, "Machine power on\n");
			break;
		default:
			PERRF(LOG_MACHINE, "unknown reset signal: %d\n", _signal);
			throw std::exception();
	}
	if(_signal == MACHINE_POWER_ON || _signal == MACHINE_HARD_RESET) {
		m_s.virt_time       = 0;
		m_s.next_timer_time = 0;
		m_mt_virt_time      = 0;
		for(unsigned i = 0; i < m_num_timers; i++) {
			if(m_timers[i].in_use && m_timers[i].active && m_timers[i].continuous) {
				m_timers[i].time_to_fire = m_timers[i].period;
			}
		}
	}
	m_s.cycles_left = 0;
	g_memory.reset();
	g_devices.reset(_signal);
	set_DOS_program_name("");
}

void Machine::power_off()
{
	if(!m_on) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "The machine power is already off\n");
		return;
	}
	PINFOF(LOG_V0, LOG_MACHINE, "Machine power off\n");
	m_on = false;
	g_cpu.power_off();
	g_devices.power_off();

	set_DOS_program_name("");
}

void Machine::config_changed()
{
	PINFOF(LOG_V1, LOG_MACHINE, "Loading the SYSTEM ROM\n");
	try {
		std::string romset = g_program.config().find_file(SYSTEM_SECTION, SYSTEM_ROMSET);
		m_sysrom.load(romset);
	} catch(std::exception &e) {
		PERRF(LOG_MACHINE, "unable to load the SYSTEM ROM!\n");
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialisation error",
				"Unable to load the SYSTEM ROM.\nUpdate " PACKAGE ".ini with the correct path.",
		        nullptr);
		throw;
	}

	try {
		m_model = g_program.config().get_enum_quiet(SYSTEM_SECTION, SYSTEM_MODEL, g_ini_model_names);
	} catch(std::exception &) {
		m_model = m_sysrom.bios().machine_model;
	}

	PINFOF(LOG_V0, LOG_MACHINE, "Selected model: %s\n", model().print().c_str());

	g_cpu.config_changed();
	g_memory.config_changed();
	g_devices.config_changed();

	m_cpu_cycles = g_cpu.frequency() * m_heartbeat;
	m_cycles_factor = 1.0;

	PINFOF(LOG_V1, LOG_MACHINE, "Machine beat period: %u usec\n", m_heartbeat);
	PINFOF(LOG_V1, LOG_MACHINE, "CPU cycles per beat: %.3f\n", m_cpu_cycles);

	PDEBUGF(LOG_V1, LOG_MACHINE, "Registered timers: %u\n", m_num_timers);
	for(unsigned i=0; i<m_num_timers; i++) {
		PDEBUGF(LOG_V1, LOG_MACHINE, "   %u: %s\n", i, m_timers[i].name);
	}
	PINFOF(LOG_V1, LOG_MACHINE, "IRQ channels:\n");
	for(unsigned i=0; i<16; i++) {
		PINFOF(LOG_V1, LOG_MACHINE, "   %u: %s\n", i, m_irq_names[i].c_str());
	}
	PINFOF(LOG_V1, LOG_MACHINE, "DMA channels:\n");
	for(unsigned i=0; i<8; i++) {
		PINFOF(LOG_V1, LOG_MACHINE, "   %u: %s\n", i, g_devices.dma()->get_device_name(i).c_str());
	}
}

bool Machine::main_loop()
{
	static double cycles_rem = 0.0;
	static int64_t next_beat_diff = 0L;

	while(true) {
		uint64_t time = m_main_chrono.elapsed_usec();
		if(time < m_heartbeat) {
			uint64_t sleep = m_heartbeat - time;
			uint64_t t0 = m_main_chrono.get_usec();
			std::this_thread::sleep_for( std::chrono::microseconds(sleep + next_beat_diff) );
			m_main_chrono.start();
			uint64_t t1 = m_main_chrono.get_usec();
			next_beat_diff = (sleep + next_beat_diff) - (t1 - t0);
		} else {
			m_main_chrono.start();
		}

		m_bench.beat_start();

		Machine_fun_t fn;
		while(m_cmd_fifo.pop(fn)) {
			fn();
		}

		if(m_quit) {
			return false;
		}
		if(m_on) {
			if(!m_cpu_single_step) {
				double needed_cycles = m_cpu_cycles * m_cycles_factor;
				int32_t cycles = round(needed_cycles + cycles_rem);
				cycles_rem += needed_cycles - cycles;
				core_step(cycles);
				m_bench.cpu_cycles(cycles);
			}
		}
		m_bench.beat_end();
	}

	return true;
}

void Machine::core_step(int32_t _cpu_cycles)
{
	int32_t cycles_left = _cpu_cycles;
	if(_cpu_cycles>0) {
		cycles_left = _cpu_cycles + m_s.cycles_left;
	} else {
		cycles_left = 1;
	}
	uint32_t cycle_time = g_cpu.cycle_time_ns();
	while(cycles_left>0) {

		int32_t c = g_cpu.step();
		if(c>0) {
			//c is 0 only if (REP && CX==0)
			m_bench.cpu_step();

			uint32_t elapsed_ns = c * cycle_time;
			uint64_t cpu_time = m_s.virt_time + elapsed_ns;

			if(cpu_time >= m_s.next_timer_time) {
				while(!update_timers(cpu_time));
			}

			cycles_left -= c;
			m_s.virt_time = cpu_time;
			m_mt_virt_time.store(cpu_time);
		}

		if(m_breakpoint_cs > 0) {
			if(m_breakpoint_cs == REG_CS.sel.value && m_breakpoint_eip == REG_EIP) {
				pause();
				m_breakpoint_clbk();
				m_breakpoint_cs = 0;
			}
		}
		if(m_cpu_single_step && cycles_left>0) {
			_cpu_cycles -= cycles_left;
			cycles_left = 0;
		}
	}
	m_s.cycles_left = cycles_left;
}

void Machine::pause()
{
	set_single_step(true);
	g_mixer.cmd_pause();
}

void Machine::resume()
{
	set_single_step(false);
	g_mixer.cmd_resume();
}

bool Machine::update_timers(uint64_t _cpu_time)
{
	// We need to service all the active timers, and invoke callbacks
	// from those timers which have fired.
	m_s.next_timer_time = (uint64_t) -1;
	std::multimap<uint64_t, ushort> triggered;
	for(uint i = 0; i < m_num_timers; i++) {
		//triggered[i] = false; // Reset triggered flag.
		if(m_timers[i].active) {
			if(m_timers[i].time_to_fire <= _cpu_time) {

				//timers need to fire in an ordered manner
				triggered.insert(std::pair<uint64_t,ushort>(m_timers[i].time_to_fire,i));

			} else {

				// This timer is not ready to fire yet.
				if(m_timers[i].time_to_fire < m_s.next_timer_time) {
					m_s.next_timer_time = m_timers[i].time_to_fire;
				}

			}
		}
	}

	uint64_t prevtimer_time = 0;
	for(auto timer : triggered) {
		unsigned thistimer = timer.second;
		uint64_t thistimer_time = timer.first;
		uint64_t system_time = m_s.virt_time;
		assert(thistimer_time >= prevtimer_time);
		assert(thistimer_time <= _cpu_time);
		assert(thistimer_time >= system_time);

		// Call requested timer function.  It may request a different
		// timer period or deactivate etc.
		// it can even reactivate the same timer and set it to fire BEFORE the next vtime
		if(!m_timers[thistimer].continuous) {
			// If triggered timer is one-shot, deactive.
			m_timers[thistimer].active = false;
		} else {
			// Continuous timer, increment time-to-fire by period.
			m_timers[thistimer].time_to_fire += m_timers[thistimer].period;
			if(m_timers[thistimer].time_to_fire < m_s.next_timer_time) {
				m_s.next_timer_time = m_timers[thistimer].time_to_fire;
			}
		}
		if(m_timers[thistimer].fire != nullptr) {
			//the current time is when the timer fires
			//virt_time must advance in a monotonic way (that's why we use a map)
			m_s.virt_time = thistimer_time;
			m_mt_virt_time = thistimer_time;
			m_timers[thistimer].fire(m_s.virt_time);
			if(m_timers[thistimer].time_to_fire <= _cpu_time) {
				// the timer set itself to fire again before or at the time point
				// we need to reorder
				return false;
			}
		}
		prevtimer_time = thistimer_time;
	}
	return true;
}

void Machine::set_single_step(bool _val)
{
	m_cpu_single_step = _val;
}

int Machine::register_timer(timer_fun_t _func, const char *_name)
{
	unsigned timer = NULL_TIMER_HANDLE;

	if(m_num_timers >= MAX_TIMERS) {
    	PERRF(LOG_MACHINE, "register_timer: too many registered timers\n");
    	throw std::exception();
	}

	// search for new timer
	for(unsigned i = 0; i < m_num_timers; i++) {
		//check if there's another timer with the same name
		if(m_timers[i].in_use && strcmp(m_timers[i].name, _name)==0) {
			//cannot be 2 timers with the same name
			return NULL_TIMER_HANDLE;
		}
		if((!m_timers[i].in_use) && (timer==NULL_TIMER_HANDLE)) {
			//free timer found
			timer = i;
		}
	}
	if(timer == NULL_TIMER_HANDLE) {
		// If we didn't find a free slot, increment the bound m_num_timers.
		timer = m_num_timers;
		m_num_timers++;
	}
	m_timers[timer].in_use = true;
	m_timers[timer].period = 0;
	m_timers[timer].time_to_fire = 0;
	m_timers[timer].active = false;
	m_timers[timer].continuous = false;
	m_timers[timer].fire = _func;
	snprintf(m_timers[timer].name, TIMER_NAME_LEN, "%s", _name);

	PDEBUGF(LOG_V2,LOG_MACHINE,"timer id %d registered for '%s'\n", timer, _name);

	return timer;
}

void Machine::unregister_timer(int &_timer)
{
	if(_timer == NULL_TIMER_HANDLE) {
		return;
	}
	assert(_timer < MAX_TIMERS);
	m_timers[_timer].in_use = false;
	m_timers[_timer].active = false;
	m_timers[_timer].fire = nullptr;
	_timer = NULL_TIMER_HANDLE;
}

void Machine::activate_timer(unsigned _timer, uint64_t _nsecs, bool _continuous)
{
	assert(_timer<m_num_timers);

	// if _nsecs = 0, use default stored in period field
	if(_nsecs == 0) {
		_nsecs = m_timers[_timer].period;
	}

	m_timers[_timer].active = true;
	m_timers[_timer].period = _nsecs;
	m_timers[_timer].time_to_fire = m_s.virt_time + _nsecs;
	m_timers[_timer].continuous = _continuous;

	if(m_timers[_timer].time_to_fire < m_s.next_timer_time) {
		m_s.next_timer_time = m_timers[_timer].time_to_fire;
	}
}

void Machine::deactivate_timer(unsigned _timer)
{
	assert(_timer<m_num_timers);

	m_timers[_timer].active = false;
}

uint64_t Machine::get_timer_eta(unsigned _timer) const
{
	assert(_timer<m_num_timers);

	if(!m_timers[_timer].active) {
		return 0;
	}
	assert(m_timers[_timer].time_to_fire >= m_s.virt_time);
	return (m_timers[_timer].time_to_fire - m_s.virt_time);
}

void Machine::set_timer_callback(unsigned _timer, timer_fun_t _func)
{
	assert(_timer<m_num_timers);

	m_timers[_timer].fire = _func;
}

void Machine::register_irq(uint8_t _irq, const char* _name)
{
	assert(_irq<16);
	m_irq_names[_irq] = _name;
}

void Machine::unregister_irq(uint8_t _irq)
{
	assert(_irq<16);
	m_irq_names[_irq] = "";
}

const char* Machine::get_irq_name(uint8_t _irq)
{
	assert(_irq<16);
	return m_irq_names[_irq].c_str();
}

void Machine::memdump(uint32_t _base, uint32_t _len)
{
	try {
		std::stringstream ss;
		ss << g_program.config().get_cfg_home() + "/memdump-0x";
		uint32_t len = _len;
		uint32_t base = _base;
		if(len==0) {
			base = 0;
			len = g_memory.get_ram_size();
		}
		ss << std::hex << std::uppercase << std::internal << std::setfill('0');
		ss << std::setw(6) << base << "-" << std::setw(4) << len;
		ss << ".bin";
		g_memory.dump(ss.str(),base,len);
		PINFOF(LOG_V0, LOG_MACHINE, "memory content dumped in %s\n", ss.str().c_str());
	} catch(std::exception &e) {}
}

void Machine::cmd_quit()
{
	m_cmd_fifo.push([this] () {
		m_quit = true;
	});
}

void Machine::cmd_power_on()
{
	m_cmd_fifo.push([this] () {
		if(m_on) return;
		reset(MACHINE_POWER_ON);
		m_on = true;
	});
}

void Machine::cmd_power_off()
{
	m_cmd_fifo.push([this] () {
		power_off();
	});
}

void Machine::cmd_cpu_step()
{
	m_cmd_fifo.push([this] () {
		core_step(0);
	});
}

void Machine::cmd_cpu_breakpoint(uint16_t _cs, uint32_t _eip, std::function<void()> _callback)
{
	m_cmd_fifo.push([=] () {
		m_breakpoint_cs = _cs;
		m_breakpoint_eip = _eip;
		m_breakpoint_clbk = _callback;
	});
}

void Machine::cmd_soft_reset()
{
	m_cmd_fifo.push([this] () {
		reset(CPU_SOFT_RESET);
	});
}

void Machine::cmd_reset()
{
	m_cmd_fifo.push([this] () {
		reset(MACHINE_HARD_RESET);
	});
}

void Machine::cmd_switch_power()
{
	m_cmd_fifo.push([this] () {
		if(m_on) {
			power_off();
		} else {
			reset(MACHINE_POWER_ON);
		}
	});
}

void Machine::cmd_pause()
{
	m_cmd_fifo.push([this] () {
		if(!m_cpu_single_step) {
			pause();
			PINFOF(LOG_V0, LOG_MACHINE, "emulation paused\n");
		}
	});
}

void Machine::cmd_resume()
{
	m_cmd_fifo.push([this] () {
		if(m_cpu_single_step) {
			resume();
			PINFOF(LOG_V0, LOG_MACHINE, "emulation resumed\n");
		}
	});
}

void Machine::cmd_memdump(uint32_t _base, uint32_t _len)
{
	m_cmd_fifo.push([=] () {
		memdump(_base,_len);
	});
}

void Machine::cmd_dtdump(const std::string &_name)
{
	m_cmd_fifo.push([=] () {
		uint32_t base;
		uint16_t limit;
		if(_name == "GDT") {
			base = GET_BASE(GDTR);
			limit = GET_LIMIT(GDTR);
		} else if(_name == "LDT") {
			base = GET_BASE(LDTR);
			limit = GET_LIMIT(LDTR);
		} else if(_name == "IDT") {
			base = GET_BASE(IDTR);
			limit = GET_LIMIT(IDTR);
		} else {
			PERRF(LOG_MACHINE, "%s is not a valid descriptor table\n", _name.c_str());
			return;
		}
		if(limit == 0) {
			PWARNF(LOG_MACHINE, "%s is empty\n", _name.c_str());
			return;
		}
		std::stringstream filename;
		filename << g_program.config().get_cfg_home() + "/" + _name + "dump-0x";
		filename << std::hex << std::uppercase << std::internal << std::setfill('0');
		filename << std::setw(6) << base << "-" << std::setw(4) << limit;
		filename << ".csv";
		try {
			std::ofstream file(filename.str().c_str());
			if(file.is_open()) {
				file << CPUDebugger::descriptor_table_to_CSV(g_memory, base, limit).c_str();
				file.close();
				PINFOF(LOG_V0, LOG_MACHINE, "%s content dumped to %s\n", _name.c_str(),
					filename.str().c_str());
			}
		} catch(std::exception &e) {
			PERRF(LOG_MACHINE, "error dumping %s: %s\n", _name.c_str(), e.what());
		}
	});
}

void Machine::cmd_cpulog()
{
	m_cmd_fifo.push([this] () {
		g_cpu.write_log();
	});
}

void Machine::cmd_prg_cpulog(std::string _prg_name)
{
	m_cmd_fifo.push([=] () {
		g_cpu.enable_prg_log(_prg_name);
	});
}

void Machine::cmd_cycles_adjust(double _factor)
{
	m_cmd_fifo.push([=] () {
		m_cycles_factor = _factor;
		std::stringstream ss;
		ss << "emulation speed at " << std::setprecision(3) << (_factor*100.f) << "%";
		PINFOF(LOG_V0, LOG_MACHINE, "%s\n", ss.str().c_str());
		g_gui.show_message(ss.str().c_str());
	});
}

void Machine::cmd_save_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_fifo.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		save_state(_state);
		_cv.notify_one();
	});
}

void Machine::cmd_restore_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_fifo.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		_state.m_last_restore = true;
		try {
			restore_state(_state);
			m_on = true;
		} catch(std::exception &e) {
			PERRF(LOG_MACHINE, "error restoring the state\n");
			_state.m_last_restore = false;
		}
		_cv.notify_one();
	});
}

void Machine::cmd_insert_media(uint _drive, uint _type, std::string _file, bool _wp)
{
	m_cmd_fifo.push([=] () {
		g_devices.device<FloppyCtrl>()->insert_media(_drive, _type, _file.c_str(), _wp);
	});
}

void Machine::cmd_eject_media(uint _drive)
{
	m_cmd_fifo.push([=] () {
		g_devices.device<FloppyCtrl>()->eject_media(_drive);
	});
}

void Machine::sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_fifo.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		if(m_on) {
			power_off();
		}
		config_changed();
		_cv.notify_one();
	});
}

void Machine::send_key_to_kbctrl(uint32_t _key)
{
	g_devices.device<Keyboard>()->gen_scancode(_key);
}

void Machine::register_mouse_fun(mouse_fun_t _mouse_fun)
{
	m_mouse_fun = _mouse_fun;
}

void Machine::mouse_motion(int _delta_x, int _delta_y, int _delta_z, uint _button_state)
{
	if(m_mouse_fun) {
		m_mouse_fun(_delta_x, _delta_y, _delta_z, _button_state);
	}
}

void Machine::register_joystick_fun(joystick_mfun_t _motion_fun, joystick_bfun_t _button_fun)
{
	m_joystick_mfun = _motion_fun;
	m_joystick_bfun = _button_fun;
}

void Machine::joystick_motion(int _jid, int _axis, int _value)
{
	if(m_joystick_mfun) {
		m_joystick_mfun(_jid, _axis, _value);
	}
}

void Machine::joystick_button(int _jid, int _button, int _state)
{
	if(m_joystick_bfun) {
		m_joystick_bfun(_jid, _button, _state);
	}
}

uint8_t Machine::get_POST_code()
{
	SystemBoard * sb = g_devices.sysboard();
	if(sb != nullptr) {
		return sb->get_POST_code();
	}
	return 0;
}

void Machine::set_DOS_program_name(const char *_name)
{
	std::unique_lock<std::mutex> lock(ms_gui_lock);
	strncpy(m_s.curr_prgname, _name, PRG_NAME_LEN);
	m_s.curr_prgname[PRG_NAME_LEN-1] = 0;
	m_curr_prgname_changed = true;
}

void Machine::DOS_program_launch(std::string _name)
{
	g_cpu.DOS_program_launch(_name);
	set_DOS_program_name(_name.c_str());
}

void Machine::DOS_program_start(std::string _name)
{
	PINFOF(LOG_V2, LOG_MACHINE, "program start: %s\n", _name.c_str());
	g_cpu.DOS_program_start(_name);
}

void Machine::DOS_program_finish(std::string _name, std::string _newname)
{
	PINFOF(LOG_V2, LOG_MACHINE, "program finish: %s\n", _name.c_str());
	g_cpu.DOS_program_finish(_name);
	set_DOS_program_name(_newname.c_str());
}
