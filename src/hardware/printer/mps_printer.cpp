/*
 * Copyright (C) Rene Garcia
 * Copyright (C) 2022-2023  Marco Bortolin
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

// Derivative work of MPS Emulator by Rene Garcia, released under GPLv3 and
// included in 1541 Ultimate software (https://github.com/GideonZ/1541ultimate)

#include "ibmulator.h"
#include "filesys.h"
#include "utils.h"
#include "program.h"
#include "gui/gui.h"
#include "mps_printer.h"
#include <cmath>

std::map<mps_printer_paper, PrinterPaper> MpsPrinter::ms_paper_types = {
	{ MPS_PRINTER_LETTER,  { 8.5,          11,            80, "US-Letter (11\")" }},
	{ MPS_PRINTER_A4,      { 8.2677165354, 11.6929133858, 80, "ISO A4 (11.69\")" }},
	{ MPS_PRINTER_FANFOLD, { 8.5,          12,            80, "Intl. Fanfold (12\")" }},
	{ MPS_PRINTER_LEGAL,   { 8.5,          14,            80, "US-Legal (14\")"  }},
};

// =======  Horizontal pitch for letters
uint8_t MpsPrinter::ms_spacing_x[7][26] =
{
	{  0, 2, 4, 6, 8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50 },  // Pica              24px/char
	{  0, 2, 3, 5, 7, 8,10,12,13,15,17,18,20,22,23,25,27,28,30,32,33,35,37,38,40,42 },  // Elite             20px/char
	{  0, 1, 3, 4, 5, 7, 8, 9,11,12,13,15,16,17,19,20,21,23,24,25,27,28,29,31,32,33 },  // Micro             16px/char
	{  0, 1, 2, 3, 5, 6, 7, 8, 9,10,12,13,14,15,16,17,19,20,21,22,23,24,26,27,28,29 },  // Compressed        14px/char
	{  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25 },  // Pica Compressed   12px/char
	{  0, 1, 2, 2, 3, 4, 5, 6, 7, 7, 8, 9,10,11,12,12,13,14,15,16,17,17,18,19,20,21 },  // Elite Compressed  10px/char
	{  0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 9, 9,10,11,11,12,13,13,14,15,15,16,17 },  // Micro Compressed  8px/char
};

// =======  Vertical pitch for sub/super-script
uint8_t MpsPrinter::ms_spacing_y[6][17] =
{
	{  0, 3, 6, 9,12,15,18,21,24,27,30,33,36,39,42,45,48 },  // Normal Draft & NLQ High
	{  2, 5, 8,11,14,17,20,23,26,28,32,35,38,41,44,47,50 },  // Normal NLQ Low
	{  0, 2, 3, 4, 6, 7, 8,10,12,13,14,16,17,18,20,21,22 },  // Superscript Draft & NLQ High
	{  1, 3, 4, 5, 7, 8, 9,11,13,14,15,17,18,19,21,22,23 },  // Superscript NLQ Low
	{ 10,12,13,14,16,17,18,20,22,23,24,26,27,28,30,31,32 },  // Subscript Draft & NLQ High
	{ 11,13,14,15,17,18,19,21,23,24,25,27,28,29,31,32,33 },  // Subscript NLQ Low
};

// =======  Color Palette
// 
// Palette made by hand with photoshop and saved to .act file
// CMYK with C,M,Y from values 0% 25% 50% 100%
// K from values 0% 14% 45% 100%
// black dot compensation applied for colors where C,M,Y = 0,0,0
// in this table each value is RED then GREEN then BLUE on 8 bits
// for 256 colors
// 
uint8_t MpsPrinter::ms_rgb_palette[768] =
{
	0xFF, 0xFF, 0xFF, 0xB9, 0xE5, 0xFB, 0x6D, 0xCF, 0xF6, 0x00, 0xAE, 0xEF,
	0xF9, 0xCB, 0xDF, 0xBB, 0xB8, 0xDC, 0x7D, 0xA7, 0xD9, 0x00, 0x8F, 0xD5,
	0xF4, 0x9A, 0xC1, 0xBD, 0x8C, 0xBF, 0x87, 0x81, 0xBD, 0x00, 0x72, 0xBC,
	0xEC, 0x00, 0x8C, 0xBD, 0x1A, 0x8D, 0x92, 0x27, 0x8F, 0x2E, 0x31, 0x92,
	0xFF, 0xFB, 0xCC, 0xC0, 0xE2, 0xCA, 0x7A, 0xCC, 0xC8, 0x00, 0xAB, 0xC5,
	0xFB, 0xC8, 0xB4, 0xC1, 0xB6, 0xB3, 0x86, 0xA6, 0xB2, 0x00, 0x8E, 0xB0,
	0xF5, 0x98, 0x9D, 0xC0, 0x8C, 0x9C, 0x8D, 0x81, 0x9C, 0x00, 0x71, 0x9C,
	0xED, 0x09, 0x73, 0xBF, 0x1E, 0x74, 0x94, 0x29, 0x77, 0x32, 0x32, 0x7B,
	0xFF, 0xF7, 0x99, 0xC4, 0xDF, 0x9B, 0x82, 0xCA, 0x9C, 0x00, 0xA9, 0x9D,
	0xFD, 0xC6, 0x89, 0xC3, 0xB4, 0x8B, 0x8A, 0xA4, 0x8C, 0x00, 0x8C, 0x8D,
	0xF6, 0x96, 0x79, 0xC2, 0x8B, 0x7B, 0x8F, 0x80, 0x7D, 0x00, 0x70, 0x7E,
	0xED, 0x14, 0x5B, 0xBF, 0x24, 0x5E, 0x94, 0x2C, 0x61, 0x34, 0x34, 0x65,
	0xFF, 0xF2, 0x00, 0xCB, 0xDB, 0x2A, 0x8D, 0xC6, 0x3F, 0x00, 0xA6, 0x51,
	0xFF, 0xC2, 0x0E, 0xC8, 0xB1, 0x2F, 0x91, 0xA2, 0x3D, 0x00, 0x8A, 0x4B,
	0xFF, 0xA9, 0x17, 0xC5, 0x89, 0x2F, 0x94, 0x7F, 0x3A, 0x00, 0x6F, 0x45,
	0xED, 0x1C, 0x24, 0xC1, 0x27, 0x2D, 0x96, 0x2F, 0x34, 0x36, 0x36, 0x39,
	0xE0, 0xE0, 0xE0, 0xA3, 0xCA, 0xDD, 0x61, 0xB7, 0xD9, 0x00, 0x9A, 0xD3,
	0xD9, 0xB2, 0xC5, 0xA5, 0xA2, 0xC2, 0x6F, 0x94, 0xC0, 0x00, 0x7F, 0xBC,
	0xD5, 0x87, 0xAB, 0xA6, 0x7C, 0xA9, 0x78, 0x73, 0xA8, 0x00, 0x64, 0xA6,
	0xCF, 0x00, 0x7B, 0xA6, 0x10, 0x7C, 0x81, 0x1C, 0x7E, 0x28, 0x27, 0x81,
	0xE1, 0xDC, 0xB4, 0xA8, 0xC7, 0xB3, 0x6C, 0xB4, 0xB1, 0x00, 0x98, 0xAE,
	0xDC, 0xB0, 0x9F, 0xA9, 0xA1, 0x9E, 0x76, 0x93, 0x9D, 0x00, 0x7E, 0x9C,
	0xD7, 0x86, 0x8B, 0xA9, 0x7B, 0x8A, 0x7C, 0x72, 0x8A, 0x00, 0x64, 0x8A,
	0xCF, 0x00, 0x65, 0xA8, 0x16, 0x67, 0x82, 0x20, 0x69, 0x2C, 0x29, 0x6C,
	0xE3, 0xD9, 0x88, 0xAB, 0xC4, 0x89, 0x72, 0xB2, 0x8A, 0x00, 0x96, 0x8B,
	0xDD, 0xAE, 0x7A, 0xAB, 0x9F, 0x7B, 0x79, 0x91, 0x7C, 0x00, 0x7C, 0x7D,
	0xD7, 0x84, 0x6B, 0xAA, 0x7A, 0x6C, 0x7E, 0x71, 0x6E, 0x00, 0x63, 0x6F,
	0xD0, 0x0D, 0x4F, 0xA8, 0x1B, 0x52, 0x82, 0x23, 0x54, 0x2D, 0x2B, 0x58,
	0xE5, 0xD4, 0x00, 0xB1, 0xC0, 0x25, 0x7B, 0xAF, 0x37, 0x00, 0x93, 0x48,
	0xDE, 0xAA, 0x0E, 0xAF, 0x9C, 0x27, 0x7F, 0x8F, 0x34, 0x00, 0x7A, 0x42,
	0xD8, 0x82, 0x19, 0xAD, 0x78, 0x27, 0x82, 0x70, 0x31, 0x00, 0x62, 0x3C,
	0xD0, 0x18, 0x1F, 0xA9, 0x21, 0x25, 0x84, 0x27, 0x2A, 0x30, 0x2D, 0x30,
	0xA0, 0xA0, 0xA0, 0x73, 0x91, 0xA0, 0x43, 0x84, 0x9D, 0x00, 0x6F, 0x9A,
	0x9B, 0x7F, 0x8E, 0x76, 0x75, 0x8D, 0x4F, 0x6B, 0x8B, 0x00, 0x5A, 0x89,
	0x99, 0x5F, 0x7B, 0x78, 0x58, 0x7A, 0x55, 0x51, 0x7A, 0x00, 0x45, 0x79,
	0x95, 0x00, 0x58, 0x78, 0x00, 0x58, 0x5D, 0x00, 0x5A, 0x18, 0x0F, 0x5E,
	0x9F, 0x9D, 0x82, 0x77, 0x8F, 0x81, 0x4A, 0x82, 0x80, 0x00, 0x6E, 0x7E,
	0x9C, 0x7E, 0x73, 0x78, 0x73, 0x72, 0x52, 0x6A, 0x72, 0x00, 0x5A, 0x71,
	0x99, 0x5E, 0x63, 0x79, 0x57, 0x63, 0x58, 0x51, 0x63, 0x00, 0x46, 0x63,
	0x95, 0x00, 0x46, 0x79, 0x00, 0x48, 0x5D, 0x07, 0x4A, 0x1A, 0x12, 0x4D,
	0xA0, 0x9A, 0x61, 0x79, 0x8C, 0x62, 0x4E, 0x80, 0x63, 0x00, 0x6D, 0x64,
	0x9C, 0x7C, 0x56, 0x79, 0x72, 0x57, 0x54, 0x68, 0x58, 0x00, 0x59, 0x59,
	0x99, 0x5D, 0x4B, 0x79, 0x56, 0x4C, 0x58, 0x50, 0x4D, 0x00, 0x45, 0x4F,
	0x94, 0x00, 0x34, 0x78, 0x05, 0x37, 0x5C, 0x0D, 0x39, 0x1A, 0x15, 0x3D,
	0xA1, 0x97, 0x00, 0x7B, 0x89, 0x16, 0x52, 0x7D, 0x24, 0x00, 0x6C, 0x32,
	0x9D, 0x79, 0x00, 0x7A, 0x6F, 0x16, 0x57, 0x66, 0x20, 0x00, 0x58, 0x2C,
	0x99, 0x5B, 0x05, 0x79, 0x55, 0x14, 0x5A, 0x4F, 0x1D, 0x00, 0x45, 0x26,
	0x94, 0x07, 0x0A, 0x78, 0x0E, 0x0F, 0x5C, 0x13, 0x15, 0x1C, 0x18, 0x1C,
	0x00, 0x00, 0x00, 0x0C, 0x1A, 0x22, 0x00, 0x15, 0x22, 0x00, 0x06, 0x24,
	0x23, 0x0E, 0x15, 0x11, 0x06, 0x18, 0x00, 0x01, 0x19, 0x00, 0x01, 0x21,
	0x23, 0x00, 0x09, 0x16, 0x00, 0x10, 0x0A, 0x00, 0x17, 0x00, 0x00, 0x1E,
	0x29, 0x00, 0x03, 0x21, 0x00, 0x0F, 0x1A, 0x00, 0x15, 0x0E, 0x00, 0x1A,
	0x20, 0x1D, 0x12, 0x09, 0x19, 0x14, 0x00, 0x15, 0x15, 0x00, 0x06, 0x17,
	0x21, 0x0D, 0x05, 0x0F, 0x07, 0x08, 0x00, 0x01, 0x0B, 0x00, 0x01, 0x15,
	0x21, 0x00, 0x00, 0x15, 0x00, 0x02, 0x07, 0x00, 0x0B, 0x00, 0x01, 0x14,
	0x27, 0x00, 0x02, 0x1F, 0x00, 0x04, 0x18, 0x00, 0x0C, 0x0E, 0x00, 0x1A,
	0x1E, 0x1C, 0x00, 0x06, 0x18, 0x02, 0x06, 0x18, 0x02, 0x00, 0x05, 0x08,
	0x1F, 0x0C, 0x00, 0x0B, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
	0x1F, 0x00, 0x00, 0x11, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x09,
	0x25, 0x00, 0x01, 0x1D, 0x00, 0x01, 0x16, 0x00, 0x03, 0x07, 0x00, 0x0B,
	0x18, 0x1A, 0x00, 0x00, 0x18, 0x00, 0x00, 0x14, 0x00, 0x00, 0x05, 0x00,
	0x1A, 0x0C, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x1C, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x23, 0x00, 0x01, 0x1A, 0x00, 0x01, 0x12, 0x00, 0x00, 0x03, 0x00, 0x00
};

// =======  B/W Palette
uint8_t MpsPrinter::ms_bw_palette[12] =
{
	255, 255, 255, // White
	224, 224, 224, // Light grey
	160, 160, 160, // Dark grey
	0,   0,   0    // Black
};


MpsPrinter::MpsPrinter()
{
	init_config();
	init_interpreter();
}

MpsPrinter::~MpsPrinter()
{
	if(m_lodeinit) {
		lodepng_state_cleanup(&m_lodepng_state);
	}
	if(m_sdl_palette) {
		SDL_FreePalette(m_sdl_palette);
	}
}

void MpsPrinter::init_config()
{
	bool color = g_program.config().get_bool(PRN_SECTION, PRN_COLOR, false);
	init_color(color);

	mps_printer_interpreter mode = static_cast<mps_printer_interpreter>(
		g_program.config().get_enum(PRN_SECTION, PRN_MODE, {
			{"epson", MPS_PRINTER_INTERPRETER_EPSON },
			{"ibmpp", MPS_PRINTER_INTERPRETER_IBMPP },
			{"ibmgp", MPS_PRINTER_INTERPRETER_IBMGP }
		}, MPS_PRINTER_INTERPRETER_EPSON));
	set_interpreter(mode);

	unsigned ink = g_program.config().get_enum(PRN_SECTION, PRN_INK, {
			{ "low",    0 }, { "lo",  0 }, { "0", 0 },
			{ "medium", 1 }, { "med", 1 }, { "1", 1 },
			{ "high",   2 }, { "hi",  2 }, { "2", 2 }
		}, 1);
	set_dot_size(ink);

	m_config.epson_charset = g_program.config().get_enum(PRN_SECTION, PRN_EPSON_CSET, {
			{ "basic",     0 },
			{ "usa",       1 }, { "us",   1 },
			{ "france",    2 }, { "fr",   2 },
			{ "germany",   3 }, { "de",   3 },
			{ "england",   4 }, { "uk",   4 },
			{ "denmark1",  5 }, { "dk1",  5 },
			{ "sweden",    6 }, { "se",   6 },
			{ "italy",     7 }, { "it",   7 },
			{ "spain",     8 }, { "sp",   8 },
			{ "japan",     9 }, { "jp",   9 },
			{ "norway",   10 }, { "no",  10 },
			{ "denmark2", 11 }, { "dk2", 11 }
		}, 0);

	m_config.ibm_charset = g_program.config().get_enum(PRN_SECTION, PRN_IBM_CSET, {
			{ "intl1",    1 },
			{ "intl2",    2 },
			{ "israel",   3 }, { "il", 3 },
			{ "greece",   4 }, { "gr", 4 },
			{ "portugal", 5 }, { "pt", 5 },
			{ "spain",    6 }, { "sp", 6 }
		}, 1);

	m_config.bof = g_program.config().get_int(PRN_SECTION, PRN_BOF, 0);
	if(m_config.bof < 0) {
		m_config.bof = 0;
	}

	m_config.top_margin = g_program.config().get_real(PRN_SECTION, PRN_TOP_MARGIN, -1.0);

	m_config.bottom_margin = g_program.config().get_real(PRN_SECTION, PRN_BOTTOM_MARGIN, MPS_PRINTER_MIN_BOTTOM_MARGIN);
	if(m_config.bottom_margin < 0) {
		m_config.bottom_margin = 0;
	}

	m_config.preview_div = int(g_program.config().get_enum(PRN_SECTION, PRN_PREVIEW_DIV, {
			{ "max",  1 },
			{ "high", 3 },
			{ "low",  6 }
		}, 6));
	m_config.preview_x_dpi = MPS_PRINTER_DPI_X / m_config.preview_div;
	m_config.preview_y_dpi = MPS_PRINTER_DPI_Y / m_config.preview_div;
}

MpsPrinter::PageBuffer::PageBuffer(MpsPrinter *_prn, double _w_inch, double _h_inch, bool _color)
{
	auto [width, height] = _prn->get_bitmap_px(_w_inch, _h_inch);
	int depth = _color ? MPS_PRINTER_PAGE_DEPTH_COLOR : MPS_PRINTER_PAGE_DEPTH_BW;
	int bitmap_bytes = ((width * height * depth + 7) >> 3);
	bitmap.resize(bitmap_bytes);

	auto [pw,ph] = _prn->get_preview_px(_w_inch, _h_inch);
	preview = SDL_CreateRGBSurface(0, pw, ph, 32,
			0x000000ff,
			0x0000ff00,
			0x00ff0000,
			0xff000000
	);
	SDL_SetSurfaceBlendMode(preview, SDL_BLENDMODE_NONE);

	clear();
}

MpsPrinter::PageBuffer::PageBuffer(PageBuffer &&_b)
{
	bitmap = std::move(_b.bitmap);
	preview = _b.preview;
	_b.preview = nullptr;
	clean = _b.clean;
}

void MpsPrinter::PageBuffer::clear()
{
	std::fill(bitmap.begin(), bitmap.end(), 0);
	SDL_FillRect(preview, NULL, SDL_MapRGB(preview->format, 255,255,255));
	clean = true;
}

MpsPrinter::PageBuffer::~PageBuffer()
{
	if(preview) {
		SDL_FreeSurface(preview);
	}
}

void MpsPrinter::thread_start()
{
	PDEBUGF(LOG_V0, LOG_PRN, "MpsPrinter: thread started\n");

	while(true) {
		PDEBUGF(LOG_V5, LOG_PRN, "MpsPrinter: waiting for commands\n");
		std::function<void()> fn;
		m_cmd_queue.wait_and_pop(fn);
		fn();
		if(m_quit) {
			break;
		}
	}

	PDEBUGF(LOG_V0, LOG_PRN, "MpsPrinter: thread stopped\n");
}

void MpsPrinter::cmd_quit()
{
	m_cmd_queue.push([this] () {
		unload_paper();
		m_quit = true;
	});
}

void MpsPrinter::set_base_dir(std::string _path)
{
	m_outdir = _path + FS_SEP + FileSys::get_next_dirname(_path, "printer_");
}

/**
 * Changes the ink dot size.
 * 
 * @param _ds New dot diameter: 0 = 1 pixel, 1 = 2 pixels, 2 = 3 pixels
 */
