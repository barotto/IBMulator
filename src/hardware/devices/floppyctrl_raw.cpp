/*
 * Copyright (C) 2002-2014  The Bochs Project
 * Copyright (C) 2015-2022  Marco Bortolin
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
 * Intel 82077AA Floppy Disk Controller.
 * Basic implementation, mostly based on Bochs' code with fixes and additions,
 * like non-DMA mode. Only for IBM formatted raw sector-based disk images.
 */

#include "ibmulator.h"
#include "program.h"
#include "dma.h"
#include "pic.h"
#include "hardware/devices/systemboard.h"
#include "floppyctrl_raw.h"
#include "floppyfmt_img.h"
#include "floppyfmt_imd.h"

const std::map<unsigned,FloppyCtrl_Raw::CmdDef> FloppyCtrl_Raw::ms_cmd_list = {
	{ FDC_CMD_READ        , {FDC_CMD_READ        , 9, "read data",          &FloppyCtrl_Raw::cmd_read_data} },
	{ FDC_CMD_READ_DEL    , {FDC_CMD_READ_DEL    , 9, "read deleted data",  &FloppyCtrl_Raw::cmd_not_implemented} },
	{ FDC_CMD_WRITE       , {FDC_CMD_WRITE       , 9, "write data",         &FloppyCtrl_Raw::cmd_write_data} },
	{ FDC_CMD_WRITE_DEL   , {FDC_CMD_WRITE_DEL   , 9, "write deleted data", &FloppyCtrl_Raw::cmd_not_implemented} },
	{ FDC_CMD_READ_TRACK  , {FDC_CMD_READ_TRACK  , 9, "read track",         &FloppyCtrl_Raw::cmd_not_implemented} },
	{ FDC_CMD_VERIFY      , {FDC_CMD_VERIFY      , 9, "verify",             &FloppyCtrl_Raw::cmd_not_implemented} },
	{ FDC_CMD_VERSION     , {FDC_CMD_VERSION     , 1, "version",            &FloppyCtrl_Raw::cmd_version} },
	{ FDC_CMD_FORMAT_TRACK, {FDC_CMD_FORMAT_TRACK, 6, "format track",       &FloppyCtrl_Raw::cmd_format_track} },
	{ FDC_CMD_SCAN_EQ     , {FDC_CMD_SCAN_EQ     , 9, "scan equal",         &FloppyCtrl_Raw::cmd_not_implemented} },
	{ FDC_CMD_SCAN_LO_EQ  , {FDC_CMD_SCAN_LO_EQ  , 9, "scan low or equal",  &FloppyCtrl_Raw::cmd_not_implemented} },
	{ FDC_CMD_SCAN_HI_EQ  , {FDC_CMD_SCAN_HI_EQ  , 9, "scan high or equal", &FloppyCtrl_Raw::cmd_not_implemented} },
	{ FDC_CMD_RECALIBRATE , {FDC_CMD_RECALIBRATE , 2, "recalibrate",        &FloppyCtrl_Raw::cmd_recalibrate} },
	{ FDC_CMD_SENSE_INT   , {FDC_CMD_SENSE_INT   , 1, "sense interrupt",    &FloppyCtrl_Raw::cmd_sense_int} },
	{ FDC_CMD_SPECIFY     , {FDC_CMD_SPECIFY     , 3, "specify",            &FloppyCtrl_Raw::cmd_specify} },
	{ FDC_CMD_SENSE_DRIVE , {FDC_CMD_SENSE_DRIVE , 2, "sense drive status", &FloppyCtrl_Raw::cmd_sense_drive} },
	{ FDC_CMD_CONFIGURE   , {FDC_CMD_CONFIGURE   , 4, "configure",          &FloppyCtrl_Raw::cmd_configure} },
	{ FDC_CMD_SEEK        , {FDC_CMD_SEEK        , 3, "seek",               &FloppyCtrl_Raw::cmd_seek} },
	{ FDC_CMD_DUMPREG     , {FDC_CMD_DUMPREG     , 1, "dumpreg",            &FloppyCtrl_Raw::cmd_dumpreg} },
	{ FDC_CMD_READ_ID     , {FDC_CMD_READ_ID     , 2, "read ID",            &FloppyCtrl_Raw::cmd_read_id} },
	{ FDC_CMD_PERP_MODE   , {FDC_CMD_PERP_MODE   , 2, "perpendicular mode", &FloppyCtrl_Raw::cmd_perp_mode} },
	{ FDC_CMD_LOCK        , {FDC_CMD_LOCK        , 1, "lock/unlock",        &FloppyCtrl_Raw::cmd_lock} },
	{ FDC_CMD_INVALID     , {FDC_CMD_INVALID     , 1, "INVALID COMMAND",    &FloppyCtrl_Raw::cmd_invalid} }
};

#define FDC_ST_HDS(DRIVE) ((m_s.flopi[DRIVE].head<<2) | DRIVE)
#define FDC_DOR_DRIVE(DRIVE) ((m_s.DOR & 0xFC) | DRIVE)


FloppyCtrl_Raw::FloppyCtrl_Raw(Devices *_dev) 
: FloppyCtrl(_dev)
{
	m_floppy_formats.emplace_back(new FloppyFmt_IMG());
	m_floppy_formats.emplace_back(new FloppyFmt_IMD());
}

void FloppyCtrl_Raw::install()
{
	FloppyCtrl::install();

	memset(&m_s, 0, sizeof(m_s));

	using namespace std::placeholders;
	m_devices->dma()->register_8bit_channel(
			DMA_CHAN,
			std::bind(&FloppyCtrl_Raw::dma_read, this, _1, _2, _3),
			std::bind(&FloppyCtrl_Raw::dma_write, this, _1, _2, _3),
			nullptr,
			name());

	g_machine.register_irq(IRQ_LINE, name());

	m_timer = g_machine.register_timer(
			std::bind(&FloppyCtrl_Raw::timer, this, _1),
			name()
	);

	PINFOF(LOG_V0, LOG_FDC, "Installed Intel 82077AA floppy disk controller (Raw sector images)\n");
}

void FloppyCtrl_Raw::remove()
{
	FloppyCtrl::remove();

	m_devices->dma()->unregister_channel(DMA_CHAN);
	g_machine.unregister_irq(IRQ_LINE, name());

	g_machine.unregister_timer(m_timer);
}

void FloppyCtrl_Raw::config_changed()
{
	FloppyCtrl::config_changed();

	m_latency_mult = g_program.config().get_real(DRIVES_SECTION, DRIVES_FDD_LAT);
	m_latency_mult = clamp(m_latency_mult,0.0,1.0);
}

void FloppyCtrl_Raw::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_FDC, "saving state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);

	for(unsigned i=0; i<MAX_DRIVES; i++) {
		if(m_fdd[i]) {
			m_fdd[i]->save_state(_state);
		}
	}
}

void FloppyCtrl_Raw::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_FDC, "restoring state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	for(unsigned i=0; i<MAX_DRIVES; i++) {
		if(m_fdd[i]) {
			m_fdd[i]->restore_state(_state);
		}
	}
}

uint8_t FloppyCtrl_Raw::get_drate_for_media(uint8_t _drive)
{
	if(!is_drive_present(_drive)) {
		return FloppyDisk::DRATE_250;
	}
	return m_fdd[_drive]->get_data_rate();
}

void FloppyCtrl_Raw::reset(unsigned type)
{
	if(type == MACHINE_POWER_ON) {
		// DMA is enabled from start
		memset(&m_s, 0, sizeof(m_s));

	} else {
		// Hardware RESET clears all registers except those programmed by
		// the SPECIFY command.
		m_s.pending_irq     = false;
		m_s.reset_sensei    = 0;
		m_s.main_status_reg &= FDC_MSR_NONDMA; // keep ND bit value
		m_s.status_reg0     = 0;
		m_s.status_reg1     = 0;
		m_s.status_reg2     = 0;
		m_s.status_reg3     = 0;
	}

	// hard reset and power on
	if(type != DEVICE_SOFT_RESET) {
		// motor off drive 3..0
		// DMA/INT enabled
		// normal operation
		// drive select 0
		// software reset (via DOR port 0x3f2 bit 2) does not change DOR
		m_s.DOR = FDC_DOR_NDMAGATE | FDC_DOR_NRESET;
		m_s.data_rate = 2; // 250 Kbps
		m_s.lock = false;
	}
	if(!m_s.lock) {
		m_s.config = FDC_CONF_EFIFO; // EFIFO=1 8272A compatible mode FIFO is disabled
		m_s.pretrk = 0;
	}
	m_s.perp_mode = 0;

	for(int i=0; i<4; i++) {
		m_s.flopi[i].cylinder = 0;
		m_s.flopi[i].head     = 0;
		m_s.flopi[i].sector   = 0;
		m_s.flopi[i].eot      = 0;
		m_s.flopi[i].step     = 0;
		m_s.flopi[i].wrdata   = 0;
		m_s.flopi[i].rddata   = 0;
		m_s.flopi[i].last_hut = 0;
		m_s.flopi[i].cur_cylinder = 0;
	}

	for(unsigned i=0; i<MAX_DRIVES; i++) {
		if(m_fdd[i]) {
			m_fdd[i]->reset(type);
			m_s.flopi[i].cur_cylinder = m_fdd[i]->get_cyl();
		}
	}

	m_devices->pic()->lower_irq(IRQ_LINE);
	if(!(m_s.main_status_reg & FDC_MSR_NONDMA)) {
		m_devices->dma()->set_DRQ(DMA_CHAN, false);
	}
	enter_idle_phase();
}

