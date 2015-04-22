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

#include "ibmulator.h"
#include "cpu.h"
#include "cpu/core.h"
#include "cpu/decoder.h"
#include "cpu/executor.h"
#include "cpu/debugger.h"
#include "machine.h"
#include "devices.h"
#include "devices/pic.h"
#include "devices/dma.h"
#include "program.h"
#include "utils.h"
#include <cstring>

CPU g_cpu;

CPU::CPU()
:
m_instr(NULL),
m_log_idx(0),
m_log_size(0),
m_log_file(NULL)
{

}

CPU::~CPU()
{
	if(m_log_file) {
		fclose(m_log_file);
	}
	if(m_log_prg_file) {
		fclose(m_log_prg_file);
	}
}

void CPU::init()
{
	g_cpubus.init();
	config_changed();
	m_s.icount = 0;
	m_s.ccount = 0;

	if(CPULOG) {
		std::string filename = g_program.config().get_cfg_home() + FS_SEP "cpulog.log";
		//i use the C stdlib because i hate C++ streams with a passion
		m_log_file = fopen(filename.c_str(), "w");
	}
}

void CPU::config_changed()
{
	m_freq = uint32_t(g_program.config().get_real(CPU_SECTION, CPU_FREQUENCY) * 1000000.0);
	m_cycle_time = uint32_t(1.0e9/double(m_freq));
	PINFOF(LOG_V0, LOG_CPU, "Frequency: %.1f MHz\n", float(m_freq) / 1.0e6);
	PINFOF(LOG_V1, LOG_CPU, "Cycle time: %u nsec\n", m_cycle_time);

	g_cpubus.config_changed();
}

void CPU::reset(uint _signal)
{
	bool irqwaiting = is_pending(CPU_EVENT_PENDING_INTR);

	m_s.activity_state = CPU_STATE_ACTIVE;
	m_s.event_mask = 0;
	m_s.pending_event = 0;
	m_s.async_event = 0;
	m_s.debug_trap = false;
	m_s.EXT = false;
	if(_signal==MACHINE_POWER_ON || _signal==MACHINE_HARD_RESET) {
		m_s.icount = 0;
		m_s.ccount = 0;
		m_s.HRQ = false;
		m_s.inhibit_mask = 0;
		m_s.inhibit_icount = 0;
		m_log_prg_iret = 0;
		if(m_log_prg_file) {
			disable_prg_log();
		}
	} else {
		if(irqwaiting) {
			raise_INTR();
			mask_event(CPU_EVENT_PENDING_INTR); //the CPU starts with IF=0
		}
	}

	g_cpucore.reset();
	g_cpuexecutor.reset(_signal);
	g_cpubus.invalidate_pq();
	g_cpubus.update_pq(0);
}

#define CPU_STATE_NAME "CPU"

void CPU::save_state(StateBuf &_state)
{
	//CPU state
	StateHeader h;
	h.name = CPU_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	g_cpubus.save_state(_state);
	g_cpucore.save_state(_state);
	//decoder and executor don't have a state to save and restore
}

void CPU::restore_state(StateBuf &_state)
{
	StateHeader h;
	h.name = CPU_STATE_NAME;
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	g_cpubus.restore_state(_state);
	g_cpucore.restore_state(_state);

	m_log_prg_iret = 0;
	if(m_log_prg_file) {
		disable_prg_log();
	}
}

void CPU::power_off()
{
	enter_sleep_state(CPU_STATE_SHUTDOWN);
	disable_prg_log();
}

