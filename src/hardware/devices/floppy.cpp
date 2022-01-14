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
#include "filesys.h"
#include "machine.h"
#include "dma.h"
#include "pic.h"
#include "hardware/iodevice.h"
#include "hardware/devices.h"
#include "hardware/devices/systemboard.h"
#include "floppy.h"
#include <cstring>
#include <functional>
#include <sstream>
#include <regex>
#include <iomanip>

IODEVICE_PORTS(FloppyCtrl) = {
	{ 0x03F0, 0x03F1, PORT_8BIT|PORT_R_ }, // Status Register A / B
	{ 0x03F2, 0x03F2, PORT_8BIT|PORT_RW }, // DOR
	{ 0x03F4, 0x03F4, PORT_8BIT|PORT_RW }, // MSR R / DSR W
	{ 0x03F5, 0x03F5, PORT_8BIT|PORT_RW }, // FIFO R/W
	{ 0x03F7, 0x03F7, PORT_8BIT|PORT_RW }  // DIR R / CCR W
};
#define FLOPPY_DMA_CHAN 2
#define FLOPPY_IRQ      6

#define FLOPPY_IMAGES_ARCHIVE "disk_images.zip"

enum FDCInterfaceRegisters {

	// Status Register A (SRA, Model30)
	FDC_SRA_INT_REQ   = 0x80,
	FDC_SRA_DRQ       = 0x40,
	FDC_SRA_STEP_FF   = 0x20,
	FDC_SRA_TRK0      = 0x10,
	FDC_SRA_NHDSEL    = 0x08,
	FDC_SRA_INDEX     = 0x04,
	FDC_SRA_WP        = 0x02,
	FDC_SRA_NDIR      = 0x01,

	// Status Register B (SRB, Model30)
	FDC_SRB_NDRV2     = 0x80,
	FDC_SRB_NDS1      = 0x40,
	FDC_SRB_NDS0      = 0x20,
	FDC_SRB_WRDATA_FF = 0x10,
	FDC_SRB_RDDATA_FF = 0x08,
	FDC_SRB_WE_FF     = 0x04,
	FDC_SRB_NDS3      = 0x02,
	FDC_SRB_NDS2      = 0x01,

	// Digital Output Register (DOR)
	FDC_DOR_MOTEN3    = 0x80,
	FDC_DOR_MOTEN2    = 0x40,
	FDC_DOR_MOTEN1    = 0x20,
	FDC_DOR_MOTEN0    = 0x10,
	FDC_DOR_NDMAGATE  = 0x08,
	FDC_DOR_NRESET    = 0x04,
	FDC_DOR_DRVSEL    = 0x03,

	// Main Status Register (MSR)
	FDC_MSR_RQM       = 0x80,
	FDC_MSR_DIO       = 0x40,
	FDC_MSR_NONDMA    = 0x20,
	FDC_MSR_CMDBUSY   = 0x10,
	FDC_MSR_DRV3BUSY  = 0x08,
	FDC_MSR_DRV2BUSY  = 0x04,
	FDC_MSR_DRV1BUSY  = 0x02,
	FDC_MSR_DRV0BUSY  = 0x01,

	// Datarate Select Register (DSR)
	FDC_DSR_SW_RESET  = 0x80,
	FDC_DSR_PWR_DOWN  = 0x40,
	FDC_DSR_PRECOMP   = 0x1c,
	FDC_DSR_DRATE_SEL = 0x03,

	// Digital Input Register (DIR)
	FDC_DIR_DSKCHG    = 0x80,
	FDC_DIR_NDMAGATE  = 0x08,
	FDC_DIR_NOPREC    = 0x04,
	FDC_DIR_DRATE_SEL = 0x03,

	// Configuration Control Register (CCR)
	FDC_CCR_NOPREC    = 0x04,
	FDC_CCR_DRATE_SEL = 0x03
};

enum FDCStatusRegisters {

	// Status Register 0
	FDC_ST0_IC = 0xC0, //IC Interrupt Code
		FDC_ST0_IC_NORMAL   = 0x00,
		FDC_ST0_IC_ABNORMAL = 0x40,
		FDC_ST0_IC_INVALID  = 0x80,
		FDC_ST0_IC_POLLING  = 0xC0,
	FDC_ST0_SE = 0x20, //SE Seek End
	FDC_ST0_EC = 0x10, //EC Equipment Check
	FDC_ST0_H  = 0x04, // H Head Address
	FDC_ST0_DS = 0x03, //DS Drive Select

	// Status Register 1
	FDC_ST1_EN = 0x80, // EN End of Cylinder
	FDC_ST1_DE = 0x20, // DE Data Error
	FDC_ST1_OR = 0x10, // OR Overrun/Underrun
	FDC_ST1_ND = 0x04, // ND No data
	FDC_ST1_NW = 0x02, // NW Not Writeable
	FDC_ST1_MA = 0x01, // MA Missing Address Mark

	// Status Register 2
	FDC_ST2_CM = 0x40, // CM Control Mark
	FDC_ST2_DD = 0x20, // DD Data Error in Data Field
	FDC_ST2_WC = 0x10, // Wrong Cylinder
	FDC_ST2_BC = 0x02, // BC Bad Cylinder
	FDC_ST2_MD = 0x01, // Missing Data Address Mark

	// Status Register 3
	FDC_ST3_WP = 0x40, // WP Write Protect
	FDC_ST3_T0 = 0x10, // T0 TRACK 0
	FDC_ST3_HD = 0x04, // HD Head Address
	FDC_ST3_DS = 0x03, // DS Drive Select
	FDC_ST3_BASE = 0x28 // Unused bits 3,5 always '1'
};

enum FDCConfig {
	FDC_CONF_POLL    = 0x10,
	FDC_CONF_EFIFO   = 0x20,
	FDC_CONF_EIS     = 0x40,
	FDC_CONF_FIFOTHR = 0x0f
};

enum FDCCommands {                      // parameters  code
	FDC_CMD_READ         = 0b00000110,  // MT  MFM SK  0 0 1 1 0
	FDC_CMD_READ_DEL     = 0b00001100,  // MT  MFM SK  0 1 1 0 0
	FDC_CMD_WRITE        = 0b00000101,  // MT  MFM 0   0 0 1 0 1
	FDC_CMD_WRITE_DEL    = 0b00001001,  // MT  MFM 0   0 1 0 0 1
	FDC_CMD_READ_TRACK   = 0b00000010,  // 0   MFM 0   0 0 0 1 0
	FDC_CMD_VERIFY       = 0b00010110,  // MT  MFM SK  1 0 1 1 0
	FDC_CMD_VERSION      = 0b00010000,  // 0   0   0   1 0 0 0 0
	FDC_CMD_FORMAT_TRACK = 0b00001101,  // 0   MFM 0   0 1 1 0 1
	FDC_CMD_SCAN_EQ      = 0b00010001,  // MT  MFM SK  1 0 0 0 1
	FDC_CMD_SCAN_LO_EQ   = 0b00011001,  // MT  MFM SK  1 1 0 0 1
	FDC_CMD_SCAN_HI_EQ   = 0b00011101,  // MT  MFM SK  1 1 1 0 1
	FDC_CMD_RECALIBRATE  = 0b00000111,  // 0   0   0   0 0 1 1 1
	FDC_CMD_SENSE_INT    = 0b00001000,  // 0   0   0   0 1 0 0 0
	FDC_CMD_SPECIFY      = 0b00000011,  // 0   0   0   0 0 0 1 1
	FDC_CMD_SENSE_DRIVE  = 0b00000100,  // 0   0   0   0 0 1 0 0
	FDC_CMD_CONFIGURE    = 0b00010011,  // 0   0   0   1 0 0 1 1
	FDC_CMD_SEEK         = 0b00001111,  // REL DIR 0   0 1 1 1 1
	FDC_CMD_DUMPREG      = 0b00001110,  // 0   0   0   0 1 1 1 0
	FDC_CMD_READ_ID      = 0b00001010,  // 0   MFM 0   0 1 0 1 0
	FDC_CMD_PERP_MODE    = 0b00010010,  // 0   0   0   1 0 0 1 0
	FDC_CMD_LOCK         = 0b00010100,  // LCK 0   0   1 0 1 0 0

