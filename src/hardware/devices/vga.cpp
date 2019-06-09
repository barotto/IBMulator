/*
 * Copyright (C) 2001-2013  The Bochs Project
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
 * Portions of code Copyright (C) 2002-2015  The DOSBox Team
 */

#include "ibmulator.h"
#include "vga.h"
#include "program.h"
#include "machine.h"
#include "hardware/devices.h"
#include "hardware/memory.h"
#include "hardware/devices/pic.h"
#include "gui/gui.h"
#include "filesys.h"
#include <cstring>

using namespace std::placeholders;

IODEVICE_PORTS(VGA) = {
	{ 0x03B4, 0x03B5, PORT_8BIT|PORT_RW }, //3B4-3B5 CRTC Controller Address / Data
	{ 0x03BA, 0x03BA, PORT_8BIT|PORT_RW }, //3BA mono Input Status #1 (R) / Feature Control (W)
	{ 0x03C0, 0x03C0, PORT_8BIT|PORT_RW }, //3C0 Attribute Address/Data
	{ 0x03C1, 0x03C1, PORT_8BIT|PORT_RW }, //3C1 Attribute Data Read
	{ 0x03C2, 0x03C2, PORT_8BIT|PORT_RW }, //3C2 Input Status #0 (R) / Miscellaneous Output (W)
	{ 0x03C3, 0x03C3, PORT_8BIT|PORT_RW }, //3C3 Video subsystem enable
	{ 0x03C4, 0x03C4, PORT_8BIT|PORT_RW }, //3C4 Sequencer Address
	{ 0x03C5, 0x03C5, PORT_8BIT|PORT_RW }, //3C5 Sequencer Data
	{ 0x03C6, 0x03C6, PORT_8BIT|PORT_RW }, //3C6 PEL mask
	{ 0x03C7, 0x03C7, PORT_8BIT|PORT_RW }, //3C7 DAC State (R) / DAC Address Read Mode (W)
	{ 0x03C8, 0x03C8, PORT_8BIT|PORT_RW }, //3C8 PEL Address
	{ 0x03C9, 0x03C9, PORT_8BIT|PORT_RW }, //3C9 PEL Data
	{ 0x03CA, 0x03CA, PORT_8BIT|PORT_R_ }, //3CA Feature Control (R)
	{ 0x03CC, 0x03CC, PORT_8BIT|PORT_R_ }, //3CC Miscellaneous Output (R)
	{ 0x03CE, 0x03CE, PORT_8BIT|PORT_RW }, //3CE Graphics Controller Address
	{ 0x03CF, 0x03CF, PORT_8BIT|PORT_RW }, //3CF Graphics Controller Data
	{ 0x03D4, 0x03D5, PORT_8BIT|PORT_RW }, //3D4-3D5 CRTC Controller Address / Data
	{ 0x03D0, 0x03D0, PORT_8BIT|PORT__W }, //3D0 CGA same as 3D4
	{ 0x03D1, 0x03D1, PORT_8BIT|PORT_RW }, //3D1 CGA same as 3D5
	{ 0x03D2, 0x03D2, PORT_8BIT|PORT__W }, //3D2 CGA same as 3D4
	{ 0x03D3, 0x03D3, PORT_8BIT|PORT_RW }, //3D3 CGA same as 3D5
	{ 0x03DA, 0x03DA, PORT_8BIT|PORT_RW }  //3DA color Input Status #1 (R) / Feature Control (W)
};

#define VGA_IRQ 9

enum VGATimerFunctionNames {
	VGA_VERTICAL_RETRACE,
	VGA_HORIZ_DISP_END,
	VGA_FRAME_START,
	VGA_FRAME_END
};

VGA::VGA(Devices *_dev)
: IODevice(_dev),
m_memory(nullptr),
m_rom(nullptr),
m_memsize(0x40000),
m_mem_mapping(0),
m_rom_mapping(0),
m_vga_timing(VGA_8BIT_SLOW),
m_bus_timing(1.0),
m_timer_id(NULL_TIMER_HANDLE),
m_display(nullptr),
m_renderer(nullptr),
m_num_x_tiles(0)
{
	m_line_data_buf[0].reserve(VGA_MAX_XRES);
	m_line_data_buf[1].reserve(VGA_MAX_XRES);
	
	unsigned max_x_tiles = VGA_MAX_XRES / VGA_X_TILESIZE + ((VGA_MAX_XRES % VGA_X_TILESIZE) > 0);
	m_tile_dirty.reserve(max_x_tiles * VGA_MAX_YRES);
	
	m_s = {};
	m_cur_upd_pix = 0;
}

VGA::~VGA()
{
	if(m_memory != nullptr) {
		delete[] m_memory;
	}
	if(m_rom != nullptr) {
		delete[] m_rom;
	}
}

void VGA::install()
{
	IODevice::install();
	g_machine.register_irq(VGA_IRQ, name());
	m_memory = new uint8_t[m_memsize];
	m_rom = new uint8_t[0x10000];
	m_timer_id = g_machine.register_timer(nullptr, name());
	/*
	g_memory.register_trap(0xA0000, 0xBFFFF, MEM_TRAP_READ|MEM_TRAP_WRITE,
	[this] (uint32_t addr, uint8_t rw, uint16_t value, uint8_t len) {
		if(rw == MEM_TRAP_READ) {
			PDEBUGF(LOG_V2, LOG_VGA, "%s[0x%05X] = 0x%04X\n", len==1?"b":"w", addr, value);
		} else { //write
			PDEBUGF(LOG_V2, LOG_VGA, "%s[0x%05X] := 0x%04X\n", len==1?"b":"w", addr, value);
		}
	});
	*/
	m_mem_mapping = g_memory.add_mapping(0xA0000, 0x20000, MEM_MAPPING_EXTERNAL);
	m_rom_mapping = g_memory.add_mapping(0xC0000, 0x10000, MEM_MAPPING_EXTERNAL,
		VGA::s_rom_read<uint8_t>, VGA::s_rom_read<uint16_t>, VGA::s_rom_read<uint32_t>, this);
}

void VGA::config_changed()
{
	const int video_timings[8][3] = {
		//B   W   D
		{10, 20, 40 }, // Slow 8-bit
		{ 8, 16, 32 }, // Fast 8-bit 1mb/sec
		{ 6,  8, 16 }, // Slow 16-bit 2mb/sec
		{ 4,  6, 12 }, // Mid  16-bit
		{ 3,  3,  6 }, // Fast 16-bit 4mb/sec
		{ 4,  8, 16 }, // Slow 32-bit ~8mb/sec
		{ 4,  5, 10 }, // Mid  32-bit
		{ 3,  3,  4 }  // Fast 32-bit
	};

	unsigned byte  = ceil(m_bus_timing * video_timings[m_vga_timing][0]);
	unsigned word  = ceil(m_bus_timing * video_timings[m_vga_timing][1]);
	unsigned dword = ceil(m_bus_timing * video_timings[m_vga_timing][2]);

	g_memory.set_mapping_cycles(m_mem_mapping, byte, word, dword);

	PINFOF(LOG_V2, LOG_VGA, "VRAM speed: %d/%d/%d cycles\n",
			byte, word, dword);

	g_memory.enable_mapping(m_rom_mapping, false);
	std::string romfile = g_program.config().find_file(SYSTEM_SECTION, SYSTEM_VGAROM);
	if(!romfile.empty()) {
		try {
			load_ROM(romfile);
			byte = word = ceil(m_bus_timing * 2); // ???
			dword = word*2;
			g_memory.set_mapping_cycles(m_rom_mapping, byte, word, dword);
			g_memory.enable_mapping(m_rom_mapping, true);
		} catch(std::exception &e) {}
	}
}

void VGA::remove()
{
	IODevice::remove();
	g_machine.unregister_irq(VGA_IRQ);
	g_machine.unregister_timer(m_timer_id);

	if(m_memory != nullptr) {
		delete[] m_memory;
		m_memory = nullptr;
	}
	if(m_rom != nullptr) {
		delete[] m_rom;
		m_rom = nullptr;
	}

	g_memory.remove_mapping(m_mem_mapping);
}

void VGA::reset(unsigned _type)
{
	if(_type == MACHINE_POWER_ON || _type == MACHINE_HARD_RESET) {
		if(!m_display) {
			m_display = g_gui.vga_display();
		}

		m_s = {};

		m_s.scanline = -1;
		m_s.blink_counter = VGA_BLINK_COUNTER;
		
		// Mode 3+ 80x25,9x16,70Hz,720x400
		m_s.gen_regs.misc_output.IOS = 1;
		m_s.gen_regs.misc_output.ERAM = 1;
		m_s.gen_regs.misc_output.POL = POL_400;
		m_s.gen_regs.misc_output.CS = CS_28MHZ;
		m_s.CRTC.set_registers(VGA_CRTC::modes[0x03]);
		m_s.attr_ctrl.set_registers(VGA_AttrCtrl::modes[0x03]);
		m_s.attr_ctrl.address.IPAS = 1;
		m_s.gfx_ctrl.set_registers(VGA_GfxCtrl::modes[0x03]);
		m_s.gfx_ctrl.memory_offset = 0xB8000;
		m_s.gfx_ctrl.memory_aperture = 0x8000;
		m_s.sequencer.set_registers(VGA_Sequencer::modes[0x03]);
		m_s.dac.state = 0x01;
		m_s.dac.pel_mask = 0xff;

		// TODO this stuff should be removed until VBE support is added
		m_s.plane_offset = 0;
		m_s.plane_shift = 16;
		m_s.dac_shift = 2;
		
		m_stats = {};
		
		m_s.render_mode = VGA_RENDER_FRAME;
		
		
	}
	
	memset(m_memory, 0, m_memsize);

	m_s.gen_regs.video_enable = 1;
	
	clear_screen();
	update_mem_mapping();
	calculate_timings();
	
	frame_start(g_machine.get_virt_time_ns());
}

void VGA::power_off()
{
	g_machine.deactivate_timer(m_timer_id);
	clear_screen();
}

void VGA::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "VGA: saving state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s, h);

	// memory
	h.name = std::string(name()) + " memory";
	h.data_size = m_memsize;
	_state.write(m_memory, h);

	// display
	m_display->save_state(_state);
}

void VGA::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "VGA: restoring state\n");

	StateHeader h;

	// state object
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s, h);

	// memory
	h.name = std::string(name()) + " memory";
	h.data_size = m_memsize;
	_state.read(m_memory, h);

	// display
	if(!m_display) {
		m_display = g_gui.vga_display();
	}
	m_display->restore_state(_state);

	reset_tiles();

	update_mem_mapping();
	
	switch(g_machine.get_timer(m_timer_id).func_name) {
		case VGA_VERTICAL_RETRACE:
			g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::vertical_retrace,this,_1), VGA_VERTICAL_RETRACE);
			break;
		case VGA_HORIZ_DISP_END:
			g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::horiz_disp_end,this,_1), VGA_HORIZ_DISP_END);
			break;
		case VGA_FRAME_START:
			g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::frame_start,this,_1), VGA_FRAME_START);
			break;
		case VGA_FRAME_END:
			g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::frame_end,this,_1), VGA_FRAME_END);
			break;
		default:
			throw std::exception();
	};
	
	switch(m_s.vmode.mode) {
		case VGA_M_CGA2:
			m_renderer = &VGA::draw_gfx_cga2;
			break;
		case VGA_M_CGA4:
			m_renderer = &VGA::draw_gfx_cga4;
			break;
		case VGA_M_EGA:
			m_renderer = &VGA::draw_gfx_ega;
			break;
		case VGA_M_256COL:
			m_renderer = &VGA::draw_gfx_vga256;
			break;
		case VGA_M_TEXT:
			m_renderer = nullptr;
			break;
		default:
			throw std::exception();
	};
	
	PDEBUGF(LOG_V1, LOG_VGA, "vtotal=%u\n", m_s.timings_ns.vtotal);
	g_machine.set_heartbeat(m_s.timings_ns.vtotal/1000);
	g_program.set_heartbeat(m_s.timings_ns.vtotal/1000);
	
	m_stats = {};
	m_cur_upd_pix = 0;
}

void VGA::update_mem_mapping()
{
	g_memory.resize_mapping(m_mem_mapping, m_s.gfx_ctrl.memory_offset,
		m_s.gfx_ctrl.memory_aperture);
	g_memory.set_mapping_funcs(m_mem_mapping,
		VGA::s_mem_read<uint8_t>,
		VGA::s_mem_read<uint16_t>,
		nullptr,
		this,
		VGA::s_mem_write<uint8_t>,
		VGA::s_mem_write<uint16_t>,
		nullptr,
		this);
	PDEBUGF(LOG_V1, LOG_VGA, "memory mapping: 0x%X .. 0x%X\n",
		m_s.gfx_ctrl.memory_offset,
		m_s.gfx_ctrl.memory_offset+m_s.gfx_ctrl.memory_aperture-1);
}