void FloppyCtrl_Raw::power_off()
{
	for(unsigned i=0; i<MAX_DRIVES; i++) {
		if(m_fdd[i]) {
			m_fdd[i]->power_off();
		}
	}
	m_s.DOR = 0;
}

uint16_t FloppyCtrl_Raw::read(uint16_t _address, unsigned)
{
	uint8_t value=0, drive=current_drive();

	PDEBUGF(LOG_V2, LOG_FDC, "read  0x%04X [C%02X,D%u] ", _address,
			m_s.pending_command, drive);

	m_devices->sysboard()->set_feedback();

	switch(_address) {

		case 0x3F0: // Status Register A (SRA)
		{
			if(m_mode == Mode::PC_AT) {
				PDEBUGF(LOG_V2, LOG_FDC, "SRA  -> not accessible in PC-AT mode\n");
				return ~0;
			}
			//Model30 mode:
			// Bit 7 : INT PENDING
			value |= (m_s.pending_irq << 7);
			// Bit 6 : DRQ
			value |= (m_devices->dma()->get_DRQ(DMA_CHAN) << 6);
			// Bit 5 : STEP F/F
			value |= (m_s.flopi[drive].step << 5);
			// Bit 4 : TRK0
			if(m_fdd[drive]) {
				value |= (!m_fdd[drive]->trk00_r()) << 4;
			}
			// Bit 3 : !HDSEL
			value |= (!m_s.flopi[drive].head) << 3;
			// Bit 2 : INDEX
			if(m_s.flopi[drive].sector == 0) {
				value |= 1<<2;
			}
			// Bit 1 : WP
			if(is_media_present(drive)) {
				value |= m_fdd[drive]->wpt_r() << 1;
			}
			// Bit 0 : !DIR
			value |= !m_s.flopi[drive].direction;

			PDEBUGF(LOG_V2, LOG_FDC, "SRA  -> 0x%02X ", value);
			if(value & FDC_SRA_INT_REQ) {PDEBUGF(LOG_V2, LOG_FDC, "INT_REQ ");}
			if(value & FDC_SRA_DRQ)     {PDEBUGF(LOG_V2, LOG_FDC, "DRQ ");}
			if(value & FDC_SRA_STEP_FF) {PDEBUGF(LOG_V2, LOG_FDC, "STEP_FF ");}
			if(value & FDC_SRA_TRK0)    {PDEBUGF(LOG_V2, LOG_FDC, "TRK0 ");}
			if(value & FDC_SRA_NHDSEL)  {PDEBUGF(LOG_V2, LOG_FDC, "!HDSEL ");}
			if(value & FDC_SRA_INDEX)   {PDEBUGF(LOG_V2, LOG_FDC, "INDEX ");}
			if(value & FDC_SRA_WP)      {PDEBUGF(LOG_V2, LOG_FDC, "WP ");}
			if(value & FDC_SRA_NDIR)    {PDEBUGF(LOG_V2, LOG_FDC, "!DIR ");}
			PDEBUGF(LOG_V2, LOG_FDC, "\n");
			break;
		}
		case 0x3F1: // Status Register B (SRB)
		{
			if(m_mode == Mode::PC_AT) {
				PDEBUGF(LOG_V2, LOG_FDC, "SRB  -> not accessible in PC-AT mode\n");
				return ~0;
			}
			//Model30 mode:
			// Bit 7 : !DRV2 (is B drive installed?)
			value |= (!(m_installed_fdds>1)) << 7;
			// Bit 6 : !DS1
			value |= (!(drive==1)) << 6;
			// Bit 5 : !DS0
			value |= (!(drive==0)) << 5;
			// Bit 4 : WRDATA F/F
			value |= m_s.flopi[drive].wrdata << 4;
			// Bit 3 : RDDATA F/F
			value |= m_s.flopi[drive].rddata << 3;
			// Bit 2 : WE F/F
			value |= m_s.flopi[drive].wrdata << 2; //repeat wrdata?
			// Bit 1 : !DS3
			value |= (!(drive==3)) << 1;
			// Bit 0 : !DS2
			value |= !(drive==2);

			PDEBUGF(LOG_V2, LOG_FDC, "SRB  -> 0x%02X ", value);
			if(value & FDC_SRB_NDRV2)     {PDEBUGF(LOG_V2, LOG_FDC, "!DRV2 ");}
			if(value & FDC_SRB_NDS1)      {PDEBUGF(LOG_V2, LOG_FDC, "!DS1 ");}
			if(value & FDC_SRB_NDS0)      {PDEBUGF(LOG_V2, LOG_FDC, "!DS0 ");}
			if(value & FDC_SRB_WRDATA_FF) {PDEBUGF(LOG_V2, LOG_FDC, "WRDATA_FF ");}
			if(value & FDC_SRB_RDDATA_FF) {PDEBUGF(LOG_V2, LOG_FDC, "RDDATA_FF ");}
			if(value & FDC_SRB_WE_FF)     {PDEBUGF(LOG_V2, LOG_FDC, "WE_FF ");}
			if(value & FDC_SRB_NDS3)      {PDEBUGF(LOG_V2, LOG_FDC, "!DS3 ");}
			if(value & FDC_SRB_NDS2)      {PDEBUGF(LOG_V2, LOG_FDC, "!DS2 ");}
			PDEBUGF(LOG_V2, LOG_FDC, "\n");
			break;
		}
		case 0x3F2: // Digital Output Register (DOR)
		{
			//AT-PS/2-Model30 mode
			value = m_s.DOR;
			PDEBUGF(LOG_V2, LOG_FDC, "DOR  -> 0x%02X ", value);
			if(value & FDC_DOR_MOTEN3)   {PDEBUGF(LOG_V2, LOG_FDC, "MOTEN3 ");}
			if(value & FDC_DOR_MOTEN2)   {PDEBUGF(LOG_V2, LOG_FDC, "MOTEN2 ");}
			if(value & FDC_DOR_MOTEN1)   {PDEBUGF(LOG_V2, LOG_FDC, "MOTEN1 ");}
			if(value & FDC_DOR_MOTEN0)   {PDEBUGF(LOG_V2, LOG_FDC, "MOTEN0 ");}
			if(value & FDC_DOR_NDMAGATE) {PDEBUGF(LOG_V2, LOG_FDC, "!DMAGATE ");}
			if(value & FDC_DOR_NRESET)   {PDEBUGF(LOG_V2, LOG_FDC, "!RESET ");}
			PDEBUGF(LOG_V2, LOG_FDC, "DRVSEL=%02X\n", drive);
			break;
		}
		case 0x3F4: // Main Status Register (MSR)
		{
			//AT-PS/2-Model30 mode
			value = m_s.main_status_reg;

			if(m_s.pending_command == FDC_CMD_INVALID) {
				// NONDMA will be set to a 1 only during the execution phase
				// of a command. This is for polled data transfers and helps
				// differentiate between the data transfer phase and the reading
				// of result bytes.
				value &= ~FDC_MSR_NONDMA;
			}

			PDEBUGF(LOG_V2, LOG_FDC, "MSR  -> 0x%02X ", value);
			if(value & FDC_MSR_RQM)      {PDEBUGF(LOG_V2, LOG_FDC, "RQM ");}
			if(value & FDC_MSR_DIO)      {PDEBUGF(LOG_V2, LOG_FDC, "DIO ");}
			if(value & FDC_MSR_NONDMA)   {PDEBUGF(LOG_V2, LOG_FDC, "NONDMA ");}
			if(value & FDC_MSR_CMDBUSY)  {PDEBUGF(LOG_V2, LOG_FDC, "CMDBUSY ");}
			if(value & FDC_MSR_DRV3BUSY) {PDEBUGF(LOG_V2, LOG_FDC, "DRV3BUSY ");}
			if(value & FDC_MSR_DRV2BUSY) {PDEBUGF(LOG_V2, LOG_FDC, "DRV2BUSY ");}
			if(value & FDC_MSR_DRV1BUSY) {PDEBUGF(LOG_V2, LOG_FDC, "DRV1BUSY ");}
			if(value & FDC_MSR_DRV0BUSY) {PDEBUGF(LOG_V2, LOG_FDC, "DRV0BUSY ");}
			PDEBUGF(LOG_V2, LOG_FDC, "\n");
			break;
		}
		case 0x3F5: // Data
		{
			unsigned ridx = m_s.result_index + 1;
			unsigned rsize = m_s.result_size;
			if((m_s.main_status_reg & FDC_MSR_NONDMA) && (m_s.cmd_code() == FDC_CMD_READ)) {
				if(m_s.floppy_buffer_index >= 512) {
					m_s.floppy_buffer_index = 0;
				}
				rsize = 512;
				ridx = m_s.floppy_buffer_index + 1;
				read_data(&value, 1, false);
				if(m_s.floppy_buffer_index >= 512) {
					// on a read, INT should be lowered when FIFO gets emptied,
					// ie at the end of a sector data area.
					// INT should be risen again upon entering the result phase
					lower_interrupt();
				}
			} else if(m_s.result_size == 0) {
				ridx = 0;
				m_s.main_status_reg &= FDC_MSR_NONDMA;
				value = m_s.result[0];
			} else {
				value = m_s.result[m_s.result_index++];
				m_s.main_status_reg &= 0xF0;
				lower_interrupt();
				if(m_s.result_index >= m_s.result_size) {
					enter_idle_phase();
				}
			}
			PDEBUGF(LOG_V2, LOG_FDC, "D%d/%d -> 0x%02X\n", ridx, rsize, value);
			break;
		}
		case 0x3F7: // Digital Input Register (DIR)
		{
			// turn on the drive motor bit before access the DIR register for a selected drive
			if(is_motor_on(drive)) {
				if(m_mode == Mode::PC_AT) {
					value |= m_fdd[drive]->dskchg_r()==0 ? FDC_DIR_DSKCHG : 0;
					PDEBUGF(LOG_V2, LOG_FDC, "DIR  -> 0x%02X ", value);
					if(value & FDC_DIR_DSKCHG)   {PDEBUGF(LOG_V2, LOG_FDC, "DSKCHG");}
					PDEBUGF(LOG_V2, LOG_FDC, "\n");
				} else {
					// Model30 mode
					// Bit 7 : !DSKCHG
					value |= m_fdd[drive]->dskchg_r()==1 ? FDC_DIR_DSKCHG : 0;
					// Bit 3 : !DMAGATE (DOR)
					value |= m_s.DOR & FDC_DIR_NDMAGATE; //same position
					// Bit 2 : NOPREC (CCR)
					value |= (m_s.noprec << 2);
					// Bit 1-0 : DRATE SEL1-0 (CCR)
					value |= m_s.data_rate;
					PDEBUGF(LOG_V2, LOG_FDC, "DIR  -> 0x%02X ", value);
					if(value & FDC_DIR_DSKCHG)   {PDEBUGF(LOG_V2, LOG_FDC, "!DSKCHG ");}
					if(value & FDC_DIR_NDMAGATE) {PDEBUGF(LOG_V2, LOG_FDC, "!DMAGATE ");}
					if(value & FDC_DIR_NOPREC)   {PDEBUGF(LOG_V2, LOG_FDC, "NOPREC ");}
					PDEBUGF(LOG_V2, LOG_FDC, "DRATE=%02X\n", value & FDC_DIR_DRATE_SEL);
				}
				// The STEP bit is latched with the Step output going active
				// and is cleared with a read to the DIR register, Hardware or Software RESET
				m_s.flopi[drive].step = false;
				// according to docs, RDDATA (3) and WRDATA (4) are also cleared by
				// reading the DIR register and RESETs (p.9)
				m_s.flopi[drive].rddata = false;
				m_s.flopi[drive].wrdata = false;
			} else {
				PDEBUGF(LOG_V2, LOG_FDC, "DIR  -> 0 (DRV%u motor is off)\n", drive);
			}
			break;
		}
		default:
			assert(false);
			return 0;
	}

	return value;
}

