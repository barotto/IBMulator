/*
 * Copyright (C) 2001-2015  The Bochs Project
 * Copyright (C) 2016-2022  Marco Bortolin
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
#include "cdrom.h"

#define ATA_MAX_CHANNEL 1
#define ATA_MAX_MULTIPLE_SECTORS 16


class StorageCtrl_ATA : public StorageCtrl
{
	IODEVICE(StorageCtrl_ATA, "ATA Storage Controller")

private:
	enum DeviceType
	{
		ATA_NONE,
		ATA_DISK,
		ATA_CDROM
	};

	struct CDROM
	{
		bool ready;
		bool locked;
		uint32_t max_lba;
		uint32_t curr_lba;
		uint32_t next_lba;
		int remaining_blocks;
		uint8_t error_recovery[8];

		CDROM();
	};

	struct Controller {
		struct {
			bool busy;
			bool drive_ready;
			bool write_fault;
			bool seek_complete;
			bool drq;
			bool corrected_data;
			bool index_pulse;
			uint64_t index_pulse_time;
			bool err;
		} status;
		uint8_t error_register;
		uint8_t head_no;
		union {
			uint8_t sector_count;
			struct {
				unsigned c_d : 1;
				unsigned i_o : 1;
				unsigned rel : 1;
				unsigned tag : 5;
			} interrupt_reason;
		};
		uint8_t sector_no;
		union {
			uint16_t cylinder_no;
			uint16_t byte_count;
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
		uint64_t look_ahead_time; // starting time of read buffer operation
	};

	// FIXME:
	// For each ATA channel we should have one controller
	struct Drive {
		DeviceType device_type;
		uint16_t id_drive[256];
		bool identify_set;
		Controller controller;
		CDROM cdrom;
		struct {
			uint8_t sense_key;
			uint8_t information[4];
			uint8_t specific_inf[4];
			uint8_t key_spec[3];
			uint8_t fruc;
			uint8_t asc;
			uint8_t ascq;
		} sense;
		struct {
			uint8_t command;
			int drq_bytes;
			int total_bytes_remaining;
		} atapi;
		int64_t prev_cyl;
		int64_t curr_lba;
		int64_t next_lba;
		bool status_changed;
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
	typedef std::pair<const char*, std::function<void(StorageCtrl_ATA&, int, int)>>
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
	void init_send_atapi_command(int _ch, uint8_t _cmd, int _req_len, int _alloc_len, bool _lazy = false);
	void ready_to_send_atapi(int _ch);
	void init_mode_sense_single(int _ch, const void *_src, size_t _size);
	void set_signature(int _ch, int _dev);
	bool set_cd_media_status(int _ch, int _dev, bool _inserted, bool _interrupt);

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

	void atapi_cmd_test_unit_ready(int _ch, uint8_t _cmd);
	void atapi_cmd_request_sense(int _ch, uint8_t _cmd);
	void atapi_cmd_start_stop_unit(int _ch, uint8_t _cmd);
	void atapi_cmd_mechanism_status(int _ch, uint8_t _cmd);
	void atapi_cmd_mode_sense(int _ch, uint8_t _cmd);
	void atapi_cmd_inquiry(int _ch, uint8_t _cmd);
	void atapi_cmd_read_cdrom_capacity(int _ch, uint8_t _cmd);
	void atapi_cmd_read_cd(int _ch, uint8_t _cmd);
	void atapi_cmd_read_toc(int _ch, uint8_t _cmd);
	void atapi_cmd_read(int _ch, uint8_t _cmd);
	void atapi_cmd_seek(int _ch, uint8_t _cmd);
	void atapi_cmd_prevent_allow_medium_removal(int _ch, uint8_t _cmd);
	void atapi_cmd_read_subchannel(int _ch, uint8_t _cmd);
	void atapi_cmd_read_disc_info(int _ch, uint8_t _cmd);
	void atapi_cmd_not_implemented(int _ch, uint8_t _cmd);

	void atapi_cmd_nop(Controller &_controller);
	void atapi_cmd_error(int _ch, uint8_t _sense_key, uint8_t _asc);

	inline Drive & drive(int _ch, int _dev) {
		return m_channels[_ch].drives[_dev];
	}
	inline Controller & ctrl(int _ch, int _dev) {
		return drive(_ch, _dev).controller;
	}

	inline bool is_hdd(int _ch, int _dev) {
		return drive(_ch, _dev).device_type == ATA_DISK;
	};
	inline bool is_cd(int _ch, int _dev) {
		return drive(_ch, _dev).device_type == ATA_CDROM;
	};

	inline HardDiskDrive * storage_hdd(int _ch, int _dev) {
		return dynamic_cast<HardDiskDrive*>(m_storage[_ch][_dev].get());
	}
	inline CDROMDrive * storage_cd(int _ch, int _dev) {
		return dynamic_cast<CDROMDrive*>(m_storage[_ch][_dev].get());
	}

	inline Drive & selected_drive(int _ch) {
		return drive(_ch, m_channels[_ch].drive_select);
	}
	inline Controller & selected_ctrl(int _ch) {
		return ctrl(_ch, m_channels[_ch].drive_select);
	}
	inline StorageDev & selected_storage(int _ch) {
		return *m_storage[_ch][m_channels[_ch].drive_select];
	}
	inline HardDiskDrive * selected_hdd(int _ch) {
		return storage_hdd(_ch, m_channels[_ch].drive_select);
	}
	inline CDROMDrive * selected_cd(int _ch) {
		return storage_cd(_ch, m_channels[_ch].drive_select);
	}
	inline TimerID & selected_timer(int _ch) {
		return m_cmd_timers[_ch][m_channels[_ch].drive_select];
	}

	inline bool drive_is_present(int _ch, int _dev) {
		return drive(_ch, _dev).device_type != ATA_NONE;
	}
	inline bool master_is_present(int _ch) {
		return drive_is_present(_ch, 0);
	}
	inline bool slave_is_present(int _ch) {
		return drive_is_present(_ch, 1);
	}
	inline bool any_is_present(int _ch) {
		return master_is_present(_ch) || slave_is_present(_ch);
	}

	inline bool master_is_selected(int _ch) {
		return m_channels[_ch].drive_select == 0;
	}
	inline bool slave_is_selected(int _ch) {
		return m_channels[_ch].drive_select == 1;
	}

	inline bool selected_is_present(int _ch) {
		return drive_is_present(_ch, m_channels[_ch].drive_select);
	}
	inline bool selected_is_hdd(int _ch) {
		return is_hdd(_ch, m_channels[_ch].drive_select);
	}
	inline bool selected_is_cd(int _ch) {
		return is_cd(_ch, m_channels[_ch].drive_select);
	}

	inline const char * selected_type_string(int _ch) {
		return ((selected_is_cd(_ch)) ? "CD-ROM" : "HDD");
	}

	inline const char * ata_cmd_string(int _cmd) {
		return ms_ata_commands.at(_cmd).first;
	}
	inline const char * atapi_cmd_string(int _cmd) {
		return ms_atapi_commands.at(_cmd).first;
	}

	//WARNING: not mt safe
	const char * device_string(int _ch, int _dev);
	const char * selected_string(int _ch);
};

#endif