void VGA::load_ROM(const std::string &_filename)
{
	uint64_t size = FileSys::get_file_size(_filename.c_str());
	if(size > 0x10000) {
		PERRF(LOG_VGA, "ROM file '%s' is too big: %d bytes (64K max)\n", size);
		throw std::exception();
	}

	auto file = FileSys::make_file(_filename.c_str(), "rb");
	if(file == nullptr) {
		PERRF(LOG_VGA, "Error opening ROM file '%s'\n", _filename.c_str());
		throw std::exception();
	}

	PINFOF(LOG_V0, LOG_VGA, "Loading ROM '%s'\n", _filename.c_str());
	size = fread((void*)m_rom, size, 1, file.get());
	if(size != 1) {
		PERRF(LOG_VGA, "Error reading ROM file '%s'\n", _filename.c_str());
		throw std::exception();
	}
}

void VGA::reset_tiles()
{
	m_num_x_tiles = m_s.vmode.imgw / VGA_X_TILESIZE + ((m_s.vmode.imgw % VGA_X_TILESIZE) > 0);
	m_tile_dirty.resize(m_num_x_tiles * m_s.vmode.imgh);
	redraw_all();
}

void VGA::calculate_timings()
{
	// VERTICAL TIMINGS

	uint32_t vtotal  = m_s.CRTC.latches.vtotal + 2;
	uint32_t vdend   = m_s.CRTC.latches.vdisplay_end + 1;
	uint32_t vbstart = m_s.CRTC.latches.start_vblank;
	uint32_t vbend   = m_s.CRTC.end_vblank & 0x7f;
	uint32_t vrstart = m_s.CRTC.latches.vretrace_start;
	uint32_t vrend   = (m_s.CRTC.vretrace_end.VRE - vrstart) & 0xF;

	if(vrend == 0) {
		vrend = vrstart + 0xf + 1;
	} else {
		vrend = vrstart + vrend;
	}

	if(vbstart != 0) {
		vbstart += 1;
		vbend = (vbend - vbstart) & 0x7f;
		if(vbend == 0) {
			vbend = vbstart + 0x7f + 1;
		} else {
			vbend = vbstart + vbend;
		}
	} else {
		// When vbstart is 0, lines zero to vbend are blanked.
		// According to DosBox:
		//   ET3000 blanks lines 1 to vbend (255/6 lines).
		//   ET4000 doesn't blank if vbstart == vbend.
	}
	vbend++;

	// HORIZONTAL TIMINGS

	uint32_t htotal  = (m_s.CRTC.htotal + 5) << m_s.sequencer.clocking.DC;
	uint32_t hdend   = m_s.CRTC.hdisplay_end + 1;
	uint32_t hbstart = m_s.CRTC.start_hblank;
	uint32_t hbend   = hbstart + ((m_s.CRTC.latches.end_hblank - hbstart) & 0x3F);
	uint32_t hrstart = m_s.CRTC.start_hretrace;
	uint32_t hrend   = (m_s.CRTC.end_hretrace.EHR - hrstart) & 0x1F;
	uint32_t cwidth  = m_s.sequencer.clocking.D89 ? 8 : 9;

	if(hrend == 0) {
		hrend = hrstart + 0x1f + 1;
	} else {
		hrend = hrstart + hrend;
	}

	if(htotal == 0 || vtotal == 0) {
		g_machine.deactivate_timer(m_timer_id);
		return;
	}

	// DOT CLOCK FREQUENCIES

	double clock;
	switch(m_s.gen_regs.misc_output.CS & 3) {
	case 0:
		clock = 25175000.0;
		break;
	case 1:
	default:
		clock = 28322000.0;
		break;
	}

	m_s.timings.hfreq = (clock / (htotal * cwidth))/1000.0;
	clock /= cwidth;

	// The screen refresh frequency
	m_s.timings.vfreq = clock / (htotal * vtotal);

	m_s.timings.vtotal  = vtotal;
	m_s.timings.vdend   = vdend;
	m_s.timings.vbstart = vbstart;
	m_s.timings.vbend   = vbend;
	m_s.timings.vrstart = vrstart;
	m_s.timings.vrend   = vrend;
	m_s.timings.htotal  = htotal;
	m_s.timings.hdend   = hdend;
	m_s.timings.hbstart = hbstart;
	m_s.timings.hbend   = hbend;
	m_s.timings.hrstart = hrstart;
	m_s.timings.hrend   = hrend;
	m_s.timings.cwidth  = cwidth;
	
	double invclock_ns = 1e9 / clock;
	m_s.timings_ns.htotal  = htotal * invclock_ns;
	m_s.timings_ns.hdend   = hdend * invclock_ns;
	m_s.timings_ns.hbstart = hbstart * invclock_ns;
	m_s.timings_ns.hbend   = hbend * invclock_ns;
	m_s.timings_ns.hrstart = hrstart * invclock_ns;
	m_s.timings_ns.hrend   = hrend * invclock_ns;

	m_s.timings_ns.vdend   = m_s.timings_ns.htotal * vdend;
	m_s.timings_ns.vrstart = m_s.timings_ns.htotal * vrstart;
	m_s.timings_ns.vrend   = m_s.timings_ns.htotal * vrend;

	uint32_t vtotal_ns  = 1e9 / m_s.timings.vfreq;
	
	if(m_s.timings_ns.vtotal != vtotal_ns) {
		m_s.timings_ns.vtotal = vtotal_ns;
		PDEBUGF(LOG_V1, LOG_VGA, "vtotal=%u\n", m_s.timings_ns.vtotal);
		g_machine.set_heartbeat(m_s.timings_ns.vtotal/1000);
		g_program.set_heartbeat(m_s.timings_ns.vtotal/1000);
		
		m_display->lock();
		m_display->set_timings(m_s.timings.hfreq, m_s.timings.vfreq);
		m_display->unlock();
	}
	
	update_video_mode();
}

void VGA::update_video_mode()
{
	PDEBUGF(LOG_V2, LOG_VGA, "mode setting\n");
	
	VideoModeInfo oldmode = m_s.vmode;
	
	// IMAGE RESOLUTION

	uint32_t hdend = m_s.timings.hdend;
	// Check to prevent useless black areas
	if(m_s.timings.hbstart < m_s.timings.hdend) {
		hdend = m_s.timings.hbstart;
	}
	m_s.vmode.xres = (hdend * m_s.timings.cwidth) << m_s.sequencer.clocking.DC;

	// Vertical blanking tricks
	m_s.vmode.yres = m_s.timings.vdend;
	m_s.timings.vblank_skip = 0;
	
	uint16_t vbstart = m_s.timings.vbstart;
	if(m_s.timings.vrstart < m_s.timings.vbstart) {
		vbstart = m_s.timings.vrstart;
	}
	if(vbstart < m_s.timings.vtotal) { // There will be no blanking at all otherwise
		if(m_s.timings.vbend > m_s.timings.vtotal) {
			// Blanking wrap to line vblank_skip
			// this is used for example in Sid & Al's Incredible Toons

			// blanking wraps to the start of the screen
			m_s.timings.vblank_skip = m_s.timings.vbend & 0x7f;

			// on blanking wrap to 0, the first line is not blanked
			// this is used by the S3 BIOS and other S3 drivers in some SVGA modes
			if(m_s.timings.vblank_skip == 1) {
				m_s.timings.vblank_skip = 0;
			}

			// it might also cut some lines off the bottom
			if(vbstart < m_s.timings.vdend) {
				m_s.vmode.yres = vbstart;
			}
		} else if(vbstart <= 1) {
			// Upper vblank_skip lines of the screen blanked
			// blanking is used to cut lines at the start of the screen
			m_s.timings.vblank_skip = m_s.timings.vbend;
		} else if(vbstart < m_s.timings.vdend) {
			if(m_s.timings.vbend < m_s.timings.vdend) {
				// the program wants a black bar somewhere on the screen
				// Unsupported blanking at line (vbstart - vbend)
			} else {
				// blanking is used to cut off some lines from the bottom
				m_s.vmode.yres = vbstart;
			}
		}
		m_s.vmode.yres -= m_s.timings.vblank_skip;
	}
	m_s.timings.last_vis_sl = m_s.timings.vblank_skip + (m_s.vmode.yres - 1);
	// careful: last_vis_sl is 0-based
	m_s.timings_ns.frame_end = (m_s.timings.last_vis_sl * m_s.timings_ns.htotal) + m_s.timings_ns.htotal;
	

	// BORDERS (TODO)

	uint32_t start = m_s.timings.vbstart;
	if(m_s.timings.vrstart < m_s.timings.vbstart || m_s.timings.vbstart <= 1) {
		start = m_s.timings.vrstart;
	}
	m_s.vmode.borders.bottom = start - (m_s.timings.vdend-1);

	if(m_s.timings.vbend < m_s.timings.vtotal) {
		uint32_t end = m_s.timings.vbend;
		if(m_s.timings.vbend < m_s.timings.vrend) {
			end = m_s.timings.vrend;
		}
		m_s.vmode.borders.top = (m_s.timings.vtotal+1) - end;
	} else {
		m_s.vmode.borders.top = 0;
	}

	uint32_t end = m_s.timings.hbend;
	if(m_s.timings.hbend < m_s.timings.hrend) {
		end = m_s.timings.hrend-1;
	}
	m_s.vmode.borders.left = (((m_s.timings.htotal>>m_s.sequencer.clocking.DC)-1) - end) * m_s.timings.cwidth;
	m_s.vmode.borders.left <<= m_s.sequencer.clocking.DC;

	if(m_s.timings.hbstart >= m_s.timings.hdend) {
		m_s.vmode.borders.right = (m_s.timings.hbstart - (m_s.timings.hdend-1)) * m_s.timings.cwidth;
	} else {
		m_s.vmode.borders.right = 0;
	}
	m_s.vmode.borders.right <<= m_s.sequencer.clocking.DC;


	// VIDEO MODE
	
	m_s.vmode.imgw = m_s.vmode.xres;
	m_s.vmode.imgh = m_s.vmode.yres;
	if(m_s.gfx_ctrl.misc.GM) {
		if(m_s.gfx_ctrl.gfx_mode.C256 == 0) {
			if(m_s.gfx_ctrl.gfx_mode.SR == 0) {
				if(m_s.gfx_ctrl.misc.MM == MM_B8000_32K) {
					// CGA-compatible 640x200 2 colour graphics
					m_s.vmode.mode = VGA_M_CGA2;
					m_s.vmode.imgh >>= m_s.CRTC.max_scanline.DSC;
					m_s.vmode.nscans = 1 << m_s.CRTC.max_scanline.DSC;
					m_s.vmode.ndots = 1;
					m_s.render_mode = VGA_RENDER_LINE;
					m_renderer = &VGA::draw_gfx_cga2;
				} else {
					// EGA/VGA multiplane 16 colour
					m_s.vmode.mode = VGA_M_EGA;
					m_s.vmode.imgw >>= m_s.sequencer.clocking.DC;
					m_s.vmode.imgh /= m_s.CRTC.scanlines_div();
					m_s.vmode.nscans = m_s.CRTC.scanlines_div();
					m_s.vmode.ndots = 1 << m_s.sequencer.clocking.DC;
					if(m_s.vmode.imgw < 640 || m_s.vmode.imgh < 480) {
						// if the image is smaller than 640x480 I assume it's some
						// game or intro/demo that needs per-line rendering,
						// otherwise it's probably a GUI that is fine with per-frame
						// rendering
						m_s.render_mode = VGA_RENDER_LINE;
					} else {
						m_s.render_mode = VGA_RENDER_FRAME;
					}
					m_renderer = &VGA::draw_gfx_ega;
				}
			} else {
				// CGA-compatible 320x200 4 colour graphics
				m_s.vmode.mode = VGA_M_CGA4;
				m_s.vmode.imgw >>= m_s.sequencer.clocking.DC;
				m_s.vmode.imgh >>= m_s.CRTC.max_scanline.DSC;
				m_s.vmode.nscans = 1 << m_s.CRTC.max_scanline.DSC;
				m_s.vmode.ndots = 1 << m_s.sequencer.clocking.DC;
				m_s.render_mode = VGA_RENDER_LINE;
				m_renderer = &VGA::draw_gfx_cga4;
			}
		} else {
			// VGA 256 colour
			m_s.vmode.mode = VGA_M_256COL;
			m_s.vmode.imgw /= 2;
			m_s.vmode.imgh /= m_s.CRTC.scanlines_div();
			m_s.vmode.nscans = m_s.CRTC.scanlines_div();
			m_s.vmode.ndots = 2;
			m_s.render_mode = VGA_RENDER_LINE;
			m_renderer = &VGA::draw_gfx_vga256;
		}
		m_s.vmode.cwidth = 0;
		m_s.vmode.cheight = 0;
		m_s.vmode.textrows = 0;
		m_s.vmode.textcols = 0;
	} else {
		m_s.vmode.mode = VGA_M_TEXT;
		m_s.vmode.imgw >>= m_s.sequencer.clocking.DC;
		m_s.vmode.imgh >>= m_s.CRTC.max_scanline.DSC;
		m_s.vmode.nscans = 1 << m_s.CRTC.max_scanline.DSC;
		m_s.vmode.ndots = 1 << m_s.sequencer.clocking.DC;
		unsigned charheight = 0;
		charheight = m_s.CRTC.max_scanline.MSL + 1;
		if((charheight == 2) && (m_s.vmode.yres == 400)) {
			// emulated CGA graphics mode 160x100x16 colors
			charheight = 4;
		}
		m_s.vmode.cwidth = m_s.timings.cwidth;
		m_s.vmode.cheight = charheight;

		m_s.vmode.textcols = m_s.vmode.imgw / m_s.vmode.cwidth;
		m_s.vmode.textrows = m_s.vmode.imgh / m_s.vmode.cheight;

		// TODO do a per-line text renderer?
		m_s.render_mode = VGA_RENDER_FRAME;
		m_renderer = nullptr;
	}
	
	if(!(oldmode == m_s.vmode)) {
		if(!m_s.sequencer.clocking.SO) {
			if(m_s.vmode.mode == VGA_M_TEXT) {
			PDEBUGF(LOG_V1, LOG_VGA, "mode: %ux%u %s %ux%u %ux%u\n",
				m_s.vmode.imgw, m_s.vmode.imgh, current_mode_string(),
				m_s.vmode.textcols, m_s.vmode.textrows,
				m_s.vmode.cwidth, m_s.vmode.cheight);
			} else {
				PDEBUGF(LOG_V1, LOG_VGA, "mode: %ux%u %s\n",
					m_s.vmode.imgw, m_s.vmode.imgh, current_mode_string());
			}
		}
		
		reset_tiles();
		m_display->lock();
		m_display->set_mode(m_s.vmode);
		m_display->unlock();
		
		m_line_data_buf[0].resize(m_s.vmode.imgw);
		m_line_data_buf[1].resize(m_s.vmode.imgw);
	}
}