void MpsPrinter::cmd_set_dot_size(uint8_t _ds)
{
	m_cmd_queue.push([=] () {
		set_dot_size(_ds);
		PDEBUGF(LOG_V1, LOG_PRN, "Dotsize changed to %d\n", m_dot_size);
	});
}

void MpsPrinter::set_dot_size(uint8_t _ds)
{
	m_dot_size = _ds;
	if(m_dot_size > 2) {
		m_dot_size = 2;
	}
}

/**
* Changes interpreter. The interpreter state is reset but the head stays at the
* same place and the page is not cleared.
* Command for other threads.
* 
* @param _in New interpreter
*/
void MpsPrinter::cmd_set_interpreter(mps_printer_interpreter _in)
{
	m_cmd_queue.push([=] () {
		set_interpreter(_in);
		init_interpreter();
	});
}

/**
* Changes interpreter. The interpreter state is reset but the head stays at the
* same place and the page is not cleared.
* 
* @param _in New interpreter
*/
void MpsPrinter::set_interpreter(mps_printer_interpreter _in)
{
	// Check if a change of interpreter is requested
	if(m_interpreter != _in) {
		static const char *names[] = {
			"Epson FX-80",
			"IBM Proprinter",
			"IBM Graphics"
		};
		PINFOF(LOG_V1, LOG_PRN, "Changed interpreter to %s\n", names[_in]);
		m_interpreter = _in;
	}
}

