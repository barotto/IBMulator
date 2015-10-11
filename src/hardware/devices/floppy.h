/*
 * 	Copyright (c) 2001-2012  The Bochs Project
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

#ifndef IBMULATOR_HW_FLOPPY_H
#define IBMULATOR_HW_FLOPPY_H

#include "hardware/iodevice.h"
#include "mediaimage.h"

class FloppyCtrl;
extern FloppyCtrl g_floppy;

#define FDD_NONE  0 // floppy not present
#define FDD_525DD 1 // 360K  5.25"
#define FDD_525HD 2 // 1.2M  5.25"
#define FDD_350DD 3 // 720K  3.5"
#define FDD_350HD 4 // 1.44M 3.5"
#define FDD_350ED 5 // 2.88M 3.5"

#define FLOPPY_NONE   10 // media not present
#define FLOPPY_1_2    11 // 1.2M  5.25"
#define FLOPPY_1_44   12 // 1.44M 3.5"
#define FLOPPY_2_88   13 // 2.88M 3.5"
#define FLOPPY_720K   14 // 720K  3.5"
#define FLOPPY_360K   15 // 360K  5.25"
#define FLOPPY_160K   16 // 160K  5.25"
#define FLOPPY_180K   17 // 180K  5.25"
#define FLOPPY_320K   18 // 320K  5.25"
#define FLOPPY_LAST   18 // last legal value of floppy type

#define FLOPPY_AUTO     19 // autodetect image size (not implemented)
#define FLOPPY_UNKNOWN  20 // image size doesn't match one of the types above

#define FROM_FLOPPY 10
#define TO_FLOPPY   11

struct FloppyDisk
{
	std::string path; /* the image file or mounted vvfat dir (for saving state) */
	int  fd;         /* file descriptor of floppy image file */
	uint sectors_per_track;    /* number of sectors/track */
	uint sectors;    /* number of formatted sectors on diskette */
	uint tracks;      /* number of tracks */
	uint heads;      /* number of heads */
	uint type;
	bool write_protected;
	bool vvfat_floppy;
	MediaImage *vvfat;

	bool open(uint8_t devtype, uint8_t type, const char *path);
	void close();
};

class FloppyCtrl : public IODevice
{

private:

	struct {

		uint8_t data_rate; //CCR
		bool noprec; //CCR

		uint8_t command[10]; /* largest command size ??? */
		uint8_t command_index;
		uint8_t command_size;
		bool    command_complete;
		uint8_t pending_command;

		bool    multi_track;
		bool    pending_irq;
		uint8_t reset_sensei;
		uint8_t format_count;
		uint8_t format_fillbyte;

		uint8_t result[10];
		uint8_t result_index;
		uint8_t result_size;

		uint8_t DOR; // Digital Ouput Register
		uint8_t TDR; // Tape Drive Register
		uint8_t cylinder[4]; // really only using 2 drives
		uint8_t cur_cylinder[4]; // to determine the TRK0 pin value
		bool direction[4]; // to determine the !DIR bit in regA
		uint8_t head[4];     // really only using 2 drives
		uint8_t sector[4];   // really only using 2 drives
		uint8_t eot[4];      // really only using 2 drives
		bool step[4]; // for status reg A, latched. is it drive dependent?
		bool wrdata[4]; // for status reg B, latched. is it drive dependent?
		bool rddata[4]; // for status reg B, latched. is it drive dependent?
		bool TC;          // Terminal Count status from DMA controller

		/* MAIN STATUS REGISTER (03F4)
		 * b7: MRQ: main request 1=data register ready     0=data register not ready
		 * b6: DIO: data input/output:
		 *     1=controller->CPU (ready for data read)
		 *     0=CPU->controller (ready for data write)
		 * b5: NDMA: non-DMA mode: 1=controller not in DMA modes
		 *                         0=controller in DMA mode
		 * b4: BUSY: instruction(device busy) 1=active 0=not active
		 * b3-0: ACTD, ACTC, ACTB, ACTA:
		 *       drive D,C,B,A in positioning mode 1=active 0=not active
		 */
		uint8_t main_status_reg;

		uint8_t status_reg0;
		uint8_t status_reg1;
		uint8_t status_reg2;
		uint8_t status_reg3;

		uint8_t floppy_buffer[512+2]; // 2 extra for good measure
		uint    floppy_buffer_index;
		uint8_t DIR[4]; // Digital Input Register:
					    // b7: 0=diskette is present and has not been changed
					    //     1=diskette missing or changed
		bool    lock;      // FDC lock status
		uint8_t SRT;       // step rate time
		uint8_t HUT;       // head unload time
		uint8_t HLT;       // head load time
		uint8_t config;    // configure byte #1
		uint8_t pretrk;    // precompensation track
		uint8_t perp_mode; // perpendicular mode

	} m_s;  // state information

	// drive field allows up to 4 drives, even though only 2 will ever be used.
	FloppyDisk m_media[4];
	bool m_media_present[4];
	uint8_t m_device_type[4];
	uint m_num_installed_floppies;
	int m_timer_index;
	bool m_disk_changed[4]; //!< used by the GUI to know when a disk has been changed
	std::mutex m_lock; //!< used for machine-GUI comm

	uint16_t dma_write(uint8_t *buffer, uint16_t maxlen);
	uint16_t dma_read(uint8_t *buffer, uint16_t maxlen);
	void floppy_command(void);
	void floppy_xfer(uint8_t drive, uint32_t offset, uint8_t *buffer, uint32_t bytes, uint8_t direction);
	void raise_interrupt(void);
	void lower_interrupt(void);
	void enter_idle_phase(void);
	void enter_result_phase(void);
	uint32_t calculate_step_delay(uint8_t drive, uint8_t new_cylinder);
	void reset_changeline(void);
	bool get_tc(void);
	void timer(void);
	void increment_sector(void);

	void floppy_drive_setup(uint drive);

public:

	FloppyCtrl();
	~FloppyCtrl();

	void init();
	void reset(uint type);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t address, uint io_len);
	void write(uint16_t address, uint16_t value, uint io_len);
	const char * get_name() { return "Floppy Controller"; }

	bool insert_media(uint _drive, uint _type, const char *_path, bool _write_protected);
	void eject_media(uint _drive);
	inline bool get_motor_enable(uint _drive) {
		return (m_device_type[_drive] != FDD_NONE) && (m_s.DOR & (1 << (_drive+4)));
	}
	inline bool is_media_present(uint _drive) {
		return m_media_present[_drive];
	}
	//this is not the DIR bit 7, this is used by the GUI
	inline bool get_disk_changed(uint _drive) {
		std::lock_guard<std::mutex> lock(m_lock);
		bool changed = m_disk_changed[_drive];
		m_disk_changed[_drive] = false;
		return changed;
	}
	inline uint get_current_drive() {
		return (m_s.DOR & 0x03);
	}
	inline uint8_t get_drive_type(uint _drive) {
		return m_device_type[_drive%4];
	}

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
