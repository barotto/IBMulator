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

/* IBM's proprietary 8-bit HDD interface. It's derived from the XT-IDE interface
 * and it's been used on the PS/1 model 2011, the SEGA TeraDrive, and apparently
 * the PS/2 model 30-286.
 * This implementation is incomplete and almost no error checking is performed,
 * guest code is supposed to be bug free and well behaving.
 * Only DMA data transfer implemented. No PIO mode.
 */

#include "ibmulator.h"
#include "hdc_ps1.h"
#include "hddparams.h"
#include "program.h"
#include "machine.h"
#include "hardware/memory.h"
#include "hardware/devices.h"
#include "hardware/devices/systemboard.h"
#include "hardware/devices/dma.h"
#include "hardware/devices/pic.h"
#include <cstring>


IODEVICE_PORTS(StorageCtrl_PS1) = {
	{ 0x0320, 0x0320, PORT_8BIT|PORT_RW }, // Data Register R / W
	{ 0x0322, 0x0322, PORT_8BIT|PORT_RW }, // Attachment Status Reg R / Attachment Control Reg W
	{ 0x0324, 0x0324, PORT_8BIT|PORT_RW }  // Interrupt Status Reg R / Attention Reg W
};

#define HDC_DMA  3
#define HDC_IRQ  14

#define DEFTIME_US   10u  // default busy time

//Attachment Status Reg bits
#define ASR_TX_EN    0x1  // Transfer Enable
#define ASR_INT_REQ  0x2  // Interrupt Request
#define ASR_BUSY     0x4  // Busy
#define ASR_DIR      0x8  // Direction
#define ASR_DATA_REQ 0x10 // Data Request

//Attention Reg bits
#define ATT_DATA 0x10 // Data Request
#define ATT_SSB  0x20 // Sense Summary Block
#define ATT_CSB  0x40 // Command Specify Block
#define ATT_CCB  0x80 // Command Control Block

//Attachment Control Reg bits
#define ACR_DMA_EN 0x1  // DMA Enable
#define ACR_INT_EN 0x2  // Interrupt Enable
#define ACR_RESET  0x80 // Reset

//Interrupt Status Reg bits
#define ISR_CMD_REJECT  0x20 // Command Reject
#define ISR_INVALID_CMD 0x40 // Invalid Command
#define ISR_TERMINATION 0x80 // Termination Error

//CCB commands
enum CMD {
	READ_DATA   = 0x1,
	READ_CHECK  = 0x2,
	READ_EXT    = 0x3,
	READ_ID     = 0x5,
	RECALIBRATE = 0x8,
	WRITE_DATA  = 0x9,
	WRITE_VFY   = 0xA,
	WRITE_EXT   = 0xB,
	FORMAT_DISK = 0xD,
	SEEK        = 0xE,
	FORMAT_TRK  = 0xF
};

/* These are the command execution times in microseconds.
 * They have been determined through direct observations of a real WDL-330P
 * drive, but only for the READ_DATA, SEEK, and RECALIBRATE commands.
 * Others have been arbitrarily set with the same value as of READ_DATA.
 */
const uint32_t StorageCtrl_PS1::ms_cmd_times[0xF+1] = {
       0, // 0x0 undefined
    2200, // 0x1 READ_DATA
    2200, // 0x2 READ_CHECK
    2200, // 0x3 READ_EXT
       0, // 0x4 undefined
    2200, // 0x5 READ_ID
       0, // 0x6 undefined
       0, // 0x7 undefined
 4000000, // 0x8 RECALIBRATE
    1800, // 0x9 WRITE_DATA TODO a little discount: dual buffering is not implemented for the write
    2200, // 0xA WRITE_VFY
    2200, // 0xB WRITE_EXT
       0, // 0xC undefined
    2200, // 0xD FORMAT_DISK
    2940, // 0xE SEEK
    2200  // 0xF FORMAT_TRK
};

const std::function<void(StorageCtrl_PS1&)> StorageCtrl_PS1::ms_cmd_funcs[0xF+1] = {
	&StorageCtrl_PS1::cmd_undefined,    // 0x0
	&StorageCtrl_PS1::cmd_read_data,    // 0x1
	&StorageCtrl_PS1::cmd_read_check,   // 0x2
	&StorageCtrl_PS1::cmd_read_ext,     // 0x3
	&StorageCtrl_PS1::cmd_undefined,    // 0x4
	&StorageCtrl_PS1::cmd_read_id,      // 0x5
	&StorageCtrl_PS1::cmd_undefined,    // 0x6
	&StorageCtrl_PS1::cmd_undefined,    // 0x7
	&StorageCtrl_PS1::cmd_recalibrate,  // 0x8
	&StorageCtrl_PS1::cmd_write_data,   // 0x9
	&StorageCtrl_PS1::cmd_write_vfy,    // 0xA
	&StorageCtrl_PS1::cmd_write_ext,    // 0xB
	&StorageCtrl_PS1::cmd_undefined,    // 0xC
	&StorageCtrl_PS1::cmd_format_disk,  // 0xD
	&StorageCtrl_PS1::cmd_seek,         // 0xE
	&StorageCtrl_PS1::cmd_format_trk    // 0xF
};

//Sense Summary Block bits
#define SSB_B0_B_NR 7 // not ready;
#define SSB_B0_B_SE 6 // seek end;
#define SSB_B0_B_WF 4 // write fault;
#define SSB_B0_B_CE 3 // cylinder error;
#define SSB_B0_B_T0 0 // on track 0
#define SSB_B1_B_EF 7 // error is on ID field
#define SSB_B1_B_ET 6 // error occurred
#define SSB_B1_B_AM 5 // address mark not found
#define SSB_B1_B_BT 4 // ID field with all bits set detected.
#define SSB_B1_B_WC 3 // cylinder bytes read did not match the cylinder requested in the CCB
#define SSB_B1_B_ID 0 // ID match not found
#define SSB_B2_B_RR 6 // reset needed
#define SSB_B2_B_RG 5 // read or write retry corrected the error
#define SSB_B2_B_DS 4 // defective sector bit in the ID field is 1.

#define SSB_B0_SE (1 << SSB_B0_B_SE)
#define SSB_B0_CE (1 << SSB_B0_B_CE)
#define SSB_B0_T0 (1 << SSB_B0_B_T0)
#define SSB_B1_WC (1 << SSB_B1_B_WC)
#define SSB_B2_RR (1 << SSB_B2_B_RR)