	FDC_CMD_INVALID      = 0,
	FDC_CMD_RESET        = 0b00011111   // contrived
};

const std::map<unsigned,FloppyCtrl::CmdDef> FloppyCtrl::ms_cmd_list = {
	{ FDC_CMD_READ        , {FDC_CMD_READ        , 9, "read data",          &FloppyCtrl::cmd_read_data} },
	{ FDC_CMD_READ_DEL    , {FDC_CMD_READ_DEL    , 9, "read deleted data",  &FloppyCtrl::cmd_not_implemented} },
	{ FDC_CMD_WRITE       , {FDC_CMD_WRITE       , 9, "write data",         &FloppyCtrl::cmd_write_data} },
	{ FDC_CMD_WRITE_DEL   , {FDC_CMD_WRITE_DEL   , 9, "write deleted data", &FloppyCtrl::cmd_not_implemented} },
	{ FDC_CMD_READ_TRACK  , {FDC_CMD_READ_TRACK  , 9, "read track",         &FloppyCtrl::cmd_not_implemented} },
	{ FDC_CMD_VERIFY      , {FDC_CMD_VERIFY      , 9, "verify",             &FloppyCtrl::cmd_not_implemented} },
	{ FDC_CMD_VERSION     , {FDC_CMD_VERSION     , 1, "version",            &FloppyCtrl::cmd_version} },
	{ FDC_CMD_FORMAT_TRACK, {FDC_CMD_FORMAT_TRACK, 6, "format track",       &FloppyCtrl::cmd_format_track} },
	{ FDC_CMD_SCAN_EQ     , {FDC_CMD_SCAN_EQ     , 9, "scan equal",         &FloppyCtrl::cmd_not_implemented} },
	{ FDC_CMD_SCAN_LO_EQ  , {FDC_CMD_SCAN_LO_EQ  , 9, "scan low or equal",  &FloppyCtrl::cmd_not_implemented} },
	{ FDC_CMD_SCAN_HI_EQ  , {FDC_CMD_SCAN_HI_EQ  , 9, "scan high or equal", &FloppyCtrl::cmd_not_implemented} },
	{ FDC_CMD_RECALIBRATE , {FDC_CMD_RECALIBRATE , 2, "recalibrate",        &FloppyCtrl::cmd_recalibrate} },
	{ FDC_CMD_SENSE_INT   , {FDC_CMD_SENSE_INT   , 1, "sense interrupt",    &FloppyCtrl::cmd_sense_int} },
	{ FDC_CMD_SPECIFY     , {FDC_CMD_SPECIFY     , 3, "specify",            &FloppyCtrl::cmd_specify} },
	{ FDC_CMD_SENSE_DRIVE , {FDC_CMD_SENSE_DRIVE , 2, "sense drive status", &FloppyCtrl::cmd_sense_drive} },
	{ FDC_CMD_CONFIGURE   , {FDC_CMD_CONFIGURE   , 4, "configure",          &FloppyCtrl::cmd_configure} },
	{ FDC_CMD_SEEK        , {FDC_CMD_SEEK        , 3, "seek",               &FloppyCtrl::cmd_seek} },
	{ FDC_CMD_DUMPREG     , {FDC_CMD_DUMPREG     , 1, "dumpreg",            &FloppyCtrl::cmd_dumpreg} },
	{ FDC_CMD_READ_ID     , {FDC_CMD_READ_ID     , 2, "read ID",            &FloppyCtrl::cmd_read_id} },
	{ FDC_CMD_PERP_MODE   , {FDC_CMD_PERP_MODE   , 2, "perpendicular mode", &FloppyCtrl::cmd_perp_mode} },
	{ FDC_CMD_LOCK        , {FDC_CMD_LOCK        , 1, "lock/unlock",        &FloppyCtrl::cmd_lock} },
	{ FDC_CMD_INVALID     , {FDC_CMD_INVALID     , 1, "INVALID COMMAND",    &FloppyCtrl::cmd_invalid} }
};

#define FDC_ST_HDS(DRIVE) ((m_s.head[DRIVE]<<2) | DRIVE)
#define FDC_DOR_DRIVE(DRIVE) ((m_s.DOR & 0xFC) | DRIVE)

#define FROM_FLOPPY 10
#define TO_FLOPPY   11


typedef struct {
	unsigned id;
	uint8_t  trk;
	uint8_t  hd;
	uint8_t  spt;
	unsigned sectors;
	uint8_t  drive_mask;
	const char *str;
} floppy_type_t;

static floppy_type_t floppy_type[FLOPPY_TYPE_CNT] = {
	{ FLOPPY_NONE,  0, 0,  0,    0, 0x00, "none"  },
	{ FLOPPY_160K, 40, 1,  8,  320, 0x03, "160K"  },
	{ FLOPPY_180K, 40, 1,  9,  360, 0x03, "180K"  },
	{ FLOPPY_320K, 40, 2,  8,  640, 0x03, "320K"  },
	{ FLOPPY_360K, 40, 2,  9,  720, 0x03, "360K"  },
	{ FLOPPY_720K, 80, 2,  9, 1440, 0x1f, "720K"  },
	{ FLOPPY_1_2,  80, 2, 15, 2400, 0x02, "1.2M"  },
	{ FLOPPY_1_44, 80, 2, 18, 2880, 0x18, "1.44M" },
	{ FLOPPY_2_88, 80, 2, 36, 5760, 0x10, "2.88M" }
};

static uint16_t drate_in_k[4] = {
	500, 300, 250, 1000
};

static std::map<std::string, unsigned> drive_str_type = {
	{ "none", FDD_NONE  },
	{ "3.5",  FDD_350HD },
	{ "5.25", FDD_525HD }
};

static std::map<unsigned, std::string> drive_type_str = {
	{ FDD_NONE,  "none" },
	{ FDD_350HD, "3.5"  },
	{ FDD_525HD, "5.25" }
};

static std::map<std::string, unsigned> disk_types_350 = {
	{ "720K", FLOPPY_720K },
	{ "1.44M",FLOPPY_1_44 },
	{ "2.88M",FLOPPY_2_88 }
};

static std::map<std::string, unsigned> disk_types_525 = {
	{ "160K", FLOPPY_160K },
	{ "180K", FLOPPY_180K },
	{ "320K", FLOPPY_320K },
	{ "360K", FLOPPY_360K },
	{ "1.2M", FLOPPY_1_2  }
};

FloppyCtrl::FloppyCtrl(Devices *_dev)
: IODevice(_dev)
{
}

FloppyCtrl::~FloppyCtrl()
{
	for(int i = 0; i < 2; i++) {
		m_media[i].close();
	}
}

void FloppyCtrl::install()
{
	IODevice::install();

	memset(&m_s, 0, sizeof(m_s));

	using namespace std::placeholders;
	m_devices->dma()->register_8bit_channel(
			FLOPPY_DMA_CHAN,
			std::bind(&FloppyCtrl::dma_read, this, _1, _2),
			std::bind(&FloppyCtrl::dma_write, this, _1, _2),
			name());
	g_machine.register_irq(FLOPPY_IRQ, name());

	m_timer = g_machine.register_timer(
			std::bind(&FloppyCtrl::timer, this, _1),
			name()
	);

	for(unsigned i=0; i<4; i++) {
		m_media[i].type            = FLOPPY_NONE;
		m_media[i].spt             = 0;
		m_media[i].tracks          = 0;
		m_media[i].heads           = 0;
		m_media[i].sectors         = 0;
		m_media[i].fd              = -1;
		m_media[i].write_protected = false;
		m_media_present[i]         = false;
		m_device_type[i]           = FDD_NONE;
		m_disk_changed[i]          = false;
	}
	m_num_installed_floppies = 0;

	m_fx_enabled = g_program.config().get_bool(SOUNDFX_SECTION, SOUNDFX_ENABLED);
	if(m_fx_enabled) {
		m_fx[0].install("A");
		m_fx[1].install("B");
	}
}

