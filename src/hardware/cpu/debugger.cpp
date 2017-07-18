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
/*
 * Portions of code Copyright (C) 2002-2013  The DOSBox Team
 *
 */

#include "ibmulator.h"
#include "debugger.h"
#include "hardware/cpu.h"
#include "hardware/cpu/disasm.h"
#include "hardware/memory.h"
#include <cstring>
#include <sstream>
#include <iomanip>

char * upcase(char * str) {
    for (char* idx = str; *idx ; idx++) *idx = toupper(*reinterpret_cast<unsigned char*>(idx));
    return str;
}

char * lowcase(char * str) {
	for(char* idx = str; *idx ; idx++)  *idx = tolower(*reinterpret_cast<unsigned char*>(idx));
	return str;
}

char * skip_blanks(char * str) {
	while(*str == ' ' || *str == '\t') {
		str++;
	}
	return str;
}

uint CPUDebugger::disasm(char *_buf, uint _buflen, uint32_t _addr, uint32_t _eip,
		CPUCore *_core, Memory *_mem, const uint8_t *_instr_buf, uint _instr_buf_len, bool _cs_def)
{
	return m_dasm.disasm(_buf, _buflen, _addr, _eip, _core, _mem, _instr_buf, _instr_buf_len, _cs_def);
}

unsigned CPUDebugger::last_disasm_opsize()
{
	return m_dasm.last_operand_size();
}

uint32_t CPUDebugger::get_hex_value(char *_str, char *&_hex, CPUCore *_core)
{
	uint32_t value = 0;
	uint32_t regval = 0;
	_hex = _str;
	while (*_hex==' ') _hex++;
	     if(strstr(_hex,"EAX")==_hex){ _hex+=3; regval = _core->get_EAX(); }
	else if(strstr(_hex,"EBX")==_hex){ _hex+=3; regval = _core->get_EBX(); }
	else if(strstr(_hex,"ECX")==_hex){ _hex+=3; regval = _core->get_ECX(); }
	else if(strstr(_hex,"EDX")==_hex){ _hex+=3; regval = _core->get_EDX(); }
	else if(strstr(_hex,"ESI")==_hex){ _hex+=3; regval = _core->get_ESI(); }
	else if(strstr(_hex,"EDI")==_hex){ _hex+=3; regval = _core->get_EDI(); }
	else if(strstr(_hex,"EBP")==_hex){ _hex+=3; regval = _core->get_EBP(); }
	else if(strstr(_hex,"ESP")==_hex){ _hex+=3; regval = _core->get_ESP(); }
	else if(strstr(_hex,"EIP")==_hex){ _hex+=3; regval = _core->get_EIP(); }
	else if(strstr(_hex,"AX")==_hex) { _hex+=2; regval = _core->get_AX(); }
	else if(strstr(_hex,"BX")==_hex) { _hex+=2; regval = _core->get_BX(); }
	else if(strstr(_hex,"CX")==_hex) { _hex+=2; regval = _core->get_CX(); }
	else if(strstr(_hex,"DX")==_hex) { _hex+=2; regval = _core->get_DX(); }
	else if(strstr(_hex,"SI")==_hex) { _hex+=2; regval = _core->get_SI(); }
	else if(strstr(_hex,"DI")==_hex) { _hex+=2; regval = _core->get_DI(); }
	else if(strstr(_hex,"BP")==_hex) { _hex+=2; regval = _core->get_BP(); }
	else if(strstr(_hex,"SP")==_hex) { _hex+=2; regval = _core->get_SP(); }
	else if(strstr(_hex,"IP")==_hex) { _hex+=2; regval = _core->get_EIP()&0xFFFF; }
	else if(strstr(_hex,"CS")==_hex) { _hex+=2; regval = _core->get_CS().sel.value; }
	else if(strstr(_hex,"DS")==_hex) { _hex+=2; regval = _core->get_DS().sel.value; }
	else if(strstr(_hex,"ES")==_hex) { _hex+=2; regval = _core->get_ES().sel.value; }
	else if(strstr(_hex,"SS")==_hex) { _hex+=2; regval = _core->get_SS().sel.value; }
	else if(strstr(_hex,"FS")==_hex) { _hex+=2; regval = _core->get_FS().sel.value; }
	else if(strstr(_hex,"GS")==_hex) { _hex+=2; regval = _core->get_GS().sel.value; };

	int mult = 1;
	while(*_hex) {
		if((*_hex>='0') && (*_hex<='9')) {
			value = (value<<4) + *_hex-'0';
		} else if((*_hex>='A') && (*_hex<='F')) {
			value = (value<<4) + *_hex-'A'+10;
		} else {
			if(*_hex == '+') {
				_hex++;
				return (regval + value)*mult + get_hex_value(_hex,_hex,_core);
			};
			if(*_hex == '-') {
				_hex++;
				return (regval + value)*mult - get_hex_value(_hex,_hex,_core);
			};
			if(*_hex == '*') {
				_hex++;
				mult = *_hex-'0';
			} else {
				break; // No valid char
			}
		}
		_hex++;
	};
	return (regval + value)*mult;
};

unsigned CPUDebugger::get_seg_idx(char *_str)
{
	     if(strstr(_str,"CS")==_str) { return REGI_CS; }
	else if(strstr(_str,"DS")==_str) { return REGI_DS; }
	else if(strstr(_str,"ES")==_str) { return REGI_ES; }
	else if(strstr(_str,"SS")==_str) { return REGI_SS; }
	else if(strstr(_str,"FS")==_str) { return REGI_FS; }
	else if(strstr(_str,"GS")==_str) { return REGI_GS; }

	//return something, but what? throw an exception?
	return REGI_CS;
}

char * CPUDebugger::analyze_instruction(char *_dasm_inst, CPUCore *_core,
		Memory *_memory, uint _opsize)
{
	static char result[256];

	char instu[256];
	char prefix[3];
	unsigned seg;

	strcpy(instu, _dasm_inst);
	upcase(instu);

	result[0] = 0;
	char* pos = strchr(instu,'[');
	if(pos) {
		// Segment prefix ?
		if(*(pos-1) == ':') {
			char* segpos = pos-3;
			prefix[0] = tolower(*segpos);
			prefix[1] = tolower(*(segpos+1));
			prefix[2] = 0;
			seg = get_seg_idx(segpos);
		} else {
			if(strstr(pos,"SP") || strstr(pos,"BP")) {
				seg = REGI_SS;
				strcpy(prefix,"ss");
			} else {
				seg = REGI_DS;
				strcpy(prefix,"ds");
			};
		};

		pos++;
		uint32_t adr = get_hex_value(pos, pos, _core);
		while (*pos!=']') {
			if (*pos=='+') {
				pos++;
				adr += get_hex_value(pos, pos, _core);
			} else if (*pos=='-') {
				pos++;
				adr -= get_hex_value(pos, pos, _core);
			} else {
				pos++;
			}
		};

		if(_memory) {
			static char outmask[] = "%s:[%04X]=%02X";
			if(_core->is_pmode()) {
				outmask[6] = '8';
			}
			try {
				uint32_t address = _core->dbg_get_phyaddr(seg, adr, _memory);
				switch (_opsize) {
					case 8 : {	uint8_t val = _memory->dbg_read_byte(address);
								outmask[12] = '2';
								sprintf(result,outmask,prefix,adr,val);
							}	break;
					case 16: {	uint16_t val = _memory->dbg_read_word(address);
								outmask[12] = '4';
								sprintf(result,outmask,prefix,adr,val);
							}	break;
					case 32: {	uint32_t val = _memory->dbg_read_dword(address);
								outmask[12] = '8';
								sprintf(result,outmask,prefix,adr,val);
							}	break;
				}
			} catch(CPUException &) { }
		}
		// Variable found ?
		/* TODO
		DebugVar* var = DebugVar::FindVar(address);
		if (var) {
			// Replace occurrence
			char* pos1 = strchr(inst,'[');
			char* pos2 = strchr(inst,']');
			if (pos1 && pos2) {
				char temp[256];
				strcpy(temp,pos2);				// save end
				pos1++; *pos1 = 0;				// cut after '['
				strcat(inst,var->GetName());	// add var name
				strcat(inst,temp);				// add end
			};
		};
		// show descriptor info, if available
		if ((cpu.pmode) && saveSelector) {
			strcpy(curSelectorName,prefix);
		};
		*/
	};

	//CALL
	if(strstr(instu,"CALL") == instu) {
		//eg: CALL 000F2084  ($-3325)
		pos = strchr(instu,' ');
		assert(pos);
		pos++;
		uint32_t addr;
		if(sscanf(pos, "%x",&addr)) {
			auto name = ms_addrnames.find(addr);
			if(name != ms_addrnames.end()) {
				sprintf(result,"%s", name->second);
			}
		}
	}
	// Must be a jump
	if (instu[0] == 'J')
	{
		bool jmp = false;
		switch (instu[1]) {
		case 'A' :	{	jmp = (_core->get_FLAGS(FMASK_CF)?false:true) && (_core->get_FLAGS(FMASK_ZF)?false:true); // JA
					}	break;
		case 'B' :	{	if (instu[2] == 'E') {
							jmp = (_core->get_FLAGS(FMASK_CF)?true:false) || (_core->get_FLAGS(FMASK_ZF)?true:false); // JBE
						} else {
							jmp = _core->get_FLAGS(FMASK_CF)?true:false; // JB
						}
					}	break;
		case 'C' :	{	if (instu[2] == 'X') {
							jmp = _core->get_CX() == 0; // JCXZ
						} else {
							jmp = _core->get_FLAGS(FMASK_CF)?true:false; // JC
						}
					}	break;
		case 'E' :	{	jmp = _core->get_FLAGS(FMASK_ZF)?true:false; // JE
					}	break;
		case 'G' :	{	if (instu[2] == 'E') {
							jmp = (_core->get_FLAGS(FMASK_SF)?true:false)==(_core->get_FLAGS(FMASK_OF)?true:false); // JGE
						} else {
							jmp = (_core->get_FLAGS(FMASK_ZF)?false:true) && ((_core->get_FLAGS(FMASK_SF)?true:false)==(_core->get_FLAGS(FMASK_OF)?true:false)); // JG
						}
					}	break;
		case 'L' :	{	if (instu[2] == 'E') {
							jmp = (_core->get_FLAGS(FMASK_ZF)?true:false) || ((_core->get_FLAGS(FMASK_SF)?true:false)!=(_core->get_FLAGS(FMASK_OF)?true:false)); // JLE
						} else {
							jmp = (_core->get_FLAGS(FMASK_SF)?true:false)!=(_core->get_FLAGS(FMASK_OF)?true:false); // JL
						}
					}	break;
		case 'M' :	{	jmp = true; // JMP
					}	break;
		case 'N' :	{	switch (instu[2]) {
						case 'B' :
						case 'C' :	{	jmp = _core->get_FLAGS(FMASK_CF)?false:true;	// JNB / JNC
									}	break;
						case 'E' :	{	jmp = _core->get_FLAGS(FMASK_ZF)?false:true;	// JNE
									}	break;
						case 'O' :	{	jmp = _core->get_FLAGS(FMASK_OF)?false:true;	// JNO
									}	break;
						case 'P' :	{	jmp = _core->get_FLAGS(FMASK_PF)?false:true;	// JNP
									}	break;
						case 'S' :	{	jmp = _core->get_FLAGS(FMASK_SF)?false:true;	// JNS
									}	break;
						case 'Z' :	{	jmp = _core->get_FLAGS(FMASK_ZF)?false:true;	// JNZ
									}	break;
						}
					}	break;
		case 'O' :	{	jmp = _core->get_FLAGS(FMASK_OF)?true:false; // JO
					}	break;
		case 'P' :	{	if (instu[2] == 'O') {
							jmp = _core->get_FLAGS(FMASK_PF)?false:true; // JPO
						} else {
							jmp = _core->get_FLAGS(FMASK_SF)?true:false; // JP / JPE
						}
					}	break;
		case 'S' :	{	jmp = _core->get_FLAGS(FMASK_SF)?true:false; // JS
					}	break;
		case 'Z' :	{	jmp = _core->get_FLAGS(FMASK_ZF)?true:false; // JZ
					}	break;
		}
		pos = strchr(instu,' ');
		assert(pos);

		if(!_core->is_pmode()) {
			uint32_t addr=0;
			uint32_t seg,off;
			pos = skip_blanks(pos);
			if(sscanf(pos, "%x:%x",&seg,&off)==2) {
				//eg: JMP  F000:E05B
				addr = (seg << 4) + off;
			} else if(sscanf(pos, "%x",&addr)==1) {
				//absolute address
			} else if(strstr(pos,"NEAR") == pos) {
				//jump near to EA word (abs offset)
				pos = strchr(pos,' ');
				pos = skip_blanks(pos);
				if(pos[0]=='B' && pos[1]=='X') {
					addr = _core->dbg_get_phyaddr(REGI_CS, _core->get_BX());
				}
			}
			if(addr != 0) {
				auto name = ms_addrnames.find(addr);
				if(name != ms_addrnames.end()) {
					sprintf(result,"%s", name->second);
				}
			}
		}

		char * curpos = result + strlen(result);
		if (jmp) {
			pos = strchr(instu,'$');
			if (pos) {
				pos = strchr(instu,'+');
				if (pos) {
					strcpy(curpos,"(down)");
				} else {
					strcpy(curpos,"(up)");
				}
			}
		} else {
			sprintf(curpos,"(no jmp)");
		}
	}
	return result;
};

