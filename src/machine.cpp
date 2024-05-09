/*
 * Copyright (C) 2015-2024  Marco Bortolin
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
#include "hardware/devices/dma.h"
#include "hardware/devices/systemboard.h"
#include "hardware/devices/floppyctrl.h"
#include "hardware/devices/floppyfmt_img.h"
#include "hardware/devices/storagectrl.h"
#include "hardware/devices/storagectrl_ata.h"
#include "hardware/devices/storagectrl_ps1.h"
#include "hardware/devices/parallel.h"
#include "filesys.h"

#include "gui/gui.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <unistd.h>
#include <thread>

Machine g_machine;
std::mutex Machine::ms_gui_lock;


Machine::Machine()
{
	memset(&m_s, 0, sizeof(m_s));
}

Machine::~Machine()
{

}

#define MACHINE_STATE_NAME "Machine state"

void Machine::save_state(StateBuf &_state)
{
	/*
	 * This method should be called by Program only via cmd_save_state()
	 */

	StateHeader h;

	h.name = MACHINE_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.write(&m_s, h);

	m_timers.save_state(_state);

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

	try {

		StateHeader h;

		//MACHINE state
		h.name = MACHINE_STATE_NAME;
		h.data_size = sizeof(m_s);
		_state.read(&m_s, h);

		//timers
		m_timers.restore_state(_state);

		//exceptions will be thrown if the buffer size is smaller than expected
		try {
			//CPU state
			g_cpu.restore_state(_state);
		} catch(std::exception &e) {
			PERRF(LOG_MACHINE, "Error restoring the CPU\n");
			throw;
		}

		try {
			//MEMORY state
			g_memory.restore_state(_state);
		} catch(std::exception &e) {
			PERRF(LOG_MACHINE, "Error restoring the main memory\n");
			throw;
		}

		try {
			//DEVICES state
			g_devices.restore_state(_state);
		} catch(std::exception &e) {
			PERRF(LOG_MACHINE, "Error restoring the I/O devices\n");
			throw;
		}

		std::unique_lock<std::mutex> lock(ms_gui_lock);
		m_curr_prgname_changed = true;

		PINFOF(LOG_V0, LOG_MACHINE, "Machine state restored\n");

	} catch(...) {

		// invalid state will remain until the program is terminated
		m_valid_state = false;
		throw;

	}
}

void Machine::calibrate(const Pacer &_p)
{
	m_pacer.calibrate(_p);
}

void Machine::init()
{
	/* the time keeping in IBMulator is equivalent to that of Bochs slowdown:
	 * the emulator is deterministic, and the clock/s are kept in sync with real time
	 * by slowing emulation down when virtual time gets ahead of real time.
	 */

	m_pacer.start();
	m_bench.init(m_pacer.chrono(), 1000);
	m_s.curr_prgname[0] = 0;

	m_timers.set_log_facility(LOG_MACHINE);
	m_timers.init();

	g_cpu.init();
	g_cpu.set_shutdown_trap([this] () {
		reset(CPU_SOFT_RESET);
	});
	g_memory.init();
	m_sysrom.init();
	g_devices.init(this);

	// FLOPPY LOADER THREAD
	m_floppy_loader = std::make_unique<FloppyLoader>(this);
	m_floppy_loader_thread = std::thread(&FloppyLoader::thread_start, m_floppy_loader.get());

	// CD-ROM LOADER THREAD
	m_cdrom_loader = std::make_unique<CdRomLoader>(this);
	m_cdrom_loader_thread = std::thread(&CdRomLoader::thread_start, m_cdrom_loader.get());

	// PRINTER THREAD
	if(g_program.config().get_bool(PRN_SECTION, PRN_CONNECTED)) {

		m_printer = std::make_shared<MpsPrinter>();

		std::string path = g_program.config().find_file(CAPTURE_SECTION, CAPTURE_DIR);
		m_printer->set_base_dir(path);

		mps_printer_paper paper_size = static_cast<mps_printer_paper>( 
			g_program.config().get_enum(PRN_SECTION, PRN_PAPER_SIZE, {
				{ "a4",     MPS_PRINTER_A4 },
				{ "letter", MPS_PRINTER_LETTER },
				{ "legal",  MPS_PRINTER_LEGAL }
			}, MPS_PRINTER_LETTER));

		bool single_sheet = static_cast<bool>( 
			g_program.config().get_enum(PRN_SECTION, PRN_PAPER_TYPE, {
				{ "single", 1 }, { "sheet", 1 },
				{ "continuous", 0 }, { "forms", 0 }
			}, 0));

		m_printer->cmd_load_paper(paper_size, single_sheet);

		m_printer_thread = std::thread(&MpsPrinter::thread_start, m_printer.get());
	}
}