void FloppyCtrl::remove()
{
	IODevice::remove();

	for(int i = 0; i < 2; i++) {
		m_media[i].close();
	}

	m_devices->dma()->unregister_channel(FLOPPY_DMA_CHAN);
	g_machine.unregister_irq(FLOPPY_IRQ, name());
	g_machine.unregister_timer(m_timer);

	if(m_fx_enabled) {
		m_fx[0].remove();
		m_fx[1].remove();
	}
}

void FloppyCtrl::config_changed()
{
	m_mode = static_cast<Mode>(g_program.config().get_enum(DRIVES_SECTION, DRIVES_FDC_MODE, {
		{ "at",       Mode::PC_AT },
		{ "pc",       Mode::PC_AT },
		{ "pc/at",    Mode::PC_AT },
		{ "model30",  Mode::MODEL_30 },
		{ "model 30", Mode::MODEL_30 }
	}, Mode::MODEL_30));

	const char *modestr;
	switch(m_mode) {
		case Mode::PC_AT: modestr = "PC/AT"; break;
		case Mode::MODEL_30: modestr = "Model 30"; break;
		default: assert(false); break;
	}
	PINFOF(LOG_V1, LOG_FDC, "Controller in %s mode\n", modestr);
	g_program.config().set_string(DRIVES_SECTION, DRIVES_FDC_MODE, modestr);

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

	//TODO drives should be setup in the install()
	floppy_drive_setup(0);
	floppy_drive_setup(1);

	m_latency_mult = g_program.config().get_real(DRIVES_SECTION, DRIVES_FDD_LAT);
	m_latency_mult = clamp(m_latency_mult,0.0,1.0);

	if(m_fx_enabled) {
		for(int i=0; i<2; i++) {
			m_fx[i].config_changed();
		}
	}
}

void FloppyCtrl::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_FDC, "saving state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void FloppyCtrl::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_FDC, "restoring state\n");

	StateHeader h;
	h.name = name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);

	if(m_fx_enabled) {
		for(int i=0; i<2; i++) {
			m_fx[i].reset();
			if(is_motor_spinning(i)) {
				m_fx[i].spin(true,false);
			} else {
				m_fx[i].spin(false,false);
			}
		}
	}
}

FloppyDriveType FloppyCtrl::config_drive_type(unsigned drive)
{
	assert(drive < 2);

	FloppyDriveType devtype;
	if(drive == 0) {
		try {
			devtype = (FloppyDriveType)g_program.config().get_enum_quiet(DRIVES_SECTION, DRIVES_FDD_A,
					drive_str_type);
		} catch(std::exception &e) {
			devtype = (FloppyDriveType)g_machine.model().floppy_a;
		}
	} else {
		try {
			devtype = (FloppyDriveType)g_program.config().get_enum_quiet(DRIVES_SECTION, DRIVES_FDD_B,
					drive_str_type);
		} catch(std::exception &e) {
			devtype = (FloppyDriveType)g_machine.model().floppy_b;
		}
	}
	return devtype;
}

FloppyDiskType FloppyCtrl::create_new_floppy_image(std::string _imgpath,
		FloppyDriveType _devtype, FloppyDiskType _disktype, bool _formatted)
{
	if(FileSys::file_exists(_imgpath.c_str())) {
		PERRF(LOG_FDC, "Floppy image file '%s' already exists\n", _imgpath.c_str());
		return FLOPPY_NONE;
	}

	if(_disktype == FLOPPY_NONE) {
		switch(_devtype) {
			case FDD_525DD: _disktype = FLOPPY_360K; break;
			case FDD_525HD: _disktype = FLOPPY_1_2;  break;
			case FDD_350DD: _disktype = FLOPPY_720K; break;
			case FDD_350HD: _disktype = FLOPPY_1_44; break;
			case FDD_350ED: _disktype = FLOPPY_2_88; break;
			default:
				return FLOPPY_NONE;
		}
	} else if(_devtype != FDD_NONE){
		if(!(floppy_type[_disktype].drive_mask & _devtype)) {
			PERRF(LOG_FDC, "Floppy drive incompatible with disk type '%s'\n", floppy_type[_disktype].str);
			return FLOPPY_NONE;
		}
	}

	PINFOF(LOG_V0, LOG_FDC, "Creating new image file '%s'...\n", _imgpath.c_str());

	if(_formatted) {
		std::string archive = g_program.config().get_file_path(FLOPPY_IMAGES_ARCHIVE, FILE_TYPE_ASSET);
		if(!FileSys::file_exists(archive.c_str())) {
			PERRF(LOG_FDC, "Cannot find the image file archive '%s'\n", FLOPPY_IMAGES_ARCHIVE);
			throw std::runtime_error("Missing data");
		}
		std::string imgname = std::regex_replace(floppy_type[_disktype].str,
				std::regex("[\\.]"), "_");
		imgname = "floppy-" + imgname + ".img";
		if(!FileSys::extract_file(archive.c_str(), imgname.c_str(), _imgpath.c_str())) {
			PERRF(LOG_FDC, "Cannot extract image file '%s'\n", imgname.c_str());
			throw std::runtime_error("Missing data");
		}
	} else {
		//create a 0-filled image
		FlatMediaImage image;
		image.create(_imgpath.c_str(), floppy_type[_disktype].sectors);
		PINFOF(LOG_V0, LOG_FDC, "The image is not pre-formatted: use FORMAT under DOS\n");
	}

	return _disktype;
}

void FloppyCtrl::floppy_drive_setup(unsigned drive)
{
	assert(drive < 2);

	const char *drivename, *section;
	if(drive == 0) {
		drivename = "A";
		section = DISK_A_SECTION;
	} else {
		drivename = "B";
		section = DISK_B_SECTION;
	}

	FloppyDriveType devtype = config_drive_type(drive);
	m_device_type[drive] = devtype;
	g_program.config().set_string(DRIVES_SECTION,
			drive==0?DRIVES_FDD_A:DRIVES_FDD_B, drive_type_str[devtype]);
	std::map<std::string, unsigned> *mediatypes = nullptr;
	if(devtype != FDD_NONE) {
		m_num_installed_floppies++;
		PINFOF(LOG_V0, LOG_FDC, "Installed floppy %s as %s\n",
				drivename, devtype==FDD_350HD?"3.5\" HD":"5.25\" HD");
		if(devtype == FDD_350HD) {
			mediatypes = &disk_types_350;
		} else if(devtype == FDD_525HD) {
			mediatypes = &disk_types_525;
		} else {
			throw std::exception();
		}
	} else {
		return;
	}

	std::string diskpath = g_program.config().find_media(section, DISK_PATH);
	if(!diskpath.empty() && g_program.config().get_bool(section, DISK_INSERTED)) {
		FloppyDiskType disktype = FLOPPY_NONE;
		std::string typestr = g_program.config().get_string(section, DISK_TYPE);
		if(FileSys::is_directory(diskpath.c_str())) {
			PERRF(LOG_FDC, "The floppy image can't be a directory\n");
			throw std::exception();
		}
		if(typestr == "auto") {
			uint64_t disksize = FileSys::get_file_size(diskpath.c_str());
			switch(disksize) {
				case 0:
					try {
						disktype = create_new_floppy_image(diskpath, devtype, FLOPPY_NONE);
					} catch(...) {}
					break;
				case 320*512:  disktype = FLOPPY_160K; break;
				case 360*512:  disktype = FLOPPY_180K; break;
				case 640*512:  disktype = FLOPPY_320K; break;
				case 720*512:  disktype = FLOPPY_360K; break;
				case 1440*512: disktype = FLOPPY_720K; break;
				case 2400*512: disktype = FLOPPY_1_2;  break;
				case 2880*512:
				case 3360*512:
				case 3444*512:
				case 3680*512:
					disktype = FLOPPY_1_44;
					break;
				case 5760*512: disktype = FLOPPY_2_88; break;
				default:
					PERRF(LOG_FDC, "The floppy image '%s' is of wrong size\n",
							diskpath.c_str());
					throw std::exception();
			}
		} else {
			try {
				disktype = (FloppyDiskType)g_program.config().get_enum(section, DISK_TYPE, *mediatypes);
			} catch(std::exception &e) {
				PERRF(LOG_FDC, "Floppy type '%s' not valid for current drive type\n",
					typestr.c_str());
				throw;
			}
			if(!FileSys::file_exists(diskpath.c_str())) {
				disktype = create_new_floppy_image(diskpath, devtype, disktype);
			}
		}
		if(disktype != FLOPPY_NONE) {
			insert_media(drive, disktype, diskpath.c_str(),
					g_program.config().get_bool(section, DISK_READONLY));
		}
	}
}

