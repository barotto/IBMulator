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
#include "statebuf.h"
#include <cstring>

/*******************************************************************************
 * StateHeader
 */

struct header_raw_t
{
	size_t header_size;
	size_t data_size;
	size_t name_len;
};

bool StateHeader::check(uint8_t *_raw, size_t _raw_size) const
{
	if(_raw_size < sizeof(header_raw_t)) {
		return false;
	}

	header_raw_t *h = (header_raw_t *)_raw;
	if(h->header_size != sizeof(header_raw_t) + h->name_len) {
		return false;
	}
	if(_raw_size < h->header_size + h->data_size) {
		return false;
	}
	if(h->name_len) {
		return (name.compare((const char*)(_raw+sizeof(header_raw_t))) == 0);
	}

	return (name.length() == 0);
}

size_t StateHeader::get_size() const
{
	size_t namelen = name.length();
	if(namelen) {
		namelen++;
	}
	return sizeof(header_raw_t) + namelen;
}

size_t StateHeader::read(uint8_t *_source, size_t _source_size)
{
	data_size = 0;
	name = "";

	if(_source_size < sizeof(header_raw_t)) {
		PERRF(LOG_MACHINE, "StateHeader::read(): state buffer too small (%u < %u)\n",
				_source_size, sizeof(header_raw_t));
		throw std::exception();
	}

	header_raw_t *h = (header_raw_t *)_source;
	if(h->header_size != sizeof(header_raw_t) + h->name_len) {
		PERRF(LOG_MACHINE, "StateHeader::read(): header_size mismatch (%u != %u)\n",
				h->header_size, sizeof(header_raw_t) + h->name_len);
		throw std::exception();
	}

	data_size = h->data_size;

	if(_source_size < h->header_size + h->data_size) {
		PERRF(LOG_MACHINE, "StateHeader::read(): state buffer too small (%u < %u)\n",
				_source_size, h->header_size + h->data_size);
		throw std::exception();
	}

	if(h->name_len) {
		//find the null terminator
		_source += sizeof(header_raw_t);
		size_t l = h->name_len;
		uint8_t *s = _source;
		while(*s++!=0 && l--);
		if(s==0) {
			//terminator not found, we have a problem
			PERRF(LOG_MACHINE, "StateHeader::read(): name string not valid\n");
			throw std::exception();
		}
		name = (char*)_source;
	}

	return h->header_size;
}

size_t StateHeader::write(uint8_t *_dest, size_t _dest_size) const
{
	header_raw_t h;

	h.data_size = data_size;
	h.name_len = name.length();
	if(h.name_len) {
		h.name_len++;
	}
	h.header_size = sizeof(header_raw_t) + h.name_len;
	if(_dest_size < h.header_size) {
		throw std::exception();
	}

	memcpy(_dest, &h, sizeof(header_raw_t));
	_dest += sizeof(header_raw_t);

	if(h.name_len) {
		size_t written = snprintf((char*)_dest, h.name_len, "%s", name.c_str());
		if(written != h.name_len-1) {
			throw std::exception();
		}
	}

	return h.header_size;
}


/*******************************************************************************
 * StateBuf
 */

StateBuf::StateBuf(const std::string &_basename)
:
m_basename(_basename),
m_buf(nullptr),
m_size(0),
m_curptr(nullptr),
m_last_restore(false)
{

}

StateBuf::~StateBuf()
{
	free(m_buf);
}


void StateBuf::write(const void *_data, const StateHeader &_header)
{
	size_t lumpsize = _header.get_size() + _header.data_size;
	size_t curoff = m_curptr - m_buf;
	size_t bleft = get_bytesleft();
	if(bleft < lumpsize) {
		size_t newsize = m_size + lumpsize - (m_size-curoff);
		uint8_t *newbuf = (uint8_t*)realloc(m_buf, newsize);
		if(newbuf == nullptr) {
			throw std::exception();
		}
		m_buf = newbuf;
		m_size = newsize;
		m_curptr = m_buf + curoff;
		bleft = get_bytesleft();
	}
	m_curptr += _header.write(m_curptr, bleft);

	if(_header.data_size) {
		bleft = get_bytesleft();
		assert(bleft>=_header.data_size);
		memcpy(m_curptr, _data, _header.data_size);
		m_curptr += _header.data_size;
	}
}

void StateBuf::read(void *_data, const StateHeader &_header)
{
	check(_header);
	memcpy(_data, m_curptr, _header.data_size);
	m_curptr += _header.data_size;
}

void StateBuf::seek(size_t _pos)
{
	if(_pos >= m_size) {
		_pos = m_size-1;
	}
	m_curptr = m_buf + _pos;
}

void StateBuf::advance(size_t _off)
{
	uint8_t *lastpos = m_buf + (m_size-1);
	if(m_curptr + _off >= lastpos) {
		m_curptr = lastpos;
	} else {
		m_curptr += _off;
	}
}

void StateBuf::skip()
{
	StateHeader h;
	m_curptr += h.read(m_curptr, get_bytesleft());
	m_curptr += h.data_size;
}

void StateBuf::check(const StateHeader &_header)
{
	if(!_header.check(m_curptr, get_bytesleft())) {
		PERRF(LOG_MACHINE, "wrong state buffer header for '%s'\n", _header.name.c_str());
		throw std::exception();
	}
	m_curptr += _header.get_size();
}

void StateBuf::get_next_lump_header(StateHeader &_header)
{
	_header.read(m_curptr, get_bytesleft());
}

void StateBuf::load(const std::string &_path)
{
	std::ifstream binfile(_path.c_str(), std::ios::in|std::ios::binary|std::ios::ate);

	if(!binfile.is_open()) {
		PERRF(LOG_FS,"unable to open '%s' for reading\n", _path.c_str());
		throw std::exception();
	}

	std::streampos size = binfile.tellg();
	binfile.seekg(0, std::ios::beg);

	uint8_t* newbuf = (uint8_t*)malloc(size);
	if(newbuf == nullptr) {
		throw std::exception();
	}

	binfile.read((char*)newbuf, size);

	if(binfile.rdstate() & std::ifstream::failbit) {
		free(newbuf);
		PERRF(LOG_FS,"error reading the state image file\n");
		throw std::exception();
	}

	free(m_buf);
	m_buf = newbuf;
	m_size = size;
	m_curptr = m_buf;
	m_last_restore = true;
}

void StateBuf::save(const std::string &_path)
{
	std::ofstream binfile(_path.c_str(), std::ios::binary);

	if(!binfile.is_open()) {
		PERRF(LOG_FS,"unable to open '%s' for writing\n", _path.c_str());
		throw std::exception();
	}

	binfile.write((char*)m_buf, m_size);

	if(binfile.rdstate() & std::ofstream::failbit) {
		PERRF(LOG_FS,"error writing the state image file\n");
		throw std::exception();
	}
}