uint CPU::step()
{
	uint cycles, decode_cycles;
	uint32_t csip, prev_csip;
	CPUCore core_log;

	g_cpubus.reset_counters();
	decode_cycles = 0;
	csip = 0;
	prev_csip = 0;

	if(m_s.activity_state == CPU_STATE_ACTIVE) {

		if(m_s.async_event) {
			// check on events which occurred for previous instructions (traps)
			// and ones which are asynchronous to the CPU (hardware interrupts)
			handle_async_event();
			//an interrupt could have invalidated the pq, we must update
			g_cpubus.update_pq(0);
		}
		csip = GET_PHYADDR(CS,REG_IP);
		prev_csip = (m_instr!=NULL)?m_instr->csip:0;
		if(m_instr==NULL || !(m_instr->rep && prev_csip==csip)) {
			//if the prev instr is the same as the next (REP), don't decode
			if(!g_cpubus.is_pq_valid()) {
				/*
				According to various sources, the decoding time should be
				proportional to the size of the next instruction (1 cycle per
				decoded byte). But after some empirical tests, 1 cycle seems
				more appropriate.
				*/
				decode_cycles = 1;
			}
			m_instr = g_cpudecoder.decode();
			if(CPULOG) {
				core_log = g_cpucore;
			}
		}
		try {
			g_cpuexecutor.execute(m_instr);
			cycles = get_execution_cycles();
			m_instr->cycles.rep = 0;
		} catch(CPUException &e) {
			PDEBUGF(LOG_V2, LOG_CPU, "CPU exception %u\n", e.vector);
			if(STOP_AT_EXC) {
				g_machine.set_single_step(true);
				if(e.vector == CPU_UD_EXC && UD6_AUTO_DUMP) {
					PERRF(LOG_CPU,"illegal opcode at 0x%07X, dumping code segment\n", m_instr->csip);
					g_machine.memdump(REG_CS.desc.base, GET_LIMIT(CS));
				}
			}
			exception(e);
			cycles = 15; //just a random number
		}

	} else {
		// the CPU is idle and waiting for an external event
		wait_for_event();
		//we need to spend at least 1 cycle, otherwise the timers will never fire
		cycles = 1;
	}

	g_cpubus.update_pq(cycles);

	uint dramtx = g_cpubus.get_dram_tx();
	uint vramtx = g_cpubus.get_vram_tx();
	uint penalty = g_cpubus.get_cycles_penalty();
	cycles += decode_cycles + dramtx*DRAM_TX_CYCLES + penalty + vramtx*VRAM_TX_CYCLES;
	if((dramtx||penalty) && (g_machine.get_virt_time_ns()%15085)<((cycles*m_cycle_time))) {
		cycles += DRAM_REFRESH_CYCLES;
	}
	if(CPULOG && prev_csip!=csip && !g_pic.get_isr()) {
		add_to_log(*m_instr, g_machine.get_virt_time_us(), core_log, g_cpubus, cycles);
	}
	m_s.icount++;
	m_s.ccount += cycles;
	ASSERT(cycles>0 || (m_instr->rep && REG_CX==0));

	return cycles;
}

uint CPU::get_execution_cycles()
{
	uint cycles_spent = m_instr->cycles.rep;
	uint base;
	if(m_instr->rep) {
		base = m_instr->cycles.base_rep;
	} else {
		base = m_instr->cycles.base;
	}
	if(IS_PMODE() && m_instr->cycles.pmode>0) {
		//cycles_spent += m_instr->cycles.pmode;
		//TODO pmode values are with mem tx
		cycles_spent += base;
	} else {
		if(m_instr->cycles.noj>0) {
			//TODO consider the BOUND case
			if(g_cpubus.is_pq_valid()) {
				//jmp not taken
				cycles_spent += m_instr->cycles.noj;
			} else {
				cycles_spent += base;
			}
		} else {
			//TODO complete the pmode CALL/JMP
			cycles_spent += base;
		}
	}
	return cycles_spent;
}

void CPU::enter_sleep_state(CPUActivityState _state)
{
	switch(_state) {
		case CPU_STATE_ACTIVE:
			ASSERT(false); // should not be used for entering active CPU state
			break;

		case CPU_STATE_HALT:
			break;

		case CPU_STATE_SHUTDOWN:
			SET_FLAG(IF,0); // masking interrupts
			PINFO(LOG_CPU, "Shutdown\n");
			break;

		default:
			PERRF_ABORT(LOG_CPU,"enter_sleep_state: unknown state %d\n", _state);
			break;
	}

	// artificial trap bit, why use another variable.
	m_s.activity_state = _state;
	m_s.async_event = true; // so processor knows to check
	// Execution completes.  The processor will remain in a sleep
	// state until one of the wakeup conditions is met.
}