void FloppyCtrl_Raw::write(uint16_t _address, uint16_t _value, unsigned)
{
	PDEBUGF(LOG_V2, LOG_FDC, "write 0x%04X          ", _address);

	m_devices->sysboard()->set_feedback();

	switch(_address) {

		case 0x3F2: // Digital Output Register (DOR)
		{
			uint8_t normal_op  = _value & FDC_DOR_NRESET;
			uint8_t drive_sel  = _value & FDC_DOR_DRVSEL;
			uint8_t prev_normal_op = m_s.DOR & FDC_DOR_NRESET;

			m_s.DOR = _value;

			if(prev_normal_op==0 && normal_op) {
				// transition from RESET to NORMAL
				g_machine.activate_timer(m_timer, 250_us, false);
			} else if(prev_normal_op && normal_op==0) {
				// transition from NORMAL to RESET
				m_s.main_status_reg &= FDC_MSR_NONDMA;
				m_s.pending_command = FDC_CMD_RESET; // RESET pending
			}
			PDEBUGF(LOG_V2, LOG_FDC, "DOR  <- 0x%02X ", _value);
			if(_value & FDC_DOR_MOTEN0)   { PDEBUGF(LOG_V2, LOG_FDC, "MOT0 "); }
			if(_value & FDC_DOR_MOTEN1)   { PDEBUGF(LOG_V2, LOG_FDC, "MOT1 "); }
			if(_value & FDC_DOR_MOTEN2)   { PDEBUGF(LOG_V2, LOG_FDC, "MOT2 "); }
			if(_value & FDC_DOR_MOTEN3)   { PDEBUGF(LOG_V2, LOG_FDC, "MOT3 "); }
			if(_value & FDC_DOR_NDMAGATE) { PDEBUGF(LOG_V2, LOG_FDC, "!DMAGATE "); }
			if(_value & FDC_DOR_NRESET)   { PDEBUGF(LOG_V2, LOG_FDC, "!RESET "); }
			PDEBUGF(LOG_V2, LOG_FDC, "DRVSEL=%01X\n", drive_sel);
			if(drive_sel >= MAX_DRIVES || !m_fdd[drive_sel]) {
				PDEBUGF(LOG_V0, LOG_FDC, "WARNING: non existing drive selected\n");
			}
			for(unsigned i=0; i<MAX_DRIVES; i++) {
				if(m_fdd[i]) {
					auto mot_on = (m_s.DOR >> (4+i)) & 1;
					if(mot_on) {
						PDEBUGF(LOG_V2, LOG_FDC, "Drive %u motor ON\n", i);
					}
					m_fdd[i]->mon_w(!mot_on);
				}
			}

			break;
		}
		case 0x3F4: // Datarate Select Register (DSR)
		{
			m_s.data_rate = _value & FDC_DSR_DRATE_SEL;
			if(_value & FDC_DSR_SW_RESET) {
				m_s.main_status_reg &= FDC_MSR_NONDMA;
				m_s.pending_command = FDC_CMD_RESET; // RESET pending
				g_machine.activate_timer(m_timer, 250_us, false);
			}
			PDEBUGF(LOG_V2, LOG_FDC, "DSR  <- 0x%02X ", _value);
			if(_value & FDC_DSR_SW_RESET) { PDEBUGF(LOG_V2, LOG_FDC, "RESET "); }
			if(_value & FDC_DSR_PWR_DOWN) { PDEBUGF(LOG_V2, LOG_FDC, "PWRDOWN "); }
			PDEBUGF(LOG_V2, LOG_FDC, "PRECOMP=%u ", (_value & FDC_DSR_PRECOMP) >> 2);
			PDEBUGF(LOG_V2, LOG_FDC, "DRATESEL=%u (%ukbit) ", m_s.data_rate, drate_in_k[m_s.data_rate]);
			if(_value & (FDC_DSR_PWR_DOWN | FDC_DSR_PRECOMP)) {
				PDEBUGF(LOG_V2, LOG_FDC, "(unsupported bits set)");
			}
			PDEBUGF(LOG_V2, LOG_FDC, "\n");
			break;
		}
		case 0x3F5: // Data FIFO
		{
			if((m_s.main_status_reg & FDC_MSR_NONDMA) && ( 
				(m_s.cmd_code() == FDC_CMD_WRITE) ||
				(m_s.cmd_code() == FDC_CMD_FORMAT_TRACK)
			)) {
				if(m_s.cmd_code() == FDC_CMD_WRITE) {
					unsigned rsize = 512;
					unsigned ridx = m_s.floppy_buffer_index + 1;
					PDEBUGF(LOG_V2, LOG_FDC, "D%d/%d <- 0x%02X\n", ridx, rsize, _value);
				} else if(m_s.cmd_code() == FDC_CMD_FORMAT_TRACK) {
					PDEBUGF(LOG_V2, LOG_FDC, "D%d <- 0x%02X\n", m_s.format_count, _value);
				}
				write_data((uint8_t *) &_value, 1, false);
				lower_interrupt();
				break;
			} else if(m_s.command_complete) {
				if(m_s.pending_command != FDC_CMD_INVALID) {
					PDEBUGF(LOG_V2, LOG_FDC, "D0/0 <- 0x%02X new command with old one [%02X] pending\n",
							_value, m_s.pending_command);
					return;
				}
				m_s.command[0] = _value;
				m_s.command_complete = false;
				m_s.command_index = 1;
				// read/write command in progress
				m_s.main_status_reg &= ~FDC_MSR_DIO; // leave drive status untouched
				// CMDBUSY
				//  This bit is set to a one when a command is in progress.
				//  This bit will go active after the command byte has been accepted
				//  and goes inactive at the end of the results phase. If there is no
				//  result phase (SEEK, RECALIBRATE commands), this bit is returned to
				//  a 0 after the last command byte.
				// RQM
				//  Indicates that the host can transfer data if set to a 1.
				//  No access is permitted if set to a 0.
				m_s.main_status_reg |= FDC_MSR_RQM | FDC_MSR_CMDBUSY;
				auto cmd_def = ms_cmd_list.find(_value & FDC_CMD_MASK);
				if(cmd_def == ms_cmd_list.end()) {
					cmd_def = ms_cmd_list.find(FDC_CMD_INVALID);
				}
				m_s.command_size = cmd_def->second.size;
				PDEBUGF(LOG_V2, LOG_FDC, "D1/%d <- 0x%02X (cmd: %s)\n",
						m_s.command_size, _value, cmd_def->second.name);
			} else {
				// in command phase
				m_s.command[m_s.command_index++] = _value;
				PDEBUGF(LOG_V2, LOG_FDC, "D%d/%d <- 0x%02X\n", m_s.command_index, m_s.command_size, _value);
			}
			if(m_s.command_index == m_s.command_size) {
				// read/write command not in progress any more
				enter_execution_phase();
				m_s.command_complete = true;
			}
			return;
		}
		case 0x3F7: // Configuration Control Register (CCR)
		{
			PDEBUGF(LOG_V2, LOG_FDC, "CCR  <- 0x%02X ", _value);
			m_s.data_rate = _value & FDC_CCR_DRATE_SEL;
			switch (m_s.data_rate) {
				case 0: PDEBUGF(LOG_V2, LOG_FDC, "500 Kbps"); break;
				case 1: PDEBUGF(LOG_V2, LOG_FDC, "300 Kbps"); break;
				case 2: PDEBUGF(LOG_V2, LOG_FDC, "250 Kbps"); break;
				case 3: PDEBUGF(LOG_V2, LOG_FDC, "1 Mbps"); break;
				default: assert(false); break;
			}
			m_s.noprec = _value & FDC_CCR_NOPREC;
			if(m_s.noprec) { PDEBUGF(LOG_V2, LOG_FDC, " NWPC"); }
			PDEBUGF(LOG_V2, LOG_FDC, "\n");
			break;
		}
		default:
			PDEBUGF(LOG_V0, LOG_FDC, "    <- 0x%02X ignored\n", _value);
			break;

	}
}

