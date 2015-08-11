/*
 * 	Copyright (c) 2001-2013  The Bochs Project
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

#include "ibmulator.h"
#include "vga.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "hardware/memory.h"
#include "hardware/devices/pic.h"
#include "gui/gui.h"
#include <cstring>

#define VGA_IRQ 9

VGA g_vga;

// Only reference the array if the tile numbers are within the bounds
// of the array.  If out of bounds, do nothing.
#define SET_TILE_UPDATED(xtile, ytile, value)                   \
  do {                                                          \
    if(((xtile) < m_s.num_x_tiles) && ((ytile) < m_s.num_y_tiles)) \
      m_s.vga_tile_updated[(xtile)+(ytile)* m_s.num_x_tiles] = value; \
  } while (0)

// Only reference the array if the tile numbers are within the bounds
// of the array.  If out of bounds, return 0.
#define GET_TILE_UPDATED(xtile,ytile)                        \
  ((((xtile) < m_s.num_x_tiles) && ((ytile) < m_s.num_y_tiles))? \
     m_s.vga_tile_updated[(xtile)+(ytile)* m_s.num_x_tiles]      \
     : false)

static const uint16_t charmap_offset[8] = {
	0x0000, 0x4000, 0x8000, 0xc000,
	0x2000, 0x6000, 0xa000, 0xe000
};

static const uint8_t ccdat[16][4] = {
	{ 0x00, 0x00, 0x00, 0x00 },
	{ 0xff, 0x00, 0x00, 0x00 },
	{ 0x00, 0xff, 0x00, 0x00 },
	{ 0xff, 0xff, 0x00, 0x00 },
	{ 0x00, 0x00, 0xff, 0x00 },
	{ 0xff, 0x00, 0xff, 0x00 },
	{ 0x00, 0xff, 0xff, 0x00 },
	{ 0xff, 0xff, 0xff, 0x00 },
	{ 0x00, 0x00, 0x00, 0xff },
	{ 0xff, 0x00, 0x00, 0xff },
	{ 0x00, 0xff, 0x00, 0xff },
	{ 0xff, 0xff, 0x00, 0xff },
	{ 0x00, 0x00, 0xff, 0xff },
	{ 0xff, 0x00, 0xff, 0xff },
	{ 0x00, 0xff, 0xff, 0xff },
	{ 0xff, 0xff, 0xff, 0xff },
};


VGA::VGA()
{
	m_s.memory = NULL;
	m_s.vga_tile_updated = NULL;
	m_timer_id = NULL_TIMER_HANDLE;
}

VGA::~VGA()
{
	if(m_s.memory != NULL) {
		delete[] m_s.memory;
		m_s.memory = NULL;
	}
	if(m_s.vga_tile_updated != NULL) {
		delete[] m_s.vga_tile_updated;
		m_s.vga_tile_updated = NULL;
	}
}

void VGA::init()
{
	uint addr, i;

	g_machine.register_irq(VGA_IRQ, get_name());

	for(addr=0x03B4; addr<=0x03B5; addr++) {
		g_devices.register_read_handler(this, addr, 1);
		g_devices.register_write_handler(this, addr, 3);
	}

	for(addr=0x03BA; addr<=0x03BA; addr++) {
		g_devices.register_read_handler(this, addr, 1);
		g_devices.register_write_handler(this, addr, 3);
	}

	i = 0;
	uint8_t io_mask[16] = {3, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1};
	for(addr=0x03C0; addr<=0x03CF; addr++) {
		g_devices.register_read_handler(this, addr, io_mask[i++]);
		g_devices.register_write_handler(this, addr, 3);
	}

	for(addr=0x03D4; addr<=0x03D5; addr++) {
		g_devices.register_read_handler(this, addr, 3);
		g_devices.register_write_handler(this, addr, 3);
	}

	//CGA (Color Graphics Adapter) - MIRRORS OF 03D4/03D5
	for(addr=0x03D0; addr<=0x03D3; addr++) {
		if(addr & 1) {
			//only 3d1 and 3d3 are RW
			g_devices.register_read_handler(this, addr, 3);
		}
		g_devices.register_write_handler(this, addr, 3);
	}

	for(addr=0x03DA; addr<=0x03DA; addr++) {
		g_devices.register_read_handler(this, addr, 1);
		g_devices.register_write_handler(this, addr, 3);
	}

	m_timer_id = g_machine.register_timer(
		nullptr,
		0,
		false, //continuous
		false, //active
		get_name()
	);

	g_memory.register_trap(0xA0000, 0xBFFFF, 3, [this] (uint32_t addr, uint rw, uint16_t value, uint8_t len) {
		if(rw == 0) { //read
			PDEBUGF(LOG_V2, LOG_VGA, "%s[0x%05X] = 0x%04X\n", len==1?"b":"w", addr, value);
		} else { //write
			PDEBUGF(LOG_V2, LOG_VGA, "%s[0x%05X] := 0x%04X\n", len==1?"b":"w", addr, value);
		}
	});
}

void VGA::reset(uint type)
{
	delete[] m_s.memory;
	delete[] m_s.vga_tile_updated;

	memset(&m_s, 0, sizeof(m_s));

	m_s.vga_enabled = 1;
	m_s.blink_counter = 16;
	m_s.misc_output.color_emulation  = 1;
	m_s.misc_output.enable_ram  = 1;
	m_s.misc_output.horiz_sync_pol   = 1;
	m_s.misc_output.vert_sync_pol    = 1;

	m_s.attribute_ctrl.mode_ctrl.enable_line_graphics = 1;
	m_s.attribute_ctrl.video_status_mux = 0;
	m_s.line_offset = 80;
	m_s.line_compare = 1023;
	m_s.vertical_display_end = 399;

	m_s.attribute_ctrl.video_enabled = 1;
	m_s.attribute_ctrl.color_plane_enable = 0x0f;
	m_s.pel.dac_state = 0x01;
	m_s.pel.mask = 0xff;
	m_s.graphics_ctrl.memory_mapping = 2; // monochrome text mode

	m_s.sequencer.reset1 = 1;
	m_s.sequencer.reset2 = 1;
	m_s.sequencer.extended_mem = 1; // display mem greater than 64K
	m_s.sequencer.odd_even = 1; // use sequential addressing mode

	m_s.plane_shift = 16;
	m_s.dac_shift = 2;
	m_s.last_bpp = 8;
	m_s.htotal_usec = 31;
	m_s.vtotal_usec = 14285;

	m_s.max_xres = VGA_MAX_XRES;
	m_s.max_yres = VGA_MAX_YRES;

	m_s.memsize = 0x40000;
	m_s.planesize = 0x10000;
	m_s.memory = new uint8_t[m_s.memsize];

	memset(m_s.memory, 0, m_s.memsize);

	m_s.num_x_tiles = m_s.max_xres / VGA_X_TILESIZE + ((m_s.max_xres % VGA_X_TILESIZE) > 0);
	m_s.num_y_tiles = m_s.max_yres / VGA_Y_TILESIZE + ((m_s.max_yres % VGA_Y_TILESIZE) > 0);

	m_s.vga_tile_updated = new bool[m_s.num_x_tiles * m_s.num_y_tiles];

	for(uint y = 0; y < m_s.num_y_tiles; y++) {
		for(uint x = 0; x < m_s.num_x_tiles; x++) {
			SET_TILE_UPDATED(x, y, false);
		}
	}
}

void VGA::power_off()
{
	m_display->lock();
	m_display->clear_screen();
	m_display->unlock();
	g_gui.vga_update();
	g_machine.deactivate_timer(m_timer_id);
}

void VGA::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "saving state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	//vga_tile_updated
	h.name = "VGA tupd";
	h.data_size = sizeof(bool) * m_s.num_x_tiles * m_s.num_y_tiles;
	_state.write(m_s.vga_tile_updated,h);

	//memory
	h.name = "VGA mem";
	h.data_size = m_s.memsize;
	_state.write(m_s.memory,h);

	//display
	m_display->save_state(_state);
}

void VGA::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "restoring state\n");

	StateHeader h;

	//state object
	delete[] m_s.memory;
	delete[] m_s.vga_tile_updated;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	//vga_tile_updated
	m_s.vga_tile_updated = new bool[m_s.num_x_tiles * m_s.num_y_tiles];
	h.name = "VGA tupd";
	h.data_size = sizeof(bool) * m_s.num_x_tiles * m_s.num_y_tiles;
	_state.read(m_s.vga_tile_updated,h);

	//memory
	m_s.memory = new uint8_t[m_s.memsize];
	h.name = "VGA mem";
	h.data_size = m_s.memsize;
	_state.read(m_s.memory,h);

	//display
	m_display->restore_state(_state);

	double vfreq = 1000000.0 / m_s.vtotal_usec;
	if(vfreq >= 35.0 && vfreq <= 75.0) {
		g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::update,this));
		g_machine.activate_timer(m_timer_id, m_s.vtotal_usec, false);
	} else {
		g_machine.deactivate_timer(m_timer_id);
	}
}

void VGA::determine_screen_dimensions(uint *piHeight, uint *piWidth)
{
	int ai[0x20];
	int i,h,v;
	for(i = 0 ; i < 0x19 ; i++)
		ai[i] = m_s.CRTC.reg[i];

	h = (ai[1] + 1) * 8;
	v = (ai[18] | ((ai[7] & 0x02) << 7) | ((ai[7] & 0x40) << 3)) + 1;

	if(m_s.graphics_ctrl.shift_reg == 0) {
		*piWidth = 640;
		*piHeight = 480;

		if(m_s.CRTC.reg[6] == 0xBF) {
			if(m_s.CRTC.reg[23] == 0xA3 && m_s.CRTC.reg[20] == 0x40 && m_s.CRTC.reg[9] == 0x41)
			{
				*piWidth = 320;
				*piHeight = 240;
			} else {
				if(m_s.x_dotclockdiv2)
					h <<= 1;
				*piWidth = h;
				*piHeight = v;
			}
		} else if((h >= 640) && (v >= 400)) {
			*piWidth = h;
			*piHeight = v;
		}
	} else if(m_s.graphics_ctrl.shift_reg == 2) {
		*piWidth = h;
		*piHeight = v;
	} else {
		if(m_s.x_dotclockdiv2)
			h <<= 1;
		*piWidth = h;
		*piHeight = v;
	}
}

void VGA::calculate_retrace_timing()
{
	uint32_t dot_clock[4] = {25175000, 28322000, 25175000, 25175000};
	uint32_t htotal, hbstart, hbend, clock, cwidth, vtotal, vrstart, vrend;
	double hfreq, vfreq;

	htotal = m_s.CRTC.reg[0] + 5;
	htotal <<= m_s.x_dotclockdiv2;
	cwidth = ((m_s.sequencer.reg1 & 0x01) == 1) ? 8 : 9;
	clock = dot_clock[m_s.misc_output.clock_select];
	hfreq = clock / (htotal * cwidth);
	m_s.htotal_usec = 1000000 / hfreq;
	hbstart = m_s.CRTC.reg[2];
	m_s.hbstart_usec = (1000000 * hbstart * cwidth) / clock;
	hbend = (m_s.CRTC.reg[3] & 0x1f) + ((m_s.CRTC.reg[5] & 0x80) >> 2);
	hbend = hbstart + ((hbend - hbstart) & 0x3f);
	m_s.hbend_usec = (1000000 * hbend * cwidth) / clock;
	vtotal = m_s.CRTC.reg[6] + ((m_s.CRTC.reg[7] & 0x01) << 8) +
		   ((m_s.CRTC.reg[7] & 0x20) << 4) + 2;
	vrstart = m_s.CRTC.reg[16] + ((m_s.CRTC.reg[7] & 0x04) << 6) +
			((m_s.CRTC.reg[7] & 0x80) << 2);
	vrend = ((m_s.CRTC.reg[17] & 0x0f) - vrstart) & 0x0f;
	vrend = vrstart + vrend + 1;
	vfreq = hfreq / vtotal;
	m_s.vtotal_usec = 1000000.0 / vfreq;
	m_s.vblank_usec = m_s.htotal_usec * m_s.vertical_display_end;
	m_s.vbspan_usec = m_s.vtotal_usec - m_s.vblank_usec;
	m_s.vrstart_usec = m_s.htotal_usec * vrstart;
	m_s.vrend_usec = m_s.htotal_usec * vrend;
	m_s.vrspan_usec = m_s.vrend_usec - m_s.vrstart_usec;

	PDEBUGF(LOG_V1, LOG_VGA, "hfreq = %.1f kHz\n", ((double)hfreq / 1000));

	if(vfreq >= 35.0 && vfreq <= 75.0) {
		PINFOF(LOG_V1, LOG_VGA, "vfreq = %.2f Hz\n", vfreq);
		vertical_retrace();
	} else {
		g_machine.deactivate_timer(m_timer_id);
		PWARNF(LOG_VGA, "vfreq = %.2f Hz: out of range\n", vfreq);
	}
}

uint16_t VGA::read(uint16_t address, uint io_len)
{
	uint64_t display_usec, line_usec, now_usec;
	uint16_t ret16;
	uint8_t retval;

	if(io_len == 2) {
		ret16 = read(address, 1);
		ret16 |= (read(address+1, 1)) << 8;
		return ret16;
	}

	PDEBUGF(LOG_V2, LOG_VGA, "io read from 0x%04x\n", address);

	if((address >= 0x03b0) && (address <= 0x03bf) && (m_s.misc_output.color_emulation)) {
		return 0xff;
	}
	if((address >= 0x03d0) && (address <= 0x03df) && (!m_s.misc_output.color_emulation)) {
		return 0xff;
	}

	switch (address) {
		case 0x03ca: /* Feature Control ??? */
			return 0;
		case 0x03ba: /* Input Status 1 (monochrome emulation modes) */
		case 0x03da: /* Input Status 1 (color emulation modes) */
			// bit3: Vertical Retrace
			//       0 = display is in the display mode
			//       1 = display is in the vertical retrace mode
			// bit0: Display Enable
			//       0 = display is in the display mode
			//       1 = display is not in the display mode; either the
			//           horizontal or vertical retrace period is active

			retval = 0;
			now_usec = g_machine.get_virt_time_us();
			if(now_usec <= m_s.vblank_time_usec+m_s.vbspan_usec) {
				retval |= 0x01;
				if(now_usec <= m_s.vretrace_time_usec+m_s.vrspan_usec) {
					retval |= 0x08;
				}
				PDEBUGF(LOG_V2, LOG_VGA, "ISR1: %02X vert.\n", retval);
			} else {
				display_usec = now_usec - (m_s.vblank_time_usec+m_s.vbspan_usec);
				line_usec = display_usec % m_s.htotal_usec;
				if((line_usec >= m_s.hbstart_usec) && (line_usec <= m_s.hbend_usec)) {
					retval |= 0x01;
					PDEBUGF(LOG_V2, LOG_VGA, "ISR1: %02X horiz.\n", retval);
				} else {
					PDEBUGF(LOG_V2, LOG_VGA, "ISR1: 0 display\n");
				}
			}

			//this is to trick the VGA BIOS to think that the hardware is working:
			m_s.attribute_ctrl.video_feedback = !m_s.attribute_ctrl.video_feedback;
			retval |= m_s.attribute_ctrl.video_feedback << 4;

			/* reading this port resets the flip-flop to address mode */
			m_s.attribute_ctrl.flip_flop = false;
			return retval;

		case 0x03c0: /* */
			if(!m_s.attribute_ctrl.flip_flop) {
				//PDEBUGF(LOG_V1, LOG_VGA, "io read: 0x3c0: flip_flop = false"));
				retval = (m_s.attribute_ctrl.video_enabled << 5) | m_s.attribute_ctrl.address;
				return retval;
			} else {
				PDEBUGF(LOG_V0, LOG_VGA, "io read: 0x3c0: flip_flop != 0\n");
				return(0);
			}
			break;

		case 0x03c1: /* */
			switch (m_s.attribute_ctrl.address) {
				case 0x00: case 0x01: case 0x02: case 0x03:
				case 0x04: case 0x05: case 0x06: case 0x07:
				case 0x08: case 0x09: case 0x0a: case 0x0b:
				case 0x0c: case 0x0d: case 0x0e: case 0x0f:
					retval = m_s.attribute_ctrl.palette_reg[m_s.attribute_ctrl.address];
					return(retval);
					break;
				case 0x10: /* mode control register */
					retval =
					(m_s.attribute_ctrl.mode_ctrl.graphics_alpha << 0) |
					(m_s.attribute_ctrl.mode_ctrl.display_type << 1) |
					(m_s.attribute_ctrl.mode_ctrl.enable_line_graphics << 2) |
					(m_s.attribute_ctrl.mode_ctrl.blink_intensity << 3) |
					(m_s.attribute_ctrl.mode_ctrl.pixel_panning_compat << 5) |
					(m_s.attribute_ctrl.mode_ctrl.pixel_clock_select << 6) |
					(m_s.attribute_ctrl.mode_ctrl.internal_palette_size << 7);
					return(retval);
					break;
				case 0x11: /* overscan color register */
					return(m_s.attribute_ctrl.overscan_color);
					break;
				case 0x12: /* color plane enable */
					return(m_s.attribute_ctrl.color_plane_enable);
					break;
				case 0x13: /* horizontal PEL panning register */
					return(m_s.attribute_ctrl.horiz_pel_panning);
					break;
				case 0x14: /* color select register */
					return(m_s.attribute_ctrl.color_select);
					break;
				default:
					PDEBUGF(LOG_V1, LOG_VGA, "io read: 0x3c1: unknown register 0x%02x\n", m_s.attribute_ctrl.address);
					return(0);
			}
			break;

		case 0x03c2: /* Input Status 0 */
			PDEBUGF(LOG_V2, LOG_VGA, "io read 0x3c2: input status #0\n");
			retval =
				(m_s.pel.dac_sense << 4) |
				(m_s.CRTC.interrupt << 7);
			return retval;

		case 0x03c3: /* VGA Enable Register */
			return(m_s.vga_enabled);

		case 0x03c4: /* Sequencer Index Register */
			return(m_s.sequencer.index);

		case 0x03c5: /* Sequencer Registers 00..04 */
			switch (m_s.sequencer.index) {
				case 0: /* sequencer: reset */
					PDEBUGF(LOG_V2, LOG_VGA, "io read 0x3c5: sequencer reset\n");
					return(m_s.sequencer.reset1 | (m_s.sequencer.reset2<<1));
				case 1: /* sequencer: clocking mode */
					PDEBUGF(LOG_V2, LOG_VGA, "io read 0x3c5: sequencer clocking mode\n");
					return(m_s.sequencer.reg1);
				case 2: /* sequencer: map mask register */
					return(m_s.sequencer.map_mask);
				case 3: /* sequencer: character map select register */
					return(m_s.sequencer.char_map_select);
				case 4: /* sequencer: memory mode register */
					retval =
					(m_s.sequencer.extended_mem   << 1) |
					(m_s.sequencer.odd_even       << 2) |
					(m_s.sequencer.chain_four     << 3);
					return(retval);
				default:
					PDEBUGF(LOG_V2, LOG_VGA, "io read 0x3c5: index %u unhandled\n", m_s.sequencer.index);
					return(0);
			}
			break;

		case 0x03c6: /* PEL mask ??? */
			return(m_s.pel.mask);

		case 0x03c7: /* DAC state, read = 11b, write = 00b */
			return(m_s.pel.dac_state);

		case 0x03c8: /* PEL address write mode */
			return(m_s.pel.write_data_register);

		case 0x03c9: /* PEL Data Register, colors 00..FF */
			if(m_s.pel.dac_state == 0x03) {
				switch (m_s.pel.read_data_cycle) {
					case 0:
						retval = m_s.pel.data[m_s.pel.read_data_register].red;
						break;
					case 1:
						retval = m_s.pel.data[m_s.pel.read_data_register].green;
						break;
					case 2:
						retval = m_s.pel.data[m_s.pel.read_data_register].blue;
						break;
					default:
						retval = 0; // keep compiler happy
				}
				m_s.pel.read_data_cycle++;
				if(m_s.pel.read_data_cycle >= 3) {
					m_s.pel.read_data_cycle = 0;
					m_s.pel.read_data_register++;
				}
			} else {
				retval = 0x3f;
			}
			return(retval);

		case 0x03cc: /* Miscellaneous Output / Graphics 1 Position ??? */
			retval =
			((m_s.misc_output.color_emulation  & 0x01) << 0) |
			((m_s.misc_output.enable_ram       & 0x01) << 1) |
			((m_s.misc_output.clock_select     & 0x03) << 2) |
			((m_s.misc_output.select_high_bank & 0x01) << 5) |
			((m_s.misc_output.horiz_sync_pol   & 0x01) << 6) |
			((m_s.misc_output.vert_sync_pol    & 0x01) << 7);
			return(retval);

		case 0x03ce: /* Graphics Controller Index Register */
			return(m_s.graphics_ctrl.index);

		case 0x03cd: /* ??? */
			PDEBUGF(LOG_V2, LOG_VGA, "io read from 03cd\n");
			return(0x00);

		case 0x03cf: /* Graphics Controller Registers 00..08 */
			switch (m_s.graphics_ctrl.index) {
				case 0: /* Set/Reset */
					return(m_s.graphics_ctrl.set_reset);
				case 1: /* Enable Set/Reset */
					return(m_s.graphics_ctrl.enable_set_reset);
				case 2: /* Color Compare */
					return(m_s.graphics_ctrl.color_compare);
				case 3: /* Data Rotate */
					retval =
					((m_s.graphics_ctrl.raster_op & 0x03) << 3) |
					((m_s.graphics_ctrl.data_rotate & 0x07) << 0);
					return(retval);
				case 4: /* Read Map Select */
					return(m_s.graphics_ctrl.read_map_select);
				case 5: /* Mode */
					retval =
					((m_s.graphics_ctrl.shift_reg & 0x03) << 5) |
					((m_s.graphics_ctrl.odd_even & 0x01) << 4) |
					((m_s.graphics_ctrl.read_mode & 0x01) << 3) |
					((m_s.graphics_ctrl.write_mode & 0x03) << 0);

					if(m_s.graphics_ctrl.odd_even || m_s.graphics_ctrl.shift_reg) {
						PDEBUGF(LOG_V2, LOG_VGA, "io read 0x3cf: reg 05 = 0x%02x\n", retval);
					}
					return(retval);
				case 6: /* Miscellaneous */
					retval =
					((m_s.graphics_ctrl.memory_mapping & 0x03) << 2) |
					((m_s.graphics_ctrl.odd_even & 0x01) << 1) |
					((m_s.graphics_ctrl.graphics_alpha & 0x01) << 0);
					return(retval);
				case 7: /* Color Don't Care */
					return(m_s.graphics_ctrl.color_dont_care);
				case 8: /* Bit Mask */
					return(m_s.graphics_ctrl.bitmask);
				default: /* ??? */
					PDEBUGF(LOG_V2, LOG_VGA, "io read: 0x3cf: index %u unhandled\n", m_s.graphics_ctrl.index);
					return(0);
			}
			break;

		case 0x03d4: /* CRTC Index Register (color emulation modes) */
			return(m_s.CRTC.address);

		case 0x03b5: /* CRTC Registers (monochrome emulation modes) */
		case 0x03d5: /* CRTC Registers (color emulation modes) */
		case 0x03d1: /* CGA mirror port of 3d5 */
		case 0x03d3: /* CGA mirror port of 3d5 */
			if(m_s.CRTC.address > 0x18) {
				PDEBUGF(LOG_V2, LOG_VGA, "io read: invalid CRTC register 0x%02x\n", m_s.CRTC.address);
				return(0);
			}
			return(m_s.CRTC.reg[m_s.CRTC.address]);

		case 0x03b4: /* CRTC Index Register (monochrome emulation modes) */
		case 0x03cb: /* not sure but OpenBSD reads it a lot */
		default:
			PDEBUGF(LOG_V1, LOG_VGA, "io read from vga port 0x%04x\n", address);
			return(0); /* keep compiler happy */
	}
}

