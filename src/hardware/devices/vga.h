/*
 * Copyright (c) 2001-2012  The Bochs Project
 * Copyright (C) 2015-2019  Marco Bortolin
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


#include "vga_genregs.h"
#include "vga_sequencer.h"
#include "vga_crtc.h"
#include "vga_gfxctrl.h"
#include "vga_attrctrl.h"
#include "vga_dac.h"
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

enum VGAModes {
	VGA_M_INVALID,
	VGA_M_CGA2,
	VGA_M_CGA4,
	VGA_M_EGA,
	VGA_M_256COL,
	VGA_M_TEXT
};

enum VGARenderMode {
	VGA_RENDER_LINE,
	VGA_RENDER_FRAME
};

struct VideoModeInfo
{
	VGAModes mode;
	uint16_t xres;
	uint16_t yres;
	uint16_t cwidth;
	uint16_t cheight;
	uint16_t imgw;
	uint16_t imgh;
	uint16_t textcols;
	uint16_t textrows;
	struct {
		uint8_t top, bottom, left, right;
	} borders;
};

struct VideoTimings
{
	uint16_t vtotal;         // total number of scanlines
	uint16_t vdend;          // number of visible scanlines
	uint16_t vbstart, vbend; // line of v blank start,end
	uint16_t vrstart, vrend; // line of v retrace start,end
	uint16_t vblank_skip;    // lines of top image blanking
	uint16_t htotal;         // total number of characters per line
	uint16_t hdend;          // number of visible characters
	uint16_t hbstart, hbend; // char of h blank start,end
	uint16_t hrstart, hrend; // char of h retrace start,end
	uint16_t cwidth;         // character width in pixels
	uint16_t last_vis_sl;    // the last visible scan line (0-based!)
	double   hfreq;          // h frequency (kHz)
	double   vfreq;          // v frequency (Hz)
};

// Values not used for emulation, only for debugging purposes.
struct VideoStats
{
	uint32_t frame_cnt;       // frame counter
	uint32_t updated_pix;     // numer of pixels that have been updated on the framebuffer in the last frame
	uint16_t last_saddr_line; // scanline at which the last start address update happened
	uint16_t last_pal_line;   // scanline at which the last palette update happened
};

// text mode blink feature
#define TEXT_BLINK_MODE      0x01
#define TEXT_BLINK_TOGGLE    0x02
#define TEXT_BLINK_STATE     0x04

// The blink rate is dependent on the vertical frame rate. The on/off state
// of the cursor changes every 16 vertical frames, which amounts to 1.875 blinks
// per second at 60 vertical frames per second (60/32).
#define VGA_BLINK_COUNTER  16

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
	bool     double_dot;
	bool     double_scanning;
	uint8_t  blink_flags;
	uint8_t  actl_palette[16];
};

#include "vgadisplay.h"

class VGA : public IODevice
{
	IODEVICE(VGA, "VGA")

protected:
	// state information
	struct {
		VGA_GenRegs   gen_regs;
		VGA_Sequencer sequencer;
		VGA_CRTC      CRTC;
		VGA_GfxCtrl   gfx_ctrl;
		VGA_AttrCtrl  attr_ctrl;
		VGA_DAC       dac;

		bool needs_update;  // 1=screen needs to be updated
		int16_t scanline;   // scanline counter (per-line rendering)
		VGARenderMode render_mode;
		// blinking support (text cursor and monochrome gfx)
		unsigned blink_counter;
		bool blink_toggle;
		bool blink_visible;
		// text mode support
		uint8_t text_snapshot[128 * 1024]; // current text snapshot
		uint16_t charmap_address[2];
		// timings
		uint64_t frame_start_time_nsec; // Time of the last frame start event
		VideoTimings timings;
		struct {
			uint32_t htotal;         // Horizontal total (how long a line takes, including blank and retrace)
			uint32_t hdend;
			uint32_t hbstart, hbend; // Start and End of horizontal blanking
			uint32_t hrstart, hrend; // Start and End of horizontal retrace
			uint32_t vtotal;         // Vertical total (including blank and retrace)
			uint32_t vdend;          // Vertical display end
			uint32_t vrstart, vrend; // Start and End of vertical retrace pulse
			uint32_t frame_end;      // Time of end of the last visible scan line
		} timings_ns;
		// current mode
		VideoModeInfo vmode;
		// shift values for VBE (TODO)
		uint8_t  plane_shift;
		uint32_t plane_offset;
		uint8_t  dac_shift;
	} m_s;

	uint8_t  *m_memory;      // video memory buffer
	uint8_t  *m_rom;         // BIOS code buffer
	uint32_t m_memsize;      // size of memory buffer
	int m_mem_mapping;       // video memory mapping ID
	int m_rom_mapping;       // BIOS mapping ID
	VGATimings m_vga_timing; // VGA memory timings
	double m_bus_timing;     // System bus timings
	int m_timer_id;          // Machine timer ID
	VGADisplay *m_display;   // VGADisplay object
	// tiling system
	uint16_t m_num_x_tiles;
	std::vector<uint8_t> m_tile_dirty; // don't use bool, it's not thread safe

	VideoStats m_stats; // Stats update needs to be enabled with VGA_STATS_ENABLED
	uint32_t m_cur_upd_pix;
	
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
	void set_bus_timings(double _bus, VGATimings _vga) {
		m_bus_timing = _bus;
		m_vga_timing = _vga;
	}
	const char *current_mode_string();
	virtual void state_to_textfile(std::string _filepath);
	inline const VideoModeInfo & video_mode() const { return m_s.vmode; }
	inline const VideoTimings & timings() const { return m_s.timings; }
	
	inline const VGA_CRTC & crtc() const { return m_s.CRTC; }
	
	double current_scanline();
	double current_scanline(bool &disp_, bool &hretr_, bool &vretr_);
	
	const VideoStats & stats() const { return m_stats; }
	
protected:
	virtual void update_mem_mapping();
	void load_ROM(const std::string &_filename);
	void redraw_all();
	void init_iohandlers();
	void init_systemtimer();
	uint8_t get_vga_pixel(uint16_t _x, uint16_t _y, uint16_t _lc, uint8_t * const *_plane);
	void update_video_mode(uint64_t _time);
	void gfx_update(uint16_t _sl0, uint16_t _sl1);
	void text_update(uint16_t _l0, uint16_t _l1);
	void horiz_disp_end(uint64_t _time);
	void frame_start(uint64_t _time);
	void frame_end(uint64_t _time);
	void vertical_retrace(uint64_t _time);
	void tiles_update();
	void calculate_timings();
	bool is_video_disabled();
	void raise_interrupt();
	void lower_interrupt();
	void clear_screen();
	
	void set_all_tiles(bool _value);
	void set_tile(unsigned _scanline, unsigned _xtile, bool _value);
	void set_tiles(unsigned _scanline, bool _value);
	void set_tiles(unsigned _scanline, unsigned _lines, bool _value);
	void set_tiles(unsigned _scanline, unsigned _lines, unsigned _xtile, bool _value);
	bool is_tile_dirty(unsigned _scanline, unsigned _xtile) const;

	template<typename FN>
	void gfx_scanline_update(uint16_t _sl0, uint16_t _sl1, FN _get_pixel, bool _force_upd);
	template<typename FN>
	uint32_t gfx_scanline_update_thread(uint16_t _sl0, uint16_t _sl1, FN _get_pixel, bool _force_upd, 
		int _thread_id, int _thread_pool_size);
	template <typename FN>
	void gfx_scanline_update_mode13(uint16_t _sl0, uint16_t _sl1, FN _pixel_x, unsigned _pan);

	template<class T>
	static uint32_t s_mem_read(uint32_t _addr, void *_priv);
	template<class T>
	static void s_mem_write(uint32_t _addr, uint32_t _value, void *_priv);

	void mem_write_chain4(uint32_t _offset, uint8_t _value);
	void mem_write_planar(uint32_t _offset, uint8_t _value);
	
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

inline void VGA::set_all_tiles(bool _value)
{
	std::fill(m_tile_dirty.begin(), m_tile_dirty.end(), _value);
}

inline void VGA::set_tile(unsigned _scanline, unsigned _xtile, bool _value)
{
	assert(_scanline < m_s.vmode.yres && _xtile < m_num_x_tiles);
	m_tile_dirty[_scanline*m_num_x_tiles + _xtile] = _value;
}

inline void VGA::set_tiles(unsigned _scanline, bool _value)
{
	assert(_scanline < m_s.vmode.yres);
	std::fill(&m_tile_dirty[_scanline*m_num_x_tiles], 
	          &m_tile_dirty[_scanline*m_num_x_tiles + m_num_x_tiles],
	          _value);
}

inline void VGA::set_tiles(unsigned _scanline, unsigned _lines, bool _value)
{
	assert(_scanline+_lines <= m_s.vmode.yres);
	std::fill(&m_tile_dirty[_scanline*m_num_x_tiles], 
	          &m_tile_dirty[(_scanline+_lines)*m_num_x_tiles],
	          _value);
}

inline void VGA::set_tiles(unsigned _scanline, unsigned _lines, unsigned _xtile, bool _value)
{
	assert(_lines > 0);
	while(_lines-- && _scanline+_lines<m_s.vmode.yres) {
		set_tile(_scanline+_lines, _xtile, _value);
	};
}

inline bool VGA::is_tile_dirty(unsigned _scanline, unsigned _xtile) const
{
	assert(_scanline < m_s.vmode.yres && _xtile < m_num_x_tiles);
	return m_tile_dirty[_scanline*m_num_x_tiles + _xtile] == VGA_TILE_DIRTY;
}

inline bool VGA::is_video_disabled()
{
	// skip screen update when vga/video is disabled or the sequencer is in reset mode
	return (!m_s.gen_regs.video_enable || !m_s.attr_ctrl.address.IPAS
	        || !m_s.sequencer.reset.SR || !m_s.sequencer.reset.ASR
	        || m_s.sequencer.clocking.SO);
}

#endif
