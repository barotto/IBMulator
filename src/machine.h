/*
 * Copyright (C) 2015-2022  Marco Bortolin
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
#include "timers.h"
#include "shared_queue.h"
#include "pacer.h"
#include "hwbench.h"
#include "statebuf.h"
#include "hardware/cpu.h"
#include "hardware/systemrom.h"
#include "hardware/devices.h"
#include "hardware/devices/keyboard.h"
#include "hardware/devices/floppydisk.h"
#include "hardware/devices/floppyloader.h"

class CPU;
class Memory;
class IODevice;
class Machine;
extern Machine g_machine;

typedef std::function<void()> Machine_fun_t;
typedef std::function<void(int _delta_x, int _delta_y, int _delta_z)> mouse_mfun_t;
typedef std::function<void(MouseButton _button, bool _state)> mouse_bfun_t;
typedef std::function<void(int _jid, int _axis, int _value)> joystick_mfun_t;
typedef std::function<void(int _jid, int _button, int _state)> joystick_bfun_t;

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
public:
	enum MediaCommit {
		MEDIA_ASK, MEDIA_COMMIT, MEDIA_DISCARD, MEDIA_DISCARD_STATES 
	};
private:
	unsigned m_model;
	ModelConfig m_configured_model;

	Pacer m_pacer;
	HWBench m_bench;

	int64_t m_heartbeat;
	bool m_quit;
	bool m_on;
	bool m_valid_state = true;
	int m_config_id = 0;
	bool m_cpu_single_step;
	uint16_t m_breakpoint_cs;
	uint32_t m_breakpoint_eip;
	std::function<void()> m_breakpoint_clbk;
	double m_cpu_cycles;
	uint m_cpu_cycle_time;
	double m_cycles_factor;
	std::atomic<double> m_vtime_ratio;

	EventTimers m_timers;

	struct {
		int32_t cycles_left;
		char curr_prgname[PRG_NAME_LEN];
	} m_s;

	std::vector<std::string> m_irq_names[16];

	SystemROM m_sysrom;
	void load_bios_patches();

	bool m_curr_prgname_changed;

	void main_loop();
	void run_loop();
	void core_step(int32_t _cpu_cycles);
	void pause();
	void resume();
	void mem_reset();
	void power_off();
	bool update_timers(uint64_t _vtime);

	shared_queue<Machine_fun_t> m_cmd_queue;

	mouse_mfun_t m_mouse_mfun = nullptr;
	mouse_bfun_t m_mouse_bfun = nullptr;
	joystick_mfun_t m_joystick_mfun = nullptr;
	joystick_bfun_t m_joystick_bfun = nullptr;

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	void set_DOS_program_name(const char *_name);

	// the floppy loader thread is in the machine object instead of gui, devices,
	// controller, or drive objects to be in a middle point between all the
	// entities that have to use its facilities
	std::unique_ptr<FloppyLoader> m_floppy_loader;
	std::thread m_floppy_loader_thread;

	MediaCommit m_floppy_commit = MEDIA_ASK;
	MediaCommit m_hdd_commit = MEDIA_ASK;

	void commit_floppy(FloppyDisk *_floppy, std::function<void(bool)> _cb);

public:
	bool ms_restore_fail;
	//used for machine-gui synchronization
	static std::mutex ms_gui_lock;

public:

	Machine();
	~Machine();

	void init();
	void calibrate(const Pacer &_p);
	void start();
	void config_changed(bool _startup);

	void set_heartbeat(int64_t _ns);
	inline int64_t get_heartbeat() const { return m_heartbeat; }
	inline uint64_t get_virt_time_ns() const { return m_timers.get_time(); }
	inline uint64_t get_virt_time_us() const { return NSEC_TO_USEC(m_timers.get_time()); }
	inline uint64_t get_virt_time_ns_mt() const { return m_timers.get_time_mt(); }
	inline uint64_t get_virt_time_us_mt() const { return NSEC_TO_USEC(m_timers.get_time_mt()); }
	inline HWBench & get_bench() { return m_bench; }

	inline unsigned type() const { return model().type; }
	inline std::string type_str() const { return g_machine_type_str.at(type()); }
	inline const ModelConfig & model() const { return g_machine_db.at(m_model); }
	inline const ModelConfig & configured_model() const { return m_configured_model; }
	inline SystemROM & sys_rom() { return m_sysrom; }
	inline CPU & cpu() { return g_cpu; }
	inline Devices & devices() { return g_devices; }

	TimerID register_timer(TimerFn _func, const std::string &_name, unsigned _data = 0);
	void unregister_timer(TimerID &_timer);
	void activate_timer(TimerID _timer, uint64_t _nsecs, bool _continuous);
	void activate_timer(TimerID _timer, uint64_t _delay_ns, uint64_t _period_ns, bool _continuous);
	uint64_t get_timer_eta(TimerID _timer) const;
	void deactivate_timer(TimerID _timer);
	void set_timer_callback(TimerID _timer, TimerFn _func, unsigned _data = 0);
	bool is_timer_active(TimerID _timer) const {
		return m_timers.is_timer_active(_timer);
	}
	const EventTimer & get_event_timer(TimerID _timer) const {
		return m_timers.get_event_timer(_timer);
	}

	void register_irq(uint8_t irq, const char* name);
	void unregister_irq(uint8_t _irq, const char* name);
	std::string get_irq_names(uint8_t irq);

	void reset(uint _signal);
	//this is not the x86 DEBUG single step, it's a machine emulation single step
	void set_single_step(bool _val);

	uint8_t get_POST_code();
	void memdump(uint32_t base=0, uint32_t len=0);

	inline bool is_on() const { return m_on; }
	inline bool is_paused() const { return m_cpu_single_step; }
	inline double cycles_factor() const { return m_cycles_factor; }
	inline double vtime_ratio() const { return m_vtime_ratio; }

	MediaCommit hdd_commit_strategy() const { return m_hdd_commit; }

	//inter-thread commands:
	void cmd_quit();
	void cmd_power_on();
	void cmd_power_off();
	void cmd_cpu_step();
	void cmd_cpu_breakpoint(uint16_t _cs, uint32_t _eip, std::function<void()> _callback);
	void cmd_soft_reset();
	void cmd_reset();
	void cmd_switch_power();
	void cmd_pause(bool _show_notice=true);
	void cmd_resume(bool _show_notice=true);
	void cmd_memdump(uint32_t base=0, uint32_t len=0);
	void cmd_dtdump(const std::string &_name);
	void cmd_cpulog();
	void cmd_prg_cpulog(std::string _prg_name);
	void cmd_cycles_adjust(double _factor);
	void cmd_save_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_restore_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv);
	void cmd_insert_floppy(uint8_t _drive, std::string _file, bool _wp, std::function<void(bool)> _cb);
	void cmd_insert_floppy(uint8_t _drive, FloppyDisk *_floppy, std::function<void(bool)> _cb, int _config_id);
	void cmd_eject_floppy(uint8_t _drive, std::function<void(bool)> _cb);
	void cmd_print_VGA_text(std::vector<uint16_t> _text);
	void cmd_reset_bench();
	void cmd_commit_media(std::function<void()> _cb);

	void sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv);

	//used by the GUI. inter threading considerations are in keyboard.h/cpp
	void send_key_to_kbctrl(Keys _key, uint32_t _event);
	void register_mouse_fun(mouse_mfun_t _mouse_mfun, mouse_bfun_t _mouse_bfun);
	void mouse_motion(int _delta_x, int _delta_y, int _delta_z);
	void mouse_button(MouseButton _button, bool _state);
	void register_joystick_fun(joystick_mfun_t _motion_fun, joystick_bfun_t _button_fun);
	void joystick_motion(int _jid, int _axis, int _value);
	void joystick_button(int _jid, int _button, int _state);

	void register_floppy_loader_state_cb(FloppyLoader::state_cb_t _cb);

	void DOS_program_launch(std::string _name);
	void DOS_program_start(std::string _name);
	void DOS_program_finish(std::string _name, std::string _newname);
	inline bool current_program_name_has_changed() {
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