void FloppyCtrl_Raw::enter_execution_phase()
{
	PDEBUGF(LOG_V1, LOG_FDC, "COMMAND: ");
	PDEBUGF(LOG_V2, LOG_FDC, "%s ", bytearray_to_string(m_s.command,m_s.command_size).c_str());

	// controller is busy, data FIFO is not ready.
	// this is also the "hang" condition.
	// fdc hangs should be handled by the host software with a timeout counter.
	// CMDBUSY will be cleared at the end of the result phase.
	m_s.main_status_reg &= FDC_MSR_NONDMA;
	m_s.main_status_reg |= FDC_MSR_CMDBUSY;

	m_s.pending_command = m_s.command[0];

	auto cmd_def = ms_cmd_list.find(m_s.cmd_code());
	if(cmd_def == ms_cmd_list.end()) {
		cmd_def = ms_cmd_list.find(FDC_CMD_INVALID);
	}
	cmd_def->second.fn(*this);
}

bool FloppyCtrl_Raw::start_read_write_cmd()
{
	const char *cmd = (m_s.cmd_code()==FDC_CMD_READ) ? "read" : "write";
	m_s.multi_track = m_s.cmd_mtrk();
	if((m_s.DOR & FDC_DOR_NDMAGATE) == 0) {
		PWARNF(LOG_V0, LOG_FDC, "%s with INT disabled is untested!\n", cmd);
	}
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.DOR = FDC_DOR_DRIVE(drive);

	uint8_t cylinder    = m_s.command[2]; // 0..79 depending
	uint8_t head        = m_s.command[3] & 0x01;
	uint8_t sector      = m_s.command[4]; // 1..36 depending
	uint8_t sector_size = m_s.command[5];
	uint8_t eot         = m_s.command[6]; // 1..36 depending
	uint8_t data_length = m_s.command[8];

	PDEBUGF(LOG_V1, LOG_FDC, "%s data DRV%u, %s C=%u,H=%u,S=%u,N=%u,EOT=%u,DTL=%u\n",
			cmd, drive, m_s.cmd_mtrk()?"MT,":"",
			cylinder, head, sector, sector_size, eot, data_length);

	if(!is_drive_present(drive) || !is_motor_on(drive)) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: motor not on\n", cmd);
		return false; // Hang controller
	}

	// check that head number in command[1] bit two matches the head
	// reported in the head number field.  Real floppy drives are
	// picky about this, as reported in SF bug #439945, (Floppy drive
	// read input error checking).
	if(head != ((m_s.command[1]>>2)&1)) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: head number in command[1] doesn't match head field\n", cmd);
		m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
		m_s.status_reg1 = FDC_ST1_ND;
		m_s.status_reg2 = 0x00;
		enter_result_phase();
		return false;
	}

	if(!is_media_present(drive)) {
		// the controller would fail to receive the index pulse and lock-up
		// since the index pulses are required for termination of the execution phase.
		PDEBUGF(LOG_V1, LOG_FDC, "%s: attempt to read/write sector %u with media not present\n", cmd, sector);
		return false; // Hang controller
	}

	if(sector_size != 0x02) { // 512 bytes
		// TODO
		PERRF(LOG_FDC, "%s: sector size %d not supported\n", cmd, 128<<sector_size);
		return false; // Hang controller ?
	}

	auto props = m_fdd[drive]->get_media_props();

	if(cylinder >= props.tracks) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: norm r/w parms out of range: sec#%02xh cyl#%02xh eot#%02xh head#%02xh\n",
				cmd, sector, cylinder, eot, head);
		return false; // Hang controller ?
	}

	// This hack makes older versions of the Bochs BIOS work
	if(eot == 0) {
		// TODO should be hang or abnormal termination?
		eot = props.spt;
	}
	m_s.flopi[drive].direction = (m_s.flopi[drive].cur_cylinder > cylinder);
	m_s.flopi[drive].cylinder  = cylinder;
	m_s.flopi[drive].head      = head;
	m_s.flopi[drive].sector    = sector;
	m_s.flopi[drive].eot       = eot;

	bool sec_exists = (cylinder < props.tracks) && 
	                  (head < props.sides) && 
	                  (sector <= props.spt);
	if(!sec_exists || m_s.data_rate != get_drate_for_media(drive)) {
		if(!sec_exists) {
			PDEBUGF(LOG_V0, LOG_FDC, "%s: attempt to %s non existant sector chs:%u/%u/%u\n",
					cmd, cmd, cylinder, head, sector);
		} else {
			PDEBUGF(LOG_V0, LOG_FDC, "%s: attempt to %s at wrong data rate\n", cmd, cmd);
		}
		m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
		m_s.status_reg1 = FDC_ST1_ND;
		m_s.status_reg2 = 0;
		if(cylinder > props.tracks) {
			m_s.status_reg2 |= FDC_ST2_WC;
		}
		enter_result_phase();
		return false;
	}

	m_fdd[drive]->dir_w(m_s.flopi[drive].direction);
	m_fdd[drive]->ss_w(head); // side select

	int phy_cylinder = cylinder << m_fdd[drive]->is_double_step_media();
	if(phy_cylinder != m_s.flopi[drive].cur_cylinder && !(m_s.config & FDC_CONF_EIS)) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: cylinder request (%u) != current cylinder (%u), EIS=0\n",
				cmd, cylinder, m_s.flopi[drive].cur_cylinder);
		m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
		m_s.status_reg1 = FDC_ST1_ND;
		m_s.status_reg2 = 0x00;
		enter_result_phase();
		return false;
	}

	return true;
}

void FloppyCtrl_Raw::cmd_read_data()
{
	if(!start_read_write_cmd()) {
		return;
	}

	uint8_t drive = m_s.command[1] & 0x03;

	// DMA:
	// pre-fill buffer with data > timer > DRQ > fill buffer > timer > DRQ > ... > TC
	// non-DMA:
	// pre-fill buffer with data > timer > INT > fill buffer ... > TC

	m_s.flopi[drive].rddata = true;
	uint32_t step_time_us = 0;
	if(m_s.config & FDC_CONF_EIS) {
		int phy_cylinder = m_s.flopi[drive].cylinder << m_fdd[drive]->is_double_step_media();
		if(phy_cylinder != m_s.flopi[drive].cur_cylinder) {
			step_time_us = calculate_step_delay_us(drive, m_s.flopi[drive].cur_cylinder, phy_cylinder);
			m_fdd[drive]->step_to(phy_cylinder, step_time_us*1_us);
		}
	}
	floppy_xfer(drive, FROM_FLOPPY);

	uint32_t sector_time = calculate_rw_delay(drive, true);
	g_machine.activate_timer(m_timer, uint64_t(step_time_us+sector_time)*1_us, false);
}

