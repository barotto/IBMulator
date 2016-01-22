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

#ifndef IBMULATOR_VGM_H
#define IBMULATOR_VGM_H

#include <vector>

#define VGM_IDENT       0x206d6756 // "Vgm "
#define VGM_VERSION     0x00000170 // 1.70
#define VGM_DATA_OFFSET 0xCC;

struct VGMHeader {

	uint32_t ident;
	uint32_t EOF_offset;
	uint32_t version;
	uint32_t SN76489_clock;
	uint32_t YM2413_clock;
	uint32_t GD3_offset;
	uint32_t tot_num_samples;
	uint32_t loop_offset;
	uint32_t loop_num_samples;
	uint32_t rate;
	uint16_t SN76489_feedback;
	uint8_t  SN76489_shift_width;
	uint8_t  SN76489_flags;
	uint32_t YM2612_clock;
	uint32_t YM2151_clock;
	uint32_t data_offset;
	uint32_t Sega_PCM_clock;
	uint32_t SPCM_interface;
	uint32_t RF5C68_clock;
	uint32_t YM2203_clock;
	uint32_t YM2608_clock;
	uint32_t YM2610_B_clock;
	uint32_t YM3812_clock;
	uint32_t YM3526_clock;
	uint32_t Y8950_clock;
	uint32_t YMF262_clock;
	uint32_t YMF278B_clock;
	uint32_t YMF271_clock;
	uint32_t YMZ280B_clock;
	uint32_t RF5C164_clock;
	uint32_t PWM_clock;
	uint32_t AY8910_clock;
	uint8_t  AY8910_chip_type;
	uint8_t  AY8910_flags;
	uint8_t  YM2203_AY8910_flags;
	uint8_t  YM2608_AY8910_flags;
	uint8_t  volume_modifier;
	uint8_t  reserved1;
	uint8_t  loop_base;
	uint8_t  loop_modifier;
	uint32_t GB_DMG_clock;
	uint32_t NES_APU_clock;
	uint32_t MultiPCM_clock;
	uint32_t uPD7759_clock;
	uint32_t OKIM6258_clock;
	uint8_t  OKIM6258_flags;
	uint8_t  K054539_flags;
	uint8_t  C140_chip_type;
	uint8_t  reserved2;
	uint32_t OKIM6295_clock;
	uint32_t K051649_clock;
	uint32_t K054539_clock;
	uint32_t HuC6280_clock;
	uint32_t C140_clock;
	uint32_t K053260_clock;
	uint32_t Pokey_clock;
	uint32_t QSound_clock;
	uint8_t  reserved3[4];
	uint32_t extra_hdr_offset;
	uint8_t  reserved4[64];

} GCC_ATTRIBUTE(packed);


#define SIZEOF_VGMHEADER 256


class VGMFile
{
public:

	enum ChipType {
		SN76489 = 3
	};

private:
	struct VGMEvent {
		uint64_t time;
		uint8_t  cmd;
		uint32_t data;
	};
	std::vector<VGMEvent> m_events;
	VGMHeader m_header;
	ChipType m_chip;
	std::string m_filepath;

public:

	VGMFile();
	~VGMFile();

	void open(std::string _filepath);
	inline bool is_open() { return !m_filepath.empty(); };
	void command(uint64_t _time, uint8_t _command, uint32_t _data);
	void set_chip(ChipType _chip);
	void set_clock(uint32_t _value);
	void set_SN76489_feedback(uint16_t _value);
	void set_SN76489_shift_width(uint8_t _value);
	void set_SN76489_flags(uint8_t _value);
	void close();
};

#endif
