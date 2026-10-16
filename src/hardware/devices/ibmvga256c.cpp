/*
 * Copyright (C) 2017-2026  Marco Bortolin
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

/* Bare bones emulation of the integrated IBM VGA256C video adapter.
 * 
 * The point of this emulator is to: 
 *  1. allow the POST procedure to succeed without errors.
 *  2. enable SVGA graphics modes using the official IBM VESA TSR drivers.
 *
 * About the IBM 84F7985 (= TI CF62011BPC) IBM VGA256C:
 * "there's no official documentation for this chip but it appears to be an XGA
 * on a 16bit bus. It probably lacks any coprocessor functionality but it
 * implements the Extended Graphics mode and has a VESA DOS driver which allows
 * video modes like 640x480 256-colors and 132-column x 25-row text when at
 * least 512KB of video memory is installed. No official Windows 3 drivers are
 * known to exist but the one for the IBM PS/2 Thinkpad 700C/720C can be used."
 * 
 * More here: https://www.ardent-tool.com/video/SVGA_Chipset.html
 * 
 * VESA mode 109h (text 132-column x 25-row) is broken on the PS/1 2121 (tested
 * on real HW). It was present in VESA drivers v1.0, removed in v1.5, and then
 * back again in v2.1 (but still broken).
 * 
 * This chip is used, with RAMDACs of different speeds, in the following
 * systems:
 * 
 *  - IBM PS/1 2121 as IBM 84F7985 (= TI CF62011BPC)
 *  - IBM PS/1 Pro 2123 as IBM 84F7985
 *  - IBM PS/2 8525SX as IBM 84F7985
 *  - IBM PS/2 8535SX as IBM 84F7985
 *  - IBM PS/2 8540SX as IBM 84F7985
 *  - IBM PS/2 8557SX as IBM 84F7985
 *  - IBM PS/2 Thinkpad 700C/720C as IBM 39G8600 (§)
 *  - IBM SVGA Adapter/A as IBM 42G3410 (= TI CF63011PCM)
 *  
 *  § On every site about the Thinkpad 700C/720C (including Wikipedia) it is
 *  reported that the chip used is the WD90C26, but the official IBM drivers are
 *  for the IBM VGA256C, which appears to be a completely different beast.
 *  The IBM 39G8600 looks like the IBM VGA256C with an integrated RAMDAC.
 */

#include "ibmulator.h"
#include "ibmvga256c.h"
#include "hardware/memory.h"
#include "hardware/devices.h"
#include "filesys.h"
#include <cstring>

IODEVICE_PORTS(IBMVGA256C) = {
	{ 0x2100, 0x210F, PORT_8BIT|PORT_RW }
};


IBMVGA256C::IBMVGA256C(Devices *_dev)
: VGA(_dev)
{
}

IBMVGA256C::~IBMVGA256C()
{
}

void IBMVGA256C::install()
{
	VGA::install();
	IODevice::install(&VGA::ioports()->at(0), VGA::ioports()->size());
}

void IBMVGA256C::remove()
{
	VGA::remove();
	IODevice::remove(&VGA::ioports()->at(0), VGA::ioports()->size());
}

void IBMVGA256C::reset(unsigned _type)
{
	if(_type == MACHINE_POWER_ON || _type == MACHINE_HARD_RESET) {
		// Registers are not initialized by soft reset.
		m_s = {};
		m_s.disp_ctrl.op_mode = 0x01; // VGA Mode (address decode enabled)
		m_s.disp_ctrl.ap_ctrl = 0x01; // aperture 64KB at address 0xA0000
		m_s.disp_ctrl.ap_idx = 0x00;  // bank 0

		memset(m_s.ext_gfx.regs, 0xFF, 0x7F);
		m_s.ext_gfx.regs[IBM_DISP_CTRL] = 0;
		m_s.ext_gfx.regs[IBM_CLOCK_SEL] = 0;
	}

	VGA::reset(_type);
}

void IBMVGA256C::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "%s: saving state\n", name());

	_state.write(&m_s, {sizeof(m_s), name()});

	VGA::save_state(_state);
}

void IBMVGA256C::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_VGA, "%s: restoring state\n", name());

	_state.read(&m_s, {sizeof(m_s), name()});

	VGA::restore_state(_state);
}

