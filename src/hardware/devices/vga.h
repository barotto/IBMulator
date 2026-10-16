/*
 * Copyright (c) 2001-2012  The Bochs Project
 * Copyright (C) 2015-2026  Marco Bortolin
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
#include "machine.h"

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
	VGA_M_SVGA8,
	VGA_M_TEXT
};

enum VGARenderMode {
	VGA_RENDER_LINE,
	VGA_RENDER_FRAME
};

struct VideoModeInfo
{
	VGAModes mode;
	uint16_t framew;   // screen frame width in dots (incl. overscan)
	uint16_t frameh;   // screen frame height in lines (incl. overscan)
	uint16_t xres;     // screen image x resolution (excl. overscan)
	uint16_t yres;     // screen image y resolution (excl. overscan)
	uint16_t imgw;     // video memory image width in dots
	uint16_t imgh;     // video memory image height in lines
	uint16_t nscans;   // scanlines per v.mem image line
	uint16_t ndots;    // dots per v.mem image dot
	uint16_t cwidth;   // character width in dots
	uint16_t cheight;  // character height in lines
	uint16_t textcols; // memory image width in characters (text modes only)
	uint16_t textrows; // memory image height in characters (text modes only)
	struct {
		uint16_t left, right; // width of left and right borders in dots
		uint16_t top, bottom; // height of top and bottom borders in lines
		bool present;         // are ovescan values valid?
	} overscan;

	inline bool operator==(const VideoModeInfo &_rhs) const {
		return (mode==_rhs.mode &&
				framew==_rhs.framew &&
				frameh==_rhs.frameh &&
				xres==_rhs.xres &&
				yres==_rhs.yres &&
				imgw==_rhs.imgw &&
				imgh==_rhs.imgh &&
				cwidth==_rhs.cwidth &&
				cheight==_rhs.cheight &&
				textcols==_rhs.textcols &&
				textrows==_rhs.textrows &&
				nscans==_rhs.nscans &&
				ndots==_rhs.ndots
				);
	}
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
	double   clock;          // pixel clock
	double   hfreq;          // h frequency (kHz)
	double   vfreq;          // v frequency (Hz)
	// derived values:
	struct {
		uint32_t htotal;         // Horizontal total (how long a line takes, including blank and retrace)
		uint32_t hdend;
		uint32_t hbstart, hbend; // Start / End of horizontal blanking
		uint32_t hrstart, hrend; // Start / End of horizontal retrace
		uint32_t vtotal;         // Vertical total (including blank and retrace)
		uint32_t vdend;          // Vertical display end
		uint32_t vbstart, vbend; // Start / End of vertical blanking period
		uint32_t vrstart, vrend; // Start / End of vertical retrace pulse
		uint32_t frame_end;      // Time distance from frame start of the end of the last visible scanline
	} ns;
	struct {
		int left, right, hretr; // in chars
		int top, bottom, vretr; // in lines
	} blank;
	struct {
		int left, right; // in chars
		int top, bottom; // in lines
	} overscan;
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
#define CURSOR_BLINK_TOGGLE  0x08
#define CURSOR_BLINK_STATE   0x10

// The blink rate is dependent on the vertical frame rate. The on/off state
// of the blinking areas changes every 16 vertical frames, which amounts to 1.875 blinks
// per second at 60 vertical frames per second (60/32).
#define VGA_DEFAULT_BLINK_RATE  32
#define CURSOR_DEFAULT_BLINK_RATE (VGA_DEFAULT_BLINK_RATE / 2)

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

class VGA;
typedef unsigned (VGA::*VGADrawFn)(unsigned _scanline, uint32_t _scanaddr, std::vector<uint8_t> &line_data_);

#include "vgadisplay.h"

#define VGA_THREAD_POOL_SIZE 2

class VGA : public IODevice
{
	IODEVICE(VGA, "VGA")
	IODEVICE_ID(VGA, 0x0000)

protected:
	// state information
	struct {
		VGA_GenRegs   gen_regs;
		VGA_Sequencer sequencer;
		VGA_CRTC      CRTC;
		VGA_GfxCtrl   gfx_ctrl;
		VGA_AttrCtrl  attr_ctrl;
		VGA_DAC       dac;

		bool needs_update;      // true=screen needs to be redrawn (per-frame rendering only)
		unsigned force_redraw;  // buffer needs to be redrawn for this number of frames (per-line rendering)
		unsigned force_redraw_overs; // overscan areas need to be redrawn for this number of frames
		int16_t scanline;       // scanline counter (per-line rendering)
		uint16_t mem_addr_counter;
		VGARenderMode render_mode;
		// blinking support (text cursor and monochrome gfx)
		bool blink_toggle;
		bool blink_visible;
		bool cursor_blink_toggle;
		bool cursor_blink_visible;
		// text mode support
		uint8_t text_snapshot[128 * 1024]; // current text snapshot
		uint16_t charmap_address[2];
		// timings
		uint64_t frame_start_time_nsec; // Time of the last frame start event
		VideoTimings timings;
		// current mode
		VideoModeInfo vmode;

		uint8_t dac_shift;

		uint32_t memory_offset;
		uint32_t memory_aperture;
		uint32_t memory_bank;
	} m_s;

	uint8_t *m_memory = nullptr;       // video memory buffer (TODO use std container)
	uint8_t *m_memory_planes[4] = {};  // memory planes pointers
	uint8_t *m_rom = nullptr;          // BIOS code buffer (TODO use std container)
	uint32_t m_memsize = 0x40000;      // size of memory buffer
	uint32_t m_planesize = 0x10000;    // size of the memory planes
	int m_mem_mapping = 0;             // video memory mapping ID
	int m_rom_mapping = 0;             // BIOS mapping ID
	VGATimings m_vga_timing = VGA_8BIT_SLOW;
	double m_bus_timing = 1.0;
	TimerID m_timer_id = NULL_TIMER_ID;
	VGADisplay *m_display = nullptr;
	VGADrawFn m_renderer = nullptr;
	std::vector<uint8_t> m_line_data_buf[VGA_THREAD_POOL_SIZE];
	// tiling system
	uint16_t m_num_x_tiles = 0;
	std::vector<uint8_t> m_tile_dirty; // don't use bool, it's not thread safe

	VideoStats m_stats = {};
	uint32_t m_cur_upd_pix = 0;
	
	// bugs on which some demos depend
	struct {
		bool ps_bit = false;
	} m_bugs;

	std::atomic<bool> m_force_redraw = false;

	std::atomic<bool> m_dbg_scanl_step = false;
	std::atomic<bool> m_dbg_frame_step = false;

	bool m_use_overscan = false;
	int m_blink_toggle_rate = VGA_DEFAULT_BLINK_RATE / 2;
	int m_cursor_blink_toggle_rate = CURSOR_DEFAULT_BLINK_RATE / 2;

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
	const char *current_video_mode_string();
	const char *current_rendering_mode_string();
	virtual void state_to_textfile(std::string _filepath);
	inline const VideoModeInfo & video_mode() const { return m_s.vmode; }
	inline const VideoTimings & timings() const { return m_s.timings; }

	const VGA_GenRegs & gen_regs() const { return m_s.gen_regs; }
	const VGA_Sequencer & sequencer() const { return m_s.sequencer; }
	const VGA_CRTC & crtc() const { return m_s.CRTC; }
	const VGA_GfxCtrl & gfx_ctrl() const { return m_s.gfx_ctrl; }
	const VGA_AttrCtrl & att_ctrl() const { return m_s.attr_ctrl; }
	const VGA_DAC & dac() const { return m_s.dac; }

	bool is_video_disabled();
	constexpr bool is_video_blanked();
	constexpr uint32_t get_blank_color();

	double current_scanline();
	double current_scanline(bool &disp_, bool &hretr_, bool &vretr_);
	
	const VideoStats & stats() const { return m_stats; }
	
	void print_text(std::vector<uint16_t> _text);
	
	void force_redraw() { m_force_redraw = true; }
	
	void dbg_scanl_step() { m_dbg_scanl_step = true; }
	void dbg_frame_step() { m_dbg_frame_step = true; }

protected:
	virtual uint32_t default_memsize_kb() const { return 256u; }
	virtual void update_mem_mapping();
	virtual double clock_freq_hz() const;
	void load_ROM(const std::string &_filename);
	void redraw_all();
	void init_iohandlers();
	void init_systemtimer();
	void update_video_mode();
	virtual void update_rendering_mode();
	
	void horizontal_retrace(uint64_t _time);
	void frame_start(uint64_t _time);
	void frame_end(uint64_t _time);
	void vertical_retrace(uint64_t _time);
	
	void reset_tiles();
	void calculate_timings();
	void raise_interrupt();
	void lower_interrupt();
	void clear_screen();
	
	void set_all_tiles(bool _value);
	void set_tile(unsigned _line_y, unsigned _tile_x, bool _value);
	void set_tiles(unsigned _line_y, bool _value);
	bool is_tile_dirty(unsigned _line_y, unsigned _tile_x) const;

	void text_update();
	unsigned gfx_update_thread(int _thread_id, uint16_t _line_compare);
	
	//unsigned draw_gfx_cga2(unsigned _scanline, uint16_t _scanaddr, std::vector<uint8_t> &line_data_);
	unsigned draw_gfx_cga(unsigned _scanline, uint32_t _scanaddr, std::vector<uint8_t> &line_data_);
	unsigned draw_gfx_ega(unsigned _scanline, uint32_t _scanaddr, std::vector<uint8_t> &line_data_);
	unsigned draw_gfx_vga256(unsigned _scanline, uint32_t _scanaddr, std::vector<uint8_t> &line_data_);
	unsigned draw_blank_line(unsigned _scanline);
	
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
	uint16_t value = (
		( s_mem_read<uint8_t>(_addr    , _priv) & 0xFF)       |
		((s_mem_read<uint8_t>(_addr + 1, _priv) & 0xFF) << 8)
	);
	PDEBUGF(LOG_V3, LOG_VGA, "mem r2 0x%04X -> 0x%04X\n", _addr, value);
	return value;
}

template<> inline
uint32_t VGA::s_mem_read<uint32_t>(uint32_t _addr, void *_priv)
{
	uint32_t value = (
		( s_mem_read<uint8_t>(_addr    , _priv) & 0xFF)        |
		((s_mem_read<uint8_t>(_addr + 1, _priv) & 0xFF) << 8)  |
		((s_mem_read<uint8_t>(_addr + 2, _priv) & 0xFF) << 16) |
		((s_mem_read<uint8_t>(_addr + 3, _priv) & 0xFF) << 24)
	);
	PDEBUGF(LOG_V3, LOG_VGA, "mem r4 0x%04X -> 0x%08X\n", _addr, value);
	return value;
}

template<> void VGA::s_mem_write<uint8_t> (uint32_t _addr, uint32_t _data, void *_priv);

template<> inline
void VGA::s_mem_write<uint16_t>(uint32_t _addr, uint32_t _data, void *_priv)
{
	PDEBUGF(LOG_V3, LOG_VGA, "mem w2 0x%04X <- 0x%04X\n", _addr, _data);
	s_mem_write<uint8_t>(_addr,      _data       & 0xFF, _priv);
	s_mem_write<uint8_t>(_addr + 1, (_data >> 8) & 0xFF, _priv);
}

template<> inline
void VGA::s_mem_write<uint32_t>(uint32_t _addr, uint32_t _data, void *_priv)
{
	PDEBUGF(LOG_V3, LOG_VGA, "mem w4 0x%04X <- 0x%08X\n", _addr, _data);
	s_mem_write<uint8_t>(_addr,      _data        & 0xFF, _priv);
	s_mem_write<uint8_t>(_addr + 1, (_data >> 8)  & 0xFF, _priv);
	s_mem_write<uint8_t>(_addr + 2, (_data >> 16) & 0xFF, _priv);
	s_mem_write<uint8_t>(_addr + 3, (_data >> 24) & 0xFF, _priv);
}

inline void VGA::set_all_tiles(bool _value)
{
	assert(m_s.vmode.yres > 0 && m_num_x_tiles > 0);
	assert(m_tile_dirty.size() >= uint32_t(m_s.vmode.yres*m_num_x_tiles));
	std::fill(&m_tile_dirty[0], &m_tile_dirty[uint32_t(m_s.vmode.yres*m_num_x_tiles)], _value);
}

inline void VGA::set_tile(unsigned _line_y, unsigned _tile_x, bool _value)
{
	assert(_line_y < m_s.vmode.yres && _tile_x < m_num_x_tiles);
	m_tile_dirty[_line_y*m_num_x_tiles + _tile_x] = _value;
}

inline void VGA::set_tiles(unsigned _line_y, bool _value)
{
	assert(_line_y < m_s.vmode.yres);
	std::fill(&m_tile_dirty[_line_y*m_num_x_tiles], 
	          &m_tile_dirty[_line_y*m_num_x_tiles + m_num_x_tiles],
	          _value);
}

inline bool VGA::is_tile_dirty(unsigned _line_y, unsigned _tile_x) const
{
	assert(_line_y < m_s.vmode.yres && _tile_x < m_num_x_tiles);
	return m_tile_dirty[_line_y*m_num_x_tiles + _tile_x] == VGA_TILE_DIRTY;
}

inline bool VGA::is_video_disabled()
{
	// disable sync pulses when vga/video is disabled or the sequencer is in reset mode
	return (!m_s.gen_regs.video_enable || !m_s.sequencer.reset.SR || !m_s.sequencer.reset.ASR);
}

constexpr bool VGA::is_video_blanked()
{
	return (!m_s.attr_ctrl.address.IPAS || m_s.sequencer.clocking.SO);
}

constexpr uint32_t VGA::get_blank_color()
{
	return (!m_s.sequencer.clocking.SO) ? m_display->overscan_color_rgb() : PALETTE_ENTRY(0,0,0);
}


#endif
