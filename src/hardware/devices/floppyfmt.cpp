// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

// Based on MAME's formats/flopimg.cpp

#include "ibmulator.h"
#include "floppyfmt.h"
#include "floppydisk.h"
#include "utils.h"
#include "filesys.h"
#include "floppyfmt_img.h"
#include "floppyfmt_hfe.h"
#include <cstring>


FloppyFmt *FloppyFmt::find(std::string _image_path)
{
	// the path could point to a non existing file
	// don't depend on file access to determine the format
	std::string base, ext;
	FileSys::get_file_parts(_image_path.c_str(), base, ext);
	ext = str_to_lower(ext);
	std::unique_ptr<FloppyFmt> format;
	if(ext == ".img" || ext == ".ima") {
		format = std::make_unique<FloppyFmt_IMG>();
	} else if(ext == ".hfe") {
		format = std::make_unique<FloppyFmt_HFE>();
	} else {
		PDEBUGF(LOG_V2, LOG_FDC, "Fmt: unknown file type: '%s'\n", _image_path.c_str());
		return nullptr;
	}

	return format.release();
}

bool FloppyFmt::save(std::ofstream &, const FloppyDisk &)
{
	return false;
}

bool FloppyFmt::type_no_data(int type)
{
	return
		type == CRC_CCITT_START ||
		type == CRC_CCITT_FM_START ||
		type == CRC_END ||
		type == SECTOR_LOOP_START ||
		type == SECTOR_LOOP_END ||
		type == END;
}

bool FloppyFmt::type_data_mfm(int type, int p1, const gen_crc_info *crcs)
{
	return
		type == MFM ||
		type == MFMBITS ||
		type == TRACK_ID ||
		type == HEAD_ID ||
		type == HEAD_ID_SWAP ||
		type == SECTOR_ID ||
		type == SIZE_ID ||
		type == OFFSET_ID_O ||
		type == OFFSET_ID_E ||
		type == OFFSET_ID_FM ||
		type == SECTOR_ID_O ||
		type == SECTOR_ID_E ||
		type == REMAIN_O ||
		type == REMAIN_E ||
		type == SECTOR_DATA ||
		type == SECTOR_DATA_O ||
		type == SECTOR_DATA_E ||
		(type == CRC && (crcs[p1].type == CRC_CCITT));
}

void FloppyFmt::collect_crcs(const desc_e *desc, gen_crc_info *crcs)
{
	memset(crcs, 0, MAX_CRC_COUNT * sizeof(*crcs));
	for(int i=0; i != MAX_CRC_COUNT; i++) {
		crcs[i].write = -1;
	}

	for(int i=0; desc[i].type != END; i++) {
		switch(desc[i].type) {
		case CRC_CCITT_START:
			crcs[desc[i].p1].type = CRC_CCITT;
			break;
		case CRC_CCITT_FM_START:
			crcs[desc[i].p1].type = CRC_CCITT_FM;
			break;
		default:
			break;
		}
	}

	for(int i=0; desc[i].type != END; i++) {
		if(desc[i].type == CRC) {
			int j;
			for(j = i+1; desc[j].type != END && type_no_data(desc[j].type); j++) {};
			crcs[desc[i].p1].fixup_mfm_clock = type_data_mfm(desc[j].type, desc[j].p1, crcs);
		}
	}
}

int FloppyFmt::crc_cells_size(int type)
{
	switch(type) {
		case CRC_CCITT: return 32;
		case CRC_CCITT_FM: return 32;
		default: return 0;
	}
}

bool FloppyFmt::bit_r(const std::vector<uint32_t> &buffer, int offset)
{
	return (buffer[offset] & FloppyDisk::MG_MASK) == MG_1;
}

uint32_t FloppyFmt::bitn_r(const std::vector<uint32_t> &buffer, int offset, int count)
{
	uint32_t r = 0;
	for(int i=0; i<count; i++) {
		r = (r << 1) | (uint32_t) bit_r(buffer, offset+i);
	}
	return r;
}

void FloppyFmt::bit_w(std::vector<uint32_t> &buffer, bool val, uint32_t size, int offset)
{
	buffer[offset] = (val ? MG_1 : MG_0) | size;
}

void FloppyFmt::bit_w(std::vector<uint32_t> &buffer, bool val, uint32_t size)
{
	buffer.push_back((val ? MG_1 : MG_0) | size);
}

void FloppyFmt::raw_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size)
{
	for(int i=n-1; i>=0; i--) {
		bit_w(buffer, (val >> i) & 1, size);
	}
}

void FloppyFmt::raw_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size, int offset)
{
	for(int i=n-1; i>=0; i--) {
		bit_w(buffer, (val >> i) & 1, size, offset++);
	}
}

void FloppyFmt::mfm_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size)
{
	int prec = buffer.empty() ? 0 : bit_r(buffer, buffer.size()-1);
	for(int i=n-1; i>=0; i--) {
		int bit = (val >> i) & 1;
		bit_w(buffer, !(prec || bit), size);
		bit_w(buffer, bit, size);
		prec = bit;
	}
}

void FloppyFmt::mfm_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size, int offset)
{
	int prec = offset ? bit_r(buffer, offset-1) : 0;
	for(int i=n-1; i>=0; i--) {
		int bit = (val >> i) & 1;
		bit_w(buffer, !(prec || bit), size, offset++);
		bit_w(buffer, bit,            size, offset++);
		prec = bit;
	}
}

