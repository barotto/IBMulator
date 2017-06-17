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
#include "logger.h"
#include "debugger.h"
#include "hardware/cpu.h"
#include <cstring>

#define LOG_O32_BIT 30
#define LOG_A32_BIT 31

CPULogger::CPULogger()
:
m_log_idx(0),
m_log_size(0),
m_iret_address(0),
m_log_file(nullptr)
{
}

CPULogger::~CPULogger()
{
	close_file();
}

void CPULogger::add_entry(
		uint64_t _time,
		const Instruction &_instr,
		const CPUState &_state,
		const CPUCore &_core,
		const CPUBus &_bus,
		const CPUCycles &_cycles)
{
	//don't log outside fixed boundaries
	if(_instr.cseip<CPULOG_START_ADDR || _instr.cseip>CPULOG_END_ADDR) {
		return;
	}

	m_log_size = std::min(m_log_size+1, CPULOG_MAX_SIZE);
	m_log[m_log_idx].time = _time;
	m_log[m_log_idx].instr = _instr;
	m_log[m_log_idx].state = _state;
	m_log[m_log_idx].core = _core;
	m_log[m_log_idx].bus = _bus;
	m_log[m_log_idx].cycles = _cycles;

	int opcode_idx;
	if(CPULOG_COUNTERS) {
		opcode_idx = get_opcode_index(_instr);
		m_global_counters[opcode_idx] += 1;
	}

	if(m_log_file && (CPULOG_LOG_INTS || m_iret_address==0 || m_iret_address==_instr.cseip)) {
		m_iret_address = 0;
		write_entry(m_log_file, m_log[m_log_idx]);
		if(CPULOG_COUNTERS) {
			m_file_counters[opcode_idx] += 1;
		}
	}
	m_log_idx = (m_log_idx+1) % CPULOG_MAX_SIZE;
}

int CPULogger::get_opcode_index(const Instruction &_instr)
{
	int idx = int(_instr.opcode) << 4;
	switch(_instr.opcode) {
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x8C:
		case 0x8E:
		case 0x8F:
		case 0xC0:
		case 0xC1:
		case 0xC6:
		case 0xC7:
		case 0xD0:
		case 0xD1:
		case 0xD2:
		case 0xD3:
		case 0xF6:
		case 0xF7:
		case 0xFE:
		case 0xFF:
		case 0xF00:
		case 0xF01:
		case 0xFBA:
			idx += _instr.modrm.n;
			break;
		default:
			break;
	}
	idx |= int(_instr.op32) << LOG_O32_BIT;
	idx |= int(_instr.addr32) << LOG_A32_BIT;
	return idx;
}

void CPULogger::open_file(const std::string _filename)
{
	close_file();
	m_log_file = fopen(_filename.c_str(), "w");
	if(!m_log_file) {
		throw std::exception();
	}
	m_log_filename = _filename;
}

void CPULogger::close_file()
{
	if(!m_log_file) {
		return;
	}
	fclose(m_log_file);
	m_log_file = nullptr;
	if(CPULOG_COUNTERS) {
		write_counters(m_log_filename + ".cnt", m_file_counters);
		reset_file_counters();
	}
	m_log_filename.clear();
}

void CPULogger::set_iret_address(uint32_t _address)
{
	if(m_log_file && m_iret_address==0) {
		m_iret_address = _address;
	}
}

void CPULogger::reset_iret_address()
{
	m_iret_address = 0;
}

void CPULogger::reset_global_counters()
{
	m_global_counters.clear();
}

void CPULogger::reset_file_counters()
{
	m_file_counters.clear();
}

