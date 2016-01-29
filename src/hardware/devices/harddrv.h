/*
 * Copyright (C) 2015, 2016  Marco Bortolin
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

#ifndef IBMULATOR_HW_HARDDRV_H
#define IBMULATOR_HW_HARDDRV_H

#include "hardware/iodevice.h"
#include "mediaimage.h"
#include "harddrvfx.h"
#include <memory>

class HardDrive;
extern HardDrive g_harddrv;

#define HDD_DATA_STACK_SIZE 518

struct HDDPerformance
{
	float    seek_max;   // Maximum seek time in milliseconds
	float    seek_trk;   // Track to track seek time in milliseconds
	unsigned rot_speed;  // Rotational speed in RPM
	unsigned interleave; // Interleave ratio
	float    overh_time; // Controller overhead time in milliseconds
};

class HardDrive : public IODevice
{
private:

	struct DataBuffer {
		uint8_t  stack[HDD_DATA_STACK_SIZE];
		unsigned ptr;
		unsigned size;
		inline bool is_used() {
			return (size != 0);
		}
		inline void clear() {
			size = 0;
			ptr = 0;
		}
	};

	struct State {
		uint8_t attch_ctrl_reg;   //Attachment Control Reg
		uint8_t attch_status_reg; //Attachment Status Reg
		uint8_t int_status_reg;   //Interrupt Status Register
		uint8_t attention_reg;    //Attention Register

		struct SSB {
			//Sense Summary Block
			/*
			The sense summary block contains the current status of the drive.
			The information in the summary block is updated after each
			command is completed, after an error, or before the block is
			transferred.
			*/
			/*
				   7   6   5   4   3   2   1   0
			ByteO  -R  SE  0   WF  CE  0   0   T0
			Byte1  EF  ET  AM  BT  WC  0   0   ID
			Byte2   0  RR  RG  DS   Hd Sel State
			Byte3  Cylinder Low
			Byte4  DS Cyl High 0    Hd Number
			Byte5  Sector Number
			Byte6  Sector Size (hex 02)
			Byte7  Hd Number       0   0  Cyl High
			Byte8  Cylinder Low
			Byte9  Number of Sectors Corrected
			Byte10 Number of Retries
			Byte11 Command Syndrome
			Byte12 Drive Type Identifier
			Byte13 Reserved
			*/
			//uint8_t bytes[14];
			bool valid;
			bool not_ready;    //NR
			bool seek_end;     //SE
			bool cylinder_err; //CE
			bool track_0;      //T0
			bool reset;        //RR
			unsigned present_head;
			unsigned present_cylinder;
			unsigned last_head;
			unsigned last_cylinder;
			unsigned last_sector;
			int command_syndrome;
			int drive_type;
			void copy_to(uint8_t *_dest);
			void clear();
		} ssb;

		struct CCB {
			//Command Control Block
			/*
			The system specifies the operation by sending the 6-byte command
			control block to the controller. It can be sent through a DMA or l/O
			operation.
			*/
			/*
				   7   6   5   4   3   2   1   0
			ByteO  Command Code    ND  AS  0   EC/P
			Byte1  Head Number     0   0   Cyl High
			Byte2  CyHnderLow
			Byte3  Sector Number
			Byte4  0   0   0   0   0   0   1   0
			Byte5  Number of Sectors
			*/
			bool valid;
			int command;
			bool no_data; //ND
			bool auto_seek; //AS
			union {
				bool park; //P
				bool ecc; //EC
			};
			unsigned head;
			unsigned cylinder;
			unsigned sector;
			unsigned num_sectors;
			int sect_cnt;
			void set(uint8_t* _data);
		} ccb;

		DataBuffer sect_buffer[2];

		unsigned cur_buffer;
		unsigned cur_head;
		unsigned cur_cylinder;
		unsigned cur_sector; //warning: sectors are 1-based
		unsigned prev_cylinder;
		bool eoc;
		int reset_phase;
		uint32_t time;
	} m_s;

	int m_cmd_timer;
	int m_dma_timer;
	int m_drive_type;
	double m_last_head_pos;
	uint64_t m_last_time;
	uint32_t m_sectors;
	uint32_t m_seek_avgspeed_us;
	uint32_t m_seek_overhead_us;
	uint32_t m_trk2trk_us;
	uint32_t m_trk_read_us; // time needed by the head to read an entire track
	uint32_t m_sec_read_us; // time needed by the head to read a sector from the surface
	uint32_t m_sec_xfer_us; // time needed to transfer a sector, taking interleave into account
	double m_sect_size;

	std::unique_ptr<MediaImage> m_disk;
	HDDPerformance m_disk_performance;
	std::string m_original_path;
	MediaGeometry m_original_geom;
	bool m_write_protect;
	bool m_save_on_close;
	bool m_tmp_disk;

	static const std::function<void(HardDrive&)> ms_cmd_funcs[0xF+1];
	static const MediaGeometry ms_hdd_types[45];
	static const std::map<uint, HDDPerformance> ms_hdd_performance;
	static const uint32_t ms_cmd_times[0xF+1];

	HardDriveFX m_fx;

	inline unsigned chs_to_lba(unsigned _c, unsigned _h, unsigned _s) const;
	inline void lba_to_chs(unsigned _lba, unsigned &_c, unsigned &_h, unsigned &_s) const;
	inline double pos_to_sect(double _head_pos);
	inline double sect_to_pos(double _hw_sector);
	inline int get_hw_sector_number(int _logical_sector);
	inline double get_head_position(double _start_sector, uint32_t _elapsed_time_us);
	inline double get_current_head_position();

	void get_profile(int _type_id, MediaGeometry &geom_, HDDPerformance &perf_);
	void mount(std::string _imgpath, MediaGeometry _geom, HDDPerformance _perf);
	void unmount();

	uint32_t get_seek_time(unsigned _c);
	uint32_t get_rotational_latency(double _start_hw_sector, unsigned _dest_log_sector);
	void activate_command_timer(uint32_t _exec_time, uint32_t _seek_time,
			uint32_t _rot_latency, uint32_t _xfer_time);
	void command_timer();
	void dma_timer();

	void attention_block();
	void exec_command();
	void raise_interrupt();
	void lower_interrupt();
	void fill_data_stack(unsigned _buf, unsigned _len);
	DataBuffer* get_read_data_buffer();
	DataBuffer* get_write_data_buffer();

	uint16_t dma_write(uint8_t *_buffer, uint16_t _maxlen);
	uint16_t dma_read(uint8_t *_buffer, uint16_t _maxlen);

	void increment_sector();
	void cylinder_error();
	bool seek(unsigned _c);
	void set_cur_sector(unsigned _h, unsigned _s);
	void read_sector(unsigned _c, unsigned _h, unsigned _s, unsigned _buf);
	void write_sector(unsigned _c, unsigned _h, unsigned _s, unsigned _buf);

	bool read_auto_seek();
	void exec_read_on_next_sector();
	void command_completed();
	uint16_t crc16_ccitt_false(uint8_t *_data, int _len);
	uint64_t ecc48_noswap(uint8_t *_data, int _len);
	void cmd_read_data();
	void cmd_read_check();
	void cmd_read_ext();
	void cmd_read_id();
	void cmd_recalibrate();
	void cmd_write_data();
	void cmd_write_vfy();
	void cmd_write_ext();
	void cmd_format_disk();
	void cmd_seek();
	void cmd_format_trk();
	void cmd_undefined();

public:

	HardDrive();
	~HardDrive();

	void init();
	void reset(unsigned type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);
	const char *get_name() { return "Hard Drive"; }

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	inline bool is_busy() {
		return m_s.attention_reg & 0x80;
	}
};

#endif