std::map<uint32_t, const char*> CPUDebugger::ms_addrnames = {
		{ 0xE4920, "INT_10" },
		{ 0xF008A, "CPU_TEST" },
		{ 0xF00CB, "POST_RESET" },
		{ 0xF0149, "POST_START" },
		{ 0xF0DE8, "RESET_01" },
		{ 0xF1588, "RESET_02" },
		{ 0xF1528, "RESET_03" },
		{ 0xF1DBF, "RESET_04" },
		{ 0xF012E, "RESET_05" },
		{ 0xF15AC, "RESET_06" },
		{ 0xF158B, "RESET_07" },
		{ 0xF0BBB, "RESET_08" },
		{ 0xF5371, "RESET_09" },
		{ 0xF0134, "RESET_0A" },
		{ 0xF0138, "RESET_0B" },
		{ 0xF0540, "RESET_0C" },
		{ 0xF0EDB, "PIC_INIT" },
		{ 0xF2171, "WAIT" },
		{ 0xF2084, "CMOS_READ" },
		{ 0xF209F, "CMOS_WRITE" },
		{ 0xF2121, "SET_DS_TO_40h" },
		{ 0xF237E, "IVT_DEF_HANDLER" },
		{ 0xF23D6, "CPU_RESET" },
		{ 0xF29CD, "IDT_DEF_HANDLER" },
		{ 0xF46CA, "INT_13" },
		{ 0xF5023, "INT_15" },
		{ 0xFF065, "INT_10_JMP" },
		{ 0xFF859, "INT_15_JMP" },
		{ 0xFE05B, "RESET" },
		{ 0xFFE05B, "RESET" },
		{ 0xFFFF0, "RESET_VECTOR" }
};


/*******************************************************************************
 */

#define MAKE_INT_SEL(vec, ax, axlen) ((vec)<<24 | (ax)<<8 | axlen)

const char * CPUDebugger::INT_decode(bool call, uint8_t vector, uint16_t ax,
		CPUCore *core, Memory *mem)
{
	int reslen = 512;
	static char result[512];

	//uint8_t ah = ax>>8;
	//uint8_t al = ax&0xFF;

	uint axlen = 0;
	auto interr = ms_interrupts.find(MAKE_INT_SEL(vector, 0, 0));
	if(interr == ms_interrupts.end()) {
		axlen = 1;
		interr = ms_interrupts.find(MAKE_INT_SEL(vector, ax&0xFF00, 1));
		if(interr == ms_interrupts.end()) {
			axlen = 2;
			interr = ms_interrupts.find(MAKE_INT_SEL(vector, ax, 2));
		}
	}
	if(interr != ms_interrupts.end()) {

		if(!interr->second.decode) {
			return nullptr;
		}

		const char * op;
		if(call) {
			op = ">";
		} else {
			op = "<";
		}
		if(axlen == 1) {
			snprintf(result, reslen, "%s INT %02X/%02X %s", op, vector, (ax>>8), interr->second.name);
		} else if(axlen == 0) {
			snprintf(result, reslen, "%s INT %02X %s", op, vector, interr->second.name);
		} else {
			snprintf(result, reslen, "%s INT %02X/%04X %s", op, vector, ax, interr->second.name);
		}

		uint slen = strlen(result);
		char * curpos = result + slen;
		reslen -= slen;
		assert(reslen>0);

		if(interr->second.decoder) {
			interr->second.decoder(call, ax, core, mem, curpos, reslen);
		} else {
			if(!call) {
				INT_def_ret(core, curpos, reslen);
			}
		}
	} else {
		// for unknown INTs don't print the return
		if(call) {
			snprintf(result, reslen, "INT %02X/%04X ?", vector, ax);
		}
	}

	return result;
}

void CPUDebugger::INT_def_ret(CPUCore *core, char* buf, uint buflen)
{
	if(buflen<15) return;
	snprintf(buf, buflen, " ret CF=%u", core->get_FLAGS(FMASK_CF)>>FBITN_CF);
}

void CPUDebugger::INT_def_ret_errcode(CPUCore *core, char* buf, uint buflen)
{
	uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
	if(cf) {
		const char * errstr = ms_dos_errors[core->get_AX()];
		snprintf(buf, buflen, " ret CF=1: %s", errstr);
	} else {
		snprintf(buf, buflen, " ret CF=0");
	}
}

void CPUDebugger::INT_10_00(bool call, uint16_t ax, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}

	uint8_t al = ax&0xFF;
	switch(al) {
		case 0x00:
		case 0x01: snprintf(buf, buflen, " : 360x400x16 text"); break;
		case 0x02:
		case 0x03: snprintf(buf, buflen, " : 720x400x16 text"); break;
		case 0x04:
		case 0x05: snprintf(buf, buflen, " : 320x200x4 text"); break;
		case 0x06: snprintf(buf, buflen, " : 640x200x2 text"); break;
		case 0x07: snprintf(buf, buflen, " : 720x400x1 text"); break;
		case 0x0D: snprintf(buf, buflen, " : 320x200x16"); break;
		case 0x0E: snprintf(buf, buflen, " : 640x200x16"); break;
		case 0x0F: snprintf(buf, buflen, " : 640x350x1"); break;
		case 0x10: snprintf(buf, buflen, " : 640x350x16"); break;
		case 0x11: snprintf(buf, buflen, " : 640x480x2"); break;
		case 0x12: snprintf(buf, buflen, " : 640x480x16"); break;
		case 0x13: snprintf(buf, buflen, " : 320x200x256"); break;
		default: snprintf(buf, buflen, " : AL=0x%02X (?)", al); break;
	}
}

void CPUDebugger::INT_10_0E(bool call, uint16_t ax, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}
	//uint8_t ah = ax>>8;
	uint8_t al = ax&0xFF;
	if(al>=32 && al!=127) {
		snprintf(buf, buflen, ": '%c'", al);
	} else {
		snprintf(buf, buflen, ": 0x%02X", al);
	}
}

void CPUDebugger::INT_10_12(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}

	uint8_t bl = core->get_BL();
	const char *str = "?";
	switch(bl) {
		case 0x10: str = "VIDEO - GET EGA INFO"; break;
		case 0x20: str = "VIDEO - ALTERNATE PRTSC"; break;
		case 0x30: str = "VIDEO - SELECT VERTICAL RESOLUTION"; break;
		case 0x31: str = "VIDEO - PALETTE LOADING"; break;
		case 0x32: str = "VIDEO - VIDEO ADDRESSING"; break;
		case 0x33: str = "VIDEO - GRAY-SCALE SUMMING"; break;
		case 0x34: str = "VIDEO - CURSOR EMULATION"; break;
		case 0x35: str = "VIDEO - DISPLAY-SWITCH INTERFACE"; break;
		case 0x36: str = "VIDEO - REFRESH CONTROL"; break;
		case 0x38: str = "IBM BIOS - Private Function"; break;
		case 0x39: str = "IBM BIOS - Private Function"; break;
		case 0x3A: str = "IBM BIOS - Private Function"; break;
	}
	snprintf(buf, buflen, "%s", str);
	uint len = strlen(buf);
	buf += len;
	buflen -= len;
}

bool CPUDebugger::get_drive_CHS(const CPUCore &_core, int &_drive, int &_C, int &_H, int &_S)
{
	bool is_hdd = _core.get_DL() & 0x80;
	_drive = _core.get_DL() & 0x7F;
	_C = _core.get_CH();
	_H = _core.get_DH();
	_S = _core.get_CL() & 0x3F;
	if(is_hdd) {
		_C = _C | ((int(_core.get_CL()) & 0xC0) << 2);
	}
	return is_hdd;
}

void CPUDebugger::INT_13(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/, char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		const char * status = ms_disk_status[core->get_AH()];
		snprintf(buf, buflen, " ret CF=%u: AH=%u (%s)", cf,core->get_AH(),status);
		return;
	}
	snprintf(buf, buflen, " drive=0x%02X", core->get_DL());
}

void CPUDebugger::INT_13_02_3_4_C(bool call, uint16_t ax, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			const char * errstr = ms_disk_status[core->get_AH()];
			snprintf(buf, buflen, " ret CF=1: %s", errstr);
		} else {
			INT_def_ret(core, buf, buflen);
		}
		return;
	}
	int drive,C,H,S;
	bool is_hdd = get_drive_CHS(*core, drive,C,H,S);
	snprintf(buf, buflen, " %s=%d,C=%d,H=%d,S=%d (nS=%d)", is_hdd?"HDD":"FDD",drive,C,H,S,ax&0xFF);
}

