/*
 * 	Copyright (c) 2002-2014  The Bochs Project
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
 * Switched from AT mode to Model 30 mode.
 */

//
// Floppy Disk Controller Docs:
// Intel 82077A Data sheet
//   ftp://void-core.2y.net/pub/docs/fdc/82077AA_FloppyControllerDatasheet.pdf
// Intel 82078 Data sheet
//   ftp://download.intel.com/design/periphrl/datashts/29047403.PDF
// Other FDC references
//   http://debs.future.easyspace.com/Programming/Hardware/FDC/floppy.html
// And a port list:
//   http://mudlist.eorbit.net/~adam/pickey/ports.html
//

extern "C" {
#include <errno.h>
}

#ifdef __linux__
extern "C" {
#include <sys/ioctl.h>
#include <linux/fd.h>
#include <unistd.h>
}
#endif

#ifdef _WIN32
#include "wincompat.h"
extern "C" {
#include <winioctl.h>
#include <io.h>
}
#endif
#include <sys/stat.h>
#include <fcntl.h>

#include "ibmulator.h"
#include "program.h"
#include "machine.h"
#include "dma.h"
#include "pic.h"
#include "hardware/iodevice.h"
#include "hardware/devices.h"
#include "hardware/devices/systemboard.h"
#include "floppy.h"
#include "vvfat.h"



#include <cstring>
#include <functional>
using namespace std::placeholders;

FloppyCtrl g_floppy;


/* for main status register */
#define FD_MS_MRQ  0x80
#define FD_MS_DIO  0x40
#define FD_MS_NDMA 0x20
#define FD_MS_BUSY 0x10
#define FD_MS_ACTD 0x08
#define FD_MS_ACTC 0x04
#define FD_MS_ACTB 0x02
#define FD_MS_ACTA 0x01

#define FROM_FLOPPY 10
#define TO_FLOPPY   11

#define FLOPPY_DMA_CHAN 2

#define FDRIVE_NONE  0x00
#define FDRIVE_525DD 0x01
#define FDRIVE_525HD 0x02
#define FDRIVE_350DD 0x04
#define FDRIVE_350HD 0x08
#define FDRIVE_350ED 0x10

typedef struct {
	unsigned id;
	uint8_t trk;
	uint8_t hd;
	uint8_t spt;
	unsigned sectors;
	uint8_t drive_mask;
} floppy_type_t;

static floppy_type_t floppy_type[8] = {
	{FLOPPY_160K, 40, 1, 8, 320, 0x03},
	{FLOPPY_180K, 40, 1, 9, 360, 0x03},
	{FLOPPY_320K, 40, 2, 8, 640, 0x03},
	{FLOPPY_360K, 40, 2, 9, 720, 0x03},
	{FLOPPY_720K, 80, 2, 9, 1440, 0x1f},
	{FLOPPY_1_2,  80, 2, 15, 2400, 0x02},
	{FLOPPY_1_44, 80, 2, 18, 2880, 0x18},
	{FLOPPY_2_88, 80, 2, 36, 5760, 0x10}
};

static uint16_t drate_in_k[4] = {
	500, 300, 250, 1000
};

static std::map<std::string, uint> drive_types = {
	{ "none", FDRIVE_NONE },
	{ "3.5", FDRIVE_350HD },
	{ "5.25", FDRIVE_525HD }
};

static std::map<std::string, uint> disk_types_350 = {
	{ "none", FLOPPY_NONE },
	{ "1.44M", FLOPPY_1_44 },
	{ "720K", FLOPPY_720K }
};

static std::map<std::string, uint> disk_types_525 = {
	{ "none", FLOPPY_NONE },
	{ "1.2M", FLOPPY_1_2 },
	{ "360K", FLOPPY_360K }
};

static std::map<uint, std::string> rev_disk_types = {
	{ FLOPPY_NONE, "none" },
	{ FLOPPY_1_44, "1.44M" },
	{ FLOPPY_720K, "720K" },
	{ FLOPPY_1_2, "1.2M" },
	{ FLOPPY_360K, "360K"  }
};

FloppyCtrl::FloppyCtrl()
{
	memset(&m_s, 0, sizeof(m_s));
	m_timer_index = NULL_TIMER_HANDLE;
	for(uint i=0; i<4; i++) {
		m_media[i].fd = -1;
		m_media_present[i] = false;
		m_device_type[i] = FDRIVE_NONE;
	}
	m_num_installed_floppies = 0;
}

FloppyCtrl::~FloppyCtrl()
{
	for(int i = 0; i < 2; i++) {
		m_media[i].close();
	}
}

void FloppyCtrl::init(void)
{
	g_dma.register_8bit_channel(FLOPPY_DMA_CHAN,
			std::bind(&FloppyCtrl::dma_read, this, _1, _2),
			std::bind(&FloppyCtrl::dma_write, this, _1, _2),
			get_name());
	g_machine.register_irq(6, get_name());

	g_devices.register_read_handler(this, 0x03F0, 1); //Status Register A R

	g_devices.register_read_handler(this, 0x03F1, 1); //Status Register B R

	g_devices.register_read_handler(this, 0x03F2, 1);  //DOR R
	g_devices.register_write_handler(this, 0x03F2, 1); //DOR W; on the PS/1 tech ref is read-only,
	                                                   //on the 82077AA datasheet is R/W

	g_devices.register_read_handler(this, 0x03F4, 1);  // MSR R

	g_devices.register_write_handler(this, 0x03F4, 1); // DSR W (03F4 should be read only on AT-PS2-Mod30)

	g_devices.register_read_handler(this, 0x03F5, 1);  // FIFO R
	g_devices.register_write_handler(this, 0x03F5, 1); // FIFO W

	g_devices.register_read_handler(this, 0x03F7, 1); // DIR R

	g_devices.register_write_handler(this, 0x03F7, 1); // CCR W

	m_timer_index = g_machine.register_timer(
			std::bind(&FloppyCtrl::timer,this),
			250,    // period usec
			false,  // continuous
			false,  // active
			get_name()//name
	);

	for(uint i=0; i<4; i++) {
		m_media[i].type              = FLOPPY_NONE;
		m_media[i].sectors_per_track = 0;
		m_media[i].tracks            = 0;
		m_media[i].heads             = 0;
		m_media[i].sectors           = 0;
		m_media[i].fd                = -1;
		m_media[i].vvfat_floppy      = 0;
		m_media_present[i]           = false;
		m_device_type[i] = FDRIVE_NONE;
		m_disk_changed[i] = false;
	}

	config_changed();
}

void FloppyCtrl::config_changed()
{
	//ONLY 2 DRIVES
	//unmount previous media if present
	if(m_media_present[0]) {
		//don't call eject_media because it alters the config
		m_media[0].close();
		m_media_present[0] = false;
	}
	if(m_media_present[1]) {
		m_media[1].close();
		m_media_present[1] = false;
	}

	m_num_installed_floppies = 0;

	floppy_drive_setup(0);
	floppy_drive_setup(1);
}

void FloppyCtrl::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_FDC, "saving state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void FloppyCtrl::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_FDC, "restoring state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

void FloppyCtrl::floppy_drive_setup(uint drive)
{
	const char *drivename, *section, *typekey;
	ASSERT(drive<=1);
	if(drive == 0) {
		drivename = "A";
		section = DISK_A_SECTION;
		typekey = DRIVES_FDD_A;
	} else {
		drivename = "B";
		section = DISK_B_SECTION;
		typekey = DRIVES_FDD_B;
	}

	std::map<std::string, uint> * mediatypes = NULL;
	uint devtype;
	try {
		devtype = g_program.config().get_enum(DRIVES_SECTION, typekey, drive_types);
		if(devtype == FDRIVE_350HD) {
			mediatypes = &disk_types_350;
		} else if(devtype == FDRIVE_525HD) {
			mediatypes = &disk_types_525;
		}
	} catch(std::exception &e) {
		devtype = FDRIVE_NONE;
	}

	m_device_type[drive] = devtype;
	if(devtype != FDD_NONE) {
		m_num_installed_floppies++;
		PINFOF(LOG_V0, LOG_FDC, "Installed floppy %s as %s\n",
				drivename, devtype==FDRIVE_350HD?"3.5 HD":"5.25 HD");
	} else {
		return;
	}

	uint disktype;
	try {
		disktype = g_program.config().get_enum(section, DISK_TYPE, *mediatypes);
	} catch(std::exception &e) {
		disktype = FLOPPY_NONE;
	}
	bool inserted = g_program.config().get_bool(section, DISK_INSERTED);
	if(disktype != FLOPPY_NONE && inserted) {
		std::string diskpath = g_program.config().find_media(section, DISK_PATH);
		insert_media(drive, disktype, diskpath.c_str(), g_program.config().get_bool(section, DISK_READONLY));
	}
}