void VGA::write(uint16_t address, uint16_t value, uint io_len)
{
	uint8_t charmap1, charmap2, prev_memory_mapping;
	bool prev_video_enabled, prev_line_graphics, prev_int_pal_size, prev_graphics_alpha;
	bool needs_update = 0, charmap_update = 0;

	if(io_len == 1) {
		PDEBUGF(LOG_V2, LOG_VGA, "io write to 0x%04x = 0x%02x\n", address, value);
	}

	if(io_len == 2) {
		write(address, value & 0xff, 1);
		write(address+1, (value >> 8) & 0xff, 1);
		return;
	}

	if((address >= 0x03b0) && (address <= 0x03bf) && (m_s.misc_output.color_emulation))
		return;
	if((address >= 0x03d0) && (address <= 0x03df) && (m_s.misc_output.color_emulation==0))
		return;

	switch (address) {
		case 0x03ba: /* Feature Control (monochrome emulation modes) */
			PDEBUGF(LOG_V2, LOG_VGA, "io write 3ba: feature control: ignoring\n");
			break;

		case 0x03c0: /* Attribute Controller */
			if(!m_s.attribute_ctrl.flip_flop) { /* address mode */
				prev_video_enabled = m_s.attribute_ctrl.video_enabled;
				m_s.attribute_ctrl.video_enabled = (value >> 5) & 0x01;

				PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c0: video_enabled = %u\n", m_s.attribute_ctrl.video_enabled);
				/*
				Bit 5 must be set to 1 for normal operation of the attribute
				controller. This enables the video memory data to access
				the Palette registers. Bit 5 must be set to 0 when loading
				the Palette registers.
				*/
				if(!m_s.attribute_ctrl.video_enabled) {
					/*TODO is the clear required?
					beware: if in multithreaded mode flickering occurs.
					either an intermediate buffer should be used, or a differend
					threads synchronization mechanism.
					*/
					/*
					m_display->lock();
					m_display->clear_screen();
					m_display->unlock();
					*/
				} else if(!prev_video_enabled) {
					PDEBUGF(LOG_V2, LOG_VGA, "found enable transition\n");
					needs_update = 1;
				}
				value &= 0x1f; /* address = bits 0..4 */
				m_s.attribute_ctrl.address = value;
				switch (value) {
					case 0x00: case 0x01: case 0x02: case 0x03:
					case 0x04: case 0x05: case 0x06: case 0x07:
					case 0x08: case 0x09: case 0x0a: case 0x0b:
					case 0x0c: case 0x0d: case 0x0e: case 0x0f:
						break;
					default:
						PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c0: address mode reg=0x%02x\n", value);
				}
			} else { /* data-write mode */
				switch (m_s.attribute_ctrl.address) {
					case 0x00: case 0x01: case 0x02: case 0x03:
					case 0x04: case 0x05: case 0x06: case 0x07:
					case 0x08: case 0x09: case 0x0a: case 0x0b:
					case 0x0c: case 0x0d: case 0x0e: case 0x0f:
						if(value != m_s.attribute_ctrl.palette_reg[m_s.attribute_ctrl.address]) {
							m_s.attribute_ctrl.palette_reg[m_s.attribute_ctrl.address] = value;
							PDEBUGF(LOG_V2, LOG_VGA, "palette_reg[%u]=%u\n",m_s.attribute_ctrl.address,value);
							needs_update = 1;
						}
						break;
					case 0x10: // mode control register
						prev_line_graphics = m_s.attribute_ctrl.mode_ctrl.enable_line_graphics;
						prev_int_pal_size = m_s.attribute_ctrl.mode_ctrl.internal_palette_size;
						m_s.attribute_ctrl.mode_ctrl.graphics_alpha = (value >> 0) & 0x01;
						m_s.attribute_ctrl.mode_ctrl.display_type = (value >> 1) & 0x01;
						m_s.attribute_ctrl.mode_ctrl.enable_line_graphics = (value >> 2) & 0x01;
						m_s.attribute_ctrl.mode_ctrl.blink_intensity = (value >> 3) & 0x01;
						m_s.attribute_ctrl.mode_ctrl.pixel_panning_compat = (value >> 5) & 0x01;
						m_s.attribute_ctrl.mode_ctrl.pixel_clock_select = (value >> 6) & 0x01;
						m_s.attribute_ctrl.mode_ctrl.internal_palette_size = (value >> 7) & 0x01;
						if(((value >> 2) & 0x01) != prev_line_graphics) {
							charmap_update = 1;
						}
						if(((value >> 7) & 0x01) != prev_int_pal_size) {
							needs_update = 1;
						}
						PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c0: mode control: 0x%02x\n", value);
						break;
					case 0x11: // Overscan Color Register
						m_s.attribute_ctrl.overscan_color = (value & 0x3f);
						PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c0: overscan color = 0x%02x\n", value);
						break;
					case 0x12: // Color Plane Enable Register
						m_s.attribute_ctrl.color_plane_enable = (value & 0x0f);
						m_s.attribute_ctrl.video_status_mux = (value & 0x30);
						needs_update = 1;
						PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c0: color plane enable = 0x%02x\n", value);
						break;
					case 0x13: // Horizontal Pixel Panning Register
						m_s.attribute_ctrl.horiz_pel_panning = (value & 0x0f);
						needs_update = 1;
						PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c0: horiz pel panning = 0x%02x\n", value);
						break;
					case 0x14: // Color Select Register
						m_s.attribute_ctrl.color_select = (value & 0x0f);
						needs_update = 1;
						PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c0: color select = 0x%02x\n", m_s.attribute_ctrl.color_select);
						break;
					default:
						PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c0: data-write mode 0x%02x\n", m_s.attribute_ctrl.address);
				}
			}
			m_s.attribute_ctrl.flip_flop = !m_s.attribute_ctrl.flip_flop;
			break;

		case 0x03c2: // Miscellaneous Output Register
			m_s.misc_output.color_emulation  = (value >> 0) & 0x01;
			m_s.misc_output.enable_ram       = (value >> 1) & 0x01;
			m_s.misc_output.clock_select     = (value >> 2) & 0x03;
			m_s.misc_output.select_high_bank = (value >> 5) & 0x01;
			m_s.misc_output.horiz_sync_pol   = (value >> 6) & 0x01;
			m_s.misc_output.vert_sync_pol    = (value >> 7) & 0x01;

			PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c2:\n");
			PDEBUGF(LOG_V2, LOG_VGA, "  color_emulation (attempted) = %u\n", (value >> 0) & 0x01);
			PDEBUGF(LOG_V2, LOG_VGA, "  enable_ram = %u\n", m_s.misc_output.enable_ram);
			PDEBUGF(LOG_V2, LOG_VGA, "  clock_select = %u\n", m_s.misc_output.clock_select);
			PDEBUGF(LOG_V2, LOG_VGA, "  select_high_bank = %u\n", m_s.misc_output.select_high_bank);
			PDEBUGF(LOG_V2, LOG_VGA, "  horiz_sync_pol = %u\n", m_s.misc_output.horiz_sync_pol);
			PDEBUGF(LOG_V2, LOG_VGA, "  vert_sync_pol = %u\n", m_s.misc_output.vert_sync_pol);

			calculate_retrace_timing();
			break;

		case 0x03c3: // VGA enable
			// bit0: enables VGA display if set
			m_s.vga_enabled = value & 0x01;
			PDEBUGF(LOG_V2, LOG_VGA, "io write 0x03c3: VGA enable = %u\n", m_s.vga_enabled);
			break;

		case 0x03c4: /* Sequencer Index Register */
			if(value > 4) {
				PDEBUGF(LOG_V2, LOG_VGA, "io write 0x3c4: value > 4\n");
			}
			m_s.sequencer.index = value;
			break;

		case 0x03c5: /* Sequencer Registers 00..04 */
			switch (m_s.sequencer.index) {
				case 0: /* sequencer: reset */
					PDEBUGF(LOG_V2, LOG_VGA, "write 0x3c5: sequencer reset: value=0x%02x\n", value);
					if(m_s.sequencer.reset1 && ((value & 0x01) == 0)) {
						m_s.sequencer.char_map_select = 0;
						m_s.charmap_address = 0;
						charmap_update = 1;
					}
					m_s.sequencer.reset1 = (value >> 0) & 0x01;
					m_s.sequencer.reset2 = (value >> 1) & 0x01;
					break;
				case 1: /* sequencer: clocking mode */
					if((value ^ m_s.sequencer.reg1) & 0x29) {
						m_s.x_dotclockdiv2 = ((value & 0x08) > 0);
						m_s.sequencer.clear_screen = ((value & 0x20) > 0);
						calculate_retrace_timing();
						needs_update = 1;
					}
					m_s.sequencer.reg1 = value & 0x3d;
					break;
				case 2: /* sequencer: map mask register */
					m_s.sequencer.map_mask = (value & 0x0f);
					break;
				case 3: /* sequencer: character map select register */
					m_s.sequencer.char_map_select = value & 0x3f;
					charmap1 = value & 0x13;
					if(charmap1 > 3)
						charmap1 = (charmap1 & 3) + 4;
					charmap2 = (value & 0x2C) >> 2;
					if(charmap2 > 3)
						charmap2 = (charmap2 & 3) + 4;
					if(m_s.CRTC.reg[0x09] > 0) {
						m_s.charmap_address = charmap_offset[charmap1];
						charmap_update = 1;
					}
					if(charmap2 != charmap1) {
						PDEBUGF(LOG_V1, LOG_VGA, "char map select: map #2 in block #%d unused\n", charmap2);
					}
					break;
				case 4: /* sequencer: memory mode register */
					m_s.sequencer.extended_mem   = (value >> 1) & 0x01;
					m_s.sequencer.odd_even       = (value >> 2) & 0x01;
					m_s.sequencer.chain_four     = (value >> 3) & 0x01;

					PDEBUGF(LOG_V2, LOG_VGA, "io write 0x3c5: memory mode:\n");
					PDEBUGF(LOG_V2, LOG_VGA, " extended_mem = %u, ", m_s.sequencer.extended_mem);
					PDEBUGF(LOG_V2, LOG_VGA, " odd_even = %u, ", m_s.sequencer.odd_even);
					PDEBUGF(LOG_V2, LOG_VGA, " chain_four = %u\n", m_s.sequencer.chain_four);

					break;
				default:
					PDEBUGF(LOG_V2, LOG_VGA, "io write 0x3c5: index 0x%02x unhandled\n", m_s.sequencer.index);
					break;
			}
			break;

		case 0x03c6: /* PEL mask */
			m_s.pel.mask = value;
			if(m_s.pel.mask != 0xff) {
				PDEBUGF(LOG_V2, LOG_VGA, "io write 0x3c6: PEL mask=0x%02x != 0xFF\n", value);
			}
			// m_s.pel.mask should be and'd with final value before
			// indexing into color register m_s.pel.data[]
			break;

		case 0x03c7: // PEL address, read mode
			m_s.pel.read_data_register = value;
			m_s.pel.read_data_cycle    = 0;
			m_s.pel.dac_state = 0x03;
			break;

		case 0x03c8: /* PEL address write mode */
			m_s.pel.write_data_register = value;
			m_s.pel.write_data_cycle    = 0;
			m_s.pel.dac_state = 0x00;
			break;

		case 0x03c9: /* PEL Data Register, colors 00..FF */
			switch (m_s.pel.write_data_cycle) {
				case 0:
					m_s.pel.data[m_s.pel.write_data_register].red = value;
					break;
				case 1:
					m_s.pel.data[m_s.pel.write_data_register].green = value;
					break;
				case 2: {
					m_s.pel.data[m_s.pel.write_data_register].blue = value;

					uint8_t r = m_s.pel.data[m_s.pel.write_data_register].red;
					uint8_t g = m_s.pel.data[m_s.pel.write_data_register].green;
					uint8_t b = m_s.pel.data[m_s.pel.write_data_register].blue;
					uint8_t sense = r &	g &	b;
					//dac sensing value for color monitor:
					m_s.pel.dac_sense = (sense & 0x10);
					r <<= m_s.dac_shift;
					g <<= m_s.dac_shift;
					b <<= m_s.dac_shift;
					m_display->lock();
					needs_update = m_display->palette_change(m_s.pel.write_data_register,r,g,b);
					m_display->unlock();
					PDEBUGF(LOG_V2, LOG_VGA, "palette[%u] = (%u,%u,%u)\n",
							m_s.pel.write_data_register, r,g,b);
					break;
				}
			}

			m_s.pel.write_data_cycle++;
			if(m_s.pel.write_data_cycle >= 3) {
				//PDEBUGF(LOG_V1, LOG_VGA, "m_s.pel.data[%u] {r=%u, g=%u, b=%u}",
				//  (uint) m_s.pel.write_data_register,
				//  (uint) m_s.pel.data[m_s.pel.write_data_register].red,
				//  (uint) m_s.pel.data[m_s.pel.write_data_register].green,
				//  (uint) m_s.pel.data[m_s.pel.write_data_register].blue);
				m_s.pel.write_data_cycle = 0;
				m_s.pel.write_data_register++;
			}
			break;

		case 0x03ca: /* Graphics 2 Position (EGA) */
			// ignore, EGA only???
			break;

		case 0x03cc: /* Graphics 1 Position (EGA) */
			// ignore, EGA only???
			break;

		case 0x03cd: /* ??? */
			PDEBUGF(LOG_V2, LOG_VGA, "io write to 0x03cd = 0x%02x\n", value);
			break;

		case 0x03ce: /* Graphics Controller Index Register */
			if(value > 0x08) { /* ??? */
				PDEBUGF(LOG_V2, LOG_VGA, "io write: 0x03ce: value > 8\n");
			}
			m_s.graphics_ctrl.index = value;
			break;

		case 0x03cf: /* Graphics Controller Registers 00..08 */
			switch (m_s.graphics_ctrl.index) {
				case 0: /* Set/Reset */
					m_s.graphics_ctrl.set_reset = value & 0x0f;
					break;
				case 1: /* Enable Set/Reset */
					m_s.graphics_ctrl.enable_set_reset = value & 0x0f;
					break;
				case 2: /* Color Compare */
					m_s.graphics_ctrl.color_compare = value & 0x0f;
					break;
				case 3: /* Data Rotate */
					m_s.graphics_ctrl.data_rotate = value & 0x07;
					m_s.graphics_ctrl.raster_op    = (value >> 3) & 0x03;
					break;
				case 4: /* Read Map Select */
					m_s.graphics_ctrl.read_map_select = value & 0x03;
					PDEBUGF(LOG_V2, LOG_VGA, "io write to 0x03cf = 0x%02x (RMS)\n", value);
					break;
				case 5: /* Mode */
					m_s.graphics_ctrl.write_mode        = value & 0x03;
					m_s.graphics_ctrl.read_mode         = (value >> 3) & 0x01;
					m_s.graphics_ctrl.odd_even          = (value >> 4) & 0x01;
					m_s.graphics_ctrl.shift_reg         = (value >> 5) & 0x03;

					if(m_s.graphics_ctrl.odd_even) {
						PDEBUGF(LOG_V2, LOG_VGA, "io write: 0x03cf: mode reg: value = 0x%02x\n", value);
					}
					if(m_s.graphics_ctrl.shift_reg) {
						PDEBUGF(LOG_V2, LOG_VGA, "io write: 0x03cf: mode reg: value = 0x%02x", value);
					}
					break;
				case 6: /* Miscellaneous */
					prev_graphics_alpha = m_s.graphics_ctrl.graphics_alpha;
					// prev_chain_odd_even = m_s.graphics_ctrl.chain_odd_even;
					prev_memory_mapping = m_s.graphics_ctrl.memory_mapping;

					m_s.graphics_ctrl.graphics_alpha = value & 0x01;
					m_s.graphics_ctrl.chain_odd_even = (value >> 1) & 0x01;
					m_s.graphics_ctrl.memory_mapping = (value >> 2) & 0x03;

					PDEBUGF(LOG_V2, LOG_VGA, "memory_mapping set to %u\n", m_s.graphics_ctrl.memory_mapping);
					PDEBUGF(LOG_V2, LOG_VGA, "graphics mode set to %u\n", m_s.graphics_ctrl.graphics_alpha);
					PDEBUGF(LOG_V2, LOG_VGA, "odd_even mode set to %u\n", m_s.graphics_ctrl.odd_even);
					PDEBUGF(LOG_V2, LOG_VGA, "io write: 0x3cf: misc reg: value = 0x%02x\n", value);

					if(prev_memory_mapping != m_s.graphics_ctrl.memory_mapping)
						needs_update = 1;
					if(prev_graphics_alpha != m_s.graphics_ctrl.graphics_alpha) {
						needs_update = 1;
						m_s.last_yres = 0;
					}
					break;
				case 7: /* Color Don't Care */
					m_s.graphics_ctrl.color_dont_care = value & 0x0f;
					break;
				case 8: /* Bit Mask */
					m_s.graphics_ctrl.bitmask = value;
					break;
				default: /* ??? */
					PDEBUGF(LOG_V2, LOG_VGA, "io write: 0x03cf: index %u unhandled\n", m_s.graphics_ctrl.index);
					break;
			}
			break;

		case 0x03b4: /* CRTC Index Register (monochrome emulation modes) */
		case 0x03d4: /* CRTC Index Register (color emulation modes) */
		case 0x03d0: /* CGA mirror port of 3d4 */
		case 0x03d2: /* CGA mirror port of 3d4 */
			m_s.CRTC.address = value & 0x7f;
			if(m_s.CRTC.address > 0x18) {
				PDEBUGF(LOG_V2, LOG_VGA, "write: invalid CRTC register 0x%02x selected\n", m_s.CRTC.address);
			}
			break;

		case 0x03b5: /* CRTC Registers (monochrome emulation modes) */
		case 0x03d5: /* CRTC Registers (color emulation modes) */
		case 0x03d1: /* CGA mirror port of 3d5 */
		case 0x03d3: /* CGA mirror port of 3d5 */
			if(m_s.CRTC.address > 0x18) {
				PDEBUGF(LOG_V2, LOG_VGA, "write: invalid CRTC register 0x%02x ignored\n", m_s.CRTC.address);
				return;
			}
			if(m_s.CRTC.write_protect && (m_s.CRTC.address < 0x08)) {
				if(m_s.CRTC.address == 0x07) {
					m_s.CRTC.reg[m_s.CRTC.address] &= ~0x10;
					m_s.CRTC.reg[m_s.CRTC.address] |= (value & 0x10);
					m_s.line_compare &= 0x2ff;
					if(m_s.CRTC.reg[0x07] & 0x10) m_s.line_compare |= 0x100;
					needs_update = 1;
					break;
				} else {
					return;
				}
			}
			if(value != m_s.CRTC.reg[m_s.CRTC.address]) {
				uint8_t oldvalue = m_s.CRTC.reg[m_s.CRTC.address];
				m_s.CRTC.reg[m_s.CRTC.address] = value;
				switch (m_s.CRTC.address) {
					case 0x00:
					case 0x02:
					case 0x03:
					case 0x05:
					case 0x06:
					case 0x10:
						calculate_retrace_timing();
						break;
					case 0x07:
						m_s.vertical_display_end &= 0xff;
						if(m_s.CRTC.reg[0x07] & 0x02) m_s.vertical_display_end |= 0x100;
						if(m_s.CRTC.reg[0x07] & 0x40) m_s.vertical_display_end |= 0x200;
						m_s.line_compare &= 0x2ff;
						if(m_s.CRTC.reg[0x07] & 0x10) m_s.line_compare |= 0x100;
						calculate_retrace_timing();
						needs_update = 1;
						break;
					case 0x08:
						// Vertical pel panning change
						needs_update = 1;
						break;
					case 0x09:
						m_s.y_doublescan = ((value & 0x9f) > 0);
						m_s.line_compare &= 0x1ff;
						if(m_s.CRTC.reg[0x09] & 0x40) m_s.line_compare |= 0x200;
						charmap_update = 1;
						needs_update = 1;
						break;
					case 0x0A:
					case 0x0B:
					case 0x0E:
					case 0x0F:
						// Cursor size / location change
						m_s.vga_mem_updated = 1;
						break;
					case 0x0C:
					case 0x0D:
						// Start address change
						if(m_s.graphics_ctrl.graphics_alpha) {
							needs_update = 1;
						} else {
							m_s.vga_mem_updated = 1;
						}
						PDEBUGF(LOG_V2, LOG_VGA, "start address 0x%02X=%02X\n",
								m_s.CRTC.address, value);
						break;
					case 0x11:
						if(!(m_s.CRTC.reg[0x11] & 0x10)) {
							lower_interrupt();
						}
						m_s.CRTC.write_protect = ((m_s.CRTC.reg[0x11] & 0x80) > 0);
						if((oldvalue&0xF) != (m_s.CRTC.reg[0x11]&0xF)) {
							calculate_retrace_timing();
						}
						break;
					case 0x12:
						m_s.vertical_display_end &= 0x300;
						m_s.vertical_display_end |= m_s.CRTC.reg[0x12];
						calculate_retrace_timing();
						break;
					case 0x13:
					case 0x14:
					case 0x17:
						// Line offset change
						m_s.line_offset = m_s.CRTC.reg[0x13] << 1;
						if(m_s.CRTC.reg[0x14] & 0x40) m_s.line_offset <<= 2;
						else if((m_s.CRTC.reg[0x17] & 0x40) == 0) m_s.line_offset <<= 1;
						needs_update = 1;
						break;
					case 0x18:
						m_s.line_compare &= 0x300;
						m_s.line_compare |= m_s.CRTC.reg[0x18];
						needs_update = 1;
						break;
				}
			}
			break;

		case 0x03da: /* Feature Control (color emulation modes) */
			PDEBUGF(LOG_V2, LOG_VGA, "io write: 0x03da: ignoring: feature ctrl & vert sync\n");
			break;

		case 0x03c1: /* */
		default:
			PDEBUGF(LOG_V0, LOG_VGA, "unsupported io write to port 0x%04x, val=0x%02x\n", address, value);
			break;
	}

	if(charmap_update) {
		m_display->lock();
		m_display->set_text_charmap(& m_s.memory[0x20000 + m_s.charmap_address]);
		m_display->unlock();
		m_s.vga_mem_updated = 1;
	}
	if(needs_update) {
		// Mark all video as updated so the changes will go through
		redraw_area(0, 0, m_s.last_xres, m_s.last_yres);
	}
}