uint8_t FloppyCtrl::get_drate_for_media(uint8_t _drive)
{
	assert(_drive < 4);
	if(!m_media_present[_drive]) {
		return 2;
	}
	// There are two standardized bit rates, 250 kb/s and 500 kb/s. DD 5.25" and
	// all 3.5" drives spin at 300 rpm, 8" and HD 5.25" drives at 360 rpm. Then
	// IBM went clever and let their HD drive spin at 360 rpm all the time using
	// the non standard bit rate of 300 kb/s for DD. Foreign format drives must
	// of necessity circumvent the standard BIOS routines and may or may not
	// encounter trouble at the unusual speed.
	switch(m_media[_drive].type) {
		case FLOPPY_160K:
		case FLOPPY_180K:
		case FLOPPY_320K:
		case FLOPPY_360K:
			if(m_device_type[_drive]==FDD_525DD)
				return 2; // 250
			else // FDD_525HD
				return 1; // 300
		case FLOPPY_720K:
			return 2; // 250
		case FLOPPY_1_2:
		case FLOPPY_1_44:
			return 0; // 500
		case FLOPPY_2_88:
			return 3; // 1000
		case FLOPPY_NONE:
		default:
			return 2;
	}
}

void FloppyCtrl::reset(unsigned type)
{
	if(type == MACHINE_POWER_ON) {
		// DMA is enabled from start
		memset(&m_s, 0, sizeof(m_s));

		if(m_fx_enabled) {
			for(int i=0; i<2; i++) {
				m_fx[i].reset();
			}
		}
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

		// DIR and CCR affected only by hard reset
		for(int i=0; i<4; i++) {
			m_s.DIR[i] |= FDC_DIR_DSKCHG;
		}
		m_s.data_rate = 2; // 250 Kbps
		m_s.lock = false;
	}
	if(!m_s.lock) {
		m_s.config = FDC_CONF_EFIFO; // EFIFO=1 8272A compatible mode FIFO is disabled
		m_s.pretrk = 0;
	}
	m_s.perp_mode = 0;

	for(int i=0; i<4; i++) {
		m_s.cylinder[i]     = 0;
		m_s.head[i]         = 0;
		m_s.sector[i]       = 0;
		m_s.eot[i]          = 0;
		m_s.step[i]         = false;
		m_s.wrdata[i]       = false;
		m_s.rddata[i]       = false;
	}

	m_devices->pic()->lower_irq(FLOPPY_IRQ);
	if(!(m_s.main_status_reg & FDC_MSR_NONDMA)) {
		m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, false);
	}
	enter_idle_phase();
}

void FloppyCtrl::power_off()
{
	if(m_fx_enabled) {
		for(int i=0; i<2; i++) {
			if(is_motor_spinning(i)) {
				m_fx[i].spin(false, true);
			}
		}
	}
	m_s.DOR = 0;
}

uint16_t FloppyCtrl::read(uint16_t _address, unsigned)
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
			value |= (m_devices->dma()->get_DRQ(FLOPPY_DMA_CHAN) << 6);
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
			bool motor = m_s.DOR & (1<<(drive+4));
			if(motor) {
				if(m_mode == Mode::PC_AT) {
					value |= m_s.DIR[drive] & FDC_DIR_DSKCHG;
				} else {
					// Model30 mode
					// Bit 7 : !DSKCHG
					value |= !(bool(m_s.DIR[drive] & FDC_DIR_DSKCHG)) << 7;
					// Bit 3 : !DMAGATE (DOR)
					value |= m_s.DOR & FDC_DIR_NDMAGATE; //same position
					// Bit 2 : NOPREC (CCR)
					value |= (m_s.noprec << 2);
					// Bit 1-0 : DRATE SEL1-0 (CCR)
					value |= m_s.data_rate;
				}
				// The STEP bit is latched with the Step output going active
				// and is cleared with a read to the DIR register, Hardware or Software RESET
				m_s.step[drive] = false;
			}
			PDEBUGF(LOG_V2, LOG_FDC, "DIR  -> 0x%02X ", value);
			if(value & FDC_DIR_DSKCHG)   {PDEBUGF(LOG_V2, LOG_FDC, "DSKCHG ");}
			if(value & FDC_DIR_NDMAGATE) {PDEBUGF(LOG_V2, LOG_FDC, "!DMAGATE ");}
			if(value & FDC_DIR_NOPREC)   {PDEBUGF(LOG_V2, LOG_FDC, "NOPREC ");}
			PDEBUGF(LOG_V2, LOG_FDC, "DRATE=%02X\n", value & FDC_DIR_DRATE_SEL);
			break;
		}
		default:
			assert(false);
			return 0;
	}

	return value;
}

