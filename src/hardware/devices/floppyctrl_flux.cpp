// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

/*
 * Intel 82077AA Floppy Disk Controller
 * Based on MAME's devices/machine/upd765.cpp, devices/machine/fdc_pll.cpp
 * For flux-based disk images.
 */

#include "ibmulator.h"
#include "program.h"
#include "dma.h"
#include "pic.h"
#include "hardware/devices/systemboard.h"
#include "floppyctrl_flux.h"
#include "floppyfmt_img.h"
#include "floppyfmt_hfe.h"
#include "floppyfmt_ipf.h"
#include "floppyfmt_td0.h"
#include "floppyfmt_imd.h"

const std::map<unsigned,FloppyCtrl_Flux::CmdDef> FloppyCtrl_Flux::ms_cmd_list = {
	{ FDC_CMD_READ        , {FDC_CMD_READ        , 9, "read data",          &FloppyCtrl_Flux::cmd_read_data} },
	{ FDC_CMD_READ_DEL    , {FDC_CMD_READ_DEL    , 9, "read deleted data",  &FloppyCtrl_Flux::cmd_read_data} },
	{ FDC_CMD_WRITE       , {FDC_CMD_WRITE       , 9, "write data",         &FloppyCtrl_Flux::cmd_write_data} },
	{ FDC_CMD_WRITE_DEL   , {FDC_CMD_WRITE_DEL   , 9, "write deleted data", &FloppyCtrl_Flux::cmd_write_data} },
	{ FDC_CMD_READ_TRACK  , {FDC_CMD_READ_TRACK  , 9, "read track",         &FloppyCtrl_Flux::cmd_read_track} },
	{ FDC_CMD_VERIFY      , {FDC_CMD_VERIFY      , 9, "verify",             &FloppyCtrl_Flux::cmd_not_implemented} },
	{ FDC_CMD_VERSION     , {FDC_CMD_VERSION     , 1, "version",            &FloppyCtrl_Flux::cmd_version} },
	{ FDC_CMD_FORMAT_TRACK, {FDC_CMD_FORMAT_TRACK, 6, "format track",       &FloppyCtrl_Flux::cmd_format_track} },
	{ FDC_CMD_SCAN_EQ     , {FDC_CMD_SCAN_EQ     , 9, "scan equal",         &FloppyCtrl_Flux::cmd_scan} },
	{ FDC_CMD_SCAN_LO_EQ  , {FDC_CMD_SCAN_LO_EQ  , 9, "scan low or equal",  &FloppyCtrl_Flux::cmd_scan} },
	{ FDC_CMD_SCAN_HI_EQ  , {FDC_CMD_SCAN_HI_EQ  , 9, "scan high or equal", &FloppyCtrl_Flux::cmd_scan} },
	{ FDC_CMD_RECALIBRATE , {FDC_CMD_RECALIBRATE , 2, "recalibrate",        &FloppyCtrl_Flux::cmd_recalibrate} },
	{ FDC_CMD_SENSE_INT   , {FDC_CMD_SENSE_INT   , 1, "sense interrupt",    &FloppyCtrl_Flux::cmd_sense_int} },
	{ FDC_CMD_SPECIFY     , {FDC_CMD_SPECIFY     , 3, "specify",            &FloppyCtrl_Flux::cmd_specify} },
	{ FDC_CMD_SENSE_DRIVE , {FDC_CMD_SENSE_DRIVE , 2, "sense drive status", &FloppyCtrl_Flux::cmd_sense_drive} },
	{ FDC_CMD_CONFIGURE   , {FDC_CMD_CONFIGURE   , 4, "configure",          &FloppyCtrl_Flux::cmd_configure} },
	{ FDC_CMD_SEEK        , {FDC_CMD_SEEK        , 3, "seek",               &FloppyCtrl_Flux::cmd_seek} },
	{ FDC_CMD_DUMPREG     , {FDC_CMD_DUMPREG     , 1, "dumpreg",            &FloppyCtrl_Flux::cmd_dumpreg} },
	{ FDC_CMD_READ_ID     , {FDC_CMD_READ_ID     , 2, "read ID",            &FloppyCtrl_Flux::cmd_read_id} },
	{ FDC_CMD_PERP_MODE   , {FDC_CMD_PERP_MODE   , 2, "perpendicular mode", &FloppyCtrl_Flux::cmd_perp_mode} },
	{ FDC_CMD_LOCK        , {FDC_CMD_LOCK        , 1, "lock/unlock",        &FloppyCtrl_Flux::cmd_lock} },
	{ FDC_CMD_INVALID     , {FDC_CMD_INVALID     , 1, "INVALID COMMAND",    &FloppyCtrl_Flux::cmd_invalid} }
};

#define FDC_DOR_DRIVE(DRIVE) ((m_s.DOR & 0xFC) | DRIVE)


FloppyCtrl_Flux::FloppyCtrl_Flux(Devices *_dev)
: FloppyCtrl(_dev)
{
	m_floppy_formats.emplace_back(new FloppyFmt_IMG());
	m_floppy_formats.emplace_back(new FloppyFmt_HFE());
	m_floppy_formats.emplace_back(new FloppyFmt_IPF());
	m_floppy_formats.emplace_back(new FloppyFmt_TD0());
	m_floppy_formats.emplace_back(new FloppyFmt_IMD());
}

void FloppyCtrl_Flux::install()
{
	FloppyCtrl::install();

	memset(&m_s, 0, sizeof(m_s));

	using namespace std::placeholders;
	m_devices->dma()->register_8bit_channel(
			DMA_CHAN,
			std::bind(&FloppyCtrl_Flux::dma_read, this, _1, _2, _3),
			std::bind(&FloppyCtrl_Flux::dma_write, this, _1, _2, _3),
			std::bind(&FloppyCtrl_Flux::tc_w, this, _1),
			name());
	g_machine.register_irq(IRQ_LINE, name());

	m_polling_timer = g_machine.register_timer(
			std::bind(&FloppyCtrl_Flux::timer_polling, this, _1),
			name()
	);
	for(unsigned drive=0; drive<MAX_DRIVES; drive++) {
		// keep timers creation here
		// some commands require timers regardless of drive presence
		m_fdd_timers[drive] = g_machine.register_timer(
				std::bind(&FloppyCtrl_Flux::timer_fdd, this, drive, std::placeholders::_1),
				str_format("Floppy Drive %u", drive));
	}

	PINFOF(LOG_V0, LOG_FDC, "Installed Intel 82077AA floppy disk controller (Flux images)\n");
}

void FloppyCtrl_Flux::remove()
{
	FloppyCtrl::remove();

	g_machine.unregister_timer(m_polling_timer);
	for(unsigned drive=0; drive<MAX_DRIVES; drive++) {
		floppy_drive_remove(drive);
		g_machine.unregister_timer(m_fdd_timers[drive]);
		m_fdd_timers[drive] = NULL_TIMER_HANDLE;
	}

	m_devices->dma()->unregister_channel(DMA_CHAN);
	g_machine.unregister_irq(IRQ_LINE, name());
}

void FloppyCtrl_Flux::config_changed()
{
	FloppyCtrl::config_changed();

	m_min_cmd_time_us = g_program.config().get_int(DRIVES_SECTION, DRIVES_FDC_OVR, 0);
	PINFOF(LOG_V2, LOG_FDC, "Controller overhead: %uus\n", m_min_cmd_time_us);
}

void FloppyCtrl_Flux::save_state(StateBuf &_state)
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

void FloppyCtrl_Flux::restore_state(StateBuf &_state)
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

void FloppyCtrl_Flux::reset(unsigned type)
{
	if(type == MACHINE_POWER_ON) {
		// DMA is enabled from start
		memset(&m_s, 0, sizeof(m_s));
	}

	if(type != DEVICE_SOFT_RESET) {
		// HARD reset and power on
		//   motor off drive 3..0
		//   DMA/INT enabled
		//   drive select 0
		//   data rate 250 Kbps (p.10)
		//   unlocked
		// SOFT reset (via DOR port 0x3f2 bit 2) does not change DOR
		m_s.DOR = FDC_DOR_NDMAGATE | FDC_DOR_NRESET;
		m_s.data_rate = 2; // 250 Kbps
		m_s.lock = false;
	}

	// ALL resets

	// The non-DMA mode flag, step rate (SRT), head load
	// (HLT), and head unload times (HUT) programmed by
	// the SPECIFY command do not default to a known
	// state after a reset. This behavior is consistent with
	// the 8272A.
	m_s.main_status_reg &= FDC_MSR_NONDMA; // keep NDMA bit value only
	m_s.st1 = 0;
	m_s.st2 = 0;
	m_s.st3 = 0;

	if(!m_s.lock) {
		m_s.config = FDC_CONF_EFIFO; // EFIFO=1 8272A compatible mode FIFO is disabled
		m_s.pretrk = 0;
	}
	m_s.perp_mode = 0;

	m_s.C = 0;
	m_s.H = 0;
	m_s.R = 0;
	m_s.EOT = 0;

	for(int i=0; i<4; i++) {
		m_s.flopi[i].main_state = IDLE;
		m_s.flopi[i].sub_state = IDLE;
		m_s.flopi[i].live = false;
		m_s.flopi[i].st0 = i;
		m_s.flopi[i].st0_filled = false;
		m_s.flopi[i].step   = false;
		m_s.flopi[i].wrdata = false;
		m_s.flopi[i].rddata = false;
		m_s.flopi[i].pcn = 0;
		m_s.flopi[i].hut = 0;
	}

	for(unsigned i=0; i<MAX_DRIVES; i++) {
		if(m_fdd[i]) {
			m_fdd[i]->reset(type);
		}
	}

	m_s.data_irq = false;
	m_s.other_irq = false;
	m_s.pending_irq = false;
	m_devices->pic()->lower_irq(IRQ_LINE);

	m_s.internal_drq = false;
	m_s.tc_done = false;

	m_s.fifo_pos = 0;

	if(!(m_s.main_status_reg & FDC_MSR_NONDMA)) {
		m_devices->dma()->set_DRQ(DMA_CHAN, false);
	}

	m_s.cur_live.tm = TIME_NEVER;
	m_s.cur_live.state = IDLE;
	m_s.cur_live.next_state = -1;
	m_s.cur_live.drive = 0;

	// polling timer will start as soon as DOR bit 2 is set
}

void FloppyCtrl_Flux::power_off()
{
	for(unsigned i=0; i<MAX_DRIVES; i++) {
		if(m_fdd[i]) {
			m_fdd[i]->power_off();
		}
	}
	m_s.DOR = 0;
}