uint8_t VGA::get_vga_pixel(uint16_t x, uint16_t y, uint16_t saddr, uint16_t lc,
		bool bs, uint8_t **plane)
{
	uint8_t attribute, bit_no, palette_reg_val, DAC_regno;
	uint32_t byte_offset;
	int pan = m_s.attribute_ctrl.horiz_pel_panning;
	if(pan>=8) { pan = 0; }
	if(m_s.x_dotclockdiv2) {
		x >>= 1;
	}
	x += pan;
	bit_no = 7 - (x % 8);
	if(y > lc) {
		byte_offset = x / 8 + ((y - lc - 1) * m_s.line_offset);
	} else {
		byte_offset = saddr + x / 8 + (y * m_s.line_offset);
	}
	attribute =
		(((plane[0][byte_offset%m_s.planesize] >> bit_no) & 0x01) << 0) |
		(((plane[1][byte_offset%m_s.planesize] >> bit_no) & 0x01) << 1) |
		(((plane[2][byte_offset%m_s.planesize] >> bit_no) & 0x01) << 2) |
		(((plane[3][byte_offset%m_s.planesize] >> bit_no) & 0x01) << 3);

	attribute &= m_s.attribute_ctrl.color_plane_enable;
	// undocumented feature ???: colors 0..7 high intensity, colors 8..15 blinking
	if(m_s.attribute_ctrl.mode_ctrl.blink_intensity) {
		if(bs) {
			attribute |= 0x08;
		} else {
			attribute ^= 0x08;
		}
	}
	palette_reg_val = m_s.attribute_ctrl.palette_reg[attribute];
	if(m_s.attribute_ctrl.mode_ctrl.internal_palette_size) {
		// use 4 lower bits from palette register
		// use 4 higher bits from color select register
		// 16 banks of 16-color registers
		DAC_regno = (palette_reg_val & 0x0f) | (m_s.attribute_ctrl.color_select << 4);
	} else {
		// use 6 lower bits from palette register
		// use 2 higher bits from color select register
		// 4 banks of 64-color registers
		DAC_regno = (palette_reg_val & 0x3f) | ((m_s.attribute_ctrl.color_select & 0x0c) << 4);
	}
	// DAC_regno &= video DAC mask register ???
	return DAC_regno;
}