double IBMVGA256C::clock_freq_hz() const
{
	// from the VESA v2.1 driver
	switch(m_s.ext_gfx.regs[IBM_CLOCK_SEL] & 0x3) {
		case 0:
			if(VGA::m_s.gen_regs.misc_output.CS == 3) {
				// 1024x768 43Hz
				return 44900000.0;
			}
			return VGA::clock_freq_hz();
		case 2:
			// 640x400 84Hz, 88Hz
			// 640x480 72Hz, 75Hz
			return 31500000.0;
		case 3:
			if(VGA::m_s.gen_regs.misc_output.CS == 0) {
				// 800x600 56Hz
				return 36000000.0;
			}
			// 800x600 60Hz
			return 40000000.0;
		case 1: // ???
		default:
			return VGA::clock_freq_hz();
	}
}

void IBMVGA256C::update_mem_mapping()
{
	if(m_s.disp_ctrl.op_mode.DM & XGA_OPMODE_EXT) {
		g_memory.resize_mapping(m_mem_mapping,
			VGA::m_s.memory_offset,
			VGA::m_s.memory_aperture);
		g_memory.set_mapping_funcs(m_mem_mapping,
			IBMVGA256C::s_mem_read<uint8_t>,
			IBMVGA256C::s_mem_read<uint16_t>,
			nullptr,
			this,
			IBMVGA256C::s_mem_write<uint8_t>,
			IBMVGA256C::s_mem_write<uint16_t>,
			nullptr,
			this);
		PDEBUGF(LOG_V1, LOG_VGA, "XGA: memory mapping: 0x%X .. 0x%X\n",
				VGA::m_s.memory_offset,
				VGA::m_s.memory_offset + VGA::m_s.memory_aperture - 1);
	} else {
		VGA::update_mem_mapping();
	}
}

template<class T>
uint32_t IBMVGA256C::s_mem_read(uint32_t _addr, void *_priv)
{
	IBMVGA256C *me = static_cast<IBMVGA256C*>(_priv);

	if(!me->VGA::m_s.sequencer.mem_mode.CH4) {
		return VGA::s_mem_read<T>(_addr, _priv);
	}

	if(_addr + sizeof(T) > me->VGA::m_s.memory_offset + me->VGA::m_s.memory_aperture) {
		PDEBUGF(LOG_V3, LOG_VGA, "XGA: mem r%d 0x%04X (out of window)\n",
				sizeof(T), _addr);
		return T(0xFFFFFFFF);
	}

	uint32_t addr = me->VGA::m_s.memory_bank * me->VGA::m_s.memory_aperture 
			+ _addr - me->VGA::m_s.memory_offset;

	if(addr + sizeof(T) > me->m_memsize) {
		PDEBUGF(LOG_V3, LOG_VGA, "XGA: mem r%d 0x%04X (0x%04X) (out of memory)\n",
				sizeof(T), _addr, addr);
		return T(0xFFFFFFFF);
	}

	T value = *(T*)(&me->m_memory[addr]);

	PDEBUGF(LOG_V3, LOG_VGA, "XGA: mem r%d 0x%04X (0x%04X) -> 0x%04X\n",
			sizeof(T), _addr, addr, value);

	return value;
}

template<class T>
void IBMVGA256C::s_mem_write(uint32_t _addr, uint32_t _value, void *_priv)
{
	IBMVGA256C *me = static_cast<IBMVGA256C*>(_priv);

	if(!me->VGA::m_s.sequencer.mem_mode.CH4) {
		VGA::s_mem_write<T>(_addr, _value, _priv);
		return;
	}

	T value = T(_value);

	if(_addr + sizeof(T) > me->VGA::m_s.memory_offset + me->VGA::m_s.memory_aperture) {
		PDEBUGF(LOG_V3, LOG_VGA, "XGA: mem w%d 0x%04X <- 0x%04X (out of window)\n",
				sizeof(T), _addr, value);
		return;
	}

	uint32_t addr = me->VGA::m_s.memory_bank * me->VGA::m_s.memory_aperture
			+ _addr - me->VGA::m_s.memory_offset;

	if(addr + sizeof(T) > me->m_memsize) {
		PDEBUGF(LOG_V3, LOG_VGA, "XGA: mem w%d 0x%04X (0x%04X) <- 0x%04X (out of memory)\n",
				sizeof(T), _addr, addr, value);
		return;
	}

	PDEBUGF(LOG_V3, LOG_VGA, "XGA: mem w%d 0x%04X (0x%04X) <- 0x%X\n",
			sizeof(T), _addr, addr, value);

	*(T*)(&me->m_memory[addr]) = value;

	// TODO FIXME full redraw
	me->VGA::m_s.needs_update = true;
	me->VGA::m_s.force_redraw = 2;
}