void FloppyCtrl_Raw::cmd_write_data()
{
	if(!start_read_write_cmd()) {
		return;
	}

	uint8_t drive = m_s.command[1] & 0x03;

	// DMA:
	//  DRQ > fill buffer > write image > timer > DRQ > ... > timer > TC
	// non-DMA:
	//  INT > fill buffer > write image > fill buffer > ... > TC

	m_s.flopi[drive].wrdata = true;
	int phy_cylinder = m_s.flopi[drive].cylinder << m_fdd[drive]->is_double_step_media();
	if(m_s.flopi[drive].cur_cylinder != phy_cylinder) {
		// do a seek first
		auto step_time_us = calculate_step_delay_us(drive, m_s.flopi[drive].cur_cylinder, phy_cylinder);
		g_machine.activate_timer(m_timer, uint64_t(step_time_us)*1_us, false);
		m_fdd[drive]->step_to(phy_cylinder, step_time_us*1_us);
	} else {
		// ready to receive data
		if(m_s.main_status_reg & FDC_MSR_NONDMA) {
			m_s.main_status_reg |= FDC_MSR_RQM;
			raise_interrupt();
		} else {
			m_devices->dma()->set_DRQ(DMA_CHAN, true);
		}
	}
}

void FloppyCtrl_Raw::cmd_version()
{
	PDEBUGF(LOG_V1, LOG_FDC, "version\n");
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
	enter_result_phase();
}

void FloppyCtrl_Raw::cmd_format_track()
{
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.DOR = FDC_DOR_DRIVE(drive);

	bool motor_on = (m_s.DOR>>(drive+4)) & 0x01;
	if(!motor_on) {
		PERRF(LOG_FDC, "format track: motor not on\n");
		return; // Hang controller?
	}
	m_s.flopi[drive].head = (m_s.command[1] >> 2) & 0x01;
	uint8_t sector_size = m_s.command[2]; //N
	m_s.format_count = m_s.command[3]; //SC
	m_s.format_fillbyte = m_s.command[5]; //D

	PDEBUGF(LOG_V1, LOG_FDC, "format track DRV%u, N=%u,SC=%u,GPL=%u,D=%02x\n", drive,
			sector_size, m_s.format_count, m_s.command[4], m_s.format_fillbyte);

	if(!is_drive_present(drive)) {
		PERRF(LOG_FDC, "format track: bad drive #%d\n", drive);
		return; // Hang controller?
	}
	if(sector_size != 0x02) { // 512 bytes
		PERRF(LOG_FDC, "format track: sector size %d not supported\n", 128<<sector_size);
		return; // Hang controller?
	}
	if(!is_media_present(drive)) {
		PDEBUGF(LOG_V0, LOG_FDC, "format track: attempt to format track with media not present\n");
		return; // Hang controller
	}
	if(m_fdd[drive]->wpt_r() || m_s.format_count != m_fdd[drive]->get_media_props().spt) {
		if(m_fdd[drive]->wpt_r()) {
			PINFOF(LOG_V0, LOG_FDC, "Attempt to format with media write-protected\n");
		} else {
			// On real hardware, when you try to format a 720K floppy as 1.44M the drive will happily
			// do so regardless of the presence of the "format hole".
			PERRF(LOG_FDC, "Wrong floppy disk type! Specify the format in the DOS command line.\n");
			PDEBUGF(LOG_V0, LOG_FDC, "format track: %d sectors/track requested (%d expected)\n",
					m_s.format_count, m_fdd[drive]->get_media_props().spt);
		}
		m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
		m_s.status_reg1 = FDC_ST1_DE | FDC_ST1_ND | FDC_ST1_NW | FDC_ST1_MA;
		m_s.status_reg2 = FDC_ST2_DD | FDC_ST2_WC | FDC_ST2_MD;
		enter_result_phase();
		return;
	}

	m_fdd[drive]->ss_w(m_s.flopi[drive].head);

	// 4 header bytes per sector are required
	m_s.format_count <<= 2;

	if(m_s.main_status_reg & FDC_MSR_NONDMA) {
		// ready to receive data
		m_s.main_status_reg |= FDC_MSR_RQM;
		raise_interrupt();
	} else {
		m_devices->dma()->set_DRQ(DMA_CHAN, true);
	}
}

void FloppyCtrl_Raw::cmd_recalibrate()
{
	uint8_t drive = (m_s.command[1] & 0x03);
	m_s.DOR = FDC_DOR_DRIVE(drive);

	PDEBUGF(LOG_V1, LOG_FDC, "recalibrate DRV%u (cur.C=%u)\n", drive, m_s.flopi[drive].cur_cylinder);

	// command head to track 0
	// error condition noted in Status reg 0's equipment check bit
	// seek end bit set to 1 in Status reg 0 regardless of outcome
	// The last two are taken care of in timer().
	m_s.flopi[drive].direction = (m_s.flopi[drive].cur_cylinder > 0);
	m_s.flopi[drive].cylinder = 0;
	// clear RQM and CMDBUSY, set drive busy
	//  during the execution phase the controller is in NON BUSY state.
	// DRV x BUSY
	//  These bits are set to ones when a drive is in the seek portion of
	//  a command, including seeks, and recalibrates.
	m_s.main_status_reg &= FDC_MSR_NONDMA;
	m_s.main_status_reg |= (1 << drive);

	uint32_t step_delay_us = calculate_step_delay_us(drive, m_s.flopi[drive].cur_cylinder, 0);
	PDEBUGF(LOG_V2, LOG_FDC, "step_delay: %u us\n", step_delay_us);
	g_machine.activate_timer(m_timer, uint64_t(step_delay_us)*1_us, false);

	if(is_drive_present(drive)) {
		m_fdd[drive]->dir_w(m_s.flopi[drive].direction);
		m_fdd[drive]->step_to(0, step_delay_us*1_us);
		m_fdd[drive]->ss_w(0);
		m_fdd[drive]->recalibrate();
	}
}

void FloppyCtrl_Raw::cmd_sense_int()
{
	//  execution:
	//   get status
	// result:
	//   no interupt
	//   byte0 = status reg0
	//   byte1 = current cylinder number (0 to 79)
	PDEBUGF(LOG_V1, LOG_FDC, "sense interrupt status\n");

	if(m_s.reset_sensei > 0) {
		uint8_t drive = 4 - m_s.reset_sensei;
		m_s.status_reg0 &= FDC_ST0_IC | FDC_ST0_SE | FDC_ST0_EC;
		m_s.status_reg0 |= FDC_ST_HDS(drive);
		m_s.reset_sensei--;
	} else if(!m_s.pending_irq) {
		m_s.status_reg0 = FDC_ST0_IC_INVALID;
	}
	enter_result_phase();
}

void FloppyCtrl_Raw::cmd_specify()
{
	// execution: specified parameters are loaded
	// result: no result bytes, no interrupt
	m_s.SRT = m_s.command[1] >> 4;
	m_s.HUT = m_s.command[1] & 0x0f;
	m_s.HLT = m_s.command[2] >> 1;

	PDEBUGF(LOG_V1, LOG_FDC, "specify, SRT=%u,HUT=%u,HLT=%u,ND=%u\n",
			m_s.SRT, m_s.HUT, m_s.HLT, m_s.command[2]&1);

	m_s.main_status_reg |= (m_s.command[2] & 0x01) ? FDC_MSR_NONDMA : 0;
	enter_idle_phase();
}

void FloppyCtrl_Raw::cmd_sense_drive()
{
	uint8_t drive = (m_s.command[1] & 0x03);

	PDEBUGF(LOG_V1, LOG_FDC, "get status DRV%u\n", drive);

	m_s.flopi[drive].head = (m_s.command[1] >> 2) & 0x01;
	m_s.status_reg3 = FDC_ST3_RY | FDC_ST_HDS(drive);
	if(is_drive_present(drive)) {
		if(m_fdd[drive]->wpt_r()) {
			m_s.status_reg3 |= FDC_ST3_WP;
		}
		if(m_s.flopi[drive].cur_cylinder == 0) {
			// the head takes time to move to track0; this time is used to determine if 40 or 80 tracks
			// the value of cur_cylinder for the drive is set in the timer handler
			m_s.status_reg3 |= FDC_ST3_T0;
		}
		if(!m_fdd[drive]->twosid_r()) {
			m_s.status_reg3 |= FDC_ST3_TS;
		}
	}
	enter_result_phase();
}

void FloppyCtrl_Raw::cmd_configure()
{
	m_s.config = m_s.command[2];
	m_s.pretrk = m_s.command[3];
	PDEBUGF(LOG_V1, LOG_FDC, "configure, EIS=%u,EFIFO=%u,POLL=%u,FIFOTHR=%u,PRETRK=%u\n",
			bool(m_s.config & FDC_CONF_EIS),
			bool(m_s.config & FDC_CONF_EFIFO),
			bool(m_s.config & FDC_CONF_POLL),
			m_s.config & FDC_CONF_FIFOTHR,
			m_s.pretrk
			);
	enter_idle_phase();
}