void VGA::raise_interrupt()
{
	if(m_s.CRTC.reg[0x11] & 0x10) {
		PDEBUGF(LOG_V2, LOG_VGA, "raising IRQ %d\n", VGA_IRQ);
		g_pic.raise_irq(VGA_IRQ);
		m_s.CRTC.interrupt = true;
	}
}

void VGA::lower_interrupt()
{
	g_pic.lower_irq(VGA_IRQ);
	m_s.CRTC.interrupt = false;
}

bool VGA::skip_update()
{
	/* skip screen update when vga/video is disabled or the sequencer is in reset mode */
	if(!m_s.vga_enabled || !m_s.attribute_ctrl.video_enabled
	  || !m_s.sequencer.reset2 || !m_s.sequencer.reset1
	  || (m_s.sequencer.reg1 & 0x20))
	{
		PDEBUGF(LOG_V2, LOG_VGA, "vga_enabled=%u,",m_s.vga_enabled);
		PDEBUGF(LOG_V2, LOG_VGA, "video_enabled=%u,",m_s.attribute_ctrl.video_enabled);
		PDEBUGF(LOG_V2, LOG_VGA, "reset1=%u,",m_s.sequencer.reset1);
		PDEBUGF(LOG_V2, LOG_VGA, "reset2=%u,",m_s.sequencer.reset2);
		PDEBUGF(LOG_V2, LOG_VGA, "reg1=%u ",bool(m_s.sequencer.reg1 & 0x20));
		return true;
	}

	//don't skip
	return false;
}

