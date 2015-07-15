/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Portions of code Copyright (c) 2014 The Bochs Project
 */

#include "ibmulator.h"
#include "vga.h"
#include "vgadisplay.h"
#include "vgafont.h"
#include <cstring>


VGADisplay::VGADisplay()
{
	m_s.textmode = true;
	m_s.xres = 640;
	m_s.yres = 480;
	m_s.bpp = 8;
	m_s.fb_xsize = VGA_MAX_XRES;
	m_s.fb_ysize = VGA_MAX_YRES;
	m_s.tile_xsize = VGA_X_TILESIZE;
	m_s.tile_ysize = VGA_Y_TILESIZE;
	m_s.charmap_updated = false;
	m_s.prev_cursor_x = 0;
	m_s.prev_cursor_y = 0;
	m_s.h_panning = 0;
	m_s.v_panning = 0;
	m_s.line_compare = 1023;
	m_s.fontwidth = 8;
	m_s.fontheight = 16;
	m_s.text_rows = 25;
	m_s.text_cols = 80;

	m_fb = new uint32_t[VGA_MAX_XRES*VGA_MAX_YRES];
	memset(m_fb, 0, VGA_MAX_XRES*VGA_MAX_YRES*4);

	memset(m_s.palette, 0, sizeof(m_s.palette));

	for(uint i=0; i<256; i++) {
		for(uint j=0; j<16; j++) {
			m_s.charmap[i*32+j] = ms_font8x16[i][j];
		}
	}

	m_dim_updated = false;
}

VGADisplay::~VGADisplay()
{
	delete[] m_fb;
}

void VGADisplay::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "saving display state\n");

	std::lock_guard<std::mutex> lock(m_lock);

	StateHeader h;

	//state
	h.name = "VGADisplay";
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	//framebuffer
	h.name = "VGADisplay fb";
	h.data_size = sizeof(uint32_t)*VGA_MAX_XRES*VGA_MAX_YRES;
	_state.write(m_fb,h);
}

void VGADisplay::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "restoring display state\n");

	std::lock_guard<std::mutex> lock(m_lock);

	StateHeader h;

	//state
	h.name = "VGADisplay";
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	//framebuffer
	h.name = "VGADisplay fb";
	h.data_size = sizeof(uint32_t)*VGA_MAX_XRES*VGA_MAX_YRES;
	_state.read(m_fb,h);

	m_dim_updated = true;
}

// vga_clear_screen()
//
// Called to request that the VGA region is cleared.
void VGADisplay::clear_screen()
{
	memset(m_fb, 0, VGA_MAX_XRES*VGA_MAX_YRES*4);
}

void VGADisplay::set_text_charmap(uint8_t *_fbuffer)
{
	memcpy(&m_s.charmap, _fbuffer, 0x2000);
	for(uint i=0; i<256; i++) {
		m_s.char_changed[i] = true;
	}
	m_s.charmap_updated = true;
}

void VGADisplay::set_text_charbyte(uint16_t _address, uint8_t _data)
{
	m_s.charmap[_address] = _data;
	m_s.char_changed[_address >> 5] = true;
	m_s.charmap_updated = true;
}

// vga_palette_change()
//
// Allocate a color in the GUI, for this color, and put
// it in the colormap location 'index'.
// returns: 0=no screen update needed (color map change has direct effect)
//          1=screen updated needed (redraw using current colormap)
bool VGADisplay::palette_change(uint8_t index, uint8_t red, uint8_t green, uint8_t blue)
{
	m_s.palette[index] = PALETTE_ENTRY(red, green, blue);
	return true;
}

