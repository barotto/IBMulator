/*
 * Copyright (C) 2015-2020  Marco Bortolin
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
#include <chrono>
#include <SDL.h>

#define VGA_MAX_XRES 800
#define VGA_MAX_YRES 600
#define VGA_MAX_HFREQ 0 //31.5 TODO add ini file setting

#define VGA_X_TILESIZE 16 // should be divisible by 2
#define VGA_TILE_DIRTY true
#define VGA_TILE_CLEAN false

#define PALETTE_RMASK 0x000000FF
#define PALETTE_GMASK 0x0000FF00
#define PALETTE_BMASK 0x00FF0000
#define PALETTE_AMASK 0xFF000000
#define PALETTE_ENTRY(r,g,b) (0xFF<<24 | b<<16 | g<<8 | r)

class FrameBuffer
{
	std::vector<uint32_t> m_buffer;
	uint16_t m_width;
	uint16_t m_height;
	unsigned m_bypp;
	
public:
	FrameBuffer();
	~FrameBuffer();
	
	inline uint16_t width() const { return m_width; }
	inline uint16_t height() const { return m_height; }
	inline uint16_t pitch() const { return m_width * m_bypp; }
	inline size_t size() const { return m_buffer.size(); }
	inline size_t size_bytes() const { return m_buffer.size() * m_bypp; }
	
	void clear();
	void copy_screen_to(uint8_t *_dest, const VideoModeInfo &_mode) const;
	
	inline uint32_t & operator[](size_t _pos) { return m_buffer[_pos]; }
};

class VGADisplay
{
	FrameBuffer m_fb; // the current framebuffer, constantly updating
	
	struct {
		VideoModeInfo mode;
		VideoTimings timings;
		bool valid_mode;

		uint32_t palette[256];

		uint8_t charmap[2][0x2000];
		bool charmap_updated;
		bool charmap_select;

		unsigned prev_cursor_x, prev_cursor_y;
		uint8_t h_panning, v_panning;
		uint16_t line_compare;
	} m_s;

	std::atomic<bool> m_dim_updated;
	std::atomic<bool> m_fb_updated;
	std::mutex m_mutex;
	std::condition_variable m_cv;

	// internal double buffering
	bool m_buffering; 
	FrameBuffer m_last_fb; // the last complete framebuffer content
	VideoModeInfo m_last_mode; // the last videomode, relative to the last framebuffer
	VideoTimings m_last_timings; 
	
	static uint8_t ms_font8x16[256][16];
	static uint8_t ms_font8x8[256][8];

public:

	VGADisplay();
	~VGADisplay();

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	inline void lock() { m_mutex.lock(); }
	inline void unlock() { m_mutex.unlock(); }
	inline std::cv_status wait_for_device(unsigned _max_wait_ns) {
		std::unique_lock<std::mutex> lock(m_mutex);
		return m_cv.wait_for(lock, std::chrono::nanoseconds(_max_wait_ns));
	}
	void notify_interface();
	inline const VideoModeInfo & mode() const { return m_s.mode; }
	inline const VideoModeInfo & last_mode() const { return m_last_mode; }
	inline const VideoTimings & last_timings() const { return m_last_timings; }
	inline const FrameBuffer & framebuffer() const { return m_fb; }
	inline const FrameBuffer & last_framebuffer() const { return m_last_fb; }

	void set_mode(const VideoModeInfo &_mode);
	void set_timings(const VideoTimings &_timings);
	void set_text_charmap(bool _map, uint8_t *_fbuffer);
	void set_text_charbyte(bool _map, uint16_t _address, uint8_t _data);
	void enable_AB_charmaps(bool _enable);
	void palette_change(uint8_t _index, uint8_t _red, uint8_t _green, uint8_t _blue);
	void gfx_screen_line_update(unsigned _scanline, std::vector<uint8_t> &_linedata,
			uint8_t *_tiles, uint16_t _tiles_count);
	void gfx_screen_line_update(unsigned _scanline, std::vector<uint8_t> &_linedata);
	void text_update(uint8_t *_old_text, uint8_t *_new_text,
			unsigned _cursor_x, unsigned _cursor_y, TextModeInfo *_tm_info);
	void clear_screen();

	void copy_screen(uint8_t *_buffer);
	uint32_t get_color(uint8_t _index);

	void enable_buffering(bool _enable) { m_buffering = _enable; }
	
	inline bool fb_updated() { return m_fb_updated; }
	inline void set_fb_updated() { m_fb_updated = true; }
	inline void clear_fb_updated() { m_fb_updated = false; }
	inline bool dimension_updated() { return m_dim_updated; }
	inline void set_dimension_updated() { m_dim_updated = true; }
	inline void clear_dimension_updated() { m_dim_updated = false; }
	
	// screen recording (TODO temporary)
	void toggle_recording();
	void start_recording();
	void stop_recording();
private:
	SDL_Surface *m_rec_surface;
	std::string m_rec_dir;
	int m_rec_framecnt;
	void record_frame();
};

#endif
