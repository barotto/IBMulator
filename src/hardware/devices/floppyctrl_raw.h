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

#ifndef IBMULATOR_HW_FLOPPYCTRL_RAW_H
#define IBMULATOR_HW_FLOPPYCTRL_RAW_H

#include "floppyctrl.h"
#include "floppydisk_raw.h"

/*
 * Intel 82077AA Floppy Disk Controller.
 * Basic raw sector-based implementation, only for standard IBM PC formatted
 * disk images (512 bytes sectors).
 */
class FloppyCtrl_Raw : public FloppyCtrl
{
private:
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
		uint8_t TDR;       // Tape Drive Register
		uint8_t data_rate; // CCR
		bool    noprec;    // CCR
		bool    TC;        // Terminal Count status from DMA controller
		struct {
			uint8_t  cylinder; // C register (per drive?)
			uint8_t  head;     // H register (per drive?)
			uint8_t  sector;   // R register (per drive?)
			uint8_t  eot;      // EOT register (per drive?)
			uint8_t  cur_cylinder; // the current head position
			bool     direction;    // to determine the !DIR bit in regA
			uint64_t last_hut; // the time when a head was unloaded
			bool     step;     // for status reg A, latched. is it drive dependent?
			bool     wrdata;   // for status reg B, latched. is it drive dependent?
			bool     rddata;   // for status reg B, latched. is it drive dependent?
		} flopi[4];

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

	} m_s;  // state information

	double m_latency_mult = 1.0;
	TimerID m_timer = NULL_TIMER_ID;

public:
	FloppyCtrl_Raw(Devices *_dev);
	~FloppyCtrl_Raw() {}

	FloppyDisk* create_floppy_disk(const FloppyDisk::Properties &_props) const {
		return new FloppyDisk_Raw(_props);
	}
	bool can_use_any_floppy() const { return false; }

	void install();
	void remove();
	void reset(unsigned type);
	void power_off();
	void config_changed();

	unsigned current_drive() const { return (m_s.DOR & 0x03); }

	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

protected:
	enum XferDir {
		FROM_FLOPPY = 10,
		TO_FLOPPY   = 11
	};
	void floppy_xfer(uint8_t drive, XferDir direction);

	uint16_t read_data(uint8_t *buffer, uint16_t maxlen, bool drq, bool _tc = false);
	uint16_t write_data(uint8_t *buffer, uint16_t maxlen, bool drq, bool _tc = false);

	uint16_t dma_write(uint8_t *buffer, uint16_t maxlen, bool _tc);
	uint16_t dma_read(uint8_t *buffer, uint16_t maxlen, bool _tc);

	void raise_interrupt();
	void lower_interrupt();

	void enter_execution_phase();
	void enter_idle_phase();
	void enter_result_phase();

	uint32_t get_one_step_delay_time_us();
	uint32_t calculate_step_delay_us(uint8_t _drive, int _c1);
	uint32_t calculate_step_delay_us(uint8_t _drive, int _c0, int _c1) {
		return FloppyCtrl::calculate_step_delay_us(_drive, _c0, _c1);
	}
	uint32_t calculate_rw_delay(uint8_t _drive, bool _latency);

	void step_head();
	bool get_TC(bool _dma_tc);
	void timer(uint64_t);
	void increment_sector();
	uint8_t get_drate_for_media(uint8_t _drive);

	struct CmdDef {
		unsigned code;
		unsigned size;
		const char *name;
		std::function<void(FloppyCtrl_Raw&)> fn;
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
