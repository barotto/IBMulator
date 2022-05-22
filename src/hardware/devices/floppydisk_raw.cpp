/*
 * Copyright (C) 2022  Marco Bortolin
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
#include "floppydisk_raw.h"

bool FloppyDisk_Raw::track_is_formatted(int track, int head)
{
	if(int(track_array.size()) <= track) {
		return false;
	}
	if(int(track_array[track].size()) <= head) {
		return false;
	}
	const auto &data = track_array[track][head].cell_data;
	if(data.size() < m_props.spt) {
		return false;
	}
	return true;
}

void FloppyDisk_Raw::read_sector(uint8_t _c, uint8_t _h, uint8_t _s, uint8_t *buffer, uint32_t bytes)
{
	if(_c >= m_props.tracks || _h >= m_props.sides || _s > m_props.spt) {
		return;
	}
	if(bytes != m_props.secsize) {
		return;
	}

	const auto &track = track_array[_c][_h].cell_data;
	assert(track.size() == m_props.secsize * m_props.spt);

	for(unsigned b=0; b<m_props.secsize; b++) {
		buffer[b] = track[(_s-1)*m_props.secsize + b];
	}
}

void FloppyDisk_Raw::write_sector(uint8_t _c, uint8_t _h, uint8_t _s, const uint8_t *buffer, uint32_t bytes)
{
	if(_c >= m_props.tracks || _h >= m_props.sides || _s > m_props.spt) {
		return;
	}
	if(bytes != m_props.secsize) {
		return;
	}

	auto &track = track_array[_c][_h].cell_data;
	assert(track.size() == m_props.secsize * m_props.spt);
	
	for(unsigned b=0; b<m_props.secsize; b++) {
		track[(_s-1)*m_props.secsize + b] = buffer[b];
	}
}