void Machine::register_floppy_loader_state_cb(FloppyEvents::ActivityCbFn _cb)
{
	m_floppy_loader->register_activity_cb(_cb);
}

void Machine::start()
{
	m_quit = false;
	PDEBUGF(LOG_V1, LOG_MACHINE, "Machine thread started\n");
	main_loop();
	PDEBUGF(LOG_V1, LOG_MACHINE, "Machine thread stopping...\n");
	shutdown();
}

void Machine::shutdown()
{
	m_floppy_loader->cmd_quit();
	m_floppy_loader_thread.join();

	m_cdrom_loader->cmd_quit();
	m_cdrom_loader_thread.join();

	if(m_printer) {
		m_printer->cmd_quit();
		m_printer_thread.join();
	}

	g_devices.destroy_all();
}

void Machine::reset(uint _signal)
{
	if(_signal == MACHINE_POWER_ON || _signal == MACHINE_HARD_RESET) {
		std::mutex mtx;
		std::unique_lock<std::mutex> lock(mtx);
		std::condition_variable cv;
		g_mixer.cmd_stop_audiocards_and_signal(mtx, cv);
		cv.wait(lock);
	}
	m_on = true;
	g_cpu.reset(_signal);
	switch(_signal) {
		case CPU_SOFT_RESET:
			PDEBUGF(LOG_V2, LOG_MACHINE, "CPU software reset\n");
			break;
		case MACHINE_HARD_RESET:
			PINFOF(LOG_V1, LOG_MACHINE, "Machine hardware reset\n");
			break;
		case MACHINE_POWER_ON:
			m_bench.start();
			PINFOF(LOG_V0, LOG_MACHINE, "Machine power on\n");
			break;
		default:
			PERRF(LOG_MACHINE, "invalid reset signal: %d\n", _signal);
			throw std::exception();
	}
	if(_signal == MACHINE_POWER_ON || _signal == MACHINE_HARD_RESET) {
		m_timers.reset();
		m_s.cycles_left = 0;
		set_DOS_program_name("");
	}

	g_memory.reset(_signal);
	g_devices.reset(_signal);

	if(_signal == MACHINE_POWER_ON || _signal == MACHINE_HARD_RESET) {
		g_mixer.cmd_start_audiocards();
	}
}

void Machine::power_off()
{
	if(!m_on) {
		PDEBUGF(LOG_V2, LOG_MACHINE, "The machine power is already off\n");
		return;
	}
	PINFOF(LOG_V0, LOG_MACHINE, "Machine power off\n");
	{
		std::mutex mtx;
		std::unique_lock<std::mutex> lock(mtx);
		std::condition_variable cv;
		g_mixer.cmd_stop_audiocards_and_signal(mtx, cv);
		cv.wait(lock);
	}
	m_on = false;
	g_cpu.power_off();
	g_devices.power_off();

	set_DOS_program_name("");
}

