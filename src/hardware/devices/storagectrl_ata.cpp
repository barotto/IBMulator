/*
 * Copyright (C) 2001-2023  The Bochs Project
 * Copyright (C) 2017-2024  Marco Bortolin
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

/* ATA/ATAPI
 * Useful docs: AT Attachment with Packet Interface, working draft by T13 at
 * www.t13.org
 */

/* This is the Bochs' implementation with the following notable differences:
 * 1. DMA transfer modes removed (PIO only)
 * 2. realistic read/write/seek timings added, whith track look-ahead cache
 * 3. 16-bit data transfer only
 * 4. ata/atapi commands refactored
 *
 * DMA transfer modes to be reimplemented if/when Pentium-class emulation
 * will be added, along with 32-bit I/O.
 */

/* Notes about the Look-Ahead Cache
 * --------------------------------
 * Caching with look-ahead read decreases access time to sequential data in the
 * drive by temporarily placing small amounts of data in high speed memory.
 * Cache may contain from 1 to the size of one physical track. No head switching
 * or seeking occurs during look-ahead.
 *
 * The drive caches not only requested sectors, but "look-ahead" and caches all
 * remaining physical sectors on the current track. The "look-ahead" feature
 * prepares the drive to transfer cached data when the host request it
 * (preventing access time delays).
 *
 * On a real drive, if the drive is performing a look-ahead cache when a command
 * is received, it will stop caching and process the command so that look-ahead
 * caching does not add overhead to command processing.
 * In this implementation the look-ahead will not be interrupted, resulting in a
 * slightly faster caching. This should not be easily noticeable, however.
 *
 * Resets and Write commands to any of the cached sectors invalidate the whole
 * cache.
 *
 * The cache is only simulated through the comparison of timestamps and no real
 * disk image buffering is performed.
 */

#include "ibmulator.h"
#include "storagectrl_ata.h"
#include "hddparams.h"
#include "program.h"
#include "machine.h"
#include "pic.h"
#include <functional>

IODEVICE_PORTS(StorageCtrl_ATA) = {
	{ 0x01F0, 0x01F0, PORT_16BIT|PORT_32BIT|PORT_RW }, // Channel 0 Data Register (16/32-bit)
	{ 0x01F1, 0x01F7, PORT_8BIT|PORT_RW  }, // Channel 0 control ports (8-bit)
	{ 0x03F6, 0x03F6, PORT_8BIT|PORT_RW  }, // Channel 0 Alternate Status R / Adapter Control Reg W
	{ 0x0170, 0x0170, PORT_16BIT|PORT_32BIT|PORT_RW }, // Channel 1 Data Register (16/32-bit)
	{ 0x0171, 0x0177, PORT_8BIT|PORT_RW  }, // Channel 1 control ports (8-bit)
	{ 0x0376, 0x0376, PORT_8BIT|PORT_RW  }  // Channel 1 Alternate Status R / Adapter Control Reg W
};

// The ATA specification emulated.
// Determines how the IDENTIFY DEVICE command responds.
// Supported versions: 1 to 6.
#define ATA_VERSION  1

#define ATA_CMD_FN(_hex_, _string_, _fn_) { _hex_, { _string_, &StorageCtrl_ATA::ata_cmd_ ## _fn_ } }
#define ATAPI_CMD_FN(_hex_, _string_, _fn_) { _hex_, { _string_, &StorageCtrl_ATA::atapi_cmd_ ## _fn_ } }

const std::map<int, StorageCtrl_ATA::ata_command_fn> StorageCtrl_ATA::ms_ata_commands = {
	ATA_CMD_FN(0x10, "CALIBRATE DRIVE",                calibrate_drive             ),
	ATA_CMD_FN(0x24, "READ SECTORS EXT",               read_sectors                ),
	ATA_CMD_FN(0x29, "READ MULTIPLE EXT",              read_sectors                ),
	ATA_CMD_FN(0x20, "READ SECTORS",                   read_sectors                ),
	ATA_CMD_FN(0x21, "READ SECTORS NO RETRY",          read_sectors                ),
	ATA_CMD_FN(0xC4, "READ MULTIPLE SECTORS",          read_sectors                ),
	ATA_CMD_FN(0x34, "WRITE SECTORS EXT",              write_sectors               ),
	ATA_CMD_FN(0x39, "WRITE MULTIPLE EXT",             write_sectors               ),
	ATA_CMD_FN(0x30, "WRITE SECTORS",                  write_sectors               ),
	ATA_CMD_FN(0x31, "WRITE SECTORS NO RETRY",         write_sectors               ),
	ATA_CMD_FN(0xC5, "WRITE MULTIPLE SECTORS",         write_sectors               ),
	ATA_CMD_FN(0x90, "EXECUTE DEVICE DIAGNOSTIC",      execute_device_diagnostic   ),
	ATA_CMD_FN(0x91, "INITIALIZE DRIVE PARAMETERS",    initialize_drive_parameters ),
	ATA_CMD_FN(0xec, "IDENTIFY DEVICE",                identify_device             ),
	ATA_CMD_FN(0xef, "SET FEATURES",                   set_features                ),
	ATA_CMD_FN(0x42, "READ VERIFY SECTORS EXT",        read_verify_sectors         ),
	ATA_CMD_FN(0x40, "READ VERIFY SECTORS",            read_verify_sectors         ),
	ATA_CMD_FN(0x41, "READ VERIFY SECTORS NO RETRY",   read_verify_sectors         ),
	ATA_CMD_FN(0xc6, "SET MULTIPLE MODE",              set_multiple_mode           ),
	ATA_CMD_FN(0xa1, "IDENTIFY PACKET DEVICE (atapi)", identify_packet_device      ),
	ATA_CMD_FN(0x08, "DEVICE RESET (atapi)",           device_reset                ),
	ATA_CMD_FN(0xa0, "SEND PACKET (atapi)",            send_packet                 ),
	ATA_CMD_FN(0xa2, "SERVICE (atapi)",                not_implemented             ),
	ATA_CMD_FN(0xE0, "STANDBY NOW",                    power_stubs                 ),
	ATA_CMD_FN(0xE1, "IDLE IMMEDIATE",                 power_stubs                 ),
	ATA_CMD_FN(0xE7, "FLUSH CACHE",                    power_stubs                 ),
	ATA_CMD_FN(0xEA, "FLUSH CACHE EXT",                power_stubs                 ),
	ATA_CMD_FN(0xe5, "CHECK POWER MODE",               check_power_mode            ),
	ATA_CMD_FN(0x70, "SEEK",                           seek                        ),
	ATA_CMD_FN(0x25, "READ DMA EXT",                   not_implemented             ),
	ATA_CMD_FN(0xC8, "READ DMA",                       not_implemented             ),
	ATA_CMD_FN(0x35, "WRITE DMA EXT",                  not_implemented             ),
	ATA_CMD_FN(0xCA, "WRITE DMA",                      not_implemented             ),
	ATA_CMD_FN(0x27, "READ NATIVE MAX ADDRESS EXT",    read_native_max_address     ),
	ATA_CMD_FN(0xF8, "READ NATIVE MAX ADDRESS",        read_native_max_address     ),
	ATA_CMD_FN(0x22, "READ LONG",                      not_implemented             ),
	ATA_CMD_FN(0x23, "READ LONG NO RETRY",             not_implemented             ),
	ATA_CMD_FN(0x26, "READ DMA QUEUED EXT",            not_implemented             ),
	ATA_CMD_FN(0x2A, "READ STREAM DMA",                not_implemented             ),
	ATA_CMD_FN(0x2B, "READ STREAM PIO",                not_implemented             ),
	ATA_CMD_FN(0x2F, "READ LOG EXT",                   not_implemented             ),
	ATA_CMD_FN(0x32, "WRITE LONG",                     not_implemented             ),
	ATA_CMD_FN(0x33, "WRITE LONG NO RETRY",            not_implemented             ),
	ATA_CMD_FN(0x36, "WRITE DMA QUEUED EXT",           not_implemented             ),
	ATA_CMD_FN(0x37, "SET MAX ADDRESS EXT",            not_implemented             ),
	ATA_CMD_FN(0x38, "CFA WRITE SECTORS W/OUT ERASE",  not_implemented             ),
	ATA_CMD_FN(0x3A, "WRITE STREAM DMA",               not_implemented             ),
	ATA_CMD_FN(0x3B, "WRITE STREAM PIO",               not_implemented             ),
	ATA_CMD_FN(0x3F, "WRITE LOG EXT",                  not_implemented             ),
	ATA_CMD_FN(0x50, "FORMAT TRACK",                   not_implemented             ),
	ATA_CMD_FN(0x51, "CONFIGURE STREAM",               not_implemented             ),
	ATA_CMD_FN(0x87, "CFA TRANSLATE SECTOR",           not_implemented             ),
	ATA_CMD_FN(0x92, "DOWNLOAD MICROCODE",             not_implemented             ),
	ATA_CMD_FN(0x94, "STANDBY IMMEDIATE",              not_implemented             ),
	ATA_CMD_FN(0x95, "IDLE IMMEDIATE",                 not_implemented             ),
	ATA_CMD_FN(0x96, "STANDBY",                        not_implemented             ),
	ATA_CMD_FN(0x97, "IDLE",                           not_implemented             ),
	ATA_CMD_FN(0x98, "CHECK POWER MODE",               not_implemented             ),
	ATA_CMD_FN(0x99, "SLEEP",                          not_implemented             ),
	ATA_CMD_FN(0xB0, "SMART",                          not_implemented             ),
	ATA_CMD_FN(0xB1, "DEVICE CONFIGURATION",           not_implemented             ),
	ATA_CMD_FN(0xC0, "CFA ERASE SECTORS",              not_implemented             ),
	ATA_CMD_FN(0xC7, "READ DMA QUEUED",                not_implemented             ),
	ATA_CMD_FN(0xC9, "READ DMA NO RETRY",              not_implemented             ),
	ATA_CMD_FN(0xCC, "WRITE DMA QUEUED",               not_implemented             ),
	ATA_CMD_FN(0xCD, "CFA WRITE MULTIPLE W/OUT ERASE", not_implemented             ),
	ATA_CMD_FN(0xD1, "CHECK MEDIA CARD TYPE",          not_implemented             ),
	ATA_CMD_FN(0xDA, "GET MEDIA STATUS",               not_implemented             ),
	ATA_CMD_FN(0xDE, "MEDIA LOCK",                     not_implemented             ),
	ATA_CMD_FN(0xDF, "MEDIA UNLOCK",                   not_implemented             ),
	ATA_CMD_FN(0xE2, "STANDBY",                        not_implemented             ),
	ATA_CMD_FN(0xE3, "IDLE",                           not_implemented             ),
	ATA_CMD_FN(0xE4, "READ BUFFER",                    not_implemented             ),
	ATA_CMD_FN(0xE6, "SLEEP",                          not_implemented             ),
	ATA_CMD_FN(0xE8, "WRITE BUFFER",                   not_implemented             ),
	ATA_CMD_FN(0xED, "MEDIA EJECT",                    not_implemented             ),
	ATA_CMD_FN(0xF1, "SECURITY SET PASSWORD",          not_implemented             ),
	ATA_CMD_FN(0xF2, "SECURITY UNLOCK",                not_implemented             ),
	ATA_CMD_FN(0xF3, "SECURITY ERASE PREPARE",         not_implemented             ),
	ATA_CMD_FN(0xF4, "SECURITY ERASE UNIT",            not_implemented             ),
	ATA_CMD_FN(0xF5, "SECURITY FREEZE LOCK",           not_implemented             ),
	ATA_CMD_FN(0xF6, "SECURITY DISABLE PASSWORD",      not_implemented             ),
	ATA_CMD_FN(0xF9, "SET MAX ADDRESS",                not_implemented             )
};

const std::map<int, StorageCtrl_ATA::atapi_command_fn> StorageCtrl_ATA::ms_atapi_commands = {
	// SFF-8020i
	ATAPI_CMD_FN(0x12, "INQUIRY",                       inquiry                      ),
	ATAPI_CMD_FN(0xa6, "LOAD/UNLOAD CD",                not_implemented              ),
	ATAPI_CMD_FN(0xbd, "MECHANISM STATUS",              not_implemented              ),
	ATAPI_CMD_FN(0x55, "MODE SELECT (10)",              mode_select                  ),
	ATAPI_CMD_FN(0x1a, "MODE SENSE (6)",                mode_sense                   ),
	ATAPI_CMD_FN(0x5a, "MODE SENSE (10)",               mode_sense                   ),
	ATAPI_CMD_FN(0x4b, "PAUSE/RESUME",                  pause_resume                 ),
	ATAPI_CMD_FN(0x45, "PLAY AUDIO",                    play_audio                   ),
	ATAPI_CMD_FN(0x47, "PLAY AUDIO MSF",                play_audio_msf               ),
	ATAPI_CMD_FN(0xbc, "PLAY CD",                       not_implemented              ),
	ATAPI_CMD_FN(0x1e, "PREVENT/ALLOW MEDIUM REMOVAL",  prevent_allow_medium_removal ),
	ATAPI_CMD_FN(0x28, "READ (10)",                     read                         ),
	ATAPI_CMD_FN(0xa8, "READ (12)",                     read                         ),
	ATAPI_CMD_FN(0x25, "READ CD-ROM CAPACITY",          read_cdrom_capacity          ),
	ATAPI_CMD_FN(0xbe, "READ CD",                       not_implemented              ),
	ATAPI_CMD_FN(0xb9, "READ CD MSF",                   not_implemented              ),
	ATAPI_CMD_FN(0x44, "READ HEADER",                   not_implemented              ),
	ATAPI_CMD_FN(0x42, "READ SUB CHANNEL",              read_subchannel              ),
	ATAPI_CMD_FN(0x43, "READ TOC",                      read_toc                     ),
	ATAPI_CMD_FN(0x03, "REQUEST SENSE",                 request_sense                ),
	ATAPI_CMD_FN(0xba, "SCAN",                          not_implemented              ),
	ATAPI_CMD_FN(0x2b, "SEEK",                          seek                         ),
	ATAPI_CMD_FN(0xbb, "SET CD SPEED",                  not_implemented              ),
	ATAPI_CMD_FN(0x4e, "STOP PLAY/SCAN",                stop_play_scan               ),
	ATAPI_CMD_FN(0x1b, "START/STOP UNIT",               start_stop_unit              ),
	ATAPI_CMD_FN(0x00, "TEST UNIT READY",               test_unit_ready              ),
	// MMC-3
	ATAPI_CMD_FN(0x51, "READ DISC INFO",                read_disc_info               ),
	ATAPI_CMD_FN(0x4a, "GET EVENT STATUS NOTIFICATION", get_event_status_notification),
};

#define MIN_CMD_US      250u     // minimum busy time
#define DEFAULT_CMD_US  2200u    // default command execution time
#define SEEK_CMD_US     2940u    // seek exec time
#define CALIB_CMD_US    500000u  // calibrate exec time
#define CTRL_OVERH_US   3000u    // controller command execution overhead

enum SenseKey
{
	SENSE_NONE            = 0,
	SENSE_NOT_READY       = 2,
	SENSE_ILLEGAL_REQUEST = 5,
	SENSE_UNIT_ATTENTION  = 6,
	SENSE_ABORTED         = 0xb
};

static const std::map<int, const char*> s_sense_key_str{
	{ SENSE_NONE, "SENSE_NONE" },
	{ SENSE_NOT_READY, "SENSE_NOT_READY" },
	{ SENSE_ILLEGAL_REQUEST, "SENSE_ILLEGAL_REQUEST" },
	{ SENSE_UNIT_ATTENTION, "SENSE_UNIT_ATTENTION" },
	{ SENSE_ABORTED, "SENSE_ABORTED_COMMAND" }
};

enum ASC
{
	ASC_DRIVE_NOT_READY                 = 0x04,
	ASC_INVALID_COMMAND_OPCODE          = 0x20,
	ASC_LOGICAL_BLOCK_OOR               = 0x21,
	ASC_INVALID_FIELD_IN_CMD_PACKET     = 0x24,
	ASC_NOT_READY_TO_READY              = 0x28, // medium changed
	ASC_SAVING_PARAMETERS_NOT_SUPPORTED = 0x39,
	ASC_MEDIUM_NOT_PRESENT              = 0x3a,
	ASC_MEDIA_REMOVAL_PREVENTED         = 0x53,
};

static const std::map<int, const char*> s_asc_str{
	{ ASC_DRIVE_NOT_READY, "ASC_DRIVE_NOT_READY" },
	{ ASC_INVALID_COMMAND_OPCODE, "ASC_INVALID_COMMAND_OPCODE" },
	{ ASC_LOGICAL_BLOCK_OOR, "ASC_LOGICAL_BLOCK_OOR" },
	{ ASC_INVALID_FIELD_IN_CMD_PACKET, "ASC_INVALID_FIELD_IN_CMD_PACKET" },
	{ ASC_NOT_READY_TO_READY, "ASC_NOT_READY_TO_READY" },
	{ ASC_SAVING_PARAMETERS_NOT_SUPPORTED, "ASC_SAVING_PARAMETERS_NOT_SUPPORTED" },
	{ ASC_MEDIUM_NOT_PRESENT, "ASC_MEDIUM_NOT_PRESENT" },
	{ ASC_MEDIA_REMOVAL_PREVENTED, "ASC_MEDIA_REMOVAL_PREVENTED" },
};

enum ATAPIInt {
	ATAPI_INT_DATA = 0,
	ATAPI_INT_CMD = 1,
	ATAPI_INT_TO_DEV = 0,
	ATAPI_INT_TO_HOST = 1
};


#define ATAPI_PACKET_SIZE 12


StorageCtrl_ATA::StorageCtrl_ATA(Devices *_dev)
: StorageCtrl(_dev), m_busy(false)
{
	memset(m_channels, 0, sizeof(Channel) * ATA_MAX_CHANNEL);
}

StorageCtrl_ATA::~StorageCtrl_ATA()
{
}

void StorageCtrl_ATA::install()
{
	StorageCtrl::install();

	m_channels[0].irq = 14;
	m_channels[0].ioaddr1 = 0x01f0;
	m_channels[0].ioaddr2 = 0x03f0;
	if(ATA_MAX_CHANNEL > 1) {
		m_channels[1].irq = 15;
		m_channels[1].ioaddr1 = 0x0170;
		m_channels[1].ioaddr2 = 0x0370;
	}

	using namespace std::placeholders;
	for(int channel=0; channel<ATA_MAX_CHANNEL; channel++) {
		g_machine.register_irq(m_channels[channel].irq, name());
		for(uint8_t device=0; device<2; device ++) {
			m_cmd_timers[channel][device] = g_machine.register_timer(
				std::bind(&StorageCtrl_ATA::command_timer, this, channel, device, _1),
				device_string(channel, device)
			);
		}
	}

	PINFOF(LOG_V0, LOG_HDD, "Installed %s\n", name());
}

void StorageCtrl_ATA::remove()
{
	StorageCtrl::remove();

	for(int channel=0; channel<ATA_MAX_CHANNEL; channel++) {
		g_machine.unregister_irq(m_channels[channel].irq, name());
		for(int device=0; device<2; device ++) {
			if(drive_is_present(channel,device)) {
				m_storage[channel][device]->remove();
				m_storage[channel][device].reset();
			}
			g_machine.unregister_timer(m_cmd_timers[channel][device]);
		}
	}
}

