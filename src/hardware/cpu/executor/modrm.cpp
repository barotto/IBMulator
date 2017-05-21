/*
 * Copyright (C) 2016  Marco Bortolin
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
#include "hardware/cpu/executor.h"

SegReg & CPUExecutor::EA_get_segreg_16()
{
	switch(m_instr->modrm.rm) {
		case 0:
		case 1:
		case 4:
		case 5:
		case 7:
			return SEG_REG(m_base_ds);
		case 2:
		case 3:
			return SEG_REG(m_base_ss);
		case 6:
			if(m_instr->modrm.mod==0)
				return SEG_REG(m_base_ds);
			return SEG_REG(m_base_ss);
	}

	assert(false);

	//keep compiler happy, but you really don't want to end here!
	return REG_DS;
}

uint32_t CPUExecutor::EA_get_offset_16()
{
	uint16_t disp = m_instr->modrm.disp & 0xFFFF;
	uint16_t offset = 0;
	switch(m_instr->modrm.rm) {
		case 0:
			offset = (REG_BX + REG_SI + disp); break;
		case 1:
			offset = (REG_BX + REG_DI + disp); break;
		case 2:
			offset = (REG_BP + REG_SI + disp); break;
		case 3:
			offset = (REG_BP + REG_DI + disp); break;
		case 4:
			offset = (REG_SI + disp); break;
		case 5:
			offset = (REG_DI + disp); break;
		case 6:
			if(m_instr->modrm.mod==0) {
				offset = disp;
			} else {
				offset = (REG_BP + disp);
			}
			 break;
		case 7:
			offset = (REG_BX + disp); break;
	}
	return offset;
}

SegReg & CPUExecutor::EA_get_segreg_32()
{
	if(m_instr->modrm.rm != 4) {
		// no SIB
		if(m_instr->modrm.mod == 0) {
			return SEG_REG(m_base_ds);
		}
		if(m_instr->modrm.rm == 5) {
			return SEG_REG(m_base_ss);
		} else {
			return SEG_REG(m_base_ds);
		}
	} else {
		// SIB
		if(m_instr->modrm.base == 4) {
			return SEG_REG(m_base_ss);
		}
		if(m_instr->modrm.mod == 0) {
			return SEG_REG(m_base_ds);
		} else {
			if(m_instr->modrm.base == 5) {
				return SEG_REG(m_base_ss);
			} else {
				return SEG_REG(m_base_ds);
			}
		}
	}
}

uint32_t CPUExecutor::EA_get_offset_32()
{
	uint32_t offset = m_instr->modrm.disp;

	if(m_instr->modrm.rm != 4) {
		// no SIB
		if(m_instr->modrm.rm != 5 || m_instr->modrm.mod != 0) {
			offset += GEN_REG(m_instr->modrm.rm).dword[0];
		}
	} else {
		// SIB
		if(m_instr->modrm.index != 4) {
			offset += GEN_REG(m_instr->modrm.index).dword[0] * (1 << m_instr->modrm.scale);
		}
		if(m_instr->modrm.base != 5 || m_instr->modrm.mod != 0) {
			offset += GEN_REG(m_instr->modrm.base).dword[0];
		}
	}
	return offset;
}

uint8_t CPUExecutor::load_eb()
{
	if(m_instr->modrm.mod == 3) {
		if(m_instr->modrm.rm < 4) {
			return GEN_REG(m_instr->modrm.rm).byte[0];
		}
		return GEN_REG(m_instr->modrm.rm-4).byte[1];
	}
	return read_byte((this->*EA_get_segreg)(), (this->*EA_get_offset)());
}

uint8_t CPUExecutor::load_rb()
{
	if(m_instr->modrm.r < 4) {
		return GEN_REG(m_instr->modrm.r).byte[0];
	}
	return GEN_REG(m_instr->modrm.r-4).byte[1];
}

uint16_t CPUExecutor::load_ew()
{
	if(m_instr->modrm.mod == 3) {
		return GEN_REG(m_instr->modrm.rm).word[0];
	}
	return read_word((this->*EA_get_segreg)(), (this->*EA_get_offset)());
}

uint16_t CPUExecutor::load_rw()
{
	return GEN_REG(m_instr->modrm.r).word[0];
}

uint16_t CPUExecutor::load_rw_op()
{
	return GEN_REG(m_instr->reg).word[0];
}

uint32_t CPUExecutor::load_ed()
{
	if(m_instr->modrm.mod == 3) {
		return GEN_REG(m_instr->modrm.rm).dword[0];
	}
	return read_dword((this->*EA_get_segreg)(), (this->*EA_get_offset)());
}

void CPUExecutor::load_m1616(uint16_t &w1_, uint16_t &w2_)
{
	SegReg & sr = (this->*EA_get_segreg)();
	uint32_t off = (this->*EA_get_offset)();

	w1_ = read_word(sr, off);
	w2_ = read_word(sr, (off+2) & m_addr_mask);
}

void CPUExecutor::load_m1632(uint32_t &dw1_, uint16_t &w2_)
{
	SegReg & sr = (this->*EA_get_segreg)();
	uint32_t off = (this->*EA_get_offset)();

	//little endian
	dw1_ = read_dword(sr, off);
	 w2_ = read_word(sr, (off+4) & m_addr_mask);
}

void CPUExecutor::load_m3232(uint32_t &dw1_, uint32_t &dw2_)
{
	SegReg & sr = (this->*EA_get_segreg)();
	uint32_t off = (this->*EA_get_offset)();

	dw1_ = read_dword(sr, off);
	dw2_ = read_dword(sr, (off+4) & m_addr_mask);
}

uint32_t CPUExecutor::load_rd()
{
	return GEN_REG(m_instr->modrm.r).dword[0];
}

uint32_t CPUExecutor::load_rd_op()
{
	return GEN_REG(m_instr->reg).dword[0];
}

uint16_t CPUExecutor::load_sr()
{
	return SEG_REG(m_instr->modrm.r).sel.value;
}

void CPUExecutor::store_eb(uint8_t _value)
{
	if(m_instr->modrm.mod == 3) {
		if(m_instr->modrm.rm < 4) {
			GEN_REG(m_instr->modrm.rm).byte[0] = _value;
			return;
		}
		GEN_REG(m_instr->modrm.rm-4).byte[1] = _value;
		return;
	}
	write_byte((this->*EA_get_segreg)(), (this->*EA_get_offset)(), _value);
}

void CPUExecutor::store_rb(uint8_t _value)
{
	if(m_instr->modrm.r < 4) {
		GEN_REG(m_instr->modrm.r).byte[0] = _value;
	} else {
		GEN_REG(m_instr->modrm.r-4).byte[1] = _value;
	}
}

void CPUExecutor::store_rb_op(uint8_t _value)
{
	if(m_instr->reg < 4) {
		GEN_REG(m_instr->reg).byte[0] = _value;
	} else {
		GEN_REG(m_instr->reg-4).byte[1] = _value;
	}
}

void CPUExecutor::store_ew(uint16_t _value)
{
	if(m_instr->modrm.mod == 3) {
		GEN_REG(m_instr->modrm.rm).word[0] = _value;
	} else {
		write_word((this->*EA_get_segreg)(), (this->*EA_get_offset)(), _value);
	}
}

void CPUExecutor::store_ew_rmw(uint16_t _value)
{
	if(m_instr->modrm.mod == 3) {
		GEN_REG(m_instr->modrm.rm).word[0] = _value;
	} else {
		write_word(_value);
	}
}

void CPUExecutor::store_rw(uint16_t _value)
{
	GEN_REG(m_instr->modrm.r).word[0] = _value;
}

void CPUExecutor::store_rw_op(uint16_t _value)
{
	GEN_REG(m_instr->reg).word[0] = _value;
}

void CPUExecutor::store_ed(uint32_t _value)
{
	if(m_instr->modrm.mod == 3) {
		GEN_REG(m_instr->modrm.rm).dword[0] = _value;
	} else {
		write_dword((this->*EA_get_segreg)(), (this->*EA_get_offset)(), _value);
	}
}

void CPUExecutor::store_ed_rmw(uint32_t _value)
{
	if(m_instr->modrm.mod == 3) {
		GEN_REG(m_instr->modrm.rm).dword[0] = _value;
	} else {
		write_dword(_value);
	}
}

void CPUExecutor::store_rd(uint32_t _value)
{
	GEN_REG(m_instr->modrm.r).dword[0] = _value;
}

void CPUExecutor::store_rd_op(uint32_t _value)
{
	GEN_REG(m_instr->reg).dword[0] = _value;
}

void CPUExecutor::store_sr(uint16_t _value)
{
	SET_SR(m_instr->modrm.r, _value);

	if(m_instr->modrm.r == REGI_SS) {
		/* Any move into SS will inhibit all interrupts until after the execution
		 * of the next instruction.
		 */
		g_cpu.inhibit_interrupts(CPU_INHIBIT_INTERRUPTS_BY_MOVSS);
	}
}
