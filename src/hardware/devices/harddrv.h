/*
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

#ifndef IBMULATOR_HW_HDDRIVE_H
#define IBMULATOR_HW_HDDRIVE_H

#include "hardware/iodevice.h"
#include "mediaimage.h"
#include <memory>

class HardDrive;
extern HardDrive g_harddrv;

struct HDDPerformance
{
	float    seek_max;   // Maximum seek time in milliseconds
	float    seek_trk;   // Track to track seek time in milliseconds
	unsigned rot_speed;  // Rotational speed in RPM
	float    xfer_rate;  // Disk-to-buffer trasfer rate in Mbps
	unsigned interleave; // Interleave ratio
	float    exec_time;  // Time to execute a command in milliseconds (controller overhead)
};

class HardDrive : public IODevice
{
private:

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
			bool park; //P
			unsigned head;
			unsigned cylinder;
			unsigned sector;
			unsigned num_sectors;
			void set(uint8_t* _data);
		} ccb;

		uint8_t  data_stack[512];
		unsigned data_ptr;
		unsigned data_size;

		unsigned cur_head;
		unsigned cur_cylinder;
		unsigned cur_sector; //warning: sectors are 1-based
		bool eoc;
		int reset_phase;
		uint32_t time;
	} m_s;

	int m_cmd_timer;
	int m_dma_timer;
	int m_drive_type;
	uint32_t m_sectors;
	uint32_t m_trk2trk_us;
	uint32_t m_avg_rot_lat_us;
	uint32_t m_avg_trk_lat_us;
	uint32_t m_sec_xfer_us;
	uint32_t m_exec_time_us;

	std::unique_ptr<MediaImage> m_disk;
	std::string m_original_path;
	MediaGeometry m_original_geom;
	bool m_write_protect;
	bool m_save_on_close;
	bool m_tmp_disk;

	static const std::function<void(HardDrive&)> ms_cmd_funcs[0xF+1];
	static const MediaGeometry ms_hdd_types[45];
	static const std::map<uint, HDDPerformance> ms_hdd_performance;

	inline unsigned chs_to_lba(unsigned _c, unsigned _h, unsigned _s) const;
	inline void lba_to_chs(unsigned _lba, unsigned &_c, unsigned &_h, unsigned &_s) const;

	void get_profile(int _type_id, MediaGeometry &geom_, HDDPerformance &perf_);
	void mount(std::string _imgpath, MediaGeometry _geom, HDDPerformance _perf);
	void unmount();

	void cmd_timer();
	void dma_timer();
	void attention();
	void command();
	void raise_interrupt();
	void lower_interrupt();
	void fill_data_stack(uint8_t *_source, unsigned _len);

	uint16_t dma_write(uint8_t *_buffer, uint16_t _maxlen);
	uint16_t dma_read(uint8_t *_buffer, uint16_t _maxlen);

	void increment_sector();
	void cylinder_error();
	bool seek(unsigned _c);
	void set_cur_sector(unsigned _h, unsigned _s);
	void read_sector(unsigned _c, unsigned _h, unsigned _s);
	void write_sector(unsigned _c, unsigned _h, unsigned _s);
	uint32_t get_seek_time(unsigned _c);

	void read_data_cmd();
	void read_check_cmd();
	void read_ext_cmd();
	void read_id_cmd();
	void recalibrate_cmd();
	void write_data_cmd();
	void write_vfy_cmd();
	void write_ext_cmd();
	void format_disk_cmd();
	void seek_cmd();
	void format_trk_cmd();
	void undefined_cmd();

public:

	HardDrive();
	~HardDrive();

	void init();
	void reset(unsigned type);
	void config_changed();
	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);
	const char *get_name() { return "Hard Drive"; }

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	inline bool is_busy() { return m_s.attch_status_reg & 0x4; }
};

#endif

