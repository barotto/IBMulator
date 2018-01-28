/*
 * Copyright (C) 2015-2018  Marco Bortolin
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
#include <cstring>
#include <iomanip>

#define DECODE_ALL_INT false

/*
 * TODO this map is valid only for a specific version of the PS/1 2011 BIOS
 */
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


unsigned CPUDebugger::disasm(char *_buf, uint _buflen, uint32_t _cs, uint32_t _eip,
		CPUCore *_core, Memory *_mem, const uint8_t *_instr_buf, uint _instr_buf_len, bool _32bit)
{
	return m_dasm.disasm(_buf, _buflen, _cs, _eip, _core, _mem, _instr_buf, _instr_buf_len, _32bit);
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

		if(!interr->second.decode && !DECODE_ALL_INT) {
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

