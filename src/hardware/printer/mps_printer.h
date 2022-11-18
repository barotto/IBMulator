/*
 * Copyright (C) Rene Garcia
 * Copyright (C) 2022  Marco Bortolin
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

// 9-pin dot-matrix color printer emulator.

// Derivative work of MPS Emulator by Rene Garcia, released under GPLv3 and
// included in 1541 Ultimate software (https://github.com/GideonZ/1541ultimate)
//
// Commodore interpreter removed.
// Continuous forms paper support added.
// Multiple page sizes added.
// Page preview bitmap added.


#ifndef _MPS_PRINTER_H_
#define _MPS_PRINTER_H_

#include "shared_queue.h"
#include "lodepng/lodepng.h"
#include <SDL.h>

// defaults for US letter (11") paper:
// page height in rows = 60
// rows at bottom of page = 6 
// 1 row height = 1/6" (36 pixels)

// physical printer properties
#define MPS_PRINTER_DPI_X             240
#define MPS_PRINTER_DPI_Y             216   // minimum vertical spacing is 1/216"
#define MPS_PRINTER_HEAD_HEIGHT       27    // 9 pins x 3 pixel-per-pin (1/72" per pin)
#define MPS_PRINTER_CHAR_INCH         10    // character per inch (PICA spacing)
#define MPS_PRINTER_DEF_LINE_HEIGHT   36    // default line height at power on, 1/6" * 216dpi
#define MPS_PRINTER_MAX_COLS          80    // max columns (PICA spacing)
#define MPS_PRINTER_FORM_LEN_INCH     11    // default form length at power on
#define MPS_PRINTER_DEF_BOF_LINES     6     // default bottom of form at power on
#define MPS_PRINTER_COL_WIDTH_PX      24    // 0.1" per column * 240dpi
#define MPS_PRINTER_MAX_WIDTH_PX      (MPS_PRINTER_MAX_COLS * MPS_PRINTER_COL_WIDTH_PX) // 1920px for an 80 cols printer at 240dpi
#define MPS_PRINTER_MIN_TOP_MARGIN    (3.0/8.0)   // minimum top margin in inches (single sheet and newly loaded continuous forms)
#define MPS_PRINTER_MIN_BOTTOM_MARGIN (3.0/16.0)  // minimum bottom margin in inches (single sheet)
#define MPS_PRINTER_OFFSET_TOP        int(MPS_PRINTER_MIN_TOP_MARGIN  * MPS_PRINTER_DPI_Y)   // minimum top margin in pixels
#define MPS_PRINTER_OFFSET_BOTTOM     int(MPS_PRINTER_MIN_BOTTOM_MARGIN * MPS_PRINTER_DPI_Y) // minimum bottom margin in pixels

#define MPS_PRINTER_MAX_HTABULATIONS  32
#define MPS_PRINTER_MAX_VTABULATIONS  32
#define MPS_PRINTER_MAX_VTABSTORES    8

#define MPS_PRINTER_PAGE_DEPTH_BW     2
#define MPS_PRINTER_PAGE_DEPTH_COLOR  8

#define MPS_PRINTER_MAX_BIM_SUB       256
#define MPS_PRINTER_MAX_SPECIAL       46

#define MPS_PRINTER_SCRIPT_NORMAL     0
#define MPS_PRINTER_SCRIPT_SUPER      2
#define MPS_PRINTER_SCRIPT_SUB        4

enum mps_printer_state {
	MPS_PRINTER_STATE_INITIAL,
	MPS_PRINTER_STATE_PARAM,
	MPS_PRINTER_STATE_ESC,
	MPS_PRINTER_STATE_ESC_PARAM
};

enum mps_printer_interpreter {
	MPS_PRINTER_INTERPRETER_EPSON,
	MPS_PRINTER_INTERPRETER_IBMPP,
	MPS_PRINTER_INTERPRETER_IBMGP
};

enum mps_printer_step {
	MPS_PRINTER_STEP_PICA,
	MPS_PRINTER_STEP_ELITE,
	MPS_PRINTER_STEP_MICRO,
	MPS_PRINTER_STEP_CONDENSED,
	MPS_PRINTER_STEP_PICA_COMPRESSED,
	MPS_PRINTER_STEP_ELITE_COMPRESSED,
	MPS_PRINTER_STEP_MICRO_COMPRESSED
};

enum mps_printer_color {
	MPS_PRINTER_COLOR_BLACK,
	MPS_PRINTER_COLOR_MAGENTA,
	MPS_PRINTER_COLOR_CYAN,
	MPS_PRINTER_COLOR_VIOLET,  // CYAN + MAGENTA
	MPS_PRINTER_COLOR_YELLOW,
	MPS_PRINTER_COLOR_ORANGE,  // MANGENTA + YELLOW
	MPS_PRINTER_COLOR_GREEN    // CYAN + YELLOW
};

struct PrinterPaper
{
	double width_inch;       // width in inches
	double height_inch;      // height in inches
	int printable_cols = 0;  // printable area in columns (PICA)
	const char *name;
};

enum mps_printer_paper
{
	MPS_PRINTER_LETTER,  // US-Letter
	MPS_PRINTER_A4,      // ISO A4
	MPS_PRINTER_FANFOLD, // European Fanfold
	MPS_PRINTER_LEGAL    // US-Legal
};

class MpsPrinter
{
	private:
		static std::map<mps_printer_paper, PrinterPaper> ms_paper_types;

		// =======  Embeded data
		// Char Generators (bitmap definition of each character)
		static uint8_t ms_chargen_italic[129][12];
		static uint8_t ms_chargen_nlq_low[404][12];
		static uint8_t ms_chargen_nlq_high[404][12];
		static uint8_t ms_chargen_draft[404][12];

		// Italic chargen lookup table
		static uint16_t ms_convert_italic[404];

		// Charsets
		static uint16_t ms_charset_epson[12][128];
		static uint16_t ms_charset_ibm[7][256];
		static uint16_t ms_charset_epson_extended[128];

		// Dot spacing on X axis depending on character width (pical, elite, compressed, ...)
		static uint8_t ms_spacing_x[7][26];

		// Dot spacing on Y axis depending on character style (normal, superscript, subscript)
		static uint8_t ms_spacing_y[6][17];

		// Color palette
		static uint8_t ms_rgb_palette[768];
		static uint8_t ms_bw_palette[12];
		SDL_Palette *m_sdl_palette = nullptr; // used by the preview

		// =======  Configuration
		std::string m_outdir; // current print job directory
		std::string m_outfile; // file basename

		bool m_lodeinit = false;
		LodePNGState m_lodepng_state; // PNG writer state

		// tabulation stops
		uint16_t m_htab[MPS_PRINTER_MAX_HTABULATIONS];
		uint16_t m_vtab_store[MPS_PRINTER_MAX_VTABSTORES][MPS_PRINTER_MAX_VTABULATIONS];
		uint16_t *m_vtab;

		// True if color printer
		bool m_color_mode;

		// Current interpreter to use
		mps_printer_interpreter m_interpreter = MPS_PRINTER_INTERPRETER_EPSON;

		// Current international charset
		uint8_t m_charset = 0;

		// Saved international charsets
		bool m_epson_charset_extended;

		// =======  Print head configuration
		// Ink density
		uint8_t m_dot_size = 1;

		// Ink color
		mps_printer_color m_color;

		// Logical printer head position in pixels
		std::atomic<int> m_head_x;
		std::atomic<int> m_head_y;

		// Current interline value (the line height)
		uint16_t m_interline; // in pixels
		uint16_t m_next_interline;

		// Margins (pixels)
		uint16_t m_margin_left;  // pixel position of the left margin
		uint16_t m_margin_right; // pixel position of the right margin
		uint16_t m_form_length;  // page height
		uint16_t m_top_form;     // top margin (set by ibmpp only)
		uint16_t m_bottom_form;  // "skip over perforation" height (the space above the top-of-form position of the following page)
		uint16_t m_ff_limit;     // pixel distance from logical 0 that triggers a form feed

		// BIM
		uint8_t m_bim_density;
		uint8_t m_bim_K_density;  // EPSON specific
		uint8_t m_bim_L_density;  // EPSON specific
		uint8_t m_bim_Y_density;  // EPSON specific
		uint8_t m_bim_Z_density;  // EPSON specific
		uint8_t m_bim_position;
		bool m_bim_mode;

		// =======  Current print attributes
		bool m_reverse;       // Negative characters
		bool m_double_width;  // Double width characters
		bool m_nlq;           // Near Letter Quality
		bool m_double_strike; // Print twice at the same place
		bool m_underline;     // Underline is on
		bool m_overline;      // Overline is on (not implemented yet)
		bool m_bold;          // Bold is on
		bool m_italic;        // Italic is on
		bool m_auto_lf;       // Automatic LF on CR (IBM Proprinter only)

		// =======  Current spacing configuration
		uint8_t m_step;     // X spacing
		uint8_t m_script;   // Y spacing

		// =======  Current CBM charset variant (Uppercases/graphics or Lowercases/Uppercases)
		uint8_t m_charset_variant;

		// =======  Page properties
		PrinterPaper m_paper;
		struct PageBuffer {
			std::vector<uint8_t> bitmap;
			SDL_Surface *preview = nullptr;
			bool clean = true;

			PageBuffer(MpsPrinter *_prn, double _w_inch, double _h_inch, bool _color);
			PageBuffer(PageBuffer &&_b);
			~PageBuffer();
			void clear();
		};
		struct {
			int width_px;                // page width in pixels
			int height_px;               // page height in pixels
			int offset_top_px;           // top printing offset in pixels
			int offset_left_px;          // left printing offset in pixels
			int printable_width_px;      // printable area width in pixels
			int bottom_margin_limit;     // single sheets cannot be printed after this point
			bool single_sheet;           // true=single, false=continuous
			std::atomic<int> count = 0;  // total number of pages saved to disk
			std::deque<PageBuffer> buffers;

			bool is_loaded() const { return !buffers.empty(); }
		} m_page;

		struct {
			uint8_t epson_charset;
			uint8_t ibm_charset;
			int bof = 0;
			double top_margin = 0.0;
			double bottom_margin = 0.0;
			int preview_div = 1;
			int preview_x_dpi = MPS_PRINTER_DPI_X;
			int preview_y_dpi = MPS_PRINTER_DPI_Y;
		} m_config;

		// =======  Interpreter state
		mps_printer_state m_state;
		uint16_t m_param_count;
		uint32_t m_param_build;
		uint8_t m_bim_sub[MPS_PRINTER_MAX_BIM_SUB];
		uint16_t m_bim_count;

		// Current ESC command waiting for a parameter
		uint8_t m_esc_command;

		// Activity LED state
		std::atomic<uint8_t> m_activity = 0;

		std::atomic<bool> m_online = true;
		std::queue<uint8_t> m_data_queue;

		// =======  Thread state
		bool m_quit = false;
		shared_queue<std::function<void()>> m_cmd_queue;
		std::mutex m_preview_mtx; // GUI sync for the preview
		std::atomic<bool> m_preview_upd = false;

	public:
		MpsPrinter();
		~MpsPrinter();

		void set_base_dir(std::string _path);

		void thread_start();

		// Thread interface
		void cmd_quit();
		void cmd_set_filename(std::string _outdir, std::string _filename);
		void cmd_set_charset_variant(uint8_t _cs);
		void cmd_set_dot_size(uint8_t _ds);
		void cmd_set_interpreter(mps_printer_interpreter _it);
		void cmd_set_epson_charset(uint8_t _in);
		void cmd_set_ibm_charset(uint8_t _in);
		void cmd_send_byte(uint8_t _data);
		void cmd_set_online();
		void cmd_set_offline();
		void cmd_form_feed();
		void cmd_line_feed();
		void cmd_load_paper(mps_printer_paper _paper, bool _single_sheet);

		bool is_active() const {
			return m_activity ||
			      !m_cmd_queue.empty() ||
			      !m_data_queue.empty();
		}
		bool is_online() const { return m_online; }
		int get_page_count() const { return m_page.count; }
		bool is_paper_loaded() const { return m_paper.printable_cols; }
		PrinterPaper get_paper() const { return m_paper; }
		bool is_preview_updated() const { return m_preview_upd; }
		void copy_preview(SDL_Surface &_dest);
		std::pair<int,int> get_preview_max_size() const;
		std::pair<int,int> get_page_size_px() const { return {m_page.width_px,m_page.height_px}; }
		std::pair<int,int> get_head_pos() const;
		mps_printer_interpreter get_interpreter() const { return m_interpreter; }
		bool is_color_mode() const { return m_color_mode; }

	private:
		void init_config();
		void set_dot_size(uint8_t _ds);
		void init_color(bool _color);
		void load_paper(PrinterPaper _paper, bool _single_sheet);
		void unload_paper();
		void set_interpreter(mps_printer_interpreter _it);
		void init_interpreter();
		void interpret(uint8_t _data);
		void save_page_to_file(const PageBuffer &);

		std::pair<int,int> get_bitmap_px(double _inch_w, double _inch_h) const;
		std::pair<int,int> get_preview_px(double _inch_w, double _inch_h) const;

		void reset_head();
		void add_page();
		void clear_page(PageBuffer &_page);
		void carriage_return();
		void move_paper(int _pixels);
		void line_feed(bool _cr=true);
		void form_feed(bool _move=true);
		void set_form_length(uint16_t _pixels);
		void set_bof(uint16_t _pixels);

		std::tuple<int,int,int> get_bitmap_pos(int _x, int _y) const;
		int get_bitmap_byte(int _x, int _y) const;

		void print_dot(int _x, int _y, bool _bim=false);
		void put_ink(int _x, int _y, uint8_t _c);
		uint8_t add_color(int _buf, int _x, int _y, uint8_t _shade, uint32_t _shift);

		uint8_t get_pixel(int _buf, int _x, int _y) const;
		void put_pixel(int _buf, int _x, int _y, uint8_t _pix);

		uint16_t charset2chargen(uint8_t _input);
		uint16_t print_char(uint16_t _c);
		uint16_t print_char_italic(uint16_t _c, int _x, int _y);
		uint16_t print_char_draft(uint16_t _c, int _x, int _y);
		uint16_t print_char_nlq(uint16_t _c, int _x, int _y);

		bool is_printable(uint8_t _input);

		void led_on();
		void led_off();

		// Epson FX80 related interpreter
		void interpret_epson(uint8_t _input);
		uint16_t print_epson_bim(uint8_t _head);
		uint16_t print_epson_bim9(uint8_t _h, uint8_t _l);

		// IBM Proprinter related interpreter
		void interpret_ibmpp(uint8_t _input);

		// IBM Gaphics Printer related interpreter
		void interpret_ibmgp(uint8_t _input);

		// Debugging functions
		void print_marks();
		void print_palette();
};

#endif