void Machine::config_changed(bool _startup)
{
	if(_startup) {
		ini_enum_map_t commit_values = {
			{ "ask",     MEDIA_ASK },
			{ "yes",     MEDIA_COMMIT },
			{ "commit",  MEDIA_COMMIT },
			{ "no",      MEDIA_DISCARD },
			{ "discard", MEDIA_DISCARD },
			{ "discard_states", MEDIA_DISCARD_STATES },
			{ "discard states", MEDIA_DISCARD_STATES }
		};
		m_floppy_commit = static_cast<MediaCommit>(g_program.config().get_enum(DRIVES_SECTION, DRIVES_FLOPPY_COMMIT, commit_values, MEDIA_COMMIT));
		if(m_floppy_commit == MEDIA_DISCARD) {
			PWARN("WARNING: data written to floppy disks will be lost!\n");
		}
		m_hdd_commit = static_cast<MediaCommit>(g_program.config().get_enum(DRIVES_SECTION, DRIVES_HDD_COMMIT, commit_values, MEDIA_COMMIT));
		if(m_hdd_commit == MEDIA_DISCARD) {
			PWARN("WARNING: data written to the hard disk will be lost!\n");
		}
	} else {
		m_config_id++;
	}

	PINFOF(LOG_V1, LOG_MACHINE, "Loading the SYSTEM ROM\n");
	try {
		std::string romset = g_program.config().find_file(SYSTEM_SECTION, SYSTEM_ROMSET);
		m_sysrom.load(romset);
		load_bios_patches();
		g_program.config().set_string(SYSTEM_SECTION, SYSTEM_ROMSET, romset);
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
	g_program.config().set_string(SYSTEM_SECTION, SYSTEM_MODEL, g_machine_db.at(m_model).ini);

	PINFOF(LOG_V0, LOG_MACHINE, "Selected model: %s\n", model().print().c_str());

	g_cpu.config_changed();
	m_sysrom.config_changed();
	g_memory.config_changed();
	g_devices.config_changed();

	m_configured_model = model();
	m_configured_model.cpu_model = g_cpu.model();
	m_configured_model.cpu_freq = unsigned(g_cpu.frequency());
	m_configured_model.exp_ram = g_memory.dram_exp();
	auto flpctrl = devices().device<FloppyCtrl>();
	if(flpctrl) {
		m_configured_model.floppy_a = flpctrl->drive_type(0);
		m_configured_model.floppy_b = flpctrl->drive_type(1);
	} else {
		m_configured_model.floppy_a = FloppyDrive::FDD_NONE;
		m_configured_model.floppy_b = FloppyDrive::FDD_NONE;
	}

	m_configured_model.hdd_interface = "";
	m_configured_model.cdrom = 0;
	auto storage_ctrls = devices().devices<StorageCtrl>();
	for(auto ctrl : storage_ctrls) {
		if(strcmp(ctrl->name(), StorageCtrl_PS1::NAME) == 0) {
			m_configured_model.hdd_interface = "ps1";
		} else if(strcmp(ctrl->name(), StorageCtrl_ATA::NAME) == 0) {
			for(int i=0; i<ctrl->installed_devices(); i++) {
				switch(ctrl->get_device(i)->category()) {
					case StorageDev::DEV_HDD:
						m_configured_model.hdd_interface = "ata";
						break;
					case StorageDev::DEV_CDROM: {
						CdRomDrive *cd = dynamic_cast<CdRomDrive *>(ctrl->get_device(i));
						assert(cd);
						m_configured_model.cdrom = cd->max_speed_x();
						break;
					}
					default:
						break;
				}
			}
		}
	}

	set_heartbeat(DEFAULT_HEARTBEAT);

	PDEBUGF(LOG_V1, LOG_MACHINE, "Registered timers: %u\n", m_timers.get_timers_count());
	for(unsigned i=0; i<m_timers.get_timers_max(); i++) {
		if(m_timers.get_event_timer(i).in_use) {
			PDEBUGF(LOG_V1, LOG_MACHINE, "   %u: %s\n", i, m_timers.get_event_timer(i).name);
		}
	}
	PINFOF(LOG_V0, LOG_MACHINE, "IRQ lines:\n");
	for(unsigned i=0; i<16; i++) {
		PINFOF(LOG_V0, LOG_MACHINE, "   %u: %s\n", i, get_irq_names(i).c_str());
	}
	PINFOF(LOG_V0, LOG_MACHINE, "DMA channels:\n");
	for(unsigned i=0; i<8; i++) {
		PINFOF(LOG_V0, LOG_MACHINE, "   %u: %s\n", i, g_devices.dma()->get_device_name(i).c_str());
	}

	if(m_printer) {
		auto lpt = g_devices.device<Parallel>();
		if(lpt) {
			lpt->connect_printer(m_printer);
		}
	}
}

void Machine::load_bios_patches()
{
	std::string bios_patches = g_program.config().get_string(SYSTEM_SECTION, SYSTEM_BIOS_PATCHES, "");
	auto patches_toks = AppConfig::parse_tokens(bios_patches, "\\|");
	int count = 0;
	for(auto &patch_str : patches_toks) {
		PINFOF(LOG_V0, LOG_MACHINE, "Applying BIOS patch: %s\n", patch_str.c_str());
		auto patch_toks = AppConfig::parse_tokens(patch_str, "\\,");
		if(patch_toks.size() != 2) {
			PERRF(LOG_MACHINE, "Invalid BIOS patch definition: %s\n", patch_str.c_str());
			continue;
		}
		std::string file_name = str_trim(patch_toks[0]);
		std::string file_path = g_program.config().find_media(file_name);
		if(!FileSys::file_exists(file_path.c_str())) {
			PERRF(LOG_MACHINE, "Invalid BIOS patch file: %s\n", file_name.c_str());
			continue;
		}
		auto param_toks = AppConfig::parse_tokens(patch_toks[1], "\\=");
		if(param_toks[0] != "offset") {
			PERRF(LOG_MACHINE, "Invalid BIOS patch parameter: %s\n", param_toks[0].c_str());
			continue;
		}
		int offset = 0;
		try {
			offset = AppConfig::parse_int(param_toks[1]);
		} catch(std::exception &) {
			PERRF(LOG_MACHINE, "Invalid BIOS patch offset value: %s\n", param_toks[1].c_str());
			continue;
		}
		try {
			m_sysrom.load_bios_patch(file_path, offset);
			count++;
		} catch(std::exception &) {
			continue;
		}
	}

	if(count) {
		PINFOF(LOG_V1, LOG_MACHINE, "Successfully installed %d BIOS patches\n", count);
		m_sysrom.update_bios_checksum();
	}
}

void Machine::set_heartbeat(int64_t _nsec)
{
	int64_t oldbeat = m_heartbeat;
	double oldcycles = m_cpu_cycles;

	m_heartbeat = _nsec;
	m_cpu_cycles = g_cpu.frequency() * ((double)m_heartbeat / 1000.0);

	if(oldbeat != m_heartbeat) {
		PDEBUGF(LOG_V1, LOG_MACHINE, "Machine beat period: %lld nsec\n", m_heartbeat);
	}
	if(oldcycles != m_cpu_cycles) {
		PDEBUGF(LOG_V1, LOG_MACHINE, "CPU cycles per beat: %.3f\n", m_cpu_cycles);
	}
	
	m_pacer.set_heartbeat(m_heartbeat);
	m_bench.set_heartbeat(m_heartbeat);
}

void Machine::main_loop()
{
	while(true) {
		PDEBUGF(LOG_V1, LOG_MACHINE, "waiting...\n");
		Machine_fun_t fn;
		m_cmd_queue.wait_and_pop(fn);
		fn();
		
		if(m_on && !m_cpu_single_step) {
			run_loop();
		}
		if(m_quit) {
			break;
		}
	}
}

void Machine::run_loop()
{
	PDEBUGF(LOG_V1, LOG_MACHINE, "running...\n");
	
	static double cycles_rem = 0.0;
	
	m_bench.reset_values();

	while(true) {
		uint64_t vstart = m_timers.get_time();
		m_bench.frame_start(vstart);

		Machine_fun_t fn;
		while(m_cmd_queue.try_and_pop(fn)) {
			fn();
		}
		if(m_quit || !m_on || m_cpu_single_step) {
			return;
		}

		double needed_cycles = m_cpu_cycles * m_cycles_factor;
		int32_t cycles = round(needed_cycles + cycles_rem);
		cycles_rem += needed_cycles - cycles;

		// STEPPING THE CORE
		// everything important happens here
		core_step(cycles);

		m_bench.cpu_cycles(cycles);

		uint64_t vend = m_timers.get_time();
		double vframe_time = double(vend - vstart);

		m_bench.load_end();

		uint64_t sleep_time = m_pacer.wait(m_bench.load_time, m_bench.frame_time);

		m_bench.frame_end(vend);

		m_vtime_ratio = vframe_time / m_bench.frame_time;

		PDEBUGF(LOG_V3, LOG_MACHINE,
			"Core step, fstart=%lld, fend=%lld, lend=%lld, sleep_time=%llu, "
			"cycles=%d, load_time=%lld, frame_time=%lld (%lld), vframe_time=%.0f, vratio=%.4f\n",
			m_bench.get_frame_start(), m_bench.get_frame_end(), m_bench.get_load_end(),
			sleep_time, cycles,
			m_bench.load_time,
			m_bench.frame_time, (m_bench.frame_time - m_heartbeat),
			vframe_time,
			m_vtime_ratio.load());
	}
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
			uint64_t cpu_time = m_timers.get_time() + elapsed_ns;

			if(cpu_time >= m_timers.get_next_timer_time()) {
				while(!m_timers.update(cpu_time));
			}

			cycles_left -= c;
			m_timers.set_time(cpu_time);
		}

		if(m_breakpoint_cs > 0) {
			if(m_breakpoint_cs == REG_CS.sel.value && m_breakpoint_eip == REG_EIP) {
				PINFOF(LOG_V0, LOG_MACHINE, "virtual breakpoint at %04X:%08X\n", m_breakpoint_cs, m_breakpoint_eip);
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

void Machine::set_single_step(bool _val)
{
	m_cpu_single_step = _val;
}

TimerID Machine::register_timer(TimerFn _func, const std::string &_name, unsigned _data)
{
	return m_timers.register_timer(_func, _name, _data);
}

void Machine::unregister_timer(TimerID &_timer)
{
	m_timers.unregister_timer(_timer);
}

void Machine::activate_timer(TimerID _timer, uint64_t _delay_ns, uint64_t _period_ns, bool _continuous)
{
	m_timers.activate_timer(_timer, _delay_ns, _period_ns, _continuous);
}

void Machine::activate_timer(TimerID _timer, uint64_t _nsecs, bool _continuous)
{
	m_timers.activate_timer(_timer, _nsecs, _nsecs, _continuous);
}

void Machine::deactivate_timer(TimerID _timer)
{
	m_timers.deactivate_timer(_timer);
}

uint64_t Machine::get_timer_eta(TimerID _timer) const
{
	return m_timers.get_timer_eta(_timer);
}

void Machine::set_timer_callback(TimerID _timer, TimerFn _func, unsigned _data)
{
	m_timers.set_timer_callback(_timer, _func, _data);
}

void Machine::register_irq(uint8_t _irq, const char* _name)
{
	assert(_irq<16);
	if(!m_irq_names[_irq].empty()) {
		for(auto it = m_irq_names[_irq].begin(); it != m_irq_names[_irq].end(); it++) {
			if(it->compare(_name) == 0) {
				return;
			}
		}
		PWARNF(LOG_V0, LOG_MACHINE, "Possible conflict for IRQ %d:\n", _irq);
		for(auto &dev : m_irq_names[_irq]) {
			PWARNF(LOG_V0, LOG_MACHINE, "  %s\n", dev.c_str());
		}
		PWARNF(LOG_V0, LOG_MACHINE, "  %s\n", _name);
	}
	m_irq_names[_irq].push_back(std::string(_name));
}

void Machine::unregister_irq(uint8_t _irq, const char *_name)
{
	assert(_irq<16);
	for(auto it = m_irq_names[_irq].begin(); it != m_irq_names[_irq].end(); it++) {
		if(it->compare(_name) == 0) {
			m_irq_names[_irq].erase(it);
			PDEBUGF(LOG_V1, LOG_MACHINE, "removing '%s' from IRQ %d\n", _name, _irq);
			return;
		}
	}
	PDEBUGF(LOG_V0, LOG_MACHINE, "'%s' is not a valid device name for IRQ %d\n", _name, _irq);
}

std::string Machine::get_irq_names(uint8_t _irq)
{
	assert(_irq<16);
	return str_implode(m_irq_names[_irq]);
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
			len = g_memory.get_buffer_size();
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
	m_cmd_queue.push([this] () {
		m_quit = true;
	});
}

void Machine::cmd_power_on()
{
	m_cmd_queue.push([this] () {
		if(m_on) {
			return;
		}
		if(!m_valid_state) {
			GUI::instance()->show_message("Invalid state");
		} else {
			reset(MACHINE_POWER_ON);
		}
	});
}

void Machine::cmd_power_off()
{
	m_cmd_queue.push([this] () {
		power_off();
	});
}

void Machine::cmd_cpu_step()
{
	m_cmd_queue.push([this] () {
		if(!m_valid_state) {
			GUI::instance()->show_message("Invalid state");
		} else {
			core_step(0);
		}
	});
}

void Machine::cmd_cpu_breakpoint(uint16_t _cs, uint32_t _eip, std::function<void()> _callback)
{
	m_cmd_queue.push([=] () {
		m_breakpoint_cs = _cs;
		m_breakpoint_eip = _eip;
		m_breakpoint_clbk = _callback;
	});
}

void Machine::cmd_soft_reset()
{
	m_cmd_queue.push([this] () {
		reset(CPU_SOFT_RESET);
	});
}

void Machine::cmd_reset()
{
	m_cmd_queue.push([this] () {
		reset(MACHINE_HARD_RESET);
	});
}

void Machine::cmd_switch_power()
{
	m_cmd_queue.push([this] () {
		if(m_on) {
			power_off();
		} else {
			if(!m_valid_state) {
				GUI::instance()->show_message("Invalid state");
			} else {
				reset(MACHINE_POWER_ON);
			}
		}
	});
}

void Machine::cmd_pause(bool _show_notice)
{
	m_cmd_queue.push([=] () {
		if(!m_cpu_single_step) {
			pause();
			if(_show_notice) {
				PINFOF(LOG_V0, LOG_MACHINE, "Emulation paused\n");
				GUI::instance()->show_message("Emulation paused");
			} else {
				PDEBUGF(LOG_V0, LOG_MACHINE, "Emulation paused\n");
			}
		}
	});
}

void Machine::cmd_resume(bool _show_notice)
{
	m_cmd_queue.push([=] () {
		if(!m_valid_state) {
			GUI::instance()->show_message("Invalid state");
		} else if(m_cpu_single_step) {
			resume();
			if(_show_notice) {
				PINFOF(LOG_V0, LOG_MACHINE, "Emulation resumed\n");
				GUI::instance()->show_message("Emulation resumed");
			} else {
				PDEBUGF(LOG_V0, LOG_MACHINE, "Emulation resumed\n");
			}
		}
	});
}

void Machine::cmd_memdump(uint32_t _base, uint32_t _len)
{
	m_cmd_queue.push([=] () {
		memdump(_base,_len);
	});
}

void Machine::cmd_dtdump(const std::string &_name)
{
	m_cmd_queue.push([=] () {
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
			PWARNF(LOG_V0, LOG_MACHINE, "%s is empty\n", _name.c_str());
			return;
		}
		std::stringstream filename;
		filename << g_program.config().get_cfg_home() + "/" + _name + "dump-0x";
		filename << std::hex << std::uppercase << std::internal << std::setfill('0');
		filename << std::setw(6) << base << "-" << std::setw(4) << limit;
		filename << ".csv";
		try {
			std::ofstream file = FileSys::make_ofstream(filename.str().c_str());
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
	m_cmd_queue.push([this] () {
		g_cpu.write_log();
	});
}

void Machine::cmd_prg_cpulog(std::string _prg_name)
{
	m_cmd_queue.push([=] () {
		g_cpu.enable_prg_log(_prg_name);
	});
}

void Machine::cmd_cycles_adjust(double _factor)
{
	m_cmd_queue.push([=] () {
		m_cycles_factor = _factor;
		devices().cycles_adjust(_factor);
		std::stringstream ss;
		ss << "Emulation speed at ";
		ss << std::setprecision(3);
		ss << (_factor * 100.f) << "%";
		PINFOF(LOG_V0, LOG_MACHINE, "%s\n", ss.str().c_str());
		PDEBUGF(LOG_V0, LOG_MACHINE, "%f cycles per beat\n", m_cpu_cycles * m_cycles_factor);
		GUI::instance()->show_message(ss.str().c_str());
	});
}

void Machine::cmd_save_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_queue.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		if(!m_valid_state) {
			GUI::instance()->show_message("Invalid state");
			_state.m_last_save = false;
		} else {
			_state.m_last_save = true;
			try {
				save_state(_state);
			} catch(std::exception &e) {
				_state.m_last_save = false;
			}
		}
		_cv.notify_one();
	});
}

void Machine::cmd_restore_state(StateBuf &_state, std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_queue.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		if(!m_valid_state) {
			PERRF(LOG_MACHINE, "Cannot restore a new state while the machine is already in an invalid state\n");
			_state.m_last_restore = false;
		} else {
			_state.m_last_restore = true;
			try {
				m_on = false;
				restore_state(_state);
				m_bench.start();
				m_on = true;
			} catch(std::exception &e) {
				PERRF(LOG_MACHINE, "Error restoring the machine state\n");
				_state.m_last_restore = false;
			}
		}
		_cv.notify_one();
	});
}