void StorageCtrl_ATA::config_changed()
{
	StorageCtrl::config_changed();

	/* Possible configuration:
	 * Only 1 HDD and 1 CD-ROM drive
	 * HDD present:     ATA0:0 HDD    ATA0:1 CD-ROM
	 * HDD not present: ATA0:0 CD-ROM
	 */
	for(int ch=0; ch<ATA_MAX_CHANNEL; ch++) {
		for(uint8_t dev=0; dev<2; dev++) {
			if(drive_is_present(ch,dev)) {
				m_storage[ch][dev]->remove();
				m_storage[ch][dev].reset();
			}
		}
	}

	m_devices_cnt = 0;

	if(g_program.config().get_string(DRIVES_SECTION, DRIVES_HDC_TYPE) == "ata") {
		m_storage[0][0] = std::make_unique<HardDiskDrive>();
		m_storage[0][0]->set_name("Drive C");
		m_storage[0][0]->install(this, m_devices_cnt, DISK_C_SECTION);
		m_storage[0][0]->config_changed();
		m_devices_cnt++;
	}

	if(g_program.config().get_int_or_bool(DRIVES_SECTION, DRIVES_CDROM)) {
		m_storage[0][m_devices_cnt] = std::make_unique<CdRomDrive>();
		m_storage[0][m_devices_cnt]->set_name("CD-ROM");
		m_storage[0][m_devices_cnt]->install(this, m_devices_cnt, DISK_CD_SECTION);
		m_storage[0][m_devices_cnt]->config_changed();
		m_devices_cnt++;
	}
}

void StorageCtrl_ATA::reset(unsigned _type)
{
	for(int ch=0; ch<ATA_MAX_CHANNEL; ch++) {
		m_devices->pic()->lower_irq(m_channels[ch].irq);
		if(_type == MACHINE_POWER_ON) {
			reset_channel(ch);
			for(int dev=0; dev<2; dev++) {
				if(drive_is_present(ch, dev)) {
					m_storage[ch][dev]->power_on(g_machine.get_virt_time_us());
				}
				if(is_hdd(ch, dev)) {
					uint64_t powerup = m_storage[ch][dev]->power_up_eta_us();
					if(powerup > 0) {
						ctrl(ch,dev).status.busy        = true;
						ctrl(ch,dev).status.drive_ready = false;
						g_machine.activate_timer(m_cmd_timers[ch][dev], powerup*1_us, false);
					}
				}
			}
		}
	}
	update_busy_status();
}

void StorageCtrl_ATA::power_off()
{
	StorageCtrl::power_off();

	for(int ch=0; ch<ATA_MAX_CHANNEL; ch++) {
		for(int dev=0; dev<2; dev++) {
			if(drive_is_present(ch,dev)) {
				m_storage[ch][dev]->power_off();
			}
		}
	}

	m_busy = false;
}

void StorageCtrl_ATA::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "%s: saving state\n", name());

	_state.write(&m_channels, {sizeof(m_channels), name()});
	for(int ch=0; ch<ATA_MAX_CHANNEL; ch++) {
		for(int dev=0; dev<2; dev++) {
			if(drive_is_present(ch,dev)) {
				m_storage[ch][dev]->save_state(_state);
			}
		}
	}
}

void StorageCtrl_ATA::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "%s: restoring state\n", name());

	_state.read(&m_channels, {sizeof(m_channels), name()});
	for(int ch=0; ch<ATA_MAX_CHANNEL; ch++) {
		for(int dev=0; dev<2; dev++) {
			if(drive_is_present(ch,dev)) {
				m_storage[ch][dev]->restore_state(_state);
			}
		}
	}
	update_busy_status();
}

StorageDev * StorageCtrl_ATA::get_device(int _dev_idx)
{
	int idx = 0;
	for(int ch=0; ch<ATA_MAX_CHANNEL; ch++) {
		for(int dev=0; dev<2; dev++) {
			if(drive_is_present(ch,dev) && idx++ == _dev_idx) {
				return m_storage[ch][dev].get();
			}
		}
	}
	return nullptr;
}

void StorageCtrl_ATA::reset_channel(int _ch)
{
	m_channels[_ch].drive_select = 0;
	for(uint8_t dev=0; dev<2; dev ++) {
		drive(_ch,dev).next_lba = 0;
		drive(_ch,dev).curr_lba = 0;
		drive(_ch,dev).prev_cyl = 0;

		memset(&drive(_ch,dev).sense, 0, sizeof(Drive::Sense));

		drive(_ch,dev).atapi.command = 0;
		//drive(_ch,dev).atapi.drq_bytes = 0;
		drive(_ch,dev).atapi.bytes_remaining = 0;
		drive(_ch,dev).atapi.sectors_remaining = 0;
		drive(_ch,dev).atapi.seek_completion_time = 0;
		drive(_ch,dev).atapi.error_recovery[0] = 0x01; // Page Code (01h)
		drive(_ch,dev).atapi.error_recovery[1] = 0x06; // Page Length (06h)
		drive(_ch,dev).atapi.error_recovery[2] = 0x00; // Error Recovery Parameter, Default 0
		drive(_ch,dev).atapi.error_recovery[3] = 0x03; // Read Retry Count
		drive(_ch,dev).atapi.error_recovery[4] = 0x00; // Reserved
		drive(_ch,dev).atapi.error_recovery[5] = 0x00; // Reserved
		drive(_ch,dev).atapi.error_recovery[6] = 0x00; // Reserved
		drive(_ch,dev).atapi.error_recovery[7] = 0x00; // Reserved

		// Initialize controller state, even if device is not present
		ctrl(_ch,dev).status.busy              = false;
		ctrl(_ch,dev).status.drive_ready       = true;
		ctrl(_ch,dev).status.write_fault       = false;
		ctrl(_ch,dev).status.seek_complete     = true;
		ctrl(_ch,dev).status.drq               = false;
		ctrl(_ch,dev).status.corrected_data    = false;
		ctrl(_ch,dev).status.index_pulse       = false;
		ctrl(_ch,dev).status.index_pulse_time  = 0;
		ctrl(_ch,dev).status.err               = false;

		ctrl(_ch,dev).error_register      = 0x01; // diagnostic code: no error
		ctrl(_ch,dev).head_no             = 0;
		ctrl(_ch,dev).sector_count        = 1;
		ctrl(_ch,dev).sector_no           = 1;
		ctrl(_ch,dev).cylinder_no         = 0;
		ctrl(_ch,dev).current_command     = 0x00;
		ctrl(_ch,dev).buffer_index        = 0;
		ctrl(_ch,dev).control.reset       = false;
		ctrl(_ch,dev).control.disable_irq = false;
		ctrl(_ch,dev).reset_in_progress   = false;
		ctrl(_ch,dev).multiple_sectors    = 0;
		ctrl(_ch,dev).lba_mode            = false;
		ctrl(_ch,dev).features            = 0;
		ctrl(_ch,dev).mdma_mode           = 0;
		ctrl(_ch,dev).udma_mode           = 0;
		ctrl(_ch,dev).look_ahead_time     = g_machine.get_virt_time_us();

		drive(_ch,dev).identify_set = false;
	}
}

void StorageCtrl_ATA::command_timer(int _ch, int _dev, uint64_t /*_time*/)
{
	Controller *controller = &ctrl(_ch, _dev);
	if(is_hdd(_ch, _dev)) {
		switch(controller->current_command) {
			case 0x00: // not a command, power up finished, no IRQ
				command_successful(_ch, _dev, false);
				break;
			case 0x20: // READ SECTORS, with retries
			case 0x21: // READ SECTORS, without retries
			case 0x24: // READ SECTORS EXT
			case 0x29: // READ MULTIPLE EXT
			case 0xC4: // READ MULTIPLE SECTORS
				command_successful(_ch, _dev, true);
				controller->status.drq = true;
				break;
			case 0x40: // READ VERIFY SECTORS
			case 0x41: // READ VERIFY SECTORS NO RETRY
			case 0x42: // READ VERIFY SECTORS EXT
			{
				command_successful(_ch, _dev, true);
				int64_t next_cyl = increment_address(_ch,
							drive(_ch,_dev).next_lba,
							controller->num_sectors);
				int64_t curr_cyl = m_storage[_ch][_dev]->lba_to_cylinder(drive(_ch,_dev).curr_lba);
				if(curr_cyl != next_cyl) {
					drive(_ch,_dev).prev_cyl = curr_cyl;
				}
				drive(_ch,_dev).curr_lba = drive(_ch,_dev).next_lba;
				break;
			}
			case 0x30: // WRITE SECTORS
			case 0x31: // WRITE SECTORS NO RETRY
			case 0xC5: // WRITE MULTIPLE SECTORS
			case 0x34: // WRITE SECTORS EXT
			case 0x39: // WRITE MULTIPLE EXT
				command_successful(_ch, _dev, true);
				controller->status.drq = true;
				break;
			case 0x90: // EXECUTE DEVICE DIAGNOSTIC
				command_successful(_ch, _dev, true);
				controller->error_register = 0x01;
				break;
			default:
				command_successful(_ch, _dev, true);
				break;
		}
	} else {
		switch(drive(_ch, _dev).atapi.command) {
			case 0x1e: // prevent allow medium removal
			case 0x2b: // seek
			case 0x1b: // start/stop unit
			case 0x45: // play audio
			case 0x47: // play audio msf
				// if seek or play audio this is the cmd time, the head is still moving 
				atapi_success(_ch);
				break;
			case 0x28: // read (10)
			case 0xa8: // read (12)
				if(!selected_drive(_ch).atapi.sectors_total) {
					// a read with 0 bytes to read
					atapi_success(_ch);
				} else {
					atapi_ready_to_transfer(_ch, ATAPI_INT_DATA, ATAPI_INT_TO_HOST);
				}
				break;
			case 0x12: // inquiry
			case 0x1a: // mode_sense (6)
			case 0x5a: // mode_sense (10)
			case 0x25: // read cd-rom capacity
			case 0xbe: // read cd (TODO)
			case 0x42: // read sub-channel
			case 0x43: // read toc
			case 0x03: // request sense
			case 0x4a: // get_event_status_notification
				atapi_ready_to_transfer(_ch, ATAPI_INT_DATA, ATAPI_INT_TO_HOST);
				break;
			case 0x55: // mode select
				atapi_ready_to_transfer(_ch, ATAPI_INT_DATA, ATAPI_INT_TO_DEV);
				break;
			default:
				PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: command_timer(): command 0x%02x should not be timed!\n",
						selected_string(_ch), drive(_ch, _dev).atapi.command);
				break;
		}
	}

	update_busy_status();
}

uint16_t StorageCtrl_ATA::read(uint16_t _address, unsigned _len)
{
	uint16_t value = 0;
	int channel = ATA_MAX_CHANNEL;
	uint16_t port = 0xff; // undefined

	PDEBUGF(LOG_V2, LOG_HDD, "read  0x%03X ", _address);

	for(channel=0; channel<ATA_MAX_CHANNEL; channel++) {
		if((_address & 0xfff8) == m_channels[channel].ioaddr1) {
			port = _address - m_channels[channel].ioaddr1;
			break;
		} else if((_address & 0xfff8) == m_channels[channel].ioaddr2) {
			port = _address - m_channels[channel].ioaddr2 + 0x10;
			break;
		}
	}

	if(channel == ATA_MAX_CHANNEL) {
		channel = 0;
		if((_address < 0x03f6) || (_address > 0x03f7)) {
			PDEBUGF(LOG_V2, LOG_HDD, "channel not present\n");
			return ~0;
		} else {
			port = _address - 0x03e0;
		}
	}

	Controller *controller = &selected_ctrl(channel);

	switch(port) {
		case 0x00: // ATA/ATAPI Data (16bit) (0x1f0)
		{
			if(controller->status.drq == false) {
				if(controller->current_command == 0xa0) {
					PDEBUGF(LOG_V0, LOG_HDD, "drq == false: ATAPI last command=0x%02x\n", selected_drive(channel).atapi.command);
				} else {
					PDEBUGF(LOG_V0, LOG_HDD, "drq == false: last command=0x%02x\n", controller->current_command);
				}
				return 0;
			}
			switch (controller->current_command) {
				case 0x20: // READ SECTORS, with retries
				case 0x21: // READ SECTORS, without retries
				case 0xC4: // READ MULTIPLE SECTORS
				case 0x24: // READ SECTORS EXT
				case 0x29: // READ MULTIPLE EXT
				{
					if(controller->buffer_index >= controller->buffer_size) {
						PERRF_ABORT(LOG_HDD, "buffer_index >= %d\n", controller->buffer_size);
					}
					if(_len != 2) {
						PERRF_ABORT(LOG_HDD, "unsupported read size: %d\n", _len);
					}
					value |= uint16_t(controller->buffer[controller->buffer_index+1]) << 8;
					value |= controller->buffer[controller->buffer_index];

					PDEBUGF(LOG_V3, LOG_HDD, "READ data %04d/%04d -> 0x%04X",
							controller->buffer_index, (controller->buffer_size-1), value);
					PDEBUGF(LOG_V2, LOG_HDD, "\n");

					controller->buffer_index += _len;

					// if buffer completely read
					if(controller->buffer_index >= controller->buffer_size) {
						controller->status.drq = false;
						if(controller->num_sectors == 0) {
							// no more sectors to read
							controller->status.err = false;
							controller->buffer_size = 0;
						} else {
							// read next block of sectors into controller buffer
							uint32_t exec_time = ata_read_next_block(channel, 0);
							if(!controller->status.err) {
								activate_command_timer(channel, exec_time);
								controller->status.busy = true;
							}
						}
					}
					break;
				}
				case 0xec: // IDENTIFY DEVICE
				case 0xa1:
				{
					uint32_t index;
					index = controller->buffer_index;
					value = controller->buffer[index];
					index++;
					if(_len > 1) {
						value |= uint16_t(controller->buffer[index]) << 8;
						index++;
					}
					if(_len > 2) {
						PERRF_ABORT(LOG_HDD, "unsupported read size: %d\n", _len);
					}

					PDEBUGF(LOG_V3, LOG_HDD, "IDFY data %04d/511 -> 0x%04X",
							controller->buffer_index, value);
					PDEBUGF(LOG_V2, LOG_HDD, "\n");

					controller->buffer_index = index;

					if(controller->buffer_index >= 512) {
						controller->status.drq = false;
					}
					break;
				}
				case 0xa0: // SEND PACKET (atapi)
				{
					if(!controller->status.drq) {
						PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: DRQ not set.\n", selected_string(channel));
						break;
					}
					if(_len > 2) {
						PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: unsupported read size: %d\n", selected_string(channel), _len);
						break;
					}
					if(controller->buffer_index >= controller->buffer_size) {
						PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: buffer_index >= buffer_size (%u)\n",
								selected_string(channel), controller->buffer_size);
						break;
					}

					value = controller->buffer[controller->buffer_index];
					if(_len == 2) {
						value |= uint16_t(controller->buffer[controller->buffer_index + 1]) << 8;
					}

					PDEBUGF(LOG_V3, LOG_HDD, "%s: ATAPI: READ data %04u/%04u %04d/%04d, DRQ %04u/%04d -> 0x%04X",
							selected_string(channel),
							controller->buffer_index, controller->buffer_size,
							selected_drive(channel).atapi.bytes_remaining, selected_drive(channel).atapi.bytes_total,
							controller->drq_index,
							 //selected_drive(channel).atapi.drq_bytes,
							controller->byte_count,
							value);
					PDEBUGF(LOG_V2, LOG_HDD, "\n");

					controller->buffer_index += _len;
					controller->drq_index += _len;
					selected_drive(channel).atapi.bytes_remaining -= _len;

					//if(controller->drq_index >= uint32_t(selected_drive(channel).atapi.drq_bytes)) {
					if(controller->drq_index >= controller->byte_count) {

						controller->status.drq = false;
						controller->drq_index = 0;

						if(selected_drive(channel).atapi.bytes_remaining > 0) {
							// one or more blocks remaining
							switch(selected_drive(channel).atapi.command) {
								case 0x28: // read (10)
								case 0xa8: // read (12)
								case 0xbe: // read cd
								{
									// for reads the DRQ window and the buffer window are the same
									uint32_t exec_time = atapi_read_next_block(channel, false);
									if(!controller->status.err) {
										activate_command_timer(channel, exec_time);
										controller->status.busy = true;
									}
									break;
								}
								default:
									// other commands write all the data into the data buffer once 
									PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: PACKET new DRQ, bytes remaining=%d.\n",
											selected_string(channel), selected_drive(channel).atapi.bytes_remaining);
									// set new byte count if last block
									if(selected_drive(channel).atapi.bytes_remaining < controller->byte_count) {
										controller->byte_count = selected_drive(channel).atapi.bytes_remaining;
									}
									//selected_drive(channel).atapi.drq_bytes = controller->byte_count;
									atapi_ready_to_transfer(channel, ATAPI_INT_DATA, ATAPI_INT_TO_HOST);
									break;
							}
						} else {
							PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: PACKET all bytes read.\n", selected_string(channel));
							atapi_success(channel);
						}
					}
					break;
				}
				default:
					PDEBUGF(LOG_V0, LOG_HDD, "unexpected READ, current command is 0x%02x\n", controller->current_command);
					break;
			}
			break;
		}

		case 0x01: // ATA/ATAPI Error Register (0x1f1)
			// -- WARNING : On real hardware the controller registers are shared between drives.
			// So we must respond even if the select device is not present. Some OS uses this fact
			// to detect the disks.... minix2 for example
			value = (!any_is_present(channel)) ? 0 : controller->error_register;
			PDEBUGF(LOG_V2, LOG_HDD, "err reg   -> 0x%02X\n", value);
			break;
		case 0x02: // ATA Sector Count / ATAPI Interrupt Reason (0x1f2)
			value = (!any_is_present(channel)) ? 0 : controller->sector_count;
			PDEBUGF(LOG_V2, LOG_HDD, "sct cnt   -> 0x%02X\n", value);
			break;
		case 0x03: // ATA Sector Number (0x1f3)
			value = (!any_is_present(channel)) ? 0 : controller->sector_no;
			PDEBUGF(LOG_V2, LOG_HDD, "sct num   -> 0x%02X\n", value);
			break;
		case 0x04: // ATA Cylinder Low, ATAPI Byte Count 0-7 (0x1f4)
			value = (!any_is_present(channel)) ? 0 : (controller->cylinder_no & 0x00ff);
			PDEBUGF(LOG_V2, LOG_HDD, "cyl low   -> 0x%02X\n", value);
			break;
		case 0x05: // ATA Cylinder High, ATAPI Byte Count 8-15 (0x1f5)
			value = (!any_is_present(channel)) ? 0 : controller->cylinder_no >> 8;
			PDEBUGF(LOG_V2, LOG_HDD, "cyl high  -> 0x%02X\n", value);
			break;
		case 0x06: // ATA Drive / Head Select, ATAPI Drive Select (0x1f6)
			// b7 Extended data field for ECC
			// b6/b5: Used to be sector size.  00=256,01=512,10=1024,11=128
			//   Since 512 was always used, bit 6 was taken to mean LBA mode:
			//     b6 1=LBA mode, 0=CHS mode
			//     b5 1
			// b4: DRV
			// b3..0 HD3..HD0
			value =
				(1 << 7) |
				(controller->lba_mode << 6) |
				(1 << 5) | // 01b = 512 sector size
				(m_channels[channel].drive_select << 4) |
				(controller->head_no << 0);
			PDEBUGF(LOG_V2, LOG_HDD, "drv head -> 0x%04X\n", value);
			break;
		case 0x07: // ATA/ATAPI Status (0x1f7)
		case 0x16: // ATA/ATAPI Alternate Status (0x3f6)
		{
			if(!selected_is_present(channel)) {
				// (mch) Just return zero for these registers
				value = 0;
			} else {
				if(selected_is_cd(channel)) {
					// ATAPI: The SEEK Command will always be executed as an immediate command.
					// The command will return completion stations as soon as the seek operation has been started.
					// When the actual completion of the seek operation of immediate commands has
					// occurred, the Device shall set the DSC bit in the Status Register.
					selected_drive(channel).atapi_check_seek_completion(g_machine.get_virt_time_us());
				}

				value = (
				        (controller->status.busy << 7) |
				        (controller->status.drive_ready << 6) |
				        (controller->status.write_fault << 5) |
				        (controller->status.seek_complete << 4) |
				        (controller->status.drq << 3) |
				        (controller->status.corrected_data << 2) |
				        (controller->status.index_pulse << 1) |
				        (controller->status.err)
				        );
				controller->status.index_pulse = false;
				uint32_t elapsed = g_machine.get_virt_time_us() - controller->status.index_pulse_time;
				uint32_t rot_time = selected_storage(channel).performance().trk_read_us;
				if(elapsed >= rot_time) {
					controller->status.index_pulse = true;
					controller->status.index_pulse_time = g_machine.get_virt_time_us();
				}
			}
			std::string value_str = bitfield_to_string(value,
			{ "ERR", "IDX", "CORR", "DRQ", "DSC", "WFT", "DRDY", "BSY" },
			{ "", "", "", "", "", "", "", "" });
			PDEBUGF(LOG_V2, LOG_HDD, "status    -> 0x%02X %s\n", value, value_str.c_str());
			if(port == 0x07) {
				lower_interrupt(channel);
			}
			break;
		}
		default:
			PDEBUGF(LOG_V0, LOG_HDD, "invalid address\n");
			break;
	}

	update_busy_status();

	return value;
}

