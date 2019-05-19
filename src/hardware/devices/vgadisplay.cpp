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
/*
 * Portions of code Copyright (c) 2014 The Bochs Project
 */

#include "ibmulator.h"
#include "vga.h"
#include "vgafont.h"
#include <cstring>


VGADisplay::VGADisplay()
{
	m_s.mode.mode = VGA_M_TEXT;
	m_s.mode.xres = 640;
	m_s.mode.yres = 400;
	m_s.mode.imgw = 640;
	m_s.mode.imgh = 400;
	m_s.mode.textcols = 80;
	m_s.mode.textrows = 25;
	m_s.mode.cwidth = 8;
	m_s.mode.cheight = 16;

	m_s.valid_mode = true;

	m_s.fb_width = VGA_MAX_XRES;
	m_s.fb_height = VGA_MAX_YRES;

	m_s.prev_cursor_x = 0;
	m_s.prev_cursor_y = 0;

	m_s.h_panning = 0;
	m_s.v_panning = 0;

	m_s.line_compare = 1023;

	m_fb.resize(m_s.fb_width * m_s.fb_height);

	m_s.palette[0]  = PALETTE_ENTRY(  0,   0,   0); // black
	m_s.palette[1]  = PALETTE_ENTRY(  0,   0, 170); // blue
	m_s.palette[2]  = PALETTE_ENTRY(  0, 170,   0); // green
	m_s.palette[3]  = PALETTE_ENTRY(  0, 170, 170); // cyan
	m_s.palette[4]  = PALETTE_ENTRY(170,   0,   0); // red
	m_s.palette[5]  = PALETTE_ENTRY(170,   0, 170); // magenta
	m_s.palette[6]  = PALETTE_ENTRY(170,  85,   0); // brown
	m_s.palette[7]  = PALETTE_ENTRY(170, 170, 170); // light gray
	m_s.palette[8]  = PALETTE_ENTRY( 85,  85,  85); // dark gray
	m_s.palette[9]  = PALETTE_ENTRY( 85,  85, 255); // light blue
	m_s.palette[10] = PALETTE_ENTRY( 85, 255,  85); // light green
	m_s.palette[11] = PALETTE_ENTRY( 85, 255, 255); // light cyan
	m_s.palette[12] = PALETTE_ENTRY(255,  85,  85); // light red
	m_s.palette[13] = PALETTE_ENTRY(255,  85, 255); // light magenta
	m_s.palette[14] = PALETTE_ENTRY(255, 255,  85); // yellow
	m_s.palette[15] = PALETTE_ENTRY(255, 255, 255); // white

	for(int i=0; i<256; i++) {
		for(int j=0; j<16; j++) {
			m_s.charmap[0][i*32+j] = ms_font8x16[i][j];
			m_s.charmap[1][i*32+j] = ms_font8x16[i][j];
		}
	}
	m_s.charmap_updated = true;
	m_s.charmap_select = false;

	m_dim_updated = false;

	clear_screen();
}

VGADisplay::~VGADisplay()
{
}

void VGADisplay::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "saving display state\n");

	std::lock_guard<std::mutex> lock(m_mutex);

	StateHeader h;

	//state
	_state.write(&m_s, {sizeof(m_s), "VGADisplay"});

	//framebuffer
	_state.write(&m_fb[0], {m_fb.size()*4, "VGADisplay fb"});
}

void VGADisplay::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "restoring display state\n");

	std::lock_guard<std::mutex> lock(m_mutex);

	StateHeader h;

	//state
	_state.read(&m_s, {sizeof(m_s), "VGADisplay"});

	//framebuffer
	m_fb.resize(m_s.fb_width*m_s.fb_height);
	_state.read(&m_fb[0], {m_fb.size()*4, "VGADisplay fb"});

	m_dim_updated = true;
}

// clear_screen()
//
// Called to request that the VGA region is cleared.
void VGADisplay::clear_screen()
{
	std::fill(m_fb.begin(), m_fb.end(), 0);
}

void VGADisplay::set_text_charmap(bool _map, uint8_t *_fbuffer)
{
	memcpy(&m_s.charmap[_map], _fbuffer, 0x2000);
	m_s.charmap_updated = true;
}

void VGADisplay::set_text_charbyte(bool _map, uint16_t _address, uint8_t _data)
{
	m_s.charmap[_map][_address] = _data;
	m_s.charmap_updated = true;
}

void VGADisplay::enable_AB_charmaps(bool _enable)
{
	m_s.charmap_select = _enable;
}

// palette_change()
//
// Allocate a color in the GUI, for this color, and put it in the colormap
// location 'index'.
void VGADisplay::palette_change(uint8_t _index, uint8_t _red, uint8_t _green, uint8_t _blue)
{
	m_s.palette[_index] = PALETTE_ENTRY(_red, _green, _blue);
}