StorageCtrl_PS1::StorageCtrl_PS1(Devices *_dev)
: StorageCtrl(_dev)
{
	memset(&m_s, 0, sizeof(m_s));
}

StorageCtrl_PS1::~StorageCtrl_PS1()
{
}

void StorageCtrl_PS1::install()
{
	StorageCtrl::install();

	using namespace std::placeholders;
	m_devices->dma()->register_8bit_channel(HDC_DMA,
			std::bind(&StorageCtrl_PS1::dma_read, this, _1, _2),
			std::bind(&StorageCtrl_PS1::dma_write, this, _1, _2),
			name());
	g_machine.register_irq(HDC_IRQ, name());

	m_cmd_timer = g_machine.register_timer(
			std::bind(&StorageCtrl_PS1::command_timer, this, _1),
			"HDD-cmd"
	);
	m_dma_timer = g_machine.register_timer(
			std::bind(&StorageCtrl_PS1::dma_timer, this, _1),
			"HDD-dma"
	);

	m_disk.install();

	if(m_disk.type() == HDD_CUSTOM_DRIVE_IDX) {
		HDDParams params;
		params.cylinders  = m_disk.geometry().cylinders;
		params.heads      = m_disk.geometry().heads;
		params.rwcyl      = 0;
		params.wpcyl      = m_disk.geometry().wpcomp;
		params.ECClen     = 0;
		params.options    = (m_disk.geometry().heads>8 ? 0x08 : 0);
		params.timeoutstd = 0;
		params.timeoutfmt = 0;
		params.timeoutchk = 0;
		params.lzone      = m_disk.geometry().lzone;
		params.sectors    = m_disk.geometry().spt;
		params.reserved   = 0;
		g_machine.sys_rom().inject_custom_hdd_params(HDC_CUSTOM_BIOS_IDX, params);
	}
}

void StorageCtrl_PS1::remove()
{
	StorageCtrl::remove();

	m_disk.remove();

	m_devices->dma()->unregister_channel(HDC_DMA);
	g_machine.unregister_irq(HDC_IRQ);
	g_machine.unregister_timer(m_cmd_timer);
	g_machine.unregister_timer(m_dma_timer);
}

void StorageCtrl_PS1::reset(unsigned _type)
{
	if(m_disk.type() == HDD_CUSTOM_DRIVE_IDX) {
		m_s.ssb.drive_type = HDC_CUSTOM_BIOS_IDX;
	} else {
		m_s.ssb.drive_type = m_disk.type();
	}

	lower_interrupt();

	if(m_s.ssb.drive_type && _type == MACHINE_POWER_ON) {
		m_disk.power_on(g_machine.get_virt_time_us());
	}
}

void StorageCtrl_PS1::config_changed()
{
	m_disk.config_changed();
}

void StorageCtrl_PS1::power_off()
{
	StorageCtrl::power_off();
	m_disk.power_off();
	memset(&m_s, 0, sizeof(m_s));
}

void StorageCtrl_PS1::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "PS/1: saving state\n");

	_state.write(&m_s, {sizeof(m_s), name()});
	m_disk.save_state(_state);
}

void StorageCtrl_PS1::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_HDD, "PS/1: restoring state\n");

	_state.read(&m_s, {sizeof(m_s), name()});
	m_disk.restore_state(_state);
}

uint16_t StorageCtrl_PS1::read(uint16_t _address, unsigned)
{
	if(m_disk.type() == 0) {
		return ~0;
	}

	PDEBUGF(LOG_V2, LOG_HDD, "read  0x%04X ", _address);

	//set the Card Selected Feedback bit
	m_devices->sysboard()->set_feedback();

	uint16_t value = 0;
	switch(_address) {
		case 0x320: {
			//Data Reg
			if(!(m_s.attch_status_reg & ASR_DATA_REQ)) {
				PDEBUGF(LOG_V2, LOG_HDD, "null data read\n");
				break;
			}
			if(!(m_s.attch_status_reg & ASR_DIR)) {
				PDEBUGF(LOG_V2, LOG_HDD, "wrong data dir\n");
				break;
			}
			DataBuffer *databuf = get_read_data_buffer();
			assert(databuf);
			assert(databuf->size);
			m_s.attch_status_reg |= ASR_TX_EN;
			value = databuf->stack[databuf->ptr];
			PDEBUGF(LOG_V2, LOG_HDD, "data %02d/%02d   -> 0x%04X\n",
					databuf->ptr, (databuf->size-1), value);
			databuf->ptr++;
			if(databuf->ptr >= databuf->size) {
				m_s.attch_status_reg &= ~ASR_TX_EN;
				m_s.attch_status_reg &= ~ASR_DATA_REQ;
				m_s.attch_status_reg &= ~ASR_DIR;
				databuf->clear();
				//TODO PIO sector data transfer is incomplete (no software available)
			}
			break;
		}
		case 0x322:
			//Attachment Status Reg
			//This register contains status information on the present state of
			//the controller.
			value = m_s.attch_status_reg;
			PDEBUGF(LOG_V2, LOG_HDD, "attch status -> 0x%04X ", value);
			if(value & ASR_TX_EN)   { PDEBUGF(LOG_V2, LOG_HDD, "TX_EN "); }
			if(value & ASR_INT_REQ) { PDEBUGF(LOG_V2, LOG_HDD, "INT_REQ "); }
			if(value & ASR_BUSY)    { PDEBUGF(LOG_V2, LOG_HDD, "BUSY "); }
			if(value & ASR_DIR)     { PDEBUGF(LOG_V2, LOG_HDD, "DIR "); }
			if(value & ASR_DATA_REQ){ PDEBUGF(LOG_V2, LOG_HDD, "DATA_REQ "); }
			PDEBUGF(LOG_V2, LOG_HDD, "\n");
			break;
		case 0x324:
			//Interrupt Status Reg
			//At the end of all commands from the microprocessor, the disk
			//controller returns completion status information to this register.
			//This byte informs the system if an error occurred during the
			//execution of the command.
			value = m_s.int_status_reg;
			PDEBUGF(LOG_V2, LOG_HDD, "int status   -> 0x%04X\n", value);
			m_s.int_status_reg = 0; //<--- TODO is it correct?
			//Int req bit is cleared when this register is read:
			m_s.attch_status_reg &= ~ASR_INT_REQ;
			//lower_interrupt(); //TODO
			break;
		default:
			PERRF(LOG_HDD, "unhandled read!\n", _address);
			break;
	}
	return value;
}