uint16_t IBMVGA256C::read(uint16_t _address, unsigned _io_len)
{
	uint16_t retval = 0xFF;

	if(_address < 0x2100) {
		if((m_s.disp_ctrl.op_mode.DM & 1) || _address <= 0x105) {
			return VGA::read(_address, _io_len);
		} else {
			return retval;
		}
	}

	PDEBUGF(LOG_V2, LOG_VGA, "XGA: r %04Xh %s -> ", _address, XGA_DispCtrl::regnames[_address & 0xF]);

	// Registers 21x2-21x7,21x9 are STUB

	switch(_address & 0xF) {
		case 0x0: // 21x0 Operating Mode
			retval = m_s.disp_ctrl.op_mode;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", retval,
					(const char *)m_s.disp_ctrl.op_mode);
			break;
		case 0x1: // 21x1 Aperture Control
			retval = m_s.disp_ctrl.ap_ctrl;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", retval,
					(const char *)m_s.disp_ctrl.ap_ctrl);
			break;
		case 0x2: // 21x2 Reserved
		case 0x3: // 21x3 Reserved
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X\n", retval);
			break;
		case 0x4: // 21x4 Interrupt Enable
			retval = m_s.disp_ctrl.int_en;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", retval,
					(const char *)m_s.disp_ctrl.int_en);
			break;
		case 0x5: // 21x5 Interrupt Status
			retval = m_s.disp_ctrl.int_status;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", retval,
					(const char *)m_s.disp_ctrl.int_status);
			break;
		case 0x6: // 21x6 Virtual Memory Control
			retval = m_s.disp_ctrl.vm_ctrl;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", retval,
					(const char *)m_s.disp_ctrl.vm_ctrl);
			break;
		case 0x7: // 21x7 Virtual Memory Interrupt Status
			retval = m_s.disp_ctrl.vm_int;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", retval,
					(const char *)m_s.disp_ctrl.vm_int);
			break;
		case 0x8: // 21x8 Aperture Index
			retval = m_s.disp_ctrl.ap_idx;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", retval,
					(const char *)m_s.disp_ctrl.ap_idx);
			break;
		case 0x9: // 21x9 Memory Access Mode
			retval = m_s.disp_ctrl.mem_mode;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", retval,
					(const char *)m_s.disp_ctrl.mem_mode);
			break;
		case 0xA: // 21xA Extended Graphics mode register index
			retval = m_s.ext_gfx.index;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X %s\n", retval,
					m_s.ext_gfx.register_name(retval));
			break;
		case 0xB: // 21xB Data
		case 0xC: // 21xC Data
		case 0xD: // 21xD Data
		case 0xE: // 21xE Data
		case 0xF: // 21xF Data
			retval = m_s.ext_gfx.get_register();
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X %s\n", retval,
					(const char *)m_s.ext_gfx);
			break;
		default:
			assert(false);
			break;
	}

	return retval;
}