void FloppyCtrl_Raw::cmd_seek()
{
	// command:
	//   byte0 = 0F
	//   byte1 = drive & head select
	//   byte2 = cylinder number
	// execution:
	//   postion head over specified cylinder
	// result:
	//   no result bytes, issues an interrupt
	uint8_t drive =  m_s.command[1] & 0x03;
	uint8_t head  = (m_s.command[1] >> 2) & 0x01;
	uint8_t cylinder = m_s.command[2];

	PDEBUGF(LOG_V1, LOG_FDC, "seek DRV%u, %s C=%u (cur.C=%u)\n",
			drive, m_s.cmd_rel()?"REL":"", cylinder, m_s.flopi[drive].cur_cylinder);

	if(m_s.cmd_rel()) {
		cmd_not_implemented();
		return;
	}

	m_s.DOR = FDC_DOR_DRIVE(drive);

	// ??? should also check cylinder validity
	m_s.flopi[drive].direction = (m_s.flopi[drive].cur_cylinder > cylinder);
	m_s.flopi[drive].cylinder  = cylinder;
	m_s.flopi[drive].head = head;

	// clear RQM and CMDBUSY, set drive busy
	//  during the execution phase the controller is in NON BUSY state.
	// DRV x BUSY
	//  These bits are set to ones when a drive is in the seek portion of
	//  a command, including seeks, and recalibrates.
	m_s.main_status_reg &= FDC_MSR_NONDMA;
	m_s.main_status_reg |= (1 << drive);

	uint32_t step_delay_us = calculate_step_delay_us(drive, m_s.flopi[drive].cur_cylinder, cylinder);
	PDEBUGF(LOG_V2, LOG_FDC, "step_delay: %u us\n", step_delay_us);
	g_machine.activate_timer(m_timer, uint64_t(step_delay_us)*1_us, false);

	if(is_drive_present(drive)) {
		m_fdd[drive]->dir_w(m_s.flopi[drive].direction);
		m_fdd[drive]->step_to(cylinder, step_delay_us*1_us);
		m_fdd[drive]->ss_w(head);
	}
}

void FloppyCtrl_Raw::cmd_dumpreg()
{
	PDEBUGF(LOG_V1, LOG_FDC, "dump registers\n");
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
	enter_result_phase();
}

void FloppyCtrl_Raw::cmd_read_id()
{
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.flopi[drive].head = (m_s.command[1] >> 2) & 0x01;
	m_s.DOR = FDC_DOR_DRIVE(drive);

	PDEBUGF(LOG_V1, LOG_FDC, "read ID DRV%u\n", drive);

	if(!is_motor_on(drive)) {
		PDEBUGF(LOG_V1, LOG_FDC, "read ID: motor not on\n");
		return; // Hang controller
	}
	if(!is_drive_present(drive)) {
		PDEBUGF(LOG_V1, LOG_FDC, "read ID: bad drive #%d\n", drive);
		return; // Hang controller
	}
	if(!is_media_present(drive)) {
		PINFOF(LOG_V1, LOG_FDC, "read ID: attempt to read sector ID with media not present\n");
		return; // Hang controller
	}
	if(m_s.data_rate != get_drate_for_media(drive)) {
		m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
		m_s.status_reg1 = FDC_ST1_MA;
		m_s.status_reg2 = 0x00;
		enter_result_phase();
		return;
	}

	m_fdd[drive]->ss_w(m_s.flopi[drive].head);

	m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
	uint32_t sector_time = calculate_rw_delay(drive, true);
	g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);
}

void FloppyCtrl_Raw::cmd_perp_mode()
{
	m_s.perp_mode = m_s.command[1];
	PDEBUGF(LOG_V1, LOG_FDC, "perpendicular mode, config=0x%02X\n", m_s.perp_mode);
	enter_idle_phase();
}

void FloppyCtrl_Raw::cmd_lock()
{
	m_s.lock = m_s.cmd_lock();
	PDEBUGF(LOG_V1, LOG_FDC, "%slock status\n", !m_s.lock?"un":"");
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
	enter_result_phase();
}

void FloppyCtrl_Raw::cmd_not_implemented()
{
	PERRF(LOG_FDC, "Command 0x%02x not implemented\n", m_s.pending_command);
	m_s.status_reg0 = FDC_ST0_IC_INVALID;
	enter_result_phase();
}

void FloppyCtrl_Raw::cmd_invalid()
{
	PDEBUGF(LOG_V1, LOG_FDC, "INVALID command: 0x%02x\n", m_s.pending_command);
	m_s.status_reg0 = FDC_ST0_IC_INVALID;
	enter_result_phase();
}

void FloppyCtrl_Raw::floppy_xfer(uint8_t drive, XferDir direction)
{
	if(!is_drive_present(drive)) {
		PERRF(LOG_FDC, "floppy_xfer: bad drive #%d\n", drive);
		return;
	}

	PDEBUGF(LOG_V2, LOG_FDC, "floppy_xfer DRV%u: chs=%u/%u/%u, bytes=512, direction=%s floppy\n",
			drive, m_s.flopi[drive].cylinder, m_s.flopi[drive].head, m_s.flopi[drive].sector, (direction==FROM_FLOPPY)? "from" : "to");

	try {
		switch(direction) {
			case FROM_FLOPPY:
				m_fdd[drive]->read_sector(m_s.flopi[drive].sector, m_s.floppy_buffer, 512);
				break;
			case TO_FLOPPY:
				m_fdd[drive]->write_sector(m_s.flopi[drive].sector, m_s.floppy_buffer, 512);
				break;
		}
	} catch(std::runtime_error &e) {
		PERRF(LOG_FDC, "%s\n", e.what());
	}
}

void FloppyCtrl_Raw::timer(uint64_t)
{
	uint8_t drive = current_drive();
	switch(m_s.cmd_code()) {
		case FDC_CMD_RECALIBRATE:
		{
			// TODO parallel RECALIBRATE operations may be done on up to 4 drives at once.
			m_s.status_reg0 = FDC_ST0_SE | drive;
			if(!is_motor_on(drive)) {
				m_s.status_reg0 |= FDC_ST0_IC_ABNORMAL | FDC_ST0_EC;
			} else {
				m_s.status_reg0 |= FDC_ST0_IC_NORMAL;
			}
			m_s.flopi[drive].direction = false;
			// clear DRVxBUSY bit
			m_s.main_status_reg &= ~(1 << drive);
			// no result phase
			step_head();
			enter_idle_phase();
			raise_interrupt();
			break;
		}
		case FDC_CMD_SEEK:
			m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST0_SE | FDC_ST_HDS(drive);
			// clear DRVxBUSY bit
			m_s.main_status_reg &= ~(1 << drive);
			// no result phase
			step_head();
			enter_idle_phase();
			raise_interrupt();
			break;

		case FDC_CMD_READ_ID:
			enter_result_phase();
			break;

		case FDC_CMD_WRITE:
			if(m_s.TC) { // Terminal Count line, done
				m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
				m_s.status_reg1 = 0;
				m_s.status_reg2 = 0;
				PDEBUGF(LOG_V2, LOG_FDC, "<<WRITE DONE>> DRV%u C=%u,H=%u,S=%u\n",
						drive, m_s.flopi[drive].cylinder, m_s.flopi[drive].head, m_s.flopi[drive].sector);
				enter_result_phase();
				return;
			} else {
				if(m_s.main_status_reg & FDC_MSR_NONDMA) {
					if(!(m_s.main_status_reg & FDC_MSR_RQM)) {
						// the initial seek completed, request data
						m_s.main_status_reg |= FDC_MSR_RQM;
						raise_interrupt();
					} else {
						// FIFO underrun?
						m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
						m_s.status_reg1 = FDC_ST1_OR;
						m_s.status_reg2 = 0;
						PDEBUGF(LOG_V2, LOG_FDC, "<<WRITE DONE>> FIFO UND - DRV%u C=%u,H=%u,S=%u\n",
								drive, m_s.flopi[drive].cylinder, m_s.flopi[drive].head, m_s.flopi[drive].sector);
						enter_result_phase();
						return;
					}
				} else {
					// transfer next sector
					m_devices->dma()->set_DRQ(DMA_CHAN, true);
				}
			}
			step_head();
			break;

		case FDC_CMD_READ:
			if(m_s.main_status_reg & FDC_MSR_NONDMA) {
				if(m_s.floppy_buffer_index >= 512) {
					// FIFO overflow?
					// automatic TC with interrupt when host stops reading the FIFO
					m_s.TC = true;
					m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
					m_s.status_reg1 = FDC_ST1_OR;
					m_s.status_reg2 = 0;
					PDEBUGF(LOG_V2, LOG_FDC, "<<READ DONE>> FIFO OVR - DRV%u C=%u,H=%u,S=%u\n",
							drive, m_s.flopi[drive].cylinder, m_s.flopi[drive].head, m_s.flopi[drive].sector);
					enter_result_phase();
					return;
				} else {
					if(!(m_s.main_status_reg & FDC_MSR_RQM)) {
						// TODO verify this?
						// tell the host of available data only the first time.
						// the host will continue to read until TC.
						// auto-TC will happen if host stops reading before EOT
						raise_interrupt();
						// data byte waiting
						m_s.main_status_reg |= FDC_MSR_RQM | FDC_MSR_DIO;
					}
					// TC by FIFO overflow timeout
					// TODO should it be [time needed to reach next sector data
					// + time to fill FIFO]?
					g_machine.activate_timer(m_timer, calculate_rw_delay(drive,false)*1_us, false);
				}
			} else {
				m_s.floppy_buffer_index = 0;
				m_devices->dma()->set_DRQ(DMA_CHAN, true);
			}
			step_head();
			break;

		case FDC_CMD_FORMAT_TRACK:
			if((m_s.format_count == 0) || m_s.TC) {
				m_s.format_count = 0;
				m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
				PDEBUGF(LOG_V2, LOG_FDC, "<<FORMAT DONE>> - DRV%u C=%u,H=%u,S=%u\n",
						drive, m_s.flopi[drive].cylinder, m_s.flopi[drive].head, m_s.flopi[drive].sector);
				enter_result_phase();
				return;
			} else {
				// transfer next sector
				// TODO set a timer to force-end command if data is not provided?
				if(m_s.main_status_reg & FDC_MSR_NONDMA) {
					m_s.main_status_reg |= FDC_MSR_RQM;
					raise_interrupt();
				} else {
					m_devices->dma()->set_DRQ(DMA_CHAN, true);
				}
			}
			break;

		case FDC_CMD_RESET: // (contrived) RESET
			PDEBUGF(LOG_V1, LOG_FDC, "RESET\n");
			reset(DEVICE_SOFT_RESET);
			m_s.pending_command = FDC_CMD_INVALID;
			m_s.status_reg0 = FDC_ST0_IC_POLLING;
			raise_interrupt();
			m_s.reset_sensei = 4;
			break;

		case FDC_CMD_INVALID:
			PDEBUGF(LOG_V2, LOG_FDC, "timer(): nothing pending\n");
			break;

		default:
			PERRF_ABORT(LOG_FDC, "timer(): unknown case %02x\n", m_s.pending_command);
			break;
	}
}