void FloppyCtrl::write(uint16_t _address, uint16_t _value, unsigned)
{
	PDEBUGF(LOG_V2, LOG_FDC, "write 0x%04X      ", _address);

	m_devices->sysboard()->set_feedback();

	switch (_address) {

		case 0x3F2: // Digital Output Register (DOR)
		{
			uint8_t normal_op  = _value & FDC_DOR_NRESET;
			uint8_t drive_sel  = _value & FDC_DOR_DRVSEL;
			uint8_t prev_normal_op = m_s.DOR & FDC_DOR_NRESET;
			bool was_spinning[2] = { is_motor_spinning(0), is_motor_spinning(1) };

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
			if(m_device_type[drive_sel] == FDD_NONE) {
				PDEBUGF(LOG_V0, LOG_FDC, "WARNING: non existing drive selected\n");
			}
			// TODO add motor on time delay (3.5=300ms, 5.25=500ms)
			if(m_fx_enabled) {
				for(int i=0; i<2; i++) {
					bool is_spinning = is_motor_spinning(i);
					if(is_spinning && !was_spinning[i]) {
						m_fx[i].spin(true, true);
					} else if(!is_spinning && was_spinning[i]) {
						m_fx[i].spin(false, true);
					}
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

void FloppyCtrl::enter_execution_phase()
{
	PDEBUGF(LOG_V1, LOG_FDC, "COMMAND: ");
	PDEBUGF(LOG_V2, LOG_FDC, "%s ", print_array(m_s.command,m_s.command_size).c_str());

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

bool FloppyCtrl::start_read_write_cmd()
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

	if(!is_motor_on(drive)) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: motor not on\n", cmd);
		return false; // Hang controller
	}

	if(m_device_type[drive] == FDD_NONE) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: bad drive #%d\n", cmd, drive);
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

	if(m_media_present[drive] == 0) {
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

	if(cylinder >= m_media[drive].tracks) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: norm r/w parms out of range: sec#%02xh cyl#%02xh eot#%02xh head#%02xh\n",
				cmd, sector, cylinder, eot, head);
		return false; // Hang controller ?
	}

	// This hack makes older versions of the Bochs BIOS work
	if(eot == 0) {
		// TODO should be hang or abnormal termination?
		eot = m_media[drive].spt;
	}
	m_s.direction[drive] = (m_s.cylinder[drive]>cylinder);
	m_s.cylinder[drive]  = cylinder;
	m_s.head[drive]      = head;
	m_s.sector[drive]    = sector;
	m_s.eot[drive]       = eot;

	if(sector > m_media[drive].spt || m_s.data_rate != get_drate_for_media(drive)) {
		if(sector > m_media[drive].spt) {
			PDEBUGF(LOG_V1, LOG_FDC, "%s: attempt to %s sector %u past last sector %u\n",
					cmd, cmd, sector, m_media[drive].spt);
		}
		m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
		m_s.status_reg1 = FDC_ST1_ND;
		m_s.status_reg2 = 0x00;
		enter_result_phase();
		return false;
	}

	if(cylinder != m_s.cur_cylinder[drive] && !(m_s.config & FDC_CONF_EIS)) {
		PDEBUGF(LOG_V1, LOG_FDC, "%s: cylinder request != current cylinder, EIS=0\n", cmd);
		m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
		m_s.status_reg1 = FDC_ST1_ND;
		m_s.status_reg2 = 0x00;
		enter_result_phase();
		return false;
	}

	return true;
}

void FloppyCtrl::cmd_read_data()
{
	if(!start_read_write_cmd()) {
		return;
	}

	uint8_t drive = m_s.command[1] & 0x03;

	uint32_t logical_sector = chs_to_lba(m_s.cylinder[drive], m_s.head[drive], m_s.sector[drive], drive);
	if(logical_sector >= m_media[drive].sectors) {
		PDEBUGF(LOG_V0, LOG_FDC, "read data: logical sector out of bounds!\n");
		return; // Hang controller
	}

	// DMA:
	// pre-fill buffer with data > timer > DRQ > fill buffer > timer > DRQ > ... > TC
	// non-DMA:
	// pre-fill buffer with data > timer > INT > fill buffer ... > TC

	m_s.rddata[drive] = true;

	floppy_xfer(drive, logical_sector*512, m_s.floppy_buffer, 512, FROM_FLOPPY);

	uint32_t sector_time = calculate_rw_delay(drive, true);
	g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);

	play_seek_sound(drive, m_s.cur_cylinder[drive], m_s.cylinder[drive]);
}

void FloppyCtrl::cmd_write_data()
{
	if(!start_read_write_cmd()) {
		return;
	}

	uint8_t drive = m_s.command[1] & 0x03;

	// DMA:
	//  DRQ > fill buffer > write image > timer > DRQ > ... > timer > TC
	// non-DMA:
	//  INT > fill buffer > write image > fill buffer > ... > TC

	m_s.wrdata[drive] = true;

	if(m_s.main_status_reg & FDC_MSR_NONDMA) {
		if(m_s.cur_cylinder[drive] != m_s.cylinder[drive]) {
			// do a seek first
			uint32_t sector_time = calculate_rw_delay(drive, true);
			g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);
		} else {
			// ready to receive data
			m_s.main_status_reg |= FDC_MSR_RQM;
			raise_interrupt();
		}
	} else {
		m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, true);
	}

	play_seek_sound(drive, m_s.cur_cylinder[drive], m_s.cylinder[drive]);
}

void FloppyCtrl::cmd_version()
{
	PDEBUGF(LOG_V1, LOG_FDC, "version\n");
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
	enter_result_phase();
}

void FloppyCtrl::cmd_format_track()
{
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.DOR = FDC_DOR_DRIVE(drive);

	bool motor_on = (m_s.DOR>>(drive+4)) & 0x01;
	if(!motor_on) {
		PERRF(LOG_FDC, "format track: motor not on\n");
		return; // Hang controller?
	}
	m_s.head[drive] = (m_s.command[1] >> 2) & 0x01;
	uint8_t sector_size = m_s.command[2]; //N
	m_s.format_count = m_s.command[3]; //SC
	m_s.format_fillbyte = m_s.command[5]; //D

	PDEBUGF(LOG_V1, LOG_FDC, "format track DRV%u, N=%u,SC=%u,GPL=%u,D=%02x\n", drive,
			sector_size, m_s.format_count, m_s.command[4], m_s.format_fillbyte);

	if(m_device_type[drive] == FDD_NONE) {
		PERRF(LOG_FDC, "format track: bad drive #%d\n", drive);
		return; // Hang controller?
	}
	if(sector_size != 0x02) { // 512 bytes
		PERRF(LOG_FDC, "format track: sector size %d not supported\n", 128<<sector_size);
		return; // Hang controller?
	}
	if(!m_media_present[drive]) {
		PDEBUGF(LOG_V0, LOG_FDC, "format track: attempt to format track with media not present\n");
		return; // Hang controller
	}
	if(m_media[drive].write_protected || m_s.format_count != m_media[drive].spt) {
		if(m_media[drive].write_protected) {
			PINFOF(LOG_V0, LOG_FDC, "Attempt to format with media write-protected\n");
		} else {
			// On real hardware, when you try to format a 720K floppy as 1.44M the drive will happily
			// do so regardless of the presence of the "format hole".
			PERRF(LOG_FDC, "Wrong floppy disk type! Specify the format in the DOS command line.\n");
			PDEBUGF(LOG_V0, LOG_FDC, "format track: %d sectors/track requested (%d expected)\n",
					m_s.format_count, m_media[drive].spt);
		}
		m_s.status_reg0 = FDC_ST0_IC_ABNORMAL | FDC_ST_HDS(drive);
		m_s.status_reg1 = FDC_ST1_DE | FDC_ST1_ND | FDC_ST1_NW | FDC_ST1_MA;
		m_s.status_reg2 = FDC_ST2_DD | FDC_ST2_WC | FDC_ST2_MD;
		enter_result_phase();
		return;
	}

	// 4 header bytes per sector are required
	m_s.format_count <<= 2;

	if(m_s.main_status_reg & FDC_MSR_NONDMA) {
		// ready to receive data
		m_s.main_status_reg |= FDC_MSR_RQM;
		raise_interrupt();
	} else {
		m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, true);
	}
}

void FloppyCtrl::cmd_recalibrate()
{
	uint8_t drive = (m_s.command[1] & 0x03);
	m_s.DOR = FDC_DOR_DRIVE(drive);

	PDEBUGF(LOG_V1, LOG_FDC, "recalibrate DRV%u (cur.C=%u)\n", drive, m_s.cur_cylinder[drive]);

	if(m_device_type[drive]!=FDD_NONE && m_s.boot_time[drive]==0) {
		if(m_fx_enabled) {
			m_fx[drive].boot(m_media_present[drive]);
		}
		m_s.boot_time[drive] = g_machine.get_virt_time_ns();
	}

	// command head to track 0
	// error condition noted in Status reg 0's equipment check bit
	// seek end bit set to 1 in Status reg 0 regardless of outcome
	// The last two are taken care of in timer().
	m_s.direction[drive] = (m_s.cylinder[drive]>0);
	m_s.cylinder[drive] = 0;
	// clear RQM and CMDBUSY, set drive busy
	//  during the execution phase the controller is in NON BUSY state.
	// DRV x BUSY
	//  These bits are set to ones when a drive is in the seek portion of
	//  a command, including seeks, and recalibrates.
	m_s.main_status_reg &= FDC_MSR_NONDMA;
	m_s.main_status_reg |= (1 << drive);

	uint32_t step_delay = calculate_step_delay(drive, m_s.cur_cylinder[drive], 0);
	PDEBUGF(LOG_V2, LOG_FDC, "step_delay: %u us\n", step_delay);
	g_machine.activate_timer(m_timer, uint64_t(step_delay)*1_us, false);

	if(m_s.boot_time[drive] + 500_ms < g_machine.get_virt_time_ns()) {
		play_seek_sound(drive, m_s.cur_cylinder[drive], 0);
	}
}

void FloppyCtrl::cmd_sense_int()
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

void FloppyCtrl::cmd_specify()
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