double VGA::current_scanline()
{
	double scanline = 0.0;
	if(m_s.frame_start_time_nsec && m_s.timings_ns.htotal != 0) {
		uint64_t display_ns = (g_machine.get_virt_time_ns() - m_s.frame_start_time_nsec)
				% m_s.timings_ns.vtotal;
		scanline = double(display_ns) / m_s.timings_ns.htotal;
	}
	return scanline;
}

double VGA::current_scanline(bool &disp_, bool &hretr_, bool &vretr_)
{
	uint64_t now = g_machine.get_virt_time_ns();

	uint64_t display_ns = 0;
	if(m_s.frame_start_time_nsec) {
		display_ns = (now - m_s.frame_start_time_nsec) % m_s.timings_ns.vtotal;
	}

	double scanline = 0.0;
	uint64_t line_ns = 0;
	if(m_s.timings_ns.htotal != 0) {
		scanline = double(display_ns) / m_s.timings_ns.htotal;
		line_ns = display_ns % m_s.timings_ns.htotal;
	}

	if(display_ns >= m_s.timings_ns.frame_end ||
	  (line_ns >= m_s.timings_ns.hbstart && line_ns <= m_s.timings_ns.hbend)) {
		disp_ = false;
	} else {
		disp_ = true;
	}

	hretr_ = false;
	if(display_ns >= m_s.timings_ns.vrstart && display_ns <= m_s.timings_ns.vrend) {
		vretr_ = true;
	} else {
		vretr_ = false;
		if(line_ns >= m_s.timings_ns.hrstart && line_ns <= m_s.timings_ns.hrend) {
			hretr_ = true;
		}
	}
	
	return scanline;
}

#if NDEBUG
#define VGA_LOG_READS
#else
#define VGA_LOG_READS true &&
#endif

uint16_t VGA::read(uint16_t _address, unsigned _io_len)
{
	UNUSED(_io_len);
	uint8_t retval = 0;

	VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "r %03Xh ", _address);

	if((_address >= 0x03b0) && (_address <= 0x03bf) && (m_s.gen_regs.misc_output.IOS)) {
		VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "mono addr in color mode -> 0xFF\n");
		return 0xff;
	}
	if((_address >= 0x03d0) && (_address <= 0x03df) && (!m_s.gen_regs.misc_output.IOS)) {
		VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "color addr in mono mode -> 0xFF\n");
		return 0xff;
	}

	switch(_address) {
		case 0x0100: // adapter ID low byte
		case 0x0101: // adapter ID high byte
		case 0x0103:
		case 0x0104:
		case 0x0105:
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "POS register %d-> 0x00\n", _address&7);
			retval = 0;
			break;

		case 0x0102: // Programmable Option Select reg 2
			// The VGA responds to a single option select byte at I/O address
			// hex 0102 and treats the LSB (bit 0) of that byte as the VGA sleep
			// bit.
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "POS register 2 -> 0x02X\n", m_s.gen_regs.video_enable);
			retval = m_s.gen_regs.video_enable;
			break;

		case 0x03ba: // Input Status 1 (monochrome emulation modes)
		case 0x03da: // Input Status 1 (color emulation modes)
		{
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "ISR 1          -> ");

			// needed to pass the VGA BIOS self-test:
			static uint8_t sys_diag = 0;
			sys_diag ^= 0x30; // bit4-5 system diagnostic
			retval |= sys_diag;

			bool disp, vret, hret;
			double scanline = current_scanline(disp, hret, vret);

			const char *mode = "disp";
			if(!disp) {
				// bit0: Display Enable
				//       0 = display is in the display mode
				//       1 = display is not in the display mode; either the
				//           horizontal or vertical retrace period is active
				retval |= 0x01;
				mode = "blank";
			}
			if(vret) {
				// bit3: Vertical Retrace
				//       0 = display is in the display mode
				//       1 = display is in the vertical retrace mode
				retval |= 0x08;
				mode = "vret";
			}

			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "0x%02X %s line=%.2f\n", retval, mode, scanline);

			// reading this port resets the attribute controller flip-flop to address mode
			m_s.attr_ctrl.flip_flop = false;
			break;
		}
		case 0x03c0: // Attribute Controller
			retval = m_s.attr_ctrl.address;
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "ATTR CTRL ADDR  -> 0%02X\n", retval);
			break;

		case 0x03c1: // Attribute Data Read
			retval = m_s.attr_ctrl;
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "ATTR CTRL[%02x]  -> 0%02X %s\n",
					m_s.attr_ctrl.address.index, retval, (const char*)m_s.attr_ctrl);
			break;

		case 0x03c2: { // Input Status Register 0
			retval = m_s.CRTC.interrupt << 7;
			// BIOS uses palette entry #0 to detect color monitor
			uint8_t sense = m_s.dac.palette[0].red & m_s.dac.palette[0].green & m_s.dac.palette[0].blue;
			retval |= (sense & 0x10);
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "ISR 0          -> 0x%02X\n", retval);
			break;
		}
		case 0x03c3: // VGA Enable Register
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "VGA ENABLE     -> 0%02X\n", m_s.gen_regs.video_enable);
			retval = m_s.gen_regs.video_enable;
			break;

		case 0x03c4: // Sequencer Address Register
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "SEQ ADDRESS    -> 0%02X\n", m_s.sequencer.address);
			retval = m_s.sequencer.address;
			break;

		case 0x03c5: // Sequencer Registers 00..04
			retval = m_s.sequencer;
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "SEQ REG[%02x]    -> 0%02X %s\n",
					m_s.sequencer.address, retval, (const char*)m_s.sequencer);
			break;

		case 0x03c6: // PEL mask
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "PEL MASK       -> 0%02X\n", m_s.dac.pel_mask);
			retval = m_s.dac.pel_mask;
			break;

		case 0x03c7: // DAC state, read = 11b, write = 00b
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "DAC STATE      -> 0%02X\n", m_s.dac.state);
			retval = m_s.dac.state;
			break;

		case 0x03c8: // PEL address write mode
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "PEL ADDRESS    -> 0%02X\n", m_s.dac.write_data_register);
			retval = m_s.dac.write_data_register;
			break;

		case 0x03c9: // PEL Data Register, colors 00..FF
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "PEL DATA");
			if(m_s.dac.state == 0x03) {
				VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "[%03u:%u]", m_s.dac.read_data_register, m_s.dac.read_data_cycle);
				switch(m_s.dac.read_data_cycle) {
					case 0:
						retval = m_s.dac.palette[m_s.dac.read_data_register].red;
						break;
					case 1:
						retval = m_s.dac.palette[m_s.dac.read_data_register].green;
						break;
					case 2:
						retval = m_s.dac.palette[m_s.dac.read_data_register].blue;
						break;
					default:
						break;
				}
				m_s.dac.read_data_cycle++;
				if(m_s.dac.read_data_cycle >= 3) {
					m_s.dac.read_data_cycle = 0;
					m_s.dac.read_data_register++;
				}
			} else {
				VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "       ");
				retval = 0x3f;
			}
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "-> 0x%02X\n", retval);
			break;

		case 0x03ca: // Feature Control
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "FEATURE CTRL   -> 0x00\n");
			break;

		case 0x03cc: // Miscellaneous Output / Graphics 1 Position
			retval = m_s.gen_regs.misc_output;
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "MISC OUTPUT    -> 0%02X [%s]\n", retval,
					(const char*)m_s.gen_regs.misc_output);
			break;

		case 0x03cd: // Graphics 2 Position
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "GFX 2 POS      -> 0x00\n");
			break;

		case 0x03ce: // Graphics Controller Address Register
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "CTRL ADDRESS   -> 0x%02X\n", m_s.gfx_ctrl.address);
			retval = m_s.gfx_ctrl.address;
			break;

		case 0x03cf: // Graphics Controller Registers 00..08
			retval = m_s.gfx_ctrl;
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "CTRL REG[%02x]    -> 0x%02X %s\n",
					m_s.gfx_ctrl.address, retval, (const char*)m_s.gfx_ctrl);
			break;

		case 0x03b4: // CRTC Address Register (monochrome emulation modes)
		case 0x03d4: // CRTC Address Register (color emulation modes)
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "CRTC ADDRESS   -> 0x%02X\n", m_s.CRTC.address);
			retval = m_s.CRTC.address;
			break;

		case 0x03b5: // CRTC Registers (monochrome emulation modes)
		case 0x03d5: // CRTC Registers (color emulation modes)
		case 0x03d1: // CGA mirror port of 3d5
		case 0x03d3: // CGA mirror port of 3d5
			retval = m_s.CRTC;
			VGA_LOG_READS PDEBUGF(LOG_V2, LOG_VGA, "CRTC REG[%02x]   -> 0x%02X %s\n",
					m_s.CRTC.address, retval, (const char*)m_s.CRTC);
			break;

		default:
			PERRF(LOG_VGA, "invalid port!\n");
			assert(false);
			break;
	}

	return retval;
}

