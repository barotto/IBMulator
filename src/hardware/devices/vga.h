/*
 * Copyright (c) 2001-2012  The Bochs Project
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

#ifndef IBMULATOR_HW_VGA_H
#define IBMULATOR_HW_VGA_H

#include "vgadisplay.h"
#include "hardware/iodevice.h"

/* Video timings - values taken from PCem

8-bit - 1mb/sec
	B = 8 ISA clocks
	W = 16 ISA clocks
	D = 32 ISA clocks

Slow 16-bit - 2mb/sec
	B = 6 ISA clocks
	W = 8 ISA clocks
	D = 16 ISA clocks

Fast 16-bit - 4mb/sec
	B = 3 ISA clocks
	W = 3 ISA clocks
	D = 6 ISA clocks

Slow 32-bit - ~8mb/sec
	B = 4 bus clocks
	W = 8 bus clocks
	D = 16 bus clocks

Mid 32-bit -
	B = 4 bus clocks
	W = 5 bus clocks
	D = 10 bus clocks

Fast 32-bit -
	B = 3 bus clocks
	W = 3 bus clocks
	D = 4 bus clocks
*/

enum VGATimings {
	VGA_8BIT,
	VGA_16BIT_SLOW,
	VGA_16BIT_FAST,
	VGA_32BIT_SLOW,
	VGA_32BIT_MID,
	VGA_32BIT_FAST
};

#define VGA_WORKERS 4

// text mode blink feature
#define TEXT_BLINK_MODE      0x01
#define TEXT_BLINK_TOGGLE    0x02
#define TEXT_BLINK_STATE     0x04

struct TextModeInfo
{
	uint16_t start_address;
	uint8_t  cs_start;
	uint8_t  cs_end;
	uint16_t line_offset;
	uint16_t line_compare;
	uint8_t  h_panning;
	uint8_t  v_panning;
	bool     line_graphics;
	bool     split_hpanning;
	uint8_t  blink_flags;
	uint8_t  actl_palette[16];
};


class VGA : public IODevice
{
	IODEVICE(VGA, "VGA")

protected:
	struct {
		struct {
			bool color_emulation;  // 1=color emulation, base address = 3Dx
								   // 0=mono emulation,  base address = 3Bx
			bool enable_ram;       // enable CPU access to video memory if set
			uint8_t clock_select;  // 0=25Mhz 1=28Mhz
			bool select_high_bank; // when in odd/even modes, select
								   // high 64k bank if set
			bool horiz_sync_pol;   // bit6: negative if set
			bool vert_sync_pol;    // bit7: negative if set
			                       //   bit7,bit6 represent number of lines on display:
			                       //   0 = reserved
			                       //   1 = 400 lines
			                       //   2 = 350 lines
			                       //   3 - 480 lines
		} misc_output;

		struct {
			uint8_t address;
			uint8_t reg[0x19];
			bool write_protect;
			bool interrupt;
			uint16_t start_address;
		} CRTC;

		struct {
			bool flip_flop; /* 0 = address, 1 = data-write */
			uint address;  /* register number */
			bool video_enabled;
			uint8_t palette_reg[16];
			uint8_t overscan_color;
			uint8_t color_plane_enable;
			uint8_t video_status_mux; // unused
			bool video_feedback; // used to fake the correct working of ST01 bits 4,5
			uint8_t horiz_pel_panning;
			uint8_t color_select;
			struct {
				bool graphics_alpha;
				bool display_type;
				bool enable_line_graphics;
				bool blink_intensity;
				bool pixel_panning_compat;
				bool pixel_clock_select;
				bool internal_palette_size;
			} mode_ctrl;
		} attribute_ctrl;

		struct {
			uint8_t write_data_register;
			uint8_t write_data_cycle; /* 0, 1, 2 */
			uint8_t read_data_register;
			uint8_t read_data_cycle; /* 0, 1, 2 */
			uint8_t dac_state;
			struct {
				uint8_t red;
				uint8_t green;
				uint8_t blue;
			} data[256];
			uint8_t mask;
			bool dac_sense; // ST0 bit 4 (DAC sensing), used to determine the presence of a color monitor
		} pel;

		struct {
			uint8_t index;
			uint8_t set_reset;
			uint8_t enable_set_reset;
			uint8_t color_compare;
			uint8_t data_rotate;
			uint8_t raster_op;
			uint8_t read_map_select;
			uint8_t write_mode;
			bool read_mode;
			bool odd_even;
			bool chain_odd_even;
			uint8_t shift_reg;
			bool graphics_alpha;
			uint8_t memory_mapping; /* 0 = use A0000-BFFFF
			                         * 1 = use A0000-AFFFF EGA/VGA graphics modes
			                         * 2 = use B0000-B7FFF Monochrome modes
			                         * 3 = use B8000-BFFFF CGA modes
			                         */
			uint32_t memory_offset;
			uint32_t memory_aperture;
			uint8_t color_dont_care;
			uint8_t bitmask;
			uint8_t latch[4];
		} graphics_ctrl;