void FloppyFmt::fm_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size)
{
	for(int i=n-1; i>=0; i--) {
		int bit = (val >> i) & 1;
		bit_w(buffer, true, size);
		bit_w(buffer, bit,  size);
	}
}

void FloppyFmt::fm_w(std::vector<uint32_t> &buffer, int n, uint32_t val, uint32_t size, int offset)
{
	for(int i=n-1; i>=0; i--) {
		int bit = (val >> i) & 1;
		bit_w(buffer, true, size, offset++);
		bit_w(buffer, bit,  size, offset++);
	}
}

void FloppyFmt::mfm_half_w(std::vector<uint32_t> &buffer, int start_bit, uint32_t val, uint32_t size)
{
	int prec = buffer.empty() ? 0 : bit_r(buffer, buffer.size()-1);
	for(int i=start_bit; i>=0; i-=2) {
		int bit = (val >> i) & 1;
		bit_w(buffer, !(prec || bit), size);
		bit_w(buffer, bit,            size);
		prec = bit;
	}
}

uint16_t FloppyFmt::calc_crc_ccitt(const std::vector<uint32_t> &buffer, int start, int end)
{
	uint32_t res = 0xffff;
	int size = end - start;
	for(int i=1; i<size; i+=2) {
		res <<= 1;
		if(bit_r(buffer, start + i))
			res ^= 0x10000;
		if(res & 0x10000)
			res ^= 0x11021;
	}
	return res;
}

void FloppyFmt::fixup_crc_ccitt(std::vector<uint32_t> &buffer, const gen_crc_info *crc)
{
	mfm_w(buffer, 16, calc_crc_ccitt(buffer, crc->start, crc->end), 1000, crc->write);
}

void FloppyFmt::fixup_crc_ccitt_fm(std::vector<uint32_t> &buffer, const gen_crc_info *crc)
{
	fm_w(buffer, 16, calc_crc_ccitt(buffer, crc->start, crc->end), 1000, crc->write);
}

void FloppyFmt::fixup_crcs(std::vector<uint32_t> &buffer, gen_crc_info *crcs)
{
	for(int i=0; i != MAX_CRC_COUNT; i++)
		if(crcs[i].write != -1) {
			switch(crcs[i].type) {
			case CRC_CCITT:    fixup_crc_ccitt(buffer, crcs+i); break;
			case CRC_CCITT_FM: fixup_crc_ccitt_fm(buffer, crcs+i); break;
			default: assert(false); break;
			}
			if(crcs[i].fixup_mfm_clock) {
				int offset = crcs[i].write + crc_cells_size(crcs[i].type);
				bit_w(buffer, !((offset ? bit_r(buffer, offset-1) : false) || bit_r(buffer, offset+1)), 1000, offset);
			}
			crcs[i].write = -1;
		}
}

int FloppyFmt::calc_sector_index(int num, int interleave, int skew, int total_sectors, int track_head)
{
	int i = 0;
	int sec = 0;
	// use interleave
	while (i != num) {
		i++;
		i += interleave;
		i %= total_sectors;
		sec++;
		// This line prevents lock-ups of the emulator when the interleave is not appropriate
		if (sec > total_sectors) {
			throw std::runtime_error(str_format("interleave %d not appropriate for %d sectors per track",
					interleave, total_sectors).c_str());
		}
	}
	// use skew param
	sec -= track_head * skew;
	sec %= total_sectors;
	if (sec < 0) {
		sec += total_sectors;
	}
	return sec;
}