void Machine::cmd_insert_floppy(uint8_t _drive, std::string _img_path, bool _wp,
		std::function<void(bool)> _cb)
{
	// called by the GUI
	if(!g_devices.device<FloppyCtrl>()) {
		return;
	}
	int config_id = m_config_id;
	m_cmd_queue.push([=] () {
		auto type = g_devices.device<FloppyCtrl>()->drive_type(_drive);
		if(type == FloppyDrive::FDD_NONE) {
			PDEBUGF(LOG_V0, LOG_MACHINE, "cmd_insert_floppy(): invalid drive\n");
			return;
		}
		m_floppy_loader->cmd_load_floppy(_drive, type, _img_path, _wp, _cb, config_id);
	});
}

void Machine::cmd_insert_floppy(uint8_t _drive, FloppyDisk *_floppy,
		std::function<void(bool)> _cb, int _config_id)
{
	// called by the floppy loader
	m_cmd_queue.push([=] () {
		if(_config_id != m_config_id) {
			// configuration changed after a call to cmd_insert_floppy() by the GUI
			PDEBUGF(LOG_V1, LOG_MACHINE, "Floppy arrived too late...\n");
			delete _floppy;
			return;
		}
		if(!_floppy) {
			if(_cb) {
				_cb(false);
			}
			return;
		}
		bool result = g_devices.device<FloppyCtrl>()->insert_media(_drive, _floppy);
		if(!result) {
			delete _floppy;
		}
		if(_cb) {
			_cb(result);
		}
	});
}