void FloppyCtrl::cmd_sense_drive()
{
	uint8_t drive = (m_s.command[1] & 0x03);

	PDEBUGF(LOG_V1, LOG_FDC, "get status DRV%u\n", drive);

	m_s.head[drive] = (m_s.command[1] >> 2) & 0x01;
	m_s.status_reg3 = FDC_ST3_BASE | FDC_ST_HDS(drive);
	if(m_media[drive].write_protected) {
		m_s.status_reg3 |= FDC_ST3_WP;
	}
	if((m_device_type[drive] != FDD_NONE) && (m_s.cur_cylinder[drive] == 0)) {
		// the head takes time to move to track0; this time is used to determine if 40 or 80 tracks
		// the value of cur_cylinder for the drive is set in the timer handler
		m_s.status_reg3 |= FDC_ST3_T0;
	}
	enter_result_phase();
}

void FloppyCtrl::cmd_configure()
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

void FloppyCtrl::cmd_seek()
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
			drive, m_s.cmd_rel()?"REL":"", cylinder, m_s.cur_cylinder[drive]);

	if(m_s.cmd_rel()) {
		cmd_not_implemented();
		return;
	}

	m_s.DOR = FDC_DOR_DRIVE(drive);

	// ??? should also check cylinder validity
	m_s.direction[drive] = (m_s.cylinder[drive]>cylinder);
	m_s.cylinder[drive] = cylinder;
	m_s.head[drive] = head;

	// clear RQM and CMDBUSY, set drive busy
	//  during the execution phase the controller is in NON BUSY state.
	// DRV x BUSY
	//  These bits are set to ones when a drive is in the seek portion of
	//  a command, including seeks, and recalibrates.
	m_s.main_status_reg &= FDC_MSR_NONDMA;
	m_s.main_status_reg |= (1 << drive);

	uint32_t step_delay = calculate_step_delay(drive, m_s.cylinder[drive], cylinder);
	PDEBUGF(LOG_V2, LOG_FDC, "step_delay: %u us\n", step_delay);
	g_machine.activate_timer(m_timer, uint64_t(step_delay)*1_us, false);

	if(m_s.boot_time[drive] + 500_ms < g_machine.get_virt_time_ns()) {
		play_seek_sound(drive, m_s.cur_cylinder[drive], cylinder);
	}
}

void FloppyCtrl::cmd_dumpreg()
{
	PDEBUGF(LOG_V1, LOG_FDC, "dump registers\n");
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
	enter_result_phase();
}

void FloppyCtrl::cmd_read_id()
{
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.head[drive] = (m_s.command[1] >> 2) & 0x01;
	m_s.DOR = FDC_DOR_DRIVE(drive);

	PDEBUGF(LOG_V1, LOG_FDC, "read ID DRV%u\n", drive);

	if(!is_motor_on(drive)) {
		PDEBUGF(LOG_V1, LOG_FDC, "read ID: motor not on\n");
		return; // Hang controller
	}
	if(m_device_type[drive] == FDD_NONE) {
		PDEBUGF(LOG_V1, LOG_FDC, "read ID: bad drive #%d\n", drive);
		return; // Hang controller
	}
	if(m_media_present[drive] == 0) {
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
	m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
	uint32_t sector_time = calculate_rw_delay(drive, true);
	g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);
}

void FloppyCtrl::cmd_perp_mode()
{
	m_s.perp_mode = m_s.command[1];
	PDEBUGF(LOG_V1, LOG_FDC, "perpendicular mode, config=0x%02X\n", m_s.perp_mode);
	enter_idle_phase();
}

void FloppyCtrl::cmd_lock()
{
	m_s.lock = m_s.cmd_lock();
	PDEBUGF(LOG_V1, LOG_FDC, "%slock status\n", !m_s.lock?"un":"");
	uint8_t drive = m_s.command[1] & 0x03;
	m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
	enter_result_phase();
}

void FloppyCtrl::cmd_not_implemented()
{
	PERRF(LOG_FDC, "Command 0x%02x not implemented\n", m_s.pending_command);
	m_s.status_reg0 = FDC_ST0_IC_INVALID;
	enter_result_phase();
}

void FloppyCtrl::cmd_invalid()
{
	PDEBUGF(LOG_V1, LOG_FDC, "INVALID command: 0x%02x\n", m_s.pending_command);
	m_s.status_reg0 = FDC_ST0_IC_INVALID;
	enter_result_phase();
}

void FloppyCtrl::floppy_xfer(uint8_t drive, uint32_t offset, uint8_t *buffer, uint32_t bytes, uint8_t direction)
{
	int ret = 0;

	if(m_device_type[drive] == FDD_NONE) {
		PERRF_ABORT(LOG_FDC, "floppy_xfer: bad drive #%d\n", drive);
	}

	PDEBUGF(LOG_V2, LOG_FDC, "floppy_xfer DRV%u: offset=%u, bytes=%u, direction=%s floppy\n",
			drive, offset, bytes, (direction==FROM_FLOPPY)? "from" : "to");

	ret = (int)lseek(m_media[drive].fd, offset, SEEK_SET);

	if(ret < 0) {
		//TODO return proper error code
		PERRF_ABORT(LOG_FDC, "could not perform lseek() to %d on floppy image file\n", offset);
		return;
	}

	if(direction == FROM_FLOPPY) {

		ret = ::read(m_media[drive].fd, buffer, bytes);

		if(ret < int(bytes)) {
			if(ret > 0) {
				//TODO return proper error code
				PERRF(LOG_FDC, "partial read() on floppy image returns %u/%u\n", ret, bytes);
				memset(buffer + ret, 0, bytes - ret);
			} else {
				//TODO return proper error code
				PERRF(LOG_FDC, "read() on floppy image returns 0\n");
				memset(buffer, 0, bytes);
			}
		}
	} else { // TO_FLOPPY
		if(m_media[drive].write_protected) {
			//TODO return proper error code
			PERRF_ABORT(LOG_FDC, "floppy_xfer(): media is write protected");
		}

		ret = ::write(m_media[drive].fd, buffer, bytes);

		if(ret < int(bytes)) {
			//TODO return proper error code
			PERRF_ABORT(LOG_FDC, "could not perform write() on floppy image file\n");
		}
	}
}

void FloppyCtrl::timer(uint64_t)
{
	uint8_t drive = current_drive();
	switch (m_s.cmd_code()) {
		case FDC_CMD_RECALIBRATE:
		{
			// TODO parallel RECALIBRATE operations may be done on up to 4 drives at once.
			m_s.status_reg0 = FDC_ST0_SE | drive;
			if(!is_motor_on(drive)) {
				m_s.status_reg0 |= FDC_ST0_IC_ABNORMAL | FDC_ST0_EC;
			} else {
				m_s.status_reg0 |= FDC_ST0_IC_NORMAL;
			}
			m_s.direction[drive] = false;
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
						drive, m_s.cylinder[drive], m_s.head[drive], m_s.sector[drive]);
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
								drive, m_s.cylinder[drive], m_s.head[drive], m_s.sector[drive]);
						enter_result_phase();
						return;
					}
				} else {
					// transfer next sector
					m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, true);
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
							drive, m_s.cylinder[drive], m_s.head[drive], m_s.sector[drive]);
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
				m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, true);
			}
			step_head();
			break;

		case FDC_CMD_FORMAT_TRACK:
			if((m_s.format_count == 0) || m_s.TC) {
				m_s.format_count = 0;
				m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
				enter_result_phase();
				return;
			} else {
				// transfer next sector
				if(m_s.main_status_reg & FDC_MSR_NONDMA) {
					m_s.main_status_reg |= FDC_MSR_RQM;
					raise_interrupt();
				} else {
					m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, true);
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

uint16_t FloppyCtrl::read_data(uint8_t *_buffer_to, uint16_t _maxlen, bool _dma)
{
	uint8_t drive = current_drive();
	uint16_t len = 512 - m_s.floppy_buffer_index;
	if(len > _maxlen) {
		len = _maxlen;
	}

	memcpy(_buffer_to, &m_s.floppy_buffer[m_s.floppy_buffer_index], len);

	m_s.floppy_buffer_index += len;
	m_s.TC = get_TC() && (len == _maxlen);

	if((m_s.floppy_buffer_index >= 512) || (m_s.TC)) {
		if(m_s.floppy_buffer_index >= 512) {
			increment_sector(); // increment to next sector before retrieving next one
		}
		if(m_s.TC) { // Terminal Count line, done
			m_s.status_reg0 = FDC_ST0_IC_NORMAL | FDC_ST_HDS(drive);
			m_s.status_reg1 = 0;
			m_s.status_reg2 = 0;

			PDEBUGF(LOG_V2, LOG_FDC, "<<READ DONE>> TC - DRV%u C=%u,H=%u,S=%u\n",
					drive, m_s.cylinder[drive], m_s.head[drive], m_s.sector[drive]);

			if(_dma) {
				m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, false);
			}
			enter_result_phase();
		} else {
			// more data to transfer
			floppy_xfer(drive, chs_to_lba(drive)*512, m_s.floppy_buffer, 512, FROM_FLOPPY);

			if(_dma) {
				m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, false);
			}
			uint32_t sector_time = calculate_rw_delay(drive, false);
			g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);
		}
	}
	return len;
}