void IBMVGA256C::write(uint16_t _address, uint16_t _value, unsigned _io_len)
{
	if(_address < 0x2100) {
		if((m_s.disp_ctrl.op_mode.DM & 1) || _address <= 0x105) {
			VGA::write(_address, _value, _io_len);
		}
		return;
	}

	PDEBUGF(LOG_V2, LOG_VGA, "XGA: w %04Xh %s <- ",
			_address, XGA_DispCtrl::regnames[_address & 0xF]);

	// Registers 21x2-21x7,21x9 are STUB

	switch(_address & 0xF) {
		case 0x0: { // 21x0 Operating Mode
			uint8_t old_dm = m_s.disp_ctrl.op_mode.DM;
			m_s.disp_ctrl.op_mode = _value;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n",
					_value, (const char*)m_s.disp_ctrl.op_mode);
			if(old_dm != m_s.disp_ctrl.op_mode.DM) {
				if(m_s.disp_ctrl.op_mode.DM & XGA_OPMODE_EXT) {
					PDEBUGF(LOG_V1, LOG_VGA, "XGA: Extended Graphics mode (VGA address %s)\n",
							m_s.disp_ctrl.op_mode.DM & 1 ? "enabled" : "disabled");
					// Extended graphics mode.
					// DM values 5,6,7 are reported as Reserved but the VESA
					// driver sets DM to 7, reads it back and fails if it's != 5.
					// I'm speculating that:
					//  bit 2 = Extended mode on/off
					//  bit 1 = 132-cols text mode on/off
					//  bit 0 = Address decoding on/off
					// and then 132-cols is forced off if extended mode is enabled.
					m_s.disp_ctrl.op_mode.DM &= XGA_OPMODE_VGA | XGA_OPMODE_EXT;
					m_s.disp_ctrl.get_memory_conf(
						VGA::m_s.memory_offset,
						VGA::m_s.memory_aperture,
						VGA::m_s.memory_bank
					);
				} else if(m_s.disp_ctrl.op_mode.DM & XGA_OPMODE_132) {
					PWARNF(LOG_V1, LOG_VGA, "XGA: 132-Column text video mode (VGA address %s)\n",
							m_s.disp_ctrl.op_mode.DM & 1 ? "enabled" : "disabled");
				} else {
					PDEBUGF(LOG_V1, LOG_VGA, "XGA: VGA mode (address %s)\n",
							m_s.disp_ctrl.op_mode.DM & 1 ? "enabled" : "disabled");
					VGA::m_s.gfx_ctrl.get_memory_conf(
						VGA::m_s.memory_offset,
						VGA::m_s.memory_aperture,
						VGA::m_s.memory_bank
					);
				}
				update_mem_mapping();
				calculate_timings();
			}
			break;
		}
		case 0x1: { // 21x1 Aperture Control
			uint8_t old_val = m_s.disp_ctrl.ap_ctrl;
			m_s.disp_ctrl.ap_ctrl = _value;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", _value, (const char*)m_s.disp_ctrl.ap_ctrl);
			if(old_val != m_s.disp_ctrl.ap_ctrl && (m_s.disp_ctrl.op_mode.DM & XGA_OPMODE_EXT)) {
				m_s.disp_ctrl.get_memory_conf(
					VGA::m_s.memory_offset,
					VGA::m_s.memory_aperture,
					VGA::m_s.memory_bank
				);
				update_mem_mapping();
			}
			break;
		}
		case 0x2: // 21x2 Reserved
		case 0x3: // 21x3 Reserved
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X\n", _value);
			break;
		case 0x4: { // 21x4 Interrupt Enable
			m_s.disp_ctrl.int_en = _value;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", _value, (const char*)m_s.disp_ctrl.int_en);
			break;
		}
		case 0x5: { // 21x5 Interrupt Status
			if(_value & XGA_INT_CC) {
				m_s.disp_ctrl.int_status.CC = 0;
			}
			if(_value & XGA_INT_CR) {
				m_s.disp_ctrl.int_status.CR = 0;
			}
			if(_value & XGA_INT_SC) {
				m_s.disp_ctrl.int_status.SC = 0;
			}
			if(_value & XGA_INT_SP) {
				m_s.disp_ctrl.int_status.SP = 0;
			}
			if(_value & XGA_INT_SB) {
				m_s.disp_ctrl.int_status.SB = 0;
			}
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", _value, (const char*)m_s.disp_ctrl.int_status);
			break;
		}
		case 0x6: { // 21x6 Virtual Memory Control
			m_s.disp_ctrl.vm_ctrl = _value;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", _value, (const char*)m_s.disp_ctrl.vm_ctrl);
			break;
		}
		case 0x7: { // 21x7 Virtual Memory Interrupt Status
			if(_value & XGA_VMINT_NPS) {
				m_s.disp_ctrl.vm_int.NPS = 0;
			}
			if(_value & XGA_VMINT_PVS) {
				m_s.disp_ctrl.vm_int.PVS = 0;
			}
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", _value, (const char*)m_s.disp_ctrl.vm_int);
			break;
		}
		case 0x8: { // 21x8 Aperture Index
			m_s.disp_ctrl.ap_idx = _value;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", _value, (const char*)m_s.disp_ctrl.ap_idx);
			if(m_s.disp_ctrl.op_mode.DM & XGA_OPMODE_EXT) {
				VGA::m_s.memory_bank = m_s.disp_ctrl.ap_idx;
			}
			break;
		}
		case 0x9: { // 21x9 Memory Access Mode
			uint8_t old_value = m_s.disp_ctrl.mem_mode;
			m_s.disp_ctrl.mem_mode = _value;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X [%s]\n", _value, (const char*)m_s.disp_ctrl.mem_mode);
			if(old_value != m_s.disp_ctrl.mem_mode) {
				update_video_mode();
			}
			break;
		}
		case 0xA: { // 21xA Index register
			m_s.ext_gfx.index = _value & 0x7F;
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X (%s)\n", _value, m_s.ext_gfx.register_name());
			break;
		}
		case 0xB: // 21xB Data
		case 0xC: // 21xC Data
		case 0xD: // 21xD Data
		case 0xE: // 21xE Data
		case 0xF: // 21xF Data
		{
			m_s.ext_gfx.set_register(_value);
			PDEBUGF(LOG_V2, LOG_VGA, "0x%02X %s\n", _value, m_s.ext_gfx.register_to_string());
			if(m_s.ext_gfx.index == IBM_CLOCK_SEL) {
				calculate_timings();
			}
			break;
		}
	}
}