void StorageCtrl_ATA::write(uint16_t _address, uint16_t _value, unsigned _len)
{
	bool prev_control_reset;
	int channel = ATA_MAX_CHANNEL;
	uint16_t port = 0xff; // undefined

	PDEBUGF(LOG_V2, LOG_HDD, "write 0x%03X ", _address);

	for(channel=0; channel<ATA_MAX_CHANNEL; channel++) {
		if((_address & 0xfff8) == m_channels[channel].ioaddr1) {
			port = _address - m_channels[channel].ioaddr1;
			break;
		} else if((_address & 0xfff8) == m_channels[channel].ioaddr2) {
			port = _address - m_channels[channel].ioaddr2 + 0x10;
			break;
		}
	}
	if(channel == ATA_MAX_CHANNEL) {
		if(_address != 0x03f6) {
			PDEBUGF(LOG_V2, LOG_HDD, "channel not present\n");
			return;
		} else {
			channel = 0;
			port = _address - 0x03e0;
		}
	}

	Controller *controller = &selected_ctrl(channel);

	switch (port) {
		case 0x00: // ATA/ATAPI Data (0x1f0)
		{
			switch(controller->current_command) {
				case 0x30: // WRITE SECTORS
				case 0x31: // WRITE SECTORS NO RETRY
				case 0xC5: // WRITE MULTIPLE SECTORS
				case 0x34: // WRITE SECTORS EXT
				case 0x39: // WRITE MULTIPLE EXT
				{
					if(controller->buffer_index >= controller->buffer_size) {
						PERRF_ABORT(LOG_HDD, "buffer_index >= %d\n", controller->buffer_size);
					}
					if(_len != 2) {
						PERRF_ABORT(LOG_HDD, "unsupported io len=%d\n", _len);
					}
					controller->buffer[controller->buffer_index]   = (uint8_t) _value;
					controller->buffer[controller->buffer_index+1] = (uint8_t)(_value >> 8);

					PDEBUGF(LOG_V3, LOG_HDD, "WRITE data %04d/%04d <- 0x%04X",
							controller->buffer_index, (controller->buffer_size-1), _value);
					PDEBUGF(LOG_V2, LOG_HDD, "\n");

					controller->buffer_index += 2;

					if(controller->buffer_index >= controller->buffer_size) {
						/* buffer is completely written, we are ready to write
						 * data block to the device
						 */
						/* Don't use the timer. Assume the use of an internal
						 * fast buffer that immediately accept all the written
						 * sectors.
						 * The PS/1 BIOS awaits for IRQ 14 for no more than 1000us
						 * and then throws an error.
						 * Bochs' BIOS doesn't use the IRQ and doesn't wait at all:
						 * it just throws an error if BSY bit is set, against
						 * the ATA protocol spec.
						 */
						try {
							// the following function updates the value of next_lsector
							// with the first sector of the next block transfer
							ata_tx_sectors(channel, true, controller->buffer, controller->buffer_size);
							command_successful(channel, m_channels[channel].drive_select, true);
						} catch(std::exception &) {
							command_aborted(channel, controller->current_command);
						}
						// writes invalidate the whole cache
						controller->look_ahead_time = g_machine.get_virt_time_us();
						if(!controller->status.err && controller->num_sectors) {
							ata_write_next_block(channel);
							controller->status.drq = true;
						}
					}
					break;
				}
				case 0xa0: // PACKET
				{
					if(!controller->status.drq) {
						PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: DRQ not set.\n", selected_string(channel));
						break;
					}
					if(controller->buffer_index >= controller->buffer_size) {
						PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: buffer_index (%u) >= buffer_size (%u)\n",
								selected_string(channel),
								controller->buffer_index, controller->buffer_size);
						break;
					}
					if(_len > 2) {
						PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: unsupported io len=%d\n", selected_string(channel), _len);
						break;
					}
					controller->buffer[controller->buffer_index] = (uint8_t) _value;
					if(_len == 2) {
						controller->buffer[controller->buffer_index+1] = (uint8_t)(_value >> 8);
					}

					PDEBUGF(LOG_V3, LOG_HDD, "%s: ATAPI: data: %04d/%04d <- 0x%04X",
							selected_string(channel),
							controller->buffer_index, controller->buffer_size, _value);
					PDEBUGF(LOG_V2, LOG_HDD, "\n");

					controller->buffer_index += _len;

					if(controller->buffer_index >= controller->buffer_size) {
						// transfer complete
						controller->status.drq = false;
						controller->status.busy = true;

						Drive &drive = selected_drive(channel);
						if(drive.atapi.command == 0) {
							// new command
							if(!selected_is_present(channel)) {
								// this shouldn't happen, ATA Command (0x1f7) should not allow to be set to 0xA0,
								PERRF(LOG_HDD, "%s: ATAPI: PACKET command (0x%02x): drive not present!\n",
										selected_string(channel),
										drive.atapi.command);
								assert(false);
								return;
							}
							if(!selected_is_cd(channel)) {
								// this shouldn't happen, ata_cmd_send_packet() should not allow ATAPI cmds to non ATAPI drives
								PERRF(LOG_HDD, "%s: ATAPI: PACKET command (0x%02x): drive is not a CD-ROM!\n",
										selected_string(channel),
										drive.atapi.command);
								assert(false);
								return;
							}
							// after this point we are sure the drive exists and is a CdRomDrive
							controller->buffer_size = 0; // not expecting more data
							auto command_fn = ms_atapi_commands.find(controller->buffer[0]);
							if(command_fn != ms_atapi_commands.end()) {
								drive.atapi.command = controller->buffer[0];
								PDEBUGF(LOG_V1, LOG_HDD, "%s: ATAPI: command 0x%02x %s\n",
										selected_string(channel), drive.atapi.command, command_fn->second.first);
								if(drive.atapi.command != 0x12 // not INQUIRY
									&& !atapi_check_transitions(channel))
								{
									// give error when disc changes (except for inquiry)
									atapi_error(channel);
								} else {
									uint32_t exec_time = command_fn->second.second(*this, channel, drive.atapi.command);
									if(!controller->status.err && exec_time) {
										activate_command_timer(channel, exec_time);
									}
								}
							} else {
								PERRF(LOG_HDD, "%s: ATAPI: unknown command 0x%02x (%d)\n",
										selected_string(channel), controller->buffer[0], controller->buffer[0]);
								atapi_set_sense(channel, SENSE_ILLEGAL_REQUEST, ASC_INVALID_COMMAND_OPCODE);
								atapi_error(channel);
							}
						} else {
							// command mode
							switch(drive.atapi.command) {
								case 0x55: // MODE SELECT
									atapi_mode_select(channel, controller->buffer_size);
									atapi_success(channel);
									break;
								default:
									PERRF(LOG_HDD, "%s: ATAPI: not expecting data for command 0x%02x (%d)\n",
											selected_string(channel), drive.atapi.command, drive.atapi.command);
									atapi_set_sense(channel, SENSE_ILLEGAL_REQUEST, ASC_INVALID_COMMAND_OPCODE); // ?
									atapi_error(channel);
									break;
							}
						}
						break;
					}
					break;
				}
				default:
				{
					PDEBUGF(LOG_V0, LOG_HDD, "current command is %02xh\n", controller->current_command);
					break;
				}
			}
			break;
		}
		case 0x01: // ATA Write Precompensation (Features), ATAPI Features (0x1f1)
		{
			ctrl(channel, 0).hob.feature = ctrl(channel, 0).features;
			ctrl(channel, 1).hob.feature = ctrl(channel, 1).features;
			ctrl(channel, 0).features = _value;
			ctrl(channel, 1).features = _value;
			if(_value == 0xff) {
				PDEBUGF(LOG_V2, LOG_HDD, "features  <- no p-comp {%s}\n",
						selected_type_string(channel));
			} else {
				PDEBUGF(LOG_V2, LOG_HDD, "features  <- 0x%02x {%s}\n",
						_value, selected_type_string(channel));
			}
			break;
		}
		case 0x02: // ATA Sector Count (0x1f2)
		{
			ctrl(channel, 0).hob.nsector = ctrl(channel, 0).sector_count;
			ctrl(channel, 1).hob.nsector = ctrl(channel, 1).sector_count;
			ctrl(channel, 0).sector_count = _value;
			ctrl(channel, 1).sector_count = _value;
			PDEBUGF(LOG_V2, LOG_HDD, "sct cnt   <- %u {%s}\n",
					_value, selected_type_string(channel));
			break;
		}
		case 0x03: // ATA Sector Number (0x1f3)
		{
			ctrl(channel, 0).hob.sector = ctrl(channel, 0).sector_no;
			ctrl(channel, 1).hob.sector = ctrl(channel, 1).sector_no;
			ctrl(channel, 0).sector_no = _value;
			ctrl(channel, 1).sector_no = _value;
			PDEBUGF(LOG_V2, LOG_HDD, "sct num   <- %u {%s}\n",
					_value, selected_type_string(channel));
			break;
		}
		case 0x04: // ATA Cylinder Low, ATAPI Byte Count 0-7 (0x1f4)
		{
			ctrl(channel, 0).hob.lcyl = (uint8_t)(ctrl(channel, 0).cylinder_no & 0xff);
			ctrl(channel, 1).hob.lcyl = (uint8_t)(ctrl(channel, 1).cylinder_no & 0xff);
			ctrl(channel, 0).cylinder_no = (ctrl(channel, 0).cylinder_no & 0xff00) | _value;
			ctrl(channel, 1).cylinder_no = (ctrl(channel, 1).cylinder_no & 0xff00) | _value;
			PDEBUGF(LOG_V2, LOG_HDD, "cyl low   <- 0x%02x {%s}\n",
					_value, selected_type_string(channel));
			break;
		}
		case 0x05: // ATA Cylinder High, ATAPI Byte Count 8-15 (0x1f5)
		{
			ctrl(channel, 0).hob.hcyl = (uint8_t)(ctrl(channel, 0).cylinder_no >> 8);
			ctrl(channel, 1).hob.hcyl = (uint8_t)(ctrl(channel, 1).cylinder_no >> 8);
			ctrl(channel, 0).cylinder_no = (_value << 8) | (ctrl(channel, 0).cylinder_no & 0xff);
			ctrl(channel, 1).cylinder_no = (_value << 8) | (ctrl(channel, 1).cylinder_no & 0xff);
			PDEBUGF(LOG_V2, LOG_HDD, "cyl high  <- 0x%02x {%s} C=%d\n",
					_value, selected_type_string(channel), ctrl(channel, 0).cylinder_no);
			break;
		}

		case 0x06: // ATA Drive / Head Select, ATAPI Drive Select (0x1f6)
			// b7 Extended data field for ECC
			// b6/b5: Used to be sector size.  00=256,01=512,10=1024,11=128
			//   Since 512 was always used, bit 6 was taken to mean LBA mode:
			//     b6 1=LBA mode, 0=CHS mode
			//     b5 1
			// b4: DRV
			// b3..0 HD3..HD0
		{
			std::string value_str = bitfield_to_string(_value,
			{ "", "", "", "", "DRV1", "", "LBA", "" },
			{ "", "", "", "", "DRV0", "", "CHS", "" });
			bool lba_mode = (_value >> 6) & 1;
			if(lba_mode) {
				value_str += "LBA24-27=";
			} else {
				value_str += "H=";
			}
			PDEBUGF(LOG_V2, LOG_HDD, "drv head  <- 0x%02x %s%d\n",
					_value, value_str.c_str(), _value&0xf);
			if((_value & 0xa0) != 0xa0) { //b7 and b5 must be 1
				PDEBUGF(LOG_V2, LOG_HDD, "drv head not 1x1xxxxxb!\n");
			}
			m_channels[channel].drive_select = (_value >> 4) & 1;
			ctrl(channel, 0).head_no = _value & 0xf;
			ctrl(channel, 1).head_no = _value & 0xf;

			if(!controller->lba_mode && lba_mode) {
				PDEBUGF(LOG_V1, LOG_HDD, "now in LBA mode\n");
			} else if(controller->lba_mode && !lba_mode) {
				PDEBUGF(LOG_V1, LOG_HDD, "now in CHS mode\n");
			}
			ctrl(channel, 0).lba_mode = lba_mode;
			ctrl(channel, 1).lba_mode = lba_mode;
			if(!selected_is_present(channel)) {
				PDEBUGF(LOG_V2, LOG_HDD, "ATA%d: device set to %d which does not exist\n",
						channel, m_channels[channel].drive_select);
			}
			break;
		}
		case 0x07: // ATA Command (0x1f7)
		{
			uint8_t cmd = _value;

			PDEBUGF(LOG_V2, LOG_HDD, "command   <- 0x%02x\n", cmd);

			if(!selected_is_present(channel)) {
				PDEBUGF(LOG_V2, LOG_HDD, "%s: command issued to a not present drive\n",
						selected_string(channel));
				break;
			}

			// Writes to the command register clear the IRQ
			lower_interrupt(channel);

			if(controller->status.busy) {
				PDEBUGF(LOG_V0, LOG_HDD, "%s: command 0x%02x sent with controller BSY bit set\n",
						selected_string(channel), cmd);
				break;
			}
			if((cmd & 0xf0) == 0x10) {
				cmd = 0x10;
			}

			controller->status.busy = true;
			controller->status.err = false;
			controller->status.drive_ready = true;
			// TODO don't mess with DSC
			// controller->status.seek_complete = false;
			controller->status.drq = false;
			controller->status.corrected_data = false;
			controller->current_command = cmd;
			controller->error_register = 0;

			uint32_t exec_time = 0;
			auto command_fn = ms_ata_commands.find(cmd);
			if(command_fn != ms_ata_commands.end()) {
				if(cmd == 0xa0) {
					PDEBUGF(LOG_V2, LOG_HDD, "%s: cmd SEND ATAPI PACKET\n", selected_string(channel));
				} else {
					PDEBUGF(LOG_V1, LOG_HDD, "%s: cmd %s\n", selected_string(channel), command_fn->second.first);
				}
				exec_time = command_fn->second.second(*this, channel, cmd);
				if(!controller->status.err && exec_time>0) {
					activate_command_timer(channel, exec_time);
				}
			} else {
				PERRF(LOG_HDD, "%s: unknown ATA command 0x%02x (%d)\n", selected_string(channel), cmd, cmd);
				command_aborted(channel, cmd);
			}
			break;
		}
		case 0x16: // Device Control (0x3f6)
		{
			std::string value_str = bitfield_to_string(_value,
			{ "", "IRQ_DIS", "SRST", "", "", "", "", "" },
			{ "", "IRQ_EN", "", "", "", "", "", "" });
			PDEBUGF(LOG_V2, LOG_HDD, "adpt ctrl <- 0x%02x %s\n", _value, value_str.c_str());

			// (mch) Even if device 1 was selected, a write to this register
			// goes to device 0 (if device 1 is absent)
			prev_control_reset = controller->control.reset;
			m_channels[channel].drives[0].controller.control.reset       = _value & 0x04;
			m_channels[channel].drives[1].controller.control.reset       = _value & 0x04;
			m_channels[channel].drives[0].controller.control.disable_irq = _value & 0x02;
			m_channels[channel].drives[1].controller.control.disable_irq = _value & 0x02;

			if(!prev_control_reset && controller->control.reset) {
				// transition from 0 to 1 causes all drives to reset
				PDEBUGF(LOG_V2, LOG_HDD, "Enter RESET mode\n");

				// (mch) Set BSY, drive not ready
				for(int id = 0; id < 2; id++) {
					ctrl(channel,id).status.busy           = true;
					ctrl(channel,id).status.drive_ready    = false;
					ctrl(channel,id).reset_in_progress     = true;

					ctrl(channel,id).status.write_fault    = false;
					ctrl(channel,id).status.drq            = false;
					ctrl(channel,id).status.corrected_data = false;
					ctrl(channel,id).status.err            = false;

					if(is_cd(channel,id)) {
						// ATAPI: If an Immediate Mode command is executing when the devices
						// is issued an SRST the DSC bit shall not be cleared with the rest
						// of the status register. Instead the functionality of the DSC bit
						// shall be maintained.
						if(drive(channel,id).atapi.seek_completion_time == 0) {
							ctrl(channel,id).status.seek_complete = true;
						}
					} else {
						ctrl(channel,id).status.seek_complete  = true;
					}

					ctrl(channel,id).error_register = 0x01; // diagnostic code: no error

					ctrl(channel,id).current_command = 0x00;
					ctrl(channel,id).buffer_index = 0;

					ctrl(channel,id).multiple_sectors  = 0;
					ctrl(channel,id).lba_mode          = false;

					ctrl(channel,id).control.disable_irq = false;
					lower_interrupt(channel);
				}
			} else if(controller->reset_in_progress && !controller->control.reset) {
				// Clear BSY and DRDY
				PDEBUGF(LOG_V2, LOG_HDD, "Reset complete {%s}\n", selected_type_string(channel));
				for(int id = 0; id < 2; id++) {
					ctrl(channel,id).status.busy           = false;
					ctrl(channel,id).status.drive_ready    = true;
					ctrl(channel,id).reset_in_progress     = false;

					set_signature(channel, id);
				}
			}
			PDEBUGF(LOG_V2, LOG_HDD, "ATA%d: %s IRQ\n", channel,
					(controller->control.disable_irq) ? "disable" : "enable");
			break;
		}
		default:
			PDEBUGF(LOG_V0, LOG_HDD, "invalid address <- 0x%02x\n", _value);
			break;
	}

	update_busy_status();
}