void FloppyFmt::generate_track(const desc_e *desc, int track, int head, const desc_s *sect,
		int sect_count, int track_size, FloppyDisk &image)
{
	std::vector<uint32_t> buffer;

	gen_crc_info crcs[MAX_CRC_COUNT];
	collect_crcs(desc, crcs);

	int index = 0;
	int sector_loop_start = 0;
	int sector_idx = 0;
	int sector_cnt = 0;
	int sector_limit = 0;
	int sector_interleave = 0;
	int sector_skew = 0;

	while(desc[index].type != END) {
		switch(desc[index].type) {
		case FM:
			for(int i=0; i<desc[index].p2; i++) {
				fm_w(buffer, 8, desc[index].p1);
			}
			break;

		case MFM:
			for(int i=0; i<desc[index].p2; i++) {
				mfm_w(buffer, 8, desc[index].p1);
			}
			break;

		case MFMBITS:
			mfm_w(buffer, desc[index].p2, desc[index].p1);
			break;

		case RAW:
			for(int i=0; i<desc[index].p2; i++)
				raw_w(buffer, 16, desc[index].p1);
			break;

		case RAWBYTE:
			for(int i=0; i<desc[index].p2; i++) {
				raw_w(buffer, 8, desc[index].p1);
			}
			break;

		case RAWBITS:
			raw_w(buffer, desc[index].p2, desc[index].p1);
			break;

		case TRACK_ID:
			mfm_w(buffer, 8, track);
			break;

		case TRACK_ID_FM:
			fm_w(buffer, 8, track);
			break;

		case HEAD_ID:
			mfm_w(buffer, 8, head);
			break;

		case HEAD_ID_FM:
			fm_w(buffer, 8, head);
			break;

		case HEAD_ID_SWAP:
			mfm_w(buffer, 8, !head);
			break;

		case SECTOR_ID:
			mfm_w(buffer, 8, sect[sector_idx].sector_id);
			break;

		case SECTOR_ID_FM:
			fm_w(buffer, 8, sect[sector_idx].sector_id);
			break;

		case SIZE_ID: {
			int size = sect[sector_idx].size;
			int id;
			for(id = 0; size > 128; size >>=1, id++) {};
			mfm_w(buffer, 8, id);
			break;
		}

		case SIZE_ID_FM: {
			int size = sect[sector_idx].size;
			int id;
			for(id = 0; size > 128; size >>=1, id++) {};
			fm_w(buffer, 8, id);
			break;
		}

		case OFFSET_ID_O:
			mfm_half_w(buffer, 7, track*2+head);
			break;

		case OFFSET_ID_E:
			mfm_half_w(buffer, 6, track*2+head);
			break;

		case OFFSET_ID_FM:
			fm_w(buffer, 8, track*2+head);
			break;

		case OFFSET_ID:
			mfm_w(buffer, 8, track*2+head);
			break;

		case SECTOR_ID_O:
			mfm_half_w(buffer, 7, sector_idx);
			break;

		case SECTOR_ID_E:
			mfm_half_w(buffer, 6, sector_idx);
			break;

		case REMAIN_O:
			mfm_half_w(buffer, 7, desc[index].p1 - sector_idx);
			break;

		case REMAIN_E:
			mfm_half_w(buffer, 6, desc[index].p1 - sector_idx);
			break;

		case SECTOR_LOOP_START:
			fixup_crcs(buffer, crcs);
			sector_loop_start = index;
			sector_idx = desc[index].p1;
			sector_cnt = sector_idx;
			sector_limit = desc[index].p2 == -1 ? sector_idx+sect_count-1 : desc[index].p2;
			sector_idx = calc_sector_index(sector_cnt,sector_interleave,sector_skew,sector_limit+1,track*2 + head);
			break;

		case SECTOR_LOOP_END:
			fixup_crcs(buffer, crcs);
			if(sector_cnt < sector_limit) {
				sector_cnt++;
				sector_idx = calc_sector_index(sector_cnt,sector_interleave,sector_skew,sector_limit+1,track*2 + head);
				index = sector_loop_start;
			}
			break;

		case SECTOR_INTERLEAVE_SKEW:
			sector_interleave = desc[index].p1;
			sector_skew = desc[index].p2;
			break;

		case CRC_CCITT_START:
		case CRC_CCITT_FM_START:
			crcs[desc[index].p1].start = buffer.size();
			break;

		case CRC_END:
			crcs[desc[index].p1].end = buffer.size();
			break;

		case CRC:
			crcs[desc[index].p1].write = buffer.size();
			buffer.resize(buffer.size() + crc_cells_size(crcs[desc[index].p1].type));
			break;

		case SECTOR_DATA: {
			const desc_s *csect = sect + (desc[index].p1 >= 0 ? desc[index].p1 : sector_idx);
			for(unsigned i=0; i != csect->size; i++) {
				mfm_w(buffer, 8, csect->data[i]);
			}
			break;
		}

		case SECTOR_DATA_FM: {
			const desc_s *csect = sect + (desc[index].p1 >= 0 ? desc[index].p1 : sector_idx);
			for(unsigned i=0; i != csect->size; i++) {
				fm_w(buffer, 8, csect->data[i]);
			}
			break;
		}

		case SECTOR_DATA_O: {
			const desc_s *csect = sect + (desc[index].p1 >= 0 ? desc[index].p1 : sector_idx);
			for(unsigned i=0; i != csect->size; i++) {
				mfm_half_w(buffer, 7, csect->data[i]);
			}
			break;
		}

		case SECTOR_DATA_E: {
			const desc_s *csect = sect + (desc[index].p1 >= 0 ? desc[index].p1 : sector_idx);
			for(unsigned i=0; i != csect->size; i++) {
				mfm_half_w(buffer, 6, csect->data[i]);
			}
			break;
		}

		default:
			PDEBUGF(LOG_V0, LOG_FDC, "%d.%d.%d (%d) unhandled\n", desc[index].type, desc[index].p1, desc[index].p2, index);
			break;
		}
		index++;
	}

	if(int(buffer.size()) != track_size) {
		throw std::runtime_error(str_format("Wrong track size in generate_track, expected %d, got %d",
				track_size, buffer.size()).c_str());
	}

	fixup_crcs(buffer, crcs);

	generate_track_from_levels(track, head, buffer, 0, image);
}

void FloppyFmt::normalize_times(std::vector<uint32_t> &buffer)
{
	unsigned int total_sum = 0;
	for(unsigned int i=0; i != buffer.size(); i++) {
		total_sum += buffer[i] & FloppyDisk::TIME_MASK;
	}

	unsigned int current_sum = 0;
	for(unsigned int i=0; i != buffer.size(); i++) {
		uint32_t time = buffer[i] & FloppyDisk::TIME_MASK;
		buffer[i] = (buffer[i] & FloppyDisk::MG_MASK) | (200000000ULL * current_sum / total_sum);
		current_sum += time;
	}
}

