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
#include "ring_buffer.h"
#include <cstring>

RingBuffer::RingBuffer()
:
m_size(0),
m_read_ptr(0),
m_write_ptr(0),
m_write_avail(0)
{
}

RingBuffer::~RingBuffer( )
{
}

void RingBuffer::set_size(size_t _size)
{
	m_data.resize(_size);
	m_size = _size;
	ASSERT(m_data.size() == m_size);
	clear();
}

void RingBuffer::clear()
{
	std::fill(m_data.begin(), m_data.end(), 0);
	m_read_ptr = 0;
	m_write_ptr = 0;
	m_write_avail = m_size;
}

size_t RingBuffer::read(uint8_t *_data, size_t _len)
{
	if(_data == nullptr || !_len || m_write_avail == m_size) {
		return 0;
	}

	size_t read_avail = m_size - m_write_avail;

	if(_len > read_avail) {
		_len = read_avail;
	}

	if(_len > m_size - m_read_ptr) {
		size_t len = m_size - m_read_ptr;
		memcpy(_data, &m_data[m_read_ptr], len*sizeof(uint8_t));
		memcpy(_data+len, &m_data[0], (_len-len)*sizeof(uint8_t));
	} else {
		memcpy(_data, &m_data[m_read_ptr], _len*sizeof(uint8_t));
	}

	m_read_ptr = (m_read_ptr + _len) % m_size;
	m_write_avail += _len;

	return _len;
}

size_t RingBuffer::write(uint8_t *_data, size_t _len)
{
	if(_data == nullptr || !_len || m_write_avail == 0) {
		return 0;
	}

	if(_len > m_write_avail) {
		_len = m_write_avail;
	}

	if(_len > m_size - m_write_ptr) {
		size_t len = m_size - m_write_ptr;
		//std::copy(&_data[0], &_data[len], m_data.begin()+m_write_ptr);
		memcpy(&m_data[m_write_ptr], _data, len);
		//std::copy(&_data[len], &_data[_len-len], m_data.begin());
		memcpy(&m_data[0], &_data[len], _len-len);
	} else {
		//std::copy(&_data[0], &_data[_len], m_data.begin()+m_write_ptr);
		memcpy(&m_data[m_write_ptr], _data, _len);
	}

	m_write_ptr = (m_write_ptr + _len) % m_size;
	m_write_avail -= _len;

	return _len;
}