void StorageCtrl_ATA::update_busy_status()
{
	bool busy = false;
	for(int channel=0; channel<ATA_MAX_CHANNEL; channel++) {
		for(int drive=0; drive<2; drive++) {
			if(is_hdd(channel, drive)) {
				busy = busy || m_channels[channel].drives[drive].controller.status.busy;
			}
		}
	}
	m_busy = busy;
}

void StorageCtrl_ATA::identify_atapi_device(int _ch)
{
	Drive & drive = selected_drive(_ch);
	StorageDev & storage = selected_storage(_ch);

	memset(&drive.id_drive, 0, 512);

	// Values valid for CD-ROM

	// Word 0: General Configuration
	// Removable CD-ROM, Interrupt DRQ, 12 byte packets
	drive.id_drive[0] = 
		(1 << 15) | // 15-14: Protocol Type (10b = ATAPI)
		(5 << 8)  | // 12-8: Device type (05h = CD-ROM device)
		(1 << 7)  | // 7: Removable
		(1 << 5)  | // 6-5: CMD DRQ Type (01b = Interrupt DRQ)
		(0 << 0);   // 1-0: Command Packet Size (00b = 12 bytes, CD-ROM)

	int i;

	// Word 10-19: Serial Number
	for(i = 0; i < 10; i++) {
		drive.id_drive[10+i] = (storage.serial()[i*2] << 8) | storage.serial()[i*2 + 1];
	}

	// Word 23-26: Firmware revision
	for(i = 0; i < 4; i++) {
		drive.id_drive[23+i] = (storage.firmware()[i*2] << 8) | storage.firmware()[i*2 + 1];
	}
	assert(23+i == 27);

	// Word 27-46: Model number
	for(i = 0; i < 20; i++) {
		drive.id_drive[27+i] = (storage.model()[i*2] << 8) | storage.model()[i*2 + 1];
	}
	assert(27+i == 47);

	// Word 49: Capabilities
	drive.id_drive[49] =
		(0 << 8)  | // DMA supported
		(1 << 9)  | // LBA supported (mandatory)
		(0 << 10) | // IORDY can be disabled
		(0 << 11) | // IORDY Supported
		(0 << 13);  // Overlap Operation Supported

	// Word 51: PIO Cycle Timing
	drive.id_drive[51] = 0; // 0 = default

	// Word 52: DMA Cycle Timing
	drive.id_drive[52] = 0; // 0 = default

	// Word 53: Field Validity
	drive.id_drive[53] =
		(1 << 0) | // Fields in words 54-58 valid
		(1 << 1);  // Fields in words 64-70 valid

	// Word 62: Single Word DMA Transfer
	drive.id_drive[62] = 0;

	// Word 63: Multi Word DMA Transfer
	drive.id_drive[63] = 0;

	// Word 64: Enhanced PIO Mode 
	drive.id_drive[64] = 1; // PIO Mode 3 supported

	// Word 65: Minimum Multiword DMA transfer cycle time per word (ns)
	drive.id_drive[65] = 180;

	// Word 66: Manufacturer’s Recommended Multiword DMA Transfer Cycle Time (ns)
	drive.id_drive[66] = 180;

	// Word 67: Minimum PIO transfer cycle time without flow control
	drive.id_drive[67] = 300;

	// Word 68: Minimum PIO Transfer Cycle Time with IORDY Flow Control
	drive.id_drive[68] = 180;

	// Word 71: Typical time (μs) for release when processing an overlapped command
	drive.id_drive[71] = 30; // faked

	// Word 72: Typical time (μs) for release after receiving the service command
	drive.id_drive[72] = 30; // faked

	// Word 73-74: ATAPI Major-Minor Version Number 
	drive.id_drive[73] = 0x6; // ATAPI 1.x and 2.x (bit 1 and 2)
	drive.id_drive[74] = 0x9; // SFF-8020i (according to spec)

	drive.identify_set = true;
}

void StorageCtrl_ATA::identify_ata_device(int _ch)
{
	Drive & drive = selected_drive(_ch);
	StorageDev & storage = selected_storage(_ch);
	const MediaGeometry & geometry = storage.geometry();

	memset(&drive.id_drive, 0, 512);

	// Identify Drive command return values definition
	//
	// This code is rehashed from some that was donated.
	// I'm using ANSI X3.221-1994, AT Attachment Interface for Disk Drives
	// and X3T10 2008D Working Draft for ATA-3


	// Word 0: general config bit-significant info
	//   Note: bits 1-5 and 8-14 are ATA-1 only
	//   bit 15: 0=ATA device
	//   bit 14: 1=format speed tolerance gap required
	//   bit 13: 1=track offset option available
	//   bit 12: 1=data strobe offset option available
	//   bit 11: 1=rotational speed tolerance is > 0,5% (typo?)
	//   bit 10: 1=disk transfer rate > 10Mbs
	//   bit  9: 1=disk transfer rate > 5Mbs but <= 10Mbs
	//   bit  8: 1=disk transfer rate <= 5Mbs
	//   bit  7: 1=removable cartridge drive
	//   bit  6: 1=fixed drive
	//   bit  5: 1=spindle motor control option implemented
	//   bit  4: 1=head switch time > 15 usec
	//   bit  3: 1=not MFM encoded
	//   bit  2: 1=soft sectored
	//   bit  1: 1=hard sectored
	//   bit  0: 0=reserved
	drive.id_drive[0] = 0x0040;

	// Word 1: number of user-addressable cylinders in
	//   default translation mode.  If the value in words 60-61
	//   exceed 16,515,072, this word shall contain 16,383.
	if(geometry.cylinders > 16383) {
		drive.id_drive[1] = 16383;
	} else {
		drive.id_drive[1] = geometry.cylinders;
	}

	// Word 2: reserved

	// Word 3: number of user-addressable heads in default
	//   translation mode
	drive.id_drive[3] = geometry.heads;

	// Word 4: # unformatted bytes per translated track in default xlate mode
	// Word 5: # unformatted bytes per sector in default xlated mode
	// Word 6: # user-addressable sectors per track in default xlate mode
	// Note: words 4,5 are ATA-1 only
	drive.id_drive[4] = (512 * geometry.spt);
	drive.id_drive[5] = 512;
	drive.id_drive[6] = geometry.spt;

	// Word 7-9: Vendor specific

	// Word 10-19: Serial number (20 ASCII characters, 0000h=not specified)
	// This field is right justified and padded with spaces (20h).
	for(int i = 0; i < 10; i++) {
		drive.id_drive[10+i] = (storage.serial()[i*2] << 8) | storage.serial()[i*2 + 1];
	}

	// Word 20: buffer type
	//          0000h = not specified
	//          0001h = single ported single sector buffer which is
	//                  not capable of simulataneous data xfers to/from
	//                  the host and the disk.
	//          0002h = dual ported multi-sector buffer capable of
	//                  simulatenous data xfers to/from the host and disk.
	//          0003h = dual ported multi-sector buffer capable of
	//                  simulatenous data xfers with a read caching
	//                  capability.
	//          0004h-ffffh = reserved
	drive.id_drive[20] = 3;

	// Word 21: buffer size in 512 byte increments, 0000h = not specified
	drive.id_drive[21] = 512; // 512 Sectors = 256kB cache

	// Word 22: # of ECC bytes available on read/write long cmds
	//          0000h = not specified
	drive.id_drive[22] = 4;

	// Word 23..26: Firmware revision (8 ASCII chars, 0000h=not specified)
	// This field is left justified and padded with spaces (20h)
	for(int i=23; i<=26; i++) {
		drive.id_drive[i] = 0;
	}

	// Word 27..46: Model number (40 ASCII chars, 0000h=not specified)
	// This field is left justified and padded with spaces (20h)
	for(int i=0; i<20; i++) {
		drive.id_drive[27+i] =
				(storage.model()[i*2] << 8) |
				 storage.model()[i*2 + 1];
	}

	// Word 47: 15-8 Vendor unique
	//           7-0 00h= read/write multiple commands not implemented
	//               xxh= maximum # of sectors that can be transferred
	//                    per interrupt on read and write multiple commands
	drive.id_drive[47] = ATA_MAX_MULTIPLE_SECTORS;

	// Word 48: 0000h = cannot perform dword IO
	//          0001h = can    perform dword IO
	drive.id_drive[48] = 1;

	// Word 49: Capabilities
	//   15-10: 0 = reserved
	//       9: 1 = LBA supported
	//       8: 1 = DMA supported
	//     7-0: Vendor unique
	drive.id_drive[49] = 1<<9;


	// Word 50: Reserved

	// Word 51: 15-8 PIO data transfer cycle timing mode
	//           7-0 Vendor unique
	drive.id_drive[51] = 0x200;

	// Word 52: 15-8 DMA data transfer cycle timing mode
	//           7-0 Vendor unique
	drive.id_drive[52] = 0x200;

	// Word 53: 15-1 Reserved
	//             2 1=the fields reported in word 88 are valid
	//             1 1=the fields reported in words 64-70 are valid
	//             0 1=the fields reported in words 54-58 are valid
	drive.id_drive[53] = 0x07;

	// Word 54: # of user-addressable cylinders in curr xlate mode
	// Word 55: # of user-addressable heads in curr xlate mode
	// Word 56: # of user-addressable sectors/track in curr xlate mode
	if(geometry.cylinders > 16383) {
		drive.id_drive[54] = 16383;
	} else {
		drive.id_drive[54] = geometry.cylinders;
	}
	drive.id_drive[55] = geometry.heads;
	drive.id_drive[56] = geometry.spt;

	// Word 57-58: Current capacity in sectors
	// Excludes all sectors used for device specific purposes.
	drive.id_drive[57] = (storage.sectors() & 0xffff); // LSW
	drive.id_drive[58] = (storage.sectors() >> 16);    // MSW

	// Word 59: 15-9 Reserved
	//             8 1=multiple sector setting is valid
	//           7-0 current setting for number of sectors that can be
	//               transferred per interrupt on R/W multiple commands
	if(selected_ctrl(_ch).multiple_sectors > 0) {
		drive.id_drive[59] = 0x0100 | selected_ctrl(_ch).multiple_sectors;
	} else {
		drive.id_drive[59] = 0x0000;
	}

	// Word 60-61:
	// If drive supports LBA Mode, these words reflect total # of user
	// addressable sectors.  This value does not depend on the current
	// drive geometry.  If the drive does not support LBA mode, these
	// words shall be set to 0.
	drive.id_drive[60] = drive.id_drive[57]; // LSW
	drive.id_drive[61] = drive.id_drive[58]; // MSW

	// Word 62: 15-8 single word DMA transfer mode active
	//           7-0 single word DMA transfer modes supported
	// The low order byte identifies by bit, all the Modes which are
	// supported e.g., if Mode 0 is supported bit 0 is set.
	// The high order byte contains a single bit set to indiciate
	// which mode is active.
	drive.id_drive[62] = 0x0;

	// Word 63: 15-8 multiword DMA transfer mode active
	//           7-0 multiword DMA transfer modes supported
	// The low order byte identifies by bit, all the Modes which are
	// supported e.g., if Mode 0 is supported bit 0 is set.
	// The high order byte contains a single bit set to indiciate
	// which mode is active.
	drive.id_drive[63] = 0x0;

	if(ATA_VERSION >= 2) {
		// Word 64 PIO modes supported
		drive.id_drive[64] = 0x00;

		// Word 65-68 PIO/DMA cycle time (nanoseconds)
		for(int i=65; i<=68; i++) {
			drive.id_drive[i] = 120;
		}
	}
	if(ATA_VERSION >= 3) {
		// Word 69-79 Reserved

		// Word 80: 15-5 reserved
		//             6 supports ATA/ATAPI-6
		//             5 supports ATA/ATAPI-5
		//             4 supports ATA/ATAPI-4
		//             3 supports ATA-3
		//             2 supports ATA-2
		//             1 supports ATA-1
		//             0 reserved
		for(int i=1; i<=ATA_VERSION; i++) {
			drive.id_drive[80] |= (1<<i);
		}

		// Word 81: Minor version number
		drive.id_drive[81] = 0x00;

		// Word 82: Command set supported.
		//          15 obsolete
		//          14 NOP command supported
		//          13 READ BUFFER command supported
		//          12 WRITE BUFFER command supported
		//          11 obsolete
		//          10 Host protected area feature set supported
		//           9 DEVICE RESET command supported
		//           8 SERVICE interrupt supported
		//           7 release interrupt supported
		//           6 look-ahead supported
		//           5 write cache supported
		//           4 supports PACKET command feature set
		//           3 supports power management feature set
		//           2 supports removable media feature set
		//           1 supports securite mode feature set
		//           0 support SMART feature set
		drive.id_drive[82] = 1 << 14;

		// Word 83: 15 shall be ZERO
		//          14 shall be ONE
		//          13 FLUSH CACHE EXT command supported
		//          12 FLUSH CACHE command supported
		//          11 Device configuration overlay supported
		//          10 48-bit Address feature set supported
		//           9 Automatic acoustic management supported
		//           8 SET MAX security supported
		//           7 reserved for 1407DT PARTIES
		//           6 SetF sub-command Power-Up supported
		//           5 Power-Up in standby feature set supported
		//           4 Removable media notification supported
		//           3 APM feature set supported
		//           2 CFA feature set supported
		//           1 READ/WRITE DMA QUEUED commands supported
		//           0 Download MicroCode supported
		drive.id_drive[83] = (1 << 14) | (1 << 13) | (1 << 12) | (1 << 10);
	}
	if(ATA_VERSION >= 4) {
		// Word 84: Command set/feature supported extension.
		//          14 Shall be set to one
		drive.id_drive[84] = 1 << 14;

		// Word 85: Command set/feature enabled. See Word 82.
		//          14 1=NOP command supported
		drive.id_drive[85] = 1 << 14;

		// Word 86: Command set/feature enabled.
		//          15 shall be ZERO
		//          14 shall be ONE
		//          13 FLUSH CACHE EXT command enabled
		//          12 FLUSH CACHE command enabled
		//          11 Device configuration overlay enabled
		//          10 48-bit Address feature set enabled
		//           9 Automatic acoustic management enabled
		//           8 SET MAX security enabled
		//           7 reserved for 1407DT PARTIES
		//           6 SetF sub-command Power-Up enabled
		//           5 Power-Up in standby feature set enabled
		//           4 Removable media notification enabled
		//           3 APM feature set enabled
		//           2 CFA feature set enabled
		//           1 READ/WRITE DMA QUEUED commands enabled
		//           0 Download MicroCode enabled
		drive.id_drive[86] = (1 << 14) | (1 << 13) | (1 << 12) | (1 << 10);

		// Word 87: Command set/feature default.
		//          15 Shall be cleared to zero
		//          14 Shall be set to one
		//          13-0 Reserved
		drive.id_drive[87] = 1 << 14;

		// Word 88: 15-13 Reserved
		//          12 1 = Ultra DMA mode 4 is selected
		//             0 = Ultra DMA mode 4 is not selected
		//          11 1 = Ultra DMA mode 3 is selected
		//             0 = Ultra DMA mode 3 is not selected
		//          10 1 = Ultra DMA mode 2 is selected
		//             0 = Ultra DMA mode 2 is not selected
		//          9  1 = Ultra DMA mode 1 is selected
		//             0 = Ultra DMA mode 1 is not selected
		//          8  1 = Ultra DMA mode 0 is selected
		//             0 = Ultra DMA mode 0 is not selected
		//          7-5 Reserved
		//          4  1 = Ultra DMA mode 4 and below are supported
		//          3  1 = Ultra DMA mode 3 and below are supported
		//          2  1 = Ultra DMA mode 2 and below are supported
		//          1  1 = Ultra DMA mode 1 and below are supported
		//          0  1 = Ultra DMA mode 0 is supported
		drive.id_drive[88] = 0x0;
	}
	if(ATA_VERSION >= 5) {
		// Word 93: Hardware reset result.
		// The contents of bits 12-0 of this word shall change only during
		// the execution of a hardware reset.
		// 14 Shall be set to one.
		// 13  1 = device detected CBLID- above V iH
		//     0 = device detected CBLID- below V iL
		// 0  Shall be set to one.
		drive.id_drive[93] = 1 | (1 << 14) | 0x2000;
	}
	if(ATA_VERSION >= 6) {
		// Word 100-103: 48-bit total number of sectors
		drive.id_drive[100] = (uint16_t)(storage.sectors() & 0xffff);
		drive.id_drive[101] = (uint16_t)(storage.sectors() >> 16);
		drive.id_drive[102] = (uint16_t)(storage.sectors() >> 32);
		drive.id_drive[103] = (uint16_t)(storage.sectors() >> 48);
	}

	// Word 128-159 Vendor unique
	// Word 160-255 Reserved

	drive.identify_set = true;
}