void IBMVGA256C::update_rendering_mode()
{
	if(m_s.disp_ctrl.op_mode.DM & XGA_OPMODE_EXT) {
		VGA::m_s.vmode.imgw = VGA::m_s.vmode.xres;
		VGA::m_s.vmode.imgh = VGA::m_s.vmode.yres;
		VGA::m_s.vmode.imgw >>= VGA::m_s.sequencer.clocking.DC;
		VGA::m_s.vmode.imgh /= VGA::m_s.CRTC.scanlines_div();
		VGA::m_s.vmode.nscans = VGA::m_s.CRTC.scanlines_div();
		VGA::m_s.vmode.ndots = 1 << VGA::m_s.sequencer.clocking.DC;
		if(VGA::m_s.vmode.nscans > 1) {
			VGA::m_s.render_mode = VGA_RENDER_LINE;
		} else {
			VGA::m_s.render_mode = VGA_RENDER_FRAME;
		}
		if(VGA::m_s.gfx_ctrl.gfx_mode.C256 == 1) {
			// Packed 256 colour
			VGA::m_s.vmode.mode = VGA_M_SVGA8;
			m_renderer = static_cast<VGADrawFn>(&IBMVGA256C::draw_gfx_svga8);
		} else if(VGA::m_s.sequencer.mem_mode.CH4 == 0) {
			// Planar 16 colour
			VGA::m_s.vmode.mode = VGA_M_EGA;
			m_renderer = static_cast<VGADrawFn>(&IBMVGA256C::draw_gfx_ega);
		} else {
			PWARNF(LOG_V0, LOG_VGA, "XGA: unsupported Extended Graphics mode.\n");
			VGA::update_rendering_mode();
		}
	} else {
		VGA::update_rendering_mode();
	}
}

unsigned IBMVGA256C::draw_gfx_svga8(unsigned _scanline, uint32_t _row_addr_cnt, std::vector<uint8_t> &line_data_)
{
	// Extended Mode Packed pixel 256 colour mode.

	if(_scanline < VGA::m_s.timings.vblank_skip || _scanline > VGA::m_s.timings.last_vis_sl) {
		return 0;
	}

	if(UNLIKELY(is_video_blanked())) {
		return draw_blank_line(_scanline);
	}

	const uint16_t fb_y = _scanline - VGA::m_s.timings.vblank_skip;

	unsigned img_pixels = 0;
	uint16_t img_x = 0;

	// it seems to always be DW addressing, no matter the CRTC settings
	const uint32_t byte_offset = _row_addr_cnt << 2;
	const uint8_t pan = VGA::m_s.attr_ctrl.horiz_pel_panning & 0x7;

	for(uint16_t tile = 0; tile < m_num_x_tiles; tile++) {
		if(!is_tile_dirty(fb_y, tile)) {
			img_x += VGA_X_TILESIZE;
			continue;
		}
		for(uint16_t tile_x = 0;
			(img_x < VGA::m_s.vmode.imgw) && (tile_x < VGA_X_TILESIZE);
			tile_x++, img_x++)
		{
			uint32_t pix_location = byte_offset + img_x + pan;
			if(UNLIKELY(pix_location >= m_memsize)) {
				tile = m_num_x_tiles;
				break;
			}
			line_data_[img_x] = m_memory[pix_location];
		}
		img_pixels += VGA_X_TILESIZE;
	}
	
	m_display->gfx_screen_line_update(fb_y, line_data_, 
			&m_tile_dirty[uint32_t(fb_y*m_num_x_tiles)], m_num_x_tiles);

	unsigned ovsc_pixels = 0;
	if(VGA::m_s.force_redraw_overs) {
		ovsc_pixels = m_display->overscan_screen_line_update(fb_y, m_display->overscan_color_rgb());
	}

	return img_pixels + ovsc_pixels;
}