void VGA::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	UNUSED(_io_len);

	PDEBUGF(LOG_V2, LOG_VGA, "w %03Xh ", _address);

	if((_address >= 0x03b0) && (_address <= 0x03bf) && (m_s.gen_regs.misc_output.IOS)) {
		PDEBUGF(LOG_V2, LOG_VGA, "mono emulation addr in color mode, ignored\n");
		return;
	}
	if((_address >= 0x03d0) && (_address <= 0x03df) && (!m_s.gen_regs.misc_output.IOS)) {
		PDEBUGF(LOG_V2, LOG_VGA, "color addr in mono emulation, ignored\n");
		return;
	}

	bool needs_redraw = false;
	bool charmap_update = false;

	switch(_address) {
		case 0x0102:
			m_s.gen_regs.video_enable = _value & 1;
			PDEBUGF(LOG_V2, LOG_VGA, "POS register 2 <- 0x%02X sleep=%d\n", _value, m_s.gen_regs.video_enable);
			break;
		case 0x0103:
		case 0x0104:
		case 0x0105:
			PDEBUGF(LOG_V2, LOG_VGA, "POS register %d <- 0x%02X ignored\n", _address&7, _value);
			break;

		case 0x03ba: // Feature Control (monochrome emulation modes)
			PDEBUGF(LOG_V2, LOG_VGA, "FEATURE CTRL   <- 0x%02X\n", _value);
			break;

		case 0x03c0: // Attribute Controller
			PDEBUGF(LOG_V2, LOG_VGA, "ATTR CTRL");
			if(!m_s.attr_ctrl.flip_flop) {
				// address mode
				bool prev_pal_enabled = m_s.attr_ctrl.address.IPAS;
				m_s.attr_ctrl.address = _value;
				// Bit 5 must be set to 1 for normal operation of the attribute
				// controller. This enables the video memory data to access
				// the Palette registers. Bit 5 must be set to 0 when loading
				// the Palette registers.
				if(!m_s.attr_ctrl.address.IPAS) {
					// TODO is a clear screen required?
				} else if(!prev_pal_enabled) {
					needs_redraw = true;
				}
				PDEBUGF(LOG_V2, LOG_VGA, "      <- 0x%02X Address [%s]\n",
					_value, (const char*)m_s.attr_ctrl.address);
			} else {
				// data-write mode
				PDEBUGF(LOG_V2, LOG_VGA, "[%02x]  <- 0x%02X", m_s.attr_ctrl.address.index, _value);
				if(m_s.attr_ctrl.address.index >= ATTC_REGCOUNT) {
					PDEBUGF(LOG_V2, LOG_VGA, " invalid register, ignored\n");
					return;
				}
				uint8_t oldval = m_s.attr_ctrl;
				if(_value != oldval) {
					switch(m_s.attr_ctrl.address.index) {
						case ATTC_ATTMODE: {
							if(m_s.attr_ctrl.attr_mode.ELG != (_value&ATTC_ELG)) {
								charmap_update = true;
							}
							if(m_s.attr_ctrl.attr_mode.PS != (_value&ATTC_PS)) {
								needs_redraw = true;
							}
							break;
						}
						default: {
							needs_redraw = true;
							if(m_s.attr_ctrl.address.index <= 0x0f) {
								m_stats.last_pal_line = current_scanline();
							}
							break;
						}
					}
					m_s.attr_ctrl = _value;
				}
				PDEBUGF(LOG_V2, LOG_VGA, " %s\n", (const char*)m_s.attr_ctrl);
			}
			m_s.attr_ctrl.flip_flop = !m_s.attr_ctrl.flip_flop;
			break;

		case 0x03c2: // Miscellaneous Output Register
			m_s.gen_regs.misc_output = _value;
			PDEBUGF(LOG_V2, LOG_VGA, "MISC OUTPUT    <- 0x%02X [%s]\n", _value,
				(const char*)m_s.gen_regs.misc_output
			);
			calculate_timings();
			break;

		case 0x03c3: // VGA enable
			// bit0: enables VGA display if set
			m_s.gen_regs.video_enable = _value & 0x01;
			PDEBUGF(LOG_V2, LOG_VGA, "VGA ENABLE     <- 0x%02X\n", _value);
			break;

		case 0x03c4: // Sequencer Address Register
			PDEBUGF(LOG_V2, LOG_VGA, "SEQ ADDRESS    <- 0x%02X\n", _value);
			m_s.sequencer.address = _value;
			break;

		case 0x03c5: { // Sequencer Registers
			PDEBUGF(LOG_V2, LOG_VGA, "SEQ REG[%02x]    <- 0x%02X", m_s.sequencer.address, _value);
			if(m_s.sequencer.address >= SEQ_REGCOUNT) {
				PDEBUGF(LOG_V2, LOG_VGA, " invalid register, ignored\n");
				return;
			}
			uint8_t oldval = m_s.sequencer;
			m_s.sequencer = _value;
			PDEBUGF(LOG_V2, LOG_VGA, " %s\n", (const char*)m_s.sequencer);
			switch (m_s.sequencer.address) {
				case SEQ_RESET:
					if(((oldval & SEQ_ASR) == 1) && (m_s.sequencer.reset.ASR == 0)) {
						m_s.sequencer.char_map = 0;
						m_s.charmap_address[0] = 0;
						m_s.charmap_address[1] = 0;
						charmap_update = true;
					}
					break;
				case SEQ_CLOCKING: {
					static bool prev_SO = false;
					if(bool(oldval & SEQ_SO) != m_s.sequencer.clocking.SO) {
						if(prev_SO != m_s.sequencer.clocking.SO && m_s.sequencer.clocking.SO) {
							clear_screen();
							PDEBUGF(LOG_V2, LOG_VGA, "Screen off\n");
						}
						prev_SO = m_s.sequencer.clocking.SO;
						oldval &= ~SEQ_SO;
						_value &= ~SEQ_SO;
					}
					if(_value ^ oldval) {
						calculate_timings();
					}
					break;
				}
				case SEQ_CHARMAP: {
					uint8_t charmapB = _value & (SEQ_MBL|SEQ_MBH);
					if(charmapB > 3) {
						charmapB = (charmapB & 3) + 4;
					}
					uint8_t charmapA = (_value & (SEQ_MAL|SEQ_MAH)) >> 2;
					if(charmapA > 3) {
						charmapA = (charmapA & 3) + 4;
					}
					static const uint16_t charmap_offset[8] = {
						0x0000, 0x4000, 0x8000, 0xc000,
						0x2000, 0x6000, 0xa000, 0xe000
					};
					m_s.charmap_address[0] = charmap_offset[charmapB];
					m_s.charmap_address[1] = charmap_offset[charmapA];
					charmap_update = true;
					break;
				}
			}
			break;
		}

		case 0x03c6: // PEL mask
			PDEBUGF(LOG_V2, LOG_VGA, "PEL MASK       <- 0x%02X\n", _value);
			m_s.dac.pel_mask = _value;
			// TODO This register is AND'd with the palette index sent for each dot.
			break;

		case 0x03c7: // PEL address, read mode
			PDEBUGF(LOG_V2, LOG_VGA, "PEL ADDR read  <- 0x%02X\n", _value);
			m_s.dac.read_data_register = _value;
			m_s.dac.read_data_cycle = 0;
			m_s.dac.state = 0x03;
			break;

		case 0x03c8: // PEL address write mode
			PDEBUGF(LOG_V2, LOG_VGA, "PEL ADDR write <- 0x%02X\n", _value);
			m_s.dac.write_data_register = _value;
			m_s.dac.write_data_cycle = 0;
			m_s.dac.state = 0x00;
			break;

		case 0x03c9: { // PEL Data Register, colors 00..FF
			uint8_t reg = m_s.dac.write_data_register;
			PDEBUGF(LOG_V2, LOG_VGA, "PEL DATA[%03u:%u]<- 0x%02X", reg, m_s.dac.write_data_cycle, _value);
			switch (m_s.dac.write_data_cycle) {
				case 0:
					m_s.dac.palette[reg].red = _value;
					break;
				case 1:
					m_s.dac.palette[reg].green = _value;
					break;
				case 2: {
					m_s.dac.palette[reg].blue = _value;

					uint8_t r = m_s.dac.palette[reg].red << m_s.dac_shift;
					uint8_t g = m_s.dac.palette[reg].green << m_s.dac_shift;
					uint8_t b = m_s.dac.palette[reg].blue << m_s.dac_shift;
					m_display->lock();
					m_display->palette_change(reg, r,g,b);
					m_display->unlock();
					needs_redraw = true;
					
					PDEBUGF(LOG_V2, LOG_VGA, " palette[%u] = (%u,%u,%u)", reg,r,g,b);
					m_stats.last_pal_line = current_scanline();
					break;
				}
			}
			PDEBUGF(LOG_V2, LOG_VGA, "\n");
			m_s.dac.write_data_cycle++;
			if(m_s.dac.write_data_cycle >= 3) {
				m_s.dac.write_data_cycle = 0;
				m_s.dac.write_data_register++;
			}
			break;
		}
		case 0x03ce: // Graphics Controller Address Register
			PDEBUGF(LOG_V2, LOG_VGA, "CTRL ADDRESS   <- 0x%02X\n", _value);
			m_s.gfx_ctrl.address = _value;
			break;

		case 0x03cf: // Graphics Controller Registers
			PDEBUGF(LOG_V2, LOG_VGA, "CTRL REG[%x]    <- 0x%02X ", m_s.gfx_ctrl.address, _value);
			if(m_s.gfx_ctrl.address >= GFXC_REGCOUNT) {
				PDEBUGF(LOG_V2, LOG_VGA, "invalid register, ignored\n");
				return;
			}
			if(m_s.gfx_ctrl.address == GFXC_MISC) {
				bool prev_graphics_mode = m_s.gfx_ctrl.misc.GM;
				uint8_t prev_memory_mapping = m_s.gfx_ctrl.misc.MM;

				m_s.gfx_ctrl.misc = _value;
				PDEBUGF(LOG_V2, LOG_VGA, "%s\n", (const char*)m_s.gfx_ctrl);

				switch(m_s.gfx_ctrl.misc.MM) {
					case MM_A0000_128K: // 0xA0000 .. 0xBFFFF
						m_s.gfx_ctrl.memory_offset = 0xA0000;
						m_s.gfx_ctrl.memory_aperture = 0x20000;
						break;
					case MM_A0000_64K: // 0xA0000 .. 0xAFFFF, EGA/VGA graphics modes
						m_s.gfx_ctrl.memory_offset = 0xA0000;
						m_s.gfx_ctrl.memory_aperture = 0x10000;
						break;
					case MM_B0000_32K: // 0xB0000 .. 0xB7FFF, Monochrome modes
						m_s.gfx_ctrl.memory_offset = 0xB0000;
						m_s.gfx_ctrl.memory_aperture = 0x8000;
						break;
					case MM_B8000_32K: // 0xB8000 .. 0xBFFFF, CGA modes
						m_s.gfx_ctrl.memory_offset = 0xB8000;
						m_s.gfx_ctrl.memory_aperture = 0x8000;
						break;
				}
				if(prev_memory_mapping != m_s.gfx_ctrl.misc.MM) {
					update_mem_mapping();
				}
				if(prev_memory_mapping != m_s.gfx_ctrl.misc.MM ||
				   prev_graphics_mode != m_s.gfx_ctrl.misc.GM)
				{
					calculate_timings();
					needs_redraw = true;
				}
			} else {
				if(m_s.gfx_ctrl.address == GFXC_GFX_MODE && (
					 (m_s.gfx_ctrl.gfx_mode.C256) != bool(_value & GFXC_C256)
				  || (m_s.gfx_ctrl.gfx_mode.SR)   != bool(_value & GFXC_SR)
				)) {
					PDEBUGF(LOG_V1, LOG_VGA, "mode setting: GFXC_GFX_MODE\n");
					update_video_mode();
				}

				m_s.gfx_ctrl = _value;
				PDEBUGF(LOG_V2, LOG_VGA, "%s\n", (const char*)m_s.gfx_ctrl);
			}
			break;

		case 0x03b4: // CRTC Address Register (monochrome emulation modes)
		case 0x03d4: // CRTC Address Register (color emulation modes)
		case 0x03d0: // CGA mirror port of 3d4
		case 0x03d2: // CGA mirror port of 3d4
			m_s.CRTC.address = _value;
			PDEBUGF(LOG_V2, LOG_VGA, "CRTC ADDRESS   <- 0x%02X\n", _value);
			break;

		case 0x03b5: // CRTC Registers (monochrome emulation modes)
		case 0x03d5: // CRTC Registers (color emulation modes)
		case 0x03d1: // CGA mirror port of 3d5
		case 0x03d3: // CGA mirror port of 3d5
		{
			PDEBUGF(LOG_V2, LOG_VGA, "CRTC REG[%02x]   <- 0x%02X", m_s.CRTC.address, _value);
			if(m_s.CRTC.address >= CRTC_REGCOUNT) {
				PDEBUGF(LOG_V2, LOG_VGA, " invalid register, ignored\n");
				return;
			}
			if(m_s.CRTC.is_write_protected() && (m_s.CRTC.address <= CRTC_OVERFLOW)) {
				if(m_s.CRTC.address == CRTC_OVERFLOW) {
					// The line compare bit in the Overflow register is not protected.
					m_s.CRTC.overflow.LC8 = (_value & CRTC_LC8);
					needs_redraw = true;
					PDEBUGF(LOG_V2, LOG_VGA, " %s\n", (const char*)m_s.CRTC);
					break;
				}
				PDEBUGF(LOG_V2, LOG_VGA, " (write protected)\n");
				break;
			}
			uint8_t oldvalue = m_s.CRTC;
			m_s.CRTC = _value;
			PDEBUGF(LOG_V2, LOG_VGA, " %s\n", (const char*)m_s.CRTC);
			if(_value != oldvalue) {
				switch(m_s.CRTC.address) {
					case CRTC_HTOTAL:          // 0x00
					case CRTC_HDISPLAY_END:    // 0x01
					case CRTC_START_HBLANK:    // 0x02
					case CRTC_END_HBLANK:      // 0x03
					case CRTC_START_HRETRACE:  // 0x04
					case CRTC_END_HRETRACE:    // 0x05
					case CRTC_VTOTAL:          // 0x06
					case CRTC_OVERFLOW:        // 0x07
					case CRTC_VRETRACE_START:  // 0x10
					case CRTC_VDISPLAY_END:    // 0x12
					case CRTC_START_VBLANK:    // 0x15
					case CRTC_END_VBLANK:      // 0x16
					case CRTC_MODE_CONTROL:    // 0x17
						calculate_timings();
						break;
					case CRTC_VRETRACE_END:   // 0x11
						m_s.CRTC = _value;
						if(m_s.CRTC.vretrace_end.CVI == 0) { // inverted bit
							lower_interrupt();
						}
						if((oldvalue & CRTC_VRE) != m_s.CRTC.vretrace_end.VRE) {
							calculate_timings();
						}
						break;
					case CRTC_OFFSET:          // 0x13 line offset change
					case CRTC_UNDERLINE:       // 0x14 underline (text mode) or addr shift (mode 13h)
					case CRTC_LINE_COMPARE:    // 0x18 line compare change
					case CRTC_PRESET_ROW_SCAN: // 0x08 Vertical pel panning change
						needs_redraw = true;
						break;
					case CRTC_MAX_SCANLINE:    // 0x09
						charmap_update = true;
						needs_redraw = true; // for line compare change
						if(bool(oldvalue & CRTC_VBS9) != m_s.CRTC.max_scanline.VBS9) {
							calculate_timings();
						} else if(
							   bool(oldvalue & CRTC_DSC) != m_s.CRTC.max_scanline.DSC
							||     (oldvalue & CRTC_MSL) != m_s.CRTC.max_scanline.MSL
						) {
							PDEBUGF(LOG_V1, LOG_VGA, "mode setting: CRTC_MAX_SCANLINE\n");
							update_video_mode();
						}
						break;
					case CRTC_CURSOR_START:    // 0x0A
					case CRTC_CURSOR_END:      // 0x0B
					case CRTC_CURSOR_HI:       // 0x0E
					case CRTC_CURSOR_LO:       // 0x0F
						// Cursor size / location change
						// don't redraw everything, but let the system know something changed
						m_s.needs_update = true;
						break;
					case CRTC_STARTADDR_HI:    // 0x0C
					case CRTC_STARTADDR_LO:    // 0x0D
						PDEBUGF(LOG_V2, LOG_VGA, "CRTC start address 0x%02X=%02X\n",
								m_s.CRTC.address, _value);
						m_stats.last_saddr_line = current_scanline();
						break;
				}
			}
			break;
		}

		case 0x03da: // Feature Control (color emulation modes)
			PDEBUGF(LOG_V2, LOG_VGA, "FEATURE CTRL   <- 0x%02X ignored\n", _value);
			break;

		default:
			PERRF(LOG_VGA, "invalid port\n");
			assert(false);
			break;
	}

	if(charmap_update) {
		m_display->set_text_charmap(0, &m_memory[0x20000 + m_s.charmap_address[0]]);
		m_display->set_text_charmap(1, &m_memory[0x20000 + m_s.charmap_address[1]]);
		m_display->enable_AB_charmaps(m_s.charmap_address[0] != m_s.charmap_address[1]);
		m_s.needs_update = true;
	}

	if(needs_redraw) {
		// Mark all video as updated so the changes will go through
		redraw_all();
	}
}