void FloppyFmt::generate_track_from_bitstream(int track, int head, const uint8_t *trackbuf, int track_size,
		FloppyDisk *image, int splice)
{
	std::vector<uint32_t> &dest = image->get_buffer(track, head);
	dest.clear();

	// If the bitstream has an odd number of inversions, one needs to be added.
	// Put in in the middle of the half window after the center inversion, where
	// any fdc ignores it.

	int inversions = 0;
	for(int i=0; i != track_size; i++) {
		if(trackbuf[i >> 3] & (0x80 >> (i & 7))) {
			inversions++;
		}
	}
	bool need_flux = inversions & 1;

	uint32_t cbit = FloppyDisk::MG_A;
	uint32_t count = 0;
	for(int i=0; i != track_size; i++)
		if(trackbuf[i >> 3] & (0x80 >> (i & 7))) {
			dest.push_back(cbit | (count+2));
			cbit = cbit == FloppyDisk::MG_A ? FloppyDisk::MG_B : FloppyDisk::MG_A;
			if(need_flux) {
				need_flux = false;
				dest.push_back(cbit | 1);
				cbit = cbit == FloppyDisk::MG_A ? FloppyDisk::MG_B : FloppyDisk::MG_A;
				count = 1;
			} else {
				count = 2;
			}
		} else {
			count += 4;
		}

	if(count) {
		dest.push_back(cbit | count);
	}

	normalize_times(dest);

	if(splice >= 0 || splice < track_size) {
		int splpos = uint64_t(200000000) * splice / track_size;
		image->set_write_splice_position(track, head, splpos);
	}
}

void FloppyFmt::generate_track_from_levels(int track, int head, std::vector<uint32_t> &trackbuf,
		int splice_pos, FloppyDisk &_disk)
{
	// Retrieve the angular splice pos before messing with the data
	splice_pos = splice_pos % trackbuf.size();
	uint32_t splice_angular_pos = trackbuf[splice_pos] & FloppyDisk::TIME_MASK;

	// Check if we need to invert a cell to get an even number of
	// transitions on the whole track
	//
	// Also check if all MG values are valid

	int transition_count = 0;
	for(auto & elem : trackbuf) {
		switch(elem & FloppyDisk::MG_MASK) {
		case MG_1:
			transition_count++;
			break;

		case MG_W:
			throw std::runtime_error(str_format("Weak bits not yet handled, track %d head %d", track, head).c_str());

		case MG_0:
		case FloppyDisk::MG_N:
		case FloppyDisk::MG_D:
			break;

		case FloppyDisk::MG_A:
		case FloppyDisk::MG_B:
		default:
			throw std::runtime_error(str_format("Incorrect MG information in generate_track_from_levels, track %d head %d",
					track, head).c_str());
		}
	}

	if(transition_count & 1) {
		int pos = splice_pos;
		while((trackbuf[pos] & FloppyDisk::MG_MASK) != MG_0 && (trackbuf[pos] & FloppyDisk::MG_MASK) != MG_1) {
			pos++;
			if(pos == int(trackbuf.size())) {
				pos = 0;
			}
			if(pos == splice_pos) {
				goto meh;
			}
		}
		if((trackbuf[pos] & FloppyDisk::MG_MASK) == MG_0) {
			trackbuf[pos] = (trackbuf[pos] & FloppyDisk::TIME_MASK) | MG_1;
		} else {
			trackbuf[pos] = (trackbuf[pos] & FloppyDisk::TIME_MASK) | MG_0;
		}

	meh: //wth???
		;

	}

	// Maximal number of cells which happens when the buffer is all MG_1/MG_N alternated, which would be 3/2
	std::vector<uint32_t> &dest = _disk.get_buffer(track, head);
	dest.clear();

	uint32_t cbit = FloppyDisk::MG_A;
	uint32_t count = 0;
	for(auto & elem : trackbuf) {
		uint32_t bit = elem & FloppyDisk::MG_MASK;
		uint32_t time = elem & FloppyDisk::TIME_MASK;
		if(bit == MG_0) {
			count += time;
			continue;
		}
		if(bit == MG_1) {
			count += time >> 1;
			dest.push_back(cbit | count);
			cbit = cbit == FloppyDisk::MG_A ? FloppyDisk::MG_B : FloppyDisk::MG_A;
			count = time - (time >> 1);
			continue;
		}
		dest.push_back(cbit | count);
		dest.push_back(elem);
		count = 0;
	}

	if(count)
		dest.push_back(cbit | count);

	normalize_times(dest);
	_disk.set_write_splice_position(track, head, splice_angular_pos);
}