void IBMVGA256C::state_to_textfile(std::string _filepath)
{
	// this function is called by the GUI thread
	VGA::state_to_textfile(_filepath);

	auto file = FileSys::make_file(_filepath.c_str(), "a");
	if(file.get() == nullptr) {
		PERRF(LOG_VGA, "cannot open %s for update\n", _filepath.c_str());
		throw std::exception();
	}

	fprintf(file.get(), "\nXGA registers\n");

	auto pr = [&](uint8_t _i, uint8_t _v, const char *_s) {
		fprintf(file.get(), "0x210%X 0x%02X %03u  %s [%s]\n",
				_i, _v, _v, XGA_DispCtrl::regnames[_i], _s);
	};
	pr(0x0, m_s.disp_ctrl.op_mode, (const char*)m_s.disp_ctrl.op_mode);
	pr(0x1, m_s.disp_ctrl.ap_ctrl, (const char*)m_s.disp_ctrl.ap_ctrl);
	pr(0x4, m_s.disp_ctrl.int_en, (const char*)m_s.disp_ctrl.int_en);
	pr(0x5, m_s.disp_ctrl.int_status, (const char*)m_s.disp_ctrl.int_status);
	pr(0x6, m_s.disp_ctrl.vm_ctrl, (const char*)m_s.disp_ctrl.vm_ctrl);
	pr(0x7, m_s.disp_ctrl.vm_int, (const char*)m_s.disp_ctrl.vm_int);
	pr(0x8, m_s.disp_ctrl.ap_idx, (const char*)m_s.disp_ctrl.ap_idx);
	pr(0x9, m_s.disp_ctrl.mem_mode, (const char*)m_s.disp_ctrl.mem_mode);
	pr(0xA, m_s.ext_gfx.index, std::to_string(m_s.ext_gfx.index).c_str());
	fprintf(file.get(), "Extended Graphics Registers\n");
	fprintf(file.get(), m_s.ext_gfx.registers_to_string().c_str());
}



const char * IBMVGA256C_ExtGfx::register_name(uint8_t _index)
{
	switch(_index & 0x7F) {
		case IBM_DISP_CTRL: return "Display Control 1 ?";
		case IBM_CLOCK_SEL: return "Clock Frequency Select ?";
	}
	return "Unknown";
}

const char * IBMVGA256C_ExtGfx::register_to_string(uint8_t _index)
{
	thread_local static std::string s;

	s = register_name(_index);
	switch(_index & 0x7F) {
		case IBM_DISP_CTRL: 
			s += std::string(" [") +
				::register_to_string(regs[IBM_DISP_CTRL],
					{{2,"DB"},{1,""},{1,"SO"},{1,"VE"},{1,""},{2,"SP"}}) +
				"]";
			break;
		case IBM_CLOCK_SEL:
			s += std::string(" [") +
				::register_to_string(regs[IBM_CLOCK_SEL], {{3,"CS"}}) +
				"]";
		default:
			break;
	}

	return s.c_str();
}

const std::string & IBMVGA256C_ExtGfx::registers_to_string()
{
	thread_local static std::string s;
	s = "";
	for(int i=0; i<0x7F; i++) {
		s += str_format("0x%02X 0x%02X %03u  %s\n", i, regs[i], regs[i], register_to_string(i));
	}
	return s;
}