void CPUDebugger::INT_15_86(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}
	snprintf(buf, buflen, " %u:%u", core->get_CX(), core->get_DX());
}

void CPUDebugger::INT_15_87(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}
	try {
		uint32_t gdt = core->dbg_get_phyaddr(REGI_ES, core->get_SI(), mem);
		Descriptor from, to;
		from = mem->dbg_read_qword(gdt+0x10);
		to   = mem->dbg_read_qword(gdt+0x18);
		snprintf(buf, buflen, ": from 0x%06X to 0x%06X (0x%04X bytes)",
				from.base, to.base, core->get_CX()*2
		);
	} catch(CPUException &) { }
}

void CPUDebugger::INT_1A_00(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		snprintf(buf, buflen, " ret : %u:%u", core->get_CX(), core->get_DX());
		return;
	}
}

void CPUDebugger::INT_21_09(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	if(!call) {
		snprintf(buf, buflen, " ret");
		return;
	}
	if(buflen<=3) {
		return;
	}
	char * str;
	try {
		uint32_t addr = core->dbg_get_phyaddr(REGI_DS, core->get_DX(), mem);
		str = (char*)mem->get_buffer_ptr(addr);
	} catch(...) {
		return;
	}
	*buf++ = ':';
	*buf++ = ' ';
	buflen -= 2;
	while(*str!='$' && --buflen) {
		if(*str>=32 && *str!=127) {
			*buf++ = *str;
		} else if(*str==0xA) {
			*buf++ = '\\';
			*buf++ = 'n';
			buflen--;
		} else if(*str==0xD) {
			*buf++ = '\\';
			*buf++ = 'r';
			buflen--;
		} else {
			*buf++ = '.';
		}
		str++;
	}
	*buf = 0;
}

void CPUDebugger::INT_21_25(bool call, uint16_t ax, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}

	uint8_t al = ax&0xFF;
	snprintf(buf, buflen, ": int=%02X, handler=%04X:%04X",
			al, core->get_DS().sel.value, core->get_DX());
}

void CPUDebugger::INT_21_2C(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		snprintf(buf, buflen, " ret : %u:%u:%u.%u",
				core->get_CH(), //hour
				core->get_CL(), //minute
				core->get_DH(), //second
				core->get_DL() //1/100 seconds
			);
		return;
	}
}

void CPUDebugger::INT_2F_1116(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			const char * errstr = ms_dos_errors[core->get_AX()];
			snprintf(buf, buflen, " ret CF=1: %s", errstr);
		} else {
			INT_def_ret(core, buf, buflen);
		}
		return;
	}
	//TODO
}

void CPUDebugger::INT_2F_1123(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	const char *filename;
	if(!call) {
		INT_def_ret(core, buf, buflen);
		if(core->get_FLAGS(FMASK_CF) == 0) {
			buf += strlen(buf);
			try {
				filename = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_ES, core->get_DI(), mem));
			} catch(CPUException &) {
				filename = "[unknown]";
			}
			snprintf(buf, buflen, " : '%s'", filename);
		}
		return;
	}
	//DS:SI -> ASCIZ filename to canonicalize
	try {
		filename = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_DS, core->get_SI(), mem));
	} catch(CPUException &) {
		filename = "[unknown]";
	}
	snprintf(buf, buflen, " : '%s'", filename);
}

void CPUDebugger::INT_21_0E(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}

	char drive = 65 + core->get_DL();
	snprintf(buf, buflen, " : '%c:'", drive);
}

void CPUDebugger::INT_21_30(bool call, uint16_t ax, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		snprintf(buf, buflen, " ret : ver=%u.%u", core->get_AL(), core->get_AH());
		if((ax&0xFF) == 0) {
			const char *oem = "";
			uint8_t bh = core->get_BH();
			if(bh==0) {
				oem = "IBM";
			} else if(bh==2) {
				oem = "MS";
			}
			buflen -= strlen(buf);
			snprintf(buf+strlen(buf), buflen, " %s", oem);
		}
		return;
	}
}

void CPUDebugger::INT_21_32(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		const char * code;
		int al = core->get_AL();
		if(al==0x00) {
			code = "successful";
		} else if(al==0xFF) {
			code = "invalid or network drive";
		} else {
			code = "???";
		}
		snprintf(buf, buflen, " ret CF=%d: %s", cf,code);
		return;
	}
	snprintf(buf, buflen, " : drive=0x%02X", core->get_DL());
}

void CPUDebugger::INT_21_36(bool call, uint16_t ax, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		if(ax == 0xFFFF) {
			snprintf(buf, buflen, " : invalid drive");
		} else {
			snprintf(buf, buflen, " : sec.p.cl.=%d, free cl.=%d, bytes p.sec.=%d, tot.cl.=%d",
					ax, core->get_BX(), core->get_CX(), core->get_DX());
		}
		return;
	}
	snprintf(buf, buflen, " : drive=0x%02X", core->get_DL());
}

void CPUDebugger::INT_21_48(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	uint16_t bx = core->get_BX();
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			const char * errstr = ms_dos_errors[core->get_AX()];
			snprintf(buf, buflen, " ret CF=1: %s, %d paragraphs available (%d bytes)",
					errstr, bx, uint32_t(bx)*16);
		} else {
			snprintf(buf, buflen, " ret CF=0: segment=%04X", core->get_AX());
		}
		return;
	}
	snprintf(buf, buflen, " : %d paragraphs (%d bytes)", bx, uint32_t(bx)*16);
}

void CPUDebugger::INT_21_4A(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}
	uint16_t bx = core->get_BX();
	uint16_t es = core->get_ES().sel.value;
	snprintf(buf, buflen, " : segment=%04X, paragraphs=%d (%d bytes)",
			es,	bx, uint32_t(bx)*16);
}

void CPUDebugger::INT_21_4B(bool call, uint16_t ax, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			const char * errstr = ms_dos_errors[core->get_AX()];
			snprintf(buf, buflen, " ret CF=1: %s", errstr);
		} else {
			INT_def_ret(core, buf, buflen);
		}
		return;
	}
	//DS:DX -> ASCIZ program name
	const char * name;
	try {
		name = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_DS, core->get_DX(), mem));
	} catch(CPUException &) {
		name = "[unknown]";
	}
	const char * type = "";
	switch(ax & 0xFF) {
		case 0x0: type = "load and execute"; break;
		case 0x1: type = "load but do not execute"; break;
		case 0x3: type = "load overlay"; break;
		case 0x4: type = "load and execute in background"; break;
		default: type = ""; break;
	}
	snprintf(buf, buflen, " : '%s' %s", name, type);
}

void CPUDebugger::INT_21_39_A_B_4E(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			const char * errstr = ms_dos_errors[core->get_AX()];
			snprintf(buf, buflen, " ret CF=1: %s", errstr);
		} else {
			INT_def_ret(core, buf, buflen);
		}
		return;
	}
	//DS:DX -> ASCIZ pathname
	const char * pathname;
	try {
		pathname = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_DS, core->get_DX(), mem));
	} catch(CPUException &) {
		pathname = "[unknown]";
	}
	snprintf(buf, buflen, " : '%s'", pathname);
}

void CPUDebugger::INT_21_3D(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			snprintf(buf, buflen, " ret CF=1: %s", ms_dos_errors[core->get_AX()]);
		} else {
			snprintf(buf, buflen, " ret : handle=%d", core->get_AX());
		}
		return;
	}
	//DS:DX -> ASCIZ filename
	const char * filename;
	try {
		filename = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_DS, core->get_DX(), mem));
	} catch(CPUException &) {
		filename = "[unknown]";
	}
	const char * mode = "";
	switch(core->get_AL() & 0x7) {
		case 0x0: mode = "read only"; break;
		case 0x1: mode = "write only"; break;
		case 0x2: mode = "read/write"; break;
		case 0x3: mode = ""; break;
	}
	snprintf(buf, buflen, " : '%s' %s", filename, mode);
}

void CPUDebugger::INT_21_3E(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret_errcode(core, buf, buflen);
		return;
	}

	snprintf(buf, buflen, " : handle=%d", core->get_BX());
}

void CPUDebugger::INT_21_3F(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			snprintf(buf, buflen, " ret CF=1: %s", ms_dos_errors[core->get_AX()]);
		} else {
			snprintf(buf, buflen, " ret : %d bytes read", core->get_AX());
		}
		return;
	}

	snprintf(buf, buflen, " : handle=%d, %d bytes, dest buf %04X:%04X",
			core->get_BX(), core->get_CX(), core->get_DS().sel.value, core->get_DX());
}

void CPUDebugger::INT_21_42(bool call, uint16_t ax, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			snprintf(buf, buflen, " ret CF=1: %s", ms_dos_errors[core->get_AX()]);
		} else {
			uint32_t position = uint32_t(core->get_DX())<<16 | core->get_AX();
			snprintf(buf, buflen, " ret : %d bytes from start", position);
		}
		return;
	}

	const char *origin = "";
	switch(ax & 0xFF) {
		case 0x0: origin = "start of file"; break;
		case 0x1: origin = "current file position"; break;
		case 0x2: origin = "end of file"; break;
		default: origin = "???"; break;
	}
	uint32_t offset = uint32_t(core->get_CX())<<16 | core->get_DX();
	snprintf(buf, buflen, " : handle=%d, %s, offset=%d",
			core->get_BX(), origin, offset);
}

void CPUDebugger::INT_21_43(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			snprintf(buf, buflen, " ret CF=1: %s", ms_dos_errors[core->get_AX()]);
		} else {
			uint16_t cx = core->get_CX();
			std::string attr;
			if(cx & 32) attr += "archive ";
			if(cx & 16) attr += "directory ";
			if(cx &  8) attr += "volume-label ";
			if(cx &  4) attr += "system ";
			if(cx &  2) attr += "hidden ";
			if(cx &  1) attr += "read-only";
			snprintf(buf, buflen, " ret : %s", attr.c_str());
		}
		return;
	}
	//DS:DX -> ASCIZ filename
	const char * filename;
	try {
		filename = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_DS, core->get_DX(), mem));
	} catch(CPUException &) {
		filename = "[unknown]";
	}
	snprintf(buf, buflen, " : '%s'", filename);
}

void CPUDebugger::INT_21_440D(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		const char * retcode;
		if(cf) {
			retcode = ms_dos_errors[core->get_AX()];
		} else {
			retcode = ms_dos_errors[0];
		}
		snprintf(buf, buflen, " ret CF=%d: %s", cf,retcode);
		return;
	}
	int ch = core->get_CH();
	int cl = core->get_CL();
	const char *category;
	if(ch==0x08) {
		category = "disk drive";
	} else if(ch==0x48) {
		category = "FAT32 disk drive";
	} else if(ch<0x7F) {
		category = "Microsoft reserved";
	} else {
		category = "OEM reserved";
	}
	snprintf(buf, buflen, " : drive=%02Xh,cat=%02Xh(%s),fn=%02Xh(%s)",
			core->get_BL(),
			ch, category,
			cl, ms_ioctl_code[cl]
	);
}