uint32_t StorageCtrl_ATA::ata_cmd_calibrate_drive(int _ch, uint8_t _cmd)
{
	if(!selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V2, LOG_HDD, "%s %s: issued to non-disk\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	if(!selected_is_present(_ch)) {
		controller.error_register = 0x02; // Track 0 not found
		controller.status.busy = false;
		controller.status.drive_ready = true;
		controller.status.seek_complete = false;
		controller.status.drq = false;
		controller.status.err = true;
		raise_interrupt(_ch);
		PDEBUGF(LOG_V2, LOG_HDD, "%s %s: disk not present\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		return 0;
	}
	/* move head to cylinder 0, issue IRQ */
	selected_drive(_ch).next_lba = 0;
	controller.cylinder_no = 0;

	uint32_t seek_time = seek(_ch, g_machine.get_virt_time_us()+CALIB_CMD_US);

	return (CALIB_CMD_US + seek_time);
}

uint32_t StorageCtrl_ATA::ata_cmd_read_sectors(int _ch, uint8_t _cmd)
{
	/* update sector_no, always points to current sector
	 * after each sector is read to buffer, DRQ bit set and issue IRQ
	 * if interrupt handler transfers all data words into main memory,
	 * and more sectors to read, then set BSY bit again, clear DRQ and
	 * read next sector into buffer
	 * sector count of 0 means 256 sectors
	 */
	if(!selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to non-disk\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	// Win98 accesses 0/0/0 in CHS mode
	if(!controller.lba_mode &&
	   !controller.head_no &&
	   !controller.cylinder_no &&
	   !controller.sector_no)
	{
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: read from 0/0/0, aborting command\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	bool lba48 = false;
	if(_cmd == 0x24 || _cmd == 0x29) {
		//READ EXT
		lba48 = true;
	}
	lba48_transform(controller, lba48);

	int64_t logical_sector = calculate_logical_address(_ch);
	if(logical_sector < 0) {
		command_aborted(_ch, _cmd);
		return 0;
	}
	selected_drive(_ch).next_lba = logical_sector;

	PDEBUGF(LOG_V1, LOG_HDD, "%s %s: reading %d sector(s) at lba=%lld (%dB)\n",
			selected_string(_ch), ata_cmd_string(_cmd), controller.num_sectors, logical_sector,
			controller.num_sectors*512);

	uint32_t cmd_time = DEFAULT_CMD_US + CTRL_OVERH_US;
	uint32_t exec_time = ata_read_next_block(_ch, cmd_time);

	return exec_time;
}

uint32_t StorageCtrl_ATA::ata_read_next_block(int _ch, uint32_t _cmd_time)
{
	Controller &ctrl = selected_ctrl(_ch);

	unsigned xfer_amount = 1;
	if((ctrl.current_command == 0xC4) || (ctrl.current_command == 0x29)) {
		// READ MULTIPLE
		if(ctrl.multiple_sectors == 0) {
			command_aborted(_ch, ctrl.current_command);
			return 0;
		}
		if(ctrl.num_sectors > ctrl.multiple_sectors) {
			xfer_amount = ctrl.multiple_sectors;
		} else {
			xfer_amount = ctrl.num_sectors;
		}
	}
	ctrl.buffer_size = xfer_amount * 512;
	ctrl.buffer_index = 0;

	uint64_t now = g_machine.get_virt_time_us() + _cmd_time;
	/* If the drive is not already on the desired track, an implied seek is
	 * performed.
	 */
	int64_t curr_cyl = selected_storage(_ch).lba_to_cylinder(selected_drive(_ch).curr_lba);
	uint32_t seek_time = seek(_ch, now);

	// transfer_time_us includes rotational latency and read time
	uint32_t xfer_time = selected_storage(_ch).transfer_time_us(
			now + seek_time,
			selected_drive(_ch).next_lba,
			xfer_amount,
			ctrl.look_ahead_time
			);
	uint32_t exec_time = _cmd_time + seek_time + xfer_time;

#ifndef NDEBUG
	int64_t c0,h0,s0,c1,h1,s1;
	selected_storage(_ch).lba_to_chs(selected_drive(_ch).next_lba, c0,h0,s0);
	selected_storage(_ch).lba_to_chs(selected_drive(_ch).next_lba+xfer_amount, c1,h1,s1);
	double hpos = selected_storage(_ch).head_position(g_machine.get_virt_time_us());
	PDEBUGF(LOG_V2, LOG_HDD, "read %lld/%lld/%lld->%lld/%lld/%lld (%u), hw sect:%d->%d, current=%lld/%lld/%.2f, seek:%u, tx:%u\n",
			c0,h0,s0, c1,h1,s1, xfer_amount,
			selected_storage(_ch).chs_to_hw_sector(s0),
			selected_storage(_ch).chs_to_hw_sector(s1),
			curr_cyl,
			selected_storage(_ch).lba_to_head(selected_drive(_ch).curr_lba),
			selected_storage(_ch).pos_to_hw_sect(hpos),
			seek_time, xfer_time);
#endif

	try {
		// the following function updates the value of next_lsector
		// it will point to the first sector of the next block transfer
		ata_tx_sectors(_ch, false, ctrl.buffer, ctrl.buffer_size);
	} catch(std::exception &) {
		command_aborted(_ch, ctrl.current_command);
		return 0;
	}
	return exec_time;
}

uint32_t StorageCtrl_ATA::ata_cmd_read_verify_sectors(int _ch, uint8_t _cmd)
{
	if(!selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to non-disk\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	bool lba48 = false;
	if(_cmd == 0x42) {
		// READ EXT
		lba48 = true;
	}
	lba48_transform(controller, lba48);

	int64_t logical_sector = calculate_logical_address(_ch);
	if(logical_sector < 0) {
		command_aborted(_ch, _cmd);
		return 0;
	}

	assert(controller.num_sectors <= 256);
	selected_drive(_ch).next_lba = logical_sector;

	uint32_t cmd_time = DEFAULT_CMD_US + CTRL_OVERH_US;
	uint64_t now = g_machine.get_virt_time_us() + cmd_time;
	uint32_t seek_time = seek(_ch, now);
	uint32_t read_time = selected_storage(_ch).transfer_time_us(
			now + seek_time,
			logical_sector,
			controller.num_sectors,
			controller.look_ahead_time
			);

	return (cmd_time + seek_time + read_time);
}

uint32_t StorageCtrl_ATA::ata_cmd_write_sectors(int _ch, uint8_t _cmd)
{
	/* update sector_no, always points to current sector
	 * after each sector is read to buffer, DRQ bit set and issue IRQ
	 * if interrupt handler transfers all data words into main memory,
	 * and more sectors to read, then set BSY bit again, clear DRQ and
	 * read next sector into buffer
	 * sector count of 0 means 256 sectors
	 */
	if(!selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to non-disk\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	bool lba48 = false;
	if(_cmd == 0x34 || _cmd == 0x39) {
		//WRITE EXT
		lba48 = true;
	}
	lba48_transform(controller, lba48);

	unsigned xfer_amount = 1;
	if((_cmd == 0xC5) || (_cmd ==0x39)) {
		//WRITE MULTIPLE
		if(controller.multiple_sectors == 0) {
			command_aborted(_ch, _cmd);
			return 0;
		}
		if(controller.num_sectors > controller.multiple_sectors) {
			xfer_amount = controller.multiple_sectors;
		} else {
			xfer_amount = controller.num_sectors;
		}
	}
	controller.buffer_size = xfer_amount * 512;
	controller.buffer_index = 0;

	int64_t logical_sector = calculate_logical_address(_ch);
	if(logical_sector < 0) {
		command_aborted(_ch, _cmd);
		return 0;
	}

	selected_drive(_ch).next_lba = logical_sector;

	PDEBUGF(LOG_V1, LOG_HDD, "%s %s: writing %d sector(s) at lba=%lld (%dB)\n",
			selected_string(_ch), ata_cmd_string(_cmd), controller.sector_count,
			logical_sector, controller.sector_count*512);

	uint32_t cmd_time = DEFAULT_CMD_US + CTRL_OVERH_US;
	uint32_t seek_time = seek(_ch, g_machine.get_virt_time_us());

	return (cmd_time+seek_time);
}

void StorageCtrl_ATA::ata_write_next_block(int _ch)
{
	Controller &controller = selected_ctrl(_ch);

	assert(controller.num_sectors);

	unsigned xfer_amount = 1;
	if((controller.current_command == 0xC5) || (controller.current_command == 0x39)) {
		// WRITE MULTIPLE
		if(controller.multiple_sectors == 0) {
			command_aborted(_ch, controller.current_command);
			return;
		}
		if(controller.num_sectors > controller.multiple_sectors) {
			xfer_amount = controller.multiple_sectors;
		} else {
			xfer_amount = controller.num_sectors;
		}
	}
	controller.buffer_size = xfer_amount * 512;
	controller.buffer_index = 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_execute_device_diagnostic(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);
	set_signature(_ch, slave_is_selected(_ch));

	return (DEFAULT_CMD_US + CTRL_OVERH_US);
}

uint32_t StorageCtrl_ATA::ata_cmd_initialize_drive_parameters(int _ch, uint8_t _cmd)
{
	if(!selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to non-disk\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	// sets logical geometry of specified drive
	PDEBUGF(LOG_V2, LOG_HDD, "%s %s: sec=%u, drive sel=%u, head=%u\n",
			selected_string(_ch), ata_cmd_string(_cmd),
			controller.sector_count,
			m_channels[_ch].drive_select,
			controller.head_no);
	if(!selected_is_present(_ch)) {
		PERRF(LOG_HDD, "%s %s: disk not present\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	if(controller.sector_count != selected_storage(_ch).geometry().spt) {
		PERRF(LOG_HDD, "%s %s: logical sector count %d not supported\n",
				selected_string(_ch), ata_cmd_string(_cmd),
				controller.sector_count);
		command_aborted(_ch, _cmd);
		return 0;
	}
	if(controller.head_no == 0) {
		// Linux 2.6.x kernels use this value and don't like aborting here
		PERRF(LOG_HDD, "%s %s: max. logical head number 0 not supported\n",
				selected_string(_ch), ata_cmd_string(_cmd));
	} else if(controller.head_no != (selected_storage(_ch).geometry().heads-1)) {
		PERRF(LOG_HDD, "%s %s: max. logical head number %d not supported\n",
				selected_string(_ch), ata_cmd_string(_cmd),
				controller.head_no);
		command_aborted(_ch, _cmd);
		return 0;
	}

	command_successful(_ch, m_channels[_ch].drive_select, true);
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_identify_device(int _ch, uint8_t _cmd)
{
	if(!selected_is_present(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: disk not present, aborting\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	if(selected_is_cd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: selected is not HDD, aborting\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		set_signature(_ch, slave_is_selected(_ch));
		command_aborted(_ch, _cmd);
		return 0;
	}

	// See ATA/ATAPI-4, 8.12
	if(!selected_drive(_ch).identify_set) {
		identify_ata_device(_ch);
	}
	// now convert the id_drive array (native 256 word format) to
	// the controller buffer (512 bytes)
	for(int i=0; i<=255; i++) {
		uint16_t temp16 = selected_drive(_ch).id_drive[i];
		controller.buffer[i*2] = temp16 & 0x00ff;
		controller.buffer[i*2+1] = temp16 >> 8;
	}
	command_successful(_ch, m_channels[_ch].drive_select, true);
	controller.status.drq = true;
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_set_features(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);
	switch(controller.features) {
		case 0x03: // Set Transfer Mode
		{
			selected_drive(_ch).identify_set = 0;
			uint8_t type = (controller.sector_count >> 3);
			uint8_t mode = controller.sector_count & 0x07;
			//Singleword DMA, Multiword DMA, UDMA, are PCI Busmastering DMA types.
			// This implementation does not support BMDMA.
			// See Bochs sources to expand it when/if it will be necessary.
			// It will be necessary only for Pentium class PS/1's
			switch (type) {
				case 0x00: // PIO default
				case 0x01: // PIO mode
					PDEBUGF(LOG_V1, LOG_HDD, "%s %s: set transfer mode to PIO\n",
							selected_string(_ch), ata_cmd_string(_cmd));
					controller.mdma_mode = 0x00;
					controller.udma_mode = 0x00;
					break;
				case 0x04: // MDMA mode
					PDEBUGF(LOG_V1, LOG_HDD, "%s %s: set transfer mode to MDMA%d\n",
							selected_string(_ch), ata_cmd_string(_cmd), mode);
					controller.mdma_mode = (1 << mode);
					controller.udma_mode = 0x00;
					break;
				case 0x08: // UDMA mode
					PDEBUGF(LOG_V1, LOG_HDD, "%s %s: set transfer mode to UDMA%d\n",
							selected_string(_ch), ata_cmd_string(_cmd), mode);
					controller.mdma_mode = 0x00;
					controller.udma_mode = (1 << mode);
					break;
				default:
					PERRF(LOG_HDD, "%s %s: unknown transfer mode type 0x%02x\n",
							selected_string(_ch), ata_cmd_string(_cmd), type);
					command_aborted(_ch, _cmd);
					return 0;
			}
			break;
		}
		case 0x02: // Enable and
		case 0x82: //  Disable write cache.
		case 0xAA: // Enable and
		case 0x55: //  Disable look-ahead cache.
		case 0xCC: // Enable and
		case 0x66: //  Disable reverting to power-on default
		{
			PDEBUGF(LOG_V1, LOG_HDD, "%s %s: subcommand 0x%02x not supported, but returning success\n",
					selected_string(_ch), ata_cmd_string(_cmd), controller.features);
			break;
		}
		default:
		{
			PERRF(LOG_HDD, "%s %s: unknown subcommand: 0x%02x\n",
					selected_string(_ch), ata_cmd_string(_cmd), controller.features);
			command_aborted(_ch, _cmd);
			return 0;
		}
	}
	command_successful(_ch, m_channels[_ch].drive_select, true);
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_set_multiple_mode(int _ch, uint8_t _cmd)
{
	if(!selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to non-disk\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	if((controller.sector_count > ATA_MAX_MULTIPLE_SECTORS) ||
			((controller.sector_count & (controller.sector_count - 1)) != 0) ||
			(controller.sector_count == 0))
	{
		command_aborted(_ch, _cmd);
		return 0;
	}

	controller.multiple_sectors = controller.sector_count;
	command_successful(_ch, m_channels[_ch].drive_select, true);
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_identify_packet_device(int _ch, uint8_t _cmd)
{
	if(selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to HDD\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	if(!selected_drive(_ch).identify_set) {
		identify_atapi_device(_ch);
	}
	// now convert the id_drive array (native 256 word format) to
	// the controller buffer (512 bytes)
	for(int i = 0; i <= 255; i++) {
		uint16_t temp16 = selected_drive(_ch).id_drive[i];
		controller.buffer[i*2] = temp16 & 0x00ff;
		controller.buffer[i*2+1] = temp16 >> 8;
	}
	command_successful(_ch, m_channels[_ch].drive_select, true);
	controller.status.drq = true;
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_device_reset(int _ch, uint8_t _cmd)
{
	if(selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to HDD\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	set_signature(_ch, m_channels[_ch].drive_select);
	command_successful(_ch, m_channels[_ch].drive_select, false);
	selected_ctrl(_ch).error_register &= ~(1 << 7);
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_send_packet(int _ch, uint8_t _cmd)
{
	if(selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to HDD\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}

	// PACKET
	Controller &controller = selected_ctrl(_ch);

	controller.packet_dma = (controller.features & 1);
	if(controller.features & (1 << 1)) {
		PERRF(LOG_HDD, "%s %s: PACKET-overlapped not supported\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	if(controller.packet_dma) {
		PERRF(LOG_HDD, "%s %s: PACKET DMA not supported\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}

	controller.sector_count = 1;

	selected_drive(_ch).atapi.command = 0;

	controller.buffer_index = 0;
	controller.buffer_size = ATAPI_PACKET_SIZE;

	command_successful(_ch, m_channels[_ch].drive_select, false);
	atapi_ready_to_transfer(_ch, ATAPI_INT_CMD, ATAPI_INT_TO_DEV);

	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_power_stubs(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);
	command_successful(_ch, m_channels[_ch].drive_select, true);
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_check_power_mode(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);
	command_successful(_ch, m_channels[_ch].drive_select, true);
	selected_ctrl(_ch).sector_count = 0xff; // Active or Idle mode
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_seek(int _ch, uint8_t _cmd)
{
	if(!selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V2, LOG_HDD, "%s %s: not supported for non-disk\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}

	int64_t logical_sector = calculate_logical_address(_ch);
	if(logical_sector < 0) {
		command_aborted(_ch, _cmd);
		return 0;
	}
	selected_drive(_ch).next_lba = logical_sector;

	uint32_t seek_time = seek(_ch, g_machine.get_virt_time_us() + CTRL_OVERH_US);
	return (seek_time + CTRL_OVERH_US);
}

uint32_t StorageCtrl_ATA::ata_cmd_read_native_max_address(int _ch, uint8_t _cmd)
{
	if(!selected_is_hdd(_ch)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s %s: issued to non-disk\n",
				selected_string(_ch), ata_cmd_string(_cmd));
		command_aborted(_ch, _cmd);
		return 0;
	}
	Controller &controller = selected_ctrl(_ch);
	if(!controller.lba_mode) {
		command_aborted(_ch, _cmd);
		return 0;
	}
	bool lba48 = false;
	if(_cmd == 0x27) {
		// READ EXT
		lba48 = true;
	}

	lba48_transform(controller, lba48);
	int64_t max_sector = selected_storage(_ch).sectors() - 1;
	if(!controller.lba48) {
		controller.head_no = (uint8_t)((max_sector >> 24) & 0xf);
		controller.cylinder_no = (uint16_t)((max_sector >> 8) & 0xffff);
		controller.sector_no = (uint8_t)((max_sector) & 0xff);
	} else {
		controller.hob.hcyl = (uint8_t)((max_sector >> 40) & 0xff);
		controller.hob.lcyl = (uint8_t)((max_sector >> 32) & 0xff);
		controller.hob.sector = (uint8_t)((max_sector >> 24) & 0xff);
		controller.cylinder_no = (uint16_t)((max_sector >> 8) & 0xffff);
		controller.sector_no = (uint8_t)((max_sector) & 0xff);
	}
	command_successful(_ch, m_channels[_ch].drive_select, true);
	return 0;
}

uint32_t StorageCtrl_ATA::ata_cmd_not_implemented(int _ch, uint8_t _cmd)
{
	PERRF(LOG_HDD, "%s %s: not implemented\n", selected_string(_ch), ata_cmd_string(_cmd));
	command_aborted(_ch, _cmd);
	return 0;
}

bool StorageCtrl_ATA::atapi_init_receive(int _ch, int _tx_len)
{
	Controller &controller = selected_ctrl(_ch);
	controller.byte_count = _tx_len;
	if(controller.byte_count > sizeof(controller.buffer)) {
		controller.byte_count = sizeof(controller.buffer);
	}
	controller.buffer_index = 0;
	controller.buffer_size = controller.byte_count;
	if(controller.byte_count < _tx_len) {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: data receive: multiple DRQ transfers not supported.\n");
		return false;
	}
	return true;
}

void StorageCtrl_ATA::atapi_init_send(int _ch, uint8_t _cmd, int _req_len, int _alloc_len)
{
	Controller &controller = selected_ctrl(_ch);

	// byte_count is a union of cylinder_no;
	// lazy is used to force a data read in the buffer at the next read.

	if(controller.byte_count == 0xffff) {
		controller.byte_count = 0xfffe;
	}

	if((controller.byte_count & 1) && !(_alloc_len <= controller.byte_count)) {
		// Page 22: If the byte count is odd and the amount of data to be sent is larger
		// than the limit, then the amount of data sent on each DRQ shall be less
		// than the specified value as a drive shall not send odd length DRQs,
		// except for the last transfer. Given this, the odd byte count transfer
		// limit in the BC registers cannot be used. The device shall always
		// round down the value to the next lower even number, unless the transfer
		// length matches the actual total transfer length exactly.
		PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: odd Byte Count (0x%04x), command=0x%02x, using 0x%04x\n",
				selected_string(_ch), controller.byte_count, _cmd, controller.byte_count - 1);
		controller.byte_count--;
	}

	if(!controller.packet_dma && controller.byte_count == 0) {
		// DRQ transfer size of 0 is not allowed for commands other than READ
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: command 0x%02x with zero byte count.\n", selected_string(_ch), _cmd);
		return;
	}

	if(_alloc_len <= 0) {
		_alloc_len = controller.byte_count;
	}

	controller.status.busy = true;
	controller.status.drive_ready = true;
	controller.status.drq = false;
	controller.status.err = false;

	controller.buffer_size = _req_len;
	controller.buffer_index = 0;
	controller.drq_index = 0;

	if(controller.byte_count > _req_len) {
		controller.byte_count = _req_len;
	}

	if(controller.byte_count > _alloc_len) {
		controller.byte_count = _alloc_len;
	}

	// byte_count is the data transferred for each DRQ.
	// make a copy of its value in drq_bytes and use that as a reference
	//selected_drive(_ch).atapi.drq_bytes = controller.byte_count;
	selected_drive(_ch).atapi.bytes_remaining = _req_len;
	selected_drive(_ch).atapi.bytes_total = selected_drive(_ch).atapi.bytes_remaining;

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: send to HOST: req_len=%d, alloc_len=%d, byte_count=%u\n",
		selected_string(_ch),
		_req_len, _alloc_len,
		controller.byte_count
	);
}

uint32_t StorageCtrl_ATA::Drive::atapi_check_seek_completion(uint64_t _now_us)
{
	if(!_now_us) {
		_now_us = g_machine.get_virt_time_us();
	}
	if(atapi.seek_completion_time) {
		if(atapi.seek_completion_time <= _now_us) {
			controller.status.seek_complete = true;
			atapi.seek_completion_time = 0;
			curr_lba = next_lba;
			return 0;
		}
		return atapi.seek_completion_time - _now_us;
	}
	return 0;
}

void StorageCtrl_ATA::atapi_set_sense(int _ch, uint8_t _key, uint8_t _asc, uint8_t _ascq)
{
	selected_drive(_ch).sense.sense_key = _key;
	selected_drive(_ch).sense.asc = _asc;
	selected_drive(_ch).sense.ascq = _ascq;
}

void StorageCtrl_ATA::atapi_success(int _ch, bool _int)
{
	Controller &controller = selected_ctrl(_ch);

	controller.error_register = 0;
	controller.interrupt_reason.c_d = ATAPI_INT_CMD;
	controller.interrupt_reason.i_o = ATAPI_INT_TO_HOST;
	controller.interrupt_reason.rel = 0;
	controller.status.busy = false;
	controller.status.drive_ready = true;
	controller.status.write_fault = false;
	controller.status.drq = false;
	controller.status.err = false;

	if(_int) {
		raise_interrupt(_ch);
	}

	selected_drive(_ch).atapi.command = 0;
}

void StorageCtrl_ATA::atapi_error(int _ch, bool _int)
{
	auto &drive = selected_drive(_ch);

	std::string sense_str, asc_str;
	auto sense_str_it = s_sense_key_str.find(drive.sense.sense_key);
	if(sense_str_it != s_sense_key_str.end()) {
		sense_str = str_format(" (%s)", sense_str_it->second);
	}
	auto asc_str_it = s_asc_str.find(drive.sense.asc);
	if(asc_str_it != s_asc_str.end()) {
		asc_str = str_format(" (%s)", asc_str_it->second);
	}
	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: cmd error key=0x%02x%s, asc=0x%02x%s, ascq=0x%02x\n",
		selected_string(_ch), 
		drive.sense.sense_key, sense_str.c_str(),
		drive.sense.asc, asc_str.c_str(),
		drive.sense.ascq
	);

	Controller &controller = selected_ctrl(_ch);
	controller.error_register = drive.sense.sense_key << 4;
	controller.interrupt_reason.c_d = ATAPI_INT_CMD;
	controller.interrupt_reason.i_o = ATAPI_INT_TO_HOST;
	controller.interrupt_reason.rel = 0;
	controller.status.busy = false;
	controller.status.drive_ready = true;
	controller.status.write_fault = false;
	controller.status.drq = false;
	controller.status.err = true;

	if(_int) {
		raise_interrupt(_ch);
	}

	drive.atapi.command = 0;
}

void StorageCtrl_ATA::atapi_update_head_pos(int _ch, bool _stop_audio)
{
	auto *drive = selected_cd(_ch);

	int64_t curr_audio_lba;
	CdRomDrive::AudioStatus status = drive->get_audio_status(false, &curr_audio_lba);
	if(_stop_audio && (status == CdRomDrive::AUDIO_PLAYING || status == CdRomDrive::AUDIO_PAUSED)) {
		PDEBUGF(LOG_V1, LOG_HDD, "%s: ATAPI: Interrupting CD audio playback.\n",
				selected_string(_ch));
		// Audio lock must be acquired beforehand
		drive->stop_audio(false, false);
	}
	if(curr_audio_lba >= 0) {
		// head position is only valid during a play operation
		// remains valid until the next seek/read on the drive
		selected_drive(_ch).curr_lba = curr_audio_lba;
	}
}

bool StorageCtrl_ATA::atapi_check_transitions(int _ch)
{
	CdRomDrive *drive = selected_cd(_ch);

	if(drive->has_medium_changed(true) && drive->is_disc_accessible()) {
		// Going from medium not ready to medium ready states is
		// communicated by throwing an error.
		atapi_set_sense(_ch, SENSE_UNIT_ATTENTION, ASC_NOT_READY_TO_READY);
		return false;
	}

	// TODO check for POWER ON, RESET OR BUS DEVICE RESET OCCURRED

	atapi_set_sense(_ch, SENSE_NONE);

	return true;
}

bool StorageCtrl_ATA::atapi_access_drive(int _ch, bool _spin_up, bool _blocking, uint32_t *_time_to_ready)
{
	CdRomDrive *drive = selected_cd(_ch);

	if(_spin_up) {
		drive->spin_up();
	}

	switch(drive->disc_state()) {
		case CdRomDrive::DISC_NO_DISC:
		case CdRomDrive::DISC_DOOR_OPEN:
		case CdRomDrive::DISC_DOOR_CLOSING:
		case CdRomDrive::DISC_EJECTING:
			atapi_set_sense(_ch, SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);
			return false;
		case CdRomDrive::DISC_SPINNING_UP:
			if(_blocking) {
				// command will block until DISC_READY
				if(_time_to_ready) {
					*_time_to_ready = drive->time_to_ready_us();
				}
			} else {
				// in progress of becoming ready
				atapi_set_sense(_ch, SENSE_NOT_READY, ASC_DRIVE_NOT_READY, 1);
				return false;
			}
			break;
		case CdRomDrive::DISC_IDLE:
		case CdRomDrive::DISC_READY:
			return atapi_check_transitions(_ch);
		default:
			break;
	}

	atapi_set_sense(_ch, SENSE_NONE);

	return true;
}

uint32_t StorageCtrl_ATA::atapi_cmd_test_unit_ready(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);

	if(!atapi_access_drive(_ch)) {
		atapi_error(_ch);
		return 0;
	}
	atapi_success(_ch);
	return 0;
}

uint32_t StorageCtrl_ATA::atapi_cmd_request_sense(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);
	Drive &drive = selected_drive(_ch);

	int alloc_length = controller.buffer[4];

	atapi_init_send(_ch, _cmd, 18, alloc_length);

	// sense data
	controller.buffer[0] = (1 << 7) | 0x70; // 1 | Error Code 70h (current errors)
	controller.buffer[1] = 0; // Reserved
	controller.buffer[2] = drive.sense.sense_key;
	controller.buffer[3] = drive.sense.information[0];
	controller.buffer[4] = drive.sense.information[1];
	controller.buffer[5] = drive.sense.information[2];
	controller.buffer[6] = drive.sense.information[3];
	controller.buffer[7] = 17 - 7; // Additional Sense Length (n - 7)
	controller.buffer[8] = drive.sense.specific_inf[0];
	controller.buffer[9] = drive.sense.specific_inf[1];
	controller.buffer[10] = drive.sense.specific_inf[2];
	controller.buffer[11] = drive.sense.specific_inf[3];
	controller.buffer[12] = drive.sense.asc;
	controller.buffer[13] = drive.sense.ascq;
	controller.buffer[14] = drive.sense.fruc;
	controller.buffer[15] = drive.sense.key_spec[0];
	controller.buffer[16] = drive.sense.key_spec[1];
	controller.buffer[17] = drive.sense.key_spec[2];

	if(drive.sense.sense_key == SENSE_UNIT_ATTENTION) {
		atapi_set_sense(_ch, SENSE_NONE);
	}

	return DEFAULT_CMD_US;
}

uint32_t StorageCtrl_ATA::atapi_cmd_start_stop_unit(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);
	Controller &controller = selected_ctrl(_ch);
	CdRomDrive *drive = selected_cd(_ch);

	bool Immed = (controller.buffer[1] >> 0) & 1;
	bool LoEj = (controller.buffer[4] >> 1) & 1;
	bool Start = (controller.buffer[4] >> 0) & 1;

	uint32_t time_to_complete = 0;
	if(!LoEj && !Start) {
		// stop the disc
		PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: stopping the disc ...\n", selected_string(_ch));
		drive->spin_down();
	} else if(!LoEj && Start) {
		// start (spin up) the disc (go to ready state)
		PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: going to ready state ...\n", selected_string(_ch));
		if(!atapi_access_drive(_ch, true, true, &time_to_complete)) {
			atapi_error(_ch);
			return 0;
		}
	} else if(LoEj && !Start) {
		// Eject the disc
		if(drive->is_door_locked()) {
			atapi_set_sense(_ch, SENSE_NOT_READY, ASC_MEDIA_REMOVAL_PREVENTED);
			atapi_error(_ch);
			return 0;
		}
		PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: ejecting the disc ...\n", selected_string(_ch));
		drive->open_door();
	} else {
		// close the door
		PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: closing the door ...\n", selected_string(_ch));
		time_to_complete = NSEC_TO_USEC(drive->close_door());
	}

	if(Immed) {
		time_to_complete = 0;
	}

	return DEFAULT_CMD_US + time_to_complete;
}

uint32_t StorageCtrl_ATA::atapi_cmd_stop_play_scan(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);

	CdRomDrive *cd = selected_cd(_ch);
	assert(cd);

	cd->lock_audio();
	atapi_update_head_pos(_ch, true);
	cd->unlock_audio();

	atapi_success(_ch);
	return 0;
}

uint32_t StorageCtrl_ATA::atapi_cmd_mode_select(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);

	bool sp = controller.buffer[1] & 1;
	// The Parameter List Length field specifies the length in bytes of the mode
	// parameter list that shall be transferred from the Host Computer to the
	// ATAPI CD-ROM Drive after the Command Packet is transferred.
	uint16_t param_len = read_16bit(&controller.buffer[7]);

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: sp=%u, param_len=%u\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			sp, param_len);

	if(sp) {
		// Save Pages (SP) bit not supported
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	}

	// A parameter list length of zero indicates that no data shall be transferred.
	// This condition shall not be considered as an error.
	if(param_len == 0) {
		atapi_success(_ch);
		return 0;
	}

	if(!atapi_init_receive(_ch, param_len)) {
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	}

	return DEFAULT_CMD_US;
}

void StorageCtrl_ATA::atapi_mode_select(int _ch, uint16_t _param_len)
{
	Controller &controller = selected_ctrl(_ch);
	CdRomDrive *cd = selected_cd(_ch);

	if(_param_len == 0) {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: MODE SELECT: invalid param length of 0.\n", selected_string(_ch));
		return;
	}
	if(_param_len > sizeof(controller.buffer)) {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: MODE SELECT: param length > buffer data size\n", selected_string(_ch));
		_param_len = sizeof(controller.buffer);
	}

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SELECT: total data len: %u\n", selected_string(_ch), _param_len);

	// The first 8 bytes are the Mode Parameter Header.
	// When using the MODE SELECT command these fields are reserved.
	// Windows 95 sends all zeros there.
	uint8_t *scan = &controller.buffer[8];
	uint8_t *fence = &controller.buffer[_param_len];

	struct ATAPIModePage {
		uint8_t len;
		const char *name;
	};
	static const std::map<uint8_t, ATAPIModePage> pages{
		{ 0x1,  {0x6,  "Read Error Recovery Parameters"} },
		{ 0xD,  {0x6,  "CD-ROM Parameters"} },
		{ 0xE,  {0xE,  "CD-ROM Audio Control Mode"} },
		{ 0x2A, {0x12, "CD-ROM Capabilities and Mechanical Status"} },
	};

	while((scan + 2) < fence) {
		int page_code = scan[0] & 0x1F;
		bool ps = scan[0] & 0x80;
		int page_len = scan[1];
		int total_len = page_len + 2;

		auto page = pages.find(page_code);

		if(page == pages.end()) {
			PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: MODE SELECT: unknown page: %x\n",
				selected_string(_ch), page
			);
			scan += total_len;
			continue;
		}

		PDEBUGF(LOG_V1, LOG_HDD, "%s: ATAPI: MODE SELECT: PAGE=0x%X (%s), LEN=0x%X, PS=%u\n",
			selected_string(_ch), page_code, page->second.name, page->second.len, ps
		);

		if(page->second.len != page_len) {
			PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: MODE SELECT: invalid page length: 0x%X, expected: 0x%X\n",
				selected_string(_ch), page_len, page->second.len
			);
			scan += total_len;
			continue;
		}

		if((scan + total_len) > fence) {
			PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: MODE SELECT: current page length extends %d bytes past buffer.\n",
				selected_string(_ch), (scan + total_len - fence)
			);
			break;
		}

		switch(page_code) {
			case 0xD:
				cd->set_timeout_mult(scan[3] & 0xF);
				break;
			case 0xE: {
				bool sotc = scan[2] & 0x02;
				PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SELECT: [2] SOTC: %u\n", selected_string(_ch), sotc);

				cd->set_sotc(sotc);

				uint8_t port0_ch = scan[8] & 0xF;
				uint8_t port0_vol = scan[9];
				PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SELECT: [8-9] Port 0: 0x%X, volume: 0x%X\n",
						selected_string(_ch), port0_ch, port0_vol);

				cd->set_audio_port(0, port0_ch, port0_vol);

				uint8_t port1_ch = scan[10] & 0xF;
				uint8_t port1_vol = scan[11];
				PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SELECT: [10-11] Port 1: 0x%X, volume: 0x%X\n",
						selected_string(_ch), port1_ch, port1_vol);

				cd->set_audio_port(1, port1_ch, port1_vol);

				break;
			}
			default:
				PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SELECT: PAGE DATA: ", selected_string(_ch));
				for(int i = 2; i < total_len; i++) {
					PDEBUGF(LOG_V2, LOG_HDD, "%02x ", scan[i]);
				}
				PDEBUGF(LOG_V2, LOG_HDD, "\n");
				break;
		}

		scan += total_len;
	}
}

void StorageCtrl_ATA::atapi_init_mode_sense_single(int _ch, const void *_src, size_t _size)
{
	Controller &controller = selected_ctrl(_ch);
	CdRomDrive *cd = selected_cd(_ch);
	assert(cd);

	// Header (SFF-8020i p.109)
	controller.buffer[0] = (_size+6) >> 8;   // Length MSB
	controller.buffer[1] = (_size+6) & 0xff; // Length LSB
	controller.buffer[2] = cd->disc_type();
	controller.buffer[3] = 0; // reserved
	controller.buffer[4] = 0; // reserved
	controller.buffer[5] = 0; // reserved
	controller.buffer[6] = 0; // reserved
	controller.buffer[7] = 0; // reserved

	if(_src) {
		// Data
		memmove(&controller.buffer[8], _src, _size);
	}
}

uint32_t StorageCtrl_ATA::atapi_cmd_mode_sense(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);
	Drive &drive = selected_drive(_ch);
	CdRomDrive *cd = selected_cd(_ch);
	assert(cd);

	uint16_t alloc_length;
	if(_cmd == 0x5a) {
		// MODE SENSE (10)
		alloc_length = read_16bit(&controller.buffer[7]);
	} else {
		// MODE SENSE (6)
		assert(_cmd == 0x1a);
		alloc_length = controller.buffer[4];
	}
	uint8_t page_control = controller.buffer[2] >> 6;
	uint8_t page_code = controller.buffer[2] & 0x3f;

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: page_control=%u, page_code=0x%02X\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			page_control, page_code);

	const bool current_values = page_control == 0;
	const int header_size = 8;
	switch(page_control) {
		case 0x0: // current values
		case 0x2: // default values
		{
			switch(page_code) {
				case 0x01: // Error Recovery Parameters (SFF-8020i p.114)
					PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SENSE: PAGE=0x%X (Error Recovery Parameters)\n",
							selected_string(_ch), page_code);
					atapi_init_mode_sense_single(_ch, &drive.atapi.error_recovery,
						sizeof(Drive::Atapi::error_recovery)
					);
					atapi_init_send(_ch, _cmd,
						header_size + sizeof(Drive::Atapi::error_recovery),
						alloc_length
					);
					return DEFAULT_CMD_US;

				case 0x2a: // CD-ROM Capabilities and Mechanical Status (SFF-8020i p.118)
					PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SENSE: PAGE=0x%X (CD-ROM Capabilities and Mechanical Status)\n",
							selected_string(_ch), page_code);
					atapi_init_mode_sense_single(_ch, nullptr, 20);
					controller.buffer[8] = 0x2a;  // 0 Page Code (2Ah)
					controller.buffer[9] = 0x12;  // 1 Page Length (12h)
					controller.buffer[10] = 0x07; // 2 Method 2 | CD-E Rd | CD-R Rd
					controller.buffer[11] = 0x00; // 3 No write support
					controller.buffer[12] = 0x71; // 4 Multisession, Mode 2 Form 2, Mode 2 Form 1, Audio
					controller.buffer[13] = 0xff; // 5 UPC|ISRC|C2|R-Wde|R-W|DAacc|CDDA
					controller.buffer[14] = (uint8_t) ( // 6
							1 | // lock supported
							(cd->is_door_locked() ? (1 << 1) : 0) | // lock state
							(1 << 3) | // eject supported
							(1 << 5)   // mech. type = tray
					);
					controller.buffer[15] = 0x03; // 7 Sep. ch. mute | Sep. volume levels
					controller.buffer[16] = (cd->max_speed_kb() >> 8) & 0xff; // 8 Max. Speed in KB/s (MSB)
					controller.buffer[17] = cd->max_speed_kb() & 0xff;        // 9 Max. Speed in KB/s (LSB)
					controller.buffer[18] = 256 >> 8;   // 10 Number of volume levels (MSB)
					controller.buffer[19] = 256 & 0xff; // 11 Number of volume levels (LSB)
					controller.buffer[20] = (512 >> 8) & 0xff; // 12 Buffer Size (MSB)
					controller.buffer[21] = 512 & 0xff;        // 13 Buffer Size (LSB)
					if(current_values) {
						controller.buffer[22] = (cd->cur_speed_kb() >> 8) & 0xff; // 14 Current Speed Selected (MSB)
						controller.buffer[23] = cd->cur_speed_kb() & 0xff;        // 15 Current Speed Selected (LSB)
					} else {
						controller.buffer[22] = (cd->max_speed_kb() >> 8) & 0xff; // 14 Current Speed Selected (MSB)
						controller.buffer[23] = cd->max_speed_kb() & 0xff;        // 15 Current Speed Selected (LSB)
					}
					controller.buffer[24] = 0; // 16 Reserved
					controller.buffer[25] = 0; // 17 Drive digital output
					controller.buffer[26] = 0; // 18 Reserved
					controller.buffer[27] = 0; // 19 Reserved
					atapi_init_send(_ch, _cmd, header_size + 20, alloc_length);
					return DEFAULT_CMD_US;

				case 0x0e: // CD-ROM Audio Control Mode
				{
					PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SENSE: PAGE=0x%X (CD-ROM Audio Control Mode)\n",
							selected_string(_ch), page_code);
					atapi_init_mode_sense_single(_ch, nullptr, 16);
					controller.buffer[8] = 0x0e; // 0 Page Code (0Eh)
					controller.buffer[9] = 0x0e; // 1 Page Length (0Eh)
					controller.buffer[10] = (1 << 2); // 2 Immed
					controller.buffer[11] = 0; // 3 Reserved
					controller.buffer[12] = 0; // 4 Reserved
					controller.buffer[13] = 0; // 5 Reserved
					controller.buffer[14] = 0;  // 6 Logical Block Per Second of Audio Playback (MSB)
					controller.buffer[15] = 75; // 7 Logical Block Per Second of Audio Playback (LSB)
					if(current_values) {
						auto [ch, vol] = cd->get_audio_port(0);
						controller.buffer[16] = ch;  // 8 Output port 0 selection (1 = channel 0)
						controller.buffer[17] = vol; // 9 Output port 0 volume (0xFF = 0dB atten.)
					} else {
						controller.buffer[16] = 1;    // 8 Output port 0 selection (1 = channel 0)
						controller.buffer[17] = 0xFF; // 9 Output port 0 volume (0xFF = 0dB atten.)
					}
					if(current_values) {
						auto [ch, vol] = cd->get_audio_port(1);
						controller.buffer[18] = ch;  // 10 Output port 1 selection (2 = channel 1)
						controller.buffer[19] = vol; // 11 Output port 1 volume (0xFF = 0dB atten.)
					} else {
						controller.buffer[18] = 2;   // 10 Output port 1 selection (2 = channel 1)
						controller.buffer[19] = 0xFF; // 11 Output port 1 volume (0xFF = 0dB atten.)
					}
					controller.buffer[20] = 0; // 12 Output port 2 selection (none)
					controller.buffer[21] = 0; // 13 Output port 2 volume (0 = mute)
					controller.buffer[22] = 0; // 14 Output port 3 selection (none)
					controller.buffer[23] = 0; // 15 Output port 3 volume (0 = mute)
					atapi_init_send(_ch, _cmd, header_size + 16, alloc_length);
					return DEFAULT_CMD_US;
				}
				case 0x0d: // CD-ROM Parameters
					PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: MODE SENSE: PAGE=0x%X (CD-ROM Parameters)\n",
							selected_string(_ch), page_code);
					atapi_init_mode_sense_single(_ch, nullptr, 8);
					controller.buffer[8] = 0x0d; // 0 Page Code (0Dh)
					controller.buffer[9] = 0x06; // 1 Page Length (06h)
					controller.buffer[10] = 0;   // 2 Reserved
					controller.buffer[11] = cd->timeout_mult();  // 3 Inactivity Time Multiplier
					controller.buffer[12] = 0;   // 4 Number of MSF - S Units per MSF - M Unit (MSB)
					controller.buffer[13] = 60;  // 5 Number of MSF - S Units per MSF - M Unit (LSB)
					controller.buffer[14] = 0;   // 6 Number of MSF - F Units per MSF - S Unit (MSB)
					controller.buffer[15] = 75;  // 7 Number of MSF - F Units per MSF - S Unit (LSB)
					atapi_init_send(_ch, _cmd, header_size + 8, alloc_length);
					return DEFAULT_CMD_US;

				case 0x3f: // All
				default:
					// not implemeted by this device
					PDEBUGF(LOG_V2, LOG_HDD,
							"%s: ATAPI: MODE SENSE: PC=0x%X, PAGE=0x%X: not implemented by device.\n",
							selected_string(_ch), page_control, page_code);
					atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
					atapi_error(_ch);
					return 0;
			}
		}
		case 0x1: // changeable values
		{
			PDEBUGF(LOG_V2, LOG_HDD,
					"%s: ATAPI: MODE SENSE PC=%x, PageCode=%x, not implemented.\n",
					selected_string(_ch), page_control, page_code);
			atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
			atapi_error(_ch);
			return 0;
		}
		case 0x3: // saved values not implemented
		default:
			atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_SAVING_PARAMETERS_NOT_SUPPORTED);
			atapi_error(_ch);
			return 0;
	}

	assert(false);
}