uint16_t FloppyCtrl_Flux::read(uint16_t _address, unsigned)
{
	uint8_t value=0, drive=current_drive();

	PDEBUGF(LOG_V2, LOG_FDC, "read  0x%04X [%02X] ", _address, m_s.pending_command);

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
			if(m_fdd[drive]) {
				value |= (!m_fdd[drive]->ss_r()) << 3;
			}
			// Bit 2 : INDEX
			if(m_s.flopi[drive].index == 0) {
				value |= 1<<2;
			}
			// Bit 1 : WP
			if(is_media_present(drive)) {
				value |= m_fdd[drive]->wpt_r() << 1;
			}
			// Bit 0 : !DIR
			value |= !m_s.flopi[drive].dir;

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
				// of a command.
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

			if(m_s.data_irq) {
				m_s.data_irq = false;
				check_irq();
			}

			break;
		}
		case 0x3F5: // Data
		{
			if(m_s.result_size > 0) {
				unsigned ridx = m_s.result_index + 1;
				unsigned rsize = m_s.result_size;
				value = m_s.result[m_s.result_index++];
				PDEBUGF(LOG_V2, LOG_FDC, "R%d/%d -> 0x%02X\n", ridx, rsize, value);
				m_s.main_status_reg &= 0xF0;
				if(m_s.result_index >= m_s.result_size) {
					enter_idle_phase();
				}
			} else if(m_s.pending_command != FDC_CMD_INVALID && m_s.internal_drq) {
				value = fifo_pop(false);
				PDEBUGF(LOG_V2, LOG_FDC, "FIFO -> 0x%02X\n", value);
			} else {
				PDEBUGF(LOG_V2, LOG_FDC, "FIFO -> 0 (read with no data)\n");
			}
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

void FloppyCtrl_Flux::write(uint16_t _address, uint16_t _value, unsigned)
{
	PDEBUGF(LOG_V2, LOG_FDC, "write 0x%04X      ", _address);

	m_devices->sysboard()->set_feedback();

	switch (_address) {

		case 0x3F2: // Digital Output Register (DOR)
		{
			uint8_t drive_sel  = _value & FDC_DOR_DRVSEL;
			uint8_t cur_normal_op = _value & FDC_DOR_NRESET;
			uint8_t prev_normal_op = m_s.DOR & FDC_DOR_NRESET;

			m_s.DOR = _value;

			PDEBUGF(LOG_V2, LOG_FDC, "DOR  <- 0x%02X ", _value);
			if(_value & FDC_DOR_MOTEN0)   { PDEBUGF(LOG_V2, LOG_FDC, "MOT0 "); }
			if(_value & FDC_DOR_MOTEN1)   { PDEBUGF(LOG_V2, LOG_FDC, "MOT1 "); }
			if(_value & FDC_DOR_MOTEN2)   { PDEBUGF(LOG_V2, LOG_FDC, "MOT2 "); }
			if(_value & FDC_DOR_MOTEN3)   { PDEBUGF(LOG_V2, LOG_FDC, "MOT3 "); }
			if(_value & FDC_DOR_NDMAGATE) { PDEBUGF(LOG_V2, LOG_FDC, "!DMAGATE "); }
			if(_value & FDC_DOR_NRESET)   { PDEBUGF(LOG_V2, LOG_FDC, "!RESET "); }
			PDEBUGF(LOG_V2, LOG_FDC, "DRVSEL=%01X\n", drive_sel);

			// DOR RESET
			if(prev_normal_op==0 && cur_normal_op) {
				// transition from RESET state to NORMAL operation
				enter_idle_phase();
				g_machine.activate_timer(m_polling_timer, 250_us, false); // once
			} else if(prev_normal_op && cur_normal_op==0) {
				// transition from NORMAL operation to RESET state
				m_s.pending_command = FDC_CMD_RESET; // RESET is pending...
				PDEBUGF(LOG_V2, LOG_FDC, "RESET via DOR\n");
				reset(DEVICE_SOFT_RESET);
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
			auto old_data_rate = m_s.data_rate;
			m_s.data_rate = _value & FDC_DSR_DRATE_SEL;
			if(_value & FDC_DSR_SW_RESET) {
				// S/W RESET behaves the same as DOR RESET except that this
				// reset is self clearing.
				PDEBUGF(LOG_V2, LOG_FDC, "RESET via DSR\n");
				reset(DEVICE_SOFT_RESET);
				m_s.DOR |= FDC_DOR_NRESET;
				enter_idle_phase();
				g_machine.activate_timer(m_polling_timer, 250_us, false);
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
			if(m_s.data_rate != old_data_rate) {
				PDEBUGF(LOG_V1, LOG_FDC, "Data rate=%uk\n", drate_in_k[m_s.data_rate]);
			}
			break;
		}
		case 0x3F5: // Data FIFO
		{
			if(!(m_s.DOR & FDC_DOR_NRESET)) {
				PDEBUGF(LOG_V2, LOG_FDC, "FIFO <- 0x%02X write while in RESET state\n", _value);
				return;
			}
			if(m_s.pending_command != FDC_CMD_INVALID) {
				if(m_s.internal_drq) {
					PDEBUGF(LOG_V2, LOG_FDC, "FIFO <- 0x%02X\n", _value);
					fifo_push(_value, false);
					return;
				}
				PDEBUGF(LOG_V2, LOG_FDC, "FIFO <- 0x%02X unexpected byte\n", _value);
			} else if(m_s.command_complete) {
				// in idle phase, start command phase
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
				assert(m_s.command_size <= 10);
				PDEBUGF(LOG_V2, LOG_FDC, "D1/%d <- 0x%02X (cmd: %s)\n",
						m_s.command_size, _value, cmd_def->second.name);
				m_s.other_irq = false;
				check_irq();
			} else {
				// in command phase
				assert(m_s.command_index < m_s.command_size);
				m_s.command[m_s.command_index++] = _value;
				PDEBUGF(LOG_V2, LOG_FDC, "D%d/%d <- 0x%02X\n", m_s.command_index, m_s.command_size, _value);
			}
			if(m_s.command_index == m_s.command_size) {
				// exit command phase
				m_s.command_complete = true;
				enter_execution_phase();
			}
			return;
		}
		case 0x3F7: // Configuration Control Register (CCR)
		{
			PDEBUGF(LOG_V2, LOG_FDC, "CCR  <- 0x%02X ", _value);
			auto old_data_rate = m_s.data_rate;
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
			if(m_s.data_rate != old_data_rate) {
				PDEBUGF(LOG_V1, LOG_FDC, "Data rate=%uk\n", drate_in_k[m_s.data_rate]);
			}
			break;
		}
		default:
			PDEBUGF(LOG_V0, LOG_FDC, "    <- 0x%02X ignored\n", _value);
			break;

	}
}

void FloppyCtrl_Flux::enter_execution_phase()
{
	PDEBUGF(LOG_V1, LOG_FDC, "COMMAND: ");
	PDEBUGF(LOG_V2, LOG_FDC, "%s ", bytearray_to_string(m_s.command,m_s.command_size).c_str());

	// controller is busy, data FIFO is not ready.
	// this is also the "hang" condition.
	// fdc hangs should be handled by the host software with a timeout counter and a reset.
	// CMDBUSY will be cleared at the end of the result phase.
	m_s.main_status_reg &= FDC_MSR_NONDMA;
	m_s.main_status_reg |= FDC_MSR_CMDBUSY;

	m_s.pending_command = m_s.command[0];

	m_s.tc_done = false;

	auto cmd_def = ms_cmd_list.find(m_s.cmd_code());
	if(cmd_def == ms_cmd_list.end()) {
		cmd_def = ms_cmd_list.find(FDC_CMD_INVALID);
	}
	cmd_def->second.fn(*this);
}

unsigned FloppyCtrl_Flux::st_hds_drv(unsigned _drive)
{
	assert(_drive < MAX_DRIVES);
	if(m_fdd[_drive]) {
		return ((m_s.H << 2) | _drive);
	}
	return _drive;
}

bool FloppyCtrl_Flux::start_read_write_cmd()
{
	const char *cmd = FloppyCtrl_Flux::ms_cmd_list.at(m_s.pending_command & FDC_CMD_MASK).name;

	if((m_s.DOR & FDC_DOR_NDMAGATE) == 0) {
		PWARNF(LOG_V0, LOG_FDC, "%s with INT disabled is untested!\n", cmd);
	}
	uint8_t drive = m_s.cmd_drive();
	if(m_s.flopi[drive].main_state != IDLE) {
		PDEBUGF(LOG_V1, LOG_FDC, "Drive not in IDLE state!\n");
	}

	m_s.DOR = FDC_DOR_DRIVE(drive);

	uint8_t cylinder    = m_s.command[2];
	uint8_t head        = m_s.command[3] & 0x01;
	uint8_t sector      = m_s.command[4];
	uint8_t sector_size = m_s.command[5];
	uint8_t eot         = m_s.command[6];
	uint8_t gpl         = m_s.command[7];
	uint8_t data_length = m_s.command[8];

	PDEBUGF(LOG_V1, LOG_FDC, "%s, DRV%u, %s C=%u,H=%u,S=%u,N=%u,EOT=%u,GPL=%u,DTL=%u, rate=%uk, PCN=%u\n",
			cmd, drive, m_s.cmd_mtrk()?"MT,":"",
			cylinder, head, sector, sector_size, eot, gpl, data_length,
			drate_in_k[m_s.data_rate],
			m_s.flopi[drive].pcn);

	if(!is_motor_on(drive)) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: motor not on\n", cmd);
		return false; // Hang controller
	}

	if(!is_media_present(drive)) {
		// the controller would fail to receive the index pulse and lock-up
		// since the index pulses are required for termination of the execution phase.
		PDEBUGF(LOG_V1, LOG_FDC, "%s: attempt to %s with media not present\n",
				cmd, cmd);
		return false; // Hang controller
	}

	m_s.C   = cylinder;
	m_s.H   = head;
	m_s.R   = sector;
	m_s.EOT = eot;

	m_s.flopi[drive].dir = (m_s.flopi[drive].pcn > cylinder);
	m_fdd[drive]->ss_w(m_s.cmd_head()); // side select from the command byte 1

	uint32_t step_time_us = 0;
	if((m_s.config & FDC_CONF_EIS) && m_s.C != m_s.flopi[drive].pcn) {
		m_fdd[drive]->dir_w(m_s.flopi[drive].dir);
		m_s.flopi[drive].seek_c = m_s.C;
		m_s.flopi[drive].step = true;
		m_s.flopi[drive].hut = 0;
		step_time_us = calculate_step_delay_us(drive, m_s.C);
		m_fdd[drive]->step_to(m_s.C, step_time_us*1_us);
	} else {
		m_s.flopi[drive].seek_c = m_s.flopi[drive].pcn;
	}

	uint32_t head_load_us = calculate_head_delay_us(drive);
	uint64_t next_evt_us = step_time_us + head_load_us + m_min_cmd_time_us;

	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: next event SEEK_DONE in %uus (step=%u, head=%u, ovr=%u)\n",
			drive, next_evt_us, step_time_us, head_load_us, m_min_cmd_time_us);

	g_machine.activate_timer(m_fdd_timers[drive],
			(next_evt_us)*1_us,
			false);

	return true;
}

void FloppyCtrl_Flux::cmd_read_data()
{
	if(!start_read_write_cmd()) {
		return;
	}

	uint8_t drive = m_s.cmd_drive();

	m_s.flopi[drive].main_state = READ_DATA;
	m_s.flopi[drive].sub_state = SEEK_DONE;

	m_s.flopi[drive].st0 = st_hds_drv(drive);
	m_s.st1 = FDC_ST1_MA;
	m_s.st2 = 0;
}

void FloppyCtrl_Flux::cmd_write_data()
{
	if(!start_read_write_cmd()) {
		return;
	}

	uint8_t drive = m_s.cmd_drive();

	if(m_fdd[drive]->wpt_r()) {
		PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: disk is write protected!\n");
		g_machine.deactivate_timer(m_fdd_timers[drive]);
		m_s.flopi[drive].st0 = FDC_ST0_IC_ABNORMAL | st_hds_drv(drive);
		m_s.st1 = FDC_ST1_NW;
		m_s.st2 = 0;
		enter_result_phase(drive);
		return;
	}

	m_s.flopi[drive].main_state = WRITE_DATA;
	m_s.flopi[drive].sub_state = SEEK_DONE;

	m_s.flopi[drive].st0 = st_hds_drv(drive);
	m_s.st1 = FDC_ST1_MA;
	m_s.st2 = 0;
}

void FloppyCtrl_Flux::cmd_read_track()
{
	if(!start_read_write_cmd()) {
		return;
	}

	uint8_t drive = m_s.cmd_drive();

	m_s.flopi[drive].main_state = READ_TRACK;
	m_s.flopi[drive].sub_state = SEEK_DONE;

	m_s.flopi[drive].st0 = st_hds_drv(drive);
	m_s.st1 = FDC_ST1_MA;
	m_s.st2 = 0;
}

void FloppyCtrl_Flux::cmd_version()
{
	PDEBUGF(LOG_V1, LOG_FDC, "version\n");
	enter_result_phase(0);
}

void FloppyCtrl_Flux::cmd_format_track()
{
	uint8_t drive = m_s.cmd_drive();
	m_s.DOR = FDC_DOR_DRIVE(drive);

	PDEBUGF(LOG_V1, LOG_FDC, "format track, DRV%u, N=%u,SC=%u,GPL=%u,D=%02x\n", drive,
			m_s.command[2], m_s.command[3], m_s.command[4], m_s.command[5]);

	if(!is_motor_on(drive)) {
		PDEBUGF(LOG_V1, LOG_FDC, "DRV%d: motor not on\n", drive);
		return; // Hang controller
	}

	if(!is_media_present(drive)) {
		PDEBUGF(LOG_V0, LOG_FDC, "format track: attempt to format track with media not present\n");
		return; // Hang controller
	}

	if(m_fdd[drive]->wpt_r()) {
		PINFOF(LOG_V0, LOG_FDC, "Attempt to format disk with media write-protected\n");
		m_s.flopi[drive].st0 = FDC_ST0_IC_ABNORMAL | st_hds_drv(drive);
		m_s.st1 = FDC_ST1_NW;
		m_s.st2 = 0;
		enter_result_phase(drive);
		return;
	}
	m_s.sector_size = calc_sector_size(m_s.command[2]);
	m_s.H = m_s.cmd_head();
	m_fdd[drive]->ss_w(m_s.H);

	m_s.flopi[drive].st0 = st_hds_drv(drive);
	m_s.st1 = 0;
	m_s.st2 = 0;

	m_s.flopi[drive].main_state = FORMAT_TRACK;
	m_s.flopi[drive].sub_state = HEAD_LOAD_DONE;

	uint32_t head_load_time_us = calculate_head_delay_us(drive);
	uint64_t next_evt_us = head_load_time_us + m_min_cmd_time_us;

	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: next event HEAD_LOAD_DONE in %uus (head=%u, ovr=%u)\n",
			drive, next_evt_us, head_load_time_us, m_min_cmd_time_us);

	g_machine.activate_timer(m_fdd_timers[drive],
			next_evt_us*1_us,
			false);
}

void FloppyCtrl_Flux::cmd_scan()
{
	if(!start_read_write_cmd()) {
		return;
	}

	uint8_t drive = m_s.cmd_drive();

	m_s.flopi[drive].main_state = SCAN_DATA;
	m_s.flopi[drive].sub_state = SEEK_DONE;

	m_s.flopi[drive].st0 = st_hds_drv(drive);
	m_s.st1 = FDC_ST1_MA;
	m_s.st2 = 0;

	m_s.scan_done = false;
}