void CPUDebugger::INT_21_5F03(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}
	const char *local, *net;
	try {
		local = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_DS, core->get_SI(), mem));
	} catch(CPUException &) {
		local = "[unknown]";
	}
	try {
		net = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_ES, core->get_DI(), mem));
	} catch(CPUException &) {
		net = "[unknown]";
	}
	snprintf(buf, buflen, " : local:'%s', net:'%s'", local, net);
}

void CPUDebugger::INT_2B_01(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	//IBM - RAM LOADER - FIND FILE IN ROMDRV
	if(!call) {
		INT_def_ret(core, buf, buflen);
		if(!core->get_FLAGS(FMASK_CF)) {
			buf += strlen(buf);
			//AL = the file table index
			snprintf(buf, buflen, " : AL=%02X", core->get_AL());
		}
		return;
	}

	//DS:SI -> ASCIZ filename
	const char * filename;
	try {
		filename = (char*)mem->get_buffer_ptr(core->dbg_get_phyaddr(REGI_DS, core->get_SI(), mem));
	} catch(CPUException &) {
		filename = "[unknown]";
	}
	snprintf(buf, buflen, " : '%s'", filename);
}

std::string CPUDebugger::descriptor_table_to_CSV(Memory &_mem, uint32_t _base, uint16_t _limit)
{
	if(_base+_limit > _mem.get_buffer_size()) {
		throw std::range_error("descriptor table beyond RAM limit");
	}
	std::stringstream output;
	output << std::setfill('0');
	uint32_t ptr = _base;
	int index = 0;
	Descriptor desc;
	output << "index,base,limit/offset,base_15_0/selector,base_23_16/word_count,";
	output << "AR,type,accessed,DPL,P,valid\n";
	while(ptr < _base+_limit) {
		desc = _mem.dbg_read_qword(ptr);

		// entry number
		output << std::hex << std::setw(3) << index << ",";

		// base
		output << std::hex << std::setw(8) << int(desc.base) << ",";

		// limit/offset
		output << std::hex << std::setw(8) << int(desc.limit) << ",";

		// base_15_0/selector
		output << std::hex << std::setw(4) << int(desc.selector) << ",";

		// base_23_16/word_count
		output << std::hex << std::setw(2) << int(desc.word_count) << ",";

		//AR
		output << std::hex << std::setw(2) << int(desc.get_AR()) << ",";
		if(desc.is_system_segment()) {
			switch(desc.type) {
				case DESC_TYPE_AVAIL_286_TSS:
					output << "AVAIL 286 TSS";
					break;
				case DESC_TYPE_AVAIL_386_TSS:
					output << "AVAIL 386 TSS";
					break;
				case DESC_TYPE_LDT_DESC:
					output << "LDT DESC";
					break;
				case DESC_TYPE_BUSY_286_TSS:
					output << "BUSY 286 TSS";
					break;
				case DESC_TYPE_BUSY_386_TSS:
					output << "BUSY 386 TSS";
					break;
				case DESC_TYPE_286_CALL_GATE:
					output << "286 CALL GATE";
					break;
				case DESC_TYPE_386_CALL_GATE:
					output << "386 CALL GATE";
					break;
				case DESC_TYPE_TASK_GATE:
					output << "TASK GATE";
					break;
				case DESC_TYPE_286_INTR_GATE:
					output << "286 INTR GATE";
					break;
				case DESC_TYPE_386_INTR_GATE:
					output << "386 INTR GATE";
					break;
				case DESC_TYPE_286_TRAP_GATE:
					output << "286 TRAP GATE";
					break;
				case DESC_TYPE_386_TRAP_GATE:
					output << "386 TRAP GATE";
					break;
				default:
					output << "INVALID";
					break;
			}
			output << ",,";
		} else {
			if(desc.is_code_segment()) {
				output << "code ";
				if(!desc.is_conforming()) {
					output << "non conforming";
				} else {
					output << "conforming";
				}
				if(desc.is_readable()) {
					output << " R";
				}
			} else {
				output << "data ";
				if(desc.is_expand_down()) {
					output << "exp down ";
				}
				if(desc.is_writeable()) {
					output << "RW";
				} else {
					output << "R";
				}
			}
			output << ",";

			if(desc.accessed) { output << "accessed" << ","; }
			else { output << ","; }
		}

		// DPL
		output << std::dec << std::setw(2) << int(desc.dpl) << ",";

		// present
		if(desc.present) { output << "P" << ","; }
		else { output << "NP" << ","; }

		// valid
		if(desc.valid) { output << "valid"; }
		else { output << "invalid"; }

		ptr += 8;
		index++;
		output << "\n";
	}
	std::string str = output.str();
	return str;
}