uint16_t FloppyCtrl::write_data(uint8_t *_buffer_from, uint16_t _maxlen, bool _dma)
{
	uint8_t drive = current_drive();
	uint32_t sector_time;

	if(m_s.cmd_code() == FDC_CMD_FORMAT_TRACK) {

		m_s.format_count--;
		switch (3 - (m_s.format_count & 0x03)) {
			case 0:
				//TODO seek time should be considered and added to the sector_time below
				m_s.cylinder[drive] = *_buffer_from;
				break;
			case 1:
				if(*_buffer_from != m_s.head[drive]) {
					PDEBUGF(LOG_V0, LOG_FDC, "head number does not match head field\n");
				}
				break;
			case 2:
				m_s.sector[drive] = *_buffer_from;
				break;
			case 3:
				if(*_buffer_from != 2) {
					PDEBUGF(LOG_V0, LOG_FDC, "write_data: sector size %d not supported\n", 128<<(*_buffer_from));
				}
				PDEBUGF(LOG_V2, LOG_FDC, "formatting cylinder %u head %u sector %u\n",
						m_s.cylinder[drive], m_s.head[drive], m_s.sector[drive]);
				for(unsigned i = 0; i < 512; i++) {
					m_s.floppy_buffer[i] = m_s.format_fillbyte;
				}

				floppy_xfer(drive, chs_to_lba(drive)*512, m_s.floppy_buffer, 512, TO_FLOPPY);
				if(_dma) {
					m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, false);
				} else {
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
		m_s.TC = get_TC() && (len == _maxlen);

		if((m_s.floppy_buffer_index >= 512) || (m_s.TC)) {
			if(m_media[drive].write_protected) {
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

			floppy_xfer(drive, chs_to_lba(drive)*512, m_s.floppy_buffer, 512, TO_FLOPPY);

			sector_time = calculate_rw_delay(drive, false);
			if(m_s.floppy_buffer_index >= 512) {
				increment_sector(); // increment to next sector after writing current one
			}
			m_s.floppy_buffer_index = 0;
			if(_dma) {
				// wait until data transferred to disk
				m_devices->dma()->set_DRQ(FLOPPY_DMA_CHAN, false);
			}
			g_machine.activate_timer(m_timer, uint64_t(sector_time)*1_us, false);
		}
		return len;
	}
}

uint16_t FloppyCtrl::dma_write(uint8_t *buffer, uint16_t maxlen)
{
	// A DMA write is from I/O to Memory
	// We need to return the next data byte(s) from the floppy buffer
	// to be transfered via the DMA to memory. (read block from floppy)
	//
	// maxlen is the maximum length of the DMA transfer

	m_devices->sysboard()->set_feedback();

	PDEBUGF(LOG_V2, LOG_FDC, "DMA write DRV%u\n", current_drive());

	return read_data(buffer, maxlen, true);
}

uint16_t FloppyCtrl::dma_read(uint8_t *buffer, uint16_t maxlen)
{
	// A DMA read is from Memory to I/O
	// We need to write the data_byte which was already transfered from memory
	// via DMA to I/O (write block to floppy)
	//
	// maxlen is the length of the DMA transfer

	m_devices->sysboard()->set_feedback();

	PDEBUGF(LOG_V2, LOG_FDC, "DMA read DRV%u\n", current_drive());

	return write_data(buffer, maxlen, true);
}

void FloppyCtrl::raise_interrupt()
{
	if((m_s.DOR & FDC_DOR_NDMAGATE) && !m_s.pending_irq) {
		PDEBUGF(LOG_V2, LOG_FDC, "Raising IRQ %d\n", FLOPPY_IRQ);
		m_devices->pic()->raise_irq(FLOPPY_IRQ);
		m_s.pending_irq = true;
	}
	m_s.reset_sensei = 0;
}

void FloppyCtrl::lower_interrupt()
{
	if(m_s.pending_irq) {
		PDEBUGF(LOG_V2, LOG_FDC, "Lowering IRQ %d\n", FLOPPY_IRQ);
		m_devices->pic()->lower_irq(FLOPPY_IRQ);
		m_s.pending_irq = false;
	}
}

void FloppyCtrl::increment_sector()
{
	// this is the internal sector address update
	// current head position is m_s.cur_cylinder

	uint8_t drive = current_drive();

	// values after completion of data xfer
	// ??? calculation depends on base_count being multiple of 512
	m_s.sector[drive]++;
	if((m_s.sector[drive] > m_s.eot[drive]) || (m_s.sector[drive] > m_media[drive].spt)) {
		m_s.sector[drive] = 1;
		if(m_s.multi_track) {
			m_s.head[drive]++;
			if(m_s.head[drive] > 1) {
				m_s.head[drive] = 0;
				m_s.cylinder[drive]++;
			}
		} else {
			m_s.cylinder[drive]++;
		}
		if(m_s.cylinder[drive] >= m_media[drive].tracks) {
			// Set to 1 past last possible cylinder value.
			// I notice if I set it to tracks-1, prama linux won't boot.
			m_s.cylinder[drive] = m_media[drive].tracks;
			PDEBUGF(LOG_V1, LOG_FDC, "increment_sector: clamping cylinder to max\n");
		}
	}
}

void FloppyCtrl::play_seek_sound(uint8_t _drive, uint8_t _from_cyl, uint8_t _to_cyl)
{
	assert(_drive<2);
	
	if(!m_fx_enabled) {
		return;
	}
	
	if(is_motor_on(_drive)) {
		m_fx[_drive].seek(_from_cyl, _to_cyl, 80);
	} else {
		PDEBUGF(LOG_V1, LOG_AUDIO, "FDD seek: motor is off\n");
	}
}

void FloppyCtrl::eject_media(unsigned _drive)
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
	if(m_fx_enabled && is_motor_spinning(_drive)) {
		m_fx[_drive].spin(false,true);
	}
	m_media[_drive].close();

	if(m_media_present[_drive]) {
		m_s.DIR[_drive] |= FDC_DIR_DSKCHG;
		PINFOF(LOG_V1, LOG_FDC, "Floppy %s ejected\n",_drive==0?"A":"B");
	}
	m_media_present[_drive] = false;

	g_program.config().set_bool(drivename, DISK_INSERTED, false);
}

bool FloppyCtrl::insert_media(unsigned _drive, unsigned _mediatype, const char *_path, bool _write_protected)
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
	std::lock_guard<std::mutex> lock(m_mutex);

	// If media file is already open, close it before reopening.
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
			m_media[_drive].spt);

	g_program.config().set_bool(drivename, DISK_INSERTED, true);
	g_program.config().set_string(drivename, DISK_PATH, _path);
	g_program.config().set_bool(drivename, DISK_READONLY, _write_protected);
	g_program.config().set_string(drivename, DISK_TYPE, floppy_type[_mediatype].str);

	m_disk_changed[_drive] = true;
	if(m_fx_enabled) {
		m_fx[_drive].snatch();
	}

	return true;
}