uint16_t FloppyCtrl_Raw::read_data(uint8_t *_buffer_to, uint16_t _maxlen, bool _dma, bool _tc)
{
	uint8_t drive = current_drive();
	uint16_t len = 512 - m_s.floppy_buffer_index;
	if(len > _maxlen) {
		len = _maxlen;
	}

	memcpy(_buffer_to, &m_s.floppy_buffer[m_s.floppy_buffer_index], len);

	m_s.floppy_buffer_index += len;
	m_s.TC = get_TC(_tc) && (len == _maxlen);

	if((m_s.floppy_buffer_index >= 512) || (m_s.TC)) {
		if(m_s.floppy_buffer_index >= 512) {
			increment_sector(); // increment to next sector before retrieving next one
		}
		if(m_s.TC) { // Terminal Count line, done
			m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
			m_s.status_reg1 = 0;
			m_s.status_reg2 = 0;

			PDEBUGF(LOG_V2, LOG_FDC, "<<READ DONE>> TC - DRV%u C=%u,H=%u,S=%u\n",
					drive, m_s.flopi[drive].cylinder, m_s.flopi[drive].head, m_s.flopi[drive].sector);

			if(_dma) {
				m_devices->dma()->set_DRQ(DMA_CHAN, false);
			}
			enter_result_phase();
		} else {
			// more data to transfer
			floppy_xfer(drive, FROM_FLOPPY);

			if(_dma) {
				m_devices->dma()->set_DRQ(DMA_CHAN, false);
			}
			uint32_t sector_time = calculate_rw_delay(drive, false);
			g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);
		}
	}
	return len;
}

uint16_t FloppyCtrl_Raw::write_data(uint8_t *_buffer_from, uint16_t _maxlen, bool _dma, bool _tc)
{
	uint8_t drive = current_drive();
	uint32_t sector_time;

	if(m_s.cmd_code() == FDC_CMD_FORMAT_TRACK) {

		m_s.format_count--;
		switch (3 - (m_s.format_count & 0x03)) {
			case 0:
				//TODO seek time should be considered and added to the sector_time below
				m_s.flopi[drive].cylinder = *_buffer_from;
				break;
			case 1:
				if(*_buffer_from != m_s.flopi[drive].head) {
					PDEBUGF(LOG_V0, LOG_FDC, "head number does not match head field\n");
				}
				break;
			case 2:
				m_s.flopi[drive].sector = *_buffer_from;
				break;
			case 3:
				if(*_buffer_from != 2) {
					PDEBUGF(LOG_V0, LOG_FDC, "write_data: sector size %d not supported\n", 128<<(*_buffer_from));
				}
				PDEBUGF(LOG_V2, LOG_FDC, "formatting cylinder %u head %u sector %u\n",
						m_s.flopi[drive].cylinder, m_s.flopi[drive].head, m_s.flopi[drive].sector);
				for(unsigned i = 0; i < 512; i++) {
					m_s.floppy_buffer[i] = m_s.format_fillbyte;
				}

				floppy_xfer(drive, TO_FLOPPY);

				// can TC be asserted? should it be honored?
				// documentation doesn't say anything other than termination is when
				// fdc encounters a pulse on the IDX pin.
				if(_dma) {
					m_s.TC = get_TC(_tc) && (_maxlen==1);
					m_devices->dma()->set_DRQ(DMA_CHAN, false);
				} else {
					m_s.TC = false;
					m_s.main_status_reg &= ~FDC_MSR_RQM;
				}

				sector_time = calculate_rw_delay(drive, false);
				g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);
				break;
			default:
				assert(false); break;
		}
		return 1;

	} else { // write normal data

		uint16_t len = 512 - m_s.floppy_buffer_index;
		if(len > _maxlen) {
			len = _maxlen;
		}

		memcpy(&m_s.floppy_buffer[m_s.floppy_buffer_index], _buffer_from, len);

		m_s.floppy_buffer_index += len;
		m_s.TC = get_TC(_tc) && (len == _maxlen);

		if((m_s.floppy_buffer_index >= 512) || (m_s.TC)) {
			if(m_fdd[drive]->wpt_r()) {
				// write protected error
				PINFOF(LOG_V1, LOG_FDC, "tried to write disk %u, which is write-protected\n", drive);
				// ST0: IC1,0=01  (abnormal termination: started execution but failed)
				m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
				// ST1: DataError=1, NDAT=1, NotWritable=1, NID=1
				m_s.status_reg1 = FDC_ST1_DE | FDC_ST1_ND | FDC_ST1_NW | FDC_ST1_MA;
				// ST2: CRCE=1, SERR=1, BCYL=1(?), NDAM=1.
				m_s.status_reg2 = FDC_ST2_DD | FDC_ST2_WC | FDC_ST2_MD;
				enter_result_phase();
				return 1;
			}

			floppy_xfer(drive, TO_FLOPPY);

			sector_time = calculate_rw_delay(drive, false);
			if(m_s.floppy_buffer_index >= 512) {
				increment_sector(); // increment to next sector after writing current one
			}
			m_s.floppy_buffer_index = 0;
			if(_dma) {
				// wait until data transferred to disk
				m_devices->dma()->set_DRQ(DMA_CHAN, false);
			}
			g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);
		}
		return len;
	}
}

uint16_t FloppyCtrl_Raw::dma_write(uint8_t *buffer, uint16_t maxlen, bool _tc)
{
	// A DMA write is from I/O to Memory
	// We need to return the next data byte(s) from the floppy buffer
	// to be transfered via the DMA to memory. (read block from floppy)
	//
	// maxlen is the maximum length of the DMA transfer

	m_devices->sysboard()->set_feedback();

	PDEBUGF(LOG_V2, LOG_FDC, "DMA write DRV%u, maxlen=%u, tc=%u\n", current_drive(),
			maxlen, _tc);

	return read_data(buffer, maxlen, true, _tc);
}

uint16_t FloppyCtrl_Raw::dma_read(uint8_t *buffer, uint16_t maxlen, bool _tc)
{
	// A DMA read is from Memory to I/O
	// We need to write the data_byte which was already transfered from memory
	// via DMA to I/O (write block to floppy)
	//
	// maxlen is the length of the DMA transfer

	m_devices->sysboard()->set_feedback();

	PDEBUGF(LOG_V2, LOG_FDC, "DMA read DRV%u, maxlen=%u, tc=%u\n", current_drive(),
			maxlen, _tc);

	return write_data(buffer, maxlen, true, _tc);
}

void FloppyCtrl_Raw::raise_interrupt()
{
	if((m_s.DOR & FDC_DOR_NDMAGATE) && !m_s.pending_irq) {
		PDEBUGF(LOG_V2, LOG_FDC, "Raising IRQ %d\n", IRQ_LINE);
		m_devices->pic()->raise_irq(IRQ_LINE);
		m_s.pending_irq = true;
	}
	m_s.reset_sensei = 0;
}