// set_mode()
//
// Called when the VGA mode changes.
//
void VGADisplay::set_mode(const VideoModeInfo &_mode, double _hfreq, double _vfreq)
{
	m_s.mode = _mode;
	m_s.valid_mode = true;

	PINFOF(LOG_V1, LOG_VGA, "screen: %ux%u %.2fkHz %.2fHz\n",
		_mode.xres, _mode.yres, _hfreq, _vfreq);

	if(_mode.xres > m_s.fb_width) {
		PWARNF(LOG_VGA, "requested x res (%d) is greater than the maximum (%d)\n", _mode.xres, m_s.fb_width);
		m_s.valid_mode = false;
		m_s.mode.xres = m_s.fb_width;
	}
	if(_mode.yres > m_s.fb_height) {
		PWARNF(LOG_VGA, "requested y res (%d) is greater than the maximum (%d)\n", _mode.yres, m_s.fb_height);
		m_s.valid_mode = false;
		m_s.mode.yres = m_s.fb_height;
	}

	if(VGA_MAX_HFREQ > 0.0 && (_hfreq>VGA_MAX_HFREQ+0.5 || _hfreq<VGA_MAX_HFREQ-0.5)) {
		PWARNF(LOG_VGA, "frequency (%.2fkHz) out of range\n", _hfreq);
		m_s.valid_mode = false;
	}
	if(!m_s.valid_mode) {
		clear_screen();
	}

	m_dim_updated = true;
}

// gfx_scanline_update()
//
// Called in VGA graphics mode to request that a line be drawn to the screen,
// since some info in this line has changed.
//
// _scanline: the line of the framebuffer to be updated.
// _scandata: array of 8bit palette indices to use to update the framebuffer line.
// _tiles: array of horizontal tile statuses for the given line; each tile is VGA_X_TILESIZE px wide.
//         tiles status will be updated with VGA_TILE_CLEAN
// _tiles_count: elements count of _tiles.
void VGADisplay::gfx_scanline_update(
		unsigned _scanline,
		const uint8_t *_scandata,
		uint8_t *_tiles,
		uint16_t _tiles_count)
{
	if(!m_s.valid_mode || _scanline >= m_s.mode.yres) {
		return;
	}

	uint32_t *fb_line = &m_fb[0] + _scanline * m_s.fb_width;
	
	for(uint16_t tid=0; tid<_tiles_count; tid++, _tiles++) {
		if(*_tiles == VGA_TILE_CLEAN) {
			continue;
		}
		unsigned pixel_x = tid * VGA_X_TILESIZE;
		for(int tx=0; tx<VGA_X_TILESIZE; tx++,pixel_x++) {
			if(pixel_x > m_s.mode.xres) {
				// the last tile could be wider than needed
				break;
			}
			fb_line[pixel_x] = m_s.palette[_scandata[pixel_x]];
		};
		*_tiles = VGA_TILE_CLEAN;
	}
}

// gfx_scanline_update()
//
// Called in VGA graphics mode to request that a line be drawn to the screen,
// since the entire line has changed.
//
// _scanline: the line of the framebuffer to be updated.
// _scandata: array of 8bit palette indices to use to update the framebuffer line.
void VGADisplay::gfx_scanline_update(unsigned _scanline, const uint8_t *_scandata)
{
	if(!m_s.valid_mode || _scanline >= m_s.mode.yres) {
		return;
	}

	uint32_t *fb_line = &m_fb[0] + _scanline * m_s.fb_width;
	
	for(unsigned pixel_x=0; pixel_x<m_s.mode.xres; pixel_x++) {
		fb_line[pixel_x] = m_s.palette[_scandata[pixel_x]];
	}
}