void CPU::wait_for_event()
{
	// pass the time until an interrupt wakes up the CPU.

	if((is_pending(CPU_EVENT_PENDING_INTR) && FLAG_IF)
		|| is_unmasked_event_pending(CPU_EVENT_NMI) )
	{
		// interrupt ends the HALT condition
		m_s.activity_state = CPU_STATE_ACTIVE;
		m_s.inhibit_mask = 0; // clear inhibits for after resume
		return;
	}

	if(m_s.activity_state == CPU_STATE_ACTIVE) {
		return;
	}

	if(m_s.HRQ) {
		// handle DMA also when CPU is halted
		g_dma.raise_HLDA();
	}
}

void CPU::handle_async_event()
{
	/* Processing order:
	 * 1. instruction exception
	 * 2. single step
	 * 3. NMI
	 * 4. NPX segment overrun
	 * 5. INTR
	 *
	 * When simultaneous interrupt requests occur, they are processed in a fixed
	 * order as shown. Interrupt processing involves saving the flags, the
	 * return address, and setting CS:IP to point at the first instruction of
	 * the interrupt handler. If other interrupts remain enabled, they are
	 * processed before the first instruction of the current interrupt handler
	 * is executed. The last interrupt processed is therefore the first one
	 * serviced.
	 * (80286 programmer's reference manual 5-4)
	 */

	if(!interrupts_inhibited(CPU_INHIBIT_DEBUG)) {
	    // A trap may be inhibited on this boundary due to an instruction which loaded SS
		if(m_s.debug_trap) {
			exception(CPUException(CPU_SINGLE_STEP_INT, 0)); // no error, not interrupt
			return;
		}
	}

	// External Interrupts
	//   NMI Interrupts
	//   Maskable Hardware Interrupts
	if(interrupts_inhibited(CPU_INHIBIT_INTERRUPTS)) {
		// Processing external interrupts is inhibited on this
		// boundary because of certain instructions like STI.
	} else if(is_unmasked_event_pending(CPU_EVENT_NMI)) {
		clear_event(CPU_EVENT_NMI);
		mask_event(CPU_EVENT_NMI);
		m_s.EXT = true; /* external event */
		interrupt(2, CPU_NMI, false, 0);
	} else if(is_unmasked_event_pending(CPU_EVENT_PENDING_INTR)) {
		uint8_t vector = g_pic.IAC(); // may set INTR with next interrupt
		m_s.EXT = true; /* external event */
		interrupt(vector, CPU_EXTERNAL_INTERRUPT, 0, 0);
	} else if(m_s.HRQ) {
		// assert Hold Acknowledge (HLDA) and go into a bus hold state
		g_dma.raise_HLDA();
	}

	if(FLAG_TF) {
		m_s.debug_trap = true;
	}

	// Faults from decoding next instruction
	//   Instruction length > 15 bytes
	//   Illegal opcode
	//   Coprocessor not available
	// (handled in main decode loop etc)

	// Faults on executing an instruction
	//   Floating point execution
	//   Overflow
	//   Bound error
	//   Invalid TSS
	//   Segment not present
	//   Stack fault
	//   General protection
	// (handled by rest of the code)

	if(!(unmasked_events_pending() || m_s.debug_trap || m_s.HRQ)) {
		m_s.async_event = false;
	}
}

void CPU::interrupt(uint8_t _vector, unsigned _type, bool _push_error, uint16_t _error_code)
{
	bool soft_int = false;
	switch(_type) {
		case CPU_SOFTWARE_INTERRUPT:
		case CPU_SOFTWARE_EXCEPTION:
			soft_int = true;
			break;
		case CPU_PRIVILEGED_SOFTWARE_INTERRUPT:
		case CPU_EXTERNAL_INTERRUPT:
		case CPU_NMI:
		case CPU_HARDWARE_EXCEPTION:
			break;
		default:
			PERRF_ABORT(LOG_CPU, "interrupt(): unknown exception type %d\n", _type);
			break;
	}

	PDEBUGF(LOG_V2, LOG_CPU, "interrupt(): vector = %02x, TYPE = %u, EXT = %u\n", _vector, _type, m_s.EXT);

	// Discard any traps and inhibits for new context; traps will
	// resume upon return.
	clear_inhibit_mask();
	clear_debug_trap();

	if(_type != CPU_SOFTWARE_INTERRUPT) {
		try {
			if(IS_PMODE()) {
				g_cpuexecutor.interrupt_pmode(_vector, soft_int, _push_error, _error_code);
			} else {
				g_cpuexecutor.interrupt(_vector);
			}
		} catch(CPUException &e) {
			m_s.EXT = 0;
			throw;
		}
	}

	m_s.EXT = 0;
}