void FloppyCtrl_Raw::lower_interrupt()
{
	if(m_s.pending_irq) {
		PDEBUGF(LOG_V2, LOG_FDC, "Lowering IRQ %d\n", IRQ_LINE);
		m_devices->pic()->lower_irq(IRQ_LINE);
		m_s.pending_irq = false;
	}
}

void FloppyCtrl_Raw::increment_sector()
{
	// this is the internal sector address update
	// current head position is m_s.cur_cylinder

	uint8_t drive = current_drive();
	assert(is_drive_present(drive));

	auto mprops = m_fdd[drive]->get_media_props();

	// values after completion of data xfer
	// ??? calculation depends on base_count being multiple of 512
	m_s.flopi[drive].sector++;
	if((m_s.flopi[drive].sector > m_s.flopi[drive].eot) || (m_s.flopi[drive].sector > mprops.spt)) {
		m_s.flopi[drive].sector = 1;
		if(m_s.multi_track) {
			m_s.flopi[drive].head++;
			if(m_s.flopi[drive].head > 1) {
				m_s.flopi[drive].head = 0;
				m_s.flopi[drive].cylinder++;
			}
			m_fdd[drive]->ss_w(m_s.flopi[drive].head);
		} else {
			m_s.flopi[drive].cylinder++;
		}
		if(m_s.flopi[drive].cylinder >= mprops.tracks) {
			// Set to 1 past last possible cylinder value.
			// I notice if I set it to tracks-1, prama linux won't boot.
			m_s.flopi[drive].cylinder = m_fdd[drive]->get_media_props().tracks;
			PDEBUGF(LOG_V1, LOG_FDC, "increment_sector: clamping cylinder to max\n");
		}
	}
}

void FloppyCtrl_Raw::enter_result_phase()
{
	uint8_t drive = current_drive();

	// these are always the same
	m_s.result_index = 0;
	// not necessary to clear any status bits, we're about to set them all
	// CMDBUSY will be cleared at the end of the result phase
	m_s.main_status_reg |= FDC_MSR_RQM | FDC_MSR_DIO | FDC_MSR_CMDBUSY;

	bool raise_int = false;
	if((m_s.status_reg0 & FDC_ST0_IC) == FDC_ST0_IC_INVALID) {
		// invalid command
		m_s.result_size = 1;
		m_s.result[0] = m_s.status_reg0;
	} else {
		switch (m_s.cmd_code()) {
		case FDC_CMD_SENSE_DRIVE:
			m_s.result_size = 1;
			m_s.result[0] = m_s.status_reg3;
			break;
		case FDC_CMD_SENSE_INT:
			m_s.result_size = 2;
			m_s.result[0] = m_s.status_reg0;
			m_s.result[1] = m_s.flopi[drive].cur_cylinder;
			break;
		case FDC_CMD_DUMPREG:
			m_s.result_size = 10;
			for(unsigned i = 0; i < 4; i++) {
				m_s.result[i] = m_s.flopi[drive].cur_cylinder;
			}
			m_s.result[4] = (m_s.SRT << 4) | m_s.HUT;
			m_s.result[5] = (m_s.HLT << 1) | ((m_s.main_status_reg & FDC_MSR_NONDMA) ? 1 : 0);
			m_s.result[6] = m_s.flopi[drive].eot;
			m_s.result[7] = (m_s.lock << 7) | (m_s.perp_mode & 0x7f);
			m_s.result[8] = m_s.config;
			m_s.result[9] = m_s.pretrk;
			break;
		case FDC_CMD_VERSION:
			m_s.result_size = 1;
			m_s.result[0] = 0x90;
			break;
		case FDC_CMD_LOCK:
			m_s.result_size = 1;
			m_s.result[0] = (m_s.lock << 4);
			break;
		case FDC_CMD_READ_ID:
		case FDC_CMD_FORMAT_TRACK:
		case FDC_CMD_READ:
		case FDC_CMD_WRITE:
			m_s.result_size = 7;
			m_s.result[0] = m_s.status_reg0;
			m_s.result[1] = m_s.status_reg1;
			m_s.result[2] = m_s.status_reg2;
			m_s.result[3] = m_s.flopi[drive].cylinder;
			m_s.result[4] = m_s.flopi[drive].head;
			m_s.result[5] = m_s.flopi[drive].sector;
			m_s.result[6] = 2; // sector size code
			raise_int = true;
			break;
		default:
			assert(false); break;
		}
	}

	// exit execution phase
	m_s.pending_command = FDC_CMD_INVALID;

	PDEBUGF(LOG_V2, LOG_FDC, "RESULT: %s\n", bytearray_to_string(m_s.result,m_s.result_size).c_str());

	if(raise_int) {
		raise_interrupt();
	}
}

void FloppyCtrl_Raw::enter_idle_phase()
{
	m_s.main_status_reg &= (FDC_MSR_NONDMA | 0x0f);  // leave drive status untouched
	m_s.main_status_reg |= FDC_MSR_RQM; // data register ready

	m_s.pending_command = FDC_CMD_INVALID;
	m_s.command_complete = true; // waiting for new command
	m_s.command_index = 0;
	m_s.command_size = 0;
	m_s.result_size = 0;

	m_s.floppy_buffer_index = 0;
}

uint32_t FloppyCtrl_Raw::get_one_step_delay_time_us()
{
	// returns microseconds
	return (16 - m_s.SRT) * (500'000 / drate_in_k[m_s.data_rate]);
}

uint32_t FloppyCtrl_Raw::calculate_step_delay_us(uint8_t _drive, int _c1)
{
	assert(_drive < 4);

	// returns microseconds
	return calculate_step_delay_us(_drive, m_s.flopi[_drive].cur_cylinder, _c1);
}

uint32_t FloppyCtrl_Raw::calculate_rw_delay(uint8_t _drive, bool _latency)
{
	// returns microseconds

	assert(_drive < 4);
	uint32_t sector_time_us, max_latency_us;
	uint64_t now_us = g_machine.get_virt_time_us();

	if(m_fdd[_drive]->type() == FloppyDrive::FDD_525HD) {
		max_latency_us = (60e6 / 360); // 1 min in us / rpm
	} else {
		max_latency_us = (60e6 / 300);
	}

	// us to read 1 sector
	sector_time_us = max_latency_us / m_fdd[_drive]->get_media_props().spt;

	// Head Load Time
	uint32_t hlt = m_s.HLT;
	if(hlt == 0) {
		hlt = 128;
	}
	hlt *= 1000000 / drate_in_k[m_s.data_rate];

	if(m_s.flopi[_drive].last_hut < now_us) {
		// if the head has been unloaded, add the load time
		sector_time_us += hlt;
	}

	if(_latency) {
		// add average rotational latency?
		// average latency is half the max latency
		// I reduce it further for better results (probably due to HLT happening
		// concurrently ... ?)
		sector_time_us += (max_latency_us / 2.2) * m_latency_mult;
	}

	// Head Unload Time
	uint32_t hut = m_s.HUT;
	if(hut == 0) {
		hut = 128;
	}
	hut *= 8000000 / drate_in_k[m_s.data_rate];

	PDEBUGF(LOG_V2, LOG_FDC, "sector time = %d us\n", sector_time_us);

	// set the unload timeout
	m_s.flopi[_drive].last_hut = now_us + sector_time_us + hut;

	return sector_time_us;
}

void FloppyCtrl_Raw::step_head()
{
	uint8_t drive = current_drive();
	if(is_motor_on(drive) && m_s.flopi[drive].cur_cylinder != m_fdd[drive]->get_cyl()) {
		m_s.flopi[drive].step = true;
		m_s.flopi[drive].cur_cylinder = m_fdd[drive]->get_cyl();
	}
}

bool FloppyCtrl_Raw::get_TC(bool _dma_tc)
{
	bool terminal_count;
	if(m_s.main_status_reg & FDC_MSR_NONDMA) {
		// figure out if we've sent all the data, in non-DMA mode...
		// the drive stays on the same cylinder for a read or write, so that's
		// not going to be an issue. EOT stands for the last sector to be I/Od.
		// it does all the head 0 sectors first, then the second if any.
		// now, regarding reaching the end of the sector:
		//  == 512 would make it more precise, allowing one to spot bugs...
		//  >= 512 makes it more robust, but allows for sloppy code...
		//  pick your poison?
		// note: byte and head are 0-based; eot, sector, and heads are 1-based.
		uint8_t drive = current_drive();
		terminal_count = ((m_s.floppy_buffer_index == 512) &&
		                 (m_s.flopi[drive].sector == m_s.flopi[drive].eot));
		if(m_s.multi_track) {
			terminal_count &= (m_s.flopi[drive].head == (m_fdd[drive]->get_media_props().sides - 1));
		}
	} else {
		terminal_count = _dma_tc;
	}
	return terminal_count;
}