uint8_t MpsPrinter::get_pixel(int _buf, int _x, int _y) const
{
	assert(_buf < int(m_page.buffers.size()));
	int pixaddr = get_bitmap_byte(_x, _y);
	if(pixaddr < 0) {
		return 0;
	}
	assert(pixaddr < int(m_page.buffers[_buf].bitmap.size()));
	uint8_t byte = m_page.buffers[_buf].bitmap[pixaddr];
	if(m_color_mode) {
		return byte;
	} else {
		uint8_t sub = _x & 0x3;
		uint8_t shift = 6 - sub*2;
		return (byte >> shift) & 0x03;
	}
}

void MpsPrinter::put_pixel(int _buf, int _x, int _y, uint8_t _pix)
{
	assert(_buf < int(m_page.buffers.size()));
	int pixaddr = get_bitmap_byte(_x, _y);
	if(pixaddr < 0) {
		return;
	}
	assert(pixaddr < int(m_page.buffers[_buf].bitmap.size()));

	if(m_color_mode) {
		m_page.buffers[_buf].bitmap[pixaddr] = _pix;
	} else {
		uint8_t sub = _x & 0x03;
		uint8_t shift = 6 - sub*2;
		uint8_t byte = m_page.buffers[_buf].bitmap[pixaddr];
		byte &= uint8_t(0xff & ~(0x03 << shift));
		byte |= (_pix & 0x03) << shift;
		m_page.buffers[_buf].bitmap[pixaddr] = byte;
	}
}

void MpsPrinter::copy_preview(SDL_Surface &_dest)
{
	std::lock_guard<std::mutex> lock(m_preview_mtx);
	
	m_preview_upd = false;
	if(m_page.buffers.empty()) {
		SDL_FillRect(&_dest, NULL, SDL_MapRGB(_dest.format, 255,255,255));
		return;
	}
	auto [buf, x, y] = get_bitmap_pos(m_head_x, m_head_y);
	if(buf >= int(m_page.buffers.size()) || buf < 0) {
		SDL_FillRect(&_dest, NULL, SDL_MapRGB(_dest.format, 255,255,255));
		return;
	}
	SDL_SetSurfaceBlendMode(m_page.buffers[size_t(buf)].preview , SDL_BLENDMODE_NONE);
	SDL_SetSurfaceBlendMode(&_dest, SDL_BLENDMODE_NONE);
	if(SDL_BlitScaled(m_page.buffers[size_t(buf)].preview, NULL, &_dest, NULL) < 0) {
		PDEBUGF(LOG_V2, LOG_PRN, "Preview error: %s\n", SDL_GetError());
	}
}

std::pair<int,int> MpsPrinter::get_bitmap_px(double _inch_w, double _inch_h) const
{
	unsigned w = int(round(_inch_w * MPS_PRINTER_DPI_X));
	unsigned h = int(round(_inch_h * MPS_PRINTER_DPI_Y));
	return {w,h};
}

std::pair<int,int> MpsPrinter::get_preview_px(double _inch_w, double _inch_h) const
{
	unsigned w = int(round(_inch_w * m_config.preview_x_dpi));
	unsigned h = int(round(_inch_h * m_config.preview_y_dpi));
	return {w,h};
}

