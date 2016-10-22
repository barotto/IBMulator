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

#ifndef IBMULATOR_MACHINE_H
#define IBMULATOR_MACHINE_H

#include <functional>
#include <mutex>
#include "circular_fifo.h"
#include "chrono.h"
#include "hwbench.h"
#include "statebuf.h"
#include "hardware/systemrom.h"
#include "hardware/devices.h"
#include "timers.h"

class CPU;
class Memory;
class IODevice;
class Machine;
extern Machine g_machine;

typedef std::function<void()> Machine_fun_t;
typedef std::function<void(int delta_x, int delta_y, int delta_z, uint button_state)> mouse_fun_t;
typedef std::function<void(int _jid, int _axis, int _value)> joystick_mfun_t;
typedef std::function<void(int _jid, int _button, int _state)> joystick_bfun_t;

/* There's some confusion about the proper terminology here.
 * "Type" is the 4 digit number with which IBM identified the various PS/1's, like 2011 and 2121.
 * "Model" is the combination of Type with a particular variation, e.g. 2121-A82
 * Unfortunately people keep using Model to designate the Type.
 * I use Type in the sense IBM intended.
 */
enum MachineType {
	PS1_2011,
	PS1_2121,
	MCH_UNKNOWN
};

enum MachineReset {
	MACHINE_POWER_ON,   // Machine is switched on using the power button
	MACHINE_HARD_RESET, // Machine RESET triggered by the reset button
	                    //FIXME right now is equivalent to POWER_ON, should we remove it?
	CPU_SOFT_RESET,     // CPU RESET triggered by software
	DEVICE_SOFT_RESET   // Device RESET triggered by software
};

#define PRG_NAME_LEN 261

class Machine
{
private:

	Chrono m_main_chrono;
	HWBench m_bench;

	uint m_heartbeat;
	bool m_quit;
	bool m_on;
	bool m_cpu_single_step;
	uint16_t m_breakpoint_cs;
	uint32_t m_breakpoint_eip;
	std::function<void()> m_breakpoint_clbk;
	double m_cpu_cycles;
	uint m_cpu_cycle_time;
	double m_cycles_factor;

	struct Timer {
		bool        in_use;       // Timer slot is in-use (currently registered).
		uint64_t    period;       // Timer periodocity in nsecs.
		uint64_t    time_to_fire; // Time to fire next (in nsecs).
		bool        active;       // false=inactive, true=active.
		bool        continuous;   // false=one-shot timer, true=continuous periodicity.
		timer_fun_t fire;         // A callback function for when the timer fires.
		char        name[TIMER_NAME_LEN];
	} m_timers[MAX_TIMERS];

	uint m_num_timers;

	struct {
		uint64_t virt_time;
		uint64_t next_timer_time;
		int32_t cycles_left;
		char curr_prgname[PRG_NAME_LEN];
	} m_s;

	std::atomic<uint64_t> m_mt_virt_time;

	std::string m_irq_names[16];

	SystemROM m_sysrom;

	bool m_curr_prgname_changed;

	void core_step(int32_t _cpu_cycles);
	void pause();
	void resume();
	void mem_reset();
	void power_off();
	bool update_timers(uint64_t _vtime);

	CircularFifo<Machine_fun_t,10> m_cmd_fifo;

	mouse_fun_t m_mouse_fun;
	joystick_mfun_t m_joystick_mfun;
	joystick_bfun_t m_joystick_bfun;

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	void set_DOS_program_name(const char *_name);

public:
	bool ms_restore_fail;
	//used for machine-gui synchronization
	static std::mutex ms_gui_lock;

public:

	Machine();
	~Machine();

	void init();
	void calibrate(const Chrono &_c);
	void start();
	bool main_loop();
	void config_changed();

	inline uint64_t get_virt_time_ns() const { return m_s.virt_time; }
	inline uint64_t get_virt_time_us() const { return NSEC_TO_USEC(m_s.virt_time); }
	inline uint64_t get_virt_time_ns_mt() const { return m_mt_virt_time; }
	inline uint64_t get_virt_time_us_mt() const { return NSEC_TO_USEC(m_mt_virt_time); }
	inline HWBench & get_bench() { return m_bench; }

	inline unsigned type() const { return m_sysrom.bios().machine; }
	inline SystemROM & sys_rom() { return m_sysrom; }
	inline Devices & devices() { return g_devices; }

	int register_timer(timer_fun_t _func, const char *_name);
	void unregister_timer(int &_timer);
	void activate_timer(unsigned _timer, uint64_t _nsecs, bool _continuous);
	uint64_t get_timer_eta(unsigned _timer) const;
	void deactivate_timer(unsigned _timer);
	void set_timer_callback(unsigned _timer, timer_fun_t _func);
	inline bool is_timer_active(unsigned _timer) const {
		assert(_timer!=0 && _timer<m_num_timers);
		return m_timers[_timer].active;
	}

	void register_irq(uint8_t irq, const char* name);
	void unregister_irq(uint8_t _irq);
	const char* get_irq_name(uint8_t irq);

	void reset(uint _signal);
	//this is not the x86 DEBUG single step, it's a machine emulation single step
	void set_single_step(bool _val);

	uint8_t get_POST_code();
	void memdump(uint32_t base=0, uint32_t len=0);

	inline bool is_on() { return m_on; }
	inline bool is_paused() { return m_cpu_single_step; }

	//inter-thread commands:
	void cmd_quit();
	void cmd_power_on();
	void cmd_power_off();
	void cmd_cpu_step();
	void cmd_cpu_breakpoint(uint16_t _cs, uint32_t _eip, std::function<void()> _callback);
	void cmd_soft_reset();
	void cmd_reset();
	void cmd_switch_power();
	void cmd_pause();
	void cmd_resume();
	void cmd_memdump(uint32_t base=0, uint32_t len=0);
	void cmd_dtdump(const std::string &_name);
	void cmd_cpulog();
	void cmd_prg_cpulog(std::string _prg_name);
	void cmd_cycles_adjust(double _factor);
	void cmd_save_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_restore_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_insert_media(uint _drive, uint _type, std::string _file, bool _wp);
	void cmd_eject_media(uint _drive);

	void sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv);

	//used by the GUI. inter threading considerations are in keyboard.h/cpp
	void send_key_to_kbctrl(uint32_t key);
	void register_mouse_fun(mouse_fun_t mouse_fun);
	void mouse_motion(int _delta_x, int _delta_y, int _delta_z, uint _button_state);
	void register_joystick_fun(joystick_mfun_t _motion_fun, joystick_bfun_t _button_fun);
	void joystick_motion(int _jid, int _axis, int _value);
	void joystick_button(int _jid, int _button, int _state);

	void DOS_program_launch(std::string _name);
	void DOS_program_start(std::string _name);
	void DOS_program_finish(std::string _name, std::string _newname);
	inline bool is_current_program_name_changed() {
		// caller must gain a lock on ms_gui_lock
		bool changed = m_curr_prgname_changed;
		m_curr_prgname_changed = false;
		return changed;
	}
	inline const char * get_current_program_name() {
		// caller must gain a lock on ms_gui_lock
		return m_s.curr_prgname;
	}
};

#endif