void Machine::cmd_eject_floppy(uint8_t _drive, std::function<void(bool)> _cb)
{
	m_cmd_queue.push([=] () {
		auto ctrl = g_devices.device<FloppyCtrl>();
		if(!ctrl) {
			if(_cb) { _cb(true); }
			return;
		}
		FloppyDisk *floppy = ctrl->eject_media(_drive, false);
		if(!floppy) {
			if(_cb) { _cb(true); }
			return;
		}
		if(floppy->is_dirty()) {
			if(m_floppy_commit == MEDIA_DISCARD || 
			   (m_config_id > 0 && m_floppy_commit == MEDIA_DISCARD_STATES))
			{
				PWARNF(LOG_V0, LOG_MACHINE, "Floppy %s not saved. Written data discarded.\n", _drive ? "B" : "A");
				if(_cb) { _cb(true); }
			} else if(!floppy->can_be_committed()) {
				PWARNF(LOG_V0, LOG_MACHINE, "Floppy %s image can't be committed to storage. Written data discarded.\n",
						_drive ? "B" : "A");
				if(_cb) { _cb(false); }
			} else if(m_floppy_commit == MEDIA_ASK) {
				const char *section = _drive ? DISK_B_SECTION : DISK_A_SECTION;
				std::string path = g_program.config().get_string(section, DISK_PATH);
				GUI::instance()->show_message_box(
						str_format("Save floppy %s image", _drive ? "B" : "A"),
						str_format("Save \"%s\"?", FileSys::get_basename(path.c_str()).c_str()),
						MessageWnd::Type::MSGW_YES_NO,
						[=](){ commit_floppy(floppy, _drive, _cb); },
						[=](){ if(_cb) { _cb(true); } });
			} else {
				commit_floppy(floppy, _drive, _cb);
			}
		} else {
			if(_cb) { _cb(true); }
		}
	});
}