std::pair<int,int> MpsPrinter::get_preview_max_size() const
{
	double max_w=0.0, max_h=0.0;
	for(auto & paper : ms_paper_types) {
		if(paper.second.width_inch > max_w) {
			max_w = paper.second.width_inch;
		}
		if(paper.second.height_inch > max_h) {
			max_h = paper.second.height_inch;
		}
	}
	return get_preview_px(max_w, max_h);
}

std::pair<int,int> MpsPrinter::get_head_pos() const
{
	auto [buf, tx, ty] = get_bitmap_pos(m_head_x, m_head_y);
	return {tx,ty};
}

std::tuple<int,int,int> MpsPrinter::get_bitmap_pos(int _x, int _y) const
{
	if(m_page.buffers.empty()) {
		return {-1,0,0};
	}
	int x = _x + m_page.offset_left_px;
	int y = _y + m_page.offset_top_px;
	if(y < 0 || x < 0) {
		return {-1,0,0};
	}
	int buffer = (y / m_page.height_px);
	y %= m_page.height_px;

	return {buffer,x,y};
}

/**
 * Sets the printer to color or black and white mode.
 * 
 * @param color true for color, false for greyscale
 */
void MpsPrinter::init_color(bool _color)
{
	PDEBUGF(LOG_V1, LOG_PRN, "Mode to %s\n", _color?"color":"b/w");

	// =======  Default printer attributes

	// Except on first call, don't do anything if config has not changed
	m_color_mode = _color;

	if(m_lodeinit) {
		lodepng_state_cleanup(&m_lodepng_state);
	}

	// Initialise PNG convertor attributes
	lodepng_state_init(&m_lodepng_state);

	// PNG compression settings
	m_lodepng_state.encoder.zlibsettings.btype      = 2;
	m_lodepng_state.encoder.zlibsettings.use_lz77   = true;
	m_lodepng_state.encoder.zlibsettings.windowsize = 1024;
	m_lodepng_state.encoder.zlibsettings.minmatch   = 3;
	m_lodepng_state.encoder.zlibsettings.nicematch  = 128;

	// Initialise color palette for memory bitmap and file output
	lodepng_palette_clear(&m_lodepng_state.info_png.color);
	lodepng_palette_clear(&m_lodepng_state.info_raw);

	if (m_color_mode)
	{
		// =======  Color printer
		//
		// Each color is coded on 2 bits (3 shades + white)
		// bits 7,6 : black
		// bits 4,5 : yellow
		// bits 2,3 : magenta
		// bits 0,1 : cyan
		//

		m_sdl_palette = SDL_AllocPalette(256);

		int x=0;  // index in RGB component palette
		for (int i=0; i<256; i++)
		{
			SDL_Color col;

			col.r = ms_rgb_palette[x++];
			col.g = ms_rgb_palette[x++];
			col.b = ms_rgb_palette[x++];
			col.a = 255;

			lodepng_palette_add(&m_lodepng_state.info_png.color, col.r, col.g, col.b, col.a);
			lodepng_palette_add(&m_lodepng_state.info_raw, col.r, col.g, col.b, col.a);

			SDL_SetPaletteColors(m_sdl_palette, &col, i, 1);
		}

		// Bitmap uses 6 bit depth and a palette
		m_lodepng_state.info_png.color.colortype  = LCT_PALETTE;
		m_lodepng_state.info_png.color.bitdepth   = MPS_PRINTER_PAGE_DEPTH_COLOR;
		m_lodepng_state.info_raw.colortype        = LCT_PALETTE;
		m_lodepng_state.info_raw.bitdepth         = MPS_PRINTER_PAGE_DEPTH_COLOR;
	}
	else
	{
		// =======  Greyscale printer

		m_sdl_palette = SDL_AllocPalette(4);

		int x=0;
		for (int i=0; i<4; i++)
		{
			SDL_Color col;

			col.r = ms_bw_palette[x++];
			col.g = ms_bw_palette[x++];
			col.b = ms_bw_palette[x++];
			col.a = 255;

			lodepng_palette_add(&m_lodepng_state.info_png.color, col.r, col.g, col.b, col.a);
			lodepng_palette_add(&m_lodepng_state.info_raw, col.r, col.g, col.b, col.a);

			SDL_SetPaletteColors(m_sdl_palette, &col, i, 1);
		}

		// Bitmap uses 2 bit depth and a palette
		m_lodepng_state.info_png.color.colortype  = LCT_PALETTE;
		m_lodepng_state.info_png.color.bitdepth   = MPS_PRINTER_PAGE_DEPTH_BW;
		m_lodepng_state.info_raw.colortype        = LCT_PALETTE;
		m_lodepng_state.info_raw.bitdepth         = MPS_PRINTER_PAGE_DEPTH_BW;
	}

	// Physical page description
	m_lodepng_state.info_png.phys_defined = 1;
	m_lodepng_state.info_png.phys_unit    = 1; // 1=meters
	m_lodepng_state.info_png.phys_x       = round(double(MPS_PRINTER_DPI_X) * 39.3701); // dots-per-meter
	m_lodepng_state.info_png.phys_y       = round(double(MPS_PRINTER_DPI_Y) * 39.3701); // dots-per-meter

	// I rule, you don't
	m_lodepng_state.encoder.auto_convert  = 0;

	m_lodeinit = true;
}

/**
 * Sets the printer interpreter to default state; doesn't clear the page.
 */
void MpsPrinter::init_interpreter()
{
	PDEBUGF(LOG_V1, LOG_PRN, "Interpreter init requested\n");

	// =======  Default tabulation stops

	for (int i=0; i<MPS_PRINTER_MAX_HTABULATIONS; i++) {
		m_htab[i] = 168 + i*24*8;
	}

	for (int j=0; j<MPS_PRINTER_MAX_VTABSTORES; j++) {
		for (int i=0; i<MPS_PRINTER_MAX_VTABULATIONS; i++) {
			m_vtab_store[j][i] = 0;
		}
	}

	m_vtab = m_vtab_store[0];

	// =======  Default printer attributes

	m_step            = 0;
	m_script          = MPS_PRINTER_SCRIPT_NORMAL;
	m_interline       = MPS_PRINTER_DEF_LINE_HEIGHT;
	m_next_interline  = m_interline;
	m_charset_variant = 0;
	m_bim_density     = 0;
	m_color           = MPS_PRINTER_COLOR_BLACK;
	m_italic          = false;
	m_underline       = false;
	m_overline        = false;
	m_double_width    = false;
	m_bold            = false;
	m_nlq             = false;
	m_double_strike   = false;
	m_auto_lf         = false;
	m_bim_mode        = false;
	m_state           = MPS_PRINTER_STATE_INITIAL;
	m_top_form        = 0;
	m_bottom_form     = m_config.bof * MPS_PRINTER_DEF_LINE_HEIGHT;
	m_margin_left     = 0;
	m_margin_right    = MPS_PRINTER_MAX_WIDTH_PX;
	int len = int(round(MPS_PRINTER_FORM_LEN_INCH * MPS_PRINTER_DPI_Y));
	if(m_page.is_loaded()) {
		len = m_page.height_px;
	}
	set_form_length(len);

	m_bim_K_density   = 0;  // EPSON specific 60 dpi
	m_bim_L_density   = 1;  // EPSON specific 120 dpi
	m_bim_Y_density   = 2;  // EPSON specific 120 dpi high speed
	m_bim_Z_density   = 3;  // EPSON specific 240 dpi


	// =======  Default charsets (user defined)

	m_epson_charset_extended = false;

	switch(m_interpreter)
	{
		case MPS_PRINTER_INTERPRETER_EPSON:
			m_charset = m_config.epson_charset;
			break;

		case MPS_PRINTER_INTERPRETER_IBMPP:
		case MPS_PRINTER_INTERPRETER_IBMGP:
			m_charset = 0;
			break;
	}
}

void MpsPrinter::carriage_return()
{
	m_head_x = m_margin_left;
}

