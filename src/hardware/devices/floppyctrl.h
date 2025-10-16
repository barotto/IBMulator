/*
 * Copyright (C) 2015-2024  Marco Bortolin
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

#ifndef IBMULATOR_HW_FLOPPYCTRL_H
#define IBMULATOR_HW_FLOPPYCTRL_H

#include "hardware/iodevice.h"
#include "floppydrive.h"
#include "floppyfmt.h"

#define FDC_CMD_MASK 0x1f

class FloppyCtrl : public IODevice
{
	IODEVICE(FloppyCtrl, "Floppy Controller");

public:
	enum FDCType {
		RAW  = 1, // raw sector-based emulation
		FLUX = 2  // flux-based emulation
	};

	// Creates a compatible FloppyDisk object, the caller will be the pointer's owner
	virtual FloppyDisk* create_floppy_disk(const FloppyDisk::Properties &_props) const = 0;
	std::vector<const char*> get_compatible_file_extensions();
	const std::vector<std::unique_ptr<FloppyFmt>> &get_compatible_formats() {
		return m_floppy_formats;
	}
	virtual bool can_use_any_floppy() const = 0;

	// configurations with more than 2 drives are untested
	enum {
		MAX_DRIVES = 2,
		DMA_CHAN = 2,
		IRQ_LINE = 6
	};
	static constexpr const char * IMAGES_ARCHIVE = "disk_images.zip";

protected:
	enum Mode {
		PC_AT, MODEL_30
	} m_mode = Mode::MODEL_30;
	std::unique_ptr<FloppyDrive> m_fdd[MAX_DRIVES];
	unsigned m_installed_fdds = 0;

public:
	FloppyCtrl(Devices *_dev) : IODevice(_dev) {}
	virtual ~FloppyCtrl() {}

	virtual void install();
	virtual void remove();
	virtual void config_changed();
	virtual unsigned current_drive() const { return 0; }

	bool insert_floppy(unsigned _drive, FloppyDisk *_floppy);
	FloppyDisk* eject_floppy(unsigned _drive, bool _remove);

	bool is_drive_present(unsigned _drive) const {
		return (_drive < MAX_DRIVES && m_fdd[_drive]);
	}
	bool is_motor_on(unsigned _drive) const {
		return (is_drive_present(_drive) && m_fdd[_drive]->is_motor_on());
	}
	bool is_disk_present(unsigned _drive) const {
		return (is_drive_present(_drive) && m_fdd[_drive]->is_disk_present());
	}
	std::string get_disk_path(unsigned _drive) const {
		return (is_drive_present(_drive)) ? m_fdd[_drive]->get_disk_path() : "";
	}
	bool is_motor_spinning(unsigned _drive) const {
		return (is_motor_on(_drive) && is_disk_present(_drive));
	}
	bool is_disk_dirty(unsigned _drive, bool _since_restore) const {
		return (is_drive_present(_drive) && m_fdd[_drive]->is_disk_dirty(_since_restore));
	}
	bool can_disk_be_committed(unsigned _drive) const {
		return (is_disk_present(_drive) && m_fdd[_drive]->can_disk_be_committed());
	}
	//this is not the DIR bit 7, this is used by the GUI
	bool has_disk_changed(unsigned _drive) const {
		return (is_drive_present(_drive) && m_fdd[_drive]->has_disk_changed());
	}
	inline FloppyDrive::Type drive_type(unsigned _drive) const {
		if(!is_drive_present(_drive)) return FloppyDrive::FDD_NONE;
		return m_fdd[_drive]->type();
	}

	static FloppyDrive::Type config_drive_type(unsigned drive);
	static FloppyDisk::StdType create_new_floppy_image(std::string _imgpath,
			FloppyDrive::Type _devtype, FloppyDisk::StdType _disktype, std::string _format_name);

	virtual void fdd_index_pulse(uint8_t _drive, int _state);

	void register_activity_cb(unsigned _drive, FloppyEvents::ActivityCbFn _cb) {
		if(is_drive_present(_drive)) {
			m_fdd[_drive]->register_activity_cb(_cb);
		}
	}

protected:
	std::vector<std::unique_ptr<FloppyFmt>> m_floppy_formats;

	void floppy_drive_setup(unsigned _drive);
	void floppy_drive_remove(unsigned _drive);

	uint32_t calculate_step_delay_us(uint8_t _drive, int _c0, int _c1);
	virtual uint32_t get_one_step_delay_time_us() = 0;

	static constexpr uint16_t drate_in_k[4] = {
		500, 300, 250, 1000
	};

	enum InterfaceRegisters {

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

	enum StatusRegisters {

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
		FDC_ST2_SH = 0x08, // Scan Equal Hit
		FDC_ST2_SN = 0x04, // Scan Not Satisfied
		FDC_ST2_BC = 0x02, // BC Bad Cylinder
		FDC_ST2_MD = 0x01, // Missing Data Address Mark

		// Status Register 3
		FDC_ST3_FT = 0x80, // FT Fault
		FDC_ST3_WP = 0x40, // WP Write Protect
		FDC_ST3_RY = 0x20, // RY Ready
		FDC_ST3_T0 = 0x10, // T0 TRACK 0
		FDC_ST3_TS = 0x08, // TS Two Side
		FDC_ST3_HD = 0x04, // HD Head Address
		FDC_ST3_DS = 0x03  // DS Drive Select
	};

	enum Config {
		FDC_CONF_POLL    = 0x10, // Polling Enabled
		FDC_CONF_EFIFO   = 0x20, // FIFO disabled
		FDC_CONF_EIS     = 0x40, // No Implied Seeks
		FDC_CONF_FIFOTHR = 0x0f  // FIFO threshold
	};

	enum Commands {                         // parameters  code
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
};

#endif