int_map_t CPUDebugger::ms_interrupts = {
	/* INT 10 */
	{ MAKE_INT_SEL(0x10, 0x0000, 1), { true,  &CPUDebugger::INT_10_00, "VIDEO - SET VIDEO MODE" } },
	{ MAKE_INT_SEL(0x10, 0x0100, 1), { true,  nullptr,                 "VIDEO - SET TEXT-MODE CURSOR SHAPE" } },
	{ MAKE_INT_SEL(0x10, 0x0E00, 1), { false, nullptr,                 "TELETYPE OUTPUT" } },
	{ MAKE_INT_SEL(0x10, 0x0200, 1), { false, nullptr,                 "SET CURSOR POS" } },
	{ MAKE_INT_SEL(0x10, 0x0900, 1), { false, &CPUDebugger::INT_10_0E, "WRITE CHAR AND ATTR AT CURSOR POS" } },
	{ MAKE_INT_SEL(0x10, 0x0F00, 1), { true,  nullptr,                 "VIDEO - GET CURRENT VIDEO MODE" } },
	{ MAKE_INT_SEL(0x10, 0x1003, 2), { true,  nullptr,                 "VIDEO - TOGGLE INTENSITY/BLINKING BIT" } },
	{ MAKE_INT_SEL(0x10, 0x1007, 2), { true,  nullptr,                 "VIDEO - GET INDIVIDUAL PALETTE REGISTER" } },
	{ MAKE_INT_SEL(0x10, 0x101A, 2), { true,  nullptr,                 "VIDEO - GET VIDEO DAC COLOR-PAGE STATE (VGA)" } },
	{ MAKE_INT_SEL(0x10, 0x1100, 2), { true,  nullptr,                 "VIDEO - TEXT-MODE CHARGEN - LOAD USER-SPECIFIED PATTERNS" } },
	{ MAKE_INT_SEL(0x10, 0x1103, 2), { true,  nullptr,                 "VIDEO - TEXT-MODE CHARGEN - SET BLOCK SPECIFIER" } },
	{ MAKE_INT_SEL(0x10, 0x1122, 2), { true,  nullptr,                 "VIDEO - GRAPH-MODE CHARGEN - SET ROM 8x14 GRAPHICS CHARS" } },
	{ MAKE_INT_SEL(0x10, 0x1130, 2), { true,  nullptr,                 "VIDEO - GET FONT INFORMATION" } },
	{ MAKE_INT_SEL(0x10, 0x1200, 1), { true,  &CPUDebugger::INT_10_12, "" } },
	{ MAKE_INT_SEL(0x10, 0x1300, 1), { true,  nullptr,                 "WRITE STRING" } },
	{ MAKE_INT_SEL(0x10, 0x1A00, 2), { true,  nullptr,                 "VIDEO - GET DISPLAY COMBINATION CODE" } },
	{ MAKE_INT_SEL(0x10, 0x1B00, 2), { true,  nullptr,                 "VIDEO - FUNCTIONALITY/STATE INFORMATION" } },
	{ MAKE_INT_SEL(0x10, 0x6F00, 2), { true,  nullptr,                 "VIDEO - Video7 VGA,VEGA VGA - INSTALLATION CHECK" } },
	{ MAKE_INT_SEL(0x10, 0xF000, 1), { true,  nullptr,                 "EGA - READ ONE REGISTER" } },
	{ MAKE_INT_SEL(0x10, 0xF100, 1), { true,  nullptr,                 "EGA - WRITE ONE REGISTER" } },
	{ MAKE_INT_SEL(0x10, 0xF200, 1), { true,  nullptr,                 "EGA - READ REGISTER RANGE" } },
	{ MAKE_INT_SEL(0x10, 0xF300, 1), { true,  nullptr,                 "EGA - WRITE REGISTER RANGE" } },
	{ MAKE_INT_SEL(0x10, 0xFA00, 1), { true,  nullptr,                 "EGA - INTERROGATE DRIVER" } },
	/* INT 11 */
	{ MAKE_INT_SEL(0x11, 0x0000, 0), { true,  nullptr,                 "GET EQUIPMENT LIST" } },
	/* INT 13 */
	{ MAKE_INT_SEL(0x13, 0x0000, 1), { true,  &CPUDebugger::INT_13,    "DISK - RESET DISK SYSTEM" } },
	{ MAKE_INT_SEL(0x13, 0x0200, 1), { true,  &CPUDebugger::INT_13_02_3_4_C,"DISK - READ SECTOR(S) INTO MEMORY" } },
	{ MAKE_INT_SEL(0x13, 0x0300, 1), { true,  &CPUDebugger::INT_13_02_3_4_C,"DISK - WRITE DISK SECTOR(S)" } },
	{ MAKE_INT_SEL(0x13, 0x0400, 1), { true,  &CPUDebugger::INT_13_02_3_4_C,"DISK - VERIFY DISK SECTOR(S)" } },
	{ MAKE_INT_SEL(0x13, 0x0800, 1), { true,  &CPUDebugger::INT_13,    "DISK - GET DRIVE PARAMETERS" } },
	{ MAKE_INT_SEL(0x13, 0x0900, 1), { true,  &CPUDebugger::INT_13,    "HARD DISK - INITIALIZE CONTROLLER WITH DRIVE PARAMETERS" } },
	{ MAKE_INT_SEL(0x13, 0x0C00, 1), { true,  &CPUDebugger::INT_13_02_3_4_C,"HARD DISK - SEEK TO CYLINDER" } },
	{ MAKE_INT_SEL(0x13, 0x1100, 1), { true,  &CPUDebugger::INT_13,    "HARD DISK - RECALIBRATE DRIVE" } },
	{ MAKE_INT_SEL(0x13, 0x1500, 1), { true,  &CPUDebugger::INT_13,    "DISK - GET DISK TYPE" } },
	{ MAKE_INT_SEL(0x13, 0x1600, 1), { true,  &CPUDebugger::INT_13,    "FLOPPY - DETECT DISK CHANGE" } },
	{ MAKE_INT_SEL(0x13, 0x1700, 1), { true,  &CPUDebugger::INT_13,    "FLOPPY DISK - SET MEDIA TYPE FOR FORMAT" } },
	{ MAKE_INT_SEL(0x13, 0x1800, 1), { true,  &CPUDebugger::INT_13,    "FLOPPY DISK - SET MEDIA TYPE FOR FORMAT (new)" } },
	/* INT 15 */
	{ MAKE_INT_SEL(0x15, 0x2100, 1), { false, nullptr,                 "POWER-ON SELF-TEST ERROR LOG" } },
	{ MAKE_INT_SEL(0x15, 0x2300, 2), { true,  nullptr,                 "IBM - GET CMOS 2D-2E DATA" } },
	{ MAKE_INT_SEL(0x15, 0x2301, 2), { true,  nullptr,                 "IBM - SET CMOS 2D-2E DATA" } },
	{ MAKE_INT_SEL(0x15, 0x2302, 2), { true,  nullptr,                 "IBM - GET ROM STARTUP VIDEO REG TABLES" } },
	{ MAKE_INT_SEL(0x15, 0x2303, 2), { true,  nullptr,                 "IBM - VIDEO graphical func" } },
	{ MAKE_INT_SEL(0x15, 0x2304, 2), { true,  nullptr,                 "IBM - SYSTEM SETUP" } },
	{ MAKE_INT_SEL(0x15, 0x2305, 2), { true,  nullptr,                 "IBM - GET PROCESSOR SPEED" } },
	{ MAKE_INT_SEL(0x15, 0x4F00, 1), { false, nullptr,                 "KEYBOARD INTERCEPT" } },
	{ MAKE_INT_SEL(0x15, 0x8600, 1), { true,  &CPUDebugger::INT_15_86, "BIOS - WAIT" } },
	{ MAKE_INT_SEL(0x15, 0x8700, 1), { true,  &CPUDebugger::INT_15_87, "COPY EXTENDED MEM" } },
	{ MAKE_INT_SEL(0x15, 0x9000, 1), { true,  nullptr,                 "OS HOOK - DEVICE BUSY" } },
	{ MAKE_INT_SEL(0x15, 0x9100, 1), { true,  nullptr,                 "OS HOOK - DEVICE POST" } },
	{ MAKE_INT_SEL(0x15, 0xC000, 1), { true,  nullptr,                 "GET CONFIGURATION" } },
	{ MAKE_INT_SEL(0x15, 0xC100, 1), { false, nullptr,                 "RETURN EXT-BIOS DATA AREA SEGMENT ADDR" } },
	{ MAKE_INT_SEL(0x15, 0xC200, 2), { true,  nullptr,                 "POINTING DEV - ENABLE/DISABLE" } },
	{ MAKE_INT_SEL(0x15, 0xC201, 2), { true,  nullptr,                 "POINTING DEV - RESET" } },
	{ MAKE_INT_SEL(0x15, 0xC202, 2), { true,  nullptr,                 "POINTING DEV - SET SAMPLING RATE" } },
	{ MAKE_INT_SEL(0x15, 0xC203, 2), { true,  nullptr,                 "POINTING DEV - SET RESOLUTION" } },
	{ MAKE_INT_SEL(0x15, 0xC204, 2), { true,  nullptr,                 "POINTING DEV - GET TYPE" } },
	{ MAKE_INT_SEL(0x15, 0xC205, 2), { true,  nullptr,                 "POINTING DEV - INITIALIZE" } },
	{ MAKE_INT_SEL(0x15, 0xC206, 2), { true,  nullptr,                 "POINTING DEV - EXTENDED COMMANDS" } },
	{ MAKE_INT_SEL(0x15, 0xC207, 2), { true,  nullptr,                 "POINTING DEV - SET DEVICE HANDLER ADDR" } },
	{ MAKE_INT_SEL(0x15, 0xC208, 2), { true,  nullptr,                 "POINTING DEV - WRITE TO POINTER PORT" } },
	{ MAKE_INT_SEL(0x15, 0xC209, 2), { true,  nullptr,                 "POINTING DEV - READ FROM POINTER PORT" } },
	{ MAKE_INT_SEL(0x15, 0xC500, 1), { false, nullptr,                 "IBM - ROM BIOS TRACING CALLOUT" } },
	{ MAKE_INT_SEL(0x15, 0x8800, 1), { false, nullptr,                 "GET EXTENDED MEMORY SIZE" } },
	/* INT 16 */
	{ MAKE_INT_SEL(0x16, 0x0300, 1), { false, nullptr,                 "KEYB - SET TYPEMATIC RATE AND DELAY" } },
	{ MAKE_INT_SEL(0x16, 0x1100, 1), { false, nullptr,                 "KEYB - CHECK FOR ENHANCED KEYSTROKE" } },
	{ MAKE_INT_SEL(0x16, 0x1200, 1), { false, nullptr,                 "KEYB - GET EXTENDED SHIFT STATES" } },
	/* INT 1A */
	{ MAKE_INT_SEL(0x1A, 0x0000, 1), { false, &CPUDebugger::INT_1A_00, "TIME - GET SYSTEM TIME" } },
	/* INT 1C */
	{ MAKE_INT_SEL(0x1C, 0x0000, 0), { false, nullptr,                 "SYSTEM TIMER TICK" } },
	/* INT 21 */
	{ MAKE_INT_SEL(0x21, 0x0200, 1), { true,  nullptr,                 "DOS - WRITE CHARACTER TO STANDARD OUTPUT" } },
	{ MAKE_INT_SEL(0x21, 0x0600, 1), { false, nullptr,                 "DOS - DIRECT CONSOLE OUTPUT" } },
	{ MAKE_INT_SEL(0x21, 0x0900, 1), { true,  &CPUDebugger::INT_21_09, "DOS - WRITE STRING TO STDOUT" } },
	{ MAKE_INT_SEL(0x21, 0x0A00, 1), { true,  nullptr,                 "DOS - BUFFERED INPUT" } },
	{ MAKE_INT_SEL(0x21, 0x0B00, 1), { true,  nullptr,                 "DOS - GET STDIN STATUS" } },
	{ MAKE_INT_SEL(0x21, 0x0D00, 1), { true,  nullptr,                 "DOS - DISK RESET" } },
	{ MAKE_INT_SEL(0x21, 0x0E00, 1), { true,  &CPUDebugger::INT_21_0E, "DOS - SELECT DEFAULT DRIVE" } },
	{ MAKE_INT_SEL(0x21, 0x1900, 1), { true,  nullptr,                 "DOS - GET CURRENT DEFAULT DRIVE" } },
	{ MAKE_INT_SEL(0x21, 0x1A00, 1), { true,  nullptr,                 "DOS - SET DISK TRANSFER AREA ADDRESS" } },
	{ MAKE_INT_SEL(0x21, 0x2100, 1), { true,  nullptr,                 "DOS - READ RANDOM RECORD FROM FCB FILE" } },
	{ MAKE_INT_SEL(0x21, 0x2500, 1), { true,  &CPUDebugger::INT_21_25, "DOS - SET INTERRUPT VECTOR" } },
	{ MAKE_INT_SEL(0x21, 0x2900, 1), { true,  nullptr,                 "DOS - PARSE FILENAME INTO FCB" } },
	{ MAKE_INT_SEL(0x21, 0x2A00, 1), { true,  nullptr,                 "DOS - GET SYSTEM DATE" } },
	{ MAKE_INT_SEL(0x21, 0x2C00, 1), { false, &CPUDebugger::INT_21_2C, "DOS - GET SYSTEM TIME" } },
	{ MAKE_INT_SEL(0x21, 0x3000, 1), { true,  &CPUDebugger::INT_21_30, "DOS - GET DOS VERSION" } },
	{ MAKE_INT_SEL(0x21, 0x3200, 1), { true,  &CPUDebugger::INT_21_32, "DOS - GET DOS DRIVE PARAMETER BLOCK FOR SPECIFIC DRIVE" } },
	{ MAKE_INT_SEL(0x21, 0x3300, 2), { false, nullptr,                 "DOS - EXTENDED BREAK CHECKING (0)" } },
	{ MAKE_INT_SEL(0x21, 0x3301, 2), { false, nullptr,                 "DOS - EXTENDED BREAK CHECKING (1)" } },
	{ MAKE_INT_SEL(0x21, 0x3400, 1), { false, nullptr,                 "DOS - GET ADDRESS OF INDOS FLAG" } },
	{ MAKE_INT_SEL(0x21, 0x3500, 1), { false, nullptr,                 "DOS - GET INTERRUPT VECTOR" } },
	{ MAKE_INT_SEL(0x21, 0x3600, 1), { true,  &CPUDebugger::INT_21_36, "DOS - GET FREE DISK SPACE" } },
	{ MAKE_INT_SEL(0x21, 0x3700, 2), { true,  nullptr,                 "DOS - GET SWITCH CHARACTER" } },
	{ MAKE_INT_SEL(0x21, 0x3701, 2), { true,  nullptr,                 "DOS - SET SWITCH CHARACTER" } },
	{ MAKE_INT_SEL(0x21, 0x3800, 1), { true,  nullptr,                 "DOS - GET COUNTRY-SPECIFIC INFORMATION" } },
	{ MAKE_INT_SEL(0x21, 0x3900, 1), { true,  &CPUDebugger::INT_21_39_A_B_4E, "DOS - MKDIR" } },
	{ MAKE_INT_SEL(0x21, 0x3A00, 1), { true,  &CPUDebugger::INT_21_39_A_B_4E, "DOS - RMDIR" } },
	{ MAKE_INT_SEL(0x21, 0x3B00, 1), { true,  &CPUDebugger::INT_21_39_A_B_4E, "DOS - CHDIR" } },
	{ MAKE_INT_SEL(0x21, 0x3D00, 1), { true,  &CPUDebugger::INT_21_3D, "DOS - FILE OPEN" } },
	{ MAKE_INT_SEL(0x21, 0x3E00, 1), { true,  &CPUDebugger::INT_21_3E, "DOS - FILE CLOSE" } },
	{ MAKE_INT_SEL(0x21, 0x3F00, 1), { true,  &CPUDebugger::INT_21_3F, "DOS - FILE READ" } },
	{ MAKE_INT_SEL(0x21, 0x4000, 1), { true,  nullptr,                 "DOS - FILE WRITE" } },
	{ MAKE_INT_SEL(0x21, 0x4100, 1), { true,  nullptr,                 "DOS - FILE UNLINK" } },
	{ MAKE_INT_SEL(0x21, 0x4200, 1), { true,  &CPUDebugger::INT_21_42, "DOS - FILE SEEK" } },
	{ MAKE_INT_SEL(0x21, 0x4300, 2), { true,  &CPUDebugger::INT_21_43, "DOS - GET FILE ATTRIBUTES" } },
	{ MAKE_INT_SEL(0x21, 0x4400, 2), { true,  nullptr,                 "DOS - GET DEVICE INFORMATION" } },
	{ MAKE_INT_SEL(0x21, 0x4401, 2), { true,  nullptr,                 "DOS - SET DEVICE INFORMATION" } },
	{ MAKE_INT_SEL(0x21, 0x4408, 2), { true,  nullptr,                 "DOS - IOCTL - CHECK IF BLOCK DEVICE REMOVABLE" } },
	{ MAKE_INT_SEL(0x21, 0x440D, 2), { true,  &CPUDebugger::INT_21_440D,"DOS - IOCTL - GENERIC BLOCK DEVICE REQUEST" } },
	{ MAKE_INT_SEL(0x21, 0x440E, 2), { true,  nullptr,                 "DOS - IOCTL - GET LOGICAL DRIVE MAP" } },
	{ MAKE_INT_SEL(0x21, 0x440F, 2), { true,  nullptr,                 "DOS - IOCTL - SET LOGICAL DRIVE MAP" } },
	{ MAKE_INT_SEL(0x21, 0x4700, 1), { true,  nullptr,                 "DOS - CWD - GET CURRENT DIRECTORY" } },
	{ MAKE_INT_SEL(0x21, 0x4800, 1), { true,  &CPUDebugger::INT_21_48, "DOS - ALLOCATE MEMORY" } },
	{ MAKE_INT_SEL(0x21, 0x4900, 1), { true,  nullptr,                 "DOS - FREE MEMORY" } },
	{ MAKE_INT_SEL(0x21, 0x4A00, 1), { true,  &CPUDebugger::INT_21_4A, "DOS - RESIZE MEMORY BLOCK" } },
	{ MAKE_INT_SEL(0x21, 0x4B00, 1), { true,  &CPUDebugger::INT_21_4B, "DOS - EXEC" } },
	{ MAKE_INT_SEL(0x21, 0x4C00, 1), { true,  nullptr,                 "DOS - EXIT - TERMINATE WITH RETURN CODE" } },
	{ MAKE_INT_SEL(0x21, 0x4D00, 1), { true,  nullptr,                 "DOS - GET RETURN CODE (ERRORLEVEL)" } },
	{ MAKE_INT_SEL(0x21, 0x4E00, 1), { true,  &CPUDebugger::INT_21_39_A_B_4E, "DOS - FINDFIRST" } },
	{ MAKE_INT_SEL(0x21, 0x5000, 1), { true,  nullptr,                 "DOS - SET CURRENT PROCESS ID" } },
	{ MAKE_INT_SEL(0x21, 0x5200, 1), { false, nullptr,                 "DOS - GET LIST OF LISTS" } },
	{ MAKE_INT_SEL(0x21, 0x5D08, 2), { false, nullptr,                 "DOS NET - SET REDIRECTED PRINTER MODE" } },
	{ MAKE_INT_SEL(0x21, 0x5D09, 2), { false, nullptr,                 "DOS NET - FLUSH REDIRECTED PRINTER OUTPUT" } },
	{ MAKE_INT_SEL(0x21, 0x5F02, 2), { true,  nullptr,                 "DOS NET - GET REDIRECTION LIST ENTRY" } },
	{ MAKE_INT_SEL(0x21, 0x5F03, 2), { true,  &CPUDebugger::INT_21_5F03,"DOS NET - REDIRECT DEVICE" } },
	{ MAKE_INT_SEL(0x21, 0x6300, 2), { false, nullptr,                 "DOS - GET DOUBLE BYTE CHARACTER SET LEAD-BYTE TABLE" } },
	{ MAKE_INT_SEL(0x21, 0x6601, 2), { false, nullptr,                 "DOS - GET GLOBAL CODE PAGE TABLE" } },
	{ MAKE_INT_SEL(0x21, 0x6602, 2), { false, nullptr,                 "DOS - SET GLOBAL CODE PAGE TABLE" } },
	{ MAKE_INT_SEL(0x21, 0x6C00, 2), { true,  nullptr,                 "DOS - EXTENDED OPEN/CREATE" } },
	/* INT 28 */
	{ MAKE_INT_SEL(0x28, 0x0000, 0), { false, nullptr,                 "DOS - IDLE INTERRUPT" } },
	/* INT 29 */
	{ MAKE_INT_SEL(0x29, 0x0000, 0), { false, &CPUDebugger::INT_10_0E, "DOS - FAST CONSOLE OUTPUT" } },
	/* INT 2A */
	{ MAKE_INT_SEL(0x2A, 0x8100, 1), { false, nullptr,                 "DOS NET - END CRITICAL SECTION" } },
	{ MAKE_INT_SEL(0x2A, 0x8200, 1), { false, nullptr,                 "DOS NET - END CRITICAL SECTIONS 0-7" } },
	/* INT 2B */
	{ MAKE_INT_SEL(0x2B, 0x0000, 1), { true,  nullptr,                 "IBM - RAM LOADER - fn0" } },
	{ MAKE_INT_SEL(0x2B, 0x0100, 1), { true,  &CPUDebugger::INT_2B_01, "IBM - RAM LOADER - FIND FILE IN ROMDRV" } },
	{ MAKE_INT_SEL(0x2B, 0x0200, 1), { true,  nullptr,                 "IBM - RAM LOADER - COPY FILE FROM ROMDRV" } },
	{ MAKE_INT_SEL(0x2B, 0x0300, 1), { true,  nullptr,                 "IBM - RAM LOADER - fn3" } },
	/* INT 2F */
	{ MAKE_INT_SEL(0x2F, 0x1106, 2), { true,  nullptr,                 "NET REDIR - CLOSE REMOTE FILE" } },
	{ MAKE_INT_SEL(0x2F, 0x1108, 2), { true,  nullptr,                 "NET REDIR - READ FROM REMOTE FILE" } },
	{ MAKE_INT_SEL(0x2F, 0x1116, 2), { true,  &CPUDebugger::INT_2F_1116,"NET REDIR - OPEN EXISTING REMOTE FILE" } },
	{ MAKE_INT_SEL(0x2F, 0x111D, 2), { true,  nullptr,                 "NET REDIR - CLOSE ALL REMOTE FILES FOR PROCESS (ABORT)" } },
	{ MAKE_INT_SEL(0x2F, 0x111E, 2), { true,  nullptr,                 "NET REDIR - DO REDIRECTION" } },
	{ MAKE_INT_SEL(0x2F, 0x1120, 2), { true,  nullptr,                 "NET REDIR - FLUSH ALL DISK BUFFERS" } },
	{ MAKE_INT_SEL(0x2F, 0x1122, 2), { true,  nullptr,                 "NET REDIR - PROCESS TERMINATION HOOK" } },
	{ MAKE_INT_SEL(0x2F, 0x1125, 2), { true,  nullptr,                 "NET REDIR - REDIRECTED PRINTER MODE" } },
	{ MAKE_INT_SEL(0x2F, 0x1123, 2), { true,  &CPUDebugger::INT_2F_1123,"NET REDIR - QUALIFY REMOTE FILENAME" } },
	{ MAKE_INT_SEL(0x2F, 0x1208, 2), { true,  nullptr,                 "DOS - DECREMENT SFT REFERENCE COUNT" } },
	{ MAKE_INT_SEL(0x2F, 0x120C, 2), { true,  nullptr,                 "DOS - OPEN DEVICE AND SET SFT OWNER/MODE" } },
	{ MAKE_INT_SEL(0x2F, 0x1217, 2), { true,  nullptr,                 "DOS - GET CURRENT DIR STRUCTURE FOR DRIVE" } },
	{ MAKE_INT_SEL(0x2F, 0x122E, 2), { true,  nullptr,                 "DOS - GET OR SET ERROR TABLE ADDRESSES" } },
	{ MAKE_INT_SEL(0x2F, 0x122F, 2), { true,  nullptr,                 "DOS - SET DOS VERSION NUMBER TO RETURN" } },
	{ MAKE_INT_SEL(0x2F, 0x1230, 2), { true,  nullptr,                 "W95 - FIND SFT ENTRY IN INTERNAL FILE TABLES" } },
	{ MAKE_INT_SEL(0x2F, 0x1902, 2), { true,  nullptr,                 "SHELLB.COM - COMMAND.COM INTERFACE" } },
	{ MAKE_INT_SEL(0x2F, 0x1980, 2), { true,  nullptr,                 "IBM ROM-DOS v4.0 - INSTALLATION CHECK" } },
	{ MAKE_INT_SEL(0x2F, 0x1981, 2), { true,  nullptr,                 "IBM ROM-DOS v4.0 - GET ??? STRING" } },
	{ MAKE_INT_SEL(0x2F, 0x1982, 2), { true,  nullptr,                 "IBM ROM-DOS v4.0 - GET ??? TABLE" } },
	{ MAKE_INT_SEL(0x2F, 0x1A01, 2), { true,  nullptr,                 "DOS 4.0+ ANSI.SYS internal - GET/SET DISPLAY INFORMATION" } },
	{ MAKE_INT_SEL(0x2F, 0x1A02, 2), { true,  nullptr,                 "DOS 4.0+ ANSI.SYS internal - MISCELLANEOUS REQUESTS" } },
	{ MAKE_INT_SEL(0x2F, 0xAE00, 2), { true,  nullptr,                 "DOS - INSTALLABLE COMMAND - INSTALLATION CHECK" } },
	{ MAKE_INT_SEL(0x2F, 0xB000, 2), { true,  nullptr,                 "DOS 3.3+ GRAFTABL.COM - INSTALLATION CHECK" } },
	{ MAKE_INT_SEL(0x2F, 0xB711, 2), { true,  nullptr,                 "DOS - SET RETURN FOUND NAME STATE" } },
	/* INT 33 */
	{ MAKE_INT_SEL(0x33, 0x0000, 2), { true,  nullptr,                 "MS MOUSE - RESET DRIVER AND READ STATUS" } },
	{ MAKE_INT_SEL(0x33, 0x0001, 2), { true,  nullptr,                 "MS MOUSE - SHOW MOUSE CURSOR" } },
	{ MAKE_INT_SEL(0x33, 0x0002, 2), { true,  nullptr,                 "MS MOUSE - HIDE MOUSE CURSOR" } },
	{ MAKE_INT_SEL(0x33, 0x0003, 2), { true,  nullptr,                 "MS MOUSE - RETURN POSITION AND BUTTON STATUS" } },
	{ MAKE_INT_SEL(0x33, 0x0007, 2), { true,  nullptr,                 "MS MOUSE - DEFINE HORIZONTAL CURSOR RANGE" } },
	{ MAKE_INT_SEL(0x33, 0x0008, 2), { true,  nullptr,                 "MS MOUSE - DEFINE VERTICAL CURSOR RANGE" } },
	{ MAKE_INT_SEL(0x33, 0x0009, 2), { true,  nullptr,                 "MS MOUSE - DEFINE GRAPHICS CURSOR" } },
	{ MAKE_INT_SEL(0x33, 0x000A, 2), { true,  nullptr,                 "MS MOUSE - DEFINE TEXT CURSOR" } },
	{ MAKE_INT_SEL(0x33, 0x000C, 2), { true,  nullptr,                 "MS MOUSE - DEFINE INTERRUPT SUBROUTINE PARAMETERS" } },
	{ MAKE_INT_SEL(0x33, 0x0021, 2), { true,  nullptr,                 "MS MOUSE - SOFTWARE RESET" } },
	{ MAKE_INT_SEL(0x33, 0x0024, 2), { true,  nullptr,                 "MS MOUSE - GET SOFTWARE VERSION, MOUSE TYPE, AND IRQ NUMBER" } },
	{ MAKE_INT_SEL(0x33, 0x0026, 2), { true,  nullptr,                 "MS MOUSE - GET MAXIMUM VIRTUAL COORDINATES" } },
	{ MAKE_INT_SEL(0x33, 0x006D, 2), { true,  nullptr,                 "MS MOUSE - GET VERSION STRING" } }

};