void Machine::cmd_insert_cdrom(CdRomDrive *_drive, std::string _img_path, std::function<void(bool)> _cb)
{
	// GUI thread -> CD Loader

	int config_id = m_config_id;
	m_cmd_queue.push([=] () {
		_drive->signal_activity(CdRomEvents::MEDIUM_LOADING, 0);
		m_cdrom_loader->cmd_load_cdrom(_drive, _img_path, _cb, config_id);
	});
}

void Machine::cmd_insert_cdrom(CdRomDrive *_drive, CdRomDisc *_cdrom, std::string _img_path,
		std::function<void(bool)> _cb, int _config_id)
{
	// CD loader -> CdRomDrive

	m_cmd_queue.push([=] () {
		if(_config_id != m_config_id) {
			// configuration changed after a call to cmd_insert_cdrom() by the GUI
			PDEBUGF(LOG_V1, LOG_MACHINE, "CD-ROM arrived too late...\n");
			delete _cdrom;
		} else {
			if(_cdrom) {
				_drive->insert_medium(_cdrom, _img_path);
			} else {
				_drive->signal_activity(CdRomEvents::MEDIUM, 0);
			}
			if(_cb) {
				_cb(_cdrom != nullptr);
			}
		}
	});
}

void Machine::cmd_toggle_cdrom_door(CdRomDrive *_drive)
{
	m_cmd_queue.push([=] () {
		// if the machine is off it will remove the disc without opening the tray
		_drive->toggle_door_button();
	});
}

