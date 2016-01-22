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
#include <cstring>


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
		unsigned _cycles)
{
	//don't log outside fixed boundaries
	if(_instr.csip<CPULOG_START_ADDR || _instr.csip>CPULOG_END_ADDR) {
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

	if(m_log_file && (CPULOG_LOG_INTS || m_iret_address==0 || m_iret_address==_instr.csip)) {
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
		case 0xF000:
		case 0xF001:
			return idx + _instr.modrm.n;
			break;
		default:
			break;
	}
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

int CPULogger::write_entry(FILE *_dest, CPULogEntry &_entry)
{
	if(CPULOG_WRITE_TIME) {
		if(fprintf(_dest, "%010lu ", _entry.time) < 0)
			return -1;
	}

	if(CPULOG_WRITE_CSIP) {
		if(fprintf(_dest, "%04X:%04X ",
				_entry.core.get_CS().sel.value, _entry.core.get_IP()) < 0)
			return -1;
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
		if(fprintf(_dest, "M=%d ",
				_entry.state.event_mask) < 0)
			return -1;
	}

	if(CPULOG_WRITE_CORE) {
		if(fprintf(_dest, "AX=%04X BX=%04X CX=%04X DX=%04X ",
				_entry.core.get_AX(), _entry.core.get_BX(),
				_entry.core.get_CX(), _entry.core.get_DX()) < 0)
			return -1;
		if(fprintf(_dest, "SI=%04X DI=%04X BP=%04X SP=%04X ",
				_entry.core.get_SI(), _entry.core.get_DI(),
				_entry.core.get_BP(), _entry.core.get_SP()) < 0)
			return -1;
		if(fprintf(_dest, "ES=%04X DS=%04X SS=%04X ",
				_entry.core.get_ES().sel.value,
				_entry.core.get_DS().sel.value,
				_entry.core.get_SS().sel.value) < 0)
			return -1;
	}

	if(CPULOG_WRITE_TIMINGS) {
		if(fprintf(_dest, "c=%2u(b=%u,%u,%u),m=%2u ",
				_entry.cycles,
				_entry.bus.get_mem_cycles(), _entry.bus.get_fetch_cycles(),
				_entry.bus.get_cycles_ahead(),
				_entry.bus.get_dram_tx()) < 0)
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
		_log_entry.instr.csip, _log_entry.instr.ip,	nullptr,
		_log_entry.instr.bytes, _log_entry.instr.size
	);
	char *analize = empty;

	analize = debugger.analyze_instruction(dline, false, &_log_entry.core, nullptr);
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

void CPULogger::dump(const std::string _filename)
{
	//C stdlib, because i hate C++ streams with a passion
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

static std::vector<int> oplist = {
	0x000, 0x010, 0x020, 0x030, 0x040, 0x050, 0x060, 0x070, 0x080, 0x090, 0x0A0, 0x0B0, 0x0C0, 0x0D0, 0x0E0,
	0x100, 0x110, 0x120, 0x130, 0x140, 0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0, 0x1B0, 0x1C0, 0x1D0, 0x1E0, 0x1F0,
	0x200, 0x210, 0x220, 0x230, 0x240, 0x250,        0x270, 0x280, 0x290, 0x2A0, 0x2B0, 0x2C0, 0x2D0,        0x2F0,
	0x300, 0x310, 0x320, 0x330, 0x340, 0x350,        0x370, 0x380, 0x390, 0x3A0, 0x3B0, 0x3C0, 0x3D0,        0x3F0,
	0x400, 0x410, 0x420, 0x430, 0x440, 0x450, 0x460, 0x470, 0x480, 0x490, 0x4A0, 0x4B0, 0x4C0, 0x4D0, 0x4E0, 0x4F0,
	0x500, 0x510, 0x520, 0x530, 0x540, 0x550, 0x560, 0x570, 0x580, 0x590, 0x5A0, 0x5B0, 0x5C0, 0x5D0, 0x5E0, 0x5F0,
	0x600, 0x610, 0x620, 0x630, 0x640, 0x650, 0x660, 0x670, 0x680, 0x690, 0x6A0, 0x6B0, 0x6C0, 0x6D0, 0x6E0, 0x6F0,
	0x700, 0x710, 0x720, 0x730, 0x740, 0x750, 0x760, 0x770, 0x780, 0x790, 0x7A0, 0x7B0, 0x7C0, 0x7D0, 0x7E0, 0x7F0,

	0x800,0x801,0x802,0x803,0x804,0x805,0x806,0x807,
	0x810,0x811,0x812,0x813,0x814,0x815,0x816,0x817,
	0x820,0x821,0x822,0x823,0x824,0x825,0x826,0x827,
	0x830,0x831,0x832,0x833,0x834,0x835,0x836,0x837,

	0x840, 0x850, 0x860, 0x870, 0x880, 0x890, 0x8A0, 0x8B0,

	0x8C0, 0x8C1, 0x8C2, 0x8C3,

	0x8D0,

	0x8E0,0x8E2,0x8E3,

	0x8F0,

	0x900, 0x910, 0x920, 0x930, 0x940, 0x950, 0x960, 0x970, 0x980, 0x990, 0x9A0, 0x9B0, 0x9C0, 0x9D0, 0x9E0, 0x9F0,
	0xA00, 0xA10, 0xA20, 0xA30, 0xA40, 0xA50, 0xA60, 0xA70, 0xA80, 0xA90, 0xAA0, 0xAB0, 0xAC0, 0xAD0, 0xAE0, 0xAF0,
	0xB00, 0xB10, 0xB20, 0xB30, 0xB40, 0xB50, 0xB60, 0xB70, 0xB80, 0xB90, 0xBA0, 0xBB0, 0xBC0, 0xBD0, 0xBE0, 0xBF0,

	0xC00, 0xC01, 0xC02, 0xC03, 0xC04, 0xC05, 0xC06, 0xC07,
	0xC10, 0xC11, 0xC12, 0xC13, 0xC14, 0xC15, 0xC16, 0xC17,

	0xC20, 0xC30, 0xC40, 0xC50, 0xC60, 0xC70, 0xC80, 0xC90, 0xCA0, 0xCB0, 0xCC0, 0xCD0, 0xCE0, 0xCF0,

	0xD00, 0xD01, 0xD02, 0xD03, 0xD04, 0xD05, 0xD06, 0xD07,
	0xD10, 0xD11, 0xD12, 0xD13, 0xD14, 0xD15, 0xD16, 0xD17,
	0xD20, 0xD21, 0xD22, 0xD23, 0xD24, 0xD25, 0xD26, 0xD27,
	0xD30, 0xD31, 0xD32, 0xD33, 0xD34, 0xD35, 0xD36, 0xD37,

	0xD40, 0xD50, 0xD60, 0xD70, 0xD80, 0xD90, 0xDA0, 0xDB0, 0xDC0, 0xDD0, 0xDE0, 0xDF0,
	0xE00, 0xE10, 0xE20, 0xE30, 0xE40, 0xE50, 0xE60, 0xE70, 0xE80, 0xE90, 0xEA0, 0xEB0, 0xEC0, 0xED0, 0xEE0, 0xEF0,
								0xF40, 0xF50,

	0xF60, 0xF61, 0xF62, 0xF63, 0xF64, 0xF65, 0xF66, 0xF67,
	0xF70, 0xF71, 0xF72, 0xF73, 0xF74, 0xF75, 0xF76, 0xF77,

	0xF80, 0xF90, 0xFA0, 0xFB0, 0xFC0, 0xFD0,

	0xFE0, 0xFE1,
	0xFF0, 0xFF1, 0xFF2, 0xFF3, 0xFF4, 0xFF5,

	0x0F000, 0x0F001, 0x0F002, 0x0F003, 0x0F004, 0x0F005,
	0x0F010, 0x0F011, 0x0F012, 0x0F013, 0x0F014,          0x0F016,
	0x0F020, 0x0F030,          0x0F050, 0x0F060
};

void CPULogger::write_counters(const std::string _filename, std::map<int,uint64_t> &_cnt)
{
	FILE *file = fopen(_filename.c_str(), "w");
	if(!file) {
		PERRF(LOG_CPU, "error opening '%s' for writing\n", _filename.c_str());
		return;
	}
	uint64_t total = 0;
	PINFOF(LOG_V0, LOG_CPU, "writing counters to '%s' ... ", _filename.c_str());
	for(auto op : oplist) {
		if(fprintf(file, "0x%05X: %lu\n", op, _cnt[op]) < 0) {
			PERRF(LOG_CPU, "error writing to file\n");
			break;
		}
		total += _cnt[op];
	}
	fprintf(file, "\n total: %lu\n", total);

	PINFOF(LOG_V0, LOG_CPU, "done\n");
	fclose(file);
}