void StorageCtrl_PS1::write(uint16_t _address, uint16_t _value, unsigned)
{
	if(m_disk.type() == 0) {
		return;
	}

	PDEBUGF(LOG_V2, LOG_HDD, "write 0x%04X ", _address);

	//set the Card Selected Feedback bit
	m_devices->sysboard()->set_feedback();

	DataBuffer *databuf = &m_s.sect_buffer[0];

	switch(_address) {
		case 0x320:
			//Data Reg
			if(!(m_s.attch_status_reg & ASR_DATA_REQ)) {
				PDEBUGF(LOG_V2, LOG_HDD, "null data write\n");
				break;
			}
			if(m_s.attch_status_reg & ASR_DIR) {
				PDEBUGF(LOG_V2, LOG_HDD, "wrong data dir\n");
				break;
			}
			assert(databuf);
			assert(databuf->size);
			m_s.attch_status_reg |= ASR_TX_EN;
			PDEBUGF(LOG_V2, LOG_HDD, "data %02d/%02d   <- 0x%04X\n",
					databuf->ptr, (databuf->size-1), _value);
			databuf->stack[databuf->ptr] = _value;
			databuf->ptr++;
			if(databuf->ptr >= databuf->size) {
				m_s.attch_status_reg &= ~ASR_TX_EN;
				m_s.attch_status_reg &= ~ASR_DATA_REQ;
				if(m_s.attention_reg & ATT_DATA) {
					//PIO mode data tx finish
					//TODO the only tested PIO data transfer is of the Format
					//Control Block used by the Format track command
					if((m_s.attention_reg & ATT_CCB) && m_s.ccb.valid) {
						// we are in command mode
						command_timer(g_machine.get_virt_time_ns());
					} else {
						// discard and disable PIO tx
						m_s.attention_reg &= ~ATT_DATA;
					}
				} else {
					databuf->clear();
					attention_block();
				}
			}
			break;
		case 0x322:
			//Attachment Control Reg
			//The Attachment Control register controls the fixed-disk interrupt
			//and DMA channel, and resets the drive.
			PDEBUGF(LOG_V2, LOG_HDD, "attch ctrl   <- 0x%04X ", _value);
			if(_value & ACR_DMA_EN) { PDEBUGF(LOG_V2, LOG_HDD, "DMA_EN "); }
			if(_value & ACR_INT_EN) { PDEBUGF(LOG_V2, LOG_HDD, "INT_EN "); }
			if(_value & ACR_RESET)  { PDEBUGF(LOG_V2, LOG_HDD, "RESET "); }
			PDEBUGF(LOG_V2, LOG_HDD, "\n");
			m_s.attch_ctrl_reg = _value;
			if(!(_value & ACR_INT_EN)) {
				lower_interrupt();
			}
			if(m_s.reset_phase) {
				m_s.reset_phase++;
				if(m_s.reset_phase == 3) {
					raise_interrupt();
					m_s.reset_phase = 0;
				}
				break;
			}
			if(_value & ACR_RESET) {
				reset(MACHINE_HARD_RESET);
				m_s.reset_phase = 1;
				break;
			}
			break;
		case 0x324:
			//Attention Reg
			//The system uses this register to initiate all transactions with
			//the drive.
			PDEBUGF(LOG_V2, LOG_HDD, "attention    <- 0x%04X ", _value);
			if(_value & ATT_DATA) { PDEBUGF(LOG_V2, LOG_HDD, "DATA "); }
			if(_value & ATT_SSB)  { PDEBUGF(LOG_V2, LOG_HDD, "SSB "); }
			if(_value & ATT_CSB)  { PDEBUGF(LOG_V2, LOG_HDD, "CSB "); }
			if(_value & ATT_CCB)  { PDEBUGF(LOG_V2, LOG_HDD, "CCB "); }
			PDEBUGF(LOG_V2, LOG_HDD, "\n");
			if(_value & ATT_DATA) {
				if(!(m_s.attch_status_reg & ASR_DATA_REQ)) {
					//data is not ready, what TODO ?
					PERRF_ABORT(LOG_HDD, "data not ready\n");
				}
				if(m_s.attch_ctrl_reg & ACR_DMA_EN) {
					m_devices->dma()->set_DRQ(HDC_DMA, true);
					//g_machine.activate_timer(m_dma_timer, 500, 0);
				} else {
					//PIO mode
					m_s.attention_reg |= ATT_DATA;
				}
			} else if(_value & ATT_SSB) {
				m_s.attention_reg |= ATT_SSB;
				attention_block();
			} else if(_value & ATT_CCB) {
				databuf->ptr = 0;
				databuf->size = 6;
				m_s.attch_status_reg |= ASR_DATA_REQ;
				m_s.attention_reg |= ATT_CCB;
			}
			break;
		default:
			PERRF(LOG_HDD, "unhandled write!\n", _address);
			break;
	}

}

void StorageCtrl_PS1::exec_command()
{
	uint64_t cur_time_us = g_machine.get_virt_time_us();
	uint32_t seek_time_us = 0;
	uint32_t rot_latency_us = 0;
	uint32_t xfer_time_us = 0;
	uint32_t exec_time_us = m_disk.performance().overh_time * 1000.0
			+ ms_cmd_times[m_s.ccb.command];
	unsigned start_sector = m_s.ccb.sector;
	unsigned head = m_s.ccb.head;
	bool seek = false;

	if(m_s.ccb.auto_seek) {
		//the head arm seeks the correct track
		seek_time_us = get_seek_time(m_s.ccb.cylinder);
		seek = true;
	}

	switch(m_s.ccb.command) {
		case CMD::WRITE_DATA:
			m_s.attch_status_reg |= ASR_DATA_REQ;
			m_s.sect_buffer[0].size = 512;
			m_s.sect_buffer[0].ptr = 0;
			break;
		case CMD::FORMAT_TRK:
			m_s.attch_status_reg |= ASR_DATA_REQ;
			m_s.sect_buffer[0].size = 5;
			m_s.sect_buffer[0].ptr = 0;
			start_sector = 1;
			break;
		case CMD::READ_DATA:
		case CMD::READ_EXT:
			//read the data from the sector, put it into the buffer and
			//transfer it via DMA
			xfer_time_us = m_disk.performance().sec_xfer_us;
			break;
		case CMD::READ_CHECK:
			//read checks are done in 1 operation
			xfer_time_us = (m_disk.performance().sec_read_us * m_disk.performance().interleave)*m_s.ccb.num_sectors;
			break;
		case CMD::SEEK:
			start_sector = 0;
			if(!m_s.ccb.park) {
				seek_time_us = get_seek_time(m_s.ccb.cylinder);
				//seek exec time depends on other factors (see get_seek_time())
				exec_time_us -= ms_cmd_times[CMD::SEEK];
			}
			seek = true;
			break;
		case CMD::RECALIBRATE:
			start_sector = 0;
			head = 0;
			seek_time_us = get_seek_time(0);
			break;
		default:
			break;
	}

	// sectors are 1-based
	if(start_sector > 0) {
		// the sector must align under the head
		uint64_t time_after_seek = cur_time_us + seek_time_us + exec_time_us;
		double pos_after_seek = m_disk.head_position(time_after_seek);
		rot_latency_us = m_disk.rotational_latency_us(pos_after_seek, start_sector);
		m_s.ccb.sect_cnt--;
	}

	m_disk.set_space_time(m_disk.head_position(cur_time_us), cur_time_us);
	set_cur_sector(head, start_sector);
	activate_command_timer(exec_time_us, seek_time_us, rot_latency_us, xfer_time_us);

	if(seek) {
		m_disk.seek(m_s.cur_cylinder, m_s.ccb.cylinder);
	}
}