void Machine::cmd_dispose_cdrom(CdRomDisc *_disc)
{
	m_cdrom_loader->cmd_dispose_cdrom(_disc);
}

void Machine::commit_floppy(FloppyDisk *_floppy, uint8_t _drive, std::function<void(bool)> _cb)
{
	// if called from cmd_eject_floppy() this could be the Main thread,
	// otherwise it's the Machine thread

	// this function takes ownership of _floppy
	assert(_floppy);
	std::unique_ptr<FloppyDisk> floppy(_floppy);

	auto path = floppy->get_image_path();
	if(path.empty()) {
		PDEBUGF(LOG_V0, LOG_MACHINE, "Empty media path!\n");
		if(_cb) {
			_cb(false);
		}
		return;
	}
	auto format = floppy->get_format();
	if(!format || !format->can_save()) {
		if(format) {
			PERRF(LOG_MACHINE, "%s format doesn't support save!\n", format->name());
		} else {
			PERRF(LOG_MACHINE, "No format defined!\n");
		}
		if(_cb) {
			_cb(false);
		}
		return;
	}
	std::string base, ext;
	FileSys::get_file_parts(path.c_str(), base, ext);
	if(!format->has_file_extension(ext)) {
		path += format->default_file_extension();
	}
	m_floppy_loader->cmd_save_floppy(floppy.release(), path, format, _drive, _cb);
}