void FloppyCtrl::reset(unsigned type)
{
	uint32_t i;
	if(type == MACHINE_POWER_ON) {

		memset(&m_s, 0, sizeof(m_s));

		m_s.main_status_reg &= ~FD_MS_NDMA;  // enable DMA from start
		/* these registers are not cleared by normal reset */
		m_s.SRT = 0;
		m_s.HUT = 0;
		m_s.HLT = 0;
	}

	m_s.pending_irq = 0;
	m_s.reset_sensei = 0; /* no reset result present */

	m_s.main_status_reg = 0;
	m_s.status_reg0 = 0;
	m_s.status_reg1 = 0;
	m_s.status_reg2 = 0;
	m_s.status_reg3 = 0;

	// software reset (via DOR port 0x3f2 bit 2) does not change DOR
	if(type != DEVICE_SOFT_RESET) { //hard or power on
		m_s.DOR = 0x0c;
		// motor off, drive 3..0
		// DMA/INT enabled
		// normal operation
		// drive select 0

		// DIR and CCR affected only by hard reset
		for(i=0; i<4; i++) {
			m_s.DIR[i] |= 0x80; // disk changed
		}
		m_s.data_rate = 2; /* 250 Kbps */
		m_s.lock = 0;
	}
	if(m_s.lock == 0) {
		m_s.config = 0;
		m_s.pretrk = 0;
	}
	m_s.perp_mode = 0;

	for(i=0; i<4; i++) {
		m_s.cylinder[i] = 0;
	    m_s.cur_cylinder[i] = 0;
		m_s.head[i] = 0;
		m_s.sector[i] = 0;
		m_s.eot[i] = 0;
		m_s.step[i] = false;
		m_s.wrdata[i] = false;
		m_s.rddata[i] = false;
	}

	g_pic.lower_irq(6);
	if(!(m_s.main_status_reg & FD_MS_NDMA)) {
		g_dma.set_DRQ(FLOPPY_DMA_CHAN, 0);
	}
	enter_idle_phase();
}

void FloppyCtrl::power_off()
{
	//"shut down" the motors
	m_s.DOR = 0;
}

uint16_t FloppyCtrl::read(uint16_t address, unsigned)
{
	uint8_t value = 0, drive;

	g_sysboard.set_feedback();

	switch (address) {
		case 0x3F0: // diskette controller status register A
			drive = m_s.DOR & 0x03;
			//Model30 mode:
			// Bit 7 : INT PENDING
			value |= (m_s.pending_irq << 7);
			// Bit 6 : DRQ
			value |= (g_dma.get_DRQ(FLOPPY_DMA_CHAN) << 6);
			// Bit 5 : STEP F/F
			value |= (m_s.step[drive] << 5);
			// Bit 4 : TRK0
			if(m_s.cur_cylinder[drive] == 0) {
				value |= 1<<4;
			}
			// Bit 3 : !HDSEL
			value |= (!m_s.head[drive]) << 3;
			// Bit 2 : INDEX
			if(m_s.sector[drive] == 0) {
				value |= 1<<2;
			}
			// Bit 1 : WP
			if(m_media_present[drive] && m_media[drive].write_protected) {
				value |= 1<<1;
			}
			// Bit 0 : !DIR
			value |= !m_s.direction[drive];
			break;
		case 0x3F1: // diskette controller status register B
			drive = m_s.DOR & 0x03;
			//Model30 mode:
			// Bit 7 : !DRV2 (is B drive installed?)
			value |= (!(m_num_installed_floppies>1)) << 7;
			// Bit 6 : !DS1
			value |= (!(drive==1)) << 6;
			// Bit 5 : !DS0
			value |= (!(drive==0)) << 5;
			// Bit 4 : WRDATA F/F
			value |= m_s.wrdata[drive] << 4;
			// Bit 3 : RDDATA F/F
			value |= m_s.rddata[drive] << 3;
			// Bit 2 : WE F/F
			value |= m_s.wrdata[drive] << 2; //repeat wrdata?
			// Bit 1 : !DS3
			value |= (!(drive==3)) << 1;
			// Bit 0 : !DS2
			value |= !(drive==2);

			break;
		case 0x3F2: // diskette controller digital output register
			//AT-PS/2-Model30 mode
			value = m_s.DOR;
			break;

		case 0x3F4: /* diskette controller main status register */
			//AT-PS/2-Model30 mode
			value = m_s.main_status_reg;
			break;

		case 0x3F5: /* diskette controller data */
			if((m_s.main_status_reg & FD_MS_NDMA) && ((m_s.pending_command & 0x4f) == 0x46)) {
				dma_write(&value, 1);
				lower_interrupt();
				// don't enter idle phase until we've given CPU last data byte
				if(m_s.TC) {
					enter_idle_phase();
				}
			} else if(m_s.result_size == 0) {
				PDEBUGF(LOG_V0, LOG_FDC, "port 0x3f5: no results to read\n");
				m_s.main_status_reg &= FD_MS_NDMA;
				value = m_s.result[0];
			} else {
				value = m_s.result[m_s.result_index++];
				m_s.main_status_reg &= 0xF0;
				lower_interrupt();
				if(m_s.result_index >= m_s.result_size) {
					enter_idle_phase();
				}
			}
			break;

		case 0x3F7: // DIR: diskette controller digital input register
		{
			// turn on the drive motor bit before access the DIR register for a selected drive
			drive = m_s.DOR & 0x03;
			bool motor = m_s.DOR & (1<<(drive+4));
			if(motor) {
				//Model30 mode only
				// Bit 7 : !DSKCHG
				// Bochs is in AT mode, invert the value
				value |= !(bool(m_s.DIR[drive] & 0x80)) << 7;
				// Bit 3 : !DMAGATE (DOR)
				value |= m_s.DOR & 0x08; //same position
				// Bit 2 : NOPREC (CCR)
				value |= (m_s.noprec << 2);
				// Bit 1-0 : DRATE SEL1-0 (CCR)
				value |= m_s.data_rate;
				/*
				The	STEP bit is latched with the Step output going active
				and is cleared with a read to the DIR register, Hardware or Software RESET
				*/
				m_s.step[drive] = false;
			}

			break;
		}
		default:
			ASSERT(false);
			return 0;
	}

	PDEBUGF(LOG_V2, LOG_FDC, "read(): during command 0x%02x, port 0x%04x returns 0x%02x\n",
			m_s.pending_command, address, value);

	return value;
}