void FloppyCtrl_Flux::cmd_recalibrate()
{
	uint8_t drive = m_s.cmd_drive();
	if(m_s.flopi[drive].main_state != IDLE) {
		PDEBUGF(LOG_V1, LOG_FDC, "Drive not in IDLE state!\n");
	}

	m_s.DOR = FDC_DOR_DRIVE(drive);

	// clear RQM and CMDBUSY, set drive busy
	//  during the execution phase the controller is in NON BUSY state.
	// DRV x BUSY
	//  These bits are set to ones when a drive is in the seek portion of
	//  a command, including seeks, and recalibrates.
	m_s.main_status_reg &= FDC_MSR_NONDMA;
	m_s.main_status_reg |= (1 << drive);

	m_s.flopi[drive].main_state = RECALIBRATE;
	m_s.flopi[drive].sub_state = RECALIBRATE_WAIT_DONE;
	m_s.flopi[drive].dir = 1;
	m_s.flopi[drive].st0 = 0;

	// The 82077AA clears the contents of the PCN counter and checks the status
	// of the TRK0 pin from the FDD. (p.30)
	m_s.flopi[drive].pcn = 0;

	uint8_t seek_to_cyl = 0;
	uint32_t step_delay_us = 0;
	if(m_fdd[drive]) {
		PDEBUGF(LOG_V1, LOG_FDC, "recalibrate, DRV%u (cur.C=%u)\n", drive,
				m_fdd[drive]->get_cyl());
		m_fdd[drive]->recalibrate();
		m_fdd[drive]->dir_w(m_s.flopi[drive].dir);
		// if head is at cyl 80 or over, EC bit will be set and recalibrate has
		// to be performed again
		if(m_fdd[drive]->get_cyl() > 79) {
			seek_to_cyl = m_fdd[drive]->get_cyl() - 79;
		}
		m_s.flopi[drive].seek_c = seek_to_cyl;
		// As long as the TRK0 pin is low, step pulses are issued.
		if(m_fdd[drive]->get_cyl() != seek_to_cyl) {
			m_s.flopi[drive].step = true;
			m_s.flopi[drive].hut = 0;
		}
		step_delay_us = calculate_step_delay_us(drive, m_fdd[drive]->get_cyl(), seek_to_cyl);
		m_fdd[drive]->step_to(seek_to_cyl, step_delay_us*1_us);
	} else {
		PDEBUGF(LOG_V1, LOG_FDC, "recalibrate, DRV%u (not present)\n", drive);
		// real controller would step for at least 79 times before giving up
		m_s.flopi[drive].seek_c = 0;
		m_s.flopi[drive].step = true;
		m_s.flopi[drive].hut = 0;
		step_delay_us = calculate_step_delay_us(drive, 79, 0);
	}

	uint64_t next_evt_us = step_delay_us + m_min_cmd_time_us;

	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: next event RECALIBRATE_WAIT_DONE in %uus (step=%uus, ovr=%uus)\n",
			drive, next_evt_us, step_delay_us, m_min_cmd_time_us);

	g_machine.activate_timer(m_fdd_timers[drive], uint64_t(next_evt_us)*1_us, false);
}

void FloppyCtrl_Flux::cmd_sense_int()
{
	PDEBUGF(LOG_V1, LOG_FDC, "sense interrupt\n");

	int fid;
	for(fid=0; fid<4 && !m_s.flopi[fid].st0_filled; fid++) {};
	if(fid < 4) {
		m_s.flopi[fid].st0_filled = false;
	}
	enter_result_phase(fid);
}

void FloppyCtrl_Flux::cmd_specify()
{
	// result: no result bytes, no interrupt
	m_s.SRT = m_s.command[1] >> 4;
	m_s.HUT = m_s.command[1] & 0x0f;
	m_s.HLT = m_s.command[2] >> 1;

	for(int i=0; i<4; i++) {
		m_s.flopi[i].hut = 0;
	}

	PDEBUGF(LOG_V1, LOG_FDC, "specify, SRT=%u(%uus),HUT=%u(%uus),HLT=%u(%uus),ND=%u\n",
			m_s.SRT, get_one_step_delay_time_us(),
			m_s.HUT, get_HUT_us(),
			m_s.HLT, get_HLT_us(),
			m_s.command[2]&1);

	m_s.main_status_reg |= (m_s.command[2] & 0x01) ? FDC_MSR_NONDMA : 0;

	// no result phase
	command_end();
	enter_idle_phase();
}

void FloppyCtrl_Flux::cmd_sense_drive()
{
	uint8_t drive = m_s.cmd_drive();
	m_s.H = m_s.cmd_head();
	m_s.DOR = FDC_DOR_DRIVE(drive);

	PDEBUGF(LOG_V1, LOG_FDC, "get status, DRV%u\n", drive);

	m_s.st3 = FDC_ST3_RY | st_hds_drv(drive);
	if(is_drive_present(drive)) {
		// the head takes time to move to track0;
		// this time is used to determine if 40 or 80 tracks
		if(!m_fdd[drive]->trk00_r()) {
			m_s.st3 |= FDC_ST3_T0;
		}
		if(m_fdd[drive]->wpt_r()) {
			m_s.st3 |= FDC_ST3_WP;
		}
		if(!m_fdd[drive]->twosid_r()) {
			m_s.st3 |= FDC_ST3_TS;
		}
	}

	enter_result_phase(drive);
}

void FloppyCtrl_Flux::cmd_configure()
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

	// no result phase
	command_end();
	enter_idle_phase();
}

void FloppyCtrl_Flux::cmd_seek()
{
	uint8_t drive = m_s.cmd_drive();
	m_s.H = m_s.cmd_head();
	m_s.DOR = FDC_DOR_DRIVE(drive);

	if(m_s.flopi[drive].main_state != IDLE) {
		PDEBUGF(LOG_V1, LOG_FDC, "Drive %u not in IDLE state!\n");
	}
	uint8_t cylinder = m_s.command[2];
	uint8_t dir = m_s.flopi[drive].pcn > cylinder;

	PDEBUGF(LOG_V1, LOG_FDC, "seek DRV%u,%s C=%u (PCN=%u)\n",
			drive, m_s.cmd_rel()?" REL":"", cylinder, m_s.flopi[drive].pcn);

	if(m_s.cmd_rel()) {
		cmd_not_implemented();
		return;
	}

	m_s.flopi[drive].main_state = SEEK;
	m_s.flopi[drive].sub_state = SEEK_WAIT_DONE;
	m_s.flopi[drive].seek_c = cylinder;
	m_s.flopi[drive].dir = dir;
	m_s.flopi[drive].st0 = 0;

	m_s.main_status_reg &= FDC_MSR_NONDMA;
	m_s.main_status_reg |= (1 << drive);

	uint32_t step_delay_us = calculate_step_delay_us(drive, cylinder);
	uint64_t next_evt_us = step_delay_us + m_min_cmd_time_us;

	if(m_fdd[drive]) {
		m_fdd[drive]->dir_w(m_s.flopi[drive].dir);
		if(m_s.flopi[drive].pcn != cylinder) {
			m_s.flopi[drive].step = true;
			m_s.flopi[drive].hut = 0;
			m_fdd[drive]->step_to(cylinder, step_delay_us*1_us);
		}
		// Head is positioned over proper Cylinder
		m_fdd[drive]->ss_w(m_s.cmd_head());
	}

	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: next event SEEK_WAIT_DONE in %uus (step=%uus, ovr=%uus)\n",
			drive, next_evt_us, step_delay_us, m_min_cmd_time_us);

	g_machine.activate_timer(m_fdd_timers[drive], uint64_t(next_evt_us)*1_us, false);
}

void FloppyCtrl_Flux::cmd_dumpreg()
{
	PDEBUGF(LOG_V1, LOG_FDC, "dump registers\n");
	enter_result_phase(0);
}

void FloppyCtrl_Flux::cmd_read_id()
{
	uint8_t drive = m_s.cmd_drive();
	m_s.H = m_s.cmd_head();
	m_s.DOR = FDC_DOR_DRIVE(drive);

	PDEBUGF(LOG_V1, LOG_FDC, "read ID, DRV%u\n", drive);

	if(!is_motor_on(drive)) {
		PDEBUGF(LOG_V1, LOG_FDC, "read ID: motor not on\n");
		return; // Hang controller
	}
	if(!is_media_present(drive)) {
		// the controller would fail to receive the index pulse and lock-up
		// since the index pulses are required for termination of the execution phase.
		PDEBUGF(LOG_V1, LOG_FDC, "read ID: attempt to read with media not present\n");
		return; // Hang controller
	}

	m_s.flopi[drive].main_state = READ_ID;
	m_s.flopi[drive].sub_state = HEAD_LOAD_DONE;
	m_s.flopi[drive].st0 = st_hds_drv(drive);
	m_s.st1 = 0;
	m_s.st2 = 0;

	m_fdd[drive]->ss_w(m_s.cmd_head());

	for(int i=0; i<4; i++) {
		m_s.cur_live.idbuf[i] = 0;
	}

	uint32_t head_load_time_us = calculate_head_delay_us(drive);
	uint64_t next_evt_us = head_load_time_us + m_min_cmd_time_us;

	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: next event HEAD_LOAD_DONE in %uus (head=%uus, ovr=%uus)\n",
			drive, next_evt_us, head_load_time_us, m_min_cmd_time_us);

	g_machine.activate_timer(m_fdd_timers[drive],
			uint64_t(next_evt_us)*1_us,
			false);
}

void FloppyCtrl_Flux::cmd_perp_mode()
{
	// result: no result bytes, no interrupt
	m_s.perp_mode = m_s.command[1];
	PDEBUGF(LOG_V1, LOG_FDC, "perpendicular mode, config=0x%02X\n", m_s.perp_mode);

	// no result phase
	command_end();
	enter_idle_phase();
}

void FloppyCtrl_Flux::cmd_lock()
{
	m_s.lock = m_s.cmd_lock();
	PDEBUGF(LOG_V1, LOG_FDC, "%slock status\n", !m_s.lock?"un":"");

	enter_result_phase(0);
}

void FloppyCtrl_Flux::cmd_not_implemented()
{
	PERRF(LOG_FDC, "Command 0x%02x not implemented\n", m_s.pending_command);
	m_s.pending_command = FDC_CMD_INVALID;

	enter_result_phase(0);
}

void FloppyCtrl_Flux::cmd_invalid()
{
	PDEBUGF(LOG_V1, LOG_FDC, "INVALID command: 0x%02x\n", m_s.pending_command);
	m_s.pending_command = FDC_CMD_INVALID;

	enter_result_phase(0);
}

void FloppyCtrl_Flux::timer_fdd(unsigned _drive, uint64_t)
{
	assert(_drive < 4);

	live_sync();

	switch(m_s.flopi[_drive].sub_state) {
		// currently unused, see MAME for implementation:
		// case SEEK_WAIT_STEP_SIGNAL_TIME:
		// 	m_s.flopi[_drive].sub_state = SEEK_WAIT_STEP_SIGNAL_TIME_DONE;
		// 	break;
		// case SEEK_WAIT_STEP_TIME:
		// 	m_s.flopi[_drive].sub_state = SEEK_WAIT_STEP_TIME_DONE;
		// 	break;
		case RECALIBRATE_WAIT_DONE: // recalibrate command
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: RECALIBRATE_WAIT_DONE\n", _drive);
			//The H (Head Address) bit in ST0 will always return a 0 (p.31)
			m_s.flopi[_drive].st0 = FDC_ST0_SE | _drive;
			m_s.flopi[_drive].pcn = m_s.flopi[_drive].seek_c;
			// If the TRK0 pin is still low after 79 step pulses have been
			// issued, the 82077AA sets the SE and the EC bits of ST0 to 1, and
			// terminates the command. Disks capable of handling more than 80
			// tracks per side may require more than one RECALIBRATE command to
			// return the head back to physical Track 0.
			if(!is_motor_on(_drive) || m_fdd[_drive]->get_cyl()!=0) {
				m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL | FDC_ST0_EC;
			}
			// clear DRVxBUSY bit
			m_s.main_status_reg &= ~(1 << _drive);
			// no result phase (for result use sense int cmd)
			command_end(_drive, IRQ_OTHER);
			enter_idle_phase();
			break;
		case SEEK_WAIT_DONE: // seek command
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEEK_WAIT_DONE\n", _drive);
			// The H (Head Address) bit in ST0 will always return a 0 (p.31)
			m_s.flopi[_drive].st0 = FDC_ST0_SE | _drive;
			m_s.flopi[_drive].pcn = m_s.flopi[_drive].seek_c;
			// clear DRVxBUSY bit
			m_s.main_status_reg &= ~(1 << _drive);
			// no result phase (for result use sense int cmd)
			command_end(_drive, IRQ_OTHER);
			enter_idle_phase();
			break;
		default:
			break;
	}

	general_continue(_drive);
}

void FloppyCtrl_Flux::timer_polling(uint64_t)
{
	if(m_s.config & FDC_CONF_POLL) {
		// polling disabled
		return;
	}

	// it occurs whenever the 82077AA is waiting for a command or during SEEKs and
	// RECALIBRATEs.
	if((m_s.pending_command != FDC_CMD_INVALID && // command executing
	    m_s.pending_command != FDC_CMD_SEEK && 
	    m_s.pending_command != FDC_CMD_RECALIBRATE) ||
	    (!m_s.command_complete)) // command reading
		return;

	// The polling timer starts after a reset and fires (once) after 250us.
	// An interrupt will be generated because of the initial "not ready" status.
	for(unsigned fid=0; fid<4; fid++) {
		PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: polled\n", fid);
		if(!m_s.flopi[fid].st0_filled) {
			m_s.flopi[fid].st0 = FDC_ST0_IC_POLLING | fid;
			m_s.flopi[fid].st0_filled = true;
			m_s.other_irq = true;
		}
	}

	check_irq();
}