void MpsPrinter::move_paper(int _pixels)
{
	if(_pixels < 0 && m_head_y < abs(_pixels)) {
		// not sure what should happen
		PDEBUGF(LOG_V0, LOG_PRN, "Tried to move paper to previous form\n");
		_pixels = -int(m_head_y);
	}

	m_head_y += _pixels;

	if(m_head_y >= m_ff_limit) {
		form_feed(m_head_y <= m_form_length);
	} else {
		auto [buf,x,y] = get_bitmap_pos(m_head_x,m_head_y);
		if(m_page.single_sheet) {
			if(y >= m_page.bottom_margin_limit) {
				form_feed();
			}
		} else {
			std::lock_guard<std::mutex> lock(m_preview_mtx);
			while(buf >= int(m_page.buffers.size())) {
				add_page();
			}
			if(y + MPS_PRINTER_HEAD_HEIGHT >= m_page.height_px) {
				add_page();
			}
		}
	}
}

void MpsPrinter::line_feed(bool _cr)
{
	move_paper(m_interline);

	if(_cr) {
		carriage_return();
	}
}

void MpsPrinter::add_page()
{
	// a lock must be acquired by the caller
	m_page.buffers.emplace_back(this, m_paper.width_inch, m_paper.height_inch, m_color_mode);
	PDEBUGF(LOG_V1, LOG_PRN, "New page added: %u\n", static_cast<unsigned>(m_page.buffers.size()));
}

void MpsPrinter::clear_page(PageBuffer &_page)
{
	// a lock must be acquired by the caller
	PDEBUGF(LOG_V1, LOG_PRN, "Clear page bitmap\n");

	_page.clear();
	m_preview_upd = true;

#ifndef NDEBUG
	//PrintPalette();
	//print_marks();
#endif
}

void MpsPrinter::form_feed(bool _move)
{
	std::lock_guard<std::mutex> lock(m_preview_mtx);

	if(m_page.single_sheet) {
		save_page_to_file(m_page.buffers.front());
		clear_page(m_page.buffers.front());
		m_head_y = m_top_form;
	} else {
		/* Anatomy of continuous forms
		 * 
		 * +++ = perforation
		 * --- = form limits
		 * ... = bottom-of-form limit
		 * hy0 = head y 0 position
		 *
		 *    +++++++++++++++++++++++++++
		 *    sheet 1
		 *    top offset
		 *    
		 *    hy0------------------------
		 *    form 1
		 *    
		 *    
		 *    
		 *    
		 *    
		 *    
		 *    bof........................
		 *    
		 *    
		 *    +++++++++++++++++++++++++++
		 *    sheet 2
		 *    
		 *    
		 *    ---------------------------
		 *    form 2
		 *    
		 */
		m_page.offset_top_px += m_form_length;
		while(m_page.offset_top_px >= m_page.height_px) {
			save_page_to_file(m_page.buffers.front());
			clear_page(m_page.buffers.front());
			auto page = std::move(m_page.buffers.front());
			m_page.buffers.pop_front();
			m_page.buffers.push_back(std::move(page));
			m_page.offset_top_px -= m_page.height_px;
		}
		if(_move) {
			m_head_y = m_top_form;
		} else {
			m_head_y = m_head_y % m_form_length;
		}
	}

	carriage_return();
}

void MpsPrinter::cmd_set_online()
{
	m_cmd_queue.push([=] () {
		if(!is_online()) {
			while(!m_data_queue.empty()) {
				interpret(m_data_queue.front());
				m_data_queue.pop();
			}
			m_online = true;
		}
	});
}

void MpsPrinter::cmd_set_offline()
{
	m_cmd_queue.push([=] () {
		m_online = false;
	});
}

void MpsPrinter::cmd_form_feed()
{
	m_cmd_queue.push([=] () {
		form_feed();
	});
}

void MpsPrinter::cmd_line_feed()
{
	m_cmd_queue.push([=] () {
		line_feed();
	});
}

void MpsPrinter::cmd_load_paper(mps_printer_paper _paper, bool _single_sheet)
{
	m_cmd_queue.push([=] () {
		load_paper(ms_paper_types.at(_paper), _single_sheet);
		init_interpreter();
	});
}

void MpsPrinter::set_form_length(uint16_t _pixels)
{
	if(_pixels == 0) {
		return;
	}

	m_form_length = _pixels;

	if(m_form_length >= m_bottom_form) {
		m_ff_limit = m_form_length - m_bottom_form;
	} else {
		m_ff_limit = m_form_length;
	}

	PDEBUGF(LOG_V1, LOG_PRN, "New form length: %upx (%.1f\"), BOF:%upx\n", _pixels, (double(_pixels)/MPS_PRINTER_DPI_Y), m_ff_limit);
}

void MpsPrinter::set_bof(uint16_t _pixels)
{
	m_bottom_form = _pixels;

	if(m_form_length >= m_bottom_form) {
		m_ff_limit = m_form_length - m_bottom_form;
	} else {
		m_ff_limit = m_form_length;
	}
	
	PDEBUGF(LOG_V1, LOG_PRN, "New BOF: %upx\n", m_ff_limit);
}

void MpsPrinter::load_paper(PrinterPaper _paper, bool _single_sheet)
{
	unload_paper();

	PINFOF(LOG_V1, LOG_PRN, "Loading paper: %s (%s)\n", _paper.name, 
			_single_sheet?"single sheets":"continuous forms");

	std::lock_guard<std::mutex> lock(m_preview_mtx);

	m_page.single_sheet = _single_sheet;

	auto [wpx,hpx] = get_bitmap_px(_paper.width_inch, _paper.height_inch);
	m_page.width_px = wpx;
	m_page.height_px = hpx;

	m_page.printable_width_px = _paper.printable_cols * MPS_PRINTER_COL_WIDTH_PX;

	double top_margin = m_config.top_margin;
	if(m_config.top_margin < 0.0) {
		if(_single_sheet) {
			top_margin = MPS_PRINTER_MIN_TOP_MARGIN;
		} else {
			top_margin = 0.0;
		}
	}
	m_page.offset_top_px = int(top_margin * MPS_PRINTER_DPI_Y);
	// left offset is so that text is centered; ~32 pixels for A4 paper
	m_page.offset_left_px = int((m_page.width_px - m_page.printable_width_px) / 2.0);

	if(m_config.bottom_margin < _paper.height_inch) {
		m_page.bottom_margin_limit = m_page.height_px - int(m_config.bottom_margin * MPS_PRINTER_DPI_Y);
	} else {
		m_page.bottom_margin_limit = m_page.height_px;
	}

	PDEBUGF(LOG_V1, LOG_PRN, "  size: %.1f\"x%.1f\", %dx%dpx\n",
			_paper.width_inch, _paper.height_inch,
			m_page.width_px, m_page.height_px);
	PDEBUGF(LOG_V1, LOG_PRN, "  offsets: top:%d, left:%dpx\n", m_page.offset_top_px, m_page.offset_left_px);
	PDEBUGF(LOG_V1, LOG_PRN, "  printable width: %u cols, %dpx\n", _paper.printable_cols, m_page.printable_width_px);

	m_paper = _paper;

	add_page();

	m_head_y = 0;
	m_head_x = m_margin_left;
}

void MpsPrinter::unload_paper()
{
	if(!m_page.is_loaded()) {
		return;
	}

	for(auto &buf : m_page.buffers) {
		if(!buf.clean) {
			save_page_to_file(buf);
		}
	}

	std::lock_guard<std::mutex> lock(m_preview_mtx);
	m_page.buffers.clear();
}

/**
 * Saves the current page to a PNG.
 */
void MpsPrinter::save_page_to_file(const PageBuffer &_buf)
{
	try {
		FileSys::create_dir(m_outdir.c_str());
	} catch(std::exception &) {
		PERRF(LOG_PRN, "Cannot create directory %s\n", m_outdir.c_str());
		return;
	}

	std::string filename = FileSys::get_next_filename(m_outdir, "page_", ".png");
	PINFOF(LOG_V0, LOG_PRN, "Saving %s\n", filename.c_str());

	led_on();

	uint8_t *buffer = NULL;
	size_t outsize;
	unsigned error = lodepng_encode(&buffer, &outsize, _buf.bitmap.data(), m_page.width_px, m_page.height_px, &m_lodepng_state);

	if(!error) {
		auto file = FileSys::make_file(filename.c_str(), "wb");
		if(fwrite(buffer, outsize, 1, file.get()) != 1) {
			PERRF(LOG_PRN, "There was an error saving the PNG file\n");
		} else {
			GUI::instance()->show_message(str_format("Saved printer page to %s", filename.c_str()));
		}
	} else {
		PERRF(LOG_PRN, "There was an error encoding the image\n");
	}
	free(buffer);

	m_page.count++;

	led_off();
}

