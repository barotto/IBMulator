/*
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

#ifndef IBMULATOR_HW_VGADISPLAY_H
#define IBMULATOR_HW_VGADISPLAY_H

#include "statebuf.h"
#include <vector>
#include <condition_variable>

#define VGA_MAX_XRES 800
#define VGA_MAX_YRES 600
#define VGA_MAX_HFREQ 0 //31.5 TODO add ini file setting

#define VGA_X_TILESIZE 16
#define VGA_Y_TILESIZE 24

#define PALETTE_RMASK 0x000000FF
#define PALETTE_GMASK 0x0000FF00
#define PALETTE_BMASK 0x00FF0000
#define PALETTE_AMASK 0xFF000000
#define PALETTE_ENTRY(r,g,b) (0xFF<<24 | b<<16 | g<<8 | r)


class VGADisplay
{
	std::vector<uint32_t> m_fb; // the framebuffer

	struct {
		VideoModeInfo mode;
		bool valid_mode;
		uint16_t fb_width;
		uint16_t fb_height;

		uint32_t palette[256];

		uint8_t charmap[2][0x2000];
		bool charmap_updated;
		bool charmap_select;

		unsigned prev_cursor_x, prev_cursor_y;
		uint8_t h_panning, v_panning;
		uint16_t line_compare;
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

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	inline void lock() { m_mutex.lock(); }
	inline void unlock() { m_mutex.unlock(); }
	inline void wait() {
		std::unique_lock<std::mutex> lock(m_mutex);
		while(m_fb_updated) {
			m_cv.wait(lock);
		}
	}
	inline void notify_all() { m_cv.notify_all(); }
	inline const VideoModeInfo & mode() const { return m_s.mode; }
	inline unsigned get_fb_size() const { return m_fb.size(); }
	inline unsigned get_fb_width() const { return m_s.fb_width; }
	inline unsigned get_fb_height() const { return m_s.fb_height; }
	inline const std::vector<uint32_t>& get_fb() const { return m_fb; }
	inline bool is_valid() const { return m_s.valid_mode; }

	void set_mode(const VideoModeInfo &_mode, double _hfreq, double _vfreq);
	void set_text_charmap(bool _map, uint8_t *_fbuffer);
	void set_text_charbyte(bool _map, uint16_t _address, uint8_t _data);
	void enable_AB_charmaps(bool _enable);
	void palette_change(uint8_t _index, uint8_t _red, uint8_t _green, uint8_t _blue);
	void graphics_update(unsigned _x, unsigned _y, unsigned _width, unsigned _height, uint8_t *_snapshot);
	void text_update(uint8_t *_old_text, uint8_t *_new_text,
			unsigned _cursor_x, unsigned _cursor_y, TextModeInfo *_tm_info);
	void clear_screen();

	void copy_screen(uint8_t *_buffer);
	uint32_t get_color(uint8_t _index);

	inline bool fb_updated() { return m_fb_updated; }
	inline void set_fb_updated() { m_fb_updated = true; }
	inline void clear_fb_updated() { m_fb_updated = false; }
	inline bool dimension_updated() { return m_dim_updated; }
	inline void clear_dimension_updated() { m_dim_updated = false; }
};

#endif