void FloppyCtrl_Flux::fdd_index_pulse(uint8_t _drive, int state)
{
	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: Index pulse: %d\n", _drive, state);

	if(m_s.flopi[_drive].live) {
		live_sync();
	}

	m_s.flopi[_drive].index = state;

	if(state) {
		switch(m_s.flopi[_drive].sub_state) {
		case IDLE:
		// currently unused, see MAME for implementation:
		// case SEEK_MOVE:
		// case SEEK_WAIT_STEP_SIGNAL_TIME:
		// case SEEK_WAIT_STEP_SIGNAL_TIME_DONE:
		// case SEEK_WAIT_STEP_TIME:
		// case SEEK_WAIT_STEP_TIME_DONE:
		case HEAD_LOAD:
		case HEAD_LOAD_DONE:
		case SCAN_ID_FAILED:
		case SECTOR_READ:
			break;

		case SEEK_DONE:
			// SEEK_DONE sub state is used for read and write implied seeks
			// and head loading.
			// this is different from MAME which emulates single steps
			return;

		case RECALIBRATE_WAIT_DONE:
			// this state's not in MAME
			return;

		case SEEK_WAIT_DONE:
			// seeks operates differently compared to MAME's
			// there's no seek_continue(), case resolved in timer_fdd()
			return;

		case WAIT_INDEX:
			m_s.flopi[_drive].sub_state = WAIT_INDEX_DONE;
			break;

		case SCAN_ID:
			m_s.flopi[_drive].pulse_counter++;
			if(m_s.flopi[_drive].pulse_counter == 2) {
				m_s.flopi[_drive].sub_state = SCAN_ID_FAILED;
				live_abort();
			}
			break;

		case TRACK_DONE:
			live_abort();
			break;

		default:
			PDEBUGF(LOG_V0, LOG_FDC, "DRV%u: fdd_index_pulse(): unknown sub-state %d\n",
					_drive, m_s.flopi[_drive].sub_state);
			break;
		}
	}

	general_continue(_drive);
}

void FloppyCtrl_Flux::general_continue(unsigned _drive)
{
	assert(_drive < 4);

	if(m_s.flopi[_drive].live && m_s.cur_live.state != IDLE) {
		live_run();
		if(m_s.cur_live.state != IDLE) {
			return;
		}
	}

	switch(m_s.flopi[_drive].main_state) {
	case IDLE:
	case RECALIBRATE:
	case SEEK:
		break;

	case READ_DATA:
	case SCAN_DATA:
		read_data_continue(_drive);
		break;

	case WRITE_DATA:
		write_data_continue(_drive);
		break;

	case READ_TRACK:
		read_track_continue(_drive);
		break;

	case FORMAT_TRACK:
		format_track_continue(_drive);
		break;

	case READ_ID:
		read_id_continue(_drive);
		break;

	default:
		PDEBUGF(LOG_V0, LOG_FDC, "general continue on unknown main-state: d:%u, s:%d\n",
				_drive, m_s.flopi[_drive].main_state);
		break;
	}
}

int FloppyCtrl_Flux::calc_sector_size(uint8_t size)
{
	return size > 7 ? 16384 : 128 << size;
}

bool FloppyCtrl_Flux::sector_matches(uint8_t _drive) const
{
	PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: current C:%02u H:%02u S:%02u N:%02u - matching C:%02u H:%02u S:%02u N:%02u\n",
				_drive,
				m_s.cur_live.idbuf[0], m_s.cur_live.idbuf[1], m_s.cur_live.idbuf[2], m_s.cur_live.idbuf[3],
				m_s.C, m_s.H, m_s.R, m_s.command[5]);
	return
		m_s.cur_live.idbuf[0] == m_s.C &&
		m_s.cur_live.idbuf[1] == m_s.H &&
		m_s.cur_live.idbuf[2] == m_s.R &&
		m_s.cur_live.idbuf[3] == m_s.command[5];
}

bool FloppyCtrl_Flux::increment_sector_regs(uint8_t _drive)
{
	bool done = m_s.tc_done;
	if(m_s.R == m_s.EOT) {
		if(m_s.cmd_mtrk()) {
			m_s.H = m_s.H ^ 1;
			m_s.R = 1;
			m_fdd[_drive]->ss_w(m_s.H);
		}
		if(!m_s.cmd_mtrk() || m_s.H == 0) {
			if(m_s.tc_done || (m_s.main_status_reg & FDC_MSR_NONDMA)) {
				m_s.C++;
				m_s.R = 1;
			} else {
				PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: End of Cylinder error (EN)\n", _drive);
				m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
				m_s.st1 |= FDC_ST1_EN;
			}
			done = true;
		}
	} else {
		m_s.R++;
	}
	return done;
}

void FloppyCtrl_Flux::read_data_continue(uint8_t _drive)
{
	assert(_drive < 4);
	assert(m_fdd[_drive]);

	for(;;) {
		switch(m_s.flopi[_drive].sub_state) {
		case SEEK_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEEK_DONE\n", _drive);
			if(m_s.flopi[_drive].pcn != m_s.flopi[_drive].seek_c) {
				m_s.flopi[_drive].st0 |= FDC_ST0_SE;
				m_s.flopi[_drive].pcn = m_s.flopi[_drive].seek_c;
			}
			m_s.flopi[_drive].sub_state = SEARCH_ADDRESS_MARK_HEADER;
			break;

		case SEARCH_ADDRESS_MARK_HEADER:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEARCH_ADDRESS_MARK_HEADER\n", _drive);
			m_s.flopi[_drive].pulse_counter = 0;
			m_s.flopi[_drive].sub_state = SCAN_ID;
			live_start(_drive, SEARCH_ADDRESS_MARK_HEADER);
			return;

		case SCAN_ID:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SCAN_ID\n", _drive);
			if(m_s.cur_live.crc) {
				m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
				m_s.st1 |= FDC_ST1_DE|FDC_ST1_ND;
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
				break;
			}
			// Speedlock requires the ND flag be set when there are valid
			// sectors on the track, but the desired sector is missing, also
			// when it has no valid address marks
			m_s.st1 &= ~FDC_ST1_MA;
			m_s.st1 |= FDC_ST1_ND;
			if(!sector_matches(_drive)) {
				if(m_s.cur_live.idbuf[0] != m_s.command[2]) { // Cyl
					if(m_s.cur_live.idbuf[0] == 0xff) {
						m_s.st2 |= FDC_ST2_WC|FDC_ST2_BC;
					} else {
						m_s.st2 |= FDC_ST2_WC;
					}
				}
				PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEARCH_ADDRESS_MARK_HEADER\n", _drive);
				live_start(_drive, SEARCH_ADDRESS_MARK_HEADER);
				return;
			}
			m_s.st1 &= ~FDC_ST1_ND;
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: reading sector C:%02u H:%02u S:%02u N:%02u\n",
						_drive,
						m_s.cur_live.idbuf[0],
						m_s.cur_live.idbuf[1],
						m_s.cur_live.idbuf[2],
						m_s.cur_live.idbuf[3]);
			m_s.sector_size = calc_sector_size(m_s.cur_live.idbuf[3]);
			if(m_s.flopi[_drive].main_state == SCAN_DATA) {
				fifo_expect(m_s.sector_size, true);
			} else {
				fifo_expect(m_s.sector_size, false);
			}
			m_s.flopi[_drive].sub_state = SECTOR_READ;
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEARCH_ADDRESS_MARK_DATA\n", _drive);
			live_start(_drive, SEARCH_ADDRESS_MARK_DATA);
			return;

		case SCAN_ID_FAILED:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SCAN_ID_FAILED\n", _drive);
			m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
			m_s.flopi[_drive].sub_state = COMMAND_DONE;
			break;

		case SECTOR_READ: {
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SECTOR_READ\n", _drive);
			if(m_s.st2 & FDC_ST2_MD) {
				PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: Missing Data Address Mark\n", _drive);
				m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
				break;
			}
			if(m_s.cur_live.crc) {
				PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: Data Error in Data Field\n", _drive);
				m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
				m_s.st1 |= FDC_ST1_DE;
				m_s.st2 |= FDC_ST2_DD;
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
				break;
			}
			if ((m_s.st2 & FDC_ST2_CM) && !m_s.cmd_skip()) {
				// Encountered terminating sector while in non-skip mode.
				// This will stop reading when a normal data sector is encountered
				// during read deleted data, or when a deleted sector is encountered
				// during a read data command.
				PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: terminating sector while in non-skip mode\n", _drive);
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
				break;
			}

			bool done = increment_sector_regs(_drive);

			if(done) {
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
			} else {
				m_s.flopi[_drive].sub_state = SEARCH_ADDRESS_MARK_HEADER;
			}
			break;
		}

		case COMMAND_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: COMMAND_DONE (C:%u,H:%u,S:%u,PCN:%u)\n",
					_drive, m_s.C, m_s.H, m_s.R, m_s.flopi[_drive].pcn);
			// set the unload timeout
			m_s.flopi[_drive].hut = g_machine.get_virt_time_ns() + (get_HUT_us()*1_us);
			enter_result_phase(_drive);
			return;

		default:
			PDEBUGF(LOG_V0, LOG_FDC, "DRV%u: read_data_continue(): unknown sub-state %d\n",
					_drive,
					m_s.flopi[_drive].sub_state);
			return;
		}
	}
}

void FloppyCtrl_Flux::write_data_continue(uint8_t _drive)
{
	assert(_drive < 4);
	assert(m_fdd[_drive]);

	for(;;) {
		switch(m_s.flopi[_drive].sub_state) {
		case SEEK_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEEK_DONE\n", _drive);
			if(m_s.flopi[_drive].pcn != m_s.flopi[_drive].seek_c) {
				m_s.flopi[_drive].st0 |= FDC_ST0_SE;
				m_s.flopi[_drive].pcn = m_s.flopi[_drive].seek_c;
			}
			m_s.flopi[_drive].sub_state = SEARCH_ADDRESS_MARK_HEADER;
			break;

		case SEARCH_ADDRESS_MARK_HEADER:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEARCH_ADDRESS_MARK_HEADER\n", _drive);
			m_s.flopi[_drive].pulse_counter = 0;
			m_s.flopi[_drive].sub_state = SCAN_ID;
			live_start(_drive, SEARCH_ADDRESS_MARK_HEADER);
			return;

		case SCAN_ID:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SCAN_ID\n", _drive);
			if(!sector_matches(_drive)) {
				PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEARCH_ADDRESS_MARK_HEADER\n", _drive);
				live_start(_drive, SEARCH_ADDRESS_MARK_HEADER);
				return;
			}
			if(m_s.cur_live.crc) {
				m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
				m_s.st1 |= FDC_ST1_DE|FDC_ST1_ND;
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
				break;
			}
			m_s.st1 &= ~FDC_ST1_MA;
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: writing sector C:%02u H:%02u S:%02u N:%02u\n",
						_drive,
						m_s.cur_live.idbuf[0],
						m_s.cur_live.idbuf[1],
						m_s.cur_live.idbuf[2],
						m_s.cur_live.idbuf[3]);
			m_s.sector_size = calc_sector_size(m_s.cur_live.idbuf[3]);
			fifo_expect(m_s.sector_size, true);
			m_s.flopi[_drive].sub_state = SECTOR_WRITTEN;
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: WRITE_SECTOR_SKIP_GAP2\n", _drive);
			live_start(_drive, WRITE_SECTOR_SKIP_GAP2);
			return;

		case SCAN_ID_FAILED:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SCAN_ID_FAILED\n", _drive);
			m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
			// m_s.st1 |= ST1_ND;
			m_s.flopi[_drive].sub_state = COMMAND_DONE;
			break;

		case SECTOR_WRITTEN: {
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SECTOR_WRITTEN\n", _drive);

			bool done = increment_sector_regs(_drive);

			if(done) {
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
			} else {
				m_s.flopi[_drive].sub_state = SEARCH_ADDRESS_MARK_HEADER;
			}
			break;
		}

		case COMMAND_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: COMMAND_DONE\n", _drive);
			m_s.flopi[_drive].hut = g_machine.get_virt_time_ns() + (get_HUT_us()*1_us);
			enter_result_phase(_drive);
			return;

		default:
			PDEBUGF(LOG_V0, LOG_FDC, "DRV%u: write_data_continue(): unknown sub-state %d\n",
					_drive, m_s.flopi[_drive].sub_state);
			return;
		}
	}
}