/* writes to the floppy io ports */
void FloppyCtrl::write(uint16_t address, uint16_t value, unsigned)
{
	uint8_t dma_and_interrupt_enable;
	uint8_t normal_operation, prev_normal_operation;
	uint8_t drive_select;
	uint8_t motor_on_drive0, motor_on_drive1;

	PDEBUGF(LOG_V2, LOG_FDC, "write access to port 0x%04x, value=0x%02x\n", address, value);

	g_sysboard.set_feedback();

	switch (address) {

		case 0x3F2: /* diskette controller digital output register */
			motor_on_drive0 = value & 0x10;
			motor_on_drive1 = value & 0x20;
			dma_and_interrupt_enable = value & 0x08;
			if(!dma_and_interrupt_enable) {
				PDEBUGF(LOG_V2, LOG_FDC, "DMA and interrupt capabilities disabled\n");
			}
			normal_operation = value & 0x04;
			drive_select = value & 0x03;

			prev_normal_operation = m_s.DOR & 0x04;
			m_s.DOR = value;

			if(prev_normal_operation==0 && normal_operation) {
				// transition from RESET to NORMAL
				g_machine.activate_timer(m_timer_index, 250, 0);
			} else if(prev_normal_operation && normal_operation==0) {
				// transition from NORMAL to RESET
				m_s.main_status_reg &= FD_MS_NDMA;
				m_s.pending_command = 0xfe; // RESET pending
			}
			PDEBUGF(LOG_V2, LOG_FDC, "io_write: digital output register");
			PDEBUGF(LOG_V2, LOG_FDC, "  motor_drive0=%d", motor_on_drive0 > 0);
			PDEBUGF(LOG_V2, LOG_FDC, "  motor_drive1=%d", motor_on_drive1 > 0);
			PDEBUGF(LOG_V2, LOG_FDC, "  dma_and_interrupt_enable=%02x", dma_and_interrupt_enable);
			PDEBUGF(LOG_V2, LOG_FDC, "  normal_operation=%02x", normal_operation);
			PDEBUGF(LOG_V2, LOG_FDC, "  drive_select=%02x\n", drive_select);
			if(m_device_type[drive_select] == FDRIVE_NONE) {
				PDEBUGF(LOG_V0, LOG_FDC, "WARNING: non existing drive selected\n");
			}
			break;

		case 0x3f4: /* diskette controller data rate select register */
			PDEBUGF(LOG_V0, LOG_FDC, "write to data rate select register: should be invalid on Mod30!\n");
			m_s.data_rate = value & 0x03;
			if(value & 0x80) {
				m_s.main_status_reg &= FD_MS_NDMA;
				m_s.pending_command = 0xfe; // RESET pending
				g_machine.activate_timer(m_timer_index, 250, 0);
			}
			if(value & 0x7c) {
				PDEBUGF(LOG_V0, LOG_FDC, "write to data rate select register: unsupported bits set\n");
			}
			break;

		case 0x3F5: /* diskette controller data FIFO */
			PDEBUGF(LOG_V2, LOG_FDC, "command = 0x%02x\n", value);
			if((m_s.main_status_reg & FD_MS_NDMA) && ((m_s.pending_command & 0x4f) == 0x45)) { // write normal data, MT=0
				dma_read((uint8_t *) &value, 1);
				lower_interrupt();
				break;
			} else if(m_s.command_complete) {
				if(m_s.pending_command != 0) {
					PERRF_ABORT(LOG_FDC, "write 0x03f5: receiving new command 0x%02x, old one (0x%02x) pending\n",
							value, m_s.pending_command);
				}
				m_s.command[0] = value;
				m_s.command_complete = 0;
				m_s.command_index = 1;
				/* read/write command in progress */
				m_s.main_status_reg &= ~FD_MS_DIO; // leave drive status untouched
				m_s.main_status_reg |= FD_MS_MRQ | FD_MS_BUSY;
				switch(value) {
					case 0x03: /* specify */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = specify\n");
						m_s.command_size = 3;
						break;
					case 0x04: /* get status */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = get status\n");
						m_s.command_size = 2;
						break;
					case 0x07: /* recalibrate */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = recalibrate\n");
						m_s.command_size = 2;
						break;
					case 0x08: /* sense interrupt status */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = sense interrupt status\n");
						m_s.command_size = 1;
						break;
					case 0x0f: /* seek */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = seek\n");
						m_s.command_size = 3;
						break;
					case 0x4a: /* read ID */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = read ID\n");
						m_s.command_size = 2;
						break;
					case 0x4d: /* format track */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = format track\n");
						m_s.command_size = 6;
						break;
					case 0x45:
					case 0xc5: /* write normal data */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = write normal data\n");
						m_s.command_size = 9;
						break;
					case 0x46:
					case 0x66:
					case 0xc6:
					case 0xe6: /* read normal data */
						//PDEBUGF(LOG_V2, LOG_FDC, "command = read normal data\n");
						m_s.command_size = 9;
						break;

					//INVALID COMMANDS:
					case 0x0e: // dump registers (Enhanced drives)
					case 0x10: // Version command, enhanced controller returns 0x90
					case 0x14: // Unlock command (Enhanced)
					case 0x94: // Lock command (Enhanced)
					case 0x12: // Perpendicular mode (Enhanced)
					case 0x13: // Configure command (Enhanced)
					case 0x18: // National Semiconductor version command
					default:
						PDEBUGF(LOG_V0, LOG_FDC, "invalid floppy command 0x%02x", value);
						m_s.command_size = 0;   // make sure we don't try to process this command
						m_s.status_reg0 = 0x80; // status: invalid command
						enter_result_phase();
						break;
				}
			} else {
				m_s.command[m_s.command_index++] = value;
			}
			if(m_s.command_index == m_s.command_size) {
				/* read/write command not in progress any more */
				floppy_command();
				m_s.command_complete = 1;
			}
			return;


		case 0x3F7: /* diskette controller configuration control register */
			PDEBUGF(LOG_V2, LOG_FDC, "config control register: 0x%02x ", value);
			m_s.data_rate = value & 0x03;
			switch (m_s.data_rate) {
				case 0: PDEBUGF(LOG_V2, LOG_FDC, "  500 Kbps\n"); break;
				case 1: PDEBUGF(LOG_V2, LOG_FDC, "  300 Kbps\n"); break;
				case 2: PDEBUGF(LOG_V2, LOG_FDC, "  250 Kbps\n"); break;
				case 3: PDEBUGF(LOG_V2, LOG_FDC, "  1 Mbps\n"); break;
			}
			m_s.noprec = value & 0x04;
			break;

		default:
			PDEBUGF(LOG_V0, LOG_FDC, "io_write ignored: 0x%04x = 0x%02x\n", address, value);
			break;

	}
}