		struct {
			uint8_t index;
			uint8_t map_mask;
			bool reset1;
			bool reset2;
			uint8_t reg1;
			uint8_t char_map_select;
			bool extended_mem;
			bool odd_even;
			bool chain_four;
			bool clear_screen;
		} sequencer;

		bool vga_enabled;
		bool vga_mem_updated;
		uint32_t vga_mem_offset;
		uint line_offset;
		uint line_compare;
		uint vertical_display_end;
		uint blink_counter;
		uint8_t text_snapshot[128 * 1024]; // current text snapshot
		uint16_t charmap_address;
		bool x_dotclockdiv2;
		bool y_doublescan;
		// h/v retrace timing
		uint64_t vblank_time_usec;
		uint64_t vretrace_time_usec;
		uint32_t htotal_usec;
		uint32_t hbstart_usec;
		uint32_t hbend_usec;
		uint32_t vtotal_usec;
		uint32_t vblank_usec;
		uint32_t vbspan_usec;
		uint32_t vrstart_usec;
		uint32_t vrend_usec;
		uint32_t vrspan_usec;
		// shift values for extensions
		uint8_t  plane_shift;
		uint32_t plane_offset;
		uint8_t  dac_shift;
		// last active resolution and bpp
		uint16_t last_xres;
		uint16_t last_yres;
		uint8_t last_bpp;
		uint8_t last_msl;
	} m_s;  // state information

	uint16_t m_max_xres;
	uint16_t m_max_yres;
	uint16_t m_num_x_tiles;
	uint16_t m_num_y_tiles;
	uint32_t m_memsize;
	uint8_t *m_memory;
	bool *m_tile_updated;
	int m_timer_id;
	VGADisplay * m_display;
	int m_mapping;
	VGATimings m_vga_timing;
	double m_bus_timing;

public:
	VGA(Devices *_dev);
	virtual ~VGA();

	void install();
	void config_changed();
	void remove();
	void reset(unsigned type);
	uint16_t read(uint16_t address, uint io_len);
	void write(uint16_t address, uint16_t value, uint io_len);
	void power_off();

	void set_timings(double _bus, VGATimings _vga) {
		m_bus_timing = _bus;
		m_vga_timing = _vga;
	}

	void get_text_snapshot(uint8_t **text_snapshot, uint *txHeight, uint *txWidth);
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

protected:
	virtual void update_mem_mapping();
	void redraw_area(uint x0, uint y0, uint width, uint height);
	void init_iohandlers();
	void init_systemtimer();
	uint8_t get_vga_pixel(uint16_t x, uint16_t y, uint16_t saddr, uint16_t lc, bool bs, uint8_t * const *plane);
	void update(uint64_t _time);
	void vertical_retrace(uint64_t _time);
	void determine_screen_dimensions(uint *piHeight, uint *piWidth);
	void calculate_retrace_timing();
	bool skip_update();
	void raise_interrupt();
	void lower_interrupt();
	void clear_screen();

	template<typename FN>
	void gfx_update(FN _get_pixel, bool _force_upd);
	template<typename FN>
	void gfx_update_core(FN _get_pixel, bool _force_upd, int id, int pool_size);
	template <typename FN>
	void update_mode13(FN _pixel_x, unsigned _pan);

	template<class T>
	static uint32_t s_mem_read(uint32_t _addr, void *_priv);
	template<class T>
	static void s_mem_write(uint32_t _addr, uint32_t _value, void *_priv);
};

template<> uint32_t VGA::s_mem_read<uint8_t> (uint32_t _addr, void *_priv);
template<> inline
uint32_t VGA::s_mem_read<uint16_t>(uint32_t _addr, void *_priv)
{
	return (s_mem_read<uint8_t>(_addr,   _priv) |
	        s_mem_read<uint8_t>(_addr+1, _priv) << 8
	);
}

template<> void VGA::s_mem_write<uint8_t> (uint32_t _addr, uint32_t _data, void *_priv);
template<> inline
void VGA::s_mem_write<uint16_t>(uint32_t _addr, uint32_t _data, void *_priv)
{
	s_mem_write<uint8_t>(_addr,   _data,    _priv);
	s_mem_write<uint8_t>(_addr+1, _data>>8, _priv);
}


#endif