/**
 * Prints a single dot on the page. The dot size depends on the density setting.
 * If position is out of printable area, no dot is printed.
 * 
 * @param _x    pixel position from left of logical page
 * @param _y    pixel position from top of logical page
 * @param _bim  true if DOT is part of BIM. No double-strike or bold treatment is applied if true
 */
void MpsPrinter::print_dot(int _x, int _y, bool _bim)
{
	if(_x >= m_margin_right || _y >= m_ff_limit) {
		PDEBUGF(LOG_V2, LOG_PRN, "Dot position outside the page area: x:%d(%u),y:%d(%u)\n", _x, m_margin_right, _y,m_ff_limit);
	}

	switch (m_dot_size)
	{
		case 0:     // Density 0 : 1 single full color point (diameter 1 pixel) mostly for debug
			put_ink(_x, _y, 3);
			break;

		case 1:     // Density 1 : 1 full color point with shade around (looks like diameter 2)
			put_ink(_x,   _y,   3);

			put_ink(_x-1, _y-1, 1);
			put_ink(_x+1, _y+1, 1);
			put_ink(_x-1, _y+1, 1);
			put_ink(_x+1, _y-1, 1);

			put_ink(_x,   _y-1, 2);
			put_ink(_x,   _y+1, 2);
			put_ink(_x-1, _y,   2);
			put_ink(_x+1, _y,   2);
			break;

		case 2:     // Density 2 : 4 full color points with shade around (looks like diameter 3)
		default:
			put_ink(_x,   _y,   3);
			put_ink(_x,   _y+1, 3);
			put_ink(_x+1, _y,   3);
			put_ink(_x+1, _y+1, 3);

			put_ink(_x-1, _y-1, 1);
			put_ink(_x+2, _y-1, 1);
			put_ink(_x-1, _y+2, 1);
			put_ink(_x+2, _y+2, 1);

			put_ink(_x,   _y-1, 2);
			put_ink(_x+1, _y-1, 2);
			put_ink(_x,   _y+1, 2);
			put_ink(_x-1, _y,   2);
			put_ink(_x-1, _y+1, 2);
			put_ink(_x+2, _y,   2);
			put_ink(_x+2, _y+1, 2);
			put_ink(_x,   _y+2, 2);
			put_ink(_x+1, _y+2, 2);
			break;
	}

	if (!_bim)   // This is not BIM related, we can double strike and bold
	{
		// -------  If double strike is ON, draw a second dot just to the right of the first one
		if(m_bold) {
			print_dot(_x+2, _y, true);
		}
		if(m_double_strike) {
			print_dot(_x, _y+1, true);
			if(m_bold) {
				print_dot(_x+2, _y+1, true);
			}
		}
	}
}

int MpsPrinter::get_bitmap_byte(int _x, int _y) const
{
	if(_x < 0 || _y < 0) {
		return -1;
	}
	uint32_t depth = m_color_mode ? MPS_PRINTER_PAGE_DEPTH_COLOR : MPS_PRINTER_PAGE_DEPTH_BW;
	uint32_t byte = ((_y * m_page.width_px + _x) * depth) >> 3;
	return byte;
}

/**
 * Combines a grey level with a dot on the bitmap and gives the resulting grey.
 *
 * @param _buf    the page buffer to write to
 * @param _x      pixel position from left of 'shade 2'
 * @param _y      pixel position from top of 'shade 2'
 * @param _shade  'shade 1' grey level (0 is white, 3 is black)
 * @param _shift  color channel/sub pixel shift to read 'shade 2' and write the result to
 * @return  resulting grey level (in range range 0-3)
 */
uint8_t MpsPrinter::add_color(int _buf, int _x, int _y, uint8_t _shade, uint32_t _shift)
{
	//   white      + white         = white
	//   white      + light grey    = light grey
	//   white      + dark grey     = dark grey
	//   light grey + light grey    = dark grey
	//   light grey + dark grey     = black
	//   dark grey  + dark grey     = black
	//   black      + *whatever*    = black

	assert(_buf >= 0 && _buf < int(m_page.buffers.size()));

	int pixaddr = get_bitmap_byte(_x, _y);
	if(pixaddr < 0 || pixaddr >= int(m_page.buffers[_buf].bitmap.size())) {
		return 0;
	}

	uint8_t byte = m_page.buffers[_buf].bitmap[pixaddr];
	uint8_t current = (byte >> _shift) & 0x03;
	uint8_t color = _shade + current;
	if(color > 3) {
		color = 3;
	}
	byte &= uint8_t(0xff & ~(0x03 << _shift));
	byte |= color << _shift;

	m_page.buffers[_buf].bitmap[pixaddr] = byte;
	m_page.buffers[_buf].clean = false;

	return color;
}

/**
 * Adds ink on a single pixel position. If ink as already been added on this
 * position it will add more ink to be darker. On color printer, uses the current
 * color ribbon for ink.
 * 
 * @param _x  pixel position from left of form (logical page)
 * @param _y  pixel position from top of form (logical page)
 * @param _c  shade level 3=full, 2=dark shade, 1=light shade
 */
void MpsPrinter::put_ink(int _x, int _y, uint8_t _c)
{
	// =======  Calculate true x and y position on bitmap
	auto [buf, tx, ty] = get_bitmap_pos(_x, _y);

	if(buf < 0 || buf >= int(m_page.buffers.size())) {
		return;
	}
	if(tx >= m_page.width_px || ty >= m_page.height_px) {
		PDEBUGF(LOG_V0, LOG_PRN, "Ink position outside the bitmap area: x:%d\n", tx);
		return;
	}

	uint8_t color = _c;

	if (m_color_mode)
	{
		// -------  Which bits on byte are coding the color (1 pixel per byte)
		// Each color is coded on 2 bits (3 shades + white)
		// bits 7,6 : black
		// bits 4,5 : yellow
		// bits 2,3 : magenta
		// bits 0,1 : cyan
		switch (m_color)
		{
			case MPS_PRINTER_COLOR_BLACK:
				add_color(buf, tx, ty, color, 6);
				break;

			case MPS_PRINTER_COLOR_YELLOW:
				add_color(buf, tx, ty, color, 4);
				break;

			case MPS_PRINTER_COLOR_MAGENTA:
				add_color(buf, tx, ty, color, 2);
				break;

			case MPS_PRINTER_COLOR_CYAN:
				add_color(buf, tx, ty, color, 0);
				break;

			case MPS_PRINTER_COLOR_VIOLET:
				// CYAN + MAGENTA
				color = add_color(buf, tx, ty, color, 0);
				        add_color(buf, tx, ty, color, 2);
				break;

			case MPS_PRINTER_COLOR_ORANGE:
				// MANGENTA + YELLOW
				color = add_color(buf, tx, ty, color, 2);
				        add_color(buf, tx, ty, color, 4);
				break;

			case MPS_PRINTER_COLOR_GREEN:
				// CYAN + YELLOW
				color = add_color(buf, tx, ty, color, 0);
				        add_color(buf, tx, ty, color, 4);
				break;
		}
		int byteid = get_bitmap_byte(tx, ty);
		if(byteid < 0) {
			return;
		}
		color = m_page.buffers[buf].bitmap[byteid];
	}
	else
	{
		// -------  Which bits on byte are coding the pixel (4 pixels per byte)
		uint8_t sub = tx & 0x03;
		uint8_t shift = 6 - sub*2;
		color &= 0x3;

		// =======  Add ink to the pixel
		color = add_color(buf, tx, ty, color, shift);
	}

	int px = tx / m_config.preview_div;
	int py = ty / m_config.preview_div;
	if(px < m_page.buffers[buf].preview->w && py < m_page.buffers[buf].preview->h) {
		std::lock_guard<std::mutex> lock(m_preview_mtx);
		assert(color < m_sdl_palette->ncolors);
		SDL_Color *pal = &m_sdl_palette->colors[color];
		Uint32 col = SDL_MapRGBA(
			m_page.buffers[buf].preview->format,
			pal->r, pal->g, pal->b, pal->a
		);
		unsigned pix_byte = py * m_page.buffers[buf].preview->pitch + px * 4;
		SDL_LockSurface(m_page.buffers[buf].preview);
		memcpy(static_cast<uint8_t*>(m_page.buffers[buf].preview->pixels) + pix_byte, &col, 4);
		SDL_UnlockSurface(m_page.buffers[buf].preview);
		m_preview_upd = true;
	}
}