void VGA::update()
{
	//this is "vertical blank start"

	uint iHeight, iWidth;
	static uint cs_counter = 1; //cursor blink counter
	static bool cs_visible = false;
	bool cs_toggle = false;
	bool skip = skip_update();

	m_s.vblank_time_usec = g_machine.get_virt_time_us();

	//next is the "vertical retrace start"
	uint64_t vrdist = m_s.vrstart_usec - m_s.vblank_usec;
	g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::vertical_retrace,this));
	g_machine.activate_timer(m_timer_id, vrdist, false);

	cs_counter--;
	/* no screen update necessary */
	if((!m_s.vga_mem_updated) && (cs_counter > 0)) {
		return;
	}

	if(cs_counter == 0) {
		cs_counter = m_s.blink_counter;
		if((!m_s.graphics_ctrl.graphics_alpha) || (m_s.attribute_ctrl.mode_ctrl.blink_intensity)) {
			cs_toggle = true;
			cs_visible = !cs_visible;
		} else {
			if(!m_s.vga_mem_updated)
				return;
			cs_toggle = false;
			cs_visible = false;
		}
	}

	// fields that effect the way video memory is serialized into screen output:
	// GRAPHICS CONTROLLER:
	//   m_s.graphics_ctrl.shift_reg:
	//     0: output data in standard VGA format or CGA-compatible 640x200 2 color
	//        graphics mode (mode 6)
	//     1: output data in CGA-compatible 320x200 4 color graphics mode
	//        (modes 4 & 5)
	//     2: output data 8 bits at a time from the 4 bit planes
	//        (mode 13 and variants like modeX)

	// if(m_s.vga_mem_updated==0 || m_s.attribute_ctrl.video_enabled == 0)

	m_display->lock();

	if(m_s.graphics_ctrl.graphics_alpha) {

		/*****
		 * Graphics mode
		 */

		uint8_t color;
		uint16_t x, y;
		uint bit_no, r, c;
		uint byte_offset;
		uint xc, yc, xti, yti, pan = m_s.attribute_ctrl.horiz_pel_panning;
		const uint mode13_pan_values[8] = { 0,0,1,0,2,0,3,0 };
		if(pan>=8) { pan = 0; }

		determine_screen_dimensions(&iHeight, &iWidth);
		if((iWidth != m_s.last_xres) || (iHeight != m_s.last_yres) || (m_s.last_bpp > 8))
		{
			m_display->dimension_update(iWidth, iHeight);
			m_s.last_xres = iWidth;
			m_s.last_yres = iHeight;
			m_s.last_bpp = 8;
		}

		/* handle clear screen request from the sequencer */
		if(m_s.sequencer.clear_screen) {
			m_display->clear_screen();
			m_s.sequencer.clear_screen = false;
		}

		if(skip) {
			m_display->unlock();
			return;
		}

		PDEBUGF(LOG_V2, LOG_VGA, "graphical update\n");

		switch (m_s.graphics_ctrl.shift_reg) {
			case 0: // interleaved shift
				uint8_t attribute, palette_reg_val, DAC_regno;
				uint16_t line_compare;
				uint8_t *plane[4];

				if((m_s.CRTC.reg[0x17] & 1) == 0) { // CGA 640x200x2

					for(yc=0, yti=0; yc<iHeight; yc+=VGA_Y_TILESIZE, yti++) {
						for(xc=0, xti=0; xc<iWidth; xc+=VGA_X_TILESIZE, xti++) {
							if(GET_TILE_UPDATED (xti, yti)) {
								for(r=0; r<VGA_Y_TILESIZE; r++) {
									y = yc + r;
									if(m_s.y_doublescan) y >>= 1;
									for(c=0; c<VGA_X_TILESIZE; c++) {

										x = xc + c + pan;
										/* 0 or 0x2000 */
										byte_offset = m_s.CRTC.start_address + ((y & 1) << 13);
										/* to the start of the line */
										byte_offset += (320 / 4) * (y / 2);
										/* to the byte start */
										byte_offset += (x / 8);

										bit_no = 7 - (x % 8);
										palette_reg_val = (((m_s.memory[byte_offset%m_s.memsize]) >> bit_no) & 1);
										DAC_regno = m_s.attribute_ctrl.palette_reg[palette_reg_val];
										m_s.tile[r*VGA_X_TILESIZE + c] = DAC_regno;
									}
								}
								SET_TILE_UPDATED(xti, yti, false);
								m_display->graphics_tile_update(m_s.tile, xc, yc);
							}
						}
					}
				} else {
					// output data in serial fashion with each display plane
					// output on its associated serial output.  Standard EGA/VGA format

					plane[0] = &m_s.memory[0 << m_s.plane_shift];
					plane[1] = &m_s.memory[1 << m_s.plane_shift];
					plane[2] = &m_s.memory[2 << m_s.plane_shift];
					plane[3] = &m_s.memory[3 << m_s.plane_shift];
					line_compare = m_s.line_compare;
					if(m_s.y_doublescan) line_compare >>= 1;

					for(yc=0, yti=0; yc<iHeight; yc+=VGA_Y_TILESIZE, yti++) {
						for(xc=0, xti=0; xc<iWidth; xc+=VGA_X_TILESIZE, xti++) {
							if(cs_toggle || GET_TILE_UPDATED(xti, yti)) {
								for(r=0; r<VGA_Y_TILESIZE; r++) {
									y = yc + r;
									if(m_s.y_doublescan) y >>= 1;
									for(c=0; c<VGA_X_TILESIZE; c++) {
										x = xc + c;
										m_s.tile[r*VGA_X_TILESIZE + c] =
												get_vga_pixel(x, y, m_s.CRTC.start_address,
														line_compare, cs_visible, plane);
									}
								}
								SET_TILE_UPDATED(xti, yti, false);
								m_display->graphics_tile_update(m_s.tile, xc, yc);
							}
						}
					}
				}
				break; // case 0

			case 1:
				// output the data in a CGA-compatible 320x200 4 color graphics
				// mode.  (planar shift, modes 4 & 5)

				/* CGA 320x200x4 start */

				for(yc=0, yti=0; yc<iHeight; yc+=VGA_Y_TILESIZE, yti++) {
					for(xc=0, xti=0; xc<iWidth; xc+=VGA_X_TILESIZE, xti++) {
						if(GET_TILE_UPDATED (xti, yti)) {
							for(r=0; r<VGA_Y_TILESIZE; r++) {
								y = yc + r;
								if(m_s.y_doublescan) y >>= 1;
								for(c=0; c<VGA_X_TILESIZE; c++) {

									x = xc + c;
									if(m_s.x_dotclockdiv2) { x >>= 1; }
									x += pan;
									/* 0 or 0x2000 */
									byte_offset = m_s.CRTC.start_address + ((y & 1) << 13);
									/* to the start of the line */
									byte_offset += (320 / 4) * (y / 2);
									/* to the byte start */
									byte_offset += (x / 4);

									attribute = 6 - 2*(x % 4);
									palette_reg_val = (m_s.memory[byte_offset%m_s.memsize]) >> attribute;
									palette_reg_val &= 3;
									DAC_regno = m_s.attribute_ctrl.palette_reg[palette_reg_val];
									m_s.tile[r*VGA_X_TILESIZE + c] = DAC_regno;
								}
							}
							SET_TILE_UPDATED(xti, yti, false);
							m_display->graphics_tile_update(m_s.tile, xc, yc);
						}
					}
				}
				/* CGA 320x200x4 end */

				break; // case 1

			case 2:
				// output the data eight bits at a time from the 4 bit plane
				// (format for VGA mode 13 hex)
			case 3:
				// FIXME: is this really the same ???

				pan = mode13_pan_values[pan];
				if(m_s.CRTC.reg[0x14] & 0x40) { // DW set: doubleword mode
					uint pixely, pixelx, plane;
					m_s.CRTC.start_address *= 4;
					if(m_s.misc_output.select_high_bank != 1) {
						PERRF(LOG_VGA, "update: select_high_bank != 1\n");
					}
					for(yc=0, yti=0; yc<iHeight; yc+=VGA_Y_TILESIZE, yti++) {
						for(xc=0, xti=0; xc<iWidth; xc+=VGA_X_TILESIZE, xti++) {
							if(GET_TILE_UPDATED (xti, yti)) {
								for(r=0; r<VGA_Y_TILESIZE; r++) {
									pixely = yc + r;
									if(m_s.y_doublescan) pixely >>= 1;
									for(c=0; c<VGA_X_TILESIZE; c++) {
										pixelx = ((xc + c) >> 1) + pan;
										plane  = (pixelx % 4);
										byte_offset = (plane * 65536) +
												(pixely * m_s.line_offset)
												+ (pixelx & ~0x03);
										color = m_s.memory[(m_s.CRTC.start_address + byte_offset)%m_s.memsize];
										m_s.tile[r*VGA_X_TILESIZE + c] = color;
									}
								}
								SET_TILE_UPDATED(xti, yti, false);
								m_display->graphics_tile_update(m_s.tile, xc, yc);
							}
						}
					}
				} else if(m_s.CRTC.reg[0x17] & 0x40) { // B/W set: byte mode, modeX
					uint pixely, pixelx, plane;

					for(yc=0, yti=0; yc<iHeight; yc+=VGA_Y_TILESIZE, yti++) {
						for(xc=0, xti=0; xc<iWidth; xc+=VGA_X_TILESIZE, xti++) {
							if(GET_TILE_UPDATED (xti, yti)) {
								for(r=0; r<VGA_Y_TILESIZE; r++) {
									pixely = yc + r;
									if(m_s.y_doublescan) pixely >>= 1;
									for(c=0; c<VGA_X_TILESIZE; c++) {
										pixelx = ((xc + c) >> 1) + pan;
										plane  = (pixelx % 4);
										byte_offset = (plane * 65536) +
												(pixely * m_s.line_offset)
												+ (pixelx >> 2);
										color = m_s.memory[(m_s.CRTC.start_address + byte_offset)%m_s.memsize];
										m_s.tile[r*VGA_X_TILESIZE + c] = color;
									}
								}
								SET_TILE_UPDATED(xti, yti, false);
								m_display->graphics_tile_update(m_s.tile, xc, yc);
							}
						}
					}
				} else { // word mode
					uint pixely, pixelx, plane;
					m_s.CRTC.start_address *= 2;
					for(yc=0, yti=0; yc<iHeight; yc+=VGA_Y_TILESIZE, yti++) {
						for(xc=0, xti=0; xc<iWidth; xc+=VGA_X_TILESIZE, xti++) {
							if(GET_TILE_UPDATED (xti, yti)) {
								for(r=0; r<VGA_Y_TILESIZE; r++) {
									pixely = yc + r;
									if(m_s.y_doublescan) pixely >>= 1;
									for(c=0; c<VGA_X_TILESIZE; c++) {
										pixelx = ((xc + c) >> 1) + pan;
										plane  = (pixelx % 4);
										byte_offset = (plane * 65536) +
												(pixely * m_s.line_offset)
												+ ((pixelx >> 1) & ~0x01);
										color = m_s.memory[(m_s.CRTC.start_address + byte_offset)%m_s.memsize];
										m_s.tile[r*VGA_X_TILESIZE + c] = color;
									}
								}
								SET_TILE_UPDATED(xti, yti, false);
								m_display->graphics_tile_update(m_s.tile, xc, yc);
							}
						}
					}
				}
				break; // case 2

			default:
				PERRF(LOG_VGA, "update: shift_reg == %u\n", m_s.graphics_ctrl.shift_reg);
				break;
		}

		m_s.vga_mem_updated = 0;

	} else {

		/****
		 * text mode
		 */

		uint start_address;
		uint cursor_address, cursor_x, cursor_y;
		TextModeInfo tm_info;
		uint VDE, cols, rows, cWidth;
		uint8_t MSL;

		tm_info.start_address = 2*((m_s.CRTC.reg[12] << 8) + m_s.CRTC.reg[13]);
		tm_info.cs_start = m_s.CRTC.reg[0x0a] & 0x3f;
		if(!cs_visible) {
			tm_info.cs_start |= 0x20;
		}
		tm_info.cs_end = m_s.CRTC.reg[0x0b] & 0x1f;
		tm_info.line_offset = m_s.CRTC.reg[0x13] << 2;
		tm_info.line_compare = m_s.line_compare;
		tm_info.h_panning = m_s.attribute_ctrl.horiz_pel_panning & 0x0f;
		tm_info.v_panning = m_s.CRTC.reg[0x08] & 0x1f;
		tm_info.line_graphics = m_s.attribute_ctrl.mode_ctrl.enable_line_graphics;
		tm_info.split_hpanning = m_s.attribute_ctrl.mode_ctrl.pixel_panning_compat;
		tm_info.blink_flags = 0;
		if(m_s.attribute_ctrl.mode_ctrl.blink_intensity) {
			tm_info.blink_flags |= TEXT_BLINK_MODE;
			if(cs_toggle)
				tm_info.blink_flags |= TEXT_BLINK_TOGGLE;
			if(cs_visible)
				tm_info.blink_flags |= TEXT_BLINK_STATE;
		}
		if((m_s.sequencer.reg1 & 0x01) == 0) {
			if(tm_info.h_panning >= 8)
				tm_info.h_panning = 0;
			else
				tm_info.h_panning++;
		} else {
			tm_info.h_panning &= 0x07;
		}
		for(int index = 0; index < 16; index++) {
			tm_info.actl_palette[index] = m_s.attribute_ctrl.palette_reg[index];
		}

		// Verticle Display End: find out how many lines are displayed
		VDE = m_s.vertical_display_end;
		// Maximum Scan Line: height of character cell
		MSL = m_s.CRTC.reg[0x09] & 0x1f;
		cols = m_s.CRTC.reg[1] + 1;
		// workaround for update() calls before VGABIOS init
		if(cols == 1) {
			cols = 80;
			MSL = 15;
		}
		if((MSL == 1) && (VDE == 399)) {
			// emulated CGA graphics mode 160x100x16 colors
			MSL = 3;
		}
		rows = (VDE+1)/(MSL+1);
		if((rows * tm_info.line_offset) > (1 << 17)) {
			PDEBUGF(LOG_V0, LOG_VGA, "update(): text mode: out of memory\n");
			return;
		}
		cWidth = ((m_s.sequencer.reg1 & 0x01) == 1) ? 8 : 9;
		iWidth = cWidth * cols;
		iHeight = VDE+1;
		if((iWidth != m_s.last_xres) || (iHeight != m_s.last_yres) || (MSL != m_s.last_msl) || (m_s.last_bpp > 8))
		{
			m_display->dimension_update(iWidth, iHeight, (uint)MSL+1, cWidth);
			m_s.last_xres = iWidth;
			m_s.last_yres = iHeight;
			m_s.last_msl = MSL;
			m_s.last_bpp = 8;
		}

		/* handle clear screen request from the sequencer */
		if(m_s.sequencer.clear_screen) {
			m_display->clear_screen();
			m_s.sequencer.clear_screen = false;
		}

		if(skip) {
			m_display->unlock();
			return;
		}

		PDEBUGF(LOG_V2, LOG_VGA, "text update\n");

		// pass old text snapshot & new VGA memory contents
		start_address = tm_info.start_address;
		cursor_address = 2*((m_s.CRTC.reg[0x0e] << 8) + m_s.CRTC.reg[0x0f]);
		if(cursor_address < start_address) {
			cursor_x = 0xffff;
			cursor_y = 0xffff;
		} else {
			cursor_x = ((cursor_address - start_address)/2) % (iWidth/cWidth);
			cursor_y = ((cursor_address - start_address)/2) / (iWidth/cWidth);
		}
		m_display->text_update(m_s.text_snapshot, &m_s.memory[start_address], cursor_x, cursor_y, &tm_info);
		if(m_s.vga_mem_updated) {
			// screen updated, copy new VGA memory contents into text snapshot
			memcpy(m_s.text_snapshot, &m_s.memory[start_address], tm_info.line_offset*rows);
			m_s.vga_mem_updated = 0;
		}
	}

	m_display->unlock();
	g_gui.vga_update();
}