void FloppyCtrl_Flux::read_track_continue(uint8_t _drive)
{
	for(;;) {
		switch(m_s.flopi[_drive].sub_state) {
		case SEEK_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEEK_DONE\n", _drive);
			if(m_s.flopi[_drive].pcn != m_s.flopi[_drive].seek_c) {
				m_s.flopi[_drive].st0 |= FDC_ST0_SE;
				m_s.flopi[_drive].pcn = m_s.flopi[_drive].seek_c;
			}
			m_s.flopi[_drive].pulse_counter = 0;
			m_s.flopi[_drive].sub_state = WAIT_INDEX;
			break;

		case WAIT_INDEX:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: WAIT_INDEX\n", _drive);
			return;

		case WAIT_INDEX_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: WAIT_INDEX_DONE\n", _drive);
			m_s.flopi[_drive].sub_state = SCAN_ID;
			live_start(_drive, SEARCH_ADDRESS_MARK_HEADER);
			return;

		case SCAN_ID:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SCAN_ID\n", _drive);
			if(m_s.cur_live.crc) {
				m_s.st1 |= FDC_ST1_DE;
			}
			m_s.st1 &= ~FDC_ST1_MA;
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: reading sector C:%02u H:%02u S:%02u N:%02u\n",
						_drive,
						m_s.cur_live.idbuf[0],
						m_s.cur_live.idbuf[1],
						m_s.cur_live.idbuf[2],
						m_s.cur_live.idbuf[3]);
			if(!sector_matches(_drive)) {
				m_s.st1 |= FDC_ST1_ND;
			} else {
				m_s.st1 &= ~FDC_ST1_ND;
			}

			// TODO test?
			// should the sector size be calculated from the N command parameter or
			// the value from the ID buffer? read data uses the ID...
			m_s.sector_size = calc_sector_size(m_s.command[5]);
			fifo_expect(m_s.sector_size, false);
			m_s.flopi[_drive].sub_state = SECTOR_READ;
			live_start(_drive, SEARCH_ADDRESS_MARK_DATA);
			return;

		case SCAN_ID_FAILED:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SCAN_ID_FAILED\n", _drive);
			m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
			m_s.flopi[_drive].sub_state = COMMAND_DONE;
			break;

		case SECTOR_READ: {
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SECTOR_READ\n", _drive);
			if(m_s.st2 & FDC_ST2_MD) {
				m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
				break;
			}
			if(m_s.cur_live.crc) {
				m_s.st1 |= FDC_ST1_DE;
				m_s.st2 |= FDC_ST2_DD;
			}
			bool done = increment_sector_regs(_drive);
			if(!done) {
				m_s.flopi[_drive].sub_state = WAIT_INDEX_DONE;
			} else {
				m_s.flopi[_drive].sub_state = COMMAND_DONE;
			}
			break;
		}

		case COMMAND_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: COMMAND_DONE (C:%u,H:%u,S:%u,PCN:%u)\n",
					_drive, m_s.C, m_s.H, m_s.R, m_s.flopi[_drive].pcn);
			m_s.flopi[_drive].hut = g_machine.get_virt_time_ns() + (get_HUT_us()*1_us);
			enter_result_phase(_drive);
			return;

		default:
			PDEBUGF(LOG_V0, LOG_FDC, "DRV%u: read_track_continue(): unknown sub-state %d\n",
					_drive,
					m_s.flopi[_drive].sub_state);
			return;
		}
	}
}

void FloppyCtrl_Flux::format_track_continue(uint8_t _drive)
{
	for(;;) {
		switch(m_s.flopi[_drive].sub_state) {
		case HEAD_LOAD_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: HEAD_LOAD_DONE\n", _drive);
			m_s.flopi[_drive].sub_state = WAIT_INDEX;
			break;

		case WAIT_INDEX:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: WAIT_INDEX\n", _drive);
			return;

		case WAIT_INDEX_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: WAIT_INDEX_DONE\n", _drive);
			m_s.flopi[_drive].sub_state = TRACK_DONE;
			m_s.cur_live.pll.start_writing(g_machine.get_virt_time_ns());
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: WRITE_TRACK_PRE_SECTORS\n", _drive);
			live_start(_drive, WRITE_TRACK_PRE_SECTORS);
			return;

		case TRACK_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: TRACK_DONE\n", _drive);
			m_s.flopi[_drive].hut = g_machine.get_virt_time_ns() + (get_HUT_us()*1_us);
			enter_result_phase(_drive);
			return;

		default:
			PDEBUGF(LOG_V0, LOG_FDC, "DRV%u: format_track_continue(): unknown sub-state %d\n",
					_drive, m_s.flopi[_drive].sub_state);
			return;
		}
	}
}

void FloppyCtrl_Flux::read_id_continue(uint8_t _drive)
{
	for(;;) {
		switch(m_s.flopi[_drive].sub_state) {
		case HEAD_LOAD_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: HEAD_LOAD_DONE\n", _drive);
			m_s.flopi[_drive].pulse_counter = 0;
			m_s.flopi[_drive].sub_state = SCAN_ID;
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SEARCH_ADDRESS_MARK_HEADER\n", _drive);
			live_start(_drive, SEARCH_ADDRESS_MARK_HEADER);
			return;

		case SCAN_ID:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SCAN_ID\n", _drive);
			if(m_s.cur_live.crc) {
				m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
				m_s.st1 |= FDC_ST1_MA|FDC_ST1_DE|FDC_ST1_ND;
			}
			m_s.flopi[_drive].sub_state = COMMAND_DONE;
			break;

		case SCAN_ID_FAILED:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: SCAN_ID_FAILED\n", _drive);
			m_s.flopi[_drive].st0 |= FDC_ST0_IC_ABNORMAL;
			m_s.st1 |= FDC_ST1_ND|FDC_ST1_MA;
			m_s.flopi[_drive].sub_state = COMMAND_DONE;
			break;

		case COMMAND_DONE:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: COMMAND_DONE\n", _drive);
			m_s.flopi[_drive].hut = g_machine.get_virt_time_ns() + (get_HUT_us()*1_us);
			enter_result_phase(_drive);
			return;

		default:
			PDEBUGF(LOG_V0, LOG_FDC, "DRV%u: read_id_continue(): unknown sub-state %d\n",
					_drive, m_s.flopi[_drive].sub_state);
			return;
		}
	}
}

uint16_t FloppyCtrl_Flux::dma_write(uint8_t *buffer, uint16_t maxlen, bool)
{
	assert(maxlen);
	UNUSED(maxlen);

	// A DMA write is from I/O to Memory

	m_devices->sysboard()->set_feedback();

	buffer[0] = fifo_pop(false);

	PDEBUGF(LOG_V2, LOG_FDC, "DMA: write %d/%d -> 0x%02x\n",
			m_s.fifo_popped, m_s.fifo_to_push, buffer[0]);

	return 1;
}

uint16_t FloppyCtrl_Flux::dma_read(uint8_t *buffer, uint16_t maxlen, bool)
{
	assert(maxlen);
	UNUSED(maxlen);

	// A DMA read is from Memory to I/O

	m_devices->sysboard()->set_feedback();

	fifo_push(buffer[0], false);

	PDEBUGF(LOG_V2, LOG_FDC, "DMA read %d/%d <- 0x%02x\n",
			m_s.fifo_pushed, m_s.fifo_to_push, buffer[0]);

	return 1;
}

void FloppyCtrl_Flux::tc_w(bool _tc)
{
	if(m_s.TC != _tc) {
		if(_tc) {
			PDEBUGF(LOG_V2, LOG_FDC, "TC line asserted\n");
			live_sync();
			m_s.tc_done = true;
			m_s.TC = _tc;
			if(m_s.cur_live.drive < 4) {
				general_continue(m_s.cur_live.drive);
			}
		} else {
			PDEBUGF(LOG_V3, LOG_FDC, "TC line cleared\n");
			m_s.TC = _tc;
		}
	}
}

void FloppyCtrl_Flux::raise_interrupt()
{
	if((m_s.DOR & FDC_DOR_NDMAGATE) && !m_s.pending_irq) {
		PDEBUGF(LOG_V2, LOG_FDC, "Raising IRQ %d\n", IRQ_LINE);
		m_devices->pic()->raise_irq(IRQ_LINE);
		m_s.pending_irq = true;
	}
}

void FloppyCtrl_Flux::lower_interrupt()
{
	if(m_s.pending_irq) {
		PDEBUGF(LOG_V2, LOG_FDC, "Lowering IRQ %d\n", IRQ_LINE);
		m_devices->pic()->lower_irq(IRQ_LINE);
		m_s.pending_irq = false;
	}
}

void FloppyCtrl_Flux::check_irq()
{
	bool cur_irq = m_s.data_irq || m_s.other_irq || m_s.internal_drq;
	bool normal_op = (m_s.DOR & FDC_DOR_NRESET);
	cur_irq = cur_irq && normal_op && (m_s.DOR & FDC_DOR_NDMAGATE);
	if(cur_irq) {
		raise_interrupt();
	} else {
		lower_interrupt();
	}
}

void FloppyCtrl_Flux::enter_result_phase(unsigned _drive)
{
	// these are always the same
	m_s.result_index = 0;
	// not necessary to clear any status bits, we're about to set them all
	// CMDBUSY will be cleared at the end of the result phase
	m_s.main_status_reg |= FDC_MSR_RQM | FDC_MSR_DIO | FDC_MSR_CMDBUSY;

	if(m_s.pending_command == FDC_CMD_INVALID) {
		m_s.result_size = 1;
		m_s.result[0] = FDC_ST0_IC_INVALID;
	} else {
		switch(m_s.cmd_code()) {
		case FDC_CMD_SENSE_DRIVE:
			m_s.result_size = 1;
			m_s.result[0] = m_s.st3;
			command_end(_drive);
			break;
		case FDC_CMD_SENSE_INT:
			if(_drive == 4) {
				m_s.result[0] = FDC_ST0_IC_INVALID;
				m_s.result_size = 1;
			} else {
				m_s.result[0] = m_s.flopi[_drive].st0;
				m_s.result[1] = m_s.flopi[_drive].pcn;
				m_s.result_size = 2;
			}
			command_end(_drive);
			m_s.other_irq = false;
			check_irq();
			break;
		case FDC_CMD_DUMPREG:
			m_s.result_size = 10;
			for(unsigned i=0; i<4; i++) {
				m_s.result[i] = m_s.flopi[i].pcn;
			}
			m_s.result[4] = (m_s.SRT << 4) | m_s.HUT;
			m_s.result[5] = (m_s.HLT << 1) | ((m_s.main_status_reg & FDC_MSR_NONDMA) ? 1 : 0);
			m_s.result[6] = m_s.EOT;
			m_s.result[7] = (m_s.lock << 7) | (m_s.perp_mode & 0x7f);
			m_s.result[8] = m_s.config;
			m_s.result[9] = m_s.pretrk;
			command_end();
			break;
		case FDC_CMD_VERSION:
			m_s.result_size = 1;
			m_s.result[0] = 0x90;
			command_end();
			break;
		case FDC_CMD_LOCK:
			m_s.result_size = 1;
			m_s.result[0] = (m_s.lock << 4);
			command_end();
			break;
		case FDC_CMD_READ_ID:
			m_s.result_size = 7;
			m_s.result[0] = m_s.flopi[_drive].st0;
			m_s.result[1] = m_s.st1;
			m_s.result[2] = m_s.st2;
			m_s.result[3] = m_s.cur_live.idbuf[0];
			m_s.result[4] = m_s.cur_live.idbuf[1];
			m_s.result[5] = m_s.cur_live.idbuf[2];
			m_s.result[6] = m_s.cur_live.idbuf[3];
			command_end(_drive, IRQ_DATA);
			break;
		case FDC_CMD_READ:
		case FDC_CMD_WRITE:
		case FDC_CMD_READ_TRACK:
			m_s.result_size = 7;
			m_s.result[0] = m_s.flopi[_drive].st0;
			m_s.result[1] = m_s.st1;
			m_s.result[2] = m_s.st2;
			m_s.result[3] = m_s.C; // cylinder address
			m_s.result[4] = m_s.H; // head address
			m_s.result[5] = m_s.R; // sector address
			m_s.result[6] = m_s.command[5]; // sector size
			command_end(_drive, IRQ_DATA);
			break;
		case FDC_CMD_FORMAT_TRACK:
			m_s.result_size = 7;
			m_s.result[0] = m_s.flopi[_drive].st0;
			m_s.result[1] = m_s.st1;
			m_s.result[2] = m_s.st2;
			m_s.result[3] = 0; // Undefined
			m_s.result[4] = 0; // Undefined
			m_s.result[5] = 0; // Undefined
			m_s.result[6] = m_s.command[2]; // Undefined (N?)
			command_end(_drive, IRQ_DATA);
			break;
		default:
			assert(false); break;
		}
	}
}

void FloppyCtrl_Flux::command_end(unsigned _drive, IRQReason _irq)
{
	PDEBUGF(LOG_V1, LOG_FDC, "Command done, drive: %d, IRQ: %s, RESULT: %s\n",
			(_drive < 4 ? int(_drive) : -1),
			_irq==IRQ_DATA ? "data" : (_irq==IRQ_OTHER?"other":"no"),
			bytearray_to_string(m_s.result, m_s.result_size).c_str()
			);

	// exit execution phase
	m_s.pending_command = FDC_CMD_INVALID;
	// empty the FIFO
	// this is not done by MAME but it's necessary for PIO transfers to work
	m_s.fifo_pos = 0;

	if(_drive < 4) {
		m_s.flopi[_drive].main_state = IDLE;
		m_s.flopi[_drive].sub_state = IDLE;
		switch(_irq) {
			case IRQ_DATA:
				m_s.data_irq = true;
				check_irq();
				break;
			case IRQ_OTHER:
				m_s.other_irq = true;
				m_s.flopi[_drive].st0_filled = true;
				check_irq();
				break;
			case IRQ_NONE:
				break;
		}
	}
}

void FloppyCtrl_Flux::enter_idle_phase()
{
	m_s.main_status_reg &= (FDC_MSR_NONDMA | 0x0f);  // leave drive status untouched
	m_s.main_status_reg |= FDC_MSR_RQM; // data register ready

	m_s.pending_command = FDC_CMD_INVALID;
	m_s.command_complete = true; // waiting for new command
	m_s.command_index = 0;
	m_s.command_size = 0;
	m_s.result_size = 0;
}

