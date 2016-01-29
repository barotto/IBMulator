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

#ifndef IBMULATOR_RINGBUFFER_H
#define IBMULATOR_RINGBUFFER_H

#include <vector>

class RingBuffer
{
private:
	std::vector<uint8_t> m_data;
	size_t m_size;
	size_t m_read_ptr;
	size_t m_write_ptr;
	size_t m_write_avail;
	mutable std::mutex m_mutex;

	RingBuffer& operator=(const RingBuffer&) = delete;
	RingBuffer(const RingBuffer&) = delete;
	RingBuffer(const RingBuffer&&) = delete;

public:

	RingBuffer();
	~RingBuffer();

	void set_size(size_t _size);
	void clear();

	size_t read(uint8_t *_data, size_t _len);
	size_t write(uint8_t *_data, size_t _len);

	const uint8_t *get_data() const;
	void get_status(size_t &_size, size_t &_wr_avail, size_t &_rd_avail) const;
	size_t get_read_avail() const;

private:
	void p_clear();
};

#endif