void VGA::vertical_retrace()
{
	m_s.vretrace_time_usec = g_machine.get_virt_time_us();

	if(!(m_s.CRTC.reg[0x11] & 0x20) && !skip_update()) {
		raise_interrupt();
	}
	//the start address is latched at vretrace
	PDEBUGF(LOG_V2, LOG_VGA, "CRTC start address latch\n");
	m_s.CRTC.start_address = (m_s.CRTC.reg[0x0c] << 8) | m_s.CRTC.reg[0x0d];

	//next is the "vblank start"
	uint64_t vbstart = (m_s.vtotal_usec - m_s.vrstart_usec) + m_s.vblank_usec;
	g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::update,this));
	g_machine.activate_timer(m_timer_id, vbstart, false);
}

uint8_t VGA::mem_read(uint32_t addr)
{
	uint32_t offset;
	uint8_t *plane0, *plane1, *plane2, *plane3;

	switch (m_s.graphics_ctrl.memory_mapping) {
		case 1: // 0xA0000 .. 0xAFFFF
			if(addr > 0xAFFFF) return 0xff;
			offset = addr & 0xFFFF;
			break;
		case 2: // 0xB0000 .. 0xB7FFF
			if((addr < 0xB0000) || (addr > 0xB7FFF)) return 0xff;
			offset = addr & 0x7FFF;
			break;
		case 3: // 0xB8000 .. 0xBFFFF
			if(addr < 0xB8000) return 0xff;
			offset = addr & 0x7FFF;
			break;
		default: // 0xA0000 .. 0xBFFFF
			offset = addr & 0x1FFFF;
			break;
	}

	if(m_s.sequencer.chain_four) {
		// Mode 13h: 320 x 200 256 color mode: chained pixel representation
		return m_s.memory[(offset & ~0x03) + (offset % 4)*65536];
	}

	plane0 = &m_s.memory[(0 << m_s.plane_shift) + m_s.plane_offset];
	plane1 = &m_s.memory[(1 << m_s.plane_shift) + m_s.plane_offset];
	plane2 = &m_s.memory[(2 << m_s.plane_shift) + m_s.plane_offset];
	plane3 = &m_s.memory[(3 << m_s.plane_shift) + m_s.plane_offset];

	/* addr between 0xA0000 and 0xAFFFF */
	switch (m_s.graphics_ctrl.read_mode) {
		case 0: /* read mode 0 */
			m_s.graphics_ctrl.latch[0] = plane0[offset];
			m_s.graphics_ctrl.latch[1] = plane1[offset];
			m_s.graphics_ctrl.latch[2] = plane2[offset];
			m_s.graphics_ctrl.latch[3] = plane3[offset];
			return(m_s.graphics_ctrl.latch[m_s.graphics_ctrl.read_map_select]);
			break;

		case 1: /* read mode 1 */
		{
			uint8_t color_compare, color_dont_care;
			uint8_t latch0, latch1, latch2, latch3, retval;

			color_compare   = m_s.graphics_ctrl.color_compare & 0x0f;
			color_dont_care = m_s.graphics_ctrl.color_dont_care & 0x0f;
			latch0 = m_s.graphics_ctrl.latch[0] = plane0[offset];
			latch1 = m_s.graphics_ctrl.latch[1] = plane1[offset];
			latch2 = m_s.graphics_ctrl.latch[2] = plane2[offset];
			latch3 = m_s.graphics_ctrl.latch[3] = plane3[offset];

			latch0 ^= ccdat[color_compare][0];
			latch1 ^= ccdat[color_compare][1];
			latch2 ^= ccdat[color_compare][2];
			latch3 ^= ccdat[color_compare][3];

			latch0 &= ccdat[color_dont_care][0];
			latch1 &= ccdat[color_dont_care][1];
			latch2 &= ccdat[color_dont_care][2];
			latch3 &= ccdat[color_dont_care][3];

			retval = ~(latch0 | latch1 | latch2 | latch3);

			return retval;
		}

		default:
			return 0;
	}
}