void StorageCtrl_PS1::exec_read_on_next_sector()
{
	if(m_s.attch_status_reg & ASR_BUSY) {
		//currently reading a sector
		return;
	}
	if(m_s.sect_buffer[0].is_used() && m_s.sect_buffer[1].is_used()) {
		//data has yet to be read by the system
		return;
	}
	m_s.cur_buffer = (m_s.cur_buffer+1) % 2;

	uint32_t seek_time_us = 0;
	uint32_t rot_latency_us = 0;
	unsigned cyl = m_s.cur_cylinder;
	uint64_t cur_time = g_machine.get_virt_time_us();
	double cur_pos = m_disk.head_position(cur_time);

	increment_sector();
	m_s.ccb.sect_cnt--;

	if(cyl != m_s.cur_cylinder) {
		seek_time_us = m_disk.performance().trk2trk_us;
		double pos_after_seek = m_disk.head_position(cur_pos, seek_time_us);
		rot_latency_us = m_disk.rotational_latency_us(pos_after_seek, m_s.cur_sector);
	} else {
		rot_latency_us = m_disk.rotational_latency_us(cur_pos, m_s.cur_sector);
	}

	m_disk.set_space_time(cur_pos, cur_time);
	activate_command_timer(0, seek_time_us, rot_latency_us, m_disk.performance().sec_xfer_us);
}

void StorageCtrl_PS1::attention_block()
{
	if(m_s.attention_reg & ATT_CCB) {
		//we are in command mode
		m_s.ccb.set(m_s.sect_buffer[0].stack);
		if(!m_s.ccb.valid) {
			m_s.int_status_reg |= ISR_INVALID_CMD;
			raise_interrupt();
		} else {
			exec_command();
		}
	} else if(m_s.attention_reg & ATT_SSB) {
		m_s.attention_reg &= ~ATT_SSB;
		if(!m_s.ssb.valid) {
			m_s.ssb.clear();
			m_s.ssb.last_cylinder = m_s.cur_cylinder;
			m_s.ssb.last_head = m_s.cur_head;
			m_s.ssb.last_sector = m_s.cur_sector;
			m_s.ssb.present_cylinder = m_s.cur_cylinder;
			m_s.ssb.present_head = m_s.cur_head;
			m_s.ssb.track_0 = (m_s.cur_cylinder == 0);
		}
		m_s.cur_buffer = 0;
		m_s.ssb.copy_to(m_s.sect_buffer[0].stack);
		fill_data_stack(0, 14);
		m_s.attch_status_reg |= ASR_DIR;
		raise_interrupt();
		m_s.ssb.valid = false;
	}
}

void StorageCtrl_PS1::raise_interrupt()
{
	m_s.attch_status_reg |= ASR_INT_REQ;
	if(m_s.attch_ctrl_reg & ACR_INT_EN) {
		PDEBUGF(LOG_V2, LOG_HDD, "raising IRQ %d\n", HDC_IRQ);
		m_devices->pic()->raise_irq(HDC_IRQ);
	} else {
		PDEBUGF(LOG_V2, LOG_HDD, "flagging INT_REQ in attch status reg\n", HDC_IRQ);
	}
}

void StorageCtrl_PS1::lower_interrupt()
{
	m_devices->pic()->lower_irq(HDC_IRQ);
}

void StorageCtrl_PS1::fill_data_stack(unsigned _buf, unsigned _len)
{
	assert(_buf <= 1);
	assert(_len <= sizeof(DataBuffer::stack));
	m_s.sect_buffer[_buf].ptr = 0;
	m_s.sect_buffer[_buf].size = _len;
	m_s.attch_status_reg |= ASR_DATA_REQ;
}

StorageCtrl_PS1::DataBuffer* StorageCtrl_PS1::get_read_data_buffer()
{
	unsigned bufn = (m_s.cur_buffer+1) % 2;
	if(!m_s.sect_buffer[bufn].is_used()) {
		bufn = m_s.cur_buffer;
		if(!m_s.sect_buffer[bufn].is_used()) {
			return nullptr;
		}
	}
	return &m_s.sect_buffer[bufn];
}

uint16_t StorageCtrl_PS1::dma_write(uint8_t *_buffer, uint16_t _maxlen)
{
	// A DMA write is from I/O to Memory
	// We need to return the next data byte(s) from the buffer
	// to be transfered via the DMA to memory.
	//
	// maxlen is the maximum length of the DMA transfer

	//TODO implement control blocks DMA transfers?
	assert(m_s.ccb.valid);
	assert(m_s.attch_status_reg & ASR_DATA_REQ);
	assert(m_s.attch_status_reg & ASR_DIR);

	m_devices->sysboard()->set_feedback();

	DataBuffer *databuf = get_read_data_buffer();
	assert(databuf);
	uint16_t len = databuf->size - databuf->ptr;

	PDEBUGF(LOG_V2, LOG_HDD, "DMA write: %d / %d bytes\n", _maxlen, len);
	if(len > _maxlen) {
		len = _maxlen;
	}

	memcpy(_buffer, &databuf->stack[databuf->ptr], len);
	databuf->ptr += len;
	bool TC = m_devices->dma()->get_TC() && (len == _maxlen);

	if((databuf->ptr >= databuf->size) || TC) {
		// all data in buffer transferred
		if(databuf->ptr >= databuf->size) {
			databuf->clear();
			m_devices->dma()->set_DRQ(HDC_DMA, false);
		}
		if(TC) { // Terminal Count line, command done
			PDEBUGF(LOG_V2, LOG_HDD, "<<DMA WRITE TC>> C:%d,H:%d,S:%d,nS:%d\n",
					m_s.cur_cylinder, m_s.cur_head,
					m_s.cur_sector, m_s.ccb.sect_cnt);
			command_completed();
		} else {
			exec_read_on_next_sector();
		}
	}
	return len;
}