void FloppyCtrl::floppy_command()
{
	unsigned i;
	uint8_t motor_on;
	uint8_t head, drive, cylinder, sector, eot;
	uint8_t sector_size;
	//uint8_t data_length;
	uint32_t logical_sector, sector_time, step_delay;

	// Print command
	char buf[9+(9*5)+1], *p = buf;
	p += sprintf(p, "COMMAND: ");
	for(i=0; i<m_s.command_size; i++) {
		p += sprintf(p, "[%02x] ", m_s.command[i]);
	}
	PDEBUGF(LOG_V2, LOG_FDC, "%s\n", buf);

	m_s.pending_command = m_s.command[0];
	switch (m_s.pending_command) {
		case 0x03: // specify
			// execution: specified parameters are loaded
			// result: no result bytes, no interrupt
			m_s.SRT = m_s.command[1] >> 4;
			m_s.HUT = m_s.command[1] & 0x0f;
			m_s.HLT = m_s.command[2] >> 1;

			PDEBUGF(LOG_V2, LOG_FDC, "command: specify SRT=%u,HUT=%u,HLT=%u\n",
					m_s.SRT, m_s.HUT, m_s.HLT);

			m_s.main_status_reg |= (m_s.command[2] & 0x01) ? FD_MS_NDMA : 0;
			if(m_s.main_status_reg & FD_MS_NDMA) {
				PDEBUGF(LOG_V0, LOG_FDC, "non DMA mode not fully implemented yet\n");
			}
			enter_idle_phase();
			return;

		case 0x04: // get status
			PDEBUGF(LOG_V2, LOG_FDC, "command: get status\n");
			drive = (m_s.command[1] & 0x03);
			m_s.head[drive] = (m_s.command[1] >> 2) & 0x01;
			m_s.status_reg3 = 0x28 | (m_s.head[drive]<<2) | drive
					| (m_media[drive].write_protected ? 0x40 : 0x00);
			if((m_device_type[drive] != FDRIVE_NONE) && (m_s.cur_cylinder[drive] == 0)) {
				//the head takes time to move to track0; this time is used to determine if 40 or 80 tracks
				//the value of cur_cylinder for the drive is set in the timer handler
				m_s.status_reg3 |= 0x10;
			}
			enter_result_phase();
			return;

		case 0x07: // recalibrate
			drive = (m_s.command[1] & 0x03);
			m_s.DOR &= 0xfc;
			m_s.DOR |= drive;
			PDEBUGF(LOG_V2, LOG_FDC, "command: recalibrate drive %u\n", drive);
			step_delay = calculate_step_delay(drive, 0);
			g_machine.activate_timer(m_timer_index, step_delay, 0);
			/* command head to track 0
			* controller set to non-busy
			* error condition noted in Status reg 0'm_s equipment check bit
			* seek end bit set to 1 in Status reg 0 regardless of outcome
			* The last two are taken care of in timer().
			*/
			m_s.direction[drive] = (m_s.cylinder[drive]>0);
			m_s.cylinder[drive] = 0;
			m_s.main_status_reg &= FD_MS_NDMA;
			m_s.main_status_reg |= (1 << drive);
			return;

		case 0x08: /* sense interrupt status */
			/* execution:
			*   get status
			* result:
			*   no interupt
			*   byte0 = status reg0
			*   byte1 = current cylinder number (0 to 79)
			*/
			PDEBUGF(LOG_V2, LOG_FDC, "command: sense interrupt status\n");
			if(m_s.reset_sensei > 0) {
				drive = 4 - m_s.reset_sensei;
				m_s.status_reg0 &= 0xf8;
				m_s.status_reg0 |= (m_s.head[drive] << 2) | drive;
				m_s.reset_sensei--;
			} else if(!m_s.pending_irq) {
				m_s.status_reg0 = 0x80;
			}
			enter_result_phase();
			return;

		case 0x0f: /* seek */
			/* command:
			*   byte0 = 0F
			*   byte1 = drive & head select
			*   byte2 = cylinder number
			* execution:
			*   postion head over specified cylinder
			* result:
			*   no result bytes, issues an interrupt
			*/

			drive = m_s.command[1] & 0x03;
			m_s.DOR &= 0xfc;
			m_s.DOR |= drive;

			m_s.head[drive] = (m_s.command[1] >> 2) & 0x01;
			step_delay = calculate_step_delay(drive, m_s.command[2]);
			g_machine.activate_timer(m_timer_index, step_delay, 0);
			/* ??? should also check cylinder validity */
			m_s.direction[drive] = (m_s.cylinder[drive]>m_s.command[2]);
			m_s.cylinder[drive] = m_s.command[2];
			/* data reg not ready, drive not busy */
			m_s.main_status_reg &= FD_MS_NDMA;
			m_s.main_status_reg |= (1 << drive);

			PDEBUGF(LOG_V2, LOG_FDC, "command: seek drive %u, cyl. %u\n", drive, m_s.command[2]);

			return;

		case 0x13: // Configure
			PDEBUGF(LOG_V2, LOG_FDC, "command: configure (eis     = 0x%02x)\n", m_s.command[2] & 0x40);
			PDEBUGF(LOG_V2, LOG_FDC, "command: configure (efifo   = 0x%02x)\n", m_s.command[2] & 0x20);
			PDEBUGF(LOG_V2, LOG_FDC, "command: configure (no poll = 0x%02x)\n", m_s.command[2] & 0x10);
			PDEBUGF(LOG_V2, LOG_FDC, "command: configure (fifothr = 0x%02x)\n", m_s.command[2] & 0x0f);
			PDEBUGF(LOG_V2, LOG_FDC, "command: configure (pretrk  = 0x%02x)\n", m_s.command[3]);
			m_s.config = m_s.command[2];
			m_s.pretrk = m_s.command[3];
			enter_idle_phase();
			return;

		case 0x4a: // read ID
			drive = m_s.command[1] & 0x03;
			m_s.head[drive] = (m_s.command[1] >> 2) & 0x01;
			m_s.DOR &= 0xfc;
			m_s.DOR |= drive;

			PDEBUGF(LOG_V2, LOG_FDC, "command: read ID\n");

			motor_on = (m_s.DOR>>(drive+4)) & 0x01;
			if(motor_on == 0) {
				PDEBUGF(LOG_V0, LOG_FDC, "command: read ID: motor not on\n");
				m_s.main_status_reg &= FD_MS_NDMA;
				m_s.main_status_reg |= FD_MS_BUSY;
				return; // Hang controller
			}
			if(m_device_type[drive] == FDRIVE_NONE) {
				PDEBUGF(LOG_V0, LOG_FDC, "command: read ID: bad drive #%d\n", drive);
				m_s.main_status_reg &= FD_MS_NDMA;
				m_s.main_status_reg |= FD_MS_BUSY;
				return; // Hang controller
			}
			if(m_media_present[drive] == 0) {
				PINFOF(LOG_V1, LOG_FDC, "command: read ID: attempt to read sector ID with media not present\n");
				m_s.main_status_reg &= FD_MS_NDMA;
				m_s.main_status_reg |= FD_MS_BUSY;
				return; // Hang controller
			}
			m_s.status_reg0 = (m_s.head[drive]<<2) | drive;
			// time to read one sector at 300 rpm
			sector_time = 200000 / m_media[drive].sectors_per_track;
			g_machine.activate_timer(m_timer_index, sector_time, 0);
			/* data reg not ready, controller busy */
			m_s.main_status_reg &= FD_MS_NDMA;
			m_s.main_status_reg |= FD_MS_BUSY;
			return;

		case 0x4d: // format track
			PDEBUGF(LOG_V2, LOG_FDC, "command: format track\n");

			drive = m_s.command[1] & 0x03;
			m_s.DOR &= 0xfc;
			m_s.DOR |= drive;

			motor_on = (m_s.DOR>>(drive+4)) & 0x01;
			if(motor_on == 0) {
				PERRF_ABORT(LOG_FDC, "command: format track: motor not on\n");
				return; // Hang controller?
			}
			m_s.head[drive] = (m_s.command[1] >> 2) & 0x01;
			sector_size = m_s.command[2];
			m_s.format_count = m_s.command[3];
			m_s.format_fillbyte = m_s.command[5];
			if(m_device_type[drive] == FDRIVE_NONE) {
				PERRF_ABORT(LOG_FDC, "command: format track: bad drive #%d\n", drive);
				return; // Hang controller?
			}
			if(sector_size != 0x02) { // 512 bytes
				PERRF_ABORT(LOG_FDC, "command: format track: sector size %d not supported\n", 128<<sector_size);
				return; // Hang controller?
			}
			if(m_s.format_count != m_media[drive].sectors_per_track) {
				PERRF_ABORT(LOG_FDC, "command: format track: %d sectors/track requested (%d expected)\n",
						m_s.format_count, m_media[drive].sectors_per_track);
				return; // Hang controller?
			}
			if(m_media_present[drive] == 0) {
				PINFOF(LOG_V1, LOG_FDC, "command: format track: attempt to format track with media not present\n");
				return; // Hang controller
			}
			if(m_media[drive].write_protected) {
				// media write-protected, return error
				PINFOF(LOG_V1, LOG_FDC, "command: format track: attempt to format track with media write-protected\n");
				m_s.status_reg0 = 0x40 | (m_s.head[drive]<<2) | drive; // abnormal termination
				m_s.status_reg1 = 0x27; // 0010 0111
				m_s.status_reg2 = 0x31; // 0011 0001
				enter_result_phase();
				return;
			}

			/* 4 header bytes per sector are required */
			m_s.format_count <<= 2;

			if(m_s.main_status_reg & FD_MS_NDMA) {
				PWARNF(LOG_FDC, "command: format track: non-DMA floppy format unimplemented\n");
			} else {
				g_dma.set_DRQ(FLOPPY_DMA_CHAN, 1);
			}
			/* data reg not ready, controller busy */
			m_s.main_status_reg &= FD_MS_NDMA;
			m_s.main_status_reg |= FD_MS_BUSY;

			return;

		case 0x46: // read normal data, MT=0, SK=0
		case 0x66: // read normal data, MT=0, SK=1
		case 0xc6: // read normal data, MT=1, SK=0
		case 0xe6: // read normal data, MT=1, SK=1
		case 0x45: // write normal data, MT=0
		case 0xc5: // write normal data, MT=1
			m_s.multi_track = (m_s.command[0] >> 7);
			if((m_s.DOR & 0x08) == 0) {
				PERRF(LOG_FDC, "command: read/write command with DMA and int disabled\n");
				return;
			}
			drive = m_s.command[1] & 0x03;
			m_s.DOR &= 0xfc;
			m_s.DOR |= drive;

			motor_on = (m_s.DOR>>(drive+4)) & 0x01;
			if(motor_on == 0) {
				PERRF(LOG_FDC, "command: read/write: motor not on\n");
				return;
			}
			head = m_s.command[3] & 0x01;
			cylinder = m_s.command[2]; /* 0..79 depending */
			sector = m_s.command[4];   /* 1..36 depending */
			eot = m_s.command[6];      /* 1..36 depending */
			sector_size = m_s.command[5];
			//data_length = m_s.command[8];
			PDEBUGF(LOG_V2, LOG_FDC, "command: read/write normal data\n");
			PDEBUGF(LOG_V2, LOG_FDC, " BEFORE ");
			PDEBUGF(LOG_V2, LOG_FDC, " drive=%u", drive);
			PDEBUGF(LOG_V2, LOG_FDC, " head=%u", head);
			PDEBUGF(LOG_V2, LOG_FDC, " cylinder=%u", cylinder);
			PDEBUGF(LOG_V2, LOG_FDC, " sector=%u", sector);
			PDEBUGF(LOG_V2, LOG_FDC, " eot=%u\n", eot);
			if(m_device_type[drive] == FDRIVE_NONE) {
				PERRF(LOG_FDC, "command: read/write: bad drive #%d\n", drive);
				return;
			}

			// check that head number in command[1] bit two matches the head
			// reported in the head number field.  Real floppy drives are
			// picky about this, as reported in SF bug #439945, (Floppy drive
			// read input error checking).
			if(head != ((m_s.command[1]>>2)&1)) {
				PDEBUGF(LOG_V0, LOG_FDC, "command: read/write: head number in command[1] doesn't match head field\n");
				m_s.status_reg0 = 0x40 | (m_s.head[drive]<<2) | drive; // abnormal termination
				m_s.status_reg1 = 0x04; // 0000 0100
				m_s.status_reg2 = 0x00; // 0000 0000
				enter_result_phase();
				return;
			}

			if(m_media_present[drive] == 0) {
				PINFOF(LOG_V1, LOG_FDC, "command: read/write: attempt to read/write sector %u with media not present\n", sector);
				return; // Hang controller
			}

			if(sector_size != 0x02) { // 512 bytes
				PERRF(LOG_FDC, "command: read/write: sector size %d not supported\n", 128<<sector_size);
				return;
			}

			if(cylinder >= m_media[drive].tracks) {
				PERRF(LOG_FDC, "command: read/write: norm r/w parms out of range: sec#%02xh cyl#%02xh eot#%02xh head#%02xh\n",
						sector, cylinder, eot, head);
				return;
			}

			if(sector > m_media[drive].sectors_per_track) {
				PINFOF(LOG_V1, LOG_FDC, "command: read/write: attempt to read/write sector %u past last sector %u\n",
						sector, m_media[drive].sectors_per_track);
				m_s.direction[drive] = (m_s.cylinder[drive]>cylinder);
				m_s.cylinder[drive] = cylinder;
				m_s.head[drive]     = head;
				m_s.sector[drive]   = sector;

				m_s.status_reg0 = 0x40 | (m_s.head[drive]<<2) | drive;
				m_s.status_reg1 = 0x04;
				m_s.status_reg2 = 0x00;
				enter_result_phase();
				return;
			}

			if(cylinder != m_s.cylinder[drive]) {
				PDEBUGF(LOG_V2, LOG_FDC, "command: read/write: cylinder request != current cylinder\n");
				reset_changeline();
			}

			logical_sector = (cylinder * m_media[drive].heads * m_media[drive].sectors_per_track) +
						(head * m_media[drive].sectors_per_track) +
						(sector - 1);

			if(logical_sector >= m_media[drive].sectors) {
				PERRF_ABORT(LOG_FDC, "command: read/write: logical sector out of bounds\n");
			}
			// This hack makes older versions of the Bochs BIOS work
			if(eot == 0) {
				eot = m_media[drive].sectors_per_track;
			}
			m_s.direction[drive] = (m_s.cylinder[drive]>cylinder);
			m_s.cylinder[drive] = cylinder;
			m_s.head[drive]     = head;
			m_s.sector[drive]   = sector;
			m_s.eot[drive]      = eot;

			if((m_s.command[0] & 0x4f) == 0x46) { // read
				m_s.rddata[drive] = true;
				floppy_xfer(drive, logical_sector*512, m_s.floppy_buffer, 512, FROM_FLOPPY);
				/* controller busy; if DMA mode, data reg not ready */
				m_s.main_status_reg &= FD_MS_NDMA;
				m_s.main_status_reg |= FD_MS_BUSY;
				if(m_s.main_status_reg & FD_MS_NDMA) {
					m_s.main_status_reg |= (FD_MS_MRQ | FD_MS_DIO);
				}
				// time to read one sector at 300 rpm
				sector_time = 200000 / m_media[drive].sectors_per_track;
				g_machine.activate_timer(m_timer_index, sector_time , 0);
			} else if((m_s.command[0] & 0x7f) == 0x45) { // write
				m_s.wrdata[drive] = true;
				/* controller busy; if DMA mode, data reg not ready */
				m_s.main_status_reg &= FD_MS_NDMA;
				m_s.main_status_reg |= FD_MS_BUSY;
				if(m_s.main_status_reg & FD_MS_NDMA) {
					m_s.main_status_reg |= FD_MS_MRQ;
				} else {
					g_dma.set_DRQ(FLOPPY_DMA_CHAN, 1);
				}
			} else {
				PERRF_ABORT(LOG_FDC, "command: unknown read/write command\n");
				return;
			}
			break;

		case 0x12: // Perpendicular mode
			/*
			m_s.perp_mode = m_s.command[1];
			PINFOF(LOG_V1, LOG_FDC, "perpendicular mode: config=0x%02x", m_s.perp_mode));
			enter_idle_phase();
			break;
			*/

		default: // invalid or unsupported command; these are captured in write() above
			PERRF_ABORT(LOG_FDC, "You should never get here! cmd = 0x%02x\n", m_s.command[0]);
			break;
	}
}