void VGA::mem_write(uint32_t addr, uint8_t value)
{
	uint32_t offset;
	uint8_t new_val[4] = {0,0,0,0};
	uint8_t *plane0, *plane1, *plane2, *plane3;

	switch (m_s.graphics_ctrl.memory_mapping) {
		case 1: // 0xA0000 .. 0xAFFFF
			if((addr < 0xA0000) || (addr > 0xAFFFF)) return;
			offset = (uint32_t)addr - 0xA0000;
			break;
		case 2: // 0xB0000 .. 0xB7FFF
			if((addr < 0xB0000) || (addr > 0xB7FFF)) return;
			offset = (uint32_t)addr - 0xB0000;
			break;
		case 3: // 0xB8000 .. 0xBFFFF
			if((addr < 0xB8000) || (addr > 0xBFFFF)) return;
			offset = (uint32_t)addr - 0xB8000;
			break;
		default: // 0xA0000 .. 0xBFFFF
			if((addr < 0xA0000) || (addr > 0xBFFFF)) return;
			offset = (uint32_t)addr - 0xA0000;
			break;
	}

	if(m_s.graphics_ctrl.graphics_alpha) {
		if(m_s.graphics_ctrl.memory_mapping == 3) { // 0xB8000 .. 0xBFFFF
			uint x_tileno, x_tileno2, y_tileno;

			/* CGA 320x200x4 / 640x200x2 start */
			m_s.memory[offset] = value;
			offset -= m_s.CRTC.start_address;
			if(offset>=0x2000) {
				y_tileno = offset - 0x2000;
				y_tileno /= (320/4);
				y_tileno <<= 1; //2 * y_tileno;
				y_tileno++;
				x_tileno = (offset - 0x2000) % (320/4);
				x_tileno <<= 2; //*= 4;
			} else {
				y_tileno = offset / (320/4);
				y_tileno <<= 1; //2 * y_tileno;
				x_tileno = offset % (320/4);
				x_tileno <<= 2; //*=4;
			}
			x_tileno2=x_tileno;
			if(m_s.graphics_ctrl.shift_reg==0) {
				x_tileno*=2;
				x_tileno2+=7;
			} else {
				x_tileno2+=3;
			}
			if(m_s.x_dotclockdiv2) {
				x_tileno/=(VGA_X_TILESIZE/2);
				x_tileno2/=(VGA_X_TILESIZE/2);
			} else {
				x_tileno/=VGA_X_TILESIZE;
				x_tileno2/=VGA_X_TILESIZE;
			}
			if(m_s.y_doublescan) {
				y_tileno/=(VGA_Y_TILESIZE/2);
			} else {
				y_tileno/=VGA_Y_TILESIZE;
			}
			m_s.vga_mem_updated = 1;
			SET_TILE_UPDATED(x_tileno, y_tileno, true);
			if(x_tileno2!=x_tileno) {
				SET_TILE_UPDATED(x_tileno2, y_tileno, true);
			}
			return;
			/* CGA 320x200x4 / 640x200x2 end */
		}
		/*
		else if(m_s.graphics_ctrl.memory_mapping != 1) {
		  PERRF(LOG_VGA, "mem_write: graphics: mapping = %u",
				   (uint) m_s.graphics_ctrl.memory_mapping));
		  return;
		}
		*/
		if(m_s.sequencer.chain_four) {
			uint x_tileno, y_tileno;

			// 320 x 200 256 color mode: chained pixel representation
			m_s.memory[(offset & ~0x03) + (offset % 4)*65536] = value;
			if(m_s.line_offset > 0) {
				offset -= m_s.CRTC.start_address;
				x_tileno = (offset % m_s.line_offset) / (VGA_X_TILESIZE/2);
				if(m_s.y_doublescan) {
					y_tileno = (offset / m_s.line_offset) / (VGA_Y_TILESIZE/2);
				} else {
					y_tileno = (offset / m_s.line_offset) / VGA_Y_TILESIZE;
				}
				m_s.vga_mem_updated = 1;
				SET_TILE_UPDATED(x_tileno, y_tileno, true);
			}
			return;
		}
	}

	/* addr between 0xA0000 and 0xAFFFF */

	plane0 = &m_s.memory[(0 << m_s.plane_shift) + m_s.plane_offset];
	plane1 = &m_s.memory[(1 << m_s.plane_shift) + m_s.plane_offset];
	plane2 = &m_s.memory[(2 << m_s.plane_shift) + m_s.plane_offset];
	plane3 = &m_s.memory[(3 << m_s.plane_shift) + m_s.plane_offset];

	switch (m_s.graphics_ctrl.write_mode) {
		uint i;

		case 0: /* write mode 0 */
		{
			const uint8_t bitmask = m_s.graphics_ctrl.bitmask;
			const uint8_t set_reset = m_s.graphics_ctrl.set_reset;
			const uint8_t enable_set_reset = m_s.graphics_ctrl.enable_set_reset;
			/* perform rotate on CPU data in case its needed */
			if(m_s.graphics_ctrl.data_rotate) {
				value = (value >> m_s.graphics_ctrl.data_rotate) |
						(value << (8 - m_s.graphics_ctrl.data_rotate));
			}
			new_val[0] = m_s.graphics_ctrl.latch[0] & ~bitmask;
			new_val[1] = m_s.graphics_ctrl.latch[1] & ~bitmask;
			new_val[2] = m_s.graphics_ctrl.latch[2] & ~bitmask;
			new_val[3] = m_s.graphics_ctrl.latch[3] & ~bitmask;
			switch (m_s.graphics_ctrl.raster_op) {
				case 0: // replace
					new_val[0] |= ((enable_set_reset & 1)
								   ? ((set_reset & 1) ? bitmask : 0)
								   : (value & bitmask));
					new_val[1] |= ((enable_set_reset & 2)
								   ? ((set_reset & 2) ? bitmask : 0)
								   : (value & bitmask));
					new_val[2] |= ((enable_set_reset & 4)
								   ? ((set_reset & 4) ? bitmask : 0)
								   : (value & bitmask));
					new_val[3] |= ((enable_set_reset & 8)
								   ? ((set_reset & 8) ? bitmask : 0)
								   : (value & bitmask));
					break;
				case 1: // AND
					new_val[0] |= ((enable_set_reset & 1)
								   ? ((set_reset & 1)
									  ? (m_s.graphics_ctrl.latch[0] & bitmask)
									  : 0)
								   : (value & m_s.graphics_ctrl.latch[0]) & bitmask);
					new_val[1] |= ((enable_set_reset & 2)
								   ? ((set_reset & 2)
									  ? (m_s.graphics_ctrl.latch[1] & bitmask)
									  : 0)
								   : (value & m_s.graphics_ctrl.latch[1]) & bitmask);
					new_val[2] |= ((enable_set_reset & 4)
								   ? ((set_reset & 4)
									  ? (m_s.graphics_ctrl.latch[2] & bitmask)
									  : 0)
								   : (value & m_s.graphics_ctrl.latch[2]) & bitmask);
					new_val[3] |= ((enable_set_reset & 8)
								   ? ((set_reset & 8)
									  ? (m_s.graphics_ctrl.latch[3] & bitmask)
									  : 0)
								   : (value & m_s.graphics_ctrl.latch[3]) & bitmask);
					break;
				case 2: // OR
					new_val[0]
					  |= ((enable_set_reset & 1)
						  ? ((set_reset & 1)
							 ? bitmask
							 : (m_s.graphics_ctrl.latch[0] & bitmask))
						  : ((value | m_s.graphics_ctrl.latch[0]) & bitmask));
					new_val[1]
					  |= ((enable_set_reset & 2)
						  ? ((set_reset & 2)
							 ? bitmask
							 : (m_s.graphics_ctrl.latch[1] & bitmask))
						  : ((value | m_s.graphics_ctrl.latch[1]) & bitmask));
					new_val[2]
					  |= ((enable_set_reset & 4)
						  ? ((set_reset & 4)
							 ? bitmask
							 : (m_s.graphics_ctrl.latch[2] & bitmask))
						  : ((value | m_s.graphics_ctrl.latch[2]) & bitmask));
					new_val[3]
					  |= ((enable_set_reset & 8)
						  ? ((set_reset & 8)
							 ? bitmask
							 : (m_s.graphics_ctrl.latch[3] & bitmask))
						  : ((value | m_s.graphics_ctrl.latch[3]) & bitmask));
					break;
				case 3: // XOR
					new_val[0]
					  |= ((enable_set_reset & 1)
						 ? ((set_reset & 1)
							? (~m_s.graphics_ctrl.latch[0] & bitmask)
							: (m_s.graphics_ctrl.latch[0] & bitmask))
						 : (value ^ m_s.graphics_ctrl.latch[0]) & bitmask);
					new_val[1]
					  |= ((enable_set_reset & 2)
						 ? ((set_reset & 2)
							? (~m_s.graphics_ctrl.latch[1] & bitmask)
							: (m_s.graphics_ctrl.latch[1] & bitmask))
						 : (value ^ m_s.graphics_ctrl.latch[1]) & bitmask);
					new_val[2]
					  |= ((enable_set_reset & 4)
						 ? ((set_reset & 4)
							? (~m_s.graphics_ctrl.latch[2] & bitmask)
							: (m_s.graphics_ctrl.latch[2] & bitmask))
						 : (value ^ m_s.graphics_ctrl.latch[2]) & bitmask);
					new_val[3]
					  |= ((enable_set_reset & 8)
						 ? ((set_reset & 8)
							? (~m_s.graphics_ctrl.latch[3] & bitmask)
							: (m_s.graphics_ctrl.latch[3] & bitmask))
						 : (value ^ m_s.graphics_ctrl.latch[3]) & bitmask);
					break;
				default:
					PERRF(LOG_VGA, "vga_mem_write: write mode 0: op = %u\n", m_s.graphics_ctrl.raster_op);
					break;
			}
		}
		break;

		case 1: /* write mode 1 */
			for(i=0; i<4; i++) {
				new_val[i] = m_s.graphics_ctrl.latch[i];
			}
			break;

		case 2: /* write mode 2 */
		{
			const uint8_t bitmask = m_s.graphics_ctrl.bitmask;

			new_val[0] = m_s.graphics_ctrl.latch[0] & ~bitmask;
			new_val[1] = m_s.graphics_ctrl.latch[1] & ~bitmask;
			new_val[2] = m_s.graphics_ctrl.latch[2] & ~bitmask;
			new_val[3] = m_s.graphics_ctrl.latch[3] & ~bitmask;
			switch (m_s.graphics_ctrl.raster_op) {
				case 0: // write
					new_val[0] |= (value & 1) ? bitmask : 0;
					new_val[1] |= (value & 2) ? bitmask : 0;
					new_val[2] |= (value & 4) ? bitmask : 0;
					new_val[3] |= (value & 8) ? bitmask : 0;
					break;
				case 1: // AND
					new_val[0] |= (value & 1)
					  ? (m_s.graphics_ctrl.latch[0] & bitmask)
					  : 0;
					new_val[1] |= (value & 2)
					  ? (m_s.graphics_ctrl.latch[1] & bitmask)
					  : 0;
					new_val[2] |= (value & 4)
					  ? (m_s.graphics_ctrl.latch[2] & bitmask)
					  : 0;
					new_val[3] |= (value & 8)
					  ? (m_s.graphics_ctrl.latch[3] & bitmask)
					  : 0;
					break;
				case 2: // OR
					new_val[0] |= (value & 1)
					  ? bitmask
					  : (m_s.graphics_ctrl.latch[0] & bitmask);
					new_val[1] |= (value & 2)
					  ? bitmask
					  : (m_s.graphics_ctrl.latch[1] & bitmask);
					new_val[2] |= (value & 4)
					  ? bitmask
					  : (m_s.graphics_ctrl.latch[2] & bitmask);
					new_val[3] |= (value & 8)
					  ? bitmask
					  : (m_s.graphics_ctrl.latch[3] & bitmask);
					break;
				case 3: // XOR
					new_val[0] |= (value & 1)
					  ? (~m_s.graphics_ctrl.latch[0] & bitmask)
					  : (m_s.graphics_ctrl.latch[0] & bitmask);
					new_val[1] |= (value & 2)
					  ? (~m_s.graphics_ctrl.latch[1] & bitmask)
					  : (m_s.graphics_ctrl.latch[1] & bitmask);
					new_val[2] |= (value & 4)
					  ? (~m_s.graphics_ctrl.latch[2] & bitmask)
					  : (m_s.graphics_ctrl.latch[2] & bitmask);
					new_val[3] |= (value & 8)
					  ? (~m_s.graphics_ctrl.latch[3] & bitmask)
					  : (m_s.graphics_ctrl.latch[3] & bitmask);
					break;
			}
		}
		break;

		case 3: /* write mode 3 */
		{
			const uint8_t bitmask = m_s.graphics_ctrl.bitmask & value;
			const uint8_t set_reset = m_s.graphics_ctrl.set_reset;

			/* perform rotate on CPU data */
			if(m_s.graphics_ctrl.data_rotate) {
				value = (value >> m_s.graphics_ctrl.data_rotate) |
						(value << (8 - m_s.graphics_ctrl.data_rotate));
			}
			new_val[0] = m_s.graphics_ctrl.latch[0] & ~bitmask;
			new_val[1] = m_s.graphics_ctrl.latch[1] & ~bitmask;
			new_val[2] = m_s.graphics_ctrl.latch[2] & ~bitmask;
			new_val[3] = m_s.graphics_ctrl.latch[3] & ~bitmask;

			value &= bitmask;

			switch (m_s.graphics_ctrl.raster_op) {
				case 0: // write
					new_val[0] |= (set_reset & 1) ? value : 0;
					new_val[1] |= (set_reset & 2) ? value : 0;
					new_val[2] |= (set_reset & 4) ? value : 0;
					new_val[3] |= (set_reset & 8) ? value : 0;
					break;
				case 1: // AND
					new_val[0] |= ((set_reset & 1) ? value : 0)
					  & m_s.graphics_ctrl.latch[0];
					new_val[1] |= ((set_reset & 2) ? value : 0)
					  & m_s.graphics_ctrl.latch[1];
					new_val[2] |= ((set_reset & 4) ? value : 0)
					  & m_s.graphics_ctrl.latch[2];
					new_val[3] |= ((set_reset & 8) ? value : 0)
					  & m_s.graphics_ctrl.latch[3];
					break;
				case 2: // OR
					new_val[0] |= ((set_reset & 1) ? value : 0)
					  | m_s.graphics_ctrl.latch[0];
					new_val[1] |= ((set_reset & 2) ? value : 0)
					  | m_s.graphics_ctrl.latch[1];
					new_val[2] |= ((set_reset & 4) ? value : 0)
					  | m_s.graphics_ctrl.latch[2];
					new_val[3] |= ((set_reset & 8) ? value : 0)
					  | m_s.graphics_ctrl.latch[3];
					break;
				case 3: // XOR
					new_val[0] |= ((set_reset & 1) ? value : 0)
					  ^ m_s.graphics_ctrl.latch[0];
					new_val[1] |= ((set_reset & 2) ? value : 0)
					  ^ m_s.graphics_ctrl.latch[1];
					new_val[2] |= ((set_reset & 4) ? value : 0)
					  ^ m_s.graphics_ctrl.latch[2];
					new_val[3] |= ((set_reset & 8) ? value : 0)
					  ^ m_s.graphics_ctrl.latch[3];
					break;
			}
		}
		break;

		default:
			PERRF(LOG_VGA, "vga_mem_write: write mode %u ?\n", m_s.graphics_ctrl.write_mode);
			break;
	}

	if(m_s.sequencer.map_mask & 0x0f) {
		m_s.vga_mem_updated = 1;
		if(m_s.sequencer.map_mask & 0x01)
			plane0[offset] = new_val[0];
		if(m_s.sequencer.map_mask & 0x02)
			plane1[offset] = new_val[1];
		if(m_s.sequencer.map_mask & 0x04) {
			if((offset & 0xe000) == m_s.charmap_address) {
				m_display->lock();
				m_display->set_text_charbyte((offset & 0x1fff), new_val[2]);
				m_display->unlock();
			}
			plane2[offset] = new_val[2];
		}
		if(m_s.sequencer.map_mask & 0x08)
			plane3[offset] = new_val[3];

		uint x_tileno, y_tileno;

		if(m_s.graphics_ctrl.shift_reg == 2) {
			offset -= m_s.CRTC.start_address;
			x_tileno = (offset % m_s.line_offset) * 4 / (VGA_X_TILESIZE / 2);
			if(m_s.y_doublescan) {
				y_tileno = (offset / m_s.line_offset) / (VGA_Y_TILESIZE / 2);
			} else {
				y_tileno = (offset / m_s.line_offset) / VGA_Y_TILESIZE;
			}
			SET_TILE_UPDATED(x_tileno, y_tileno, true);
		} else {
			if(m_s.line_compare < m_s.vertical_display_end) {
				if(m_s.line_offset > 0) {
					if(m_s.x_dotclockdiv2) {
						x_tileno = (offset % m_s.line_offset) / (VGA_X_TILESIZE / 16);
					} else {
						x_tileno = (offset % m_s.line_offset) / (VGA_X_TILESIZE / 8);
					}
					if(m_s.y_doublescan) {
						y_tileno = ((offset / m_s.line_offset) * 2 + m_s.line_compare + 1) / VGA_Y_TILESIZE;
					} else {
						y_tileno = ((offset / m_s.line_offset) + m_s.line_compare + 1) / VGA_Y_TILESIZE;
					}
					SET_TILE_UPDATED(x_tileno, y_tileno, true);
				}
			}
			if(offset >= m_s.CRTC.start_address) {
				offset -= m_s.CRTC.start_address;
				if(m_s.line_offset > 0) {
					if(m_s.x_dotclockdiv2) {
						x_tileno = (offset % m_s.line_offset) / (VGA_X_TILESIZE / 16);
					} else {
						x_tileno = (offset % m_s.line_offset) / (VGA_X_TILESIZE / 8);
					}
					if(m_s.y_doublescan) {
						y_tileno = (offset / m_s.line_offset) / (VGA_Y_TILESIZE / 2);
					} else {
						y_tileno = (offset / m_s.line_offset) / VGA_Y_TILESIZE;
					}
					SET_TILE_UPDATED(x_tileno, y_tileno, true);
				}
			}
		}
	}
}