uint16_t StorageCtrl_PS1::dma_read(uint8_t *_buffer, uint16_t _maxlen)
{
	/*
	 * From Memory to I/O
	 */
	m_devices->sysboard()->set_feedback();

	//TODO implement control blocks DMA transfers?
	assert(m_s.ccb.valid);
	assert(m_s.attch_status_reg & ASR_DATA_REQ);
	assert(!(m_s.attch_status_reg & ASR_DIR));

	uint16_t len = m_s.sect_buffer[0].size - m_s.sect_buffer[0].ptr;
	if(len > _maxlen) {
		len = _maxlen;
	}
	PDEBUGF(LOG_V2, LOG_HDD, "DMA read: %d / %d bytes\n", _maxlen, len);
	memcpy(&m_s.sect_buffer[0].stack[m_s.sect_buffer[0].ptr], _buffer, len);
	m_s.sect_buffer[0].ptr += len;
	bool TC = m_devices->dma()->get_TC() && (len == _maxlen);
	if((m_s.sect_buffer[0].ptr >= m_s.sect_buffer[0].size) || TC) {
		m_s.attch_status_reg &= ~ASR_DATA_REQ;
		unsigned c = m_s.cur_cylinder;
		command_timer(g_machine.get_virt_time_ns());
		if(TC) { // Terminal Count line, done
			PDEBUGF(LOG_V2, LOG_HDD, "<<DMA READ TC>> C:%d,H:%d,S:%d,nS:%d\n",
					m_s.cur_cylinder, m_s.cur_head,
					m_s.cur_sector, m_s.ccb.sect_cnt);
			command_completed();
		} else {
			uint32_t time = m_disk.performance().sec_xfer_us;
			if(c != m_s.cur_cylinder) {
				time += m_disk.performance().trk2trk_us;
			}
			time = std::max(time, DEFTIME_US);
			g_machine.activate_timer(m_dma_timer, uint64_t(time)*1_us, false);
		}
		m_devices->dma()->set_DRQ(HDC_DMA, false);
	}
	return len;
}

uint32_t StorageCtrl_PS1::get_seek_time(unsigned _cyl)
{
	uint32_t exec_time = ms_cmd_times[CMD::SEEK];

	if(m_s.cur_cylinder == _cyl) {
		return exec_time/2;
	}

	/* I empirically determined that the settling time is 70% of the seek
	 * overhead time derived from spec documents.
	 */
	uint32_t settling_time = m_disk.performance().seek_overhead_us * 0.70 - exec_time;
	uint32_t move_time = m_disk.seek_move_time_us(m_s.cur_cylinder, _cyl);

	if(_cyl == m_s.prev_cylinder) {
		/* Analyzing CheckIt and SpinRite benchmarks I came to the conclusion
		 * that if a seek returns to the previous cylinder then the controller
		 * takes a lot less time to execute the command.
		 */
		exec_time *= 0.4;
	}

	uint32_t total_seek_time = move_time + settling_time + exec_time;

	PDEBUGF(LOG_V2, LOG_HDD, "HDD SEEK TIME exec:%d,settling:%d,total:%d\n",
			exec_time, settling_time, total_seek_time);

	return total_seek_time;
}

void StorageCtrl_PS1::activate_command_timer(uint32_t _exec_time, uint32_t _seek_time,
		uint32_t _rot_latency, uint32_t _xfer_time)
{
	uint32_t time_us = _exec_time + _seek_time + _rot_latency + _xfer_time;
	if(time_us == 0) {
		time_us = DEFTIME_US;
	}
	uint64_t spin_up = m_disk.spin_up_eta_us();
	if(spin_up) {
		PDEBUGF(LOG_V2, LOG_HDD, "drive powering up, command delayed for %dus\n", spin_up);
		time_us += spin_up;
	}
	g_machine.activate_timer(m_cmd_timer, uint64_t(time_us)*1_us, false);
	m_s.attch_status_reg |= ASR_BUSY;

	PDEBUGF(LOG_V2, LOG_HDD, "command exec C:%d,H:%d,S:%d,nS:%d: %dus",
			m_s.cur_cylinder, m_s.cur_head,	m_s.cur_sector, m_s.ccb.sect_cnt,
			time_us);
	PDEBUGF(LOG_V2, LOG_HDD, " (exec:%d,seek:%d,rot:%d,xfer:%d), pos:%.2f(%.1f)->%.2f(%d), buf:%d\n",
			_exec_time, _seek_time, _rot_latency, _xfer_time,
			m_disk.head_position(), m_disk.pos_to_sect(m_disk.head_position()),
			m_disk.sect_to_pos(m_disk.hw_sector_number(m_s.cur_sector)),
			m_disk.hw_sector_number(m_s.cur_sector),
			m_s.cur_buffer
			);
}

void StorageCtrl_PS1::command_timer(uint64_t)
{
	if(m_s.attention_reg & ATT_CCB) {
		assert(m_s.ccb.command>=0 && m_s.ccb.command<=0xF);
		m_s.ssb.clear();
		ms_cmd_funcs[m_s.ccb.command](*this);
		m_s.ssb.valid = true; //command functions update the SSB so it's valid
		PDEBUGF(LOG_V2, LOG_HDD, "command exec end: cur.pos: %.2f (%.1f)\n",
				m_disk.head_position(g_machine.get_virt_time_us()),
				m_disk.pos_to_sect(m_disk.head_position(g_machine.get_virt_time_us()))
				);
	} else if(m_s.attention_reg & ATT_CSB) {
		PERRF_ABORT(LOG_HDD, "CSB not implemented\n");
	} else {
		m_s.int_status_reg |= ISR_CMD_REJECT;
		PERRF_ABORT(LOG_HDD, "invalid attention request\n");
	}
	if(!(m_s.attch_status_reg & ASR_BUSY)) {
		g_machine.deactivate_timer(m_cmd_timer);
	}
}