doscodes_map_t CPUDebugger::ms_dos_errors = {
	{ 0x00, "no error" },
	{ 0x01, "function number invalid" },
	{ 0x02, "file not found" },
	{ 0x03, "path not found" },
	{ 0x04, "too many open files (no handles available)" },
	{ 0x05, "access denied" },
	{ 0x06, "invalid handle" },
	{ 0x07, "memory control block destroyed" },
	{ 0x08, "insufficient memory" },
	{ 0x09, "memory block address invalid" },
	{ 0x0A, "environment invalid (usually >32K in length)" },
	{ 0x0B, "format invalid" },
	{ 0x0C, "access code invalid" },
	{ 0x0D, "data invalid" },
	{ 0x0E, "reserved" },
	{ 0x0E, "(PTS-DOS 6.51+, S/DOS 1.0+) fixup overflow" },
	{ 0x0F, "invalid drive" },
	{ 0x10, "attempted to remove current directory" },
	{ 0x11, "not same device" },
	{ 0x12, "no more files" },
	{ 0x13, "disk write-protected" },
	{ 0x14, "unknown unit" },
	{ 0x15, "drive not ready" },
	{ 0x16, "unknown command" },
	{ 0x17, "data error (CRC)" },
	{ 0x18, "bad request structure length" },
	{ 0x19, "seek error" },
	{ 0x1A, "unknown media type (non-DOS disk)" },
	{ 0x1B, "sector not found" },
	{ 0x1C, "printer out of paper" },
	{ 0x1D, "write fault" },
	{ 0x1E, "read fault" },
	{ 0x1F, "general failure" },
	{ 0x20, "sharing violation" },
	{ 0x21, "lock violation" },
	{ 0x22, "disk change invalid (ES:DI -> media ID structure)(see #01681)" },
	{ 0x23, "FCB unavailable" },
	{ 0x23, "(PTS-DOS 6.51+, S/DOS 1.0+) bad FAT" },
	{ 0x24, "sharing buffer overflow" },
	{ 0x25, "(DOS 4.0+) code page mismatch" },
	{ 0x26, "(DOS 4.0+) cannot complete file operation (EOF / out of input)" },
	{ 0x27, "(DOS 4.0+) insufficient disk space" },
	{ 0x28, "reserved" },
	{ 0x29, "reserved" },
	{ 0x2A, "reserved" },
	{ 0x2B, "reserved" },
	{ 0x2C, "reserved" },
	{ 0x2D, "reserved" },
	{ 0x2E, "reserved" },
	{ 0x2F, "reserved" },
	{ 0x30, "reserved" },
	{ 0x31, "reserved" },
	{ 0x32, "network request not supported" },
	{ 0x33, "remote computer not listening" },
	{ 0x34, "duplicate name on network" },
	{ 0x35, "network name not found" },
	{ 0x36, "network busy" },
	{ 0x37, "network device no longer exists" },
	{ 0x38, "network BIOS command limit exceeded" },
	{ 0x39, "network adapter hardware error" },
	{ 0x3A, "incorrect response from network" },
	{ 0x3B, "unexpected network error" },
	{ 0x3C, "incompatible remote adapter" },
	{ 0x3D, "print queue full" },
	{ 0x3E, "queue not full" },
	{ 0x3F, "not enough space to print file" },
	{ 0x40, "network name was deleted" },
	{ 0x41, "network: Access denied / codepage switching not possible" },
	{ 0x42, "network device type incorrect" },
	{ 0x43, "network name not found" },
	{ 0x44, "network name limit exceeded" },
	{ 0x45, "network BIOS session limit exceeded" },
	{ 0x46, "temporarily paused" },
	{ 0x47, "network request not accepted" },
	{ 0x48, "network print/disk redirection paused" },
	{ 0x49, "network software not installed" },
	{ 0x4A, "unexpected adapter close" },
	{ 0x4B, "(LANtastic) password expired" },
	{ 0x4C, "(LANtastic) login attempt invalid at this time" },
	{ 0x4D, "(LANtastic v3+) disk limit exceeded on network node" },
	{ 0x4E, "(LANtastic v3+) not logged in to network node" },
	{ 0x4F, "reserved" },
	{ 0x50, "file exists" },
	{ 0x51, "(undoc) duplicated FCB" },
	{ 0x52, "cannot make directory" },
	{ 0x53, "fail on INT 24h" },
	{ 0x54, "(DOS 3.3+) too many redirections / out of structures" },
	{ 0x55, "(DOS 3.3+) duplicate redirection / already assigned" },
	{ 0x56, "(DOS 3.3+) invalid password" },
	{ 0x57, "(DOS 3.3+) invalid parameter" },
	{ 0x58, "(DOS 3.3+) network write fault" },
	{ 0x59, "(DOS 4.0+) function not supported on network / no process slots available" },
	{ 0x5A, "(DOS 4.0+) required system component not installed / not frozen" },
	{ 0x5B, "(DOS 4.0+,NetWare4) timer server table overflowed" },
	{ 0x5C, "(DOS 4.0+,NetWare4) duplicate in timer service table" },
	{ 0x5D, "(DOS 4.0+,NetWare4) no items to work on" },
	{ 0x5F, "(DOS 4.0+,NetWare4) interrupted / invalid system call" },
	{ 0x64, "(MSCDEX) unknown error" },
	{ 0x64, "(DOS 4.0+,NetWare4) open semaphore limit exceeded" },
	{ 0x65, "(MSCDEX) not ready" },
	{ 0x65, "(DOS 4.0+,NetWare4) exclusive semaphore is already owned" },
	{ 0x66, "(MSCDEX) EMS memory no longer valid" },
	{ 0x66, "(DOS 4.0+,NetWare4) semaphore was set when close attempted" },
	{ 0x67, "(MSCDEX) not High Sierra or ISO-9660 format" },
	{ 0x67, "(DOS 4.0+,NetWare4) too many exclusive semaphore requests" },
	{ 0x68, "(MSCDEX) door open" },
	{ 0x68, "(DOS 4.0+,NetWare4) operation invalid from interrupt handler" },
	{ 0x69, "(DOS 4.0+,NetWare4) semaphore owner died" },
	{ 0x6A, "(DOS 4.0+,NetWare4) semaphore limit exceeded" },
	{ 0x6B, "(DOS 4.0+,NetWare4) insert drive B: disk into A: / disk changed" },
	{ 0x6C, "(DOS 4.0+,NetWare4) drive locked by another process" },
	{ 0x6D, "(DOS 4.0+,NetWare4) broken pipe" },
	{ 0x6E, "(DOS 5.0+,NetWare4) pipe open/create failed" },
	{ 0x6F, "(DOS 5.0+,NetWare4) pipe buffer overflowed" },
	{ 0x70, "(DOS 5.0+,NetWare4) disk full" },
	{ 0x71, "(DOS 5.0+,NetWare4) no more search handles" },
	{ 0x72, "(DOS 5.0+,NetWare4) invalid target handle for dup2" },
	{ 0x73, "(DOS 5.0+,NetWare4) bad user virtual address / protection violation" },
	{ 0x74, "(DOS 5.0+) VIOKBD request" },
	{ 0x74, "(NetWare4) error on console I/O" },
	{ 0x75, "(DOS 5.0+,NetWare4) unknown category code for IOCTL" },
	{ 0x76, "(DOS 5.0+,NetWare4) invalid value for verify flag" },
	{ 0x77, "(DOS 5.0+,NetWare4) level four driver not found by DOS IOCTL" },
	{ 0x78, "(DOS 5.0+,NetWare4) invalid / unimplemented function number" },
	{ 0x79, "(DOS 5.0+,NetWare4) semaphore timeout" },
	{ 0x7A, "(DOS 5.0+,NetWare4) buffer too small to hold return data" },
	{ 0x7B, "(DOS 5.0+,NetWare4) invalid character or bad file-system name" },
	{ 0x7C, "(DOS 5.0+,NetWare4) unimplemented information level" },
	{ 0x7D, "(DOS 5.0+,NetWare4) no volume label found" },
	{ 0x7E, "(DOS 5.0+,NetWare4) module handle not found" },
	{ 0x7F, "(DOS 5.0+,NetWare4) procedure address not found" },
	{ 0x80, "(DOS 5.0+,NetWare4) CWait found no children" },
	{ 0x81, "(DOS 5.0+,NetWare4) CWait children still running" },
	{ 0x82, "(DOS 5.0+,NetWare4) invalid operation for direct disk-access handle" },
	{ 0x83, "(DOS 5.0+,NetWare4) attempted seek to negative offset" },
	{ 0x84, "(DOS 5.0+,NetWare4) attempted to seek on device or pipe" },
	{ 0x85, "(DOS 5.0+,NetWare4) drive already has JOINed drives" },
	{ 0x86, "(DOS 5.0+,NetWare4) drive is already JOINed" },
	{ 0x87, "(DOS 5.0+,NetWare4) drive is already SUBSTed" },
	{ 0x88, "(DOS 5.0+,NetWare4) can not delete drive which is not JOINed" },
	{ 0x89, "(DOS 5.0+,NetWare4) can not delete drive which is not SUBSTed" },
	{ 0x8A, "(DOS 5.0+,NetWare4) can not JOIN to a JOINed drive" },
	{ 0x8B, "(DOS 5.0+,NetWare4) can not SUBST to a SUBSTed drive" },
	{ 0x8C, "(DOS 5.0+,NetWare4) can not JOIN to a SUBSTed drive" },
	{ 0x8D, "(DOS 5.0+,NetWare4) can not SUBST to a JOINed drive" },
	{ 0x8E, "(DOS 5.0+,NetWare4) drive is busy" },
	{ 0x8F, "(DOS 5.0+,NetWare4) can not JOIN/SUBST to same drive" },
	{ 0x90, "(DOS 5.0+,NetWare4) directory must not be root directory" },
	{ 0x91, "(DOS 5.0+,NetWare4) can only JOIN to empty directory" },
	{ 0x92, "(DOS 5.0+,NetWare4) path is already in use for SUBST" },
	{ 0x93, "(DOS 5.0+,NetWare4) path is already in use for JOIN" },
	{ 0x94, "(DOS 5.0+,NetWare4) path is in use by another process" },
	{ 0x95, "(DOS 5.0+,NetWare4) directory previously SUBSTituted" },
	{ 0x96, "(DOS 5.0+,NetWare4) system trace error" },
	{ 0x97, "(DOS 5.0+,NetWare4) invalid event count for DosMuxSemWait" },
	{ 0x98, "(DOS 5.0+,NetWare4) too many waiting on mutex" },
	{ 0x99, "(DOS 5.0+,NetWare4) invalid list format" },
	{ 0x9A, "(DOS 5.0+,NetWare4) volume label too large" },
	{ 0x9B, "(DOS 5.0+,NetWare4) unable to create another TCB" },
	{ 0x9C, "(DOS 5.0+,NetWare4) signal refused" },
	{ 0x9D, "(DOS 5.0+,NetWare4) segment discarded" },
	{ 0x9E, "(DOS 5.0+,NetWare4) segment not locked" },
	{ 0x9F, "(DOS 5.0+,NetWare4) invalid thread-ID address" },
	{ 0xA0, "(DOS 5.0+) bad arguments" },
	{ 0xA0, "(NetWare4) bad environment pointer" },
	{ 0xA1, "(DOS 5.0+,NetWare4) invalid pathname passed to EXEC" },
	{ 0xA2, "(DOS 5.0+,NetWare4) signal already pending" },
	{ 0xA3, "(DOS 5.0+) uncertain media" },
	{ 0xA3, "(NetWare4) ERROR_124 mapping" },
	{ 0xA4, "(DOS 5.0+) maximum number of threads reached" },
	{ 0xA4, "(NetWare4) no more process slots" },
	{ 0xA5, "(NetWare4) ERROR_124 mapping" },
	{ 0xB0, "(MS-DOS 7.0) volume is not locked" },
	{ 0xB1, "(MS-DOS 7.0) volume is locked in drive" },
	{ 0xB2, "(MS-DOS 7.0) volume is not removable" },
	{ 0xB4, "(MS-DOS 7.0) lock count has been exceeded" },
	{ 0xB4, "(NetWare4) invalid segment number" },
	{ 0xB5, "(MS-DOS 7.0) a valid eject request failed" },
	{ 0xB5, "(DOS 5.0-6.0,NetWare4) invalid call gate" },
	{ 0xB6, "(DOS 5.0+,NetWare4) invalid ordinal" },
	{ 0xB7, "(DOS 5.0+,NetWare4) shared segment already exists" },
	{ 0xB8, "(DOS 5.0+,NetWare4) no child process to wait for" },
	{ 0xB9, "(DOS 5.0+,NetWare4) NoWait specified and child still running" },
	{ 0xBA, "(DOS 5.0+,NetWare4) invalid flag number" },
	{ 0xBB, "(DOS 5.0+,NetWare4) semaphore does not exist" },
	{ 0xBC, "(DOS 5.0+,NetWare4) invalid starting code segment" },
	{ 0xBD, "(DOS 5.0+,NetWare4) invalid stack segment" },
	{ 0xBE, "(DOS 5.0+,NetWare4) invalid module type (DLL can not be used as application)" },
	{ 0xBF, "(DOS 5.0+,NetWare4) invalid EXE signature" },
	{ 0xC0, "(DOS 5.0+,NetWare4) EXE marked invalid" },
	{ 0xC1, "(DOS 5.0+,NetWare4) bad EXE format (e.g. DOS-mode program)" },
	{ 0xC2, "(DOS 5.0+,NetWare4) iterated data exceeds 64K" },
	{ 0xC3, "(DOS 5.0+,NetWare4) invalid minimum allocation size" },
	{ 0xC4, "(DOS 5.0+,NetWare4) dynamic link from invalid Ring" },
	{ 0xC5, "(DOS 5.0+,NetWare4) IOPL not enabled" },
	{ 0xC6, "(DOS 5.0+,NetWare4) invalid segment descriptor privilege level" },
	{ 0xC7, "(DOS 5.0+,NetWare4) automatic data segment exceeds 64K" },
	{ 0xC8, "(DOS 5.0+,NetWare4) Ring2 segment must be moveable" },
	{ 0xC9, "(DOS 5.0+,NetWare4) relocation chain exceeds segment limit" },
	{ 0xCA, "(DOS 5.0+,NetWare4) infinite loop in relocation chain" },
	{ 0xCB, "(NetWare4) environment variable not found" },
	{ 0xCC, "(NetWare4) not current country" },
	{ 0xCD, "(NetWare4) no signal sent" },
	{ 0xCE, "(NetWare4) file name not 8.3" },
	{ 0xCF, "(NetWare4) Ring2 stack in use" },
	{ 0xD0, "(NetWare4) meta expansion is too long" },
	{ 0xD1, "(NetWare4) invalid signal number" },
	{ 0xD2, "(NetWare4) inactive thread" },
	{ 0xD3, "(NetWare4) file system information not available" },
	{ 0xD4, "(NetWare4) locked error" },
	{ 0xD5, "(NetWare4) attempted to execute non-family API call in DOS mode" },
	{ 0xD6, "(NetWare4) too many modules" },
	{ 0xD7, "(NetWare4) nesting not allowed" },
	{ 0xE6, "(NetWare4) non-existent pipe, or bad operation" },
	{ 0xE7, "(NetWare4) pipe is busy" },
	{ 0xE8, "(NetWare4) no data available for nonblocking read" },
	{ 0xE9, "(NetWare4) pipe disconnected by server" },
	{ 0xEA, "(NetWare4) more data available" },
	{ 0xFF, "(NetWare4) invalid drive" }
};