void CPU::exception(CPUException _exc)
{
	PDEBUGF(LOG_V2, LOG_CPU, "exception(0x%02x): error_code=%04x\n",
			_exc.vector, _exc.error_code);
	bool push_error = false;
	uint16_t error_code = (_exc.error_code & 0xfffe);
	m_s.EXT = 0;
	switch(_exc.vector) {
		case CPU_DIV_ER_EXC: //0
			RESTORE_IP();
			break;
		case CPU_SINGLE_STEP_INT: //1
			/* Intel's 286 manual: "The saved value of CS:IP will point to the
			 * next instruction.".
			 */
			break;
		case CPU_NMI_INT: //2
			RESTORE_IP();
			break;
		case CPU_BREAKPOINT_INT: //3
		case CPU_INTO_EXC: //4
			break;
		case CPU_BOUND_EXC: //5
			RESTORE_IP();
			break;
		case CPU_UD_EXC: //6
			RESTORE_IP();
			break;
		case CPU_NM_EXC: //7
			RESTORE_IP();
			break;
		case CPU_DF_EXC:  //8
		/*case CPU_IDT_LIMIT_EXC:*/
			RESTORE_IP();
			error_code = 0;
			push_error = true;
			break;
		case CPU_MP_EXC: //9
		/*case CPU_NPX_SEG_OVR_INT:*/
			RESTORE_IP();
			m_s.EXT = 1;
			break;
		case CPU_TS_EXC: //10
			RESTORE_IP();
			break;
		case CPU_NP_EXC: //11
			RESTORE_IP();
			break;
		case CPU_SS_EXC: //12
			RESTORE_IP();
			break;
		case CPU_GP_EXC: //13
		/*case CPU_SEG_OVR_EXC:*/
			RESTORE_IP();
			push_error = true;
			break;
		case CPU_MF_EXC: //16
		/*case CPU_NPX_ERR_INT:*/
			RESTORE_IP();
			m_s.EXT = 1;
			break;
		default:
			PERRF_ABORT(LOG_CPU, "exception(%u): bad vector!\n", _exc.vector);
			break;
	}

	error_code |= m_s.EXT;

	try {
		interrupt(_exc.vector, CPU_HARDWARE_EXCEPTION, push_error, error_code);
	} catch(CPUException &e) {
		/* If two separate faults occur during a single instruction, and if the
		 * first fault is any of #0, #10, #11, #12, and #13, exception 8
		 * (Double Fault) occurs (e.g., a general protection fault in level 3 is
		 * followed by a not-present fault due to a segment not-present). If
		 * another protection violation occurs during the processing of
		 * exception 8, the 80286 enters shutdown, during which time no further
		 * instructions or exceptions are processed.
		 */
		if(_exc.vector == CPU_DF_EXC) {
			PERRF(LOG_CPU,"exception(): 3rd (%d) exception with no resolution\n", e.vector);
			enter_sleep_state(CPU_STATE_SHUTDOWN);
			return;
		}
		if(_exc.vector==CPU_DIV_ER_EXC //#0
			|| _exc.vector==CPU_TS_EXC //#10
			|| _exc.vector==CPU_NP_EXC //#11
			|| _exc.vector==CPU_SS_EXC //#12
			|| _exc.vector==CPU_GP_EXC) //#13
		{
			exception(CPUException(CPU_DF_EXC,0));
		} else {
			PERRF(LOG_CPU,"exception(): #GP while resolving exc %d\n", _exc.vector);
			exception(e);
		}
	}
}

void CPU::signal_event(uint32_t _event)
{
	m_s.pending_event |= _event;
	if(!is_masked_event(_event)) {
		m_s.async_event = true;
	}
}

void CPU::clear_event(uint32_t _event)
{
	m_s.pending_event &= ~_event;
}