uint32_t StorageCtrl_ATA::atapi_cmd_pause_resume(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);
	CdRomDrive *cd = selected_cd(_ch);

	if(!atapi_access_drive(_ch, true, false, nullptr)) {
		atapi_error(_ch);
		return 0;
	}

	bool resume = controller.buffer[8] & 1;

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: resume=%d\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			resume);

	if(!cd->pause_resume_audio(resume)) {
		atapi_set_sense(_ch, SENSE_ABORTED, 0xB9);
		atapi_error(_ch);
		return 0;
	}

	atapi_success(_ch);
	return 0;
}

uint32_t StorageCtrl_ATA::atapi_cmd_play_audio(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);

	int64_t start_lba = read_32bit(&controller.buffer[2]);
	uint16_t len = read_16bit(&controller.buffer[7]);

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: start=%lld, len=%u\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			start_lba, len);

	if(start_lba == 0xffffffff) {
		start_lba = -1;
	}

	return atapi_play_audio(_ch, start_lba, -1, len);
}

uint32_t StorageCtrl_ATA::atapi_cmd_play_audio_msf(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);

	TMSF start(&controller.buffer[3]);
	TMSF end(&controller.buffer[6]);

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: start=%s, end=%s\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			start.to_string().c_str(), end.to_string().c_str());

	int64_t start_lba; 
	if(start.min == 0xff && start.sec == 0xff && start.fr == 0xff) {
		start_lba = -1;
	} else {
		start_lba = start.to_frames();
	}

	int64_t end_lba = end.to_frames();

	return atapi_play_audio(_ch, start_lba, end_lba, -1);
}