void VGA::raise_interrupt()
{
	if(m_s.CRTC.vretrace_end.EVI == 0) { // inverted bit
		PDEBUGF(LOG_V2, LOG_VGA, "raising IRQ %d\n", VGA_IRQ);
		m_devices->pic()->raise_irq(VGA_IRQ);
		m_s.CRTC.interrupt = true;
	}
}

void VGA::lower_interrupt()
{
	if(m_s.CRTC.interrupt) {
		m_devices->pic()->lower_irq(VGA_IRQ);
		m_s.CRTC.interrupt = false;
	}
}

void VGA::clear_screen()
{
	m_display->lock();
	m_display->clear_screen();
	m_display->set_fb_updated();
	m_display->unlock();
}

unsigned VGA::draw_gfx_ega(unsigned _scanline, std::vector<uint8_t> &line_data_)
{
	// Multiplane 16 colour mode, standard EGA/VGA format.
	// Output data in serial fashion with each display plane
	// output on its associated serial output.
	
	if(_scanline < m_s.timings.vblank_skip || _scanline > m_s.timings.last_vis_sl) {
		return 0;
	}
	
	uint16_t fb_y = _scanline - m_s.timings.vblank_skip;
	uint16_t img_y = fb_y / m_s.vmode.nscans;
	uint16_t line_y = _scanline / m_s.vmode.nscans;
	uint16_t line_compare = m_s.CRTC.latches.line_compare / m_s.vmode.nscans;
	
	uint8_t *plane[4];
	plane[0] = &m_memory[0 << m_s.plane_shift];
	plane[1] = &m_memory[1 << m_s.plane_shift];
	plane[2] = &m_memory[2 << m_s.plane_shift];
	plane[3] = &m_memory[3 << m_s.plane_shift];
	
	uint8_t pan = m_s.attr_ctrl.horiz_pel_panning;
	if((pan >= 8) || ((line_y > line_compare) && (m_s.attr_ctrl.attr_mode.PP == 1))) {
		pan = 0;
	}

	uint32_t offset;
	if(line_y > line_compare) {
		offset = ((line_y - line_compare - 1) * m_s.CRTC.latches.line_offset);
	} else {
		offset = m_s.CRTC.latches.start_address + (line_y * m_s.CRTC.latches.line_offset);
	}
	
	uint16_t tiles_updated = 0;
	uint16_t dot_x = 0;
	for(uint16_t tile = 0; tile < m_num_x_tiles; tile++) {
		if(!m_s.blink_toggle && !is_tile_dirty(img_y, tile)) {
			dot_x += VGA_X_TILESIZE;
			continue;
		}
		
		for(uint16_t tx = 0; (dot_x < m_s.vmode.imgw) && (tx < VGA_X_TILESIZE); tx++,dot_x++) {
			uint16_t pixel_x = dot_x + pan;
			uint8_t  bit_no = 7 - (pixel_x % 8);
			uint32_t byte_offset = (pixel_x / 8 + offset) % 0x10000;

			uint8_t attribute =
				(((plane[0][byte_offset] >> bit_no) & 0x01) << 0) |
				(((plane[1][byte_offset] >> bit_no) & 0x01) << 1) |
				(((plane[2][byte_offset] >> bit_no) & 0x01) << 2) |
				(((plane[3][byte_offset] >> bit_no) & 0x01) << 3);

			attribute &= m_s.attr_ctrl.color_plane_enable.ECP;
			if(m_s.attr_ctrl.attr_mode.EB) {
				// colors 0..7 high intensity, colors 8..15 blinking
				if(m_s.blink_visible) {
					attribute |= 0x08;
				} else {
					attribute &= ~0x08;
				}
			}
			uint8_t palette_reg_val = m_s.attr_ctrl.palette[attribute];
			uint8_t DAC_regno;
			if(m_s.attr_ctrl.attr_mode.PS) {
				// use 4 lower bits from palette register
				// use 4 higher bits from color select register
				// 16 banks of 16-color registers
				DAC_regno = (palette_reg_val & 0x0f) | (m_s.attr_ctrl.color_select << 4);
			} else {
				// use 6 lower bits from palette register
				// use 2 higher bits from color select register
				// 4 banks of 64-color registers
				DAC_regno = (palette_reg_val & 0x3f) | ((m_s.attr_ctrl.color_select & 0x0c) << 4);
			}
			// DAC_regno &= video DAC mask register ???
			line_data_[dot_x] = DAC_regno;
		}
		
		tiles_updated++;
	}
	
	if(m_s.blink_toggle) {
		m_display->gfx_screen_line_update(fb_y, img_y, line_data_);
		set_tiles(img_y, VGA_TILE_CLEAN);
	} else {
		m_display->gfx_screen_line_update(fb_y, img_y, line_data_, 
				&m_tile_dirty[img_y*m_num_x_tiles], m_num_x_tiles);
	}
	
	return tiles_updated * VGA_X_TILESIZE;
}

unsigned VGA::draw_gfx_vga256(unsigned _scanline, std::vector<uint8_t> &line_data_)
{
	// Packed pixel 256 colour mode.
	// Output the data eight bits at a time from the 4 bit plane
	// (format for VGA mode 13h / mode X)
	// See Abrash's Black Book chapters 47-49 for Mode X.
	
	if(_scanline < m_s.timings.vblank_skip || _scanline > m_s.timings.last_vis_sl) {
		return 0;
	}
	
	uint16_t fb_y = _scanline - m_s.timings.vblank_skip;
	uint16_t img_y = fb_y / m_s.vmode.nscans;
	uint16_t line_y = _scanline / m_s.vmode.nscans;
	uint16_t line_compare = m_s.CRTC.latches.line_compare / m_s.vmode.nscans;

	uint8_t pan = m_s.attr_ctrl.horiz_pel_panning;
	if((pan >= 8) || ((line_y > line_compare) && (m_s.attr_ctrl.attr_mode.PP == 1))) {
		pan = 0;
	}
	const uint8_t mode13_pan_values[8] = { 0,0,1,0,2,0,3,0 };
	pan = mode13_pan_values[pan];
	
	uint16_t start_address;
	if(m_s.CRTC.underline.DW) {
		// DW set: doubleword mode
		start_address = m_s.CRTC.latches.start_address * 4;
		if(m_s.gen_regs.misc_output.PAGE != 1) {
			PDEBUGF(LOG_V2, LOG_VGA, "update: PAGE != 1\n");
		}
	} else if(m_s.CRTC.mode_control.WB) {
		// Word/Byte set: byte mode, mode X
		start_address = m_s.CRTC.latches.start_address;
	} else {
		// word mode
		start_address = m_s.CRTC.latches.start_address * 2;
	}
	
	uint32_t offset;
	if(line_y > line_compare) {
		offset = ((line_y - line_compare - 1) * m_s.CRTC.latches.line_offset);
	} else {
		offset = start_address + (line_y * m_s.CRTC.latches.line_offset);
	}
	
	uint16_t tiles_updated = 0;
	uint16_t dot_x = 0;
	for(uint16_t tile = 0; tile < m_num_x_tiles; tile++) {
		if(!is_tile_dirty(img_y, tile)) {
			dot_x += VGA_X_TILESIZE;
			continue;
		}
		for(uint16_t tx = 0; (dot_x < m_s.vmode.imgw) && (tx < VGA_X_TILESIZE); tx++, dot_x++) {
			uint32_t pixel_x = dot_x + pan;
			uint32_t plane = (pixel_x % 4);
			uint32_t byte_offset = (plane * 65536) + offset;
			if(m_s.CRTC.underline.DW) {
				// DW set: doubleword mode
				byte_offset += (pixel_x & ~0x03);
			} else if(m_s.CRTC.mode_control.WB) {
				// Word/Byte set: byte mode, mode X
				byte_offset += (pixel_x >> 2);
			} else {
				// word mode
				byte_offset += ((pixel_x >> 1) & ~0x01);
			}
			uint8_t DACreg = m_memory[byte_offset % m_memsize];
			
			line_data_[dot_x] = DACreg;
		}
		tiles_updated++;
	}
	
	m_display->gfx_screen_line_update(fb_y, img_y, line_data_, 
			&m_tile_dirty[img_y*m_num_x_tiles], m_num_x_tiles);
	
	return tiles_updated * VGA_X_TILESIZE;
}

unsigned VGA::draw_gfx_cga2(unsigned _scanline, std::vector<uint8_t> &line_data_)
{
	// CGA compatibility mode, 640x200 2 color (mode 6)
	
	if(_scanline < m_s.timings.vblank_skip || _scanline > m_s.timings.last_vis_sl) {
		return 0;
	}
	
	uint8_t pan = m_s.attr_ctrl.horiz_pel_panning;
	if(pan >= 8) {
		pan = 0;
	}
	
	uint16_t fb_y = _scanline - m_s.timings.vblank_skip;
	uint16_t img_y = fb_y / m_s.vmode.nscans;
	uint16_t line_y = _scanline >> m_s.CRTC.max_scanline.DSC;
	
	uint16_t tiles_updated = 0;
	uint16_t dot_x = 0;
	for(uint16_t tile = 0; tile < m_num_x_tiles; tile++) {
		if(!is_tile_dirty(img_y, tile)) {
			dot_x += VGA_X_TILESIZE;
			continue;
		}
		for(uint16_t tx = 0; (dot_x < m_s.vmode.imgw) && (tx < VGA_X_TILESIZE); tx++, dot_x++) {
			uint32_t pixel_x = dot_x + pan;
			// 0 or 0x2000
			unsigned byte_offset = m_s.CRTC.latches.start_address + ((line_y & 1) << 13);
			// to the start of the line
			byte_offset += (320 / 4) * (line_y / 2);
			// to the byte start
			byte_offset += (pixel_x / 8);
	
			unsigned bit_no = 7 - (pixel_x % 8);
			uint8_t palette_reg_val = (((m_memory[byte_offset%m_memsize]) >> bit_no) & 1);
			
			line_data_[dot_x] = m_s.attr_ctrl.palette[palette_reg_val];
		}
		tiles_updated++;
	}
	
	m_display->gfx_screen_line_update(fb_y, img_y, line_data_,
			&m_tile_dirty[img_y*m_num_x_tiles], m_num_x_tiles);
	
	return tiles_updated * VGA_X_TILESIZE;
}

