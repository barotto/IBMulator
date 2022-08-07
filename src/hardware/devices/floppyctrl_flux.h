// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, Marco Bortolin

/*
 * Intel 82077AA Floppy Disk Controller.
 * Realistic, analog flux-based implementation.
 * Emulation code is from MAME (devices/machine/upd765.h and devices/machine/fdc_pll.h).
 */

#ifndef IBMULATOR_HW_FLOPPYCTRL_FLUX_H
#define IBMULATOR_HW_FLOPPYCTRL_FLUX_H

#include "floppyctrl.h"
#include "floppydrive.h"
#include "floppyfmt.h"

#define FDC_CMD_MASK 0x1f

class FloppyCtrl_Flux : public FloppyCtrl
{
private:
	struct PLL {
		int64_t ctime, period, min_period, max_period, period_adjust_base, phase_adjust;

		uint64_t write_start_time;
		uint64_t write_buffer[32];
		unsigned write_position;
		int freq_hist;

		void set_clock(uint64_t period);
		void reset(uint64_t when);
		void read_reset(uint64_t when);
		int get_next_bit(uint64_t &tm, FloppyDrive *floppy, uint64_t limit);
		int feed_read_data(uint64_t &tm, uint64_t edge, uint64_t limit);
		bool write_next_bit(bool bit, uint64_t &tm, FloppyDrive *floppy, uint64_t limit);
		void start_writing(uint64_t tm);
		void commit(FloppyDrive *floppy, uint64_t tm);
		void stop_writing(FloppyDrive *floppy, uint64_t tm);
	};

	struct State {
		uint8_t command[16];
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
		constexpr uint8_t cmd_drive() const { return command[1] & 3; }
		constexpr uint8_t cmd_head() const { return (command[1] & 4) >> 2; }

		uint8_t result[10];
		uint8_t result_index;
		uint8_t result_size;

		struct floppy_info {
			enum { IRQ_NONE, IRQ_POLLED, IRQ_SEEK, IRQ_DONE };
			int main_state;
			int sub_state;
			int dir;           // Direction of stepping
			int pulse_counter; // Index pulse counter (used in the SCAN_ID sub-phase)
			uint8_t pcn;       // Present cylinder number.
			uint8_t seek_c;    // Target cylinder of the current seek phase
			// according to MAME, each drive has its own st0 and irq trigger
			uint8_t st0;
			bool st0_filled;
			bool live;
			bool index;
			bool step;    // for status reg A, latched
			bool wrdata;  // for status reg B, latched
			bool rddata;  // for status reg B, latched
			uint64_t hut; // the time when the head was unloaded
		} flopi[4];

		struct live_info {
			enum { PT_NONE, PT_CRC_1, PT_CRC_2 };

			uint64_t tm;
			int state;
			int next_state;
			unsigned drive;
			uint16_t shift_reg;
			uint16_t crc;
			int bit_counter;
			int byte_counter;
			int previous_type;
			bool data_separator_phase;
			int data_bit_context;
			uint8_t data_reg;
			uint8_t idbuf[6];
			PLL pll;
		};

		live_info cur_live;
		live_info checkpoint_live;

		// configurations with more than 2 drives are untested
		uint8_t DOR;       // Digital Ouput Register
		uint8_t TDR;       // Tape Drive Register
		uint8_t data_rate; // CCR
		bool    noprec;    // CCR
		uint8_t C; // C cyl register
		uint8_t H; // H head register
		uint8_t R; // R sector register
		uint8_t EOT;
		bool TC; // Terminal Count line

		bool pending_irq;
		bool data_irq;
		bool other_irq;
		bool internal_drq;

		int  sector_size;
		bool scan_done;
		bool tc_done;

		int  fifo_pos;
		int  fifo_expected;
		int  fifo_to_push;
		int  fifo_popped;
		int  fifo_pushed;
		bool fifo_write; // fifo is used for a write operation
		uint8_t fifo[16];

		uint8_t main_status_reg;
		uint8_t st1;
		uint8_t st2;
		uint8_t st3;

		bool    lock;      // FDC lock status
		uint8_t SRT;       // step rate time
		uint8_t HUT;       // head unload time
		uint8_t HLT;       // head load time
		uint8_t config;    // configure byte #1
		uint8_t pretrk;    // precompensation track
		uint8_t perp_mode; // perpendicular mode

		uint64_t boot_time[4];

	} m_s;  // state information

	TimerID m_polling_timer = NULL_TIMER_ID;
	TimerID m_fdd_timers[MAX_DRIVES] = {NULL_TIMER_ID,NULL_TIMER_ID};
	uint64_t m_min_cmd_time_us = 0;

public:
	FloppyCtrl_Flux(Devices *_dev);
	~FloppyCtrl_Flux() {}

	FloppyDisk* create_floppy_disk(const FloppyDisk::Properties &_props) const {
		return new FloppyDisk(_props);
	}
	bool can_use_any_floppy() const { return true; }

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

	void fdd_index_pulse(uint8_t _drive, int _state);

protected:
	uint16_t dma_write(uint8_t *buffer, uint16_t maxlen, bool tx_tc);
	uint16_t dma_read(uint8_t *buffer, uint16_t maxlen, bool tx_tc);
	void tc_w(bool _tc);