// vga_dimension_update()
//
// Called when the VGA mode changes it's X,Y dimensions.
// Resize the framebuffer to this size.
//
// x: new VGA x size
// y: new VGA y size (add headerbar_y parameter from ::specific_init().
// fheight: new VGA character height in text mode
// fwidth : new VGA character width in text mode
// bpp : bits per pixel in graphics mode
void VGADisplay::dimension_update(uint x, uint y, uint fheight, uint fwidth, uint bpp)
{
	if(x > VGA_MAX_XRES) {
		PWARNF(LOG_VGA, "requested x res %d is greater than the maximum (%d)\n", x,VGA_MAX_XRES);
		return;
	}
	if(y > VGA_MAX_YRES) {
		PWARNF(LOG_VGA, "requested y res %d is greater than the maximum (%d)\n", y,VGA_MAX_YRES);
		return;
	}

	if(bpp != 8) {
		//TODO other bit depths?
		PERRF(LOG_GUI, "%d bpp graphics mode not supported\n", bpp);
		return;
	}

	m_s.bpp = bpp;

	m_s.textmode = (fheight > 0);
	m_s.xres = x;
	m_s.yres = y;
	if(m_s.textmode) {
		m_s.fontheight = fheight;
		m_s.fontwidth = fwidth;
		m_s.text_cols = x / m_s.fontwidth;
		m_s.text_rows = y / m_s.fontheight;
	}

	PINFOF(LOG_V1, LOG_VGA, "resolution: %dx%d\n", x,y);

	m_dim_updated = true;
}

// vga_graphics_tile_update()
//
// Called to request that a tile of graphics be drawn to the
// screen, since info in this region has changed.
//
// tile: array of 8bit values representing a block of pixels with
//       dimension equal to the 'tile_xsize' & 'tile_ysize' members.
//       Each value specifies an index into the
//       array of colors you allocated for vga_palette_change()
// x0: x origin of tile
// y0: y origin of tile
//
// note: origin of tile and of window based on (0,0) being in the upper
//       left of the window.
void VGADisplay::graphics_tile_update(uint8_t *_snapshot, uint _x, uint _y)
{
	uint32_t *buf = m_fb + _y * m_s.fb_xsize + _x;

	int i = m_s.tile_ysize;
	if(_y+i > m_s.yres)
		i = m_s.yres - _y;

	// the tile is outside the display area?
	if(i<=0) {
		return;
	}

	do {
		uint32_t *buf_row = buf;
		int j = m_s.tile_xsize;
		do {
			*buf++ = m_s.palette[*_snapshot++];
		} while(--j);
		buf = buf_row + m_s.fb_xsize;
	} while(--i);
}