unsigned VGA::draw_gfx_cga4(unsigned _scanline, std::vector<uint8_t> &line_data_)
{
	// Packed pixel 4 colour mode.
	// Output the data in a CGA-compatible 320x200 4 color graphics
	// mode (planar shift, modes 4 & 5).
	
	if(_scanline < m_s.timings.vblank_skip || _scanline > m_s.timings.last_vis_sl) {
		return 0;
	}

	uint8_t pan = m_s.attr_ctrl.horiz_pel_panning;
	if(pan >= 8) {
		pan = 0;
	}
	
	uint16_t fb_y = _scanline - m_s.timings.vblank_skip;
	uint16_t img_y = fb_y / m_s.vmode.nscans;
	uint16_t line_y = _scanline >> m_s.CRTC.max_scanline.DSC;
	
	uint16_t tiles_updated = 0;
	uint16_t dot_x = 0;
	for(uint16_t tile = 0; tile < m_num_x_tiles; tile++) {
		if(!is_tile_dirty(img_y, tile)) {
			dot_x += VGA_X_TILESIZE;
			continue;
		}
		for(uint16_t tx = 0; (dot_x < m_s.vmode.imgw) && (tx < VGA_X_TILESIZE); tx++,dot_x++) {
			uint32_t pixel_x = dot_x + pan;
			// 0 or 0x2000
			unsigned byte_offset = m_s.CRTC.latches.start_address + ((line_y & 1) << 13);
			// to the start of the line
			byte_offset += (320 / 4) * (line_y / 2);
			// to the byte start
			byte_offset += (pixel_x / 4);
	
			uint8_t attribute = 6 - 2*(pixel_x % 4);
			uint8_t palette_reg_val = (m_memory[byte_offset%m_memsize]) >> attribute;
			palette_reg_val &= 3;
			
			uint8_t DAC_regno = m_s.attr_ctrl.palette[palette_reg_val];
			
			line_data_[dot_x] = DAC_regno;
		}
		tiles_updated++;
	}
	
	m_display->gfx_screen_line_update(fb_y, img_y, line_data_,
			&m_tile_dirty[img_y*m_num_x_tiles], m_num_x_tiles);
	
	return tiles_updated * VGA_X_TILESIZE;
}

unsigned VGA::gfx_update_thread(int _thread_id, int _thread_pool_size)
{
	unsigned pix_updated = 0;

	for(unsigned scanline = m_s.timings.vblank_skip+_thread_id;
		scanline <= m_s.timings.last_vis_sl;
		scanline += _thread_pool_size)
	{
		pix_updated += (this->*m_renderer)(scanline, m_line_data_buf[_thread_id]);
	}
	
	return pix_updated;
}

void VGA::text_update()
{
	// this version works only if called at frame_end
	
	if((m_s.vmode.textrows * m_s.CRTC.latches.line_offset) > (1 << 17)) {
		PDEBUGF(LOG_V0, LOG_VGA, "update(): text mode: out of memory\n");
		return;
	}

	TextModeInfo tm_info;
	tm_info.start_address = 2 * m_s.CRTC.latches.start_address;
	tm_info.cs_start = m_s.CRTC.cursor_start;
	if(!m_s.blink_visible) {
		tm_info.cs_start |= CRTC_CO;
	}
	tm_info.cs_end = m_s.CRTC.cursor_end.RSCE;
	tm_info.line_offset = m_s.CRTC.latches.line_offset;
	tm_info.line_compare = m_s.CRTC.latches.line_compare;
	tm_info.h_panning = m_s.attr_ctrl.horiz_pel_panning;
	tm_info.v_panning = m_s.CRTC.preset_row_scan.SRS;
	tm_info.line_graphics = m_s.attr_ctrl.attr_mode.ELG;
	tm_info.split_hpanning = m_s.attr_ctrl.attr_mode.PP;
	tm_info.double_dot = m_s.sequencer.clocking.DC;
	tm_info.double_scanning = m_s.CRTC.max_scanline.DSC;
	tm_info.blink_flags = 0;
	if(m_s.attr_ctrl.attr_mode.EB) {
		tm_info.blink_flags |= TEXT_BLINK_MODE;
		if(m_s.blink_toggle) {
			tm_info.blink_flags |= TEXT_BLINK_TOGGLE;
		}
		if(m_s.blink_visible) {
			tm_info.blink_flags |= TEXT_BLINK_STATE;
		}
	}
	if(m_s.sequencer.clocking.D89 == 0) {
		if(tm_info.h_panning >= 8) {
			tm_info.h_panning = 0;
		} else {
			tm_info.h_panning++;
		}
	} else {
		tm_info.h_panning &= 0x07;
	}
	for(int index = 0; index < 16; index++) {
		tm_info.actl_palette[index] = m_s.attr_ctrl.palette[index];
	}

	// pass old text snapshot & new VGA memory contents
	unsigned cursor_address = 2 * m_s.CRTC.latches.cursor_location;
	unsigned cursor_x, cursor_y;
	if(cursor_address < tm_info.start_address) {
		cursor_x = 0xffff;
		cursor_y = 0xffff;
	} else {
		cursor_x = ((cursor_address - tm_info.start_address)/2) % m_s.vmode.textcols;
		cursor_y = ((cursor_address - tm_info.start_address)/2) / m_s.vmode.textcols;
	}
	m_display->text_update(m_s.text_snapshot, &m_memory[tm_info.start_address], cursor_x, cursor_y, &tm_info);
	if(m_s.needs_update) {
		// screen updated, copy new video memory contents into text snapshot
		memcpy(m_s.text_snapshot, &m_memory[tm_info.start_address], tm_info.line_offset*m_s.vmode.textrows);
	}
}

void VGA::horiz_disp_end(uint64_t _time)
{
	// this is horizontal display end
	// called only during per-line rendering in gfx mode
	
	UNUSED(_time);
	
	m_display->lock();
	
	if(m_renderer) {
		m_cur_upd_pix += (this->*m_renderer)(m_s.scanline, m_line_data_buf[0]);
	}
	
	m_s.scanline++;
	
	if(m_s.scanline > m_s.timings.last_vis_sl) {
		m_stats.updated_pix = m_cur_upd_pix;
		m_s.scanline = -1;
		
		// the distance to the next scan line is (current time + hborders + hblanking + hretracing)
		// the start of the next scan line is equivalent to m_s.timings_ns.last_vis_sl;
		uint32_t frame_start_dist = g_machine.get_virt_time_ns() - m_s.frame_start_time_nsec;
		uint32_t vretr_dist = 10_us;
		if(m_s.timings_ns.vrstart > frame_start_dist) {
			vretr_dist = m_s.timings_ns.vrstart - frame_start_dist;
		}
		g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::vertical_retrace,this,_1), VGA_VERTICAL_RETRACE);
		g_machine.activate_timer(m_timer_id, vretr_dist, false);
		
		m_display->set_fb_updated();
	}
	
	m_display->unlock();
}

void VGA::frame_start(uint64_t _time)
{
	// this is time 0, the start of the 1st scan line
	
	m_s.frame_start_time_nsec = _time;
	m_cur_upd_pix = 0;
	m_stats.frame_cnt++;
	
	if(THREADS_WAIT && g_program.threads_sync()) {
		m_display->wait();
	}
	
	// update cursor/blinking status for this frame
	m_s.blink_toggle = false;
	m_s.blink_counter--;
	if(m_s.blink_counter == 0) {
		m_s.blink_counter = VGA_BLINK_COUNTER;
		if((m_s.vmode.mode == VGA_M_TEXT) || (m_s.attr_ctrl.attr_mode.EB)) {
			m_s.blink_toggle = true;
			m_s.blink_visible = !m_s.blink_visible;
		} else {
			m_s.blink_toggle = false;
			m_s.blink_visible = false;
		}
	}

	if(m_s.render_mode == VGA_RENDER_LINE) {
		if(!is_video_disabled()) {
			// enable per-line rendering, 1 update for every line at hdend
			m_s.scanline = m_s.timings.vblank_skip; // skip top blank area
			uint32_t first_upd_dist = m_s.scanline * m_s.timings_ns.htotal + m_s.timings_ns.hdend;
			g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::horiz_disp_end,this,_1), VGA_HORIZ_DISP_END);
			g_machine.activate_timer(m_timer_id, first_upd_dist, m_s.timings_ns.htotal, true);
		} else {
			g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::vertical_retrace,this,_1), VGA_VERTICAL_RETRACE);
			g_machine.activate_timer(m_timer_id, m_s.timings_ns.vrstart, false);
		}
	} else {
		// enable per-frame rendering
		g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::frame_end,this,_1), VGA_FRAME_END);
		g_machine.activate_timer(m_timer_id, m_s.timings_ns.frame_end, false);
	}
}

void VGA::frame_end(uint64_t _time)
{
	// this is the beginning of the next scanline after the last visible one
	// it's called only during per-frame rendering
	
	UNUSED(_time);

	if(!is_video_disabled() && (m_s.needs_update || 
		((m_s.vmode.mode==VGA_M_EGA || m_s.vmode.mode==VGA_M_TEXT) && m_s.blink_toggle))
	)
	{
		m_display->lock();
		if(m_s.vmode.mode == VGA_M_TEXT) {
			text_update();
		} else {
			std::future<uint32_t> w0 = std::async(std::launch::async, [&]() {
				return gfx_update_thread(0, 2);
			});
			uint32_t w1 = gfx_update_thread(1, 2);
			w0.wait();
			m_stats.updated_pix = (w0.get() + w1);
		}
		m_display->set_fb_updated();
		m_display->unlock();
		m_s.needs_update = false;
	} else {
		m_stats.updated_pix = 0;
	}
	
	uint64_t dist = 10_us;
	if(m_s.timings_ns.vrstart > m_s.timings_ns.frame_end) {
		dist = m_s.timings_ns.vrstart - m_s.timings_ns.frame_end;
	}
	
	g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::vertical_retrace,this,_1), VGA_VERTICAL_RETRACE);
	g_machine.activate_timer(m_timer_id, dist, false);
}

void VGA::vertical_retrace(uint64_t _time)
{
	// this is vertical retrace start
	
	UNUSED(_time);
	
	if(m_s.CRTC.vretrace_end.EVI==0 && !is_video_disabled()) { // EVI is an inverted bit
		raise_interrupt();
	}

	// the start address is latched at vretrace
	uint16_t oldvalue = m_s.CRTC.latches.start_address;
	m_s.CRTC.latch_start_address();
	if(m_s.CRTC.latches.start_address != oldvalue) {
		set_all_tiles(VGA_TILE_DIRTY);
		m_s.needs_update = true;
	}
	
	uint64_t dist = 10_us;
	if(m_s.timings_ns.vtotal > m_s.timings_ns.vrstart) {
		dist = m_s.timings_ns.vtotal - m_s.timings_ns.vrstart;
	}
	
	g_machine.set_timer_callback(m_timer_id, std::bind(&VGA::frame_start,this,_1), VGA_FRAME_START);
	g_machine.activate_timer(m_timer_id, dist, false);
}