void FloppyCtrl::floppy_xfer(uint8_t drive, uint32_t offset, uint8_t *buffer,
            uint32_t bytes, uint8_t direction)
{
	int ret = 0;

	if(m_device_type[drive] == FDRIVE_NONE) {
		PERRF_ABORT(LOG_FDC, "floppy_xfer: bad drive #%d\n", drive);
	}

	PDEBUGF(LOG_V2, LOG_FDC, "floppy_xfer: drive=%u, offset=%u, bytes=%u, direction=%s floppy\n",
			drive, offset, bytes, (direction==FROM_FLOPPY)? "from" : "to");


	if(m_media[drive].vvfat_floppy) {
		ret = (int)m_media[drive].vvfat->lseek(offset, SEEK_SET);
	} else {
		ret = (int)lseek(m_media[drive].fd, offset, SEEK_SET);
	}
	if(ret < 0) {
		PERRF_ABORT(LOG_FDC, "could not perform lseek() to %d on floppy image file\n", offset);
		return;
	}

	if(direction == FROM_FLOPPY) {
		if(m_media[drive].vvfat_floppy) {
			ret = (int)m_media[drive].vvfat->read(buffer, bytes);
		} else {
			ret = ::read(m_media[drive].fd, buffer, bytes);
		}
		if(ret < int(bytes)) {
			if(ret > 0) {
				PINFOF(LOG_V1, LOG_FDC, "partial read() on floppy image returns %u/%u\n", ret, bytes);
				memset(buffer + ret, 0, bytes - ret);
			} else {
				PINFOF(LOG_V1, LOG_FDC, "read() on floppy image returns 0\n");
				memset(buffer, 0, bytes);
			}
		}
	} else { // TO_FLOPPY
		if(m_media[drive].write_protected) {
			PERRF_ABORT(LOG_FDC, "floppy_xfer(): media is write protected");
		}
		if(m_media[drive].vvfat_floppy) {
			ret = (int)m_media[drive].vvfat->write(buffer, bytes);
		} else {
			ret = ::write(m_media[drive].fd, buffer, bytes);
		}
		if(ret < int(bytes)) {
			PERRF_ABORT(LOG_FDC, "could not perform write() on floppy image file\n");
		}
	}
}

void FloppyCtrl::timer()
{
	uint8_t drive, motor_on;

	drive = m_s.DOR & 0x03;
	switch (m_s.pending_command) {
		case 0x07: // recalibrate
			m_s.status_reg0 = 0x20 | drive;
			motor_on = ((m_s.DOR>>(drive+4)) & 0x01);
			if((m_device_type[drive] == FDRIVE_NONE) || (motor_on == 0)) {
				m_s.status_reg0 |= 0x50;
			}
			m_s.cur_cylinder[drive] = m_s.cylinder[drive];
			m_s.direction[drive] = 0;
			m_s.step[drive] = true;
			enter_idle_phase();
			raise_interrupt();
			break;

		case 0x0f: // seek
			m_s.status_reg0 = 0x20 | (m_s.head[drive]<<2) | drive;
			m_s.step[drive] = true;
			m_s.cur_cylinder[drive] = m_s.cylinder[drive];
			enter_idle_phase();
			raise_interrupt();
			break;

		case 0x4a: /* read ID */
			enter_result_phase();
			break;

		case 0x45: /* write normal data */
		case 0xc5:
			if(m_s.TC) { // Terminal Count line, done
				m_s.status_reg0 = (m_s.head[drive] << 2) | drive;
				m_s.status_reg1 = 0;
				m_s.status_reg2 = 0;

				PDEBUGF(LOG_V2, LOG_FDC, "<<WRITE DONE>>");
				PDEBUGF(LOG_V2, LOG_FDC, "AFTER");
				PDEBUGF(LOG_V2, LOG_FDC, "  drive = %u", drive);
				PDEBUGF(LOG_V2, LOG_FDC, "  head = %u", m_s.head[drive]);
				PDEBUGF(LOG_V2, LOG_FDC, "  cylinder = %u", m_s.cylinder[drive]);
				PDEBUGF(LOG_V2, LOG_FDC, "  sector = %u\n", m_s.sector[drive]);

				enter_result_phase();
			} else {
				// transfer next sector
				if(!(m_s.main_status_reg & FD_MS_NDMA)) {
					g_dma.set_DRQ(FLOPPY_DMA_CHAN, 1);
				}
			}
			m_s.step[drive] = true;
			m_s.cur_cylinder[drive] = m_s.cylinder[drive];
			break;

		case 0x46: /* read normal data */
		case 0x66:
		case 0xc6:
		case 0xe6:
			// transfer next sector
			if(m_s.main_status_reg & FD_MS_NDMA) {
				m_s.main_status_reg &= ~FD_MS_BUSY;  // clear busy bit
				m_s.main_status_reg |= FD_MS_MRQ | FD_MS_DIO;  // data byte waiting
			} else {
				g_dma.set_DRQ(FLOPPY_DMA_CHAN, 1);
			}
			m_s.step[drive] = true;
			m_s.cur_cylinder[drive] = m_s.cylinder[drive];
			break;

		case 0x4d: /* format track */
			if((m_s.format_count == 0) || m_s.TC) {
				m_s.format_count = 0;
				m_s.status_reg0 = (m_s.head[drive] << 2) | drive;
				enter_result_phase();
			} else {
				// transfer next sector
				if(!(m_s.main_status_reg & FD_MS_NDMA)) {
					g_dma.set_DRQ(FLOPPY_DMA_CHAN, 1);
				}
			}
			m_s.step[drive] = true;
			m_s.cur_cylinder[drive] = m_s.cylinder[drive];
			break;

		case 0xfe: // (contrived) RESET
			reset(DEVICE_SOFT_RESET);
			m_s.pending_command = 0;
			m_s.status_reg0 = 0xc0;
			raise_interrupt();
			m_s.reset_sensei = 4;
			break;

		case 0x00: // nothing pending?
			break;

		default:
			PERRF_ABORT(LOG_FDC, "timer(): unknown case %02x\n", m_s.pending_command);
			break;
	}
}