uint32_t FloppyCtrl_Flux::get_one_step_delay_time_us()
{
	// returns microseconds
	return (16 - m_s.SRT) * (500'000 / drate_in_k[m_s.data_rate]);
}

uint32_t FloppyCtrl_Flux::calculate_step_delay_us(uint8_t _drive, int _c1)
{
	assert(_drive < 4);

	// returns microseconds
	return calculate_step_delay_us(_drive, m_s.flopi[_drive].pcn, _c1);
}

uint32_t FloppyCtrl_Flux::get_HUT_us()
{
	// Head Unload Time
	uint32_t hut = m_s.HUT;
	if(hut == 0) {
		hut = 128;
	}
	hut *= 8'000'000 / drate_in_k[m_s.data_rate];
	return hut;
}

uint32_t FloppyCtrl_Flux::get_HLT_us()
{
	// Head Load Time
	uint32_t hlt = m_s.HLT;
	if(hlt == 0) {
		hlt = 128;
	}
	hlt *= 1'000'000 / drate_in_k[m_s.data_rate];
	return hlt;
}

uint32_t FloppyCtrl_Flux::calculate_head_delay_us(uint8_t _drive)
{
	assert(_drive < 4);

	// At the completion of the Read Data Command, the head is not unloaded
	// until after Head Unload Time Interval (specified in the Specify Command)
	// has elapsed. If the processor issues another command before the head
	// unloads then the head settling time (HLT) may be saved between subsequent
	// reads. This time out is particularly valuable when a diskette is copied
	// from one drive to another (uPD765 spec 434)

	// returns microseconds
	if(m_s.flopi[_drive].hut < g_machine.get_virt_time_ns()) {
		// if the head has been unloaded, add the load time
		return get_HLT_us();
	}
	return 0;
}

void FloppyCtrl_Flux::fifo_push(uint8_t data, bool internal)
{
	// MZ: A bit speculative. These lines help to avoid some FIFO mess-up
	// that might happen when WRITE DATA fails to find the sector
	// but the host already starts pushing the sector data. Should not hurt.
	if(m_s.fifo_expected == 0) {
		PDEBUGF(LOG_V2, LOG_FDC, "FIFO: not expecting data, discarding\n");
		return;
	}

	if(m_s.fifo_pos == 16) {
		if(internal) {
			if(!(m_s.st1 & FDC_ST1_OR)) {
				PDEBUGF(LOG_V2, LOG_FDC, "FIFO: overrun\n");
			}
			m_s.st1 |= FDC_ST1_OR;
			m_s.tc_done = true; // automatic TC
			disable_transfer();
		}
		return;
	}

	if(internal && (m_s.main_status_reg & FDC_MSR_NONDMA)) {
		PDEBUGF(LOG_V2, LOG_FDC, "FIFO: push[%d] <- 0x%02X\n", m_s.fifo_pos, data);
	}

	m_s.fifo[m_s.fifo_pos++] = data;
	m_s.fifo_expected--;
	m_s.fifo_pushed++;

	int thr = (m_s.config & FDC_CONF_FIFOTHR) + 1;
	if(!m_s.fifo_write && (!m_s.fifo_expected || m_s.fifo_pos >= thr || (m_s.config & FDC_CONF_EFIFO))) {
		PDEBUGF(LOG_V2, LOG_FDC, "FIFO: enabling transfer, pos=%u, thres=%u\n", m_s.fifo_pos, thr);
		enable_transfer();
	}
	if(m_s.fifo_write && (m_s.fifo_pos == 16 || !m_s.fifo_expected)) {
		PDEBUGF(LOG_V2, LOG_FDC, "FIFO: disabling transfer, pos=%u\n", m_s.fifo_pos);
		disable_transfer();
	}
}

uint8_t FloppyCtrl_Flux::fifo_pop(bool internal)
{
	if(!m_s.fifo_pos) {
		if(internal) {
			if(!(m_s.st1 & FDC_ST1_OR)) {
				PDEBUGF(LOG_V2, LOG_FDC, "FIFO: underrun\n");
			}
			m_s.st1 |= FDC_ST1_OR;
			m_s.tc_done = true; // automatic TC
			disable_transfer();
		} else {
			PDEBUGF(LOG_V2, LOG_FDC, "FIFO: pop while empty!\n");
		}
		return 0;
	}

	uint8_t value = m_s.fifo[0];
	m_s.fifo_pos--;
	memmove(m_s.fifo, m_s.fifo+1, m_s.fifo_pos);
	m_s.fifo_popped++;

	if(internal && (m_s.main_status_reg & FDC_MSR_NONDMA)) {
		PDEBUGF(LOG_V2, LOG_FDC, "FIFO: pop[%d] -> 0x%02X\n", m_s.fifo_pos+1, value);
	}

	if(!m_s.fifo_write && m_s.fifo_pos==0) {
		// on a read, INT is lowered when FIFO gets emptied
		PDEBUGF(LOG_V2, LOG_FDC, "FIFO: disabling transfer, pos=%u\n", m_s.fifo_pos);
		disable_transfer();
	}
	int thr = (m_s.config & FDC_CONF_FIFOTHR);
	if(m_s.fifo_write && m_s.fifo_expected && (m_s.fifo_pos <= thr || (m_s.config & FDC_CONF_EFIFO))) {
		PDEBUGF(LOG_V2, LOG_FDC, "FIFO: enabling transfer, pos=%u, thres=%u\n", m_s.fifo_pos, thr);
		enable_transfer();
	}
	return value;
}

void FloppyCtrl_Flux::fifo_expect(int size, bool write)
{
	m_s.fifo_expected = size;
	m_s.fifo_to_push = size;
	m_s.fifo_popped = 0;
	m_s.fifo_pushed = 0;
	m_s.fifo_write = write;
	if(m_s.fifo_write) {
		enable_transfer();
	}
}

void FloppyCtrl_Flux::enable_transfer()
{
	if(m_s.main_status_reg & FDC_MSR_NONDMA) {
		// PIO
		m_s.main_status_reg |= FDC_MSR_RQM;
		if(!m_s.fifo_write) {
			m_s.main_status_reg |= FDC_MSR_DIO; // read operation
		}
		if(!m_s.internal_drq) {
			m_s.internal_drq = true;
			check_irq();
		}
	} else {
		// DMA
		if(!m_devices->dma()->get_DRQ(DMA_CHAN)) {
			PDEBUGF(LOG_V3, LOG_FDC, "DRQ enable, chan=%u\n", DMA_CHAN);
			m_devices->dma()->set_DRQ(DMA_CHAN, true);
		}
	}
}

void FloppyCtrl_Flux::disable_transfer()
{
	if(m_s.main_status_reg & FDC_MSR_NONDMA) {
		m_s.main_status_reg &= ~(FDC_MSR_RQM | FDC_MSR_DIO);
		m_s.internal_drq = false;
		check_irq();
	} else {
		if(m_devices->dma()->get_DRQ(DMA_CHAN)) {
			PDEBUGF(LOG_V3, LOG_FDC, "DRQ disable, chan=%u\n", DMA_CHAN);
			m_devices->dma()->set_DRQ(DMA_CHAN, false);
		}
	}
}

void FloppyCtrl_Flux::live_start(unsigned _drive, int _state)
{
	m_s.cur_live.tm = g_machine.get_virt_time_ns();
	m_s.cur_live.state = _state;
	m_s.cur_live.next_state = -1;
	m_s.cur_live.drive = _drive;
	m_s.cur_live.shift_reg = 0;
	m_s.cur_live.crc = 0xffff;
	m_s.cur_live.bit_counter = 0;
	m_s.cur_live.data_separator_phase = false;
	m_s.cur_live.data_reg = 0;
	m_s.cur_live.previous_type = State::live_info::PT_NONE;
	m_s.cur_live.data_bit_context = false;
	m_s.cur_live.byte_counter = 0;
	m_s.cur_live.pll.reset(m_s.cur_live.tm);
	int cur_rate = drate_in_k[m_s.data_rate] * 1000;
	m_s.cur_live.pll.set_clock(hz_to_time(m_s.cmd_mfm() ? 2*cur_rate : cur_rate));
	std::memcpy(&m_s.checkpoint_live, &m_s.cur_live, sizeof(State::live_info));
	m_s.flopi[_drive].live = true;

	live_run();
}

void FloppyCtrl_Flux::checkpoint()
{
	if(m_s.cur_live.drive < 4) {
		m_s.cur_live.pll.commit(m_fdd[m_s.cur_live.drive].get(), m_s.cur_live.tm);
	}
	std::memcpy(&m_s.checkpoint_live, &m_s.cur_live, sizeof(State::live_info));
}

void FloppyCtrl_Flux::rollback()
{
	std::memcpy(&m_s.cur_live, &m_s.checkpoint_live, sizeof(State::live_info));
}

void FloppyCtrl_Flux::live_delay(int state)
{
	assert(m_s.cur_live.drive < 4);

	m_s.cur_live.next_state = state;
	uint64_t now = g_machine.get_virt_time_ns();
	if(m_s.cur_live.tm > now) {
		g_machine.activate_timer(m_fdd_timers[m_s.cur_live.drive], m_s.cur_live.tm - now, false);
	} else {
		if(m_s.cur_live.tm < now) {
			PDEBUGF(LOG_V2, LOG_FDC, "live_delay(): time %llu < %llu\n", m_s.cur_live.tm, now);
		}
		live_sync();
	}
}

void FloppyCtrl_Flux::live_sync()
{
	uint64_t now = g_machine.get_virt_time_ns();
	if(m_s.cur_live.tm != TIME_NEVER) {
		assert(m_s.cur_live.drive < 4);
		if(m_s.cur_live.tm > now) {
			rollback();
			live_run(now);
			m_s.cur_live.pll.commit(m_fdd[m_s.cur_live.drive].get(), m_s.cur_live.tm);
		} else {
			if(m_s.cur_live.tm < now) {
				PDEBUGF(LOG_V2, LOG_FDC, "live_sync(): time %llu < %llu\n", m_s.cur_live.tm, now);
			}
			m_s.cur_live.pll.commit(m_fdd[m_s.cur_live.drive].get(), m_s.cur_live.tm);
			if(m_s.cur_live.next_state != -1) {
				m_s.cur_live.state = m_s.cur_live.next_state;
				m_s.cur_live.next_state = -1;
			}
			if(m_s.cur_live.state == IDLE) {
				m_s.cur_live.pll.stop_writing(m_fdd[m_s.cur_live.drive].get(), m_s.cur_live.tm);
				m_s.cur_live.tm = TIME_NEVER;
				m_s.flopi[m_s.cur_live.drive].live = false;
				m_s.cur_live.drive = -1;
			}
		}
		m_s.cur_live.next_state = -1;
		checkpoint();
	}
}

void FloppyCtrl_Flux::live_abort()
{
	uint64_t now = g_machine.get_virt_time_ns();

	if(m_s.cur_live.tm != TIME_NEVER && m_s.cur_live.tm > now) {
		rollback();
		live_run(now);
	}

	if(m_s.cur_live.drive < 4) {
		m_s.cur_live.pll.stop_writing(m_fdd[m_s.cur_live.drive].get(), m_s.cur_live.tm);
		m_s.flopi[m_s.cur_live.drive].live = false;
		m_s.cur_live.drive = -1;
	}

	m_s.cur_live.tm = TIME_NEVER;
	m_s.cur_live.state = IDLE;
	m_s.cur_live.next_state = -1;
}

void FloppyCtrl_Flux::live_run(uint64_t limit)
{
	if(m_s.cur_live.state == IDLE || m_s.cur_live.next_state != -1) {
		return;
	}

	if(limit == TIME_NEVER) {
		if(m_s.cur_live.drive < 4) {
			limit = m_fdd[m_s.cur_live.drive]->time_next_index();
		}
		if(limit == TIME_NEVER) {
			// Happens when there's no disk or if the fdc is not
			// connected to a drive, hence no index pulse. Force a
			// sync from time to time in that case, so that the main
			// cpu timeout isn't too painful.  Avoids looping into
			// infinity looking for data too.

			limit = g_machine.get_virt_time_ns() + 1_ms;
			g_machine.activate_timer(m_fdd_timers[m_s.cur_live.drive], 1_ms, false);
		}
	}

	for(;;) {
		switch(m_s.cur_live.state) {
		case SEARCH_ADDRESS_MARK_HEADER:
			if(read_one_bit(limit)) {
				return;
			}

			PDEBUGF(LOG_V5, LOG_FDC, "DRV%u: SEARCH_ADDRESS_MARK_HEADER shift=%04x data=%02x cnt=%d\n",
					m_s.cur_live.drive,
					m_s.cur_live.shift_reg,
					(m_s.cur_live.shift_reg & 0x4000 ? 0x80 : 0x00) |
					(m_s.cur_live.shift_reg & 0x1000 ? 0x40 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0400 ? 0x20 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0100 ? 0x10 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0040 ? 0x08 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0010 ? 0x04 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0004 ? 0x02 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0001 ? 0x01 : 0x00),
					m_s.cur_live.bit_counter);

			if(m_s.cmd_mfm() && m_s.cur_live.shift_reg == 0x4489) {
				m_s.cur_live.crc = 0x443b;
				m_s.cur_live.data_separator_phase = false;
				m_s.cur_live.bit_counter = 0;
				m_s.cur_live.state = READ_HEADER_BLOCK_HEADER;
				PDEBUGF(LOG_V3, LOG_FDC, "%s: Found A1\n", m_fdd[m_s.cur_live.drive]->name());
			}

			if(!m_s.cmd_mfm() && m_s.cur_live.shift_reg == 0xf57e) {
				m_s.cur_live.crc = 0xef21;
				m_s.cur_live.data_separator_phase = false;
				m_s.cur_live.bit_counter = 0;
				m_s.cur_live.state = READ_ID_BLOCK;
				PDEBUGF(LOG_V3, LOG_FDC, "%s: Found IDAM\n", m_fdd[m_s.cur_live.drive]->name());
			}
			break;

		case READ_HEADER_BLOCK_HEADER: {
			if(read_one_bit(limit)) {
				return;
			}

			PDEBUGF(LOG_V5, LOG_FDC, "DRV%u: READ_HEADER_BLOCK_HEADER shift=%04x data=%02x cnt=%d\n",
					m_s.cur_live.drive,
					m_s.cur_live.shift_reg,
					(m_s.cur_live.shift_reg & 0x4000 ? 0x80 : 0x00) |
					(m_s.cur_live.shift_reg & 0x1000 ? 0x40 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0400 ? 0x20 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0100 ? 0x10 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0040 ? 0x08 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0010 ? 0x04 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0004 ? 0x02 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0001 ? 0x01 : 0x00),
					m_s.cur_live.bit_counter);

			if(m_s.cur_live.bit_counter & 15)
				break;

			int slot = m_s.cur_live.bit_counter >> 4;

			if(slot < 3) {
				if(m_s.cur_live.shift_reg != 0x4489) {
					m_s.cur_live.state = SEARCH_ADDRESS_MARK_HEADER;
				} else {
					PDEBUGF(LOG_V3, LOG_FDC, "DRV%u: Found A1\n", m_s.cur_live.drive);
				}
				break;
			}
			if(m_s.cur_live.data_reg != 0xfe) {
				PDEBUGF(LOG_V3, LOG_FDC, "DRV%u: No ident byte found after triple-A1, continue search\n",
						m_s.cur_live.drive);
				m_s.cur_live.state = SEARCH_ADDRESS_MARK_HEADER;
				break;
			}

			m_s.cur_live.bit_counter = 0;
			m_s.cur_live.state = READ_ID_BLOCK;

			break;
		}

		case READ_ID_BLOCK: {
			if(read_one_bit(limit)) {
				return;
			}
			if(m_s.cur_live.bit_counter & 15) {
				break;
			}
			int slot = (m_s.cur_live.bit_counter >> 4)-1;

			PDEBUGF(LOG_V5, LOG_FDC, "DRV%u: READ_ID_BLOCK slot=%d data=%02x crc=%04x\n",
					m_s.cur_live.drive,
					slot, m_s.cur_live.data_reg, m_s.cur_live.crc);

			m_s.cur_live.idbuf[slot] = m_s.cur_live.data_reg;
			if(slot == 5) {
				live_delay(IDLE);
				return;
			}
			break;
		}

		case SEARCH_ADDRESS_MARK_DATA:
			if(read_one_bit(limit)) {
				return;
			}

			PDEBUGF(LOG_V5, LOG_FDC, "DRV%u: SEARCH_ADDRESS_MARK_DATA shift=%04x data=%02x cnt=%d.%x\n",
					m_s.cur_live.drive,
					m_s.cur_live.shift_reg,
					(m_s.cur_live.shift_reg & 0x4000 ? 0x80 : 0x00) |
					(m_s.cur_live.shift_reg & 0x1000 ? 0x40 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0400 ? 0x20 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0100 ? 0x10 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0040 ? 0x08 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0010 ? 0x04 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0004 ? 0x02 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0001 ? 0x01 : 0x00),
					m_s.cur_live.bit_counter >> 4, m_s.cur_live.bit_counter & 15);

			if(m_s.cmd_mfm()) {
				// Large tolerance due to perpendicular recording at extended density
				if(m_s.cur_live.bit_counter > 62*16) {
					live_delay(SEARCH_ADDRESS_MARK_DATA_FAILED);
					return;
				}

				if(m_s.cur_live.bit_counter >= 28*16 && m_s.cur_live.shift_reg == 0x4489) {
					m_s.cur_live.crc = 0x443b;
					m_s.cur_live.data_separator_phase = false;
					m_s.cur_live.bit_counter = 0;
					m_s.cur_live.state = READ_DATA_BLOCK_HEADER;
				}

			} else {
				if(m_s.cur_live.bit_counter > 23*16) {
					live_delay(SEARCH_ADDRESS_MARK_DATA_FAILED);
					return;
				}

				if(m_s.cur_live.bit_counter >= 11*16 && (m_s.cur_live.shift_reg == 0xf56a || m_s.cur_live.shift_reg == 0xf56f)) {
					m_s.cur_live.crc = m_s.cur_live.shift_reg == 0xf56a ? 0x8fe7 : 0xbf84;
					m_s.cur_live.data_separator_phase = false;
					m_s.cur_live.bit_counter = 0;
					m_s.cur_live.state = READ_SECTOR_DATA;
				}
			}

			break;

		case READ_DATA_BLOCK_HEADER: {
			if(read_one_bit(limit)) {
				return;
			}

			PDEBUGF(LOG_V5, LOG_FDC, "DRV%u: READ_DATA_BLOCK_HEADER shift=%04x data=%02x cnt=%d\n",
					m_s.cur_live.drive,
					m_s.cur_live.shift_reg,
					(m_s.cur_live.shift_reg & 0x4000 ? 0x80 : 0x00) |
					(m_s.cur_live.shift_reg & 0x1000 ? 0x40 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0400 ? 0x20 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0100 ? 0x10 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0040 ? 0x08 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0010 ? 0x04 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0004 ? 0x02 : 0x00) |
					(m_s.cur_live.shift_reg & 0x0001 ? 0x01 : 0x00),
					m_s.cur_live.bit_counter);

			if(m_s.cur_live.bit_counter & 15) {
				break;
			}

			int slot = m_s.cur_live.bit_counter >> 4;

			if(slot < 3) {
				if(m_s.cur_live.shift_reg != 0x4489) {
					live_delay(SEARCH_ADDRESS_MARK_DATA_FAILED);
					return;
				}
				break;
			}
			if(m_s.cur_live.data_reg != 0xfb && m_s.cur_live.data_reg != 0xf8) {
				live_delay(SEARCH_ADDRESS_MARK_DATA_FAILED);
				return;
			}

			if (
				((m_s.command[0] & 0x08) == 0 && m_s.cur_live.data_reg == 0xf8) // Encountered deleted sector during read data
				|| ((m_s.command[0] & 0x08) != 0 && m_s.cur_live.data_reg == 0xfb) // Encountered normal sector during read deleted data
			) {
				m_s.st2 |= FDC_ST2_CM;
			}

			m_s.cur_live.bit_counter = 0;
			m_s.cur_live.state = READ_SECTOR_DATA;
			break;
		}

		case SEARCH_ADDRESS_MARK_DATA_FAILED:
			m_s.st1 |= FDC_ST1_MA;
			m_s.st2 |= FDC_ST2_MD;
			m_s.cur_live.state = IDLE;
			return;

		case READ_SECTOR_DATA: {
			if(read_one_bit(limit))
				return;
			if(m_s.cur_live.bit_counter & 15)
				break;
			int slot = (m_s.cur_live.bit_counter >> 4)-1;
			if(slot < m_s.sector_size) {
				// Sector data
				if(m_s.flopi[m_s.cur_live.drive].main_state == SCAN_DATA) {
					live_delay(SCAN_SECTOR_DATA_BYTE);
				} else {
					live_delay(READ_SECTOR_DATA_BYTE);
				}
				return;

			} else if(slot < m_s.sector_size+2) {
				// CRC
				if(slot == m_s.sector_size+1) {
					live_delay(IDLE);
					return;
				}
			}
			break;
		}

		case READ_SECTOR_DATA_BYTE:
			if(!m_s.tc_done) {
				PDEBUGF(LOG_V3, LOG_FDC, "DRV%u: READ_SECTOR_DATA_BYTE: 0x%02x\n",
						m_s.cur_live.drive, m_s.cur_live.data_reg);
				fifo_push(m_s.cur_live.data_reg, true);
			} else {
				PDEBUGF(LOG_V3, LOG_FDC, "DRV%u: READ_SECTOR_DATA_BYTE: TC\n",
						m_s.cur_live.drive);
			}
			m_s.cur_live.state = READ_SECTOR_DATA;
			checkpoint();
			break;

		case SCAN_SECTOR_DATA_BYTE:
			if(!m_s.scan_done) { // TODO: handle stp, x68000 sets it to 0xff (as it would dtl)?
				int slot = (m_s.cur_live.bit_counter >> 4)-1;
				uint8_t data = fifo_pop(true);
				if(!slot) {
					m_s.st2 = (m_s.st2 & ~(FDC_ST2_SN)) | FDC_ST2_SH;
				}
				if(data != m_s.cur_live.data_reg) {
					m_s.st2 = (m_s.st2 & ~(FDC_ST2_SH)) | FDC_ST2_SN;
					if((data < m_s.cur_live.data_reg) && ((m_s.command[0] & 0x1f) == 0x19)) {
						// low
						m_s.st2 &= ~FDC_ST2_SN;
					}
					if((data > m_s.cur_live.data_reg) && ((m_s.command[0] & 0x1f) == 0x1d)) {
						// high
						m_s.st2 &= ~FDC_ST2_SN;
					}
				}
				if((slot == m_s.sector_size) && !(m_s.st2 & FDC_ST2_SN)) {
					m_s.scan_done = true;
					m_s.tc_done = true;
				}
			} else {
				if(m_s.fifo_pos) {
					fifo_pop(true);
				}
			}
			m_s.cur_live.state = READ_SECTOR_DATA;
			checkpoint();
			break;

		case WRITE_SECTOR_SKIP_GAP2:
			m_s.cur_live.bit_counter = 0;
			m_s.cur_live.byte_counter = 0;
			m_s.cur_live.state = WRITE_SECTOR_SKIP_GAP2_BYTE;
			checkpoint();
			break;

		case WRITE_SECTOR_SKIP_GAP2_BYTE:
			if(read_one_bit(limit))
				return;
			if(m_s.cmd_mfm() && m_s.cur_live.bit_counter != 22*16)
				break;
			if(!m_s.cmd_mfm() && m_s.cur_live.bit_counter != 11*16)
				break;
			m_s.cur_live.bit_counter = 0;
			m_s.cur_live.byte_counter = 0;
			live_delay(WRITE_SECTOR_DATA);
			return;

		case WRITE_SECTOR_DATA:
			if(m_s.cmd_mfm()) {
				if(m_s.cur_live.byte_counter < 12) {
					live_write_mfm(0x00);
				} else if(m_s.cur_live.byte_counter < 15) {
					live_write_raw(0x4489);
				} else if(m_s.cur_live.byte_counter < 16) {
					m_s.cur_live.crc = 0xcdb4;
					live_write_mfm(m_s.command[0] & 0x08 ? 0xf8 : 0xfb);
				} else if(m_s.cur_live.byte_counter < 16+m_s.sector_size) {
					uint8_t mfm;
					if(m_s.tc_done && !m_s.fifo_pos) {
						mfm = 0;
					} else {
						mfm = fifo_pop(true);
					}
					live_write_mfm(mfm);
				} else if(m_s.cur_live.byte_counter < 16+m_s.sector_size+2) {
					live_write_mfm(m_s.cur_live.crc >> 8);
				} else if(m_s.cur_live.byte_counter < 16+m_s.sector_size+2+m_s.command[7]) {
					live_write_mfm(0x4e);
				} else {
					m_s.cur_live.pll.stop_writing(m_fdd[m_s.cur_live.drive].get(), m_s.cur_live.tm);
					m_s.cur_live.state = IDLE;
					return;
				}

			} else {
				if(m_s.cur_live.byte_counter < 6) {
					live_write_fm(0x00);
				} else if(m_s.cur_live.byte_counter < 7) {
					m_s.cur_live.crc = 0xffff;
					live_write_raw(m_s.command[0] & 0x08 ? 0xf56a : 0xf56f);
				} else if(m_s.cur_live.byte_counter < 7+m_s.sector_size) {
					uint8_t fm;
					if(m_s.tc_done && !m_s.fifo_pos) {
						fm = 0;
					} else {
						fm = fifo_pop(true);
					}
					live_write_fm(fm);
				} else if(m_s.cur_live.byte_counter < 7+m_s.sector_size+2) {
					live_write_fm(m_s.cur_live.crc >> 8);
				} else if(m_s.cur_live.byte_counter < 7+m_s.sector_size+2+m_s.command[7]) {
					live_write_fm(0xff);
				} else {
					m_s.cur_live.pll.stop_writing(m_fdd[m_s.cur_live.drive].get(), m_s.cur_live.tm);
					m_s.cur_live.state = IDLE;
					return;
				}
			}
			m_s.cur_live.state = WRITE_SECTOR_DATA_BYTE;
			m_s.cur_live.bit_counter = 16;
			checkpoint();
			break;

		case WRITE_TRACK_PRE_SECTORS: // FORMAT
			if(!m_s.cur_live.byte_counter && m_s.command[3])
				fifo_expect(4, true);
			if(m_s.cmd_mfm()) {
				if(m_s.cur_live.byte_counter < 80)
					live_write_mfm(0x4e);
				else if(m_s.cur_live.byte_counter < 92)
					live_write_mfm(0x00);
				else if(m_s.cur_live.byte_counter < 95)
					live_write_raw(0x5224);
				else if(m_s.cur_live.byte_counter < 96)
					live_write_mfm(0xfc);
				else if(m_s.cur_live.byte_counter < 146)
					live_write_mfm(0x4e);
				else {
					m_s.cur_live.state = WRITE_TRACK_SECTOR;
					m_s.cur_live.byte_counter = 0;
					break;
				}
			} else {
				if(m_s.cur_live.byte_counter < 40)
					live_write_fm(0xff);
				else if(m_s.cur_live.byte_counter < 46)
					live_write_fm(0x00);
				else if(m_s.cur_live.byte_counter < 47)
					live_write_raw(0xf77a);
				else if(m_s.cur_live.byte_counter < 73)
					live_write_fm(0xff);
				else {
					m_s.cur_live.state = WRITE_TRACK_SECTOR;
					m_s.cur_live.byte_counter = 0;
					break;
				}
			}
			m_s.cur_live.state = WRITE_TRACK_PRE_SECTORS_BYTE;
			m_s.cur_live.bit_counter = 16;
			checkpoint();
			break;

		case WRITE_TRACK_SECTOR: // FORMAT
			if(!m_s.cur_live.byte_counter) {
				m_s.command[3]--;
				if(m_s.command[3]) {
					fifo_expect(4, true);
				}
			}
			if(m_s.cmd_mfm()) {
				if(m_s.cur_live.byte_counter < 12) {
					live_write_mfm(0x00);
				} else if(m_s.cur_live.byte_counter < 15) {
					live_write_raw(0x4489);
				} else if(m_s.cur_live.byte_counter < 16) {
					m_s.cur_live.crc = 0xcdb4;
					live_write_mfm(0xfe);
				} else if(m_s.cur_live.byte_counter < 20) {
					uint8_t byte = fifo_pop(true);
					m_s.command[12+m_s.cur_live.byte_counter-16] = byte;
					live_write_mfm(byte);
					if(m_s.cur_live.byte_counter == 19) {
						PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: formatting sector %02u %02u %02u %02u\n",
								m_s.cur_live.drive, m_s.command[12], m_s.command[13], m_s.command[14], m_s.command[15]);
					}
				} else if(m_s.cur_live.byte_counter < 22) {
					live_write_mfm(m_s.cur_live.crc >> 8);
				} else if(m_s.cur_live.byte_counter < 44) {
					live_write_mfm(0x4e);
				} else if(m_s.cur_live.byte_counter < 56) {
					live_write_mfm(0x00);
				} else if(m_s.cur_live.byte_counter < 59) {
					live_write_raw(0x4489);
				} else if(m_s.cur_live.byte_counter < 60) {
					m_s.cur_live.crc = 0xcdb4;
					live_write_mfm(0xfb);
				} else if(m_s.cur_live.byte_counter < 60+m_s.sector_size) {
					live_write_mfm(m_s.command[5]);
				} else if(m_s.cur_live.byte_counter < 62+m_s.sector_size) {
					live_write_mfm(m_s.cur_live.crc >> 8);
				} else if(m_s.cur_live.byte_counter < 62+m_s.sector_size+m_s.command[4]) {
					live_write_mfm(0x4e);
				} else {
					m_s.cur_live.byte_counter = 0;
					m_s.cur_live.state = m_s.command[3] ? WRITE_TRACK_SECTOR : WRITE_TRACK_POST_SECTORS;
					break;
				}

			} else {
				if(m_s.cur_live.byte_counter < 6) {
					live_write_fm(0x00);
				} else if(m_s.cur_live.byte_counter < 7) {
					m_s.cur_live.crc = 0xffff;
					live_write_raw(0xf57e);
				} else if(m_s.cur_live.byte_counter < 11) {
					uint8_t byte = fifo_pop(true);
					m_s.command[12+m_s.cur_live.byte_counter-7] = byte;
					live_write_fm(byte);
					if(m_s.cur_live.byte_counter == 10) {
						PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: formatting sector %02u %02u %02u %02u\n",
								m_s.cur_live.drive, m_s.command[12], m_s.command[13], m_s.command[14], m_s.command[15]);
					}
				} else if(m_s.cur_live.byte_counter < 13) {
					live_write_fm(m_s.cur_live.crc >> 8);
				} else if(m_s.cur_live.byte_counter < 24) {
					live_write_fm(0xff);
				} else if(m_s.cur_live.byte_counter < 30) {
					live_write_fm(0x00);
				} else if(m_s.cur_live.byte_counter < 31) {
					m_s.cur_live.crc = 0xffff;
					live_write_raw(0xf56f);
				} else if(m_s.cur_live.byte_counter < 31+m_s.sector_size) {
					live_write_fm(m_s.command[5]);
				} else if(m_s.cur_live.byte_counter < 33+m_s.sector_size) {
					live_write_fm(m_s.cur_live.crc >> 8);
				} else if(m_s.cur_live.byte_counter < 33+m_s.sector_size+m_s.command[4]) {
					live_write_fm(0xff);
				} else {
					m_s.cur_live.byte_counter = 0;
					m_s.cur_live.state = m_s.command[3] ? WRITE_TRACK_SECTOR : WRITE_TRACK_POST_SECTORS;
					break;
				}
			}
			m_s.cur_live.state = WRITE_TRACK_SECTOR_BYTE;
			m_s.cur_live.bit_counter = 16;
			checkpoint();
			break;

		case WRITE_TRACK_POST_SECTORS: // FORMAT
			if(m_s.cmd_mfm()) {
				live_write_mfm(0x4e);
			} else {
				live_write_fm(0xff);
			}
			m_s.cur_live.state = WRITE_TRACK_POST_SECTORS_BYTE;
			m_s.cur_live.bit_counter = 16;
			checkpoint();
			break;

		case WRITE_TRACK_PRE_SECTORS_BYTE: // FORMAT
		case WRITE_TRACK_SECTOR_BYTE: // FORMAT
		case WRITE_TRACK_POST_SECTORS_BYTE: // FORMAT
		case WRITE_SECTOR_DATA_BYTE:
			if(write_one_bit(limit)) {
				return;
			}
			if(m_s.cur_live.bit_counter == 0) {
				m_s.cur_live.byte_counter++;
				live_delay(m_s.cur_live.state-1);
				return;
			}
			break;

		default:
			PDEBUGF(LOG_V2, LOG_FDC, "DRV%u: Unknown live state %d\n",
					m_s.cur_live.drive, m_s.cur_live.state);
			return;
		}
	}
}