template<>
uint32_t VGA::s_mem_read<uint8_t>(uint32_t _addr, void *_priv)
{
	VGA &me = *(VGA*)_priv;

	assert(_addr >= me.m_s.gfx_ctrl.memory_offset && _addr < me.m_s.gfx_ctrl.memory_offset + me.m_s.gfx_ctrl.memory_aperture);

	uint32_t offset = _addr & (me.m_s.gfx_ctrl.memory_aperture - 1);

	if(me.m_s.sequencer.mem_mode.CH4) {
		// chained pixel representation
		return me.m_memory[(offset & ~0x03) + (offset % 4)*65536];
	}

	uint8_t *plane0 = &me.m_memory[(0 << me.m_s.plane_shift) + me.m_s.plane_offset];
	uint8_t *plane1 = &me.m_memory[(1 << me.m_s.plane_shift) + me.m_s.plane_offset];
	uint8_t *plane2 = &me.m_memory[(2 << me.m_s.plane_shift) + me.m_s.plane_offset];
	uint8_t *plane3 = &me.m_memory[(3 << me.m_s.plane_shift) + me.m_s.plane_offset];

	// addr between 0xA0000 and 0xAFFFF
	switch(me.m_s.gfx_ctrl.gfx_mode.RM) {
		case 0: // read mode 0
		{
			me.m_s.gfx_ctrl.latch[0] = plane0[offset];
			me.m_s.gfx_ctrl.latch[1] = plane1[offset];
			me.m_s.gfx_ctrl.latch[2] = plane2[offset];
			me.m_s.gfx_ctrl.latch[3] = plane3[offset];

			return me.m_s.gfx_ctrl.latch[me.m_s.gfx_ctrl.read_map_select];
		}
		case 1: // read mode 1
		{
			uint8_t latch0, latch1, latch2, latch3;

			latch0 = me.m_s.gfx_ctrl.latch[0] = plane0[offset];
			latch1 = me.m_s.gfx_ctrl.latch[1] = plane1[offset];
			latch2 = me.m_s.gfx_ctrl.latch[2] = plane2[offset];
			latch3 = me.m_s.gfx_ctrl.latch[3] = plane3[offset];

			latch0 ^= me.m_s.gfx_ctrl.color_compare.CC0 * 0xff;
			latch1 ^= me.m_s.gfx_ctrl.color_compare.CC1 * 0xff;
			latch2 ^= me.m_s.gfx_ctrl.color_compare.CC2 * 0xff;
			latch3 ^= me.m_s.gfx_ctrl.color_compare.CC3 * 0xff;

			latch0 &= me.m_s.gfx_ctrl.color_dont_care.M0X * 0xff;
			latch1 &= me.m_s.gfx_ctrl.color_dont_care.M1X * 0xff;
			latch2 &= me.m_s.gfx_ctrl.color_dont_care.M2X * 0xff;
			latch3 &= me.m_s.gfx_ctrl.color_dont_care.M3X * 0xff;

			uint8_t retval = ~(latch0 | latch1 | latch2 | latch3);
			return retval;
		}
	}

	return 0;
}

template<>
void VGA::s_mem_write<uint8_t>(uint32_t _addr, uint32_t _value, void *_priv)
{
	VGA &me = *(VGA*)_priv;

	assert((_addr >= me.m_s.gfx_ctrl.memory_offset) && (_addr < me.m_s.gfx_ctrl.memory_offset + me.m_s.gfx_ctrl.memory_aperture));

	uint32_t offset = _addr & (me.m_s.gfx_ctrl.memory_aperture - 1);
	
	switch(me.m_s.vmode.mode) {
		case VGA_M_CGA2:
		case VGA_M_CGA4: {
			// 0xB8000 .. 0xBFFFF
			// CGA 320x200x4 / 640x200x2
			me.m_memory[offset] = _value;

			offset -= me.m_s.CRTC.latches.start_address;
			unsigned x_tileno, x_tileno2, y_tileno;
			if(offset >= 0x2000) {
				x_tileno = ((offset - 0x2000) % (320/4)) * 4;
				y_tileno = ((offset - 0x2000) / (320/4)) * 2 + 1;
			} else {
				x_tileno = (offset % (320/4)) * 4;
				y_tileno = (offset / (320/4)) * 2;
			}
			x_tileno2 = x_tileno;
			if(me.m_s.gfx_ctrl.gfx_mode.SR == 0) {
				x_tileno *= 2;
				x_tileno2 += 7;
			} else {
				x_tileno2 += 3;
			}

			x_tileno  /= VGA_X_TILESIZE;
			x_tileno2 /= VGA_X_TILESIZE;

			me.set_tile(y_tileno, x_tileno, VGA_TILE_DIRTY);
			if(x_tileno2 != x_tileno) {
				me.set_tile(y_tileno, x_tileno2, VGA_TILE_DIRTY);
			}
			me.m_s.needs_update = true;
			return;
		}
		case VGA_M_256COL:
		case VGA_M_EGA:
			// addr between 0xA0000 and 0xAFFFF
			unsigned planes, pels_per_byte, x_tileno, y_tileno;
			if(me.m_s.sequencer.mem_mode.CH4) {
				me.mem_write_chain4(offset, _value);
				planes = 1;
			} else {
				if(me.m_s.sequencer.map_mask == 0) {
					return;
				}
				me.mem_write_planar(offset, _value);
				planes = 4;
			}
			if(me.m_s.vmode.mode == VGA_M_256COL) {
				pels_per_byte = 1;
			} else {
				pels_per_byte = 2;
			}
			if(me.m_s.CRTC.latches.line_offset > 0) {
				if(me.m_s.CRTC.latches.line_compare < me.m_s.CRTC.latches.vdisplay_end) {
					x_tileno = ((offset % me.m_s.CRTC.latches.line_offset) * (planes*pels_per_byte)) / VGA_X_TILESIZE;
					y_tileno = ((offset / me.m_s.CRTC.latches.line_offset) * me.m_s.vmode.nscans + 
							(me.m_s.CRTC.latches.line_compare - me.m_s.timings.vblank_skip)
							+ 1);
					if(y_tileno < me.m_s.vmode.yres && x_tileno < me.m_num_x_tiles) {
						me.set_tile(y_tileno/me.m_s.vmode.nscans, x_tileno, VGA_TILE_DIRTY);
						me.m_s.needs_update = true;
					}
				}
				if(offset >= me.m_s.CRTC.latches.start_address) {
					offset -= me.m_s.CRTC.latches.start_address;
					x_tileno = ((offset % me.m_s.CRTC.latches.line_offset) * (planes*pels_per_byte)) / VGA_X_TILESIZE;
					y_tileno = (offset / me.m_s.CRTC.latches.line_offset);
					if(y_tileno < me.m_s.vmode.imgh && x_tileno < me.m_num_x_tiles) {
						me.set_tile(y_tileno, x_tileno, VGA_TILE_DIRTY);
						me.m_s.needs_update = true;
					}
				}
			} else {
				// TODO fix this
				// a program that set line_offset to 0 is Copper ('92 demo).
				me.set_all_tiles(VGA_TILE_DIRTY);
				me.m_s.needs_update = true;
			}
			break;
		case VGA_M_TEXT:
			if(me.m_s.sequencer.map_mask == 0) {
				return;
			}
			me.mem_write_planar(offset, _value);
			me.m_s.needs_update = true;
			break;
		default:
			break;
	}
}

void VGA::mem_write_chain4(uint32_t _offset, uint8_t _value)
{
	m_memory[(_offset & ~0x03) + (_offset % 4)*65536] = _value;
}