void CPU::mask_event(uint32_t event)
{
	m_s.event_mask |= event;
}

void CPU::unmask_event(uint32_t event)
{
	m_s.event_mask &= ~event;
	if(is_pending(event)) {
		m_s.async_event = true;
	}
}

bool CPU::is_masked_event(uint32_t _event)
{
	return (m_s.event_mask & _event) != 0;
}

bool CPU::is_pending(uint32_t _event)
{
	return (m_s.pending_event & _event) != 0;
}

bool CPU::is_unmasked_event_pending(uint32_t _event)
{
	return (m_s.pending_event & ~m_s.event_mask & _event) != 0;
}

uint32_t CPU::unmasked_events_pending()
{
	return (m_s.pending_event & ~m_s.event_mask);
}

void CPU::interrupt_mask_change()
{
	if(FLAG_IF) {
		// IF was set, unmask events
		unmask_event(CPU_EVENT_PENDING_INTR);
		return;
	}
	// IF was cleared, INTR would be masked
	mask_event(CPU_EVENT_PENDING_INTR);
}

void CPU::raise_INTR()
{
	signal_event(CPU_EVENT_PENDING_INTR);
}

void CPU::clear_INTR()
{
	clear_event(CPU_EVENT_PENDING_INTR);
}

void CPU::deliver_NMI()
{
	signal_event(CPU_EVENT_NMI);
}

void CPU::set_HRQ(bool _val)
{
	m_s.HRQ = _val;
	if(_val) {
		m_s.async_event = true;
	}
}

void CPU::inhibit_interrupts(unsigned mask)
{
	// Loading of SS disables interrupts until the next instruction completes
	// but only under assumption that previous instruction didn't load SS also.
	if(mask != CPU_INHIBIT_INTERRUPTS_BY_MOVSS
		|| !interrupts_inhibited(CPU_INHIBIT_INTERRUPTS_BY_MOVSS))
	{
		m_s.inhibit_mask = mask;
		m_s.inhibit_icount = m_s.icount + 1; // inhibit for next instruction
	}
}

bool CPU::interrupts_inhibited(unsigned mask)
{
	return (m_s.icount <= m_s.inhibit_icount) && (m_s.inhibit_mask & mask) == mask;
}

void CPU::add_to_log(const Instruction &_instr, uint64_t _time,
		const CPUCore &_core, const CPUBus &_bus, unsigned _cycles)
{
	//don't log outside fixed boundaries
	if(_instr.csip<CPULOG_START_ADDR || _instr.csip>CPULOG_END_ADDR) {
		return;
	}

	m_log_size = std::min(m_log_size+1, CPULOG_MAX_SIZE);
	m_log[m_log_idx].time = _time;
	m_log[m_log_idx].core = _core;
	m_log[m_log_idx].bus = _bus;
	m_log[m_log_idx].instr = _instr;
	m_log[m_log_idx].cycles = _cycles;

	if(m_log_prg_file) {
		if(CPULOG_LOG_INTS || m_log_prg_iret==0 || (m_log_prg_iret==_instr.csip)) {
			m_log_prg_iret = 0;
			write_log_entry(m_log_prg_file, m_log[m_log_idx]);
		}
	}
	m_log_idx = (m_log_idx+1) % CPULOG_MAX_SIZE;
}

void CPU::enable_prg_log(std::string _prg_name)
{
	if(m_log_prg_file) {
		fclose(m_log_prg_file);
		m_log_prg_file = NULL;
	}
	m_log_prg_name = _prg_name;
	if(!_prg_name.empty()) {
		str_replace_all(_prg_name,".","\\.");
		m_log_prg_regex.assign(_prg_name+"$", std::regex::ECMAScript|std::regex::icase);
	}
}

void CPU::disable_prg_log()
{
	enable_prg_log("");
}

void CPU::DOS_program_launch(std::string /*_name*/)
{
}

void CPU::DOS_program_start(std::string _name)
{
	if(!m_log_prg_name.empty() && std::regex_search(_name, m_log_prg_regex)) {
		if(m_log_prg_file) {
			fclose(m_log_prg_file);
		}
		std::string filename = g_program.config().get_cfg_home() + FS_SEP + m_log_prg_name + ".log";
		m_log_prg_file = fopen(filename.c_str(), "w");
	}
}