uint16_t FloppyCtrl::dma_write(uint8_t *buffer, uint16_t maxlen)
{
	// A DMA write is from I/O to Memory
	// We need to return the next data byte(m_s) from the floppy buffer
	// to be transfered via the DMA to memory. (read block from floppy)
	//
	// maxlen is the maximum length of the DMA transfer

	g_sysboard.set_feedback();

	uint8_t drive = m_s.DOR & 0x03;
	uint16_t len = 512 - m_s.floppy_buffer_index;
	if(len > maxlen) len = maxlen;
	memcpy(buffer, &m_s.floppy_buffer[m_s.floppy_buffer_index], len);
	m_s.floppy_buffer_index += len;
	m_s.TC = get_tc() && (len == maxlen);

	PDEBUGF(LOG_V2, LOG_FDC, "DMA write, drive %u\n", drive);

	if((m_s.floppy_buffer_index >= 512) || (m_s.TC)) {

		if(m_s.floppy_buffer_index >= 512) {
			increment_sector(); // increment to next sector before retrieving next one
			m_s.floppy_buffer_index = 0;
		}
		if(m_s.TC) { // Terminal Count line, done
			m_s.status_reg0 = (m_s.head[drive] << 2) | drive;
			m_s.status_reg1 = 0;
			m_s.status_reg2 = 0;

			PDEBUGF(LOG_V2, LOG_FDC, "<<READ DONE>>");
			PDEBUGF(LOG_V2, LOG_FDC, "AFTER");
			PDEBUGF(LOG_V2, LOG_FDC, "  drive = %u", drive);
			PDEBUGF(LOG_V2, LOG_FDC, "  head = %u", m_s.head[drive]);
			PDEBUGF(LOG_V2, LOG_FDC, "  cylinder = %u", m_s.cylinder[drive]);
			PDEBUGF(LOG_V2, LOG_FDC, "  sector = %u\n", m_s.sector[drive]);

			if(!(m_s.main_status_reg & FD_MS_NDMA)) {
				g_dma.set_DRQ(FLOPPY_DMA_CHAN, 0);
			}
			enter_result_phase();
		} else { // more data to transfer
			uint32_t logical_sector, sector_time;

			// remember that not all floppies have two sides, multiply by m_s.head[drive]
			logical_sector =
					(m_s.cylinder[drive] * m_media[drive].heads * m_media[drive].sectors_per_track) +
					(m_s.head[drive] * m_media[drive].sectors_per_track) +
					(m_s.sector[drive] - 1);

			floppy_xfer(drive, logical_sector*512, m_s.floppy_buffer, 512, FROM_FLOPPY);
			if(!(m_s.main_status_reg & FD_MS_NDMA)) {
				g_dma.set_DRQ(FLOPPY_DMA_CHAN, 0);
			}
			// time to read one sector at 300 rpm
			sector_time = 200000 / m_media[drive].sectors_per_track;
			g_machine.activate_timer(m_timer_index, sector_time , 0);
		}
	}
	return len;
}

uint16_t FloppyCtrl::dma_read(uint8_t *buffer, uint16_t maxlen)
{
	// A DMA read is from Memory to I/O
	// We need to write the data_byte which was already transfered from memory
	// via DMA to I/O (write block to floppy)
	//
	// maxlen is the length of the DMA transfer (not implemented yet)

	uint8_t drive = m_s.DOR & 0x03;
	uint32_t logical_sector, sector_time;

	g_sysboard.set_feedback();

	PDEBUGF(LOG_V2, LOG_FDC, "DMA read, drive %u\n", drive);

	if(m_s.pending_command == 0x4d) { // format track in progress
		PDEBUGF(LOG_V2, LOG_FDC, "DMA read: format in progress\n");
		m_s.format_count--;
		switch (3 - (m_s.format_count & 0x03)) {
			case 0:
				m_s.cylinder[drive] = *buffer;
				break;
			case 1:
				if(*buffer != m_s.head[drive]) {
					PDEBUGF(LOG_V0, LOG_FDC, "head number does not match head field\n");
				}
				break;
			case 2:
				m_s.sector[drive] = *buffer;
				break;
			case 3:
				if(*buffer != 2) {
					PDEBUGF(LOG_V0, LOG_FDC, "dma_read: sector size %d not supported\n", 128<<(*buffer));
				}
				PDEBUGF(LOG_V2, LOG_FDC, "formatting cylinder %u head %u sector %u\n",
						m_s.cylinder[drive], m_s.head[drive], m_s.sector[drive]);
				for(unsigned i = 0; i < 512; i++) {
					m_s.floppy_buffer[i] = m_s.format_fillbyte;
				}
				logical_sector =
						(m_s.cylinder[drive] * m_media[drive].heads * m_media[drive].sectors_per_track) +
						(m_s.head[drive] * m_media[drive].sectors_per_track) +
						(m_s.sector[drive] - 1);
				floppy_xfer(drive, logical_sector*512, m_s.floppy_buffer, 512, TO_FLOPPY);
				if(!(m_s.main_status_reg & FD_MS_NDMA)) {
					g_dma.set_DRQ(FLOPPY_DMA_CHAN, 0);
				}
				// time to write one sector at 300 rpm
				sector_time = 200000 / m_media[drive].sectors_per_track;
				g_machine.activate_timer(m_timer_index, sector_time , 0);
				break;
		}
		return 1;

	} else { // write normal data

		uint16_t len = 512 - m_s.floppy_buffer_index;
		if(len > maxlen) {
			len = maxlen;
		}
		memcpy(&m_s.floppy_buffer[m_s.floppy_buffer_index], buffer, len);
		m_s.floppy_buffer_index += len;
		m_s.TC = get_tc() && (len == maxlen);

		if((m_s.floppy_buffer_index >= 512) || (m_s.TC)) {
			logical_sector =
					(m_s.cylinder[drive] * m_media[drive].heads * m_media[drive].sectors_per_track) +
					(m_s.head[drive] * m_media[drive].sectors_per_track) +
					(m_s.sector[drive] - 1);
			if(m_media[drive].write_protected) {
				// write protected error
				PINFOF(LOG_V1, LOG_FDC, "tried to write disk %u, which is write-protected\n", drive);
				// ST0: IC1,0=01  (abnormal termination: started execution but failed)
				m_s.status_reg0 = 0x40 | (m_s.head[drive]<<2) | drive;
				// ST1: DataError=1, NDAT=1, NotWritable=1, NID=1
				m_s.status_reg1 = 0x27; // 0010 0111
				// ST2: CRCE=1, SERR=1, BCYL=1, NDAM=1.
				m_s.status_reg2 = 0x31; // 0011 0001
				enter_result_phase();
				return 1;
			}
			floppy_xfer(drive, logical_sector*512, m_s.floppy_buffer, 512, TO_FLOPPY);
			increment_sector(); // increment to next sector after writing current one
			m_s.floppy_buffer_index = 0;
			if(!(m_s.main_status_reg & FD_MS_NDMA)) {
				g_dma.set_DRQ(FLOPPY_DMA_CHAN, 0);
			}
			// time to write one sector at 300 rpm
			sector_time = 200000 / m_media[drive].sectors_per_track;
			g_machine.activate_timer(m_timer_index, sector_time , 0);
			// the following is a kludge; i (jc) don't know how to work with the timer
			if((m_s.main_status_reg & FD_MS_NDMA) && m_s.TC) {
				enter_result_phase();
			}
		}
		return len;
	}
}