uint32_t StorageCtrl_ATA::atapi_play_audio(int _ch, int64_t _start_lba, int64_t _end_lba, int _len)
{
	uint32_t time_to_ready = 0;
	if(!atapi_access_drive(_ch, true, true, &time_to_ready)) {
		atapi_error(_ch);
		return 0;
	}

	Drive &drive = selected_drive(_ch);
	CdRomDrive *cd = selected_cd(_ch);
	assert(cd);

	if(!atapi_access_drive(_ch)) {
		atapi_error(_ch);
		return 0;
	}

	// a SEEK might be happening concurrently
	// check the destination, if it's not the same LBA of our start value,
	// we need to cancel it and perform another

	uint32_t time_to_seek_complete = drive.atapi_check_seek_completion();
	int64_t head_position;

	cd->lock_audio();

	if(time_to_seek_complete) {
		head_position = selected_drive(_ch).next_lba;
	} else {
		// head is not moving
		// update current head position from player if audio is playing
		atapi_update_head_pos(_ch, false);
		head_position = selected_drive(_ch).curr_lba;
	}

	if(_start_lba == -1) {
		// current head position is assumed
		_start_lba = head_position;
	}
	if(_end_lba == -1) {
		// if _len is < 0 the check_play_audio() will return error
		_end_lba = _start_lba + _len;
	}

	uint8_t sense, asc;
	if(!cd->check_play_audio(_start_lba, _end_lba, sense, asc)) {
		cd->unlock_audio();
		atapi_set_sense(_ch, sense, asc);
		atapi_error(_ch);
		return 0;
	}

	if(_start_lba == _end_lba) {
		// A Starting MSF address equal to an ending MSF address causes no audio
		// play operation to occur. This shall not be considered an error.
		cd->stop_audio(false, false);
		cd->unlock_audio();
		return DEFAULT_CMD_US;
	}

	if(head_position != _start_lba) {
		// perform a seek
		selected_drive(_ch).curr_lba = head_position;
		selected_drive(_ch).next_lba = _start_lba;
		time_to_seek_complete = atapi_seek(_ch);
		selected_ctrl(_ch).status.seek_complete = false;
		selected_drive(_ch).atapi.seek_completion_time = g_machine.get_virt_time_us() + time_to_ready + time_to_seek_complete;
	}

	cd->unlock_audio();
	cd->play_audio(_start_lba, _end_lba, time_to_seek_complete);

	// This command responds with immediate status
	return DEFAULT_CMD_US;
}

uint32_t StorageCtrl_ATA::atapi_cmd_inquiry(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);
	StorageDev &storage = selected_storage(_ch);

	uint8_t alloc_length = controller.buffer[4];

	controller.buffer[0] = 0x05; // CD-ROM device
	controller.buffer[1] = 0x80; // Removable
	controller.buffer[2] = 0x00; // ISO, ECMA, ANSI version (must contain a zero)
	controller.buffer[3] = 0x21; // ATAPI-2, as specified
	controller.buffer[4] = 36-5; // additional bytes after this
	controller.buffer[5] = 0x00; // reserved
	controller.buffer[6] = 0x00; // reserved
	controller.buffer[7] = 0x00; // reserved
	std::memcpy(&controller.buffer[8], storage.vendor(), 8); // 8-15: Vendor ID
	std::memcpy(&controller.buffer[16], storage.product(), 16); // 16-31: Product ID
	std::memcpy(&controller.buffer[32], storage.revision(), 4); // 32-35: Product Revision level

	atapi_init_send(_ch, _cmd, 36, alloc_length);

	return DEFAULT_CMD_US;
}

uint32_t StorageCtrl_ATA::atapi_cmd_read_cdrom_capacity(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);

	if(!atapi_access_drive(_ch)) {
		atapi_error(_ch);
		return 0;
	}

	uint32_t sec = selected_cd(_ch)->sectors();
	controller.buffer[0] = (sec >> 24) & 0xff;
	controller.buffer[1] = (sec >> 16) & 0xff;
	controller.buffer[2] = (sec >> 8) & 0xff;
	controller.buffer[3] = (sec >> 0) & 0xff;
	controller.buffer[4] = (2048 >> 24) & 0xff;
	controller.buffer[5] = (2048 >> 16) & 0xff;
	controller.buffer[6] = (2048 >> 8) & 0xff;
	controller.buffer[7] = (2048 >> 0) & 0xff;

	atapi_init_send(_ch, _cmd, 8, 0);

	return DEFAULT_CMD_US;
}

uint32_t StorageCtrl_ATA::atapi_cmd_read_toc(int _ch, uint8_t _cmd)
{
	uint32_t time_to_ready = 0;
	if(!atapi_access_drive(_ch, true, true, &time_to_ready)) {
		atapi_error(_ch);
		return 0;
	}

	Controller &controller = selected_ctrl(_ch);
	CdRomDrive *drive = selected_cd(_ch);
	assert(drive);

	uint16_t alloc_length = read_16bit(controller.buffer + 7);

	// from SFF-8020i:
	// When Format in Byte 2 is zero, then Byte 9 is used.
	// Note: The Format field in Byte 9 is a vendor-specific area and will be removed
	// in subsequent versions of this specification. Functionality is moving to Byte 2.
	uint8_t format = (controller.buffer[2] & 0xf);
	if(format == 0) {
		format = (controller.buffer[9] >> 6);
	}
	uint8_t starting_track = controller.buffer[6];
	uint8_t msf = (controller.buffer[1] >> 1) & 1;

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: format=%x, starting_track=%u, msf=%x\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			format, starting_track, msf);

	if(starting_track > 99 && starting_track != 0xaa) {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: READ TOC: invalid starting track.\n", selected_string(_ch));
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	}

	size_t toc_length = 0;

	if(format == 3) {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: READ TOC: format %u not supported\n", selected_string(_ch), format);
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	} else if(!drive->read_toc(controller.buffer, sizeof(controller.buffer), toc_length, msf, starting_track, format)) {
		atapi_set_sense(_ch, SENSE_NOT_READY, 0x57); // UNABLE TO RECOVER TABLE OF CONTENTS
		atapi_error(_ch);
		return 0;
	}

	atapi_init_send(_ch, _cmd, toc_length, alloc_length);

	// don't count the time to read the TOC from disc, consider it in the device's buffer
	// the TOC is read by the device when the disc is inserted and loaded for the first time
	return DEFAULT_CMD_US + time_to_ready;
}

uint32_t StorageCtrl_ATA::atapi_cmd_read(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);
	Drive &drive = selected_drive(_ch);
	CdRomDrive *cd = selected_cd(_ch);

	uint32_t lba = read_32bit(controller.buffer + 2);
	int32_t transfer_length;
	if(_cmd == 0x28) {
		// READ (10)
		transfer_length = read_16bit(controller.buffer + 7);
	} else {
		// READ (12)
		assert(_cmd == 0xA8);
		transfer_length = read_32bit(controller.buffer + 6);
	}

	uint32_t time_to_ready = 0;
	if(!atapi_access_drive(_ch, true, true, &time_to_ready)) {
		atapi_error(_ch);
		return 0;
	}

	if(lba > cd->max_lba()) {
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR);
		atapi_error(_ch);
		return 0;
	}

	if((lba + transfer_length - 1) > cd->max_lba()) {
		transfer_length = (cd->max_lba() - lba + 1);
	}
	if(transfer_length <= 0) {
		PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: transfer length <= 0 (%i)\n",
				selected_string(_ch), atapi_cmd_string(_cmd), transfer_length);
	}

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: lba=%u, transfer_length=%d, DMA=%u\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			lba, transfer_length, controller.packet_dma);

	if(controller.packet_dma) {
		PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: BMDMA not implemented\n",
				selected_string(_ch), atapi_cmd_string(_cmd));
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	}

	transfer_length = std::max(0, transfer_length);
	selected_drive(_ch).atapi.sector_size = 2048;
	int total_bytes = transfer_length * selected_drive(_ch).atapi.sector_size;

	// let's negotiate the DRQ byte count
	if(transfer_length && controller.byte_count == 0) {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: %s: zero Byte Count\n",
				selected_string(_ch), atapi_cmd_string(_cmd));
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	}

	// Page 22: the minimum size of the DRQ limit *shall* be no smaller than the
	// length of data to be transferred for one sector from the media.
	if(controller.byte_count < selected_drive(_ch).atapi.sector_size) {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: %s: Byte Count smaller than 2048\n",
				selected_string(_ch), atapi_cmd_string(_cmd));
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	}
	if(controller.byte_count > 0xfffe) {
		controller.byte_count = 0xfffe;
	}
	if(controller.byte_count > total_bytes) {
		controller.byte_count = total_bytes;
	}
	if(controller.byte_count > sizeof(controller.buffer)) {
		controller.byte_count = sizeof(controller.buffer);
	}

	// make it a multiple of the sector size
	selected_drive(_ch).atapi.drq_sectors = (controller.byte_count / selected_drive(_ch).atapi.sector_size);

	uint32_t time_to_seek = 0;
	int64_t cur_seek_target = selected_drive(_ch).next_lba;

	// stop current audio operation and update head position
	uint64_t next_point_in_time = g_machine.get_virt_time_us() + DEFAULT_CMD_US;
	time_to_seek = drive.atapi_check_seek_completion(next_point_in_time);
	if(!time_to_seek) {
		cd->lock_audio();
		atapi_update_head_pos(_ch, true);
		cd->unlock_audio();
	}

	selected_drive(_ch).atapi.bytes_total = total_bytes;
	selected_drive(_ch).atapi.bytes_remaining = total_bytes;
	selected_drive(_ch).atapi.sectors_total = transfer_length;
	selected_drive(_ch).atapi.sectors_remaining = transfer_length;

	controller.status.busy = true;
	controller.status.drive_ready = true;
	controller.status.drq = false;
	controller.status.err = false;

	// no bytes transfered yet
	controller.buffer_size = 0;
	controller.buffer_index = 0;
	controller.drq_index = 0;

	selected_drive(_ch).next_lba = lba;

	if(time_to_ready == 0) {
		// CD is ready, it might be seeking (executing SEEK cmd)
		if(time_to_seek) {
			// it's seeking
			if(lba != cur_seek_target) {
				// but the destination is not what we want!
				time_to_seek = atapi_seek(_ch);
			}
		} else {
			// not seeking at this moment so do it
			time_to_seek = atapi_seek(_ch);
		}
	} else {
		// CD is becoming ready, after that do a seek
		time_to_seek = atapi_seek(_ch);
	}

	// ready to read the first block
	uint32_t time_to_read = atapi_read_next_block(_ch, true);

	if(controller.status.err) {
		return 0;
	}

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: time_to_ready=%u, time_to_seek=%d, time_to_read=%u\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			time_to_ready, time_to_seek, time_to_read);

	return DEFAULT_CMD_US + time_to_ready + time_to_seek + time_to_read;
}

uint32_t StorageCtrl_ATA::atapi_read_next_block(int _ch, bool _rot_latency)
{
	Controller &ctrl = selected_ctrl(_ch);
	Drive &drive = selected_drive(_ch);
	CdRomDrive *cd = selected_cd(_ch);

	drive.curr_lba = drive.next_lba;

	unsigned xfer_amount = 1;

	if(drive.atapi.sectors_remaining > drive.atapi.drq_sectors) {
		xfer_amount = drive.atapi.drq_sectors;
	} else {
		xfer_amount = drive.atapi.sectors_remaining;
	}

	// for reads the DRQ window and the buffer window are the same
	ctrl.buffer_size = xfer_amount * drive.atapi.sector_size;
	ctrl.buffer_index = 0;

	// tell the host the size of the transfer
	ctrl.byte_count = ctrl.buffer_size;
	//drive.atapi.drq_bytes = ctrl.byte_count;

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: READ sectors remaining %u of %u\n",
			selected_string(_ch),
			drive.atapi.sectors_remaining,
			drive.atapi.sectors_total);

	uint32_t xfer_time = cd->transfer_time_us(xfer_amount);
	uint32_t rot_lat = cd->rotational_latency_us();

	int64_t c0,h0,s0,c1,h1,s1;
	cd->lba_to_chs(drive.curr_lba, c0,h0,s0);
	cd->lba_to_chs(drive.curr_lba + xfer_amount, c1,h1,s1);
	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: reading %u sector(s), lba=%lld, C:%lld/S:%lld->C:%lld/S:%lld, tx time=%u us, lat=%u us\n",
		selected_string(_ch),
		xfer_amount,
		drive.curr_lba,
		c0,s0,
		c1,s1,
		xfer_time,
		rot_lat
	);

	if(_rot_latency) {
		xfer_time += rot_lat;
	}

	try {
		uint8_t *bufptr = ctrl.buffer;
		while(xfer_amount) {
			if(!cd->read_sector(drive.curr_lba, bufptr, drive.atapi.sector_size)) {
				// the user removed the disc in the middle of a transfer and the lock is not active?
				atapi_set_sense(_ch, SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);
				atapi_error(_ch);
				return 0;
			}
			bufptr += drive.atapi.sector_size;
			drive.curr_lba++;
			xfer_amount--;
			drive.atapi.sectors_remaining--;
		};

		drive.next_lba = drive.curr_lba;

	} catch(std::exception &e) {
		// exception reading the image from host  fs / drive?
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: error reading sectors (%s)\n", e.what());
		// CIRC UNRECOVERED ERROR:
		atapi_set_sense(_ch, 0x3, 0x11, 0x6);
		atapi_error(_ch);
		return 0;
	}

	return xfer_time;
}

