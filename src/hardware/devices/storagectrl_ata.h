/*
 * Copyright (C) 2001-2015  The Bochs Project
 * Copyright (C) 2016-2024  Marco Bortolin
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

#ifndef IBMULATOR_HW_STORAGECTRL_ATA_H
#define IBMULATOR_HW_STORAGECTRL_ATA_H

#include "storagectrl.h"
#include "storagedev.h"
#include "hdd.h"
#include "cdrom_drive.h"

#define ATA_MAX_CHANNEL 1
#define ATA_MAX_MULTIPLE_SECTORS 16


class StorageCtrl_ATA : public StorageCtrl
{
	IODEVICE(StorageCtrl_ATA, "ATA Storage Controller")

private:


	struct Controller {
		struct {
			bool busy;
			bool drive_ready; // DRDY
			bool write_fault; // ATA: Write fault, ATAPI: DMA READY / DF (Drive Fault for PIO) 
			bool seek_complete; // DSC (Device Seek Complete)
			bool drq; // Data request
			bool corrected_data;
			bool index_pulse;
			uint64_t index_pulse_time;
			bool err;
		} status;
		uint8_t error_register;
		uint8_t head_no;
		union {
			uint8_t sector_count; // ATA
			struct {              // ATAPI
				unsigned c_d : 1; //  CoD, Command or Data, 0=data 1=cmd
				unsigned i_o : 1; //  IO, Direction for the Information transfer, 0=to device 1=to host
				unsigned rel : 1; //  RELEASE, Release ATA bus
				unsigned tag : 5; //  Reserved
			} interrupt_reason;
		};
		uint8_t sector_no;
		union {
			uint16_t cylinder_no; // ATA
			uint16_t byte_count;  // ATAPI number of bytes for each DRQ (PIO)
		};
		uint8_t  buffer[ATA_MAX_MULTIPLE_SECTORS*512 + 4];
		uint32_t buffer_size;
		uint32_t buffer_index;
		uint32_t drq_index;
		uint8_t  current_command;
		uint8_t  multiple_sectors; // number of sectors per data transfer block
		bool     lba_mode;
		bool     packet_dma;
		uint8_t  mdma_mode;
		uint8_t  udma_mode;
		struct {
			bool reset;
			bool disable_irq;
		} control;
		bool    reset_in_progress;
		uint8_t features;
		struct { // LBA48 High Order Byte
			uint8_t feature;
			uint8_t nsector;
			uint8_t sector;
			uint8_t lcyl;
			uint8_t hcyl;
		} hob;
		uint32_t num_sectors; // number of remaining sectors to read or write
		bool lba48;
		uint64_t look_ahead_time; // the moment in time the head has been positioned on the current cyl
	};

	// FIXME:
	// For each ATA channel we should have one controller
	struct Drive {
		uint16_t id_drive[256];
		bool identify_set;
		Controller controller;
		struct Sense { // ATAPI
			uint8_t sense_key; // Sense Key
			uint8_t information[4]; // Information
			uint8_t specific_inf[4]; // Command Specific Information
			uint8_t key_spec[3]; // Sense Key Specific
			uint8_t fruc; // Field Replaceable Unit Code
			uint8_t asc;  // Additional Sense Code
			uint8_t ascq; // Additional Sense Code Qualifier
		} sense;
		struct Atapi {
			uint8_t command; // current active command
			//int drq_bytes; // bytes to transfer per DRQ for current comand
			int drq_sectors; // sectors to trasnfer per DRQ for current Media Access command
			int bytes_total; // total bytes to transfer for current command (debug only)
			int bytes_remaining; // total bytes remaining for current command
			int sectors_total; // total sectors to transfer for current Media Access command
			int sectors_remaining; // sectors remaining for current Media Access command
			int sector_size; // sector size for current Media Access command
			uint64_t seek_completion_time;
			uint8_t error_recovery[8]; // Error Recovery Parameters (mode select / mode sense)
		} atapi;
		int64_t prev_cyl;
		int64_t curr_lba;
		int64_t next_lba;
		int status_changed;

		uint32_t atapi_check_seek_completion(uint64_t _time = 0);
	};

	struct Channel {
		Drive drives[2];
		unsigned drive_select;
		uint8_t irq;
		uint16_t ioaddr1;
		uint16_t ioaddr2;
	} m_channels[ATA_MAX_CHANNEL];

	std::unique_ptr<StorageDev> m_storage[ATA_MAX_CHANNEL][2];
	TimerID m_cmd_timers[ATA_MAX_CHANNEL][2];
	int m_devices_cnt = 0;

	std::atomic<bool> m_busy;

	typedef std::pair<const char*, std::function<uint32_t(StorageCtrl_ATA&, int, int)>>
			ata_command_fn;
	typedef std::pair<const char*, std::function<uint32_t(StorageCtrl_ATA&, int, int)>>
			atapi_command_fn;
	static const std::map<int, ata_command_fn> ms_ata_commands;
	static const std::map<int, atapi_command_fn> ms_atapi_commands;

public:
	StorageCtrl_ATA(Devices *_dev);
	~StorageCtrl_ATA();

	void install();
	void remove();
	void config_changed();
	void reset(unsigned _type);
	void power_off();
	uint16_t read(uint16_t _address, unsigned _len);
	void write(uint16_t _address, uint16_t _value, unsigned _len);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);

	int installed_devices() const { return m_devices_cnt; }
	StorageDev * get_device(int);
	bool is_busy() const {
		return m_busy;
	}

private:
	void reset_channel(int _ch);
	void raise_interrupt(int _ch);
	void lower_interrupt(int _ch);
	void identify_ata_device(int _ch);
	void identify_atapi_device(int _ch);
	void command_successful(int _ch, int _dev, bool _raise_int);
	void command_aborted(int _ch, uint8_t _cmd);

	void set_signature(int _ch, int _dev);

	int64_t calculate_logical_address(int _ch);
	int64_t increment_address(int _ch, int64_t &_lba_sector, uint8_t _amount);
	void ata_tx_sectors(int _ch, bool _write, uint8_t *_buffer, unsigned _len);
	void lba48_transform(Controller &_controller, bool _lba48);

	uint32_t ata_read_next_block(int _ch, uint32_t _seek_time);
	void ata_write_next_block(int _ch);

	uint32_t seek(int _ch, uint64_t _curr_time);
	uint32_t get_seek_time(int _ch, int64_t _c0, int64_t _c1, int64_t _cprev);
	void activate_command_timer(int _ch, uint32_t _exec_time);
	void command_timer(int _ch, int _device, uint64_t _time);

	void update_busy_status();

	uint32_t ata_cmd_calibrate_drive(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_read_sectors(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_read_verify_sectors(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_write_sectors(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_execute_device_diagnostic(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_initialize_drive_parameters(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_identify_device(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_set_features(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_set_multiple_mode(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_identify_packet_device(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_device_reset(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_send_packet(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_power_stubs(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_check_power_mode(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_seek(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_read_native_max_address(int _ch, uint8_t _cmd);
	uint32_t ata_cmd_not_implemented(int _ch, uint8_t _cmd);

	uint32_t atapi_cmd_inquiry(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_mode_select(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_mode_sense(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_pause_resume(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_play_audio(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_play_audio_msf(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_prevent_allow_medium_removal(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_read(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_read_cdrom_capacity(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_read_subchannel(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_read_toc(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_request_sense(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_seek(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_start_stop_unit(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_stop_play_scan(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_test_unit_ready(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_read_disc_info(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_get_event_status_notification(int _ch, uint8_t _cmd);
	uint32_t atapi_cmd_not_implemented(int _ch, uint8_t _cmd);

	void atapi_init_send(int _ch, uint8_t _cmd, int _req_len, int _alloc_len);
	bool atapi_init_receive(int _ch, int _tx_len);
	void atapi_ready_to_transfer(int _ch, unsigned _type, unsigned _direction, bool _interrupt = true);

	void atapi_update_head_pos(int _ch, bool _stop_audio);
	bool atapi_check_transitions(int _ch);
	bool atapi_access_drive(int _ch, bool _spin_up = false, bool _blocking = false,
			uint32_t *_time_to_ready = nullptr);
	void atapi_set_sense(int _ch, uint8_t _key, uint8_t _asc = 0, uint8_t _ascq = 0);
	void atapi_success(int _ch, bool _raise_int = true);
	void atapi_error(int _ch, bool _raise_int = true);
	uint32_t atapi_play_audio(int _ch, int64_t _start_lba, int64_t _end_lba, int _len);
	uint32_t atapi_read_next_block(int _ch, bool _rot_latency);
	uint32_t atapi_seek(int _ch);
	void atapi_init_mode_sense_single(int _ch, const void *_src, size_t _size);
	void atapi_mode_select(int _ch, uint16_t _param_len);
	
	Drive & drive(int _ch, int _dev) {
		return m_channels[_ch].drives[_dev];
	}
	Controller & ctrl(int _ch, int _dev) {
		return drive(_ch, _dev).controller;
	}

	bool is_hdd(int _ch, int _dev) {
		return drive_is_present(_ch,_dev) && m_storage[_ch][_dev]->category() == StorageDev::DEV_HDD;
	};
	bool is_cd(int _ch, int _dev) {
		return drive_is_present(_ch,_dev) && m_storage[_ch][_dev]->category() == StorageDev::DEV_CDROM;
	};

	HardDiskDrive * storage_hdd(int _ch, int _dev) {
		return dynamic_cast<HardDiskDrive*>(m_storage[_ch][_dev].get());
	}
	CdRomDrive * storage_cd(int _ch, int _dev) {
		return dynamic_cast<CdRomDrive*>(m_storage[_ch][_dev].get());
	}

	Drive & selected_drive(int _ch) {
		return drive(_ch, m_channels[_ch].drive_select);
	}
	Controller & selected_ctrl(int _ch) {
		return ctrl(_ch, m_channels[_ch].drive_select);
	}
	StorageDev & selected_storage(int _ch) {
		return *m_storage[_ch][m_channels[_ch].drive_select];
	}
	HardDiskDrive * selected_hdd(int _ch) {
		return storage_hdd(_ch, m_channels[_ch].drive_select);
	}
	CdRomDrive * selected_cd(int _ch) {
		return storage_cd(_ch, m_channels[_ch].drive_select);
	}
	TimerID & selected_timer(int _ch) {
		return m_cmd_timers[_ch][m_channels[_ch].drive_select];
	}

	bool drive_is_present(int _ch, int _dev) {
		return m_storage[_ch][_dev].get();
	}
	bool master_is_present(int _ch) {
		return drive_is_present(_ch, 0);
	}
	bool slave_is_present(int _ch) {
		return drive_is_present(_ch, 1);
	}
	bool any_is_present(int _ch) {
		return master_is_present(_ch) || slave_is_present(_ch);
	}

	bool master_is_selected(int _ch) {
		return m_channels[_ch].drive_select == 0;
	}
	bool slave_is_selected(int _ch) {
		return m_channels[_ch].drive_select == 1;
	}

	bool selected_is_present(int _ch) {
		return drive_is_present(_ch, m_channels[_ch].drive_select);
	}
	bool selected_is_hdd(int _ch) {
		return is_hdd(_ch, m_channels[_ch].drive_select);
	}
	bool selected_is_cd(int _ch) {
		return is_cd(_ch, m_channels[_ch].drive_select);
	}

	const char * selected_type_string(int _ch) {
		return ((selected_is_cd(_ch)) ? "CD-ROM" : "HDD");
	}

	const char * ata_cmd_string(int _cmd) {
		return ms_ata_commands.at(_cmd).first;
	}
	const char * atapi_cmd_string(int _cmd) {
		return ms_atapi_commands.at(_cmd).first;
	}

	//WARNING: not mt safe
	const char * device_string(int _ch, int _dev);
	const char * selected_string(int _ch);
};

#endif