std::vector<bool> FloppyFmt::generate_bitstream_from_track(int track, int head,
		int cell_size, const FloppyDisk &_disk)
{
	std::vector<bool> trackbuf;
	const std::vector<uint32_t> &tbuf = _disk.get_buffer(track, head);
	if(tbuf.size() <= 1) {
		// Unformatted track
		int track_size = 200'000'000/cell_size;
		trackbuf.resize(track_size, false);
		return trackbuf;
	}

	// Start at the write splice
	uint32_t splice = _disk.get_write_splice_position(track, head);
	unsigned cur_pos = splice;
	unsigned cur_entry = 0;
	while(cur_entry < tbuf.size()-1 && (tbuf[cur_entry+1] & FloppyDisk::TIME_MASK) < cur_pos) {
		cur_entry++;
	}

	int period = cell_size;
	int period_adjust_base = period * 0.05;

	int min_period = int(cell_size * 0.75);
	int max_period = int(cell_size * 1.25);
	int phase_adjust = 0;
	int freq_hist = 0;

	uint32_t scanned = 0;
	while(scanned < 200'000'000) {
		// Note that all magnetic cell type changes are considered
		// edges.  No randomness added for neutral/damaged cells
		int edge = tbuf[cur_entry] & FloppyDisk::TIME_MASK;
		if(edge < int(cur_pos)) {
			edge += 200'000'000;
		}
		int next = int(cur_pos) + period + phase_adjust;
		scanned += period + phase_adjust;

		if(edge >= next) {
			// No transition in the window means 0 and pll in free run mode
			trackbuf.push_back(false);
			phase_adjust = 0;

		} else {
			// Transition in the window means 1, and the pll is adjusted
			trackbuf.push_back(true);

			int delta = edge - (next - period/2);

			phase_adjust = 0.65 * delta;

			if(delta < 0) {
				if(freq_hist < 0) {
					freq_hist--;
				} else {
					freq_hist = -1;
				}
			} else if(delta > 0) {
				if(freq_hist > 0) {
					freq_hist++;
				} else {
					freq_hist = 1;
				}
			} else {
				freq_hist = 0;
			}

			if(freq_hist) {
				int afh = freq_hist < 0 ? -freq_hist : freq_hist;
				if(afh > 1) {
					int aper = period_adjust_base*delta/period;
					if(!aper) {
						aper = freq_hist < 0 ? -1 : 1;
					}
					period += aper;

					if(period < min_period) {
						period = min_period;
					} else if(period > max_period) {
						period = max_period;
					}
				}
			}
		}

		cur_pos = next;
		if(cur_pos >= 200'000'000) {
			cur_pos -= 200'000'000;
			cur_entry = 0;
		}
		while(cur_entry < tbuf.size()-1 && (tbuf[cur_entry] & FloppyDisk::TIME_MASK) < cur_pos) {
			cur_entry++;
		}

		// Wrap around
		if(cur_entry == tbuf.size()-1 && (tbuf[cur_entry] & FloppyDisk::TIME_MASK) < cur_pos) {
			// Wrap to index 0 or 1 depending on whether there is a transition exactly at the index hole
			cur_entry = (tbuf[tbuf.size()-1] & FloppyDisk::MG_MASK) != (tbuf[0] & FloppyDisk::MG_MASK) ?
				0 : 1;
		}
	}
	return trackbuf;
}

std::vector<uint8_t> FloppyFmt::generate_nibbles_from_bitstream(const std::vector<bool> &bitstream)
{
	std::vector<uint8_t> res;
	uint32_t pos = 0;
	while(pos < bitstream.size()) {
		while(pos < bitstream.size() && bitstream[pos] == 0)
			pos++;
		if(pos == bitstream.size()) {
			pos = 0;
			while(pos < bitstream.size() && bitstream[pos] == 0)
				pos++;
			if(pos == bitstream.size())
				return res;
			goto found;
		}
		pos += 8;
	}
	while(pos >= bitstream.size())
		pos -= bitstream.size();
	while(pos < bitstream.size() && bitstream[pos] == 0)
		pos++;

	found:
	for(;;) {
		uint8_t v = 0;
		for(uint32_t i=0; i != 8; i++) {
			if(bitstream[pos++])
				v |= 0x80 >> i;
			if(pos == bitstream.size())
				pos = 0;
		}
		res.push_back(v);
		if(pos < 8)
			return res;
		while(pos < bitstream.size() && bitstream[pos] == 0)
			pos++;
		if(pos == bitstream.size())
			return res;
	}
	return {};
}

int FloppyFmt::sbit_rp(const std::vector<bool> &bitstream, uint32_t &pos)
{
	int res = bitstream[pos];
	pos ++;
	if(pos == bitstream.size())
		pos = 0;
	return res;
}

uint8_t FloppyFmt::sbyte_mfm_r(const std::vector<bool> &bitstream, uint32_t &pos)
{
	uint8_t res = 0;
	for(int i=0; i<8; i++) {
		sbit_rp(bitstream, pos);
		if(sbit_rp(bitstream, pos))
			res |= 0x80 >> i;
	}
	return res;
}