void VGA::mem_write_planar(uint32_t _offset, uint8_t _value)
{
	uint8_t *plane0 = &m_memory[(0 << m_s.plane_shift) + m_s.plane_offset];
	uint8_t *plane1 = &m_memory[(1 << m_s.plane_shift) + m_s.plane_offset];
	uint8_t *plane2 = &m_memory[(2 << m_s.plane_shift) + m_s.plane_offset];
	uint8_t *plane3 = &m_memory[(3 << m_s.plane_shift) + m_s.plane_offset];

	uint8_t new_val[4] = {0,0,0,0};

	switch (m_s.gfx_ctrl.gfx_mode.WM) {
		case 0:
		{
			// Write Mode 0
			// Each memory map is written with the system data rotated by the count
			// in the Data Rotate register. If the set/reset function is enabled for a
			// specific map, that map receives the 8-bit value contained in the
			// Set/Reset register.
			const uint8_t bitmask = m_s.gfx_ctrl.bitmask;
			const bool sr0 = m_s.gfx_ctrl.set_reset.SR0;
			const bool sr1 = m_s.gfx_ctrl.set_reset.SR1;
			const bool sr2 = m_s.gfx_ctrl.set_reset.SR2;
			const bool sr3 = m_s.gfx_ctrl.set_reset.SR3;
			const bool esr0 = m_s.gfx_ctrl.enable_set_reset.ESR0;
			const bool esr1 = m_s.gfx_ctrl.enable_set_reset.ESR1;
			const bool esr2 = m_s.gfx_ctrl.enable_set_reset.ESR2;
			const bool esr3 = m_s.gfx_ctrl.enable_set_reset.ESR3;
			// perform rotate on CPU data in case its needed
			if(m_s.gfx_ctrl.data_rotate.ROTC) {
				_value = (_value >> m_s.gfx_ctrl.data_rotate.ROTC) |
				         (_value << (8 - m_s.gfx_ctrl.data_rotate.ROTC));
			}
			new_val[0] = m_s.gfx_ctrl.latch[0] & ~bitmask;
			new_val[1] = m_s.gfx_ctrl.latch[1] & ~bitmask;
			new_val[2] = m_s.gfx_ctrl.latch[2] & ~bitmask;
			new_val[3] = m_s.gfx_ctrl.latch[3] & ~bitmask;
			switch (m_s.gfx_ctrl.data_rotate.FS) {
				case 0: // replace
					new_val[0] |= (esr0 ? (sr0 ? bitmask : 0) : (_value & bitmask));
					new_val[1] |= (esr1 ? (sr1 ? bitmask : 0) : (_value & bitmask));
					new_val[2] |= (esr2 ? (sr2 ? bitmask : 0) : (_value & bitmask));
					new_val[3] |= (esr3 ? (sr3 ? bitmask : 0) : (_value & bitmask));
					break;
				case 1: // AND
					new_val[0] |= esr0 ? (sr0 ? (m_s.gfx_ctrl.latch[0] & bitmask) : 0)
					                   : (_value & m_s.gfx_ctrl.latch[0]) & bitmask;
					new_val[1] |= esr1 ? (sr1 ? (m_s.gfx_ctrl.latch[1] & bitmask) : 0)
					                   : (_value & m_s.gfx_ctrl.latch[1]) & bitmask;
					new_val[2] |= esr2 ? (sr2 ? (m_s.gfx_ctrl.latch[2] & bitmask) : 0)
					                   : (_value & m_s.gfx_ctrl.latch[2]) & bitmask;
					new_val[3] |= esr3 ? (sr3 ? (m_s.gfx_ctrl.latch[3] & bitmask) : 0)
					                   : (_value & m_s.gfx_ctrl.latch[3]) & bitmask;
					break;
				case 2: // OR
					new_val[0] |= esr0 ? (sr0 ? bitmask : (m_s.gfx_ctrl.latch[0] & bitmask))
					                   : ((_value | m_s.gfx_ctrl.latch[0]) & bitmask);
					new_val[1] |= esr1 ? (sr1 ? bitmask : (m_s.gfx_ctrl.latch[1] & bitmask))
					                   : ((_value | m_s.gfx_ctrl.latch[1]) & bitmask);
					new_val[2] |= esr2 ? (sr2 ? bitmask : (m_s.gfx_ctrl.latch[2] & bitmask))
					                   : ((_value | m_s.gfx_ctrl.latch[2]) & bitmask);
					new_val[3] |= esr3 ? (sr3 ? bitmask : (m_s.gfx_ctrl.latch[3] & bitmask))
					                   : ((_value | m_s.gfx_ctrl.latch[3]) & bitmask);
					break;
				case 3: // XOR
					new_val[0] |= esr0 ? (sr0 ? (~m_s.gfx_ctrl.latch[0] & bitmask)
					                          : (m_s.gfx_ctrl.latch[0] & bitmask))
					                   : (_value ^ m_s.gfx_ctrl.latch[0]) & bitmask;
					new_val[1] |= esr1 ? (sr1 ? (~m_s.gfx_ctrl.latch[1] & bitmask)
					                          : (m_s.gfx_ctrl.latch[1] & bitmask))
					                   : (_value ^ m_s.gfx_ctrl.latch[1]) & bitmask;
					new_val[2] |= esr2 ? (sr2 ? (~m_s.gfx_ctrl.latch[2] & bitmask)
					                          : (m_s.gfx_ctrl.latch[2] & bitmask))
					                   : (_value ^ m_s.gfx_ctrl.latch[2]) & bitmask;
					new_val[3] |= esr3 ? (sr3 ? (~m_s.gfx_ctrl.latch[3] & bitmask)
					                          : (m_s.gfx_ctrl.latch[3] & bitmask))
					                   : (_value ^ m_s.gfx_ctrl.latch[3]) & bitmask;
					break;
			}
			break;
		}

		case 1:
		{
			// Write Mode 1
			// Each memory map is written with the contents of the system latches.
			// These latches are loaded by a system read operation.
			for(int i=0; i<4; i++) {
				new_val[i] = m_s.gfx_ctrl.latch[i];
			}
			break;
		}

		case 2:
		{
			// Write Mode 2
			// Memory map n (0 through 3) is filled with 8 bits of the value of data
			// bit n.
			const uint8_t bitmask = m_s.gfx_ctrl.bitmask;
			const bool p0 = (_value & 1);
			const bool p1 = (_value & 2);
			const bool p2 = (_value & 4);
			const bool p3 = (_value & 8);
			new_val[0] = m_s.gfx_ctrl.latch[0] & ~bitmask;
			new_val[1] = m_s.gfx_ctrl.latch[1] & ~bitmask;
			new_val[2] = m_s.gfx_ctrl.latch[2] & ~bitmask;
			new_val[3] = m_s.gfx_ctrl.latch[3] & ~bitmask;
			switch (m_s.gfx_ctrl.data_rotate.FS) {
				case 0: // write
					new_val[0] |= p0 ? bitmask : 0;
					new_val[1] |= p1 ? bitmask : 0;
					new_val[2] |= p2 ? bitmask : 0;
					new_val[3] |= p3 ? bitmask : 0;
					break;
				case 1: // AND
					new_val[0] |= p0 ? (m_s.gfx_ctrl.latch[0] & bitmask) : 0;
					new_val[1] |= p1 ? (m_s.gfx_ctrl.latch[1] & bitmask) : 0;
					new_val[2] |= p2 ? (m_s.gfx_ctrl.latch[2] & bitmask) : 0;
					new_val[3] |= p3 ? (m_s.gfx_ctrl.latch[3] & bitmask) : 0;
					break;
				case 2: // OR
					new_val[0] |= p0 ? bitmask : (m_s.gfx_ctrl.latch[0] & bitmask);
					new_val[1] |= p1 ? bitmask : (m_s.gfx_ctrl.latch[1] & bitmask);
					new_val[2] |= p2 ? bitmask : (m_s.gfx_ctrl.latch[2] & bitmask);
					new_val[3] |= p3 ? bitmask : (m_s.gfx_ctrl.latch[3] & bitmask);
					break;
				case 3: // XOR
					new_val[0] |= p0 ? (~m_s.gfx_ctrl.latch[0] & bitmask) : (m_s.gfx_ctrl.latch[0] & bitmask);
					new_val[1] |= p1 ? (~m_s.gfx_ctrl.latch[1] & bitmask) : (m_s.gfx_ctrl.latch[1] & bitmask);
					new_val[2] |= p2 ? (~m_s.gfx_ctrl.latch[2] & bitmask) : (m_s.gfx_ctrl.latch[2] & bitmask);
					new_val[3] |= p3 ? (~m_s.gfx_ctrl.latch[3] & bitmask) : (m_s.gfx_ctrl.latch[3] & bitmask);
					break;
			}
			break;
		}

		case 3:
		{
			// Write Mode 3
			// Each memory map is written with the 8-bit value contained in the
			// Set/Reset register for that map (the Enable Set/Reset register has no
			// effect). Rotated system data is ANDed with the Bit Mask register to
			// form an 8-bit value that performs the same function as the Bit Mask
			// register in write modes 0 and 2
			const uint8_t bitmask = m_s.gfx_ctrl.bitmask & _value;
			const uint8_t v0 = m_s.gfx_ctrl.set_reset.SR0 ? _value : 0;
			const uint8_t v1 = m_s.gfx_ctrl.set_reset.SR1 ? _value : 0;
			const uint8_t v2 = m_s.gfx_ctrl.set_reset.SR2 ? _value : 0;
			const uint8_t v3 = m_s.gfx_ctrl.set_reset.SR3 ? _value : 0;

			// perform rotate on CPU data
			if(m_s.gfx_ctrl.data_rotate.ROTC) {
				_value = (_value >> m_s.gfx_ctrl.data_rotate.ROTC) |
				         (_value << (8 - m_s.gfx_ctrl.data_rotate.ROTC));
			}
			new_val[0] = m_s.gfx_ctrl.latch[0] & ~bitmask;
			new_val[1] = m_s.gfx_ctrl.latch[1] & ~bitmask;
			new_val[2] = m_s.gfx_ctrl.latch[2] & ~bitmask;
			new_val[3] = m_s.gfx_ctrl.latch[3] & ~bitmask;

			_value &= bitmask;

			switch (m_s.gfx_ctrl.data_rotate.FS) {
				case 0: // write
					new_val[0] |= v0;
					new_val[1] |= v1;
					new_val[2] |= v2;
					new_val[3] |= v3;
					break;
				case 1: // AND
					new_val[0] |= v0 & m_s.gfx_ctrl.latch[0];
					new_val[1] |= v1 & m_s.gfx_ctrl.latch[1];
					new_val[2] |= v2 & m_s.gfx_ctrl.latch[2];
					new_val[3] |= v3 & m_s.gfx_ctrl.latch[3];
					break;
				case 2: // OR
					new_val[0] |= v0 | m_s.gfx_ctrl.latch[0];
					new_val[1] |= v1 | m_s.gfx_ctrl.latch[1];
					new_val[2] |= v2 | m_s.gfx_ctrl.latch[2];
					new_val[3] |= v3 | m_s.gfx_ctrl.latch[3];
					break;
				case 3: // XOR
					new_val[0] |= v0 ^ m_s.gfx_ctrl.latch[0];
					new_val[1] |= v1 ^ m_s.gfx_ctrl.latch[1];
					new_val[2] |= v2 ^ m_s.gfx_ctrl.latch[2];
					new_val[3] |= v3 ^ m_s.gfx_ctrl.latch[3];
					break;
			}
			break;
		}

	}

	// planes update
	if(m_s.sequencer.map_mask.M0E) {
		plane0[_offset] = new_val[0];
	}
	if(m_s.sequencer.map_mask.M1E) {
		plane1[_offset] = new_val[1];
	}
	if(m_s.sequencer.map_mask.M2E) {
		if(!m_s.gfx_ctrl.misc.GM) {
			uint32_t mapaddr = _offset & 0xe000;
			if(mapaddr == m_s.charmap_address[0] || mapaddr == m_s.charmap_address[1]) {
				m_display->lock();
				m_display->set_text_charbyte(
						(mapaddr == m_s.charmap_address[0])?0:1,
						(_offset & 0x1fff), new_val[2]);
				m_display->unlock();
			}
		}
		plane2[_offset] = new_val[2];
	}
	if(m_s.sequencer.map_mask.M3E) {
		plane3[_offset] = new_val[3];
	}
}

const char * VGA::current_mode_string()
{
	switch(m_s.vmode.mode) {
		case VGA_M_CGA2:
			return "CGA 2-Color";
		case VGA_M_CGA4:
			return "CGA 4-Color";
		case VGA_M_EGA:
			return "EGA/VGA 16-Color";
		case VGA_M_256COL:
			if(m_s.sequencer.mem_mode.CH4) {
				return "VGA 256-Color Chain 4";
			}
			return "VGA 256-Color Planar";
		case VGA_M_TEXT:
			return "TEXT";
		default:
			return "unknown mode";
	}
}

void VGA::redraw_all()
{
	m_s.needs_update = true;

	if(m_s.vmode.mode == VGA_M_TEXT) {
		// text mode
		memset(m_s.text_snapshot, 0, sizeof(m_s.text_snapshot));
	} else {
		// graphics mode
		set_all_tiles(VGA_TILE_DIRTY);
	}
}

void VGA::state_to_textfile(std::string _filepath)
{
	// this function is called by the GUI thread
	auto file = FileSys::make_file(_filepath.c_str(), "w");
	if(!file.get()) {
		PERRF(LOG_VGA, "cannot open %s for write\n", _filepath.c_str());
		throw std::exception();
	}

	fprintf(file.get(),
		"mode = %ux%u %s\n"
		"screen = %ux%u\n"
		"  horiz total = %u chars\n"
		"  horiz disp end = char %u\n"
		"  horiz blank start = char %u\n"
		"  horiz blank end = char %u\n"
		"  horiz retr start = char %u\n"
		"  horiz retr end = char %u\n"
		"  horiz freq = %.2f kHz\n"
		"  horiz borders = left:%u right:%u\n"
		"  vert total = %u lines\n"
		"  vert display end = line %u\n"
		"  vert blank start = line %u\n"
		"  vert blank end = line %u\n"
		"  vert blank skip = %u lines\n"
		"  vert retrace start = line %u\n"
		"  vert retrace end = line %u\n"
		"  vert freq = %.2f Hz\n"
		"  vert borders = top:%u bottom:%u\n"
		"\n",

		m_s.vmode.imgw, m_s.vmode.imgh,
		current_mode_string(),
		m_s.vmode.xres, m_s.vmode.yres,

		m_s.timings.htotal,
		m_s.timings.hdend,
		m_s.timings.hbstart,
		m_s.timings.hbend,
		m_s.timings.hrstart,
		m_s.timings.hrend,
		m_s.timings.hfreq,
		m_s.vmode.borders.left, m_s.vmode.borders.right,

		m_s.timings.vtotal,
		m_s.timings.vdend,
		m_s.timings.vbstart,
		m_s.timings.vbend,
		m_s.timings.vblank_skip,
		m_s.timings.vrstart,
		m_s.timings.vrend,
		m_s.timings.vfreq,
		m_s.vmode.borders.top, m_s.vmode.borders.bottom
	);

	fprintf(file.get(), "Timings (nsec)\n");
	fprintf(file.get(), "%*u  Horizontal Total\n",          8, m_s.timings_ns.htotal);
	fprintf(file.get(), "%*u  Horizontal Blank Start\n",    8, m_s.timings_ns.hbstart);
	fprintf(file.get(), "%*u  Horizontal Blank End\n",      8, m_s.timings_ns.hbend);
	fprintf(file.get(), "%*u  Horizontal Retrace Start\n",  8, m_s.timings_ns.hrstart);
	fprintf(file.get(), "%*u  Horizontal Retrace End\n",    8, m_s.timings_ns.hrend);
	fprintf(file.get(), "%*u  Vertical Total\n",            8, m_s.timings_ns.vtotal);
	fprintf(file.get(), "%*u  Vertical Display End\n",      8, m_s.timings_ns.vdend);
	fprintf(file.get(), "%*u  Vertical Retrace Start\n",    8, m_s.timings_ns.vrstart);
	fprintf(file.get(), "%*u  Vertical Retrace End\n",      8, m_s.timings_ns.vrend);
	fprintf(file.get(), "%*u  Frame end\n",                 8, m_s.timings_ns.frame_end);

	fprintf(file.get(), "\nGeneral registers\n");
	m_s.gen_regs.registers_to_textfile(file.get());

	fprintf(file.get(), "\nSequencer\n");
	m_s.sequencer.registers_to_textfile(file.get());

	fprintf(file.get(), "\nCRT Controller\n");
	m_s.CRTC.registers_to_textfile(file.get());
	fprintf(file.get(), "        Latches\n");
	fprintf(file.get(), "0x%04X %*u Line Offset (10-bit)\n", m_s.CRTC.latches.line_offset, 5, m_s.CRTC.latches.line_offset);
	fprintf(file.get(), "0x%04X %*u Line Compare target (10-bit)\n", m_s.CRTC.latches.line_compare, 5, m_s.CRTC.latches.line_compare);
	fprintf(file.get(), "0x%04X %*u Vertical Retrace Start (10-bit)\n", m_s.CRTC.latches.vretrace_start, 5, m_s.CRTC.latches.vretrace_start);
	fprintf(file.get(), "0x%04X %*u Vertical Display Enable End (10-bit)\n", m_s.CRTC.latches.vdisplay_end, 5, m_s.CRTC.latches.vdisplay_end);
	fprintf(file.get(), "0x%04X %*u Vertical Total (10-bit)\n", m_s.CRTC.latches.vtotal, 5, m_s.CRTC.latches.vtotal);
	fprintf(file.get(), "0x%04X %*u End Horizontal Blanking (6-bit)\n", m_s.CRTC.latches.end_hblank, 5, m_s.CRTC.latches.end_hblank);
	fprintf(file.get(), "0x%04X %*u Start Vertical Blanking (10-bit)\n", m_s.CRTC.latches.start_vblank, 5, m_s.CRTC.latches.start_vblank);
	fprintf(file.get(), "0x%04X %*u Start Address (16-bit) \n", m_s.CRTC.latches.start_address, 5, m_s.CRTC.latches.start_address);
	fprintf(file.get(), "0x%04X %*u Cursor Location (16-bit)\n", m_s.CRTC.latches.cursor_location, 5, m_s.CRTC.latches.cursor_location);

	fprintf(file.get(), "\nGraphics Controller\n");
	m_s.gfx_ctrl.registers_to_textfile(file.get());

	fprintf(file.get(), "\nAttribute Controller\n");
	m_s.attr_ctrl.registers_to_textfile(file.get());

	fprintf(file.get(), "\nDigital-to-Analog Converter\n");
	m_s.dac.registers_to_textfile(file.get());
}