void CPU::DOS_program_finish(std::string _name)
{
	if(m_log_prg_file && (std::regex_search(_name, m_log_prg_regex) || _name.empty())) {
		fclose(m_log_prg_file);
		m_log_prg_file = NULL;
	}
}

void CPU::INT(uint32_t _retaddr)
{
	if(m_log_prg_file && m_log_prg_iret==0 && g_pic.get_isr()==0) {
		m_log_prg_iret = _retaddr;
	}
}

const std::string & CPU::disasm(CPULogEntry &_log_entry)
{
	CPUDebugger debugger;
	debugger.set_core(&_log_entry.core);

	static std::string str;
	str = "";

	static char empty[23] = { 32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,0 };

	char dline[200];
	debugger.disasm(dline, 200u,
		_log_entry.instr.csip, _log_entry.instr.ip,
		_log_entry.instr.bytes, _log_entry.instr.size
	);
	char *analize = empty;

	analize = debugger.analyze_instruction(dline, false);
	if(!analize || !(*analize))
		analize = empty;

	size_t alen = strlen(analize);
	if(alen<22) {
		for(size_t i=0; i<22-alen; i++) {
			analize[alen+i] = ' ';
		}
	}
	analize[22] = 0;

	size_t len = strlen(dline);
	if(len<30) {
		for(size_t i=0; i<30-len; i++) {
			dline[len + i] = ' ';
		}
	}

	dline[30] = 0;

	str = dline;
	str = str + " " + analize;

	return str;
};

void CPU::write_log_entry(FILE *_dest, CPULogEntry &_entry)
{
	if(CPULOG_WRITE_TIME) {
		fprintf(_dest, "%010lu ", _entry.time);
	}

	if(CPULOG_WRITE_CSIP) {
		fprintf(_dest, "%04X:%04X ",
				_entry.core.get_CS().sel.value, _entry.core.get_IP());
	}

	if(CPULOG_WRITE_HEX) {
		for(uint j=0; j<CPU_MAX_INSTR_SIZE; j++) {
			if(j<_entry.instr.size) {
				fprintf(_dest, "%02X ", _entry.instr.bytes[j]);
			} else {
				fprintf(_dest, "   ");
			}
		}
	}

	//the instruction
	fprintf(_dest, "%s  ", disasm(_entry).c_str());

	if(CPULOG_WRITE_CORE) {
		fprintf(_dest, "AX=%04X BX=%04X CX=%04X DX=%04X ",
				_entry.core.get_AX(), _entry.core.get_BX(),
				_entry.core.get_CX(), _entry.core.get_DX());
		fprintf(_dest, "SI=%04X DI=%04X ",
				_entry.core.get_SI(), _entry.core.get_DI());
		fprintf(_dest, "ES=%04X DS=%04X SS=%04X ",
				_entry.core.get_ES().sel.value,
				_entry.core.get_DS().sel.value,
				_entry.core.get_SS().sel.value);
	}

	if(CPULOG_WRITE_TIMINGS) {
		fprintf(_dest, "c=%2u,mtx=%2u,p=%2u ", _entry.cycles,
				_entry.bus.get_dram_tx(), _entry.bus.get_cycles_penalty());
	}

	if(USE_PREFETCH_QUEUE && CPULOG_WRITE_PQ) {
		fprintf(_dest, "pq=");
		_entry.bus.write_pq_to_logfile(_dest);
	}

	fprintf(_dest, "\n");
}

void CPU::write_log()
{
	if(!m_log_file) {
		return;
	}
	uint idx;
	if(m_log_size<CPULOG_MAX_SIZE) {
		idx = 0;
	} else {
		idx = m_log_idx; //the index is already advanced to the next element
	}
	PINFOF(LOG_V0, LOG_CPU, "writing log ... ");
	for(uint i=0; i<m_log_size; i++) {
		write_log_entry(m_log_file, m_log[idx]);
		idx = (idx+1)%CPULOG_MAX_SIZE;
	}
	PINFOF(LOG_V0, LOG_CPU, "done\n");
}
