/*
 * Copyright (C) 2001-2012  The Bochs Project
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

#ifndef IBMULATOR_HW_FLOPPYCTRL_H
#define IBMULATOR_HW_FLOPPYCTRL_H

#include "hardware/iodevice.h"
#include "floppydisk.h"
#include "floppyfx.h"


enum FloppyDriveType {
	FDD_NONE  = 0x00, // floppy not present
	FDD_525DD = 0x01, // 360K  5.25"
	FDD_525HD = 0x02, // 1.2M  5.25"
	FDD_350DD = 0x04, // 720K  3.5"
	FDD_350HD = 0x08, // 1.44M 3.5"
	FDD_350ED = 0x10  // 2.88M 3.5"
};

#define FDC_CMD_MASK 0x1f

class FloppyCtrl : public IODevice
{
	IODEVICE(FloppyCtrl, "Floppy Controller");

private:
	enum Mode {
		PC_AT, MODEL_30
	};
	struct {
		uint8_t command[10];
		uint8_t command_index;
		uint8_t command_size;
		bool    command_complete;

		uint8_t pending_command;
		constexpr uint8_t cmd_code() const { return pending_command & FDC_CMD_MASK; }
		constexpr bool cmd_mtrk() const { return pending_command & 0x80; }
		constexpr bool cmd_mfm()  const { return pending_command & 0x40; }
		constexpr bool cmd_skip() const { return pending_command & 0x20; }
		constexpr bool cmd_rel()  const { return pending_command & 0x80; }
		constexpr bool cmd_dir()  const { return pending_command & 0x40; }
		constexpr bool cmd_lock() const { return pending_command & 0x80; }

		bool    multi_track;
		bool    pending_irq;
		uint8_t reset_sensei;
		uint8_t format_count;
		uint8_t format_fillbyte;

		uint8_t result[10];
		uint8_t result_index;
		uint8_t result_size;

		// configurations with more than 2 drives are untested
		uint8_t DOR;       // Digital Ouput Register
		uint8_t DIR[4];    // Digital Input Register
		uint8_t TDR;       // Tape Drive Register
		uint8_t data_rate; // CCR
		bool    noprec;    // CCR
		uint8_t cylinder[4];
		uint8_t cur_cylinder[4]; // the current head position
		bool    direction[4];    // to determine the !DIR bit in regA
		uint8_t head[4];
		uint8_t sector[4];
		uint8_t eot[4];
		uint64_t last_hut[4][2];   // the time when a head was unloaded
		bool    step[4];   // for status reg A, latched. is it drive dependent?
		bool    wrdata[4]; // for status reg B, latched. is it drive dependent?
		bool    rddata[4]; // for status reg B, latched. is it drive dependent?
		bool    TC;        // Terminal Count status from DMA controller

		uint8_t main_status_reg;
		uint8_t status_reg0;
		uint8_t status_reg1;
		uint8_t status_reg2;
		uint8_t status_reg3;

		uint8_t  floppy_buffer[512+2]; // sector buffer (2 extra bytes for good measure?)
		unsigned floppy_buffer_index;

		bool    lock;      // FDC lock status
		uint8_t SRT;       // step rate time
		uint8_t HUT;       // head unload time
		uint8_t HLT;       // head load time
		uint8_t config;    // configure byte #1
		uint8_t pretrk;    // precompensation track
		uint8_t perp_mode; // perpendicular mode

		uint64_t boot_time[4];

	} m_s;  // state information

	Mode m_mode = Mode::MODEL_30;

	// configurations with more than 2 drives are untested
	FloppyDisk m_media[4];
	bool       m_media_present[4];
	uint8_t    m_device_type[4];
	unsigned   m_num_installed_floppies;
	double     m_latency_mult;

	int  m_timer;

	bool m_disk_changed[4]; // used by the GUI to know when a disk has been changed
	std::mutex m_mutex;     // for machine-GUI synchronization

	bool m_fx_enabled;
	FloppyFX m_fx[2];

public:
	FloppyCtrl(Devices *_dev);
	~FloppyCtrl();

	void install();
	void remove();
	void reset(unsigned type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);

	bool insert_media(unsigned _drive, unsigned _type, const char *_path, bool _write_protected);
	void eject_media(unsigned _drive);
	inline bool is_motor_on(unsigned _drive) const {
		return (m_device_type[_drive]!=0) && (m_s.DOR & (1 << (_drive+4)));
	}
	inline bool is_media_present(unsigned _drive) const {
		return m_media_present[_drive];
	}
	//this is not the DIR bit 7, this is used by the GUI
	inline bool has_disk_changed(unsigned _drive) {
		std::lock_guard<std::mutex> lock(m_mutex);
		bool changed = m_disk_changed[_drive];
		m_disk_changed[_drive] = false;
		return changed;
	}
	inline unsigned current_drive() const {
		return (m_s.DOR & 0x03);
	}
	inline uint8_t drive_type(unsigned _drive) const {
		return m_device_type[_drive%4];
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	static FloppyDriveType config_drive_type(unsigned drive);
	static FloppyDiskType create_new_floppy_image(std::string _imgpath,
			FloppyDriveType _devtype, FloppyDiskType _disktype, bool _formatted=true);

private:
	inline unsigned chs_to_lba(unsigned _d) const;
	inline unsigned chs_to_lba(unsigned _c, unsigned _h, unsigned _s, unsigned _d) const;
	uint16_t read_data(uint8_t *buffer, uint16_t maxlen, bool drq);
	uint16_t write_data(uint8_t *buffer, uint16_t maxlen, bool drq);
	uint16_t dma_write(uint8_t *buffer, uint16_t maxlen);
	uint16_t dma_read(uint8_t *buffer, uint16_t maxlen);
	void floppy_xfer(uint8_t drive, uint32_t offset, uint8_t *buffer, uint32_t bytes, uint8_t direction);
	void raise_interrupt();
	void lower_interrupt();
	void enter_execution_phase();
	void enter_idle_phase();
	void enter_result_phase();
	uint32_t calculate_step_delay(uint8_t _drive, int _c0, int _c1);
	uint32_t calculate_rw_delay(uint8_t _drive, bool _latency);
	void step_head();
	bool get_TC();
	void timer(uint64_t);
	void increment_sector();
	void play_seek_sound(uint8_t _drive, uint8_t _from_cyl, uint8_t _to_cyl);
	void floppy_drive_setup(unsigned drive);
	inline bool is_motor_spinning(unsigned _drive) const {
		return (is_motor_on(_drive) && is_media_present(_drive));
	}
	uint8_t get_drate_for_media(uint8_t _drive);

	struct CmdDef {
		unsigned code;
		unsigned size;
		const char *name;
		std::function<void(FloppyCtrl&)> fn;
	};
	static const std::map<unsigned,CmdDef> ms_cmd_list;

	bool start_read_write_cmd();
	void cmd_read_data();
	void cmd_write_data();
	void cmd_version();
	void cmd_format_track();
	void cmd_recalibrate();
	void cmd_sense_int();
	void cmd_specify();
	void cmd_sense_drive();
	void cmd_configure();
	void cmd_seek();
	void cmd_dumpreg();
	void cmd_read_id();
	void cmd_perp_mode();
	void cmd_lock();
	void cmd_not_implemented();
	void cmd_invalid();
};

#endif