uint32_t StorageCtrl_ATA::atapi_cmd_seek(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);

	uint32_t time_to_ready = 0;
	if(!atapi_access_drive(_ch, true, true, &time_to_ready)) {
		atapi_error(_ch);
		return 0;
	}

	Controller &controller = selected_ctrl(_ch);
	auto *drive = selected_cd(_ch);

	uint32_t lba = read_32bit(&controller.buffer[2]);

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: lba=%u\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			lba);

	if(lba > drive->max_lba()) {
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR);
		atapi_error(_ch);
		return 0;
	}

	// stop audio and update current head position
	drive->lock_audio();
	atapi_update_head_pos(_ch, true);
	drive->unlock_audio();

	uint64_t next_time_point = g_machine.get_virt_time_us() + SEEK_CMD_US + time_to_ready;

	selected_drive(_ch).next_lba = lba;
	uint32_t seek_time = atapi_seek(_ch);
	selected_ctrl(_ch).status.seek_complete = false;
	selected_drive(_ch).atapi.seek_completion_time = next_time_point + seek_time;

	// The SEEK Command will always be executed as an immediate command.
	// The command will return completion stations as soon as the seek operation has been started.
	return SEEK_CMD_US + time_to_ready;
}

uint32_t StorageCtrl_ATA::atapi_cmd_prevent_allow_medium_removal(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);

	bool lock = selected_ctrl(_ch).buffer[4] & 1;
	selected_cd(_ch)->lock_door(lock);

	PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: %s: door now %s.\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			lock ? "locked" : "unlocked");

	return DEFAULT_CMD_US;
}

uint32_t StorageCtrl_ATA::atapi_cmd_read_subchannel(int _ch, uint8_t _cmd)
{
	uint32_t time_to_ready = 0;
	if(!atapi_access_drive(_ch, true, true, &time_to_ready)) {
		atapi_error(_ch);
		return 0;
	}

	Controller &controller = selected_ctrl(_ch);
	CdRomDrive *cd = selected_cd(_ch);
	assert(cd);
	Drive & drive = selected_drive(_ch);

	bool msf = packet_field(controller.buffer, 1, 1, 1);
	bool sub_q = packet_field(controller.buffer, 2, 6, 1);
	uint8_t data_format = controller.buffer[3];
	uint8_t track_number = controller.buffer[6];
	uint16_t alloc_length = packet_word(controller.buffer, 7);

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: msf=%u, sub_q=%u, data_format=%u, track_number=%u\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			msf, sub_q, data_format, track_number);

	if(data_format == 0 || data_format > 3) {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: %s: invalid SubQ format: %u\n",
				selected_string(_ch), atapi_cmd_string(_cmd), data_format);
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	}

	uint32_t time_to_seek_complete = drive.atapi_check_seek_completion();
	int64_t head_position;

	cd->lock_audio();
	if(time_to_seek_complete) {
		head_position = selected_drive(_ch).next_lba;
	} else {
		// head is not moving
		// update current head position from player if audio is playing
		atapi_update_head_pos(_ch, false);
		head_position = selected_drive(_ch).curr_lba;
	}

	size_t req_len = 0;
	uint8_t sense, asc;
	bool result = cd->read_sub_channel(
		controller.buffer, sizeof(controller.buffer), req_len,
		msf, sub_q, data_format, head_position,
		sense, asc
	);
	cd->unlock_audio();

	if(!result) {
		atapi_set_sense(_ch, sense, asc);
		atapi_error(_ch);
		return 0;
	}

	atapi_init_send(_ch, _cmd, int(req_len), alloc_length);

	return DEFAULT_CMD_US + time_to_ready + time_to_seek_complete;
}

uint32_t StorageCtrl_ATA::atapi_cmd_read_disc_info(int _ch, uint8_t _cmd)
{
	UNUSED(_cmd);
	// not implemented, used to keep CD-ROM drivers (eg. Linux) happy
	atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
	atapi_error(_ch);
	return 0;
}

uint32_t StorageCtrl_ATA::atapi_cmd_get_event_status_notification(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);
	CdRomDrive *drive = selected_cd(_ch);

	bool immed = controller.buffer[1] & 1;
	uint8_t request = controller.buffer[4];
	uint16_t alloc_length = read_16bit(controller.buffer + 7);

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: %s: immed=%u, request=%d\n",
			selected_string(_ch), atapi_cmd_string(_cmd),
			immed, request);

	int event_length;

	if(immed) {
		// MEDIA event (bit 4) only
		if(request == (1<<4)) {
			controller.buffer[0] = 0;
			controller.buffer[1] = 4;          // 4 bytes long
			controller.buffer[2] = (0<<7) | 4; // 4 = MEDIA event
			controller.buffer[3] = (1<<4);     // bit 4 = MEDIA event
			controller.buffer[4] =
				(!drive->has_medium_changed(true)) ? 0 : // Event code: 0 = no change
				(drive->is_disc_accessible()) ? 4 : 3;   // Event code: 4 = media changed, 3 = removed
			controller.buffer[5] = (drive->is_disc_accessible()) ? (1<<1) : 0; // Media Status (bit 1 = Media Present)
			controller.buffer[6] = 0;
			controller.buffer[7] = 0;
			event_length = (alloc_length <= 4) ? 4 : 8;
		} else {
			controller.buffer[0] = 0;
			controller.buffer[1] = 0;
			controller.buffer[2] = (1<<7) | request;
			controller.buffer[3] = (1<<4);  // bit 4 = MEDIA event
			event_length = 4;
		}
		atapi_init_send(_ch, _cmd, event_length, alloc_length);
	} else {
		PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: %s: asynchronous mode not supported.\n",
				selected_string(_ch), atapi_cmd_string(_cmd));
		atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_FIELD_IN_CMD_PACKET);
		atapi_error(_ch);
		return 0;
	}

	return DEFAULT_CMD_US;
}

uint32_t StorageCtrl_ATA::atapi_cmd_not_implemented(int _ch, uint8_t _cmd)
{
	PDEBUGF(LOG_V0, LOG_HDD, "%s: ATAPI: command %s (0x%02x) not implemented.\n",
			selected_string(_ch), atapi_cmd_string(_cmd), _cmd);
	atapi_set_sense(_ch, SENSE_ILLEGAL_REQUEST, ASC_INVALID_COMMAND_OPCODE);
	atapi_error(_ch);
	return 0;
}

void StorageCtrl_ATA::atapi_ready_to_transfer(int _ch, unsigned _type, unsigned _direction, bool _int)
{
	Controller &controller = selected_ctrl(_ch);

	if(controller.packet_dma) {
		PERRF(LOG_HDD, "%s: BMDMA not implemented\n", selected_string(_ch));
		return;
	}

	controller.interrupt_reason.c_d = _type;
	controller.interrupt_reason.i_o = _direction;
	controller.status.busy = false;
	controller.status.drq = true;
	controller.status.err = false;

	if(_int) {
		raise_interrupt(_ch);
	}
}

void StorageCtrl_ATA::raise_interrupt(int _ch)
{
	if(!selected_ctrl(_ch).control.disable_irq) {
		PDEBUGF(LOG_V2, LOG_HDD, "%s: raising interrupt %d.\n",
				selected_string(_ch), m_channels[_ch].irq);
		m_devices->pic()->raise_irq(m_channels[_ch].irq);
	} else {
		PDEBUGF(LOG_V2, LOG_HDD, "%s: not raising interrupt.\n",
				selected_string(_ch));
	}
}

void StorageCtrl_ATA::lower_interrupt(int _ch)
{
	m_devices->pic()->lower_irq(m_channels[_ch].irq);
}

void StorageCtrl_ATA::command_successful(int _ch, int _dev, bool _raise_int)
{
	ctrl(_ch,_dev).status.busy = false;
	ctrl(_ch,_dev).status.err = false;
	ctrl(_ch,_dev).status.drq = false;
	ctrl(_ch,_dev).status.drive_ready = true;
	ctrl(_ch,_dev).status.seek_complete = true;
	ctrl(_ch,_dev).status.corrected_data = false;
	ctrl(_ch,_dev).buffer_index = 0;
	ctrl(_ch,_dev).error_register = 0x00;

	if(_raise_int) {
		raise_interrupt(_ch);
	}
}

void StorageCtrl_ATA::command_aborted(int _ch, uint8_t _cmd)
{
	Controller &controller = selected_ctrl(_ch);

	PDEBUGF(LOG_V2, LOG_HDD, "aborting on command 0x%02x {%s}\n",
			_cmd, selected_type_string(_ch));
	controller.current_command = 0;
	controller.status.busy = false;
	controller.status.drive_ready = true;
	controller.status.err = true;
	controller.error_register = 0x04; // command ABORTED
	controller.status.drq = false;
	controller.status.corrected_data = false;
	controller.buffer_index = 0;
	raise_interrupt(_ch);
}

void StorageCtrl_ATA::set_signature(int _ch, int _dev)
{
	// Device signature
	ctrl(_ch,_dev).head_no       = 0;
	ctrl(_ch,_dev).sector_count  = 1;
	ctrl(_ch,_dev).sector_no     = 1;
	if(is_hdd(_ch,_dev)) {
		ctrl(_ch,_dev).cylinder_no = 0;
		m_channels[_ch].drive_select = 0;
	} else if(is_cd(_ch,_dev)) {
		ctrl(_ch,_dev).cylinder_no = 0xeb14;
	} else {
		ctrl(_ch,_dev).cylinder_no = 0xffff;
	}
}

int64_t StorageCtrl_ATA::calculate_logical_address(int _ch)
{
	int64_t logical_sector = -1;
	Controller &controller = selected_ctrl(_ch);

	if(controller.lba_mode) {
		if(!controller.lba48) {
			logical_sector = ((uint32_t)controller.head_no) << 24 |
					((uint32_t)controller.cylinder_no) << 8 |
					(uint32_t)controller.sector_no;
		} else {
			logical_sector = ((uint64_t)controller.hob.hcyl) << 40 |
					((uint64_t)controller.hob.lcyl) << 32 |
					((uint64_t)controller.hob.sector) << 24 |
					((uint64_t)controller.cylinder_no) << 8 |
					(uint64_t)controller.sector_no;
		}
	} else {
		logical_sector = selected_storage(_ch).chs_to_lba(
				controller.cylinder_no,
				controller.head_no,
				controller.sector_no);
	}

	int64_t stg_sectors = selected_storage(_ch).sectors();
	if(logical_sector >= stg_sectors) {
		PERRF(LOG_HDD, "logical address out of bounds (%lld/%lld)\n",
				logical_sector, stg_sectors);
		return -1;
	}
	return logical_sector;
}

int64_t StorageCtrl_ATA::increment_address(int _ch, int64_t &_lba_sect, uint8_t _amount)
{
	Controller *controller = &selected_ctrl(_ch);

	controller->sector_count -= _amount;
	controller->num_sectors -= _amount;
	_lba_sect += _amount;
	int64_t curr_cyl;
	if(controller->lba_mode) {
		if(!controller->lba48) {
			controller->head_no = (uint8_t)((_lba_sect >> 24) & 0xf);
			controller->cylinder_no = (uint16_t)((_lba_sect >> 8) & 0xffff);
			controller->sector_no = (uint8_t)((_lba_sect) & 0xff);
			curr_cyl = controller->cylinder_no;
		} else {
			controller->hob.hcyl = (uint8_t)((_lba_sect >> 40) & 0xff);
			controller->hob.lcyl = (uint8_t)((_lba_sect >> 32) & 0xff);
			controller->hob.sector = (uint8_t)((_lba_sect >> 24) & 0xff);
			controller->cylinder_no = (uint16_t)((_lba_sect >> 8) & 0xffff);
			controller->sector_no = (uint8_t)((_lba_sect) & 0xff);
			curr_cyl = controller->cylinder_no | ((_lba_sect >> 16) & 0xffff0000);
		}
	} else {
		if(_lba_sect >= selected_storage(_ch).sectors()) {
			controller->sector_no = 1;
			controller->head_no = 0;
			controller->cylinder_no = selected_storage(_ch).geometry().cylinders - 1;
		} else {
			int64_t c,h,s;
			selected_storage(_ch).lba_to_chs(_lba_sect, c,h,s);
			assert(c <= UINT16_MAX);
			assert(h <= UINT8_MAX);
			assert(s <= UINT8_MAX);
			controller->cylinder_no = c;
			controller->head_no = h;
			controller->sector_no = s;
		}
		curr_cyl = controller->cylinder_no;
	}

	return curr_cyl;
}

void StorageCtrl_ATA::ata_tx_sectors(int _ch, bool _write, uint8_t *_buffer, unsigned _len)
{
	int sector_count = (_len / 512);
	assert(sector_count>0);
	uint8_t *bufptr = _buffer;
	HardDiskDrive *hdd = dynamic_cast<HardDiskDrive*>(&selected_storage(_ch));
	if(!hdd) {
		assert(false);
		return;
	}

	PDEBUGF(LOG_V2, LOG_HDD, "%s %d sector(s) at lba=%lld\n",
			_write?"writing":"reading",
			sector_count, calculate_logical_address(_ch));

	int64_t c0,c1;
	int64_t curr_cyl = selected_storage(_ch).lba_to_cylinder(selected_drive(_ch).curr_lba);
	c1 = curr_cyl;
	while(sector_count) {
		int64_t logical_sector = calculate_logical_address(_ch);
		if(logical_sector < 0) {
			PDEBUGF(LOG_V2, LOG_HDD, "ata_read_sector: invalid logical sector\n");
			throw std::exception();
		}

		if(_write) {
			hdd->write_sector(logical_sector, bufptr, 512);
		} else {
			hdd->read_sector(logical_sector, bufptr, 512);
		}

		c0 = c1;
		c1 = increment_address(_ch, logical_sector, 1);
		sector_count--;
		bufptr += 512;
		selected_drive(_ch).next_lba = logical_sector;
	};
	// don't move the head or switch track for the last sector advance
	c1 = c0;
	selected_drive(_ch).curr_lba = selected_drive(_ch).next_lba-1;
	if(curr_cyl != c1) {
		selected_drive(_ch).prev_cyl = curr_cyl;
	}
}

void StorageCtrl_ATA::lba48_transform(Controller &_controller, bool _lba48)
{
	_controller.lba48 = _lba48;

	if(!_controller.lba48) {
		if(!_controller.sector_count) {
			_controller.num_sectors = 256;
		} else {
			_controller.num_sectors = _controller.sector_count;
		}
	} else {
		if(!_controller.sector_count && !_controller.hob.nsector) {
			_controller.num_sectors = 65536;
		} else {
			_controller.num_sectors =
				(_controller.hob.nsector << 8) |
				 _controller.sector_count;
		}
	}
}

uint32_t StorageCtrl_ATA::atapi_seek(int _ch)
{
	StorageDev &storage = selected_storage(_ch);
	Drive &drive = selected_drive(_ch);

	PDEBUGF(LOG_V2, LOG_HDD, "%s: ATAPI: SEEK LBA:%lld -> LBA:%lld\n",
			selected_string(_ch), drive.curr_lba, drive.next_lba);

	int64_t c0,h0,s0;
	int64_t c1,h1,s1;
	storage.lba_to_chs(drive.curr_lba, c0, h0, s0);
	storage.lba_to_chs(drive.next_lba, c1, h1, s1);

	uint32_t move_time = get_seek_time(_ch, c0, c1, -1);
	uint32_t seek_time = move_time;

	if(c0 != c1) {
		selected_storage(_ch).seek(c0, c1);
	}

	return seek_time;
}

uint32_t StorageCtrl_ATA::seek(int _ch, uint64_t _curr_time)
{
	int64_t curr_cyl = selected_storage(_ch).lba_to_cylinder(selected_drive(_ch).curr_lba);
	int64_t dest_cyl = selected_storage(_ch).lba_to_cylinder(selected_drive(_ch).next_lba);

	if(curr_cyl == dest_cyl) {
		int64_t curr_h = selected_storage(_ch).lba_to_head(selected_drive(_ch).curr_lba);
		int64_t dest_h = selected_storage(_ch).lba_to_head(selected_drive(_ch).next_lba);
		if(curr_h != dest_h) {
			selected_ctrl(_ch).look_ahead_time = _curr_time;
		}
		return 0;
	}

	uint32_t seek_time = get_seek_time(_ch, curr_cyl, dest_cyl, selected_drive(_ch).prev_cyl);

	selected_storage(_ch).seek(curr_cyl, dest_cyl);

	selected_drive(_ch).prev_cyl = curr_cyl;
	selected_drive(_ch).curr_lba = selected_drive(_ch).next_lba;
	selected_ctrl(_ch).look_ahead_time = _curr_time + seek_time;

	return seek_time;
}

uint32_t StorageCtrl_ATA::get_seek_time(int _ch, int64_t _c0, int64_t _c1, int64_t _cprev)
{
	if(_c0 == _c1) {
		return 0;
	}

	uint32_t exec_time = SEEK_CMD_US;

	/* I empirically determined that the settling time is 70% of the seek
	 * overhead time derived from spec documents.
	 */
	uint32_t ovrh = selected_storage(_ch).performance().seek_overhead_us * 0.70;
	uint32_t settling_time = 0;
	if(ovrh >= exec_time){
		settling_time = ovrh - exec_time;
	}
	uint32_t move_time = selected_storage(_ch).seek_move_time_us(_c0, _c1);

	if(_c1 == _cprev) {
		/* If a seek returns to the previous cylinder then the controller
		 * takes a lot less time to execute the command.
		 */
		exec_time *= 0.4;
	}

	uint32_t total_seek_time = move_time + settling_time + exec_time;

	PDEBUGF(LOG_V2, LOG_HDD, "%s: SEEK C:%lld->C:%lld  exec:%u, settling:%u, total:%u\n",
			selected_string(_ch),
			_c0, _c1, exec_time, settling_time, total_seek_time);

	return total_seek_time;
}

void StorageCtrl_ATA::activate_command_timer(int _ch, uint32_t _exec_time)
{
	if(_exec_time == 0) {
		_exec_time = MIN_CMD_US;
	}
	uint64_t power_up = selected_storage(_ch).power_up_eta_us();
	if(power_up) {
		PDEBUGF(LOG_V2, LOG_HDD, "%s: drive powering up, command delayed for %lluus\n", selected_string(_ch), power_up);
		_exec_time += power_up;
	}
	g_machine.activate_timer(selected_timer(_ch), uint64_t(_exec_time)*1_us, false);

	PDEBUGF(LOG_V2, LOG_HDD, "%s: command exec time: %dus\n", selected_string(_ch), _exec_time);
}

const char * StorageCtrl_ATA::device_string(int _ch, int _dev)
{
	static char name[50];
	snprintf(name, 50, "ATA%d-%d", _ch, _dev);
	return name;
}

const char * StorageCtrl_ATA::selected_string(int _ch)
{
	return device_string(_ch, m_channels[_ch].drive_select);
}