std::vector<std::vector<uint8_t>> FloppyFmt::extract_sectors_from_bitstream_mfm_pc(
		const std::vector<bool> &bitstream)
{
	std::vector<std::vector<uint8_t>> sectors;

	// Don't bother if it's just too small
	if(bitstream.size() < 100)
		return sectors;

	// Start by detecting all id and data blocks

	// If 100 is not enough, that track is too funky to be worth
	// bothering anyway

	uint32_t idblk[100], dblk[100];
	uint32_t idblk_count = 0, dblk_count = 0;

	// Precharge the shift register to detect over-the-index stuff
	uint16_t shift_reg = 0;
	for(uint32_t i=0; i<16; i++)
		if(bitstream[bitstream.size()-16+i])
			shift_reg |= 0x8000 >> i;

	// Scan the bitstream for sync marks and follow them to check for
	// blocks
	for(uint32_t i=0; i<bitstream.size(); i++) {
		shift_reg = (shift_reg << 1) | bitstream[i];
		if(shift_reg == 0x4489) {
			uint16_t header;
			uint32_t pos = i+1;
			do {
				header = 0;
				for(int j=0; j<16; j++)
					if(sbit_rp(bitstream, pos))
						header |= 0x8000 >> j;
				// Accept strings of sync marks as long and they're not wrapping

				// Wrapping ones have already been take into account
				// thanks to the precharging
			} while(header == 0x4489 && pos > i);

			// fe, ff
			if(header == 0x5554 || header == 0x5555) {
				if(idblk_count < 100)
					idblk[idblk_count++] = pos;
				i = pos-1;
			}
			// f8, f9, fa, fb
			if(header == 0x554a || header == 0x5549 || header == 0x5544 || header == 0x5545) {
				if(dblk_count < 100)
					dblk[dblk_count++] = pos;
				i = pos-1;
			}
		}
	}

	// Then extract the sectors
	for(uint32_t i=0; i<idblk_count; i++) {
		uint32_t pos = idblk[i];
		[[maybe_unused]] uint8_t track = sbyte_mfm_r(bitstream, pos);
		[[maybe_unused]] uint8_t head = sbyte_mfm_r(bitstream, pos);
		uint8_t sector = sbyte_mfm_r(bitstream, pos);
		uint8_t size = sbyte_mfm_r(bitstream, pos);

		if(size >= 8) {
			continue;
		}
		int ssize = 128 << size;

		// Start of IDAM and DAM are supposed to be exactly 704 cells
		// apart in normal format or 1008 cells apart in perpendicular
		// format.  Of course the hardware is tolerant.  Accept +/-
		// 128 cells of shift.

		uint32_t d_index;
		for(d_index = 0; d_index < dblk_count; d_index++) {
			int delta = dblk[d_index] - idblk[i];
			if(delta >= 704-128 && delta <= 1008+128)
				break;
		}
		if(d_index == dblk_count) {
			continue;
		}

		pos = dblk[d_index];

		if(sectors.size() <= sector) {
			sectors.resize(sector+1);
		}
		auto &sdata = sectors[sector];
		sdata.resize(ssize);
		for(int j=0; j<ssize; j++) {
			sdata[j] = sbyte_mfm_r(bitstream, pos);
		}
	}

	return sectors;
}

void FloppyFmt::get_geometry_mfm_pc(const FloppyDisk &image, int cell_size,
		int &track_count, int &head_count, int &sector_count)
{
	image.get_actual_geometry(track_count, head_count);

	if(!track_count) {
		sector_count = 0;
		return;
	}

	// Extract an arbitrary track to get an idea of the number of
	// sectors

	// 20 was rarely used for protections, not near the start like
	// 0-10, not near the end like 70+, no special effects on sync
	// like 33

	auto buf = generate_bitstream_from_track(track_count > 20 ? 20 : 0, 0, cell_size, image);
	auto sectors = extract_sectors_from_bitstream_mfm_pc(buf);
	sector_count = sectors.size();
}

void FloppyFmt::get_track_data_mfm_pc(uint8_t track, uint8_t head, const FloppyDisk &image,
		unsigned cell_size, unsigned sector_size, unsigned sector_count, uint8_t *sectdata)
{
	auto bitstream = generate_bitstream_from_track(track, head, cell_size, image);
	auto sectors = extract_sectors_from_bitstream_mfm_pc(bitstream);
	for(unsigned sector=1; sector <= sector_count; sector++) {
		uint8_t *sd = sectdata + (sector-1)*sector_size;
		if(sector < sectors.size() && !sectors[sector].empty()) {
			unsigned int asize = sectors[sector].size();
			if(asize > sector_size)
				asize = sector_size;
			memcpy(sd, sectors[sector].data(), asize);
			if(asize < sector_size)
				memset(sd+asize, 0, sector_size-asize);
		} else
			memset(sd, 0, sector_size);
	}
}