#define PCPULOG(_dest_, _format_, ...) { \
	if(fprintf(_dest_, _format_, ## __VA_ARGS__) < 0) \
		return -1; \
}

int CPULogger::write_segreg(FILE *_dest, const CPUCore &_core, const SegReg &_segreg, const char *_name)
{
	PCPULOG(_dest, "%s=[%04X", _name, _segreg.sel.value);
	PCPULOG(_dest, " %s ", _segreg.desc.segment?"S":"s");
	if(_core.is_rmode() || _segreg.desc.segment) {
		PCPULOG(_dest,
			(CPU_FAMILY <= CPU_286)?"%06X-%04X":"%08X-%08X",
			_segreg.desc.base, _segreg.desc.limit);
	}
	PCPULOG(_dest, " %02X ", _segreg.desc.get_AR());
	if(CPU_FAMILY >= CPU_286 && (_core.is_rmode() || _segreg.desc.segment)) {
		PCPULOG(_dest, "%s%s",
			_segreg.desc.big?"B":"b",
			_segreg.desc.granularity?"G":"g");

	}
	PCPULOG(_dest, "%s] ", _segreg.desc.valid?"V":"v");
	return 0;
}

int CPULogger::write_entry(FILE *_dest, CPULogEntry &_entry)
{
	if(CPULOG_WRITE_TIME) {
		if(fprintf(_dest, "%010lu ", _entry.time) < 0)
			return -1;
	}

	if(CPULOG_WRITE_CSEIP) {
		if(CPU_FAMILY >= CPU_386) {
			if(fprintf(_dest, "%04X:%08X ",
					_entry.core.get_CS().sel.value, _entry.core.get_EIP()) < 0)
				return -1;
		} else {
			if(fprintf(_dest, "%04X:%04X ",
					_entry.core.get_CS().sel.value, _entry.core.get_EIP()&0xFFFF) < 0)
				return -1;
		}
	}

	if(CPULOG_WRITE_HEX) {
		for(uint j=0; j<CPU_MAX_INSTR_SIZE; j++) {
			if(j<_entry.instr.size) {
				if(fprintf(_dest, "%02X ", _entry.instr.bytes[j]) < 0)
					return -1;
			} else {
				if(fprintf(_dest, "   ") < 0)
					return -1;
			}
		}
	}

	//the instruction
	if(fprintf(_dest, "%s  ", disasm(_entry).c_str()) < 0)
		return -1;

	if(CPULOG_WRITE_STATE) {
		if(fprintf(_dest, "SE=%d,SM=%d,SA=%d ",
				_entry.state.pending_event,
				_entry.state.event_mask,
				_entry.state.async_event
				) < 0)
			return -1;
	}

	if(CPULOG_WRITE_CORE) {
		if(CPU_FAMILY >= CPU_386) {
			if(fprintf(_dest, "EF=%05X ", _entry.core.get_EFLAGS(FMASK_EFLAGS)) < 0)
				return -1;
			if(fprintf(_dest, "EAX=%08X EBX=%08X ECX=%08X EDX=%08X ",
					_entry.core.get_EAX(), _entry.core.get_EBX(),
					_entry.core.get_ECX(), _entry.core.get_EDX()) < 0)
				return -1;
			if(fprintf(_dest, "ESI=%08X EDI=%08X EBP=%08X ESP=%08X ",
					_entry.core.get_ESI(), _entry.core.get_EDI(),
					_entry.core.get_EBP(), _entry.core.get_ESP()) < 0)
				return -1;
			if(CPULOG_WRITE_SEGREGS) {
				write_segreg(_dest, _entry.core, _entry.core.get_CS(), "CS");
				write_segreg(_dest, _entry.core, _entry.core.get_ES(), "ES");
				write_segreg(_dest, _entry.core, _entry.core.get_DS(), "DS");
				write_segreg(_dest, _entry.core, _entry.core.get_SS(), "SS");
				write_segreg(_dest, _entry.core, _entry.core.get_FS(), "FS");
				write_segreg(_dest, _entry.core, _entry.core.get_GS(), "GS");
			} else {
				if(fprintf(_dest, "ES=%04X DS=%04X SS=%04X FS=%04X GS=%04X ",
						_entry.core.get_ES().sel.value,
						_entry.core.get_DS().sel.value,
						_entry.core.get_SS().sel.value,
						_entry.core.get_FS().sel.value,
						_entry.core.get_GS().sel.value) < 0)
					return -1;
			}
			if(fprintf(_dest, "CR0=PE:%d,TS:%d,PG:%d ",
					bool(_entry.core.get_CR0(CR0MASK_PE)),
					bool(_entry.core.get_CR0(CR0MASK_TS)),
					bool(_entry.core.get_CR0(CR0MASK_PG))) < 0)
				return -1;
			if(fprintf(_dest, "CR2=%08X CR3=%08X ",
					_entry.core.ctl_reg(2),
					_entry.core.ctl_reg(3)) < 0)
				return -1;
		} else {
			if(fprintf(_dest, "F=%04X ", _entry.core.get_FLAGS(FMASK_FLAGS)) < 0)
				return -1;
			if(fprintf(_dest, "AX=%04X BX=%04X CX=%04X DX=%04X ",
					_entry.core.get_AX(), _entry.core.get_BX(),
					_entry.core.get_CX(), _entry.core.get_DX()) < 0)
				return -1;
			if(fprintf(_dest, "SI=%04X DI=%04X BP=%04X SP=%04X ",
					_entry.core.get_SI(), _entry.core.get_DI(),
					_entry.core.get_BP(), _entry.core.get_SP()) < 0)
				return -1;
			if(CPULOG_WRITE_SEGREGS) {
				write_segreg(_dest, _entry.core, _entry.core.get_CS(), "CS");
				write_segreg(_dest, _entry.core, _entry.core.get_ES(), "ES");
				write_segreg(_dest, _entry.core, _entry.core.get_DS(), "DS");
				write_segreg(_dest, _entry.core, _entry.core.get_SS(), "SS");
			} else {
				if(fprintf(_dest, "ES=%04X DS=%04X SS=%04X ",
						_entry.core.get_ES().sel.value,
						_entry.core.get_DS().sel.value,
						_entry.core.get_SS().sel.value) < 0)
					return -1;
			}
			if(fprintf(_dest, "MSW=PE:%d,TS:%d ",
					bool(_entry.core.get_CR0(CR0MASK_PE)),
					bool(_entry.core.get_CR0(CR0MASK_TS))) < 0)
				return -1;
		}
	}

	if(CPULOG_WRITE_TIMINGS) {
		if(fprintf(_dest, "c=%2d(%2d,%2d,%2d,%2d,%2d,%2d)(b=%d,%d,%d),m=%2d ",
				// cpu
				_entry.cycles.sum(),
				_entry.cycles.eu,
				_entry.cycles.bu,
				_entry.cycles.decode,
				_entry.cycles.io,
				_entry.cycles.bus,
				_entry.cycles.refresh,
				// bus
				_entry.bus.pipelined_mem_cycles(),
				_entry.bus.pipelined_fetch_cycles(),
				_entry.bus.cycles_ahead(),
				// mem transfers
				_entry.bus.mem_tx_cycles()) < 0)
			return -1;
	}

	if(USE_PREFETCH_QUEUE && CPULOG_WRITE_PQ) {
		if(fprintf(_dest, "pq=") < 0)
			return -1;
		if(_entry.bus.write_pq_to_logfile(_dest) < 0)
			return -1;
	}

	if(fprintf(_dest, "\n") < 0)
		return -1;

	return 0;
}

const std::string & CPULogger::disasm(CPULogEntry &_log_entry)
{
	CPUDebugger debugger;

	static std::string str;
	str = "";

	static char empty[23] = { 32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,32,0 };

	char dline[200];
	debugger.disasm(dline, 200u,
		_log_entry.instr.cseip, _log_entry.instr.eip, nullptr,
		_log_entry.instr.bytes, _log_entry.instr.size,
		_log_entry.core.get_CS().desc.def
	);
	char *analize = empty;

	analize = debugger.analyze_instruction(dline, &_log_entry.core, nullptr,
			debugger.last_disasm_opsize());

	if(!analize || !(*analize)) {
		analize = empty;
	}

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

void CPULogger::dump(const std::string _filename)
{
	FILE *file = fopen(_filename.c_str(), "w");
	if(!file) {
		PERRF(LOG_CPU, "error opening '%s' for writing\n", _filename.c_str());
		return;
	}

	uint idx;
	if(m_log_size<CPULOG_MAX_SIZE) {
		idx = 0;
	} else {
		idx = m_log_idx; //the index is already advanced to the next element
	}
	PINFOF(LOG_V0, LOG_CPU, "writing log to '%s' ... ", _filename.c_str());
	for(uint i=0; i<m_log_size; i++) {
		if(write_entry(file, m_log[idx]) < 0) {
			PERRF(LOG_CPU, "error writing to file\n");
			break;
		}
		idx = (idx+1)%CPULOG_MAX_SIZE;
	}
	PINFOF(LOG_V0, LOG_CPU, "done\n");
	fclose(file);

	if(CPULOG_COUNTERS) {
		write_counters(_filename + ".cnt", m_global_counters);
	}
}

static std::vector<std::pair<int,const char*>> oplist = {
	{ 0x00000,"ADD Eb,Gb" },
	{ 0x00010,"ADD Ev,Gv" },
	{ 0x00020,"ADD Gb,Eb" },
	{ 0x00030,"ADD Gv,Ev" },
	{ 0x00040,"ADD AL,Ib" },
	{ 0x00050,"ADD eAX,Iv" },
	{ 0x00060,"PUSH ES" },
	{ 0x00070,"POP ES" },
	{ 0x00080,"OR Eb,Gb" },
	{ 0x00090,"OR Ev,Gv" },
	{ 0x000A0,"OR Gb,Eb" },
	{ 0x000B0,"OR Gv,Ev" },
	{ 0x000C0,"OR AL,Ib" },
	{ 0x000D0,"OR eAX,Iv" },
	{ 0x000E0,"PUSH CS" },
	{ 0x00100,"ADC Eb,Gb" },
	{ 0x00110,"ADC Ev,Gv" },
	{ 0x00120,"ADC Gb,Eb" },
	{ 0x00130,"ADC Gv,Ev" },
	{ 0x00140,"ADC AL,Ib" },
	{ 0x00150,"ADC eAX,Iv" },
	{ 0x00160,"PUSH SS" },
	{ 0x00170,"POP SS" },
	{ 0x00180,"SBB Eb,Gb" },
	{ 0x00190,"SBB Ev,Gv" },
	{ 0x001A0,"SBB Gb,Eb" },
	{ 0x001B0,"SBB Gv,Ev" },
	{ 0x001C0,"SBB AL,Ib" },
	{ 0x001D0,"SBB eAX,Iv" },
	{ 0x001E0,"PUSH DS" },
	{ 0x001F0,"POP DS" },
	{ 0x00200,"AND Eb,Gb" },
	{ 0x00210,"AND Ev,Gv" },
	{ 0x00220,"AND Gb,Eb" },
	{ 0x00230,"AND Gv,Ev" },
	{ 0x00240,"AND AL,Ib" },
	{ 0x00250,"AND eAX,Iv" },
	{ 0x00270,"DAA" },
	{ 0x00280,"SUB Eb,Gb" },
	{ 0x00290,"SUB Ev,Gv" },
	{ 0x002A0,"SUB Gb,Eb" },
	{ 0x002B0,"SUB Gv,Ev" },
	{ 0x002C0,"SUB AL,Ib" },
	{ 0x002D0,"SUB eAX,Iv" },
	{ 0x002F0,"DAS" },
	{ 0x00300,"XOR Eb,Gb" },
	{ 0x00310,"XOR Ev,Gv" },
	{ 0x00320,"XOR Gb,Eb" },
	{ 0x00330,"XOR Gv,Ev" },
	{ 0x00340,"XOR AL,Ib" },
	{ 0x00350,"XOR eAX,Iv" },
	{ 0x00370,"AAA" },
	{ 0x00380,"CMP Eb,Gb" },
	{ 0x00390,"CMP Ev,Gv" },
	{ 0x003A0,"CMP Gb,Eb" },
	{ 0x003B0,"CMP Gv,Ev" },
	{ 0x003C0,"CMP AL,Ib" },
	{ 0x003D0,"CMP eAX,Iv" },
	{ 0x003F0,"AAS" },
	{ 0x00400,"INC eAX" },
	{ 0x00410,"INC eCX" },
	{ 0x00420,"INC eDX" },
	{ 0x00430,"INC eBX" },
	{ 0x00440,"INC eSP" },
	{ 0x00450,"INC eBP" },
	{ 0x00460,"INC eSI" },
	{ 0x00470,"INC eDI" },
	{ 0x00480,"DEC eAX" },
	{ 0x00490,"DEC eCX" },
	{ 0x004A0,"DEC eDX" },
	{ 0x004B0,"DEC eBX" },
	{ 0x004C0,"DEC eSP" },
	{ 0x004D0,"DEC eBP" },
	{ 0x004E0,"DEC eSI" },
	{ 0x004F0,"DEC eDI" },
	{ 0x00500,"PUSH eAX" },
	{ 0x00510,"PUSH eCX" },
	{ 0x00520,"PUSH eDX" },
	{ 0x00530,"PUSH eBX" },
	{ 0x00540,"PUSH eSP" },
	{ 0x00550,"PUSH eBP" },
	{ 0x00560,"PUSH eSI" },
	{ 0x00570,"PUSH eDI" },
	{ 0x00580,"POP eAX" },
	{ 0x00590,"POP eCX" },
	{ 0x005A0,"POP eDX" },
	{ 0x005B0,"POP eBX" },
	{ 0x005C0,"POP eSP" },
	{ 0x005D0,"POP eBP" },
	{ 0x005E0,"POP eSI" },
	{ 0x005F0,"POP eDI" },
	{ 0x00600,"PUSHA/PUSHAD" },
	{ 0x00610,"POPA/POPAD" },
	{ 0x00620,"BOUND Gv,Ma" },
	{ 0x00630,"ARPL Ew,Gw" },
	{ 0x00680,"PUSH Iv" },
	{ 0x00690,"IMUL Gv,Ev,Iv" },
	{ 0x006A0,"PUSH Ib" },
	{ 0x006B0,"IMUL Gv,Ev,Ib" },
	{ 0x006C0,"INSB" },
	{ 0x006D0,"INSW/D" },
	{ 0x006E0,"OUTSB" },
	{ 0x006F0,"OUTSW/D" },
	{ 0x00700,"JO Jb" },
	{ 0x00710,"JNO Jb" },
	{ 0x00720,"JC Jb" },
	{ 0x00730,"JNC Jb" },
	{ 0x00740,"JE Jb" },
	{ 0x00750,"JNE Jb" },
	{ 0x00760,"JBE Jb" },
	{ 0x00770,"JA Jb" },
	{ 0x00780,"JS Jb" },
	{ 0x00790,"JNS Jb" },
	{ 0x007A0,"JPE Jb" },
	{ 0x007B0,"JPO Jb" },
	{ 0x007C0,"JL Jb" },
	{ 0x007D0,"JNL Jb" },
	{ 0x007E0,"JLE Jb" },
	{ 0x007F0,"JNLE Jb" },
	{ 0x00800,"ADD Eb,Ib" },
	{ 0x00801,"OR Eb,Ib" },
	{ 0x00802,"ADC Eb,Ib" },
	{ 0x00803,"SBB Eb,Ib" },
	{ 0x00804,"AND Eb,Ib" },
	{ 0x00805,"SUB Eb,Ib" },
	{ 0x00806,"XOR Eb,Ib" },
	{ 0x00807,"CMP Eb,Ib" },
	{ 0x00810,"ADD Ev,Iv" },
	{ 0x00811,"OR Ev,Iv" },
	{ 0x00812,"ADC Ev,Iv" },
	{ 0x00813,"SBB Ev,Iv" },
	{ 0x00814,"AND Ev,Iv" },
	{ 0x00815,"SUB Ev,Iv" },
	{ 0x00816,"XOR Ev,Iv" },
	{ 0x00817,"CMP Ev,Iv" },
	{ 0x00820,"ADD Eb,Ib" },
	{ 0x00821,"OR Eb,Ib" },
	{ 0x00822,"ADC Eb,Ib" },
	{ 0x00823,"SBB Eb,Ib" },
	{ 0x00824,"AND Eb,Ib" },
	{ 0x00825,"SUB Eb,Ib" },
	{ 0x00826,"XOR Eb,Ib" },
	{ 0x00827,"CMP Eb,Ib" },
	{ 0x00830,"ADD Ev,Ib" },
	{ 0x00831,"OR Ev,Ib" },
	{ 0x00832,"ADC Ev,Ib" },
	{ 0x00833,"SBB Ev,Ib" },
	{ 0x00834,"AND Ev,Ib" },
	{ 0x00835,"SUB Ev,Ib" },
	{ 0x00836,"XOR Ev,Ib" },
	{ 0x00837,"CMP Ev,Ib" },
	{ 0x00840,"TEST Eb,Gb" },
	{ 0x00850,"TEST Ev,Gv" },
	{ 0x00860,"XCHG Eb,Gb" },
	{ 0x00870,"XCHG Ev,Gv" },
	{ 0x00880,"MOV Eb,Gb" },
	{ 0x00890,"MOV Ev,Gv" },
	{ 0x008A0,"MOV Gb,Eb" },
	{ 0x008B0,"MOV Gv,Ev" },
	{ 0x008C0,"MOV Ew,ES" },
	{ 0x008C1,"MOV Ew,CS" },
	{ 0x008C2,"MOV Ew,SS" },
	{ 0x008C3,"MOV Ew,DS" },
	{ 0x008C4,"MOV Ew,FS" },
	{ 0x008C5,"MOV Ew,GS" },
	{ 0x008D0,"LEA Gv,M" },
	{ 0x008E0,"MOV ES,Ew" },
	{ 0x008E2,"MOV SS,Ew" },
	{ 0x008E3,"MOV DS,Ew" },
	{ 0x008E4,"MOV FS,Ew" },
	{ 0x008E5,"MOV GS,Ew" },
	{ 0x008F0,"POP Ev" },
	{ 0x00900,"NOP" },
	{ 0x00910,"XCHG eAX,eCX" },
	{ 0x00920,"XCHG eAX,eDX" },
	{ 0x00930,"XCHG eAX,eBX" },
	{ 0x00940,"XCHG eAX,eSP" },
	{ 0x00950,"XCHG eAX,eBP" },
	{ 0x00960,"XCHG eAX,eSI" },
	{ 0x00970,"XCHG eAX,eDI" },
	{ 0x00980,"CBW/CWDE" },
	{ 0x00990,"CWD/CDQ" },
	{ 0x009A0,"CALL Ap" },
	{ 0x009B0,"WAIT" },
	{ 0x009C0,"PUSHF/PUSHFD Fv" },
	{ 0x009D0,"POPF/POPFD Fv" },
	{ 0x009E0,"SAHF" },
	{ 0x009F0,"LAHF" },
	{ 0x00A00,"MOV AL,Ob" },
	{ 0x00A10,"MOV eAX,Ov" },
	{ 0x00A20,"MOV Ob,AL" },
	{ 0x00A30,"MOV Ov,eAX" },
	{ 0x00A40,"MOVSB" },
	{ 0x00A50,"MOVSW/D" },
	{ 0x00A60,"CMPSB" },
	{ 0x00A70,"CMPSW/D" },
	{ 0x00A80,"TEST AL,Ib" },
	{ 0x00A90,"TEST eAX,Iv" },
	{ 0x00AA0,"STOSB" },
	{ 0x00AB0,"STOSW/D" },
	{ 0x00AC0,"LODSB" },
	{ 0x00AD0,"LODSW/D" },
	{ 0x00AE0,"SCASB" },
	{ 0x00AF0,"SCASW/D" },
	{ 0x00B00,"MOV AL,Ib" },
	{ 0x00B10,"MOV CL,Ib" },
	{ 0x00B20,"MOV DL,Ib" },
	{ 0x00B30,"MOV BL,Ib" },
	{ 0x00B40,"MOV AH,Ib" },
	{ 0x00B50,"MOV CH,Ib" },
	{ 0x00B60,"MOV DH,Ib" },
	{ 0x00B70,"MOV BH,Ib" },
	{ 0x00B80,"MOV eAX,Iv" },
	{ 0x00B90,"MOV eCX,Iv" },
	{ 0x00BA0,"MOV eDX,Iv" },
	{ 0x00BB0,"MOV eBX,Iv" },
	{ 0x00BC0,"MOV eSP,Iv" },
	{ 0x00BD0,"MOV eBP,Iv" },
	{ 0x00BE0,"MOV eSI,Iv" },
	{ 0x00BF0,"MOV eDI,Iv" },
	{ 0x00C00,"ROL Eb,Ib" },
	{ 0x00C01,"ROR Eb,Ib" },
	{ 0x00C02,"RCL Eb,Ib" },
	{ 0x00C03,"RCR Eb,Ib" },
	{ 0x00C04,"SAL Eb,Ib" },
	{ 0x00C05,"SHR Eb,Ib" },
	{ 0x00C06,"SHL Eb,Ib" },
	{ 0x00C07,"SAR Eb,Ib" },
	{ 0x00C10,"ROL Ev,Ib" },
	{ 0x00C11,"ROR Ev,Ib" },
	{ 0x00C12,"RCL Ev,Ib" },
	{ 0x00C13,"RCR Ev,Ib" },
	{ 0x00C14,"SAL Ev,Ib" },
	{ 0x00C15,"SHR Ev,Ib" },
	{ 0x00C16,"SHL Ev,Ib" },
	{ 0x00C17,"SAR Ev,Ib" },
	{ 0x00C20,"RETN Iw" },
	{ 0x00C30,"RETN" },
	{ 0x00C40,"LES Gv,Mp" },
	{ 0x00C50,"LDS Gv,Mp" },
	{ 0x00C60,"MOV Eb,Ib" },
	{ 0x00C70,"MOV Ev,Iv" },
	{ 0x00C80,"ENTER Iw,Ib" },
	{ 0x00C90,"LEAVE" },
	{ 0x00CA0,"RETF Iw" },
	{ 0x00CB0,"RETF" },
	{ 0x00CC0,"INT 3" },
	{ 0x00CD0,"INT Ib" },
	{ 0x00CE0,"INTO" },
	{ 0x00CF0,"IRET/IRETD" },
	{ 0x00D00,"ROL Eb,1" },
	{ 0x00D01,"ROR Eb,1" },
	{ 0x00D02,"RCL Eb,1" },
	{ 0x00D03,"RCR Eb,1" },
	{ 0x00D04,"SAL Eb,1" },
	{ 0x00D05,"SHR Eb,1" },
	{ 0x00D06,"SHL Eb,1" },
	{ 0x00D07,"SAR Eb,1" },
	{ 0x00D10,"ROL Ev,1" },
	{ 0x00D11,"ROR Ev,1" },
	{ 0x00D12,"RCL Ev,1" },
	{ 0x00D13,"RCR Ev,1" },
	{ 0x00D14,"SAL Ev,1" },
	{ 0x00D15,"SHR Ev,1" },
	{ 0x00D16,"SHL Ev,1" },
	{ 0x00D17,"SAR Ev,1" },
	{ 0x00D20,"ROL Eb,CL" },
	{ 0x00D21,"ROR Eb,CL" },
	{ 0x00D22,"RCL Eb,CL" },
	{ 0x00D23,"RCR Eb,CL" },
	{ 0x00D24,"SAL Eb,CL" },
	{ 0x00D25,"SHR Eb,CL" },
	{ 0x00D26,"SHR Eb,CL" },
	{ 0x00D27,"SAR Eb,CL" },
	{ 0x00D30,"ROL Ev,CL" },
	{ 0x00D31,"ROR Ev,CL" },
	{ 0x00D32,"RCL Ev,CL" },
	{ 0x00D33,"RCR Ev,CL" },
	{ 0x00D34,"SAL Ev,CL" },
	{ 0x00D35,"SHR Ev,CL" },
	{ 0x00D36,"SHR Ev,CL" },
	{ 0x00D37,"SAR Ev,CL" },
	{ 0x00D40,"AAM Ib" },
	{ 0x00D50,"AAD Ib" },
	{ 0x00D60,"SALC" },
	{ 0x00D70,"XLATB" },
	{ 0x00D80,"FPU ESC" },
	{ 0x00D90,"FPU ESC" },
	{ 0x00DA0,"FPU ESC" },
	{ 0x00DB0,"FPU ESC" },
	{ 0x00DC0,"FPU ESC" },
	{ 0x00DD0,"FPU ESC" },
	{ 0x00DE0,"FPU ESC" },
	{ 0x00DF0,"FPU ESC" },
	{ 0x00E00,"LOOPNZ Jb" },
	{ 0x00E10,"LOOPZ Jb" },
	{ 0x00E20,"LOOP Jb" },
	{ 0x00E30,"JCXZ/JECX Jb" },
	{ 0x00E40,"IN AL,Ib" },
	{ 0x00E50,"IN eAX,Ib" },
	{ 0x00E60,"OUT Ib,AL" },
	{ 0x00E70,"OUT Ib,eAX" },
	{ 0x00E80,"CALL Jv" },
	{ 0x00E90,"JMP Jv" },
	{ 0x00EA0,"JMPF Ap" },
	{ 0x00EB0,"JMP Jb" },
	{ 0x00EC0,"IN AL,DX" },
	{ 0x00ED0,"IN eAX,DX" },
	{ 0x00EE0,"OUT DX,AL" },
	{ 0x00EF0,"OUT DX,eAX" },
	{ 0x00F10,"INT 1" },
	{ 0x00F40,"HLT" },
	{ 0x00F50,"CMC" },
	{ 0x00F60,"TEST Eb,Ib" },
	{ 0x00F61,"TEST Eb,Ib" },
	{ 0x00F62,"NOT Eb" },
	{ 0x00F63,"NEG Eb" },
	{ 0x00F64,"MUL Eb" },
	{ 0x00F65,"IMUL Eb" },
	{ 0x00F66,"DIV Eb" },
	{ 0x00F67,"IDIV Eb" },
	{ 0x00F70,"TEST Ev,Iv" },
	{ 0x00F71,"TEST Ev,Iv" },
	{ 0x00F72,"NOT Ev" },
	{ 0x00F73,"NEG Ev" },
	{ 0x00F74,"MUL Ev" },
	{ 0x00F75,"IMUL Ev" },
	{ 0x00F76,"DIV Ev" },
	{ 0x00F77,"IDIV Ev" },
	{ 0x00F80,"CLC" },
	{ 0x00F90,"STC" },
	{ 0x00FA0,"CLI" },
	{ 0x00FB0,"STI" },
	{ 0x00FC0,"CLD" },
	{ 0x00FD0,"STD" },
	{ 0x00FE0,"INC Eb" },
	{ 0x00FE1,"DEC Eb" },
	{ 0x00FF0,"INC Ev" },
	{ 0x00FF1,"DEC Ev" },
	{ 0x00FF2,"CALL Ev" },
	{ 0x00FF3,"CALLF Mp" },
	{ 0x00FF4,"JMP Ev" },
	{ 0x00FF5,"JMPF Mp" },
	{ 0x00FF6,"PUSH Ev" },
	{ 0x0F000,"SLDT Ew" },
	{ 0x0F001,"STR Ew" },
	{ 0x0F002,"LLDT Ew" },
	{ 0x0F003,"LTR Ew" },
	{ 0x0F004,"VERR Ew" },
	{ 0x0F005,"VERW Ew" },
	{ 0x0F010,"SGDT Ms" },
	{ 0x0F011,"SIDT Ms" },
	{ 0x0F012,"LGDT Ms" },
	{ 0x0F013,"LIDT Ms" },
	{ 0x0F014,"SMSW Ew" },
	{ 0x0F016,"LMSW Ew" },
	{ 0x0F020,"LAR Gv,Ew" },
	{ 0x0F030,"LSL Gv,Ew" },
	{ 0x0F050,"286 LOADALL" },
	{ 0x0F060,"CLTS" },
	{ 0x0F070,"386 LOADALL" },
	{ 0x0F200,"MOV Rd,Cd" },
	{ 0x0F210,"MOV Rd,Dd" },
	{ 0x0F220,"MOV Cd,Rd" },
	{ 0x0F230,"MOV Dd,Rd" },
	{ 0x0F240,"MOV Rd,Td" },
	{ 0x0F260,"MOV Td,Rd" },
	{ 0x0F800,"JO Jv" },
	{ 0x0F810,"JNO Jv" },
	{ 0x0F820,"JC Jv" },
	{ 0x0F830,"JNC Jv" },
	{ 0x0F840,"JE Jv" },
	{ 0x0F850,"JNE Jv" },
	{ 0x0F860,"JBE Jv" },
	{ 0x0F870,"JA Jv" },
	{ 0x0F880,"JS Jv" },
	{ 0x0F890,"JNS Jv" },
	{ 0x0F8A0,"JPE Jv" },
	{ 0x0F8B0,"JPO Jv" },
	{ 0x0F8C0,"JL Jv" },
	{ 0x0F8D0,"JNL Jv" },
	{ 0x0F8E0,"JLE Jv" },
	{ 0x0F8F0,"JNLE Jv" },
	{ 0x0F900,"SETO Eb" },
	{ 0x0F910,"SETNO Eb" },
	{ 0x0F920,"SETB Eb" },
	{ 0x0F930,"SETNB Eb" },
	{ 0x0F940,"SETE Eb" },
	{ 0x0F950,"SETNE Eb" },
	{ 0x0F960,"SETBE Eb" },
	{ 0x0F970,"SETNBE Eb" },
	{ 0x0F980,"SETS Eb" },
	{ 0x0F990,"SETNS Eb" },
	{ 0x0F9A0,"SETP Eb" },
	{ 0x0F9B0,"SETNP Eb" },
	{ 0x0F9C0,"SETL Eb" },
	{ 0x0F9D0,"SETNL Eb" },
	{ 0x0F9E0,"SETLE Eb" },
	{ 0x0F9F0,"SETNLE Eb" },
	{ 0x0FA00,"PUSH FS" },
	{ 0x0FA10,"POP FS" },
	{ 0x0FA30,"BT Ev,Gv" },
	{ 0x0FA40,"SHLD Ev,Gv,Ib" },
	{ 0x0FA50,"SHLD Ev,Gv,CL" },
	{ 0x0FA80,"PUSH GS" },
	{ 0x0FA90,"POP GS" },
	{ 0x0FAB0,"BTS Ev,Gv" },
	{ 0x0FAC0,"SHRD Ev,Gv,Ib" },
	{ 0x0FAD0,"SHRD Ev,Gv,CL" },
	{ 0x0FAF0,"IMUL Gv,Ev" },
	{ 0x0FB20,"LSS Gv,Mp" },
	{ 0x0FB30,"BTR Ev,Gv" },
	{ 0x0FB40,"LFS Gv,Mp" },
	{ 0x0FB50,"LGS Gv,Mp" },
	{ 0x0FB60,"MOVZX Gv,Eb" },
	{ 0x0FB70,"MOVZX Gv,Ew" },
	{ 0x0FBA4,"BT Ev,Ib" },
	{ 0x0FBA5,"BTS Ev,Ib" },
	{ 0x0FBA6,"BTR Ev,Ib" },
	{ 0x0FBA7,"BTC Ev,Ib" },
	{ 0x0FBB0,"BTC Ev,Gv" },
	{ 0x0FBC0,"BSF Gv,Ev" },
	{ 0x0FBD0,"BSR Gv,Ev" },
	{ 0x0FBE0,"MOVSX Gv,Eb" },
	{ 0x0FBF0,"MOVSX Gv,Ew" }
};

void CPULogger::write_counters(const std::string _filename, std::map<int,uint64_t> &_cnt)
{
	FILE *file = fopen(_filename.c_str(), "w");
	if(!file) {
		PERRF(LOG_CPU, "error opening '%s' for writing\n", _filename.c_str());
		return;
	}
	uint64_t totals[4] = {0,0,0,0};
	PINFOF(LOG_V0, LOG_CPU, "writing counters to '%s' ... ", _filename.c_str());
	if(fprintf(file, "opcode      op16 ad16     op16 ad32     op32 ad16     op32 ad32  mnemonic\n\n") > 0) {
		for(auto op : oplist) {
			int code = op.first;
			const char* mnemonic = op.second;
			uint64_t o16a16 = _cnt[code];
			uint64_t o32a16 = _cnt[code|(1<<LOG_O32_BIT)];
			uint64_t o16a32 = _cnt[code|(1<<LOG_A32_BIT)];
			uint64_t o32a32 = _cnt[(code|(1<<LOG_O32_BIT))|(1<<LOG_A32_BIT)];
			if(fprintf(file, "0x%05X: %12lu  %12lu  %12lu  %12lu  %s\n", code,
					o16a16, o16a32, o32a16, o32a32, mnemonic) < 0)
			{
				PERRF(LOG_CPU, "error writing to file\n");
				break;
			}
			totals[0] += o16a16;
			totals[1] += o16a32;
			totals[2] += o32a16;
			totals[3] += o32a32;
		}
		fprintf(file, "\n totals: %12lu  %12lu  %12lu  %12lu\n",
				totals[0], totals[1], totals[2], totals[3]);
	}
	PINFOF(LOG_V0, LOG_CPU, "done\n");
	fclose(file);
}