void FloppyCtrl::raise_interrupt(void)
{
	  g_pic.raise_irq(6);
	  m_s.pending_irq = 1;
	  m_s.reset_sensei = 0;
}

void FloppyCtrl::lower_interrupt(void)
{
	if(m_s.pending_irq) {
		g_pic.lower_irq(6);
		m_s.pending_irq = 0;
	}
}

void FloppyCtrl::increment_sector(void)
{
	uint8_t drive;

	drive = m_s.DOR & 0x03;

	// values after completion of data xfer
	// ??? calculation depends on base_count being multiple of 512
	m_s.sector[drive]++;
	if((m_s.sector[drive] > m_s.eot[drive]) || (m_s.sector[drive] > m_media[drive].sectors_per_track)) {
		m_s.sector[drive] = 1;
		if(m_s.multi_track) {
			m_s.head[drive]++;
			if(m_s.head[drive] > 1) {
				m_s.head[drive] = 0;
				m_s.cylinder[drive]++;
				reset_changeline();
			}
		} else {
			m_s.cylinder[drive]++;
			reset_changeline();
		}
		if(m_s.cylinder[drive] >= m_media[drive].tracks) {
			// Set to 1 past last possible cylinder value.
			// I notice if I set it to tracks-1, prama linux won't boot.
			m_s.cylinder[drive] = m_media[drive].tracks;
			PDEBUGF(LOG_V1, LOG_FDC, "increment_sector: clamping cylinder to max\n");
		}
	}
}

void FloppyCtrl::eject_media(uint _drive)
{
	const char * drivename;
	if(_drive == 0) {
		drivename = DISK_A_SECTION;
	} else if(_drive == 1){
		drivename = DISK_B_SECTION;
	} else {
		PERRF(LOG_FDC, "only 2 drives supported\n");
		return;
	}

	m_media[_drive].close();

	if(m_media_present[_drive]) {
		m_s.DIR[_drive] |= 0x80; // disk changed line
		PINFOF(LOG_V1, LOG_FDC, "Floppy %s ejected\n",_drive==0?"A":"B");
	}
	m_media_present[_drive] = false;

	g_program.config().set_bool(drivename, DISK_INSERTED, false);
}

bool FloppyCtrl::insert_media(uint _drive, uint _mediatype, const char *_path, bool _write_protected)
{
	const char * drivename;
	if(_drive == 0) {
		drivename = DISK_A_SECTION;
	} else if(_drive == 1){
		drivename = DISK_B_SECTION;
	} else {
		PERRF(LOG_FDC, "only 2 drives supported\n");
		return false;
	}
	std::lock_guard<std::mutex> lock(m_lock);

	//If media file is already open, close it before reopening.
	eject_media(_drive);

	m_media[_drive].write_protected = _write_protected;
	if(!m_media[_drive].open(m_device_type[_drive], _mediatype, _path)) {
		PERRF(LOG_FDC, "unable to open media '%s'\n", _path);
		m_media_present[_drive] = false;
		m_disk_changed[_drive] = true;
		return false;
	}

	m_media_present[_drive] = true;

	PINFOF(LOG_V0, LOG_FDC, "Floppy %s: '%s' ro=%d, h=%d,t=%d,spt=%d\n",
			_drive==0?"A":"B",
			_path,
			m_media[_drive].write_protected,
			m_media[_drive].heads,
			m_media[_drive].tracks,
			m_media[_drive].sectors_per_track);

	g_program.config().set_bool(drivename, DISK_INSERTED, true);
	g_program.config().set_string(drivename, DISK_PATH, _path);
	g_program.config().set_bool(drivename, DISK_READONLY, _write_protected);
	g_program.config().set_string(drivename, DISK_TYPE, rev_disk_types[_mediatype]);

	m_disk_changed[_drive] = true;

	return true;
}

void FloppyCtrl::enter_result_phase(void)
{
	uint8_t drive;
	unsigned i;

	drive = m_s.DOR & 0x03;

	/* these are always the same */
	m_s.result_index = 0;
	// not necessary to clear any status bits, we're about to set them all
	m_s.main_status_reg |= FD_MS_MRQ | FD_MS_DIO | FD_MS_BUSY;

	/* invalid command */
	if((m_s.status_reg0 & 0xc0) == 0x80) {
		m_s.result_size = 1;
		m_s.result[0] = m_s.status_reg0;
		return;
	}

	switch (m_s.pending_command) {
		case 0x04: // get status
			m_s.result_size = 1;
			m_s.result[0] = m_s.status_reg3;
			break;
		case 0x08: // sense interrupt
			m_s.result_size = 2;
			m_s.result[0] = m_s.status_reg0;
			m_s.result[1] = m_s.cylinder[drive];
			break;
		case 0x0e: // dump registers
			m_s.result_size = 10;
			for(i = 0; i < 4; i++) {
				m_s.result[i] = m_s.cylinder[i];
			}
			m_s.result[4] = (m_s.SRT << 4) | m_s.HUT;
			m_s.result[5] = (m_s.HLT << 1) | ((m_s.main_status_reg & FD_MS_NDMA) ? 1 : 0);
			m_s.result[6] = m_s.eot[drive];
			m_s.result[7] = (m_s.lock << 7) | (m_s.perp_mode & 0x7f);
			m_s.result[8] = m_s.config;
			m_s.result[9] = m_s.pretrk;
			break;
		case 0x10: // version
			m_s.result_size = 1;
			m_s.result[0] = 0x90;
			break;
		case 0x14: // unlock
		case 0x94: // lock
			m_s.lock = (m_s.pending_command >> 7);
			m_s.result_size = 1;
			m_s.result[0] = (m_s.lock << 4);
			break;
		case 0x4a: // read ID
		case 0x4d: // format track
		case 0x46: // read normal data
		case 0x66:
		case 0xc6:
		case 0xe6:
		case 0x45: // write normal data
		case 0xc5:
			m_s.result_size = 7;
			m_s.result[0] = m_s.status_reg0;
			m_s.result[1] = m_s.status_reg1;
			m_s.result[2] = m_s.status_reg2;
			m_s.result[3] = m_s.cylinder[drive];
			m_s.result[4] = m_s.head[drive];
			m_s.result[5] = m_s.sector[drive];
			m_s.result[6] = 2; /* sector size code */
			raise_interrupt();
			break;
	}

	// Print command result (max. 10 bytes)
	char buf[8+(10*5)+1], *p = buf;
	p += sprintf(p, "RESULT: ");
	for(i=0; i<m_s.result_size; i++) {
		p += sprintf(p, "[%02x] ", (unsigned) m_s.result[i]);
	}
	PDEBUGF(LOG_V2, LOG_FDC, "%s\n", buf);
}

void FloppyCtrl::enter_idle_phase(void)
{
	m_s.main_status_reg &= (FD_MS_NDMA | 0x0f);  // leave drive status untouched
	m_s.main_status_reg |= FD_MS_MRQ; // data register ready

	m_s.command_complete = 1; /* waiting for new command */
	m_s.command_index = 0;
	m_s.command_size = 0;
	m_s.pending_command = 0;
	m_s.result_size = 0;

	m_s.floppy_buffer_index = 0;
}

uint32_t FloppyCtrl::calculate_step_delay(uint8_t drive, uint8_t new_cylinder)
{
	uint8_t steps;
	uint32_t one_step_delay;

	if(new_cylinder == m_s.cylinder[drive]) {
		steps = 1;
	} else {
		steps = abs(new_cylinder - m_s.cylinder[drive]);
		reset_changeline();
	}
	one_step_delay = ((m_s.SRT ^ 0x0f) + 1) * 500000 / drate_in_k[m_s.data_rate];
	return (steps * one_step_delay);
}

void FloppyCtrl::reset_changeline(void)
{
	uint8_t drive = m_s.DOR & 0x03;
	if(m_media_present[drive]) {
		m_s.DIR[drive] &= ~0x80;
	}
}

bool FloppyCtrl::get_tc(void)
{
	uint8_t drive;
	bool terminal_count;
	if(m_s.main_status_reg & FD_MS_NDMA) {
		drive = m_s.DOR & 0x03;
		/* figure out if we've sent all the data, in non-DMA mode...
		* the drive stays on the same cylinder for a read or write, so that'm_s
		* not going to be an issue. EOT stands for the last sector to be I/Od.
		* it does all the head 0 sectors first, then the second if any.
		* now, regarding reaching the end of the sector:
		*  == 512 would make it more precise, allowing one to spot bugs...
		*  >= 512 makes it more robust, but allows for sloppy code...
		*  pick your poison?
		* note: byte and head are 0-based; eot, sector, and heads are 1-based. */
		terminal_count = ((m_s.floppy_buffer_index == 512) &&
				(m_s.sector[drive] == m_s.eot[drive]) &&
				(m_s.head[drive] == (m_media[drive].heads - 1)));
	} else {
		terminal_count = g_dma.get_TC();
	}
	return terminal_count;
}