void StorageCtrl_PS1::dma_timer(uint64_t)
{
	m_devices->dma()->set_DRQ(HDC_DMA, true);
	g_machine.deactivate_timer(m_dma_timer);
}

void StorageCtrl_PS1::set_cur_sector(unsigned _h, unsigned _s)
{
	m_s.cur_head = _h;
	if(_h >= m_disk.geometry().heads) {
		PDEBUGF(LOG_V2, LOG_HDD, "seek: head %d >= %d\n", _h, m_disk.geometry().heads);
		m_s.cur_head %= m_disk.geometry().heads;
	}

	//warning: sectors are 1-based
	if(_s > 0) {
		if(_s > m_disk.geometry().spt) {
			PDEBUGF(LOG_V2, LOG_HDD, "seek: sector %d > %d\n", _s, m_disk.geometry().spt);
			m_s.cur_sector = (_s - 1)%m_disk.geometry().spt + 1;
		} else {
			m_s.cur_sector = _s;
		}
	}
}

bool StorageCtrl_PS1::seek(unsigned _c)
{
	if(_c >= m_disk.geometry().cylinders) {
		//TODO is it a temination error?
		//what about command reject and ERP invoked?
		m_s.int_status_reg |= ISR_TERMINATION;
		m_s.ssb.cylinder_err = true;
		PDEBUGF(LOG_V2, LOG_HDD, "seek error: cyl=%d > %d\n", _c, m_disk.geometry().cylinders);
		return false;
	}
	m_s.eoc = false;
	m_s.prev_cylinder = m_s.cur_cylinder;
	m_s.cur_cylinder = _c;
	return true;
}

void StorageCtrl_PS1::increment_sector()
{
	m_s.cur_sector++;
	//warning: sectors are 1-based
	if(m_s.cur_sector > m_disk.geometry().spt) {
		m_s.cur_sector = 1;
		m_s.cur_head++;
		if(m_s.cur_head >= m_disk.geometry().heads) {
			m_s.cur_head = 0;
			m_s.prev_cylinder = m_s.cur_cylinder;
			m_s.cur_cylinder++;
		}

		if(m_s.cur_cylinder >= m_disk.geometry().cylinders) {
			m_s.cur_cylinder = m_disk.geometry().cylinders;
			m_s.eoc = true;
			PDEBUGF(LOG_V2, LOG_HDD, "increment_sector: clamping cylinder to max\n");
		}
	}
}

void StorageCtrl_PS1::read_sector(unsigned _c, unsigned _h, unsigned _s, unsigned _buf)
{
	assert(_buf <= 1);
	PDEBUGF(LOG_V2, LOG_HDD, "SECTOR READ C:%d,H:%d,S:%d -> buf:%d\n",
			_c, _h, _s, _buf);

	m_disk.read_sector(_c, _h, _s, m_s.sect_buffer[_buf].stack);
}

void StorageCtrl_PS1::write_sector(unsigned _c, unsigned _h, unsigned _s, unsigned _buf)
{
	assert(_buf <= 1);
	PDEBUGF(LOG_V2, LOG_HDD, "SECTOR WRITE C:%d,H:%d,S:%d <- buf:%d\n",
			_c, _h, _s, _buf);

	m_disk.write_sector(_c, _h, _s, m_s.sect_buffer[_buf].stack);
}

void StorageCtrl_PS1::cylinder_error()
{
	m_s.int_status_reg |= ISR_TERMINATION;
	m_s.ssb.cylinder_err = true;
	PDEBUGF(LOG_V2, LOG_HDD, "error: cyl > %d\n", m_disk.geometry().cylinders);
}

bool StorageCtrl_PS1::read_auto_seek()
{
	if(m_s.ccb.auto_seek) {
		if(!seek(m_s.ccb.cylinder)) {
			/* When the CCB specifies a cylinder beyond the limit, no step
			 * operation is done and the heads do not move.
			 */
			raise_interrupt();
			return false;
		}
		m_s.ccb.auto_seek = false;
	}
	if(m_s.eoc) {
		cylinder_error();
		raise_interrupt();
		return false;
	}
	return true;
}

uint16_t StorageCtrl_PS1::crc16_ccitt_false(uint8_t *_data, int _len)
{
	/* 16-bit CRC polynomial:
	 * x^16 + x^12 + x^5 + 1
	 *
	 * Rocksoft Model CRC Algorithm parameters:
	 * width=16
	 * poly=0x1021
	 * init=0xffff
	 * refin=false
	 * refout=false
	 * xorout=0x0000
	 * check=0x29b1
	 * name="CRC-16/CCITT-FALSE"
	 */
	const uint16_t poly = 0x1021;
	uint16_t rem  = 0xffff;
	for(int i = 0; i<_len; i++) {
		rem = rem ^ (uint16_t(_data[i]) << 8);
		for(int j=0; j<8; j++) {
			if(rem & 0x8000) {
				rem = (rem << 1) ^ poly;
			} else {
				rem = rem << 1;
			}
		}
	}

	rem = (rem << 8) | (rem >> 8);
	return rem;
}

uint64_t StorageCtrl_PS1::ecc48_noswap(uint8_t *_data, int _len)
{
	/* 48-bit ECC polynomial:
	 * x^48 + x^44 + x^37 + x^32 + x^16 + x^12 + x^5 + 1
	 *
	 * Rocksoft Model CRC Algorithm parameters:
	 * width=48
	 * poly=0x102100011021
	 * init=0x752f00008ad0
	 * refin=false
	 * refout=false
	 * xorout=0x000000000000
	 * check=0xc9980cc2329c
	 *
	 * If we consider a init value of 0xffffffffffff (which is possible
	 * given the available info regarding CRC algo in WD disk controllers)
	 * xorout would be 0xa1bcffff5e43.
	 *
	 * Reverse engineered using:
	 *  http://www.cosc.canterbury.ac.nz/greg.ewing/essays/CRC-Reverse-Engineering.html
	 *  CRC RevEng (http://reveng.sourceforge.net/)
	 *  extra/HDDTEST.C
	 */
	const uint64_t poly = 0x102100011021;
	uint64_t rem = 0x752f00008ad0;

	for(int i = 0; i<_len; i++) {
		rem = rem ^ (uint64_t(_data[i]) << 40);
		for(int j=0; j<8; j++) {
			if(rem & 0x800000000000) {
				rem = (rem << 1) ^ poly;
			} else {
				rem = rem << 1;
			}
		}
	}
	rem &= 0x0000ffffffffffff;
	return rem;
}

