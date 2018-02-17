/*
 * Copyright (C) 2018  Marco Bortolin
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
#include "../debugger.h"
#include "hardware/cpu.h"
#include <cstring>

void update_buffer(char* &buf, unsigned &buflen)
{
	unsigned len = strlen(buf);
	buf += len;
	buflen -= len;
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

static const char *ctrl_chars[7] = {
	"\\a",  //07 Alert (Beep, Bell)
	"\\b",  //08 Backspace
	"\\t",  //09 Horizontal Tab
	"\\n",  //0A Newline (Line Feed)
	"\\v",  //0B Vertical Tab
	"\\f",  //0C Formfeed
	"\\r"   //0D Carriage Return
};

void print_char_to_buf(char c, char* buf, unsigned buflen)
{
	if(c>=32 && c!=127) {
		snprintf(buf, buflen, ": '%c'", c);
	} else if(c==0) {
		snprintf(buf, buflen, ": '\\0'");
	} else if(c>=7 && c<=13) {
		snprintf(buf, buflen, ": '%s'", ctrl_chars[c-7]);
	} else {
		snprintf(buf, buflen, ": 0x%02X", c);
	}
}

void CPUDebugger::INT_10(bool call, uint16_t ax, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	if(!call) {
		INT_def_ret(core, buf, buflen);
		return;
	}
	uint8_t ah = ax>>8;
	uint8_t al = ax&0xFF;
	switch(ah) {
		case 0x00: // INT 0x29, FAST CONSOLE OUTPUT
		case 0x09:
		case 0x0A:
		case 0x0E: {
			print_char_to_buf(al, buf, buflen);
			break;
		}
		case 0x13: {
			uint32_t addr;
			char *ptr;
			try {
				addr = core->dbg_get_phyaddr(REGI_ES, core->get_BP(), mem);
				ptr = (char*)mem->get_buffer_ptr(addr);
			} catch(...) {
				return;
			}
			std::string str;
			//AL bit 1: string contains alternating characters and attributes
			int step = (al&2)?2:1;
			int len = core->get_CX();
			for(int i=0; i<len; i++) {
				char c = *(ptr+i*step);
				if(c==0) {
					str += "\\0";
				} else if(c>=7 && c<=13) {
					str += ctrl_chars[c-7];
				} else if(c<32 || c==127) {
					str += 32;
				} else {
					str += c;
				}
			}
			snprintf(buf, buflen, " pos=%dx%d, str=%s",
				core->get_DH(), core->get_DL(), str.c_str());
			break;
		}
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

void CPUDebugger::INT_13(bool call, uint16_t ax, CPUCore *core, Memory *mem, char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		const char * status = ms_disk_status[core->get_AH()];
		snprintf(buf, buflen, " ret CF=%u: AH=%u (%s)", cf,core->get_AH(),status);
		if(cf == 0) {
			if((ax>>8) == 0x25) { // IDENTIFY DRIVE (PS/1)
				size_t blen = strlen(buf);
				char model[41];
				model[40] = 0;
				uint32_t addr = core->dbg_get_phyaddr(REGI_ES, core->get_BX(), mem);
				uint8_t *info = (uint8_t*)mem->get_buffer_ptr(addr);
				memcpy(model, info+0x36, 40);
				snprintf(buf+blen, buflen-blen, " CHS %d/%d/%d \"%s\"",
					*(uint16_t*)(info+0x2),
					*(uint16_t*)(info+0x6),
					*(uint16_t*)(info+0xC),
					model);
			}
		}
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

void CPUDebugger::INT_20(bool call, uint16_t /*ax*/, CPUCore *core, Memory *mem,
		char* buf, uint buflen)
{
	uint32_t vxd = 0;
	try {
		uint32_t vxd_addr = core->dbg_get_phyaddr(REGI_CS, core->get_EIP(), mem);
		vxd = *(uint32_t*)mem->get_buffer_ptr(vxd_addr);
	} catch(...) {
		return;
	}
	uint16_t service = vxd;
	uint16_t device = vxd>>16;

	snprintf(buf, buflen, "DOS - TERM. PROG. / Windows - VxD %04x:%04x ", device, service);
	update_buffer(buf, buflen);

	snprintf(buf, buflen, "%s", CPUDebugger::ms_int20_vxd[device]);
	update_buffer(buf, buflen);

	if(device == 0x0001) {
		snprintf(buf, buflen, ":%s", CPUDebugger::ms_int20_vmm[service]);
		update_buffer(buf, buflen);
	}

	if(!call) {
		INT_def_ret(core, buf, buflen);
	}
}

void CPUDebugger::INT_21_02(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(call) {
		print_char_to_buf(core->get_DL(), buf, buflen);
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

	snprintf(buf, buflen, " : handle=%d, bytes=%d, buffer=%04X:%04X",
			core->get_BX(), core->get_CX(), core->get_DS().sel.value, core->get_DX());
}

void CPUDebugger::INT_21_40(bool call, uint16_t /*ax*/, CPUCore *core, Memory */*mem*/,
		char* buf, uint buflen)
{
	if(!call) {
		uint cf = core->get_FLAGS(FMASK_CF)>>FBITN_CF;
		if(cf) {
			snprintf(buf, buflen, " ret CF=1: %s", ms_dos_errors[core->get_AX()]);
		} else {
			snprintf(buf, buflen, " ret CF=0: %d bytes written", core->get_AX());
		}
		return;
	}

	snprintf(buf, buflen, " : handle=%d, bytes=%d, buffer=%04X:%04X",
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
