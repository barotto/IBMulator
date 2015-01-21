/*
 * 	Copyright (c) 2001-2014  The Bochs Project
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

#define MAX_MULTIPLE_SECTORS 16

typedef enum _sense {
      SENSE_NONE = 0, SENSE_NOT_READY = 2, SENSE_ILLEGAL_REQUEST = 5,
      SENSE_UNIT_ATTENTION = 6
} sense_t;

typedef enum _asc {
      ASC_ILLEGAL_OPCODE = 0x20,
      ASC_LOGICAL_BLOCK_OOR = 0x21,
      ASC_INV_FIELD_IN_CMD_PACKET = 0x24,
      ASC_MEDIUM_MAY_HAVE_CHANGED = 0x28,
      ASC_SAVING_PARAMETERS_NOT_SUPPORTED = 0x39,
      ASC_MEDIUM_NOT_PRESENT = 0x3a
} asc_t;


typedef struct {
  struct {
    bool busy;
    bool drive_ready;
    bool write_fault;
    bool seek_complete;
    bool drq;
    bool corrected_data;
    bool index_pulse;
    unsigned index_pulse_count;
    bool err;
  } status;
  uint8_t    error_register;
  uint8_t    head_no;
  union {
    uint8_t    sector_count;
    struct {
#ifdef BX_LITTLE_ENDIAN
      unsigned c_d : 1;
      unsigned i_o : 1;
      unsigned rel : 1;
      unsigned tag : 5;
#else  /* BX_BIG_ENDIAN */
      unsigned tag : 5;
      unsigned rel : 1;
      unsigned i_o : 1;
      unsigned c_d : 1;
#endif
    } interrupt_reason;
  };
  uint8_t    sector_no;
  union {
    uint16_t   cylinder_no;
    uint16_t   byte_count;
  };
  uint8_t    buffer[MAX_MULTIPLE_SECTORS*512 + 4];
  uint32_t   buffer_size;
  uint32_t   buffer_index;
  uint32_t   drq_index;
  uint8_t    current_command;
  uint8_t    multiple_sectors;
  bool  lba_mode;
  bool  packet_dma;
  uint8_t    mdma_mode;
  uint8_t    udma_mode;
  struct {
    bool reset;       // 0=normal, 1=reset controller
    bool disable_irq; // 0=allow irq, 1=disable irq
  } control;
  uint8_t    reset_in_progress;
  uint8_t    features;
  struct {
    uint8_t  feature;
    uint8_t  nsector;
    uint8_t  sector;
    uint8_t  lcyl;
    uint8_t  hcyl;
  } hob;
  uint32_t   num_sectors;
  bool  lba48;
} controller_t;

struct sense_info_t {
  sense_t sense_key;
  struct {
    uint8_t arr[4];
  } information;
  struct {
    uint8_t arr[4];
  } specific_inf;
  struct {
    uint8_t arr[3];
  } key_spec;
  uint8_t fruc;
  uint8_t asc;
  uint8_t ascq;
};

struct error_recovery_t {
  unsigned char data[8];

  error_recovery_t ();
};

uint16_t read_16bit(const uint8_t* buf);
uint32_t read_32bit(const uint8_t* buf);


typedef enum {
      XT_NONE,
      XT_DISK
} device_type_t;

class HardDrive : public IODevice
{

private:

	struct drive_t {

		device_type_t device_type;
		// 512 byte buffer for ID drive command
		// These words are stored in native word endian format, as
		// they are fetched and returned via a return(), so
		// there's no need to keep them in x86 endian format.
		uint16_t id_drive[256];
		bool identify_set;

		controller_t controller;

		sense_info_t sense;

		MediaImage* hdimage;

		int64_t curr_lsector;
		int64_t next_lsector;

		uint8_t model_no[41];
		int seek_timer_index;

	} drive;

	unsigned drive_select;

	uint16_t ioaddr1;
	uint16_t ioaddr2;
	uint8_t  irq;


	bool calculate_logical_address(uint8_t channel, int64_t *sector);
	void increment_address(uint8_t channel, int64_t *sector);
	void identify_drive(uint8_t channel);
	void command_aborted(uint8_t channel, unsigned command);
	void raise_interrupt(uint8_t channel);
	void init_mode_sense_single(uint8_t channel, const void* src, int size);
	void set_signature(uint8_t channel, uint8_t id);
	bool ide_read_sector(uint8_t channel, uint8_t *buffer, uint32_t buffer_size);
	bool ide_write_sector(uint8_t channel, uint8_t *buffer, uint32_t buffer_size);
	void start_seek(uint8_t channel);

public:

	HardDrive();
	~HardDrive();

	void init();
	void reset(unsigned type);
	uint16_t read(uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t value, unsigned io_len);
	const char *get_name() { return "Hard Drive"; }

	void seek_timer(void);

	void save_state(uint8_t * &buf_, size_t &size_);
	size_t restore_state(uint8_t * _buf, size_t _size);
};

#endif