	void check_irq();
	void raise_interrupt();
	void lower_interrupt();

	void enter_execution_phase();
	void enter_idle_phase();
	void enter_result_phase(unsigned _drive);

	uint32_t get_HUT_us();
	uint32_t get_HLT_us();
	uint32_t get_one_step_delay_time_us();
	uint32_t calculate_step_delay_us(uint8_t _drive, int _c1);
	uint32_t calculate_step_delay_us(uint8_t _drive, int _c0, int _c1) {
		return FloppyCtrl::calculate_step_delay_us(_drive, _c0, _c1);
	}
	uint32_t calculate_head_delay_us(uint8_t _drive);

	void timer_fdd(unsigned _drive, uint64_t);
	void timer_polling(uint64_t);

	struct CmdDef {
		unsigned code;
		unsigned size;
		const char *name;
		std::function<void(FloppyCtrl_Flux&)> fn;
	};
	static const std::map<unsigned,CmdDef> ms_cmd_list;

	unsigned st_hds_drv(unsigned _drive);

	bool start_read_write_cmd();
	void cmd_read_data();
	void cmd_write_data();
	void cmd_version();
	void cmd_format_track();
	void cmd_scan();
	void cmd_recalibrate();
	void cmd_sense_int();
	void cmd_specify();
	void cmd_sense_drive();
	void cmd_configure();
	void cmd_seek();
	void cmd_dumpreg();
	void cmd_read_id();
	void cmd_read_track();
	void cmd_perp_mode();
	void cmd_lock();
	void cmd_not_implemented();
	void cmd_invalid();

	enum IRQReason {
		IRQ_DATA, IRQ_OTHER, IRQ_NONE
	};
	void command_end(unsigned _drive = -1, IRQReason _irq = IRQ_NONE);

	void general_continue(unsigned _drive);
	void read_data_continue(uint8_t _drive);
	void write_data_continue(uint8_t _drive);
	void read_track_continue(uint8_t _drive);
	void format_track_continue(uint8_t _drive);
	void read_id_continue(uint8_t _drive);

	int calc_sector_size(uint8_t size);
	bool sector_matches(uint8_t _drive) const;
	bool increment_sector_regs(uint8_t _drive);

	void live_start(unsigned _drive, int state);
	void checkpoint();
	void rollback();
	void live_delay(int state);
	void live_sync();
	void live_abort();
	void live_run(uint64_t limit = TIME_NEVER);

	void fifo_push(uint8_t data, bool internal);
	uint8_t fifo_pop(bool internal);
	void fifo_expect(int size, bool write);

	void enable_transfer();
	void disable_transfer();

	bool read_one_bit(uint64_t limit);
	bool write_one_bit(uint64_t limit);
	void live_write_mfm(uint8_t mfm);
	void live_write_fm(uint8_t fm);
	void live_write_raw(uint16_t raw);

	enum {
		// General "doing nothing" state
		IDLE,

		// Main states
		RECALIBRATE,
		SEEK,
		READ_DATA,
		WRITE_DATA,
		READ_TRACK,
		FORMAT_TRACK,
		READ_ID,
		SCAN_DATA,

		// Sub-states
		COMMAND_DONE,

		RECALIBRATE_WAIT_DONE,

		SEEK_MOVE,
		SEEK_WAIT_STEP_SIGNAL_TIME,
		SEEK_WAIT_STEP_SIGNAL_TIME_DONE,
		SEEK_WAIT_STEP_TIME,
		SEEK_WAIT_STEP_TIME_DONE,
		SEEK_WAIT_DONE,
		SEEK_DONE,

		HEAD_LOAD,
		HEAD_LOAD_DONE,

		WAIT_INDEX,
		WAIT_INDEX_DONE,

		SCAN_ID,
		SCAN_ID_FAILED,

		SECTOR_READ,
		SECTOR_WRITTEN,
		TC_DONE,

		TRACK_DONE,

		// Live states
		// order must be preserved because states are changed using arithmetic ops,
		// see live_run()
		SEARCH_ADDRESS_MARK_HEADER,
		READ_HEADER_BLOCK_HEADER,
		READ_DATA_BLOCK_HEADER,
		READ_ID_BLOCK,
		SEARCH_ADDRESS_MARK_DATA,
		SEARCH_ADDRESS_MARK_DATA_FAILED,
		READ_SECTOR_DATA,
		READ_SECTOR_DATA_BYTE,
		SCAN_SECTOR_DATA_BYTE,

		WRITE_SECTOR_SKIP_GAP2,
		WRITE_SECTOR_SKIP_GAP2_BYTE,
		WRITE_SECTOR_DATA,
		WRITE_SECTOR_DATA_BYTE,

		WRITE_TRACK_PRE_SECTORS,
		WRITE_TRACK_PRE_SECTORS_BYTE,

		WRITE_TRACK_SECTOR,
		WRITE_TRACK_SECTOR_BYTE,

		WRITE_TRACK_POST_SECTORS,
		WRITE_TRACK_POST_SECTORS_BYTE
	};
};

#endif
