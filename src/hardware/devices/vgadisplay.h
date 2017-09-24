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

#ifndef IBMULATOR_HW_VGADISPLAY_H
#define IBMULATOR_HW_VGADISPLAY_H

#include "statebuf.h"
#include <condition_variable>

#define VGA_MAX_XRES 720
#define VGA_MAX_YRES 480

#define VGA_X_TILESIZE 16
#define VGA_Y_TILESIZE 24

#define PALETTE_RMASK 0x000000FF
#define PALETTE_GMASK 0x0000FF00
#define PALETTE_BMASK 0x00FF0000
#define PALETTE_AMASK 0xFF000000
#define PALETTE_ENTRY(r,g,b) (0xFF<<24 | b<<16 | g<<8 | r)

class TextModeInfo;

class VGADisplay
{
	uint32_t *m_fb; // the framebuffer

	struct {
		bool textmode;
		uint xres;
		uint yres;
		uint bpp;
		uint fb_xsize;
		uint fb_ysize;
		uint tile_xsize;
		uint tile_ysize;

		uint32_t palette[256];

		unsigned char charmap[0x2000];
		bool charmap_updated;
		bool char_changed[256];

		uint prev_cursor_x, prev_cursor_y;
		uint8_t h_panning, v_panning;
		uint16_t line_compare;
		int fontwidth, fontheight;
		uint text_rows, text_cols;
	} m_s;

	bool m_dim_updated;
	std::atomic<bool> m_fb_updated;
	std::mutex m_mutex;
	std::condition_variable m_cv;

	static uint8_t ms_font8x16[256][16];
	static uint8_t ms_font8x8[256][8];

public:

	VGADisplay();
	~VGADisplay();

	inline void lock() { m_mutex.lock(); }
	inline void unlock() { m_mutex.unlock(); }
	inline void wait() {
		std::unique_lock<std::mutex> lock(m_mutex);
		while(m_fb_updated) {
			m_cv.wait(lock);
		}
	}
	inline void notify_all() { m_cv.notify_all(); }
	inline uint get_screen_xres() { return m_s.xres; }
	inline uint get_screen_yres() { return m_s.yres; }
	inline uint get_fb_xsize() { return m_s.fb_xsize; }
	inline uint get_fb_ysize() { return m_s.fb_ysize; }
	inline uint32_t* get_framebuffer() { return m_fb; }
	inline uint32_t get_framebuffer_data_size() { return VGA_MAX_XRES*VGA_MAX_YRES*4; }
	inline uint32_t get_framebuffer_row_length() { return VGA_MAX_XRES*4; }

	void set_text_charmap(uint8_t *_fbuffer);
	void set_text_charbyte(uint16_t _address, uint8_t _data);
	bool palette_change(uint8_t index, uint8_t red, uint8_t green, uint8_t blue);
	void dimension_update(uint x, uint y, uint fheight=0, uint fwidth=0, uint bpp=8);
	void graphics_tile_update(uint8_t *snapshot, uint x, uint y);
	void text_update(uint8_t *_old_text, uint8_t *_new_text,
			uint _cursor_x, uint _cursor_y, TextModeInfo *_tm_info);
	void clear_screen();

	void copy_screen(uint8_t *_buffer);
	uint32_t get_color(uint8_t index);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	inline bool fb_updated() { return m_fb_updated; }
	inline void set_fb_updated() { m_fb_updated = true; }
	inline void clear_fb_updated() { m_fb_updated = false; }
	inline bool dimension_updated() { return m_dim_updated; }
	inline void clear_dimension_updated() { m_dim_updated = false; }

};

#endif