/**
 * Prints margin marks on the paper (debugging)
 */
void MpsPrinter::print_marks()
{
	for(int h=0; h<10; h++) {
		put_pixel(0, 0, h, 1);
	}
	for(int w=0; w<10; w++) {
		put_pixel(0, w, 0, 1);
	}
}

/**
 * Prints a color palette on the paper (debugging)
 */
void MpsPrinter::print_palette()
{
	for (int x=0; x<256; x++) {
		for (int y=0; y<256; y++) {
			int c = (x >> 4) | (y & 0xF0);
			put_pixel(0, x, y, c);
		}
	}
}

/**
 * Prints a single italic draft quality character.
 * 
 * @param c char from italic chargen table
 * @param x first pixel position from left of printable area
 * @param y first pixel position from top of printable area
 * @return printed char width (in pixels)
 */
uint16_t MpsPrinter::print_char_italic(uint16_t _c, int _x, int _y)
{
	// =======  Check if chargen is in italic chargen range
	if (_c > 128) {
		return 0;
	}

	PDEBUGF(LOG_V2, LOG_PRN, "Print char italic 0x%02x at x:%u,y:%u\n", _c, _x, _y);
	
	uint8_t lst_head = 0; // Last printed pattern to calculate reverse
	uint8_t shift = ms_chargen_italic[_c][11] & 1; // 8 down pins from 9 ?

	// =======  For each value of the pattern
	for (int i=0; i<(m_double_width?24:12); i++)
	{
		uint8_t cur_head;

		if (m_double_width)
		{
			if (i & 1) {
				cur_head = 0;
			} else {
				cur_head = (i>>1 == 11) ? 0 : ms_chargen_italic[_c][i>>1];
				if (i>1) {
					cur_head |= ms_chargen_italic[_c][(i>>1)-1];
				}
			}
		}
		else
		{
			cur_head = (i == 11) ? 0 : ms_chargen_italic[_c][i];
		}
	
		// -------  Reverse is negative printing
		if (m_reverse)
		{
			uint8_t saved_head = cur_head;
			cur_head = cur_head | lst_head;
			cur_head ^= 0xFF;
			lst_head = saved_head;
		}
	
		// Calculate x position according to width and stepping
		int dx = _x + ms_spacing_x[m_step][i];
	
		// -------  Each dot to print (LSB is up)
		for (int j=0; j<8; j++)
		{
			// pin 9 is used for underline, can't be used on shifted chars
			if (m_underline && shift && m_script == MPS_PRINTER_SCRIPT_NORMAL && j==7) {
				continue;
			}
	
			// Need to print a dot ?
			if (cur_head & 0x01)
			{
				// vertical position according to stepping for normal, subscript or superscript
				int dy = _y + ms_spacing_y[m_script][j+shift];
	
				// The dot itself
				print_dot(dx, dy);
			}
	
			cur_head >>= 1;
		}
	
		// -------  Need to underline ?
	
		// Underline is one dot every 2 pixels
		if (!(i&1) && m_underline)
		{
			int dy = _y + ms_spacing_y[MPS_PRINTER_SCRIPT_NORMAL][8];
			print_dot(dx, dy);
		}
	}
	
	// =======  This is the width of the printed char
	return ms_spacing_x[m_step][12] << (m_double_width ? 1 : 0);
}

/**
 * Prints a single regular draft quality character.
 *
 * @param _c  char from draft chargen table
 * @param _x  pixel position from left of logical page
 * @param _y  pixel position from top of logical page
 * @return printed char width (in pixels)
 */
uint16_t MpsPrinter::print_char_draft(uint16_t _c, int _x, int _y)
{
	// =======  Check is chargen is in draft chargen range
	if (_c > 403) {
		return 0;
	}
	
	PDEBUGF(LOG_V2, LOG_PRN, "Print char draft 0x%02x at x:%u,y:%u\n", _c, _x, _y);
	
	uint8_t lst_head = 0; // Last printed pattern to calculate reverse
	uint8_t shift = ms_chargen_draft[_c][11] & 1;     // 8 down pins from 9 ?
	
	// =======  For each value of the pattern
	for (int i=0; i<(m_double_width?24:12); i++)
	{
		uint8_t cur_head;
	
		if (m_double_width)
		{
			if (i & 1) {
				cur_head = 0;
			} else {
				cur_head = (i>>1 == 11) ? 0 : ms_chargen_draft[_c][i>>1];
				if (i>1) {
					cur_head |= ms_chargen_draft[_c][(i>>1)-1];
				}
			}
		}
		else
		{
			cur_head = (i == 11) ? 0 : ms_chargen_draft[_c][i];
		}
	
		// -------  Reverse is negative printing
		if (m_reverse)
		{
			uint8_t saved_head = cur_head;
			cur_head = cur_head | lst_head;
			cur_head ^= 0xFF;
			lst_head = saved_head;
		}
	
		// Calculate x position according to width and stepping
		int dx = _x + ms_spacing_x[m_step][i];
	
		// -------  Each dot to print (LSB is up)
		for (int j=0; j<8; j++)
		{
			// pin 9 is used for underline, can't be used on shifted chars
			if (m_underline && shift && m_script == MPS_PRINTER_SCRIPT_NORMAL && j==7) {
				cur_head >>= 1;
				continue;
			}
			if (m_overline && j==(shift?3:4)) {
				cur_head >>= 1;
				continue;
			}
	
			// Need to print a dot ?
			if (cur_head & 0x01)
			{
				// vertical position according to stepping for normal, subscript or superscript
				int dy = _y + ms_spacing_y[m_script][j+shift];
	
				// The dot itself
				print_dot(dx, dy);
			}
	
			cur_head >>= 1;
		}
	
		// -------  Need to underline ?
	
		// Overline is one dot every 2 pixels
		if (!(i&1) && m_overline)
		{
			int dy = _y + ms_spacing_y[m_script][4];
			print_dot(dx, dy);
		}
	
		// Underline is one dot every 2 pixels
		if (!(i&1) && m_underline)
		{
			int dy = _y + ms_spacing_y[MPS_PRINTER_SCRIPT_NORMAL][8];
			print_dot(dx, dy);
		}
	}
	
	// =======  If the char is completed by a second chargen below, go print it
	if (ms_chargen_draft[_c][11] & 0x80)
	{
		print_char_draft((ms_chargen_draft[_c][11] & 0x70) >> 4, _x, _y+ms_spacing_y[m_script][shift+8]);
	}
	
	// =======  This is the width of the printed char
	return ms_spacing_x[m_step][12] << (m_double_width ? 1 : 0);
}

/**
 * Prints a single regular NLQ character.
 * 
 * @param _c  char from draft nlq (high and low) table
 * @param _x  first pixel position from left of printable area
 * @param _y  first pixel position from top of printable area
 * @return printed char width (in pixels)
 */