void StorageCtrl_PS1::command_completed()
{
	PDEBUGF(LOG_V2, LOG_HDD, "command completed\n");
	m_s.sect_buffer[0].clear();
	m_s.sect_buffer[1].clear();
	m_s.cur_buffer = 0;
	m_s.attention_reg &= ~ATT_CCB;  // command mode off
	m_s.attention_reg &= ~ATT_DATA; // PIO mode off
	m_s.attch_status_reg = 0;
	raise_interrupt();
}

void StorageCtrl_PS1::cmd_read_data()
{
	if(!read_auto_seek()) {
		return;
	}

	read_sector(m_s.cur_cylinder, m_s.cur_head, m_s.cur_sector, m_s.cur_buffer);
	fill_data_stack(m_s.cur_buffer, 512);

	m_s.attch_status_reg |= ASR_DIR;
	m_s.attch_status_reg &= ~ASR_BUSY;

	if(m_s.attch_ctrl_reg & ACR_DMA_EN) {
		m_devices->dma()->set_DRQ(HDC_DMA, true);
	} else {
		//DATA Request required, the OS can decide later if DMA or PIO writing
		//to the attch ctrl reg
		raise_interrupt();
	}
	if(m_s.ccb.sect_cnt > 0) {
		exec_read_on_next_sector();
	}
}

void StorageCtrl_PS1::cmd_read_check()
{
	command_completed();

	if(m_s.ccb.auto_seek) {
		if(!seek(m_s.ccb.cylinder)) {
			return;
		}
	}
	while(m_s.ccb.sect_cnt>0) {
		if(m_s.eoc) {
			cylinder_error();
			return;
		}
		//nothing to do, data checks are always successful
		m_s.ccb.sect_cnt--;
		if(m_s.ccb.sect_cnt > 0) {
			increment_sector();
		}
	}
}

void StorageCtrl_PS1::cmd_read_ext()
{
	if(!read_auto_seek()) {
		return;
	}
	read_sector(m_s.cur_cylinder, m_s.cur_head, m_s.cur_sector, 0);
	fill_data_stack(0, 518);
	// Initialize the parity buffer
	memset(&m_s.sect_buffer[0].stack[512], 0, 6);
	if(!m_s.ccb.ecc) {
		//CRC
		//http://www.dataclinic.co.uk/hard-disk-crc/
		/* The divisor or generator polynomial used for hard disk drives is
		 * defined as 11021h or x^16 + x^12 + x^5 + 1 (CRC-16-CCITT)
		 * The data sector is made up of 512 bytes. If this is extended by 2
		 * bytes of 0 lengths, the new sector is 514 bytes in size. A checksum
		 * can be calculated for this 514 byte sector using modulo-2 and this
		 * will be 2 bytes in width. If the 2 zero width bytes of the 514 sector
		 * are replaced by the checksum evaluated, a method for detecting errors
		 * has been integrated into the sector. This is because on calculating
		 * the checksum of this new 514 byte sector, this will result in a
		 * remainder of 0. If the remainder is not zero, it implies an error has
		 * occurred.
		 * Therefore, when the device controller writes data on to the platters,
		 * it includes 2 bytes for the CRC checksum in each sector. On reading
		 * back the sectors, if the checksum is not equal to 0, then an error
		 * has occurred.
		 */
		/* According to http://reveng.sourceforge.net/crc-catalogue/16.htm
		 * the CRC-16 variant used in disk controllers and floppy disc formats
		 * is CRC-16/CCITT-FALSE. I assume the same variant is used here,
		 * although I can't test if it's true.
		 */
		uint16_t crc = crc16_ccitt_false(m_s.sect_buffer[0].stack, 514);
		*((uint16_t*)&m_s.sect_buffer[0].stack[512]) = crc;
	} else {
		//ECC
		/* The ECC used in Winchester controllers of the '80s was a computer
		 * generated 32-bit CRC, or a 48-bit variant for more recent
		 * controllers, until the '90s when the Reed-Solomon algorithm
		 * superseded them.
		 * The PS/1's HDD controller uses a 48-bit ECC.
		 */
		uint64_t ecc48 = ecc48_noswap(m_s.sect_buffer[0].stack, 512);
		uint8_t *eccptr = (uint8_t *)&ecc48;
		m_s.sect_buffer[0].stack[512] = eccptr[5];
		m_s.sect_buffer[0].stack[513] = eccptr[4];
		m_s.sect_buffer[0].stack[514] = eccptr[3];
		m_s.sect_buffer[0].stack[515] = eccptr[2];
		m_s.sect_buffer[0].stack[516] = eccptr[1];
		m_s.sect_buffer[0].stack[517] = eccptr[0];
	}

	m_s.attch_status_reg |= ASR_DIR;

	if(m_s.attch_ctrl_reg & ACR_DMA_EN) {
		m_devices->dma()->set_DRQ(HDC_DMA, true);
	} else {
		raise_interrupt();
	}
}

void StorageCtrl_PS1::cmd_read_id()
{
	PERRF_ABORT(LOG_HDD, "READ_ID: command not implemented\n");
}

void StorageCtrl_PS1::cmd_recalibrate()
{
	seek(0);
	command_completed();
}

void StorageCtrl_PS1::cmd_write_data()
{
	if(m_s.ccb.auto_seek) {
		if(!seek(m_s.ccb.cylinder)) {
			/* When the CCB specifies a cylinder beyond the limit, no step
			 * operation is done and the heads do not move.
			 */
			raise_interrupt();
			return;
		}
		m_s.ccb.auto_seek = false;
	}
	if(!(m_s.attch_status_reg & ASR_DATA_REQ)) {
		assert(m_s.sect_buffer[0].size == 512);
		assert(m_s.sect_buffer[0].ptr == 512);
		assert(m_s.ccb.sect_cnt>=0);

		if(m_s.eoc) {
			cylinder_error();
			raise_interrupt();
			return;
		}

		write_sector(m_s.cur_cylinder, m_s.cur_head, m_s.cur_sector, 0);

		m_s.sect_buffer[0].ptr = 0;
		if(m_s.ccb.sect_cnt > 0) {
			increment_sector();
			m_s.ccb.sect_cnt--;
			m_s.attch_status_reg |= ASR_DATA_REQ;
			m_s.sect_buffer[0].size = 512;
		}
	} else {
		m_s.attch_status_reg &= ~ASR_BUSY;
		raise_interrupt();
	}
}