/*******************************************************************************
 * Floppy disk
 */

#ifdef O_BINARY
#define RDONLY O_RDONLY | O_BINARY
#define RDWR O_RDWR | O_BINARY
#else
#define RDONLY O_RDONLY
#define RDWR O_RDWR
#endif

bool FloppyDisk::open(uint8_t _devtype, uint8_t _type, const char *_path)
{
	struct stat stat_buf;
	int i, ret;
	int type_idx = -1;
#ifdef __linux__
	struct floppy_struct floppy_geom;
#endif
#ifdef _WIN32
	char sTemp[1024];
	bool raw_floppy = 0;
	HANDLE hFile;
	DWORD bytes;
	DISK_GEOMETRY dg;
	unsigned wtracks = 0, wheads = 0, wspt = 0;
#endif

	path = _path;

	// check media type
	if(_type == FLOPPY_NONE) {
		return false;
	}

	for(i = 0; i < 8; i++) {
		if(_type == floppy_type[i].id) {
			type_idx = i;
			break;
		}
	}
	if(type_idx == -1) {
		PERRF(LOG_FDC, "unknown media type %d\n", _type);
		return false;
	}
	uint mediacheck = floppy_type[type_idx].drive_mask & _devtype;
	if(!mediacheck) {
		PERRF(LOG_FDC, "media type %02Xh not valid for this floppy drive (%02Xh)\n",
				_type, floppy_type[type_idx].drive_mask);
		return 0;
	}

	// use virtual VFAT support if requested
	if(!strncmp(_path, "vvfat:", 6) && (_devtype == FDRIVE_350HD)) {
		vvfat = new VVFATMediaImage(1474560, "", !write_protected);
		if(vvfat != NULL) {
			if(vvfat->open(_path + 6) == 0) {
				type              = FLOPPY_1_44;
				tracks            = vvfat->geometry.cylinders;
				heads             = vvfat->geometry.heads;
				sectors_per_track = vvfat->geometry.spt;
				sectors           = 2880;
				vvfat_floppy      = true;
				fd                = 0;
			}
		}
		if(vvfat_floppy) {
			return true;
		}
	}

	// open media file (image file or device)

#ifdef _WIN32
	if((isalpha(_path[0])) && (_path[1] == ':') && (strlen(_path) == 2)) {
		raw_floppy = 1;
		wsprintf(sTemp, "\\\\.\\%s", _path);
		hFile = CreateFile(sTemp, GENERIC_READ, FILE_SHARE_WRITE, NULL,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile == INVALID_HANDLE_VALUE) {
			PDEBUGF(LOG_V0, LOG_FDC, "Cannot open floppy drive\n");
			return false;
		}
		if(!DeviceIoControl(hFile, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dg, sizeof(dg), &bytes, NULL)) {
			PDEBUGF(LOG_V0, LOG_FDC, "No media in floppy drive\n");
			CloseHandle(hFile);
			return false;
		} else {
			wtracks = (unsigned)dg.Cylinders.QuadPart;
			wheads  = (unsigned)dg.TracksPerCylinder;
			wspt    = (unsigned)dg.SectorsPerTrack;
		}
		CloseHandle(hFile);
		if(!write_protected)
			fd = ::open(sTemp, RDWR);
		else
			fd = ::open(sTemp, RDONLY);
	}
	else
#endif
	{
		if(!write_protected)
			fd = ::open(_path, RDWR);
		else
			fd = ::open(_path, RDONLY);
	}

	if(!write_protected && (fd < 0)) {
		PINFOF(LOG_V1, LOG_FDC, "tried to open '%s' read/write: %s\n", _path, strerror(errno));
		// try opening the file read-only
		write_protected = true;
#ifdef _WIN32
		if(raw_floppy == 1)
			fd = ::open(sTemp, RDONLY);
		else
#endif
			fd = ::open(_path, RDONLY);

		if(fd < 0) {
			// failed to open read-only too
			PINFOF(LOG_V1, LOG_FDC, "tried to open '%s' read only: %s\n", _path, strerror(errno));
			type = _type;
			return false;
		}
	}

#if defined(_WIN32)
	if(raw_floppy) {
		memset (&stat_buf, 0, sizeof(stat_buf));
		stat_buf.st_mode = S_IFCHR;
		ret = 0;
	}
	else
#endif
	{ // unix
		ret = fstat(fd, &stat_buf);
	}
	if(ret) {
		PERRF_ABORT(LOG_FDC, "fstat floppy 0 drive image file returns error: %s\n", strerror(errno));
		return false;
	}

	if(S_ISREG(stat_buf.st_mode)) {
		// regular file
		switch(_type) {
			// use CMOS reserved types
			case FLOPPY_160K: // 160K 5.25"
			case FLOPPY_180K: // 180K 5.25"
			case FLOPPY_320K: // 320K 5.25"
			// standard floppy types
			case FLOPPY_360K: // 360K 5.25"
			case FLOPPY_720K: // 720K 3.5"
			case FLOPPY_1_2: // 1.2M 5.25"
			case FLOPPY_2_88: // 2.88M 3.5"
				type              = _type;
				tracks            = floppy_type[type_idx].trk;
				heads             = floppy_type[type_idx].hd;
				sectors_per_track = floppy_type[type_idx].spt;
				sectors           = floppy_type[type_idx].sectors;
				if(stat_buf.st_size > (int)(sectors * 512)) {
					PDEBUGF(LOG_V0, LOG_FDC, "size of file '%s' (%lu) too large for selected type\n",
							_path, (unsigned long) stat_buf.st_size);
					return false;
				}
				break;
			default: // 1.44M 3.5"
				type              = _type;
				if(stat_buf.st_size <= 1474560) {
					tracks            = floppy_type[type_idx].trk;
					heads             = floppy_type[type_idx].hd;
					sectors_per_track = floppy_type[type_idx].spt;
				} else if(stat_buf.st_size == 1720320) {
					sectors_per_track = 21;
					tracks            = 80;
					heads             = 2;
				} else if(stat_buf.st_size == 1763328) {
					sectors_per_track = 21;
					tracks            = 82;
					heads             = 2;
				} else if(stat_buf.st_size == 1884160) {
					sectors_per_track = 23;
					tracks            = 80;
					heads             = 2;
				} else {
					PDEBUGF(LOG_V0, LOG_FDC, "file '%s' of unknown size %lu\n",
							_path, (unsigned long) stat_buf.st_size);
					return false;
				}
				sectors = heads * tracks * sectors_per_track;
				break;
		}
		return (sectors > 0); // success
	}

	else if(S_ISCHR(stat_buf.st_mode)
#ifdef S_ISBLK
            || S_ISBLK(stat_buf.st_mode)
#endif
	) {
		// character or block device
		// assume media is formatted to typical geometry for drive
		type              = _type;
#ifdef __linux__
		if(ioctl(fd, FDGETPRM, &floppy_geom) < 0) {
			PWARNF(LOG_FDC, "cannot determine media geometry, trying to use defaults\n");
			tracks            = floppy_type[type_idx].trk;
			heads             = floppy_type[type_idx].hd;
			sectors_per_track = floppy_type[type_idx].spt;
			sectors           = floppy_type[type_idx].sectors;
			return (sectors > 0);
		}
		tracks            = floppy_geom.track;
		heads             = floppy_geom.head;
		sectors_per_track = floppy_geom.sect;
		sectors           = floppy_geom.size;
#elif defined(_WIN32)
		tracks            = wtracks;
		heads             = wheads;
		sectors_per_track = wspt;
		sectors = heads * tracks * sectors_per_track;
#else
		tracks            = floppy_type[type_idx].trk;
		heads             = floppy_type[type_idx].hd;
		sectors_per_track = floppy_type[type_idx].spt;
		sectors           = floppy_type[type_idx].sectors;
#endif
		return (sectors > 0); // success
	} else {
		// unknown file type
		PDEBUGF(LOG_V0, LOG_FDC, "unknown mode type\n");
		return false;
	}
}

void FloppyDisk::close()
{
	if(fd >= 0) {
		if(vvfat_floppy) {
			vvfat->close();
			delete vvfat;
			vvfat_floppy = 0;
		} else {
			::close(fd);
		}
		fd = -1;
	}
}