doscodes_map_t CPUDebugger::ms_disk_status = {
	{ 0x00, "successful completion" },
	{ 0x01, "invalid function in AH or invalid parameter" },
	{ 0x02, "address mark not found" },
	{ 0x03, "disk write-protected" },
	{ 0x04, "sector not found/read error" },
	{ 0x05, "reset failed (hard disk)" },
	{ 0x05, "data did not verify correctly (TI Professional PC)" },
	{ 0x06, "disk changed (floppy)" },
	{ 0x07, "drive parameter activity failed (hard disk)" },
	{ 0x08, "DMA overrun" },
	{ 0x09, "data boundary error (attempted DMA across 64K boundary or >80h sectors)" },
	{ 0x0A, "bad sector detected (hard disk)" },
	{ 0x0B, "bad track detected (hard disk)" },
	{ 0x0C, "unsupported track or invalid media" },
	{ 0x0D, "invalid number of sectors on format (PS/2 hard disk)" },
	{ 0x0E, "control data address mark detected (hard disk)" },
	{ 0x0F, "DMA arbitration level out of range (hard disk)" },
	{ 0x10, "uncorrectable CRC or ECC error on read" },
	{ 0x11, "data ECC corrected (hard disk)" },
	{ 0x20, "controller failure" },
	{ 0x31, "no media in drive (IBM/MS INT 13 extensions)" },
	{ 0x32, "incorrect drive type stored in CMOS (Compaq)" },
	{ 0x40, "seek failed" },
	{ 0x80, "timeout (not ready)" },
	{ 0xAA, "drive not ready (hard disk)" },
	{ 0xB0, "volume not locked in drive (INT 13 extensions)" },
	{ 0xB1, "volume locked in drive (INT 13 extensions)" },
	{ 0xB2, "volume not removable (INT 13 extensions)" },
	{ 0xB3, "volume in use (INT 13 extensions)" },
	{ 0xB4, "lock count exceeded (INT 13 extensions)" },
	{ 0xB5, "valid eject request failed (INT 13 extensions)" },
	{ 0xB6, "volume present but read protected (INT 13 extensions)" },
	{ 0xBB, "undefined error (hard disk)" },
	{ 0xCC, "write fault (hard disk)" },
	{ 0xE0, "status register error (hard disk)" },
	{ 0xFF, "sense operation failed (hard disk)" }
};