void VGA::get_text_snapshot(uint8_t **text_snapshot, uint *txHeight, uint *txWidth)
{
	uint VDE, MSL;

	if(!m_s.graphics_ctrl.graphics_alpha) {
		*text_snapshot = &m_s.text_snapshot[0];
		VDE = m_s.vertical_display_end;
		MSL = m_s.CRTC.reg[0x09] & 0x1f;
		*txHeight = (VDE+1)/(MSL+1);
		*txWidth = m_s.CRTC.reg[1] + 1;
	} else {
		*txHeight = 0;
		*txWidth = 0;
	}
}

void VGA::redraw_area(uint x0, uint y0, uint width, uint height)
{
	uint xti, yti, xt0, xt1, yt0, yt1, xmax, ymax;

	if(width == 0 || height == 0) {
		return;
	}

	m_s.vga_mem_updated = 1;

	if(m_s.graphics_ctrl.graphics_alpha) {
		// graphics mode
		xmax = m_s.last_xres;
		ymax = m_s.last_yres;
		xt0 = x0 / VGA_X_TILESIZE;
		yt0 = y0 / VGA_Y_TILESIZE;
		if(x0 < xmax) {
			xt1 = (x0 + width  - 1) / VGA_X_TILESIZE;
		} else {
			xt1 = (xmax - 1) / VGA_X_TILESIZE;
		}
		if(y0 < ymax) {
			yt1 = (y0 + height - 1) / VGA_Y_TILESIZE;
		} else {
			yt1 = (ymax - 1) / VGA_Y_TILESIZE;
		}
		for(yti=yt0; yti<=yt1; yti++) {
			for(xti=xt0; xti<=xt1; xti++) {
				SET_TILE_UPDATED(xti, yti, true);
			}
		}
	} else {
		// text mode
		memset(m_s.text_snapshot, 0, sizeof(m_s.text_snapshot));
	}
}