std::vector<std::vector<uint8_t>> FloppyFmt::extract_sectors_from_bitstream_fm_pc(
		const std::vector<bool> &bitstream)
{
	std::vector<std::vector<uint8_t>> sectors;

	// Don't bother if it's just too small
	if(bitstream.size() < 100)
		return sectors;

	// Start by detecting all id and data blocks

	// If 100 is not enough, that track is too funky to be worth
	// bothering anyway

	uint32_t idblk[100], dblk[100];
	uint32_t idblk_count = 0, dblk_count = 0;

	// Precharge the shift register to detect over-the-index stuff
	uint16_t shift_reg = 0;
	for(int i=0; i<16; i++) {
		if(bitstream[bitstream.size()-16+i]) {
			shift_reg |= 0x8000 >> i;
		}
	}

	// Scan the bitstream for sync marks and follow them to check for
	// blocks
	// We scan for address marks only, as index marks are not mandatory,
	// and many formats actually do not use them

	for(uint32_t i=0; i<bitstream.size(); i++) {
		shift_reg = (shift_reg << 1) | bitstream[i];

		// fe
		if(shift_reg == 0xf57e) {       // address mark
			if(idblk_count < 100)
				idblk[idblk_count++] = i+1;
		}
		// f8, f9, fa, fb
		if(shift_reg == 0xf56a || shift_reg == 0xf56b ||
			shift_reg == 0xf56e || shift_reg == 0xf56f) {       // data mark
			if(dblk_count < 100) {
				dblk[dblk_count++] = i+1;
			}
		}
	}

	// Then extract the sectors
	for(uint32_t i=0; i<idblk_count; i++) {
		uint32_t pos = idblk[i];
		[[maybe_unused]] uint8_t track = sbyte_mfm_r(bitstream, pos);
		[[maybe_unused]] uint8_t head = sbyte_mfm_r(bitstream, pos);
		uint8_t sector = sbyte_mfm_r(bitstream, pos);
		uint8_t size = sbyte_mfm_r(bitstream, pos);
		if(size >= 8)
			continue;
		int ssize = 128 << size;

		// Start of IDAM and DAM are supposed to be exactly 384 cells
		// apart.  Of course the hardware is tolerant.  Accept +/- 128
		// cells of shift.

		unsigned d_index;
		for(d_index = 0; d_index < dblk_count; d_index++) {
			int delta = dblk[d_index] - idblk[i];
			if(delta >= 384-128 && delta <= 384+128) {
				break;
			}
		}
		if(d_index == dblk_count) {
			continue;
		}

		pos = dblk[d_index];

		if(sectors.size() <= sector)
			sectors.resize(sector+1);
		auto &sdata = sectors[sector];
		sdata.resize(ssize);
		for(int j=0; j<ssize; j++)
			sdata[j] = sbyte_mfm_r(bitstream, pos);
	}

	return sectors;
}

void FloppyFmt::get_geometry_fm_pc(const FloppyDisk &image, int cell_size,
		int &track_count, int &head_count, int &sector_count)
{
	image.get_actual_geometry(track_count, head_count);

	if(!track_count) {
		sector_count = 0;
		return;
	}

	// Extract an arbitrary track to get an idea of the number of
	// sectors

	// 20 was rarely used for protections, not near the start like
	// 0-10, not near the end like 70+, no special effects on sync
	// like 33

	auto bitstream = generate_bitstream_from_track(track_count > 20 ? 20 : 0, 0, cell_size, image);
	auto sectors = extract_sectors_from_bitstream_fm_pc(bitstream);
	sector_count = sectors.size();
}


void FloppyFmt::get_track_data_fm_pc(uint8_t track, uint8_t head, const FloppyDisk &image,
		unsigned cell_size, unsigned sector_size, unsigned sector_count, uint8_t *sectdata)
{
	auto bitstream = generate_bitstream_from_track(track, head, cell_size, image);
	auto sectors = extract_sectors_from_bitstream_fm_pc(bitstream);
	for(unsigned sector=1; sector < sector_count; sector++) {
		uint8_t *sd = sectdata + (sector-1)*sector_size;
		if(sector < sectors.size() && !sectors[sector].empty()) {
			unsigned int asize = sectors[sector].size();
			if(asize > sector_size)
				asize = sector_size;
			memcpy(sd, sectors[sector].data(), asize);
			if(asize < sector_size)
				memset(sd+asize, 0, sector_size-asize);
		} else
			memset(sd, 0, sector_size);
	}
}

int FloppyFmt::calc_default_pc_gap3_size(uint32_t disk_size, int sector_size)
{
	return
		disk_size == FloppyDisk::SIZE_8 ? 25 :
		sector_size < 512 ?
		(disk_size == FloppyDisk::SIZE_3_5 ? 54 : 50) :
		(disk_size == FloppyDisk::SIZE_3_5 ? 84 : 80);
}