void Machine::cmd_commit_media(std::function<void()> _cb)
{
	// The thread may be stopped waiting for user input from the GUI, so the machine
	// should be paused beforehand.

	m_cmd_queue.push([=](){

		std::mutex mtx;
		std::condition_variable cond;
		std::unique_lock<std::mutex> save_lock(mtx);

		auto hdc = g_devices.device<StorageCtrl>();
		if(hdc && (
		      m_hdd_commit == MEDIA_ASK
		  ||  m_hdd_commit == MEDIA_COMMIT
		  || (m_hdd_commit == MEDIA_DISCARD_STATES && m_config_id <= 0)
		))
		{
			for(int i=0; i<hdc->installed_devices(); i++) {
				const StorageDev *dev = hdc->get_device(i);
				if(!dev || dev->is_read_only() || !dev->is_dirty(true)) {
					continue;
				}
				bool save_this = true;
				if(m_hdd_commit == MEDIA_ASK) {
					GUI::instance()->show_message_box(
							str_format("Save %s image", dev->name()),
							str_format("Save \"%s\"?", FileSys::get_basename(dev->path()).c_str()),
							MessageWnd::Type::MSGW_YES_NO,
							[&](){ std::unique_lock<std::mutex> lock(mtx); save_this = true; cond.notify_all(); },
							[&](){ std::unique_lock<std::mutex> lock(mtx); save_this = false; cond.notify_all(); });
					cond.wait(save_lock);
				}
				if(save_this) {
					dev->commit();
				}
			}
		}

		auto fdc = g_devices.device<FloppyCtrl>();

		if(!fdc || 
		    m_floppy_commit == MEDIA_DISCARD || 
		   (m_config_id > 0 && m_floppy_commit == MEDIA_DISCARD_STATES))
		{
			if(_cb) { _cb(); }
			return;
		}

		// I want the caller to be notified only after the last floppy has been saved.
		// saving results are not reported.
		int last_dirty = -1;
		for(int i=0; i<int(FloppyCtrl::MAX_DRIVES); i++) {
			// If the machine is in a savestate, commit floppy only if it has been written
			// to after the savestate restore. A floppy is committed always only
			// after an explicit eject (press of the button).
			if(fdc->is_media_dirty(i,true) && i > last_dirty) {
				last_dirty = i;
			}
		}

		if(last_dirty >= 0) {
			GUI::instance()->show_message("Saving floppy disks...");

			for(int i=0; i<int(FloppyCtrl::MAX_DRIVES); i++) {
				if(fdc->is_media_dirty(i,true)) {
					bool save_this = true;
					if(!fdc->can_media_be_committed(i)) {
						PWARNF(LOG_V0, LOG_MACHINE, "Floppy %s format doesn't support save. Written data discarded.\n",
								i ? "B" : "A");
						save_this = false;
					} else if(m_floppy_commit == MEDIA_ASK) {
						const char *section = i ? DISK_B_SECTION : DISK_A_SECTION;
						std::string path = g_program.config().get_string(section, DISK_PATH);
						GUI::instance()->show_message_box(
								str_format("Save floppy %s image", i ? "B" : "A"),
								str_format("Save \"%s\"?", FileSys::get_basename(path.c_str()).c_str()),
								MessageWnd::Type::MSGW_YES_NO,
								[&](){ std::unique_lock<std::mutex> lock(mtx); save_this = true; cond.notify_all(); },
								[&](){ std::unique_lock<std::mutex> lock(mtx); save_this = false; cond.notify_all(); });
						cond.wait(save_lock);
					}
					if(save_this) {
						FloppyDisk *floppy = fdc->eject_media(i, true);
						assert(floppy);
						commit_floppy(floppy, i, [=](bool){
							// this is the FloppyLoader thread
							if(_cb && i == last_dirty) {
								_cb();
							}
						});
					} else {
						if(_cb && i == last_dirty) {
							_cb();
						}
					}
				}
			}
		} else {
			if(_cb) { _cb(); }
		}
	});
}

void Machine::cmd_print_VGA_text(std::vector<uint16_t> _text)
{
	m_cmd_queue.push([=] () {
		g_devices.vga()->print_text(_text);
	});
}

void Machine::cmd_reset_bench()
{
	m_cmd_queue.push([=] () {
		m_bench.reset_values();
	});
}

void Machine::sig_config_changed(std::mutex &_mutex, std::condition_variable &_cv)
{
	m_cmd_queue.push([&] () {
		std::unique_lock<std::mutex> lock(_mutex);
		if(m_on) {
			power_off();
		}
		config_changed(false);
		_cv.notify_one();
	});
}

void Machine::send_key_to_kbctrl(Keys _key, uint32_t _event)
{
	g_devices.device<Keyboard>()->gen_scancode(_key, _event);
}

void Machine::register_mouse_fun(mouse_mfun_t _mouse_mfun, mouse_bfun_t _mouse_bfun)
{
	m_mouse_mfun = _mouse_mfun;
	m_mouse_bfun = _mouse_bfun;
}

void Machine::mouse_motion(int _delta_x, int _delta_y, int _delta_z)
{
	if(m_mouse_mfun) {
		m_mouse_mfun(_delta_x, _delta_y, _delta_z);
	}
}

void Machine::mouse_button(MouseButton _button, bool _state)
{
	if(m_mouse_bfun) {
		m_mouse_bfun(_button, _state);
	}
}

void Machine::register_joystick_fun(joystick_mfun_t _motion_fun, joystick_bfun_t _button_fun)
{
	m_joystick_mfun = _motion_fun;
	m_joystick_bfun = _button_fun;
}

void Machine::joystick_motion(int _jid, int _axis, int _value)
{
	if(m_on && m_joystick_mfun) {
		m_joystick_mfun(_jid, _axis, _value);
	}
}

void Machine::joystick_button(int _jid, int _button, int _state)
{
	if(m_on && m_joystick_bfun) {
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