void FloppyCtrl::enter_result_phase()
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
			m_s.result[1] = m_s.cur_cylinder[drive];
			break;
		case FDC_CMD_DUMPREG:
			m_s.result_size = 10;
			for(unsigned i = 0; i < 4; i++) {
				m_s.result[i] = m_s.cur_cylinder[i];
			}
			m_s.result[4] = (m_s.SRT << 4) | m_s.HUT;
			m_s.result[5] = (m_s.HLT << 1) | ((m_s.main_status_reg & FDC_MSR_NONDMA) ? 1 : 0);
			m_s.result[6] = m_s.eot[drive];
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
			m_s.result[3] = m_s.cylinder[drive];
			m_s.result[4] = m_s.head[drive];
			m_s.result[5] = m_s.sector[drive];
			m_s.result[6] = 2; // sector size code
			raise_int = true;
			break;
		default:
			assert(false); break;
		}
	}

	// exit execution phase
	m_s.pending_command = FDC_CMD_INVALID;

	PDEBUGF(LOG_V2, LOG_FDC, "RESULT: %s\n", print_array(m_s.result,m_s.result_size).c_str());

	if(raise_int) {
		raise_interrupt();
	}
}

void FloppyCtrl::enter_idle_phase()
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

unsigned FloppyCtrl::chs_to_lba(unsigned _d) const
{
	assert(_d<4);
	return chs_to_lba(m_s.cylinder[_d], m_s.head[_d], m_s.sector[_d], _d);
}

unsigned FloppyCtrl::chs_to_lba(unsigned _c, unsigned _h, unsigned _s, unsigned _d) const
{
	assert(_s>0);
	assert(_d<4);
	return (_c * m_media[_d].heads + _h ) * m_media[_d].spt + (_s-1);
}

uint32_t FloppyCtrl::calculate_step_delay(uint8_t _drive, int _c0, int _c1)
{
	assert(_drive<4);
	uint32_t one_step_delay = (16 - m_s.SRT) * (500000 / drate_in_k[m_s.data_rate]);

	if(!is_motor_on(_drive)) {
		return one_step_delay;
	}
	int steps;
	if(_c0 == _c1) {
		steps = 1;
	} else {
		steps = abs(_c1 - _c0);
	}

	const uint32_t settling_time = 15000;
	return (one_step_delay * steps) + settling_time;
}

uint32_t FloppyCtrl::calculate_rw_delay(uint8_t _drive, bool _latency)
{
	// returns microseconds

	assert(_drive < 4);
	uint32_t sector_time_us, max_latency_us;
	uint64_t now_us = g_machine.get_virt_time_us();

	if(m_device_type[_drive] == FDD_525HD) {
		max_latency_us = (60e6 / 360); // 1 min in us / rpm
	} else {
		max_latency_us = (60e6 / 300);
	}

	// us to read 1 sector
	sector_time_us = max_latency_us / m_media[_drive].spt;

	// Head Load Time
	uint32_t hlt = m_s.HLT;
	if(hlt == 0) {
		hlt = 128;
	}
	hlt *= 1000000 / drate_in_k[m_s.data_rate];

	if(m_s.last_hut[_drive][m_s.head[_drive]] < now_us) {
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
	m_s.last_hut[_drive][m_s.head[_drive]] = now_us + sector_time_us + hut;

	return sector_time_us;
}

void FloppyCtrl::step_head()
{
	uint8_t drive = current_drive();
	if(is_motor_on(drive) && m_s.cur_cylinder[drive] != m_s.cylinder[drive]) {
		m_s.step[drive] = true;
		m_s.cur_cylinder[drive] = m_s.cylinder[drive];
		if(m_media_present[drive]) {
			// to clear DSKCHG select the drive and pulse the STEP signal
			// to move the read/write head stepper motor
			m_s.DIR[drive] &= ~FDC_DIR_DSKCHG;
		}
	}
}

bool FloppyCtrl::get_TC()
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
		                 (m_s.sector[drive] == m_s.eot[drive]));
		if(m_s.multi_track) {
			terminal_count &= (m_s.head[drive] == (m_media[drive].heads - 1));
		}
	} else {
		terminal_count = m_devices->dma()->get_TC();
	}
	return terminal_count;
}

std::string FloppyCtrl::print_array(uint8_t *_data, unsigned _len)
{
	std::stringstream ss;
	ss << std::setfill('0');
	ss << "[";
	for(unsigned i=0; i<_len; i++) {
		ss << std::hex << std::setw(2) << int(_data[i]);
		if(i<_len-1) {
			ss << "|";
		}
	}
	ss << "]";
	return ss.str();
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

bool FloppyDisk::open(unsigned _devtype, unsigned _type, const char *_path)
{
	struct stat stat_buf;
	int ret;
	path = _path;

	if(_type == FLOPPY_NONE) {
		return false;
	}
	unsigned mediacheck = floppy_type[_type].drive_mask & _devtype;
	if(!mediacheck) {
		PERRF(LOG_FDC, "media type %s not valid for this floppy drive (%02Xh)\n",
				floppy_type[_type].str, floppy_type[_type].drive_mask);
		return false;
	}

	// open media file
	if(!write_protected) {
		fd = FileSys::open(_path, RDWR);
	} else {
		fd = FileSys::open(_path, RDONLY);
	}

	if(!write_protected && (fd < 0)) {
		PINFOF(LOG_V1, LOG_FDC, "tried to open '%s' read/write: %s\n", _path, strerror(errno));
		// try opening the file read-only
		write_protected = true;
		fd = FileSys::open(_path, RDONLY);
		if(fd < 0) {
			// failed to open read-only too
			PINFOF(LOG_V1, LOG_FDC, "tried to open '%s' read only: %s\n", _path, strerror(errno));
			type = _type;
			return false;
		}
	}

	ret = fstat(fd, &stat_buf);

	if(ret) {
		PERRF_ABORT(LOG_FDC, "fstat floppy 0 drive image file returns error: %s\n", strerror(errno));
		return false;
	}

	if(S_ISREG(stat_buf.st_mode)) {
		// regular file
		switch(_type) {
			case FLOPPY_160K: // 160K 5.25"
			case FLOPPY_180K: // 180K 5.25"
			case FLOPPY_320K: // 320K 5.25"
			case FLOPPY_360K: // 360K 5.25"
			case FLOPPY_720K: // 720K 3.5"
			case FLOPPY_1_2:  // 1.2M 5.25"
			case FLOPPY_2_88: // 2.88M 3.5"
				type    = _type;
				tracks  = floppy_type[_type].trk;
				heads   = floppy_type[_type].hd;
				spt     = floppy_type[_type].spt;
				sectors = floppy_type[_type].sectors;
				if(stat_buf.st_size > (int)(sectors * 512)) {
					PDEBUGF(LOG_V0, LOG_FDC, "size of file '%s' (%lu) too large for selected type\n",
							_path, (unsigned long) stat_buf.st_size);
					return false;
				}
				break;
			default: // 1.44M 3.5"
				type = _type;
				if(stat_buf.st_size <= 1474560) {
					tracks = floppy_type[_type].trk;
					heads  = floppy_type[_type].hd;
					spt    = floppy_type[_type].spt;
				} else if(stat_buf.st_size == 1720320) {
					spt    = 21;
					tracks = 80;
					heads  = 2;
				} else if(stat_buf.st_size == 1763328) {
					spt    = 21;
					tracks = 82;
					heads  = 2;
				} else if(stat_buf.st_size == 1884160) {
					spt    = 23;
					tracks = 80;
					heads  = 2;
				} else {
					PDEBUGF(LOG_V0, LOG_FDC, "file '%s' of unknown size %lu\n",
							_path, (unsigned long) stat_buf.st_size);
					return false;
				}
				sectors = heads * tracks * spt;
				break;
		}
		return (sectors > 0); // success
	}

	// unknown file type
	PDEBUGF(LOG_V0, LOG_FDC, "unknown mode type\n");
	return false;
}

void FloppyDisk::close()
{
	if(fd >= 0) {
		::close(fd);
		fd = -1;
	}
}