doscodes_map_t CPUDebugger::ms_ioctl_code = {
	{ 0x00, "(OS/2) lock drive" },
	{ 0x01, "(OS/2) unlock drive" },
	{ 0x40, "set device parameters" },
	{ 0x41, "write logical device track" },
	{ 0x42, "format and verify logical device track" },
	{ 0x46, "(DOS 4.0+) set volume serial number" },
	{ 0x47, "(DOS 4.0+) set access flag" },
	{ 0x48, "(Enh. Disk Drive Spec) set media lock state" },
	{ 0x49, "(Enh. Disk Drive Spec) eject media in drive" },
	{ 0x4A, "(MS-DOS 7.0) lock logical volume" },
	{ 0x4B, "(MS-DOS 7.0) lock physical volume" },
	{ 0x50, "(PCMCIA) attribute memory write" },
	{ 0x51, "(PCMCIA) common memory write" },
	{ 0x52, "(PCMCIA) force media change" },
	{ 0x53, "(PCMCIA) erase drive" },
	{ 0x54, "(PCMCIA) erase media" },
	{ 0x56, "(PCMCIA) set erase status callback" },
	{ 0x57, "(PCMCIA) append Card Information Structure (CIS) tuple" },
	{ 0x58, "(PCMCIA) erase CIS tuples" },
	{ 0x60, "get device parameters" },
	{ 0x61, "read logical device track" },
	{ 0x62, "verify logical device track" },
	{ 0x66, "(DOS 4.0+) get volume serial number" },
	{ 0x67, "(DOS 4.0+) get access flag" },
	{ 0x68, "(DOS 5.0+) sense media type" },
	{ 0x6A, "(MS-DOS 7.0) unlock logical volume" },
	{ 0x6B, "(MS-DOS 7.0) unlock physical volume" },
	{ 0x6C, "(MS-DOS 7.0) get lock flag" },
	{ 0x6D, "(MS-DOS 7.0) enumerate open files" },
	{ 0x6E, "(MS-DOS 7.0) find swap file" },
	{ 0x6F, "(MS-DOS 7.0) get drive map information" },
	{ 0x70, "(PCMCIA) attribute memory read" },
	{ 0x70, "(MS-DOS 7.0) get current lock state" },
	{ 0x71, "(MS-DOS 7.0) get first cluster" },
	{ 0x73, "(PCMCIA) get memory media information" },
	{ 0x76, "(PCMCIA) get erase status callback" },
	{ 0x77, "(PCMCIA) get first Card Information Structure (CIS) tuple" },
	{ 0x78, "(PCMCIA) get next CIS tuple" },
	{ 0x7F, "(PCMCIA) get ??? information" }
};