// vga_text_update()
//
// Called in a VGA text mode, to update the screen with
// new content.
//
// old_text: array of character/attributes making up the contents
//           of the screen from the last call.  See below
// new_text: array of character/attributes making up the current
//           contents, which should now be displayed.  See below
//
// format of old_text & new_text: each is _tm_info->line_offset*text_rows
//     bytes long. Each character consists of 2 bytes.  The first byte is
//     the character value, the second is the attribute byte.
//
// cursor_x: new x location of cursor
// cursor_y: new y location of cursor
// _tm_info:  this structure contains information for additional
//           features in text mode (cursor shape, line offset,...)
void VGADisplay::text_update(uint8_t *_old_text, uint8_t *_new_text,
		uint _cursor_x, uint _cursor_y, TextModeInfo *_tm_info)
{
	//PDEBUGF(LOG_V2, LOG_VGA, "text update\n");

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
	uint i;
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

	uint32_t *buf_row = m_fb;

	uint curs;
	// first invalidate character at previous and new cursor location
	if((m_s.prev_cursor_y < m_s.text_rows) && (m_s.prev_cursor_x < m_s.text_cols)) {
		curs = m_s.prev_cursor_y * _tm_info->line_offset + m_s.prev_cursor_x * 2;
		_old_text[curs] = ~_new_text[curs];
	}
	bool cursor_visible = ((_tm_info->cs_start <= _tm_info->cs_end) && (_tm_info->cs_start < m_s.fontheight));
	if((cursor_visible) && (_cursor_y < m_s.text_rows) && (_cursor_x < m_s.text_cols)) {
		curs = _cursor_y * _tm_info->line_offset + _cursor_x * 2;
		_old_text[curs] = ~_new_text[curs];
	} else {
		curs = 0xffff;
	}

	int rows = m_s.text_rows;
	if(m_s.v_panning) {
		rows++;
	}
	uint y = 0;
	uint cs_y = 0;
	uint8_t *text_base = _new_text - _tm_info->start_address;
	uint8_t split_textrow, split_fontrows;
	if(m_s.line_compare < m_s.yres) {
		split_textrow = (m_s.line_compare + m_s.v_panning) / m_s.fontheight;
		split_fontrows = ((m_s.line_compare + m_s.v_panning) % m_s.fontheight) + 1;
	} else {
		split_textrow = rows + 1;
		split_fontrows = 0;
	}
	bool split_screen = false;

	do {
		uint32_t *buf = buf_row;
		uint hchars = m_s.text_cols;
		if(m_s.h_panning) {
			hchars++;
		}
		uint8_t cfheight = m_s.fontheight;
		uint8_t cfstart = 0;
		if(split_screen) {
			if(rows == 1) {
				cfheight = (m_s.yres - m_s.line_compare - 1) % m_s.fontheight;
				if(cfheight == 0) {
					cfheight = m_s.fontheight;
				}
			}
		} else if(m_s.v_panning) {
			if(y == 0) {
				cfheight -= m_s.v_panning;
				cfstart = m_s.v_panning;
			} else if(rows == 1) {
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
		uint x = 0;
		uint offset = cs_y * _tm_info->line_offset;
		do {
			uint8_t cfwidth = m_s.fontwidth;
			if(m_s.h_panning) {
				if(hchars > m_s.text_cols) {
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
				bool invert = ((offset == curs) && (cursor_visible));
				bool gfxcharw9 = ((_tm_info->line_graphics) && ((_new_text[0] & 0xE0) == 0xC0));

				// Display this one char
				uint8_t fontrows = cfheight;
				uint8_t fontline = cfstart;
				uint8_t *pfont_row;
				if(y > 0) {
					pfont_row = &m_s.charmap[(_new_text[0] << 5)];
				} else {
					pfont_row = &m_s.charmap[(_new_text[0] << 5) + cfstart];
				}
				uint32_t *buf_char = buf;
				do {
					uint16_t font_row = *pfont_row++;
					if(gfxcharw9) {
						font_row = (font_row << 1) | (font_row & 0x01);
					} else {
						font_row <<= 1;
					}
					if(hchars > m_s.text_cols) {
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
						if((font_row & 0x100) == mask) {
							*buf = bgcolor;
						} else {
							*buf = fgcolor;
						}
						buf++;
						font_row <<= 1;
					} while(--fontpixels);
					buf -= cfwidth;
					buf += m_s.fb_xsize;
					fontline++;
				} while(--fontrows);

				// restore output buffer ptr to start of this char
				buf = buf_char;
			}
			// move to next char location on screen
			buf += cfwidth;

			// select next char in old/new text
			_new_text+=2;
			_old_text+=2;
			offset+=2;
			x++;

			// process one entire horizontal row
		} while(--hchars);

		//PDEBUGF(LOG_V2, LOG_VGA, "\n");

		// go to next character row location
		buf_row += m_s.fb_xsize * cfheight;
		if(!split_screen && (y == split_textrow)) {
			_new_text = text_base;
			forceUpdate = true;
			cs_y = 0;
			if(_tm_info->split_hpanning) {
				m_s.h_panning = 0;
			}
			rows = ((m_s.yres - m_s.line_compare + m_s.fontheight - 2) / m_s.fontheight) + 1;
			split_screen = 1;
		} else {
			_new_text = new_line + _tm_info->line_offset;
			_old_text = old_line + _tm_info->line_offset;
			cs_y++;
			y++;
		}
	} while(--rows);
	m_s.h_panning = _tm_info->h_panning;
	m_s.prev_cursor_x = _cursor_x;
	m_s.prev_cursor_y = _cursor_y;
}

// copy_screen
// Copies the screen to a provided buffer. The buffer must be big enough to hold
// xres * yres * 4 bytes. the buffer pitch is always xres*4
void VGADisplay::copy_screen(uint8_t *_buffer)
{
	uint8_t *dest = _buffer;
	uint8_t *src = (uint8_t*)m_fb;
	const uint w = m_s.xres;
	const uint h = m_s.yres;
	const uint bytespp = 4;
	const uint spitch = m_s.fb_xsize * bytespp;
	const uint dpitch = w * bytespp;
	for(uint y=0; y<h; y++) {
		for(uint x=0; x<w; x++) {
			*((uint32_t*)(&dest[y*dpitch + x*bytespp])) = *((uint32_t*)(&src[y*spitch + x*bytespp]));
		}
	}
}