void StorageCtrl_PS1::cmd_write_vfy()
{
	PERRF_ABORT(LOG_HDD, "WRITE_VFY: command not implemented\n");
}

void StorageCtrl_PS1::cmd_write_ext()
{
	PERRF_ABORT(LOG_HDD, "WRITE_EXT: command not implemented\n");
}

void StorageCtrl_PS1::cmd_format_disk()
{
	PERRF_ABORT(LOG_HDD, "FORMAT_DISK: command not implemented\n");
}

void StorageCtrl_PS1::cmd_seek()
{
	if(m_s.ccb.park) {
		//not really a park...
		seek(0);
	} else {
		seek(m_s.ccb.cylinder);
	}
	command_completed();
}

void StorageCtrl_PS1::cmd_format_trk()
{
	// This command needs a Format Control Block which is transferred via PIO
	assert(!(m_s.attch_ctrl_reg & ACR_DMA_EN));

	if(!(m_s.attch_status_reg & ASR_DATA_REQ)) {
		if((m_s.ccb.num_sectors&1) && m_s.ccb.sect_cnt<0) {
			// the extra byte has been transferred, nothing else to do
			command_completed();
			return;
		}
		if(m_s.eoc) {
			cylinder_error();
			raise_interrupt();
			return;
		}

		//nothing to do, we are not really formatting anything
		PDEBUGF(LOG_V2, LOG_HDD, "SECTOR FORMAT: ID's sect num: %d\n",
				m_s.sect_buffer[0].stack[2]
				);

		m_s.sect_buffer[0].ptr = 0;
		if(m_s.ccb.sect_cnt == 0) {
			if(m_s.ccb.num_sectors & 1) {
				/* The control block must contain an even number of bytes. If an
				 * odd number of sectors are being formatted, an additional byte
				 * is sent with all bits 0.
				*/
				PDEBUGF(LOG_V2, LOG_HDD, "FORMAT_TRK: odd number of sectors\n");
				m_s.sect_buffer[0].size = 1;
				m_s.ccb.sect_cnt--;
				m_s.attch_status_reg |= ASR_DATA_REQ;
			} else {
				command_completed();
			}
		} else {
			increment_sector();
			m_s.ccb.sect_cnt--;
			m_s.attch_status_reg |= ASR_DATA_REQ;
		}
	} else {
		m_s.attch_status_reg &= ~ASR_BUSY;
		raise_interrupt();
	}
}

void StorageCtrl_PS1::cmd_undefined()
{
	PERRF_ABORT(LOG_HDD, "unknown command!\n");
}

void StorageCtrl_PS1::State::CCB::set(uint8_t* _data)
{
	valid = true;

	command = _data[0] >> 4;
	no_data = (_data[0] >> 3) & 1; //ND
	auto_seek = (_data[0] >> 2) & 1; //AS
	park = _data[0] & 1; // EC/P
	head = _data[1] >> 4;
	cylinder = ((_data[1] & 3) << 8) + _data[2];
	sector = _data[3];
	num_sectors = _data[5];
	sect_cnt = num_sectors;

	PDEBUGF(LOG_V1, LOG_HDD, "command: ");
	switch(command) {
		case CMD::READ_DATA:   { PDEBUGF(LOG_V1, LOG_HDD, "READ_DATA "); break; }
		case CMD::READ_CHECK:  { PDEBUGF(LOG_V1, LOG_HDD, "READ_CHECK "); break; }
		case CMD::READ_EXT:    { PDEBUGF(LOG_V1, LOG_HDD, "READ_EXT "); break; }
		case CMD::READ_ID:     { PDEBUGF(LOG_V1, LOG_HDD, "READ_ID "); break; }
		case CMD::RECALIBRATE: { PDEBUGF(LOG_V1, LOG_HDD, "RECALIBRATE "); break; }
		case CMD::WRITE_DATA:  { PDEBUGF(LOG_V1, LOG_HDD, "WRITE_DATA "); break; }
		case CMD::WRITE_VFY:   { PDEBUGF(LOG_V1, LOG_HDD, "WRITE_VFY "); break; }
		case CMD::WRITE_EXT:   { PDEBUGF(LOG_V1, LOG_HDD, "WRITE_EXT "); break; }
		case CMD::FORMAT_DISK: { PDEBUGF(LOG_V1, LOG_HDD, "FORMAT_DISK "); break; }
		case CMD::SEEK:        { PDEBUGF(LOG_V1, LOG_HDD, "SEEK "); break; }
		case CMD::FORMAT_TRK:  { PDEBUGF(LOG_V1, LOG_HDD, "FORMAT_TRK "); break; }
		default:
			PDEBUGF(LOG_V1, LOG_HDD, "invalid!\n");
			valid = false;
			return;
	}

	PDEBUGF(LOG_V1, LOG_HDD, " C:%d,H:%d,S:%d,nS:%d\n",
			cylinder, head, sector, num_sectors);
}

void StorageCtrl_PS1::State::SSB::copy_to(uint8_t *_dest)
{
	_dest[0] = not_ready << SSB_B0_B_NR;
	_dest[0] |= seek_end << SSB_B0_B_SE;
	_dest[0] |= cylinder_err << SSB_B0_B_CE;
	_dest[0] |= track_0 << SSB_B0_B_T0;
	_dest[1] = 0;
	_dest[2] = reset << SSB_B2_B_RR;
	_dest[3] = last_cylinder & 0xFF;
	_dest[4] = ((last_cylinder & 0x300) >> 3) + last_head;
	_dest[5] = last_sector;
	_dest[6] = 0x2; //sector size: the value is always hex 02 to indicate 512 bytes.
	_dest[7] = (present_head << 4) + ((present_cylinder & 0x300)>>8);
	_dest[8] = present_cylinder & 0xFF;
	_dest[9] = 0;
	_dest[10] = 0;
	_dest[11] = command_syndrome;
	_dest[12] = drive_type;
	_dest[13] = 0;
}

void StorageCtrl_PS1::State::SSB::clear()
{
	not_ready = false;
	seek_end = false;
	cylinder_err = false;
	track_0 = false;
	reset = false;
	present_head = 0;
	present_cylinder = 0;
	last_head = 0;
	last_cylinder = 0;
	last_sector = 0;
	command_syndrome = 0;

	//drive_type is static
}