void FloppyFmt::build_pc_track_fm(uint8_t track, uint8_t head, FloppyDisk &image,
		unsigned cell_count, unsigned sector_count, const desc_pc_sector *sects,
		int gap_3, int gap_4a, int gap_1, int gap_2)
{
	std::vector<uint32_t> track_data;

	// gap 4a, IAM and gap 1
	if(gap_4a != -1) {
		for(int i=0; i<gap_4a; i++) fm_w(track_data, 8, 0xff);
		for(int i=0; i< 6;     i++) fm_w(track_data, 8, 0x00);
		raw_w(track_data, 16, 0xf77a);
	}
	for(int i=0; i<gap_1; i++) fm_w(track_data, 8, 0xff);

	int total_size = 0;
	for(unsigned i=0; i<sector_count; i++)
		total_size += sects[i].actual_size;

	unsigned etpos = track_data.size() + (sector_count*(6+5+2+gap_2+6+1+2) + total_size)*16;

	if(etpos > cell_count) {
		throw std::runtime_error(str_format("Incorrect layout on track %d head %d, expected_size=%d, current_size=%d",
				track, head, cell_count, etpos).c_str());
	}
	if(etpos + gap_3*16*(sector_count-1) > cell_count) {
		gap_3 = (cell_count - etpos) / 16 / (sector_count-1);
	}
	// Build the track
	for(unsigned i=0; i<sector_count; i++) {
		uint16_t crc;
		// sync and IDAM and gap 2
		for(int j=0; j< 6; j++) fm_w(track_data, 8, 0x00);

		unsigned int cpos = track_data.size();
		raw_w(track_data, 16, 0xf57e);
		fm_w (track_data, 8, sects[i].track);
		fm_w (track_data, 8, sects[i].head);
		fm_w (track_data, 8, sects[i].sector);
		fm_w (track_data, 8, sects[i].size);
		crc = calc_crc_ccitt(track_data, cpos, track_data.size());
		fm_w (track_data, 16, crc);
		for(int j=0; j<gap_2; j++) fm_w(track_data, 8, 0xff);

		if(!sects[i].data)
			for(int j=0; j<6+1+sects[i].actual_size+2+(i != sector_count-1 ? gap_3 : 0); j++) fm_w(track_data, 8, 0xff);

		else {
			// sync, DAM, data and gap 3
			for(int j=0; j< 6; j++) fm_w(track_data, 8, 0x00);
			cpos = track_data.size();
			raw_w(track_data, 16, sects[i].deleted ? 0xf56a : 0xf56f);
			for(int j=0; j<sects[i].actual_size; j++) fm_w(track_data, 8, sects[i].data[j]);
			crc = calc_crc_ccitt(track_data, cpos, track_data.size());
			if(sects[i].bad_crc)
				crc = 0xffff^crc;
			fm_w(track_data, 16, crc);
			if(i != sector_count-1)
				for(int j=0; j<gap_3; j++) fm_w(track_data, 8, 0xff);
		}
	}

	// Gap 4b
	assert(cell_count >= 15);
	while(track_data.size() < cell_count-15) fm_w(track_data, 8, 0xff);
	raw_w(track_data, cell_count-int(track_data.size()), 0xffff >> (16+int(track_data.size())-cell_count));

	generate_track_from_levels(track, head, track_data, 0, image);
}

void FloppyFmt::build_pc_track_mfm(uint8_t track, uint8_t head, FloppyDisk &image,
		unsigned cell_count, unsigned sector_count, const desc_pc_sector *sects,
		int gap_3, int gap_4a, int gap_1, int gap_2)
{
	std::vector<uint32_t> track_data;

	// gap 4a, IAM and gap 1
	if(gap_4a != -1) {
		for(int i=0; i<gap_4a; i++) mfm_w(track_data, 8, 0x4e);
		for(int i=0; i<12;     i++) mfm_w(track_data, 8, 0x00);
		for(int i=0; i< 3;     i++) raw_w(track_data, 16, 0x5224);
		mfm_w(track_data, 8, 0xfc);
	}
	for(int i=0; i<gap_1; i++) mfm_w(track_data, 8, 0x4e);

	int total_size = 0;
	for(unsigned i=0; i<sector_count; i++)
		total_size += sects[i].actual_size;

	unsigned etpos = track_data.size() + (sector_count*(12+3+5+2+gap_2+12+3+1+2) + total_size)*16;

	if(etpos > cell_count)
		throw std::runtime_error(str_format("Incorrect layout on track %d head %d, expected_size=%d, current_size=%d",
				track, head, cell_count, etpos).c_str());

	if(etpos + gap_3*16*(sector_count-1) > cell_count)
		gap_3 = (cell_count - etpos) / 16 / (sector_count-1);

	// Build the track
	for(unsigned i=0; i<sector_count; i++) {
		uint16_t crc;
		// sync and IDAM and gap 2
		for(int j=0; j<12; j++) mfm_w(track_data, 8, 0x00);
		unsigned int cpos = track_data.size();
		for(int j=0; j< 3; j++) raw_w(track_data, 16, 0x4489);
		mfm_w(track_data, 8, 0xfe);
		mfm_w(track_data, 8, sects[i].track);
		mfm_w(track_data, 8, sects[i].head);
		mfm_w(track_data, 8, sects[i].sector);
		mfm_w(track_data, 8, sects[i].size);
		crc = calc_crc_ccitt(track_data, cpos, track_data.size());
		mfm_w(track_data, 16, crc);
		for(int j=0; j<gap_2; j++) mfm_w(track_data, 8, 0x4e);

		if(!sects[i].data)
			for(int j=0; j<12+4+sects[i].actual_size+2+(i != sector_count-1 ? gap_3 : 0); j++) mfm_w(track_data, 8, 0x4e);

		else {
			// sync, DAM, data and gap 3
			for(int j=0; j<12; j++) mfm_w(track_data, 8, 0x00);
			cpos = track_data.size();
			for(int j=0; j< 3; j++) raw_w(track_data, 16, 0x4489);
			mfm_w(track_data, 8, sects[i].deleted ? 0xf8 : 0xfb);
			for(int j=0; j<sects[i].actual_size; j++) mfm_w(track_data, 8, sects[i].data[j]);
			crc = calc_crc_ccitt(track_data, cpos, track_data.size());
			if(sects[i].bad_crc)
				crc = 0xffff^crc;
			mfm_w(track_data, 16, crc);
			if(i != sector_count-1)
				for(int j=0; j<gap_3; j++) mfm_w(track_data, 8, 0x4e);
		}
	}

	// Gap 4b
	assert(cell_count >= 15);
	while(track_data.size() < cell_count-15) mfm_w(track_data, 8, 0x4e);
	raw_w(track_data, cell_count-int(track_data.size()), 0x9254 >> (16+int(track_data.size())-cell_count));

	generate_track_from_levels(track, head, track_data, 0, image);
}
