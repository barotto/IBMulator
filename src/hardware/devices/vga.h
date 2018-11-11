/*
 * Copyright (c) 2001-2012  The Bochs Project
 * Copyright (C) 2015-2018  Marco Bortolin
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

#include "vgacrtc.h"
#include "vgadisplay.h"
#include "hardware/iodevice.h"

enum VGATimings {
	VGA_8BIT_SLOW,
	VGA_8BIT_FAST,
	VGA_16BIT_SLOW,
	VGA_16BIT_MID,
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
	// state information
	struct {

		// General registers
		struct {
			// Miscellaneous Output Register
			struct {
				bool io_addr_select;   // bit0: I/O Address Select
				bool enable_ram;       // bit1: Enable RAM
				uint8_t clock_select;  // bit2-3: Clock Select
				bool select_high_bank; // bit5: select high 64k bank
				uint8_t sync_polarity; // bit6-7: Sync Polarity
			} misc_output;
			bool vga_enable; // Video Subsystem Enable Register (bit0)
		} general;

		// CRT Controller
		VGA_CRTC CRTC;

		// Attribute Controller
		struct {
			uint8_t address;            // Address Register
			bool flip_flop;             // 0=address mode, 1=data-write mode
			bool palette_enable;        // Address Register bit5: Internal Palette Address Source
			uint8_t palette_reg[16];    // Internal Palette Registers
			uint8_t overscan_color;     // Overscan Color Register
			uint8_t color_plane_enable; // Color Plane Enable Register (bit0-3)
			uint8_t horiz_pel_panning;  // Horizontal Pixel Panning Register (bit0-3)
			uint8_t color_select;       // Color Select Register (bit0-3)
			// Mode Control Register
			struct {
				bool graphics_alpha;        // bit0: Graphics/Alphanumeric Mode
				bool display_type;          // bit1: Mono Emulation
				bool enable_line_graphics;  // bit2: Enable Line Graphics Character Code
				bool blink_intensity;       // bit3: Enable Blink/Select Background Intensity
				bool pel_panning_mode;      // bit5: PEL Panning Compatibility
				bool pel_clock_select;      // bit6: PEL Width
				bool internal_palette_size; // bit7: P5, P4 Select
			} mode_ctrl;
		} attribute_ctrl;

		// Digital-to-Analog Converter
		struct {
			uint8_t write_data_register; // Palette Address Register (write mode)
			uint8_t write_data_cycle;    // 0,1,2: current write data cycle
			uint8_t read_data_register;  // Palette Address Register (read mode)
			uint8_t read_data_cycle;     // 0,1,2: current read data cycle
			uint8_t state;               // DAC State Register
			struct {
				uint8_t red;             // Palette entry red value (6-bit)
				uint8_t green;           // Palette entry green value (6-bit)
				uint8_t blue;            // Palette entry blue value (6-bit)
			} palette[256];              // Palette Data registers
			uint8_t pel_mask;            // PEL Mask Register
		} dac;

		// Graphics Controller
		struct {
			uint8_t address;          // Address register
			uint8_t set_reset;        // Set/Reset register
			uint8_t enable_set_reset; // Enable Set/Reset register
			uint8_t color_compare;    // Color Compare register
			uint8_t data_rotate;      // Data Rotate register
			uint8_t raster_op;        // Data Rotate register bit3-4: Function Select
			uint8_t read_map_select;  // Read Map Select register
			uint8_t write_mode;       // Graphics Mode Register bit0-1: Write Mode
			bool read_mode;           // Graphics Mode Register bit3: Read Mode
			bool odd_even;            // Graphics Mode Register bit4: Odd/Even
			uint8_t shift_c256;       // Graphics Mode Register bit5-6:
			                          //   Shift Register Mode (5)
			                          //   Graphics 256 Colour Control (6)
			bool graphics_mode;       // Miscellaneous Register bit0: Graphics Mode
			bool chain_odd_even;      // Miscellaneous Register bit1: Odd/Even
			uint8_t memory_mapping;   // Miscellaneous Register bit2-3: Memory Map
			uint8_t color_dont_care;  // Color Don't Care Register
			uint8_t bitmask;          // Bit Mask Register
			uint32_t memory_offset;   // current phy start address of video memory
			uint32_t memory_aperture; // current video memory accessible size
			uint8_t latch[4];         // data latches
		} graphics_ctrl;

		// Sequencer
		struct {
			uint8_t address;         // Address register
			uint8_t clocking;        // Clocking Mode register
			uint8_t map_mask;        // Map Mask register
			uint8_t char_map_select; // Character Map Select register
			bool reset_asr;          // Reset register bit0: Asynchronous reset
			bool reset_sr;           // Reset register bit1: Synchronous reset
			bool extended_mem;       // Memory Mode register bit1: Extended Memory
			bool odd_even;           // Memory Mode register bit2: Odd/Even
			bool chain_four;         // Memory Mode register bit3: Chain 4
			bool x_dotclockdiv2;     // Clocking Mode register bit3: Dot Clock
		} sequencer;

		bool needs_update;  // 1=screen needs to be updated
		bool clear_screen;  // 1=screen must be cleared at next update
		// text mode support
		unsigned blink_counter;
		uint8_t text_snapshot[128 * 1024]; // current text snapshot
		uint16_t charmap_address;
		// h/v retrace timing
		uint64_t vblank_time_usec;   // Time of the last vblank event
		uint64_t vretrace_time_usec; // Time of the last vretrace event
		uint32_t htotal_usec;
		uint32_t hbstart_usec;
		uint32_t hbend_usec;
		uint32_t vtotal_usec;
		uint32_t vblank_usec;
		uint32_t vbspan_usec;
		uint32_t vrstart_usec;
		uint32_t vrend_usec;
		uint32_t vrspan_usec;
		// shift values for VBE (TODO)
		uint8_t  plane_shift;
		uint32_t plane_offset;
		uint8_t  dac_shift;
		// last active resolution
		uint16_t last_xres;
		uint16_t last_yres;
		uint8_t last_msl;
	} m_s;

	uint8_t  *m_memory;      // video memory buffer
	uint8_t  *m_rom;         // BIOS code buffer
	uint32_t m_memsize;      // size of memory buffer
	int m_mem_mapping;       // video memory mapping ID
	int m_rom_mapping;       // BIOS mapping ID
	VGATimings m_vga_timing; // VGA timings
	double m_bus_timing;     // System bus timings
	int m_timer_id;          // Machine timer ID, for vblank and vretrace events
	VGADisplay *m_display;   // VGADisplay object
	// tiling system
	uint16_t m_tile_width;
	uint16_t m_tile_height;
	uint16_t m_num_x_tiles;
	uint16_t m_num_y_tiles;
	std::vector<uint8_t> m_tile_dirty; // don't use bool, it's not thread safe

public:
	VGA(Devices *_dev);
	virtual ~VGA();

	void install();
	void config_changed();
	void remove();
	void reset(unsigned _type);
	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);
	void power_off();
	void set_timings(double _bus, VGATimings _vga) {
		m_bus_timing = _bus;
		m_vga_timing = _vga;
	}
	void get_text_snapshot(uint8_t **text_snapshot_, unsigned *txHeight_, unsigned *txWidth_);

protected:
	virtual void update_mem_mapping();
	void load_ROM(const std::string &_filename);
	void redraw_area(unsigned _x0, unsigned _y0, unsigned _width, unsigned _height);
	void init_iohandlers();
	void init_systemtimer();
	uint8_t get_vga_pixel(uint16_t _x, uint16_t _y, uint16_t _saddr, uint16_t _lc, bool _bs, uint8_t * const *_plane);
	void update(uint64_t _time);
	void vertical_retrace(uint64_t _time);
	void determine_screen_dimensions(unsigned *height_, unsigned *width_);
	void tiles_update(unsigned _width, unsigned _height);
	void calculate_retrace_timing();
	bool skip_update();
	void raise_interrupt();
	void lower_interrupt();
	void clear_screen();
	void set_tiles_dirty();
	ALWAYS_INLINE void set_tile_dirty(unsigned _xtile, unsigned _ytile, bool _value) {
		if(((_xtile) < m_num_x_tiles) && ((_ytile) < m_num_y_tiles)) {
			m_tile_dirty[(_xtile) + (_ytile)*m_num_x_tiles] = _value;
		}
	}
	ALWAYS_INLINE bool is_tile_dirty(unsigned _xtile, unsigned _ytile) const {
		return ((((_xtile) < m_num_x_tiles) && ((_ytile) < m_num_y_tiles)) ?
			m_tile_dirty[(_xtile) + (_ytile)*m_num_x_tiles]
			: false);
	}

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

	template<class T>
	static uint32_t s_rom_read(uint32_t _addr, void *_priv) {
		return *(T*)&(((VGA*)_priv)->m_rom)[_addr&0xFFFF];
	}
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