// text_update()
//
// Called in a VGA text mode, to update the screen with new content.
//
// _old_text: array of character/attributes making up the contents
//            of the screen from the last call.  See below
// _new_text: array of character/attributes making up the current
//            contents, which should now be displayed.  See below
//
// format of old_text & new_text: each is _tm_info->line_offset*text_rows
//     bytes long. Each character consists of 2 bytes.  The first byte is
//     the character value, the second is the attribute byte.
//
// _cursor_x: new x location of cursor
// _cursor_y: new y location of cursor
// _tm_info:  this structure contains information for additional
//           features in text mode (cursor shape, line offset,...)
void VGADisplay::text_update(uint8_t *_old_text, uint8_t *_new_text,
		unsigned _cursor_x, unsigned _cursor_y, TextModeInfo *_tm_info)
{
	if(!m_s.valid_mode) {
		return;
	}

	bool forceUpdate = false;
	bool blink_mode = (_tm_info->blink_flags & TEXT_BLINK_MODE) > 0;
	bool blink_state = (_tm_info->blink_flags & TEXT_BLINK_STATE) > 0;
	if(blink_mode) {
		if(_tm_info->blink_flags & TEXT_BLINK_TOGGLE)
			forceUpdate = true;
	}
	if(m_s.charmap_updated) {
		forceUpdate = true;
		m_s.charmap_updated = false;
	}

	uint32_t text_palette[16];
	unsigned i;
	for(i=0; i<16; i++) {
		text_palette[i] = m_s.palette[_tm_info->actl_palette[i]];
	}

	if((_tm_info->h_panning != m_s.h_panning) || (_tm_info->v_panning != m_s.v_panning)) {
		forceUpdate = true;
		m_s.h_panning = _tm_info->h_panning;
		m_s.v_panning = _tm_info->v_panning;
	}
	if(_tm_info->line_compare != m_s.line_compare) {
		forceUpdate = true;
		m_s.line_compare = _tm_info->line_compare;
	}

	unsigned line_compare = m_s.line_compare >> _tm_info->double_scanning;

	uint32_t *buf_row = &m_fb[0];

	unsigned curs;
	// first invalidate character at previous and new cursor location
	if((m_s.prev_cursor_y < m_s.mode.textrows) && (m_s.prev_cursor_x < m_s.mode.textcols)) {
		curs = m_s.prev_cursor_y * _tm_info->line_offset + m_s.prev_cursor_x * 2;
		_old_text[curs] = ~_new_text[curs];
	}
	bool cursor_visible = ((_tm_info->cs_start <= _tm_info->cs_end) && (_tm_info->cs_start < m_s.mode.cheight));
	if((cursor_visible) && (_cursor_y < m_s.mode.textrows) && (_cursor_x < m_s.mode.textcols)) {
		curs = _cursor_y * _tm_info->line_offset + _cursor_x * 2;
		_old_text[curs] = ~_new_text[curs];
	} else {
		curs = 0xffff;
	}

	int text_rows = m_s.mode.textrows; // needs to be int! see the "while" below.
	if(m_s.v_panning) {
		text_rows++;
	}
	unsigned y = 0;
	unsigned cs_y = 0;
	uint8_t *text_base = _new_text - _tm_info->start_address;
	uint8_t split_textrow, split_fontrows;
	if(line_compare < m_s.mode.imgh) {
		split_textrow = (line_compare + m_s.v_panning) / m_s.mode.cheight;
		split_fontrows = ((line_compare + m_s.v_panning) % m_s.mode.cheight) + 1;
	} else {
		split_textrow = text_rows + 1;
		split_fontrows = 0;
	}
	bool split_screen = false;

	do {
		uint32_t *buf = buf_row;
		unsigned hchars = m_s.mode.textcols;
		if(m_s.h_panning) {
			hchars++;
		}
		uint8_t cfheight = m_s.mode.cheight;
		uint8_t cfstart = 0;
		if(split_screen) {
			if(text_rows == 1) {
				cfheight = (m_s.mode.imgh - line_compare - 1) % m_s.mode.cheight;
				if(cfheight == 0) {
					cfheight = m_s.mode.cheight;
				}
			}
		} else if(m_s.v_panning) {
			if(y == 0) {
				cfheight -= m_s.v_panning;
				cfstart = m_s.v_panning;
			} else if(text_rows == 1) {
				cfheight = m_s.v_panning;
			}
		}
		if(!split_screen && (y == split_textrow)) {
			if((split_fontrows - cfstart) < cfheight) {
				cfheight = split_fontrows - cfstart;
			}
		}
		uint8_t *new_line = _new_text;
		uint8_t *old_line = _old_text;
		unsigned x = 0;
		unsigned offset = cs_y * _tm_info->line_offset;
		do {
			uint8_t cfwidth = m_s.mode.cwidth;
			if(m_s.h_panning) {
				if(hchars > m_s.mode.textcols) {
					cfwidth -= m_s.h_panning;
				} else if(hchars == 1) {
					cfwidth = m_s.h_panning;
				}
			}
			// check if char needs to be updated
			if(forceUpdate || (_old_text[0] != _new_text[0]) || (_old_text[1] != _new_text[1]))
			{
				//PDEBUGF(LOG_V2, LOG_VGA, "%s", ICONV("IBM850", "UTF-8", (char*)(&_new_text[0]), 1));

				// Get Foreground/Background pixel colors
				uint32_t fgcolor = text_palette[_new_text[1] & 0x0F];
				uint32_t bgcolor;
				if(blink_mode) {
					bgcolor = text_palette[(_new_text[1] >> 4) & 0x07];
					if(!blink_state && (_new_text[1] & 0x80)) {
						fgcolor = bgcolor;
					}
				} else {
					bgcolor = text_palette[(_new_text[1] >> 4) & 0x0F];
				}
				bool map = 0;
				if(m_s.charmap_select) {
					map = (_new_text[1] & 0x08);
				}
				bool invert = ((offset == curs) && (cursor_visible));
				bool gfxcharw9 = ((_tm_info->line_graphics) && ((_new_text[0] & 0xE0) == 0xC0));

				// Display this one char
				uint8_t fontrows = cfheight;
				uint8_t fontline = cfstart;
				uint8_t *pfont_row;
				if(y > 0) {
					pfont_row = &m_s.charmap[map][(_new_text[0] << 5)];
				} else {
					pfont_row = &m_s.charmap[map][(_new_text[0] << 5) + cfstart];
				}
				uint32_t *buf_char = buf;
				do {
					uint16_t font_row = *pfont_row++;
					if(gfxcharw9) {
						font_row = (font_row << 1) | (font_row & 0x01);
					} else {
						font_row <<= 1;
					}
					if(hchars > m_s.mode.textcols) {
						font_row <<= m_s.h_panning;
					}
					uint8_t fontpixels = cfwidth;
					uint16_t mask;
					if((invert) && (fontline >= _tm_info->cs_start) && (fontline <= _tm_info->cs_end)) {
						mask = 0x100;
					} else {
						mask = 0x00;
					}
					do {
						uint32_t color = fgcolor;
						if((font_row & 0x100) == mask) {
							color = bgcolor;
						}
						*buf = color;
						if(_tm_info->double_dot) {
							*(buf+1) = color;
						}
						if(_tm_info->double_scanning) {
							uint32_t *dbufptr = buf + m_s.fb_width;
							*dbufptr = color;
							if(_tm_info->double_dot) {
								*(dbufptr+1) = color;
							}
						}
						buf += 1<<_tm_info->double_dot;
						font_row *= 2;
					} while(--fontpixels);
					buf -= cfwidth<<_tm_info->double_dot;
					buf += (m_s.fb_width << _tm_info->double_scanning);
					fontline++;
				} while(--fontrows);

				// restore output buffer ptr to start of this char
				buf = buf_char;
			}
			// move to next char location on screen
			buf += cfwidth<<_tm_info->double_dot;

			// select next char in old/new text
			_new_text += 2;
			_old_text += 2;
			offset += 2;
			x++;

			// process one entire horizontal row
		} while(--hchars);

		//PDEBUGF(LOG_V2, LOG_VGA, "\n");

		// go to next character row location
		buf_row += (m_s.fb_width << _tm_info->double_scanning) * cfheight;

		if(!split_screen && (y == split_textrow)) {
			_new_text = text_base;
			forceUpdate = true;
			cs_y = 0;
			if(_tm_info->split_hpanning) {
				m_s.h_panning = 0;
			}
			text_rows = ((m_s.mode.imgh - line_compare + m_s.mode.cheight - 2) / m_s.mode.cheight) + 1;
			split_screen = true;
		} else {
			_new_text = new_line + _tm_info->line_offset;
			_old_text = old_line + _tm_info->line_offset;
			cs_y++;
			y++;
		}
	} while(--text_rows >= 0);

	m_s.h_panning = _tm_info->h_panning;
	m_s.prev_cursor_x = _cursor_x;
	m_s.prev_cursor_y = _cursor_y;
}

// copy_screen
// Copies the screen to a provided buffer. The buffer must be big enough to hold
// xres * yres * 4 bytes. the buffer pitch is always xres*4
void VGADisplay::copy_screen(uint8_t *_buffer)
{
	if(!m_s.valid_mode) {
		return;
	}
	// this function is called by the GUI thread
	uint8_t *dest = _buffer;
	uint8_t *src = (uint8_t*)&m_fb[0];
	const unsigned w = m_s.mode.xres;
	const unsigned h = m_s.mode.yres;
	const unsigned bytespp = 4;
	const unsigned spitch = m_s.fb_width * bytespp;
	const unsigned dpitch = w * bytespp;
	for(unsigned y=0; y<h; y++) {
		for(unsigned x=0; x<w; x++) {
			*((uint32_t*)(&dest[y*dpitch + x*bytespp])) = *((uint32_t*)(&src[y*spitch + x*bytespp]));
		}
	}
}

// get_color
// Returns the color at the given index
uint32_t VGADisplay::get_color(uint8_t _index)
{
	return m_s.palette[_index];
}