bool FloppyCtrl_Flux::read_one_bit(uint64_t limit)
{
	m_s.flopi[m_s.cur_live.drive].rddata = true;
	int bit = m_s.cur_live.pll.get_next_bit(m_s.cur_live.tm, m_fdd[m_s.cur_live.drive].get(), limit);
	if(bit < 0) {
		return true;
	}
	m_s.cur_live.shift_reg = (m_s.cur_live.shift_reg << 1) | bit;
	m_s.cur_live.bit_counter++;
	if(m_s.cur_live.data_separator_phase) {
		m_s.cur_live.data_reg = (m_s.cur_live.data_reg << 1) | bit;
		if((m_s.cur_live.crc ^ (bit ? 0x8000 : 0x0000)) & 0x8000) {
			m_s.cur_live.crc = (m_s.cur_live.crc << 1) ^ 0x1021;
		} else {
			m_s.cur_live.crc = m_s.cur_live.crc << 1;
		}
	}
	m_s.cur_live.data_separator_phase = !m_s.cur_live.data_separator_phase;
	return false;
}

bool FloppyCtrl_Flux::write_one_bit(uint64_t limit)
{
	m_s.flopi[m_s.cur_live.drive].wrdata = true;
	bool bit = m_s.cur_live.shift_reg & 0x8000;
	if(m_s.cur_live.pll.write_next_bit(bit, m_s.cur_live.tm, m_fdd[m_s.cur_live.drive].get(), limit))
		return true;
	if(m_s.cur_live.bit_counter & 1) {
		if((m_s.cur_live.crc ^ (bit ? 0x8000 : 0x0000)) & 0x8000) {
			m_s.cur_live.crc = (m_s.cur_live.crc << 1) ^ 0x1021;
		} else {
			m_s.cur_live.crc = m_s.cur_live.crc << 1;
		}
	}
	m_s.cur_live.shift_reg = m_s.cur_live.shift_reg << 1;
	m_s.cur_live.bit_counter--;
	return false;
}

