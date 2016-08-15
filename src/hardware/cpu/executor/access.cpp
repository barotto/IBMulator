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

void CPUExecutor::seg_check_read(SegReg & _seg, uint32_t _offset, unsigned _len, uint8_t _vector, uint16_t _errcode)
{
	assert(_len!=0);
	if(_vector == CPU_INVALID_INT) {
		_vector = _seg.is(REG_SS)?CPU_SS_EXC:CPU_GP_EXC;
	}
	if(!_seg.desc.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_read(): segment not valid\n");
		throw CPUException(_vector, _errcode);
	}
	if(_seg.desc.is_expand_down()) {
		uint32_t upper_limit = 0xFFFF;
		if(_seg.desc.big) {
			upper_limit = 0xFFFFFFFF;
		}
		if(_offset <= _seg.desc.limit || _offset > upper_limit || (upper_limit - _offset) < _len) {
			PDEBUGF(LOG_V2, LOG_CPU, "seg_check_read(): segment limit violation exp.down\n");
			throw CPUException(_vector, _errcode);
		}
	} else if(_offset+_len-1 > _seg.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_read(): segment limit violation\n");
		throw CPUException(_vector, _errcode);
	}
	if(_seg.desc.is_code_segment() && !_seg.desc.is_readable()) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_read(): execute only\n");
		throw CPUException(_vector, _errcode);
	}
}

void CPUExecutor::seg_check_write(SegReg & _seg, uint32_t _offset, unsigned _len, uint8_t _vector, uint16_t _errcode)
{
	assert(_len!=0);
	if(_vector == CPU_INVALID_INT) {
		_vector = _seg.is(REG_SS)?CPU_SS_EXC:CPU_GP_EXC;
	}
	if(!_seg.desc.valid) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_write(): segment not valid\n");
		throw CPUException(_vector, _errcode);
	}
	if(!_seg.desc.is_writeable()) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_write(): segment not writeable\n");
		throw CPUException(_vector, _errcode);
	}
	if(_seg.desc.is_expand_down()) {
		uint32_t upper_limit = 0xFFFF;
		if(_seg.desc.big) {
			upper_limit = 0xFFFFFFFF;
		}
		if(_offset <= _seg.desc.limit || _offset > upper_limit || (upper_limit - _offset) < _len) {
			PDEBUGF(LOG_V2, LOG_CPU, "seg_check_write(): segment limit violation exp.down\n");
			throw CPUException(_vector, _errcode);
		}
	} else if(_offset+_len-1 > _seg.desc.limit) {
		PDEBUGF(LOG_V2, LOG_CPU, "seg_check_write(): segment limit violation\n");
		throw CPUException(_vector, _errcode);
	}
}

void CPUExecutor::mem_access_check(SegReg & _seg, uint32_t _offset, unsigned _len,
		bool _user, bool _write, uint8_t _vector, uint16_t _errcode)
{
	if(_write) {
		seg_check_write(_seg, _offset, _len, _vector, _errcode);
	} else {
		seg_check_read(_seg, _offset, _len, _vector, _errcode);
	}
	uint32_t linear = _seg.desc.base + _offset;
	if(IS_PAGING()) {
		if((PAGE_OFFSET(linear) + _len) <= 4096) {
			m_cached_phy.phy1 = TLB_lookup(linear, _len, _user, _write);
			m_cached_phy.len1 = _len;
			m_cached_phy.pages = 1;
		} else {
			uint32_t page_offset = PAGE_OFFSET(linear);
			m_cached_phy.len1 = 4096 - page_offset;
			m_cached_phy.len2 = _len - m_cached_phy.len1;
			m_cached_phy.phy1 = TLB_lookup(linear, m_cached_phy.len1, _user, _write);
			m_cached_phy.phy2 = TLB_lookup(linear + m_cached_phy.len1, m_cached_phy.len2, _user, _write);
			m_cached_phy.pages = 2;
		}
	} else {
		m_cached_phy.phy1 = linear;
		m_cached_phy.len1 = _len;
		m_cached_phy.pages = 1;
	}
}