uint16_t MpsPrinter::print_char_nlq(uint16_t _c, int _x, int _y)
{
	// =======  Check if chargen is in NLQ chargen range
	if (_c > 403) {
		return 0;
	}

	PDEBUGF(LOG_V2, LOG_PRN, "Print char NLQ 0x%02x at x:%u,y:%u\n", _c, _x, _y);
	
	uint8_t lst_head_low = 0;   // Last low printed pattern to calculate reverse
	uint8_t lst_head_high = 0;  // Last high printed pattern to calculate reverse
	uint8_t shift = ms_chargen_nlq_high[_c][11] & 1;  // 8 down pins from 9 ?
	
	// =======  For each value of the pattern
	for (int i=0; i<(m_double_width?24:12); i++)
	{
		uint8_t cur_head_high;
		uint8_t cur_head_low;
	
		// -------  Calculate last column
		if (m_double_width)
		{
			cur_head_high = (i>>1 == 11) ? 0 : ms_chargen_nlq_high[_c][i>>1];
			if (i > 1) {
				cur_head_high |= ms_chargen_nlq_high[_c][(i>>1)-1];
			}
	
			cur_head_low = (i>>1 == 11) ? 0 : ms_chargen_nlq_low[_c][i>>1];
			if (i > 1) {
				cur_head_low |= ms_chargen_nlq_low[_c][(i>>1)-1];
			}
		}
		else
		{
			if (i == 11)
			{
				if (ms_chargen_nlq_high[_c][11] & 0x04)
				{
					// Repeat last colums
					cur_head_high = ms_chargen_nlq_high[_c][10];
				}
				else
				{
					// Blank column
					cur_head_high = 0;
				}
	
				if (ms_chargen_nlq_low[_c][11] & 0x04)
				{
					// Repeat last colums
					cur_head_high = ms_chargen_nlq_low[_c][10];
				}
				else
				{
					// Blank column
					cur_head_low = 0;
				}
			}
			else
			{
				/* Not on last column, get data from chargen table */
				cur_head_high = ms_chargen_nlq_high[_c][i];
				cur_head_low = ms_chargen_nlq_low[_c][i];
			}
		}
	
		// -------  Reverse is negative printing
		if (m_reverse)
		{
			uint8_t saved_head_high = cur_head_high;
			uint8_t saved_head_low = cur_head_low;
	
			cur_head_high = cur_head_high | lst_head_high;
			cur_head_high ^= 0xFF;
			lst_head_high = saved_head_high;
	
			cur_head_low = cur_head_low | lst_head_low;
			cur_head_low ^= 0xFF;
			lst_head_low = saved_head_low;
		}
	
		// -------  First we start with the high pattern
		uint8_t head = cur_head_high;
	
		// Calculate x position according to width and stepping
		int dx = _x + ms_spacing_x[m_step][i];
	
		// -------  Each dot to print (LSB is up)
		for (int j=0; j<8; j++)
		{
			// Need to print a dot ?
			if (head & 0x01)
			{
				// vertical position according to stepping for normal, subscript or superscript
				int dy = _y + ms_spacing_y[m_script][j+shift];
	
				// The dot itself
				print_dot(dx, dy);
			}
	
			head >>= 1;
		}
	
		// -------  Now we print the low pattern
		head = cur_head_low;
	
		// -------  Each dot to print (LSB is up)
		for (int j=0; j<8; j++)
		{
			// pin 9 on low pattern is used for underline, can't be used on shifted chars
			if (m_underline && shift && m_script == MPS_PRINTER_SCRIPT_NORMAL && j==7) {
				continue;
			}
	
			// Need to print a dot ?
			if (head & 0x01)
			{
				// vertical position according to stepping for normal, subscript or superscript
				int dy = _y + ms_spacing_y[m_script+1][j+shift];
	
				// The dot itself
				print_dot(dx, dy);
			}
	
			head >>= 1;
		}
	
		// -------  Need to underline ?
		// Underline is one dot every pixel on NLQ quality
		if (m_underline)
		{
			int dy = _y+ms_spacing_y[MPS_PRINTER_SCRIPT_NORMAL+1][8];
			print_dot(dx, dy);
		}
	}
	
	// =======  If the char is completed by a second chargen below, go print it
	if (ms_chargen_nlq_high[_c][11] & 0x80)
	{
		print_char_nlq((ms_chargen_nlq_high[_c][11] & 0x70) >> 4, _x, _y+ms_spacing_y[m_script][shift+8]);
	}
	
	// =======  This is the width of the printed char
	return ms_spacing_x[m_step][12] << (m_double_width ? 1 : 0);
}

/**
 * Interpret a byte of data as sent by the computer.
 * 
 * @param _data  data to interpret
 */
void MpsPrinter::interpret(uint8_t _data)
{
	PDEBUGF(LOG_V5, LOG_PRN, "interpret: 0x%02X\n", _data);

	led_on();

	switch (m_interpreter)
	{
		case MPS_PRINTER_INTERPRETER_EPSON:
			interpret_epson(_data);
			break;

		case MPS_PRINTER_INTERPRETER_IBMPP:
			interpret_ibmpp(_data);
			break;

		case MPS_PRINTER_INTERPRETER_IBMGP:
			interpret_ibmgp(_data);
			break;

		default:
			assert(false);
			break;
	}

	led_off();
}

void MpsPrinter::cmd_send_byte(uint8_t _data)
{
	m_cmd_queue.push([=] () {
		if(is_online()) {
			interpret(_data);
		} else {
			m_data_queue.push(_data);
		}
	});
}

/**
 * Tells if a char code is printable or not
 * 
 * @param _input  Byte code to test
 * @return true if printable
 */
bool MpsPrinter::is_printable(uint8_t _input)
{
	bool result = false;

	// In charset Tables, non printables are coded 500

	switch (m_interpreter)
	{
		case MPS_PRINTER_INTERPRETER_EPSON:
			if (m_epson_charset_extended) {
				result = (ms_charset_epson_extended[_input & 0x7F] == 500) ? false : true;
			}
			if (!result) {
				result = (ms_charset_epson[m_charset][_input & 0x7F] == 500) ? false : true;
			}
			break;

		case MPS_PRINTER_INTERPRETER_IBMPP:
		case MPS_PRINTER_INTERPRETER_IBMGP:
			result = (ms_charset_ibm[m_charset][_input] == 500) ? false : true;
			break;
	}

	return result;
}

/**
 * Gives the chargen code of a charset character
 * 
 * @param _input  Byte code to convert
 * @return chargen code, 500 if non printable, >=1000 if italic (sub 1000 to get
 *         italic chargen code)
 */
uint16_t MpsPrinter::charset2chargen(uint8_t _input)
{
	uint16_t chargen_id = 500;
	
	// =======  Read char entry in charset
	switch (m_interpreter)
	{
		case MPS_PRINTER_INTERPRETER_EPSON:
			if (m_epson_charset_extended) {
				chargen_id = ms_charset_epson_extended[_input & 0x7F];
			}
			// Get chargen entry for this char in Epson charset
			if (chargen_id == 500) {
				chargen_id = ms_charset_epson[m_charset][_input & 0x7F];
			}
			break;
	
		case MPS_PRINTER_INTERPRETER_IBMPP:
		case MPS_PRINTER_INTERPRETER_IBMGP:
			// Get chargen entry for this char in IBM charset
			chargen_id = ms_charset_ibm[m_charset][_input];
			break;
	}
	
	// =======  Italic is enabled, get italic code if not 500
	// In EPSON, ASCII codes from 128-255 are the same as 0-127 but with italic enabled
	if ((m_italic || (m_interpreter == MPS_PRINTER_INTERPRETER_EPSON && (_input & 0x80)))
		&& ms_convert_italic[chargen_id] != 500)
	{
		// Add 1000 to result to tell the drawing routine that this is from italic chargen
		chargen_id = ms_convert_italic[chargen_id] + 1000;
	}
	
	return chargen_id;
}

/**
 * Prints a char on a page; it will call Italic, Draft or NLQ print method
 * depending on current configuration.
 * 
 * @param c Chargen code to print (+1000 if italic)
 * @return printed char width
 */
uint16_t MpsPrinter::print_char(uint16_t _c)
{
	// 1000 is italic offset
	if (_c >= 1000)
	{
		// call Italic print method
		return print_char_italic(_c - 1000, m_head_x, m_head_y);
	}
	else
	{
		if (m_nlq)
		{
			// call NLQ print method
			return print_char_nlq(_c, m_head_x, m_head_y);
		}
		else
		{
			// call NLQ print method
			return print_char_draft(_c, m_head_x, m_head_y);
		}
	}
}

/**
 * Turns on the activity LED (calls can be nested)
 */
void MpsPrinter::led_on()
{
	m_activity++;
}

/**
 * Turns off the activity LED (calls can be nested)
 */
void MpsPrinter::led_off()
{
	if(m_activity > 0) {
		m_activity--;
	}
}