void FloppyCtrl_Flux::live_write_mfm(uint8_t mfm)
{
	bool context = m_s.cur_live.data_bit_context;
	uint16_t raw = 0;
	for(int i=0; i<8; i++) {
		bool bit = mfm & (0x80 >> i);
		if(!(bit || context)) {
			raw |= 0x8000 >> (2*i);
		}
		if(bit) {
			raw |= 0x4000 >> (2*i);
		}
		context = bit;
	}
	m_s.cur_live.data_reg = mfm;
	m_s.cur_live.shift_reg = raw;
	m_s.cur_live.data_bit_context = context;
	PDEBUGF(LOG_V5, LOG_FDC, "DRV%u: write mfm=%02x, crc=%04x, raw=%04x\n",
			m_s.cur_live.drive, mfm, m_s.cur_live.crc, raw);
}

void FloppyCtrl_Flux::live_write_fm(uint8_t fm)
{
	uint16_t raw = 0xaaaa;
	for(int i=0; i<8; i++) {
		if(fm & (0x80 >> i)) {
			raw |= 0x4000 >> (2*i);
		}
	}
	m_s.cur_live.data_reg = fm;
	m_s.cur_live.shift_reg = raw;
	m_s.cur_live.data_bit_context = fm & 1;
	PDEBUGF(LOG_V5, LOG_FDC, "DRV%u: write fm=%02x, crc=%04x, raw=%04x\n",
			m_s.cur_live.drive, fm, m_s.cur_live.crc, raw);
}

void FloppyCtrl_Flux::live_write_raw(uint16_t raw)
{
	PDEBUGF(LOG_V5, LOG_FDC, "DRV%u: write raw=%04x, crc=%04x\n",
			m_s.cur_live.drive, raw, m_s.cur_live.crc);
	m_s.cur_live.shift_reg = raw;
	m_s.cur_live.data_bit_context = raw & 1;
}

void FloppyCtrl_Flux::PLL::set_clock(uint64_t _period)
{
	period = _period;
	period_adjust_base = double(_period) * 0.05;
	min_period = double(_period) * 0.75;
	max_period = double(_period) * 1.25;
}

void FloppyCtrl_Flux::PLL::reset(uint64_t when)
{
	read_reset(when);
	write_position = 0;
	write_start_time = TIME_NEVER;
}

void FloppyCtrl_Flux::PLL::read_reset(uint64_t when)
{
	ctime = when;
	phase_adjust = 0;
	freq_hist = 0;
}

void FloppyCtrl_Flux::PLL::start_writing(uint64_t tm)
{
	write_start_time = tm;
	write_position = 0;
}

void FloppyCtrl_Flux::PLL::stop_writing(FloppyDrive *floppy, uint64_t tm)
{
	commit(floppy, tm);
	write_start_time = TIME_NEVER;
}

void FloppyCtrl_Flux::PLL::commit(FloppyDrive *floppy, uint64_t tm)
{
	if(write_start_time == TIME_NEVER || tm == write_start_time)
		return;

	if(floppy) {
		floppy->write_flux(write_start_time, tm, write_position, write_buffer);
	}
	write_start_time = tm;
	write_position = 0;
}

int FloppyCtrl_Flux::PLL::get_next_bit(uint64_t &tm, FloppyDrive *floppy, uint64_t limit)
{
	uint64_t edge = floppy ? floppy->get_next_transition(ctime) : TIME_NEVER;

	return feed_read_data(tm, edge, limit);
}

int FloppyCtrl_Flux::PLL::feed_read_data(uint64_t &tm, uint64_t edge, uint64_t limit)
{
	int64_t next = ctime + period + phase_adjust;

	if(next > int64_t(limit)) {
		return -1;
	}

	ctime = next;
	tm = next;

	if(edge == TIME_NEVER || int64_t(edge) > next) {
		// No transition in the window means 0 and pll in free run mode
		phase_adjust = 0;
		return 0;
	}

	// Transition in the window means 1, and the pll is adjusted

	int64_t delta = int64_t(edge) - (next - period/2);

	if(delta < 0) {
		phase_adjust = -((-delta) * 65) / 100;
	} else {
		phase_adjust = (delta * 65) / 100;
	}

	if(delta < 0) {
		if(freq_hist < 0) {
			freq_hist--;
		} else {
			freq_hist = -1;
		}
	} else if(delta > 0) {
		if(freq_hist > 0) {
			freq_hist++;
		} else {
			freq_hist = 1;
		}
	} else {
		freq_hist = 0;
	}

	if(freq_hist) {
		int afh = freq_hist < 0 ? -freq_hist : freq_hist;
		if(afh > 1) {
			period += double(period_adjust_base) * double(delta) / double(period);

			if(period < min_period) {
				period = min_period;
			} else if(period > max_period) {
				period = max_period;
			}
		}
	}

	return 1;
}

bool FloppyCtrl_Flux::PLL::write_next_bit(bool bit, uint64_t &tm, FloppyDrive *floppy, uint64_t limit)
{
	UNUSED(floppy);

	if(write_start_time == TIME_NEVER) {
		write_start_time = ctime;
		write_position = 0;
	}

	int64_t etime = ctime + period;
	if(etime > int64_t(limit)) {
		return true;
	}

	if(bit && write_position < std::size(write_buffer)) {
		write_buffer[write_position++] = ctime + period/2;
	}

	tm = etime;
	ctime = etime;
	return false;
}