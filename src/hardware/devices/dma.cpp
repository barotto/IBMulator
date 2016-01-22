/*
 * Copyright (C) 2002-2014  The Bochs Project
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

#include "ibmulator.h"
#include "machine.h"
#include "dma.h"
#include "hardware/devices.h"
#include "hardware/cpu.h"
#include <cstring>

DMA g_dma;


#define DMA_MODE_DEMAND  0
#define DMA_MODE_SINGLE  1
#define DMA_MODE_BLOCK   2
#define DMA_MODE_CASCADE 3


DMA::DMA()
{

}

DMA::~DMA()
{
}

void DMA::init(void)
{
	unsigned i;

	// 0000..000F
	for(i=0x0000; i<=0x000F; i++) {
		g_devices.register_read_handler(this, i, 1);
		g_devices.register_write_handler(this, i, 3);
	}

	// 00080..008F
	for(i=0x0080; i<=0x008F; i++) {
		g_devices.register_read_handler(this, i, 1);
		g_devices.register_write_handler(this, i, 3);
	}

	// 000C0..00DE
	for(i=0x00C0; i<=0x00DE; i+=2) {
		g_devices.register_read_handler(this, i, 1);
		g_devices.register_write_handler(this, i, 3);
	}

	memset(&m_chused, 0, sizeof(m_chused));
	m_chused[1][0] = true; // cascade channel in use
	PDEBUGF(LOG_V2, LOG_DMA, "channel 4 used by cascade\n");
}

void DMA::config_changed()
{
	//nothing to do.
	//no config dependent params
}

void DMA::reset(unsigned type)
{
	if(type==MACHINE_POWER_ON) {

		memset(&m_s, 0, sizeof(m_s));

		//everything is memsetted to 0, so is the following code relevant?
		unsigned c, i, j;
		for(i=0; i < 2; i++) {
			for(j=0; j < 4; j++) {
				m_s.dma[i].DRQ[j] = 0;
				m_s.dma[i].DACK[j] = 0;
			}
		}
		m_s.HLDA = false;
		m_s.TC = false;

		for(i=0; i<2; i++) {
			for(c=0; c<4; c++) {
				m_s.dma[i].chan[c].mode.mode_type = 0;         // demand mode
				m_s.dma[i].chan[c].mode.address_decrement = 0; // address increment
				m_s.dma[i].chan[c].mode.autoinit_enable = 0;   // autoinit disable
				m_s.dma[i].chan[c].mode.transfer_type = 0;     // verify
				m_s.dma[i].chan[c].base_address = 0;
				m_s.dma[i].chan[c].current_address = 0;
				m_s.dma[i].chan[c].base_count = 0;
				m_s.dma[i].chan[c].current_count = 0;
				m_s.dma[i].chan[c].page_reg = 0;
			}
		}
	}

	//HARD reset and POWER ON
	reset_controller(0);
	reset_controller(1);
}

void DMA::save_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_DMA, "saving state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.write(&m_s,h);
}

void DMA::restore_state(StateBuf &_state)
{
	PINFOF(LOG_V1, LOG_DMA, "restoring state\n");

	StateHeader h;
	h.name = get_name();
	h.data_size = sizeof(m_s);
	_state.read(&m_s,h);
}

void DMA::reset_controller(unsigned num)
{
	m_s.dma[num].mask[0] = 1;
	m_s.dma[num].mask[1] = 1;
	m_s.dma[num].mask[2] = 1;
	m_s.dma[num].mask[3] = 1;
	m_s.dma[num].ctrl_disabled = 0;
	m_s.dma[num].command_reg = 0;
	m_s.dma[num].status_reg = 0;
	m_s.dma[num].flip_flop = 0;
}

// index to find channel from register number (only [0],[1],[2],[6] used)
uint8_t channelindex[7] = {2, 3, 1, 0, 0, 0, 0};

uint16_t DMA::read(uint16_t address, unsigned /*io_len*/)
{
	uint8_t retval;
	uint8_t channel;

	PDEBUGF(LOG_V2, LOG_DMA, "read addr=%04x\n", (unsigned) address);

	bool ma_sl = (address >= 0xc0);

	switch (address) {
		case 0x00: /* DMA-1 current address, channel 0 */
		case 0x02: /* DMA-1 current address, channel 1 */
		case 0x04: /* DMA-1 current address, channel 2 */
		case 0x06: /* DMA-1 current address, channel 3 */
		case 0xc0: /* DMA-2 current address, channel 0 */
		case 0xc4: /* DMA-2 current address, channel 1 */
		case 0xc8: /* DMA-2 current address, channel 2 */
		case 0xcc: /* DMA-2 current address, channel 3 */
			channel = (address >> (1 + ma_sl)) & 0x03;
			if(m_s.dma[ma_sl].flip_flop==0) {
				m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
				return (m_s.dma[ma_sl].chan[channel].current_address & 0xff);
			} else {
				m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
				return (m_s.dma[ma_sl].chan[channel].current_address >> 8);
			}

		case 0x01: /* DMA-1 current count, channel 0 */
		case 0x03: /* DMA-1 current count, channel 1 */
		case 0x05: /* DMA-1 current count, channel 2 */
		case 0x07: /* DMA-1 current count, channel 3 */
		case 0xc2: /* DMA-2 current count, channel 0 */
		case 0xc6: /* DMA-2 current count, channel 1 */
		case 0xca: /* DMA-2 current count, channel 2 */
		case 0xce: /* DMA-2 current count, channel 3 */
			channel = (address >> (1 + ma_sl)) & 0x03;
			if(m_s.dma[ma_sl].flip_flop==0) {
				m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
				return (m_s.dma[ma_sl].chan[channel].current_count & 0xff);
			} else {
				m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
				return (m_s.dma[ma_sl].chan[channel].current_count >> 8);
			}

		case 0x08: // DMA-1 Status Register
		case 0xd0: // DMA-2 Status Register
			// bit 7: 1 = channel 3 request
			// bit 6: 1 = channel 2 request
			// bit 5: 1 = channel 1 request
			// bit 4: 1 = channel 0 request
			// bit 3: 1 = channel 3 has reached terminal count
			// bit 2: 1 = channel 2 has reached terminal count
			// bit 1: 1 = channel 1 has reached terminal count
			// bit 0: 1 = channel 0 has reached terminal count
			// reading this register clears lower 4 bits (hold flags)
			retval = m_s.dma[ma_sl].status_reg;
			m_s.dma[ma_sl].status_reg &= 0xf0;
			return retval;

		case 0x0d: // DMA-1: temporary register
		case 0xda: // DMA-2: temporary register
			// only used for memory-to-memory transfers
			// write to 0x0d / 0xda clears temporary register
			PERRF(LOG_DMA, "DMA-%d: read of temporary register always returns 0\n", ma_sl+1);
			return 0;

		case 0x0081: // DMA-1 page register, channel 2
		case 0x0082: // DMA-1 page register, channel 3
		case 0x0083: // DMA-1 page register, channel 1
		case 0x0087: // DMA-1 page register, channel 0
			channel = channelindex[address - 0x81];
			return m_s.dma[0].chan[channel].page_reg;

		case 0x0089: // DMA-2 page register, channel 2
		case 0x008a: // DMA-2 page register, channel 3
		case 0x008b: // DMA-2 page register, channel 1
		case 0x008f: // DMA-2 page register, channel 0
			channel = channelindex[address - 0x89];
			return m_s.dma[1].chan[channel].page_reg;

		case 0x0080:
		case 0x0084:
		case 0x0085:
		case 0x0086:
		case 0x0088:
		case 0x008c:
		case 0x008d:
		case 0x008e:
			PDEBUGF(LOG_V2, LOG_DMA, "read: extra page register 0x%04x (unused)\n", address);
			return m_s.ext_page_reg[address & 0x0f];

		case 0x0f: // DMA-1: undocumented: read all mask bits
		case 0xde: // DMA-2: undocumented: read all mask bits
			retval = m_s.dma[ma_sl].mask[0] |
			   (m_s.dma[ma_sl].mask[1] << 1) |
			   (m_s.dma[ma_sl].mask[2] << 2) |
			   (m_s.dma[ma_sl].mask[3] << 3);
			return (0xf0 | retval);

		default:
			PERRF(LOG_DMA, "read ignored at port %04xh\n", address);
			return 0;
	}
}

void DMA::write(uint16_t address, uint16_t value, unsigned io_len)
{
	uint8_t set_mask_bit;
	uint8_t channel;

	if(io_len > 1) {
		if((io_len == 2) && (address == 0x0b)) {
			write(address,   value & 0xff, 1);
			write(address+1, value >> 8,   1);
			return;
		}
		PERRF(LOG_DMA, "write to address %08x, len=%u\n", address, io_len);
		return;
	}

	PDEBUGF(LOG_V2, LOG_DMA, "write: address=%04x value=%02x\n", address, value);

	bool ma_sl = (address >= 0xc0);

	switch (address) {
		case 0x00:
		case 0x02:
		case 0x04:
		case 0x06:
		case 0xc0:
		case 0xc4:
		case 0xc8:
		case 0xcc:
			channel = (address >> (1 + ma_sl)) & 0x03;
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d base and current address, channel %d\n", ma_sl+1, channel);
			if(m_s.dma[ma_sl].flip_flop==0) { /* 1st byte */
				m_s.dma[ma_sl].chan[channel].base_address = value;
				m_s.dma[ma_sl].chan[channel].current_address = value;
			} else { /* 2nd byte */
				m_s.dma[ma_sl].chan[channel].base_address |= (value << 8);
				m_s.dma[ma_sl].chan[channel].current_address |= (value << 8);
				PDEBUGF(LOG_V2, LOG_DMA, "    base = %04x\n", m_s.dma[ma_sl].chan[channel].base_address);
				PDEBUGF(LOG_V2, LOG_DMA, "    curr = %04x\n", m_s.dma[ma_sl].chan[channel].current_address);
			}
			m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
			break;

		case 0x01:
		case 0x03:
		case 0x05:
		case 0x07:
		case 0xc2:
		case 0xc6:
		case 0xca:
		case 0xce:
			channel = (address >> (1 + ma_sl)) & 0x03;
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d base and current count, channel %d\n", ma_sl+1, channel);
			if(m_s.dma[ma_sl].flip_flop==0) { /* 1st byte */
				m_s.dma[ma_sl].chan[channel].base_count = value;
				m_s.dma[ma_sl].chan[channel].current_count = value;
			} else { /* 2nd byte */
				m_s.dma[ma_sl].chan[channel].base_count |= (value << 8);
				m_s.dma[ma_sl].chan[channel].current_count |= (value << 8);
				PDEBUGF(LOG_V2, LOG_DMA, "    base = %04x\n", m_s.dma[ma_sl].chan[channel].base_count);
				PDEBUGF(LOG_V2, LOG_DMA, "    curr = %04x\n", m_s.dma[ma_sl].chan[channel].current_count);
			}
			m_s.dma[ma_sl].flip_flop = !m_s.dma[ma_sl].flip_flop;
			break;

		case 0x08: /* DMA-1: command register */
		case 0xd0: /* DMA-2: command register */
			if((value & 0xfb) != 0x00)
				PERRF(LOG_DMA, "write to command register: value 0x%02x not supported\n", value);
			m_s.dma[ma_sl].command_reg = value;
			m_s.dma[ma_sl].ctrl_disabled = (value >> 2) & 0x01;
			control_HRQ(ma_sl);
			break;

		case 0x09: // DMA-1: request register
		case 0xd2: // DMA-2: request register
			channel = value & 0x03;
			// note: write to 0x0d / 0xda clears this register
			if(value & 0x04) {
				// set request bit
				m_s.dma[ma_sl].status_reg |= (1 << (channel+4));
				PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d: set request bit for channel %u\n", ma_sl+1, channel);
			} else {
				// clear request bit
				m_s.dma[ma_sl].status_reg &= ~(1 << (channel+4));
				PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d: cleared request bit for channel %u\n", ma_sl+1, channel);
			}
			control_HRQ(ma_sl);
			break;

		case 0x0a:
		case 0xd4:
			set_mask_bit = value & 0x04;
			channel = value & 0x03;
			m_s.dma[ma_sl].mask[channel] = (set_mask_bit > 0);
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d: set_mask_bit=%u, channel=%u, mask now=%02xh\n",
					ma_sl+1, set_mask_bit, channel, m_s.dma[ma_sl].mask[channel]);
			control_HRQ(ma_sl);
			break;

		case 0x0b: /* DMA-1 mode register */
		case 0xd6: /* DMA-2 mode register */
			channel = value & 0x03;
			m_s.dma[ma_sl].chan[channel].mode.mode_type = (value >> 6) & 0x03;
			m_s.dma[ma_sl].chan[channel].mode.address_decrement = (value >> 5) & 0x01;
			m_s.dma[ma_sl].chan[channel].mode.autoinit_enable = (value >> 4) & 0x01;
			m_s.dma[ma_sl].chan[channel].mode.transfer_type = (value >> 2) & 0x03;
			PDEBUGF(LOG_V2, LOG_DMA,
					"  DMA-%d: mode register[%u]: mode=%u, dec=%u, autoinit=%u, txtype=%u\n",
					ma_sl+1, channel,
					m_s.dma[ma_sl].chan[channel].mode.mode_type,
					m_s.dma[ma_sl].chan[channel].mode.address_decrement,
					m_s.dma[ma_sl].chan[channel].mode.autoinit_enable,
					m_s.dma[ma_sl].chan[channel].mode.transfer_type);
			break;

		case 0x0c: /* DMA-1 clear byte flip/flop */
		case 0xd8: /* DMA-2 clear byte flip/flop */
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d: clear flip/flop\n", ma_sl+1);
			m_s.dma[ma_sl].flip_flop = 0;
			break;

		case 0x0d: // DMA-1: master clear
		case 0xda: // DMA-2: master clear
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d: master clear\n", ma_sl+1);
			// writing any value to this port resets DMA controller 1 / 2
			// same action as a hardware reset
			// mask register is set (chan 0..3 disabled)
			// command, status, request, temporary, and byte flip-flop are all cleared
			reset_controller(ma_sl);
			break;

		case 0x0e: // DMA-1: clear mask register
		case 0xdc: // DMA-2: clear mask register
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d: clear mask register\n", ma_sl+1);
			m_s.dma[ma_sl].mask[0] = 0;
			m_s.dma[ma_sl].mask[1] = 0;
			m_s.dma[ma_sl].mask[2] = 0;
			m_s.dma[ma_sl].mask[3] = 0;
			control_HRQ(ma_sl);
			break;

		case 0x0f: // DMA-1: write all mask bits
		case 0xde: // DMA-2: write all mask bits
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-%d: write all mask bits\n", ma_sl+1);
			m_s.dma[ma_sl].mask[0] = value & 0x01; value >>= 1;
			m_s.dma[ma_sl].mask[1] = value & 0x01; value >>= 1;
			m_s.dma[ma_sl].mask[2] = value & 0x01; value >>= 1;
			m_s.dma[ma_sl].mask[3] = value & 0x01;
			control_HRQ(ma_sl);
			break;

		case 0x81: /* DMA-1 page register, channel 2 */
		case 0x82: /* DMA-1 page register, channel 3 */
		case 0x83: /* DMA-1 page register, channel 1 */
		case 0x87: /* DMA-1 page register, channel 0 */
			/* address bits A16-A23 for DMA channel */
			channel = channelindex[address - 0x81];
			m_s.dma[0].chan[channel].page_reg = value;
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-1: page register %d = %02x\n", channel, value);
			break;

		case 0x89: /* DMA-2 page register, channel 2 */
		case 0x8a: /* DMA-2 page register, channel 3 */
		case 0x8b: /* DMA-2 page register, channel 1 */
		case 0x8f: /* DMA-2 page register, channel 0 */
			/* address bits A16-A23 for DMA channel */
			channel = channelindex[address - 0x89];
			m_s.dma[1].chan[channel].page_reg = value;
			PDEBUGF(LOG_V2, LOG_DMA, "  DMA-2: page register %d = %02x\n", channel + 4, value);
			break;

		case 0x0080:
		case 0x0084:
		case 0x0085:
		case 0x0086:
		case 0x0088:
		case 0x008c:
		case 0x008d:
		case 0x008e:
			PDEBUGF(LOG_V2, LOG_DMA, "write: extra page register 0x%04x (unused)\n", address);
			m_s.ext_page_reg[address & 0x0f] = value;
			break;

		default:
			PERRF(LOG_DMA, "write ignored: %04xh = %02xh\n", address, value);
			break;
	}
}

void DMA::set_DRQ(unsigned channel, bool val)
{
	uint32_t dma_base, dma_roof;
	uint8_t ma_sl;

	if(channel > 7) {
		PERRF(LOG_DMA, "set_DRQ() channel > 7\n");
		return;
	}
	ma_sl = (channel > 3)?1:0;
	m_s.dma[ma_sl].DRQ[channel & 0x03] = val;
	if(!m_chused[ma_sl][channel & 0x03]) {
		PERRF(LOG_DMA, "set_DRQ(): channel %d not connected to device\n", channel);
		return;
	}
	channel &= 0x03;
	if(!val) {
		// clear bit in status reg
		m_s.dma[ma_sl].status_reg &= ~(1 << (channel+4));
		control_HRQ(ma_sl);
		return;
	}

	m_s.dma[ma_sl].status_reg |= (1 << (channel+4));

	if((m_s.dma[ma_sl].chan[channel].mode.mode_type != DMA_MODE_SINGLE) &&
	  (m_s.dma[ma_sl].chan[channel].mode.mode_type != DMA_MODE_DEMAND) &&
	  (m_s.dma[ma_sl].chan[channel].mode.mode_type != DMA_MODE_CASCADE))
	{
		PERRF(LOG_DMA, "set_DRQ: mode_type(%02x) not handled\n",
				m_s.dma[ma_sl].chan[channel].mode.mode_type);
		return;
	}

	dma_base = (m_s.dma[ma_sl].chan[channel].page_reg << 16) |
	           (m_s.dma[ma_sl].chan[channel].base_address << ma_sl);
	if(m_s.dma[ma_sl].chan[channel].mode.address_decrement) {
		dma_roof = dma_base - (m_s.dma[ma_sl].chan[channel].base_count << ma_sl);
	} else {
		dma_roof = dma_base + (m_s.dma[ma_sl].chan[channel].base_count << ma_sl);
	}
	if(channel!=0 && ((dma_base & (0x7fff0000 << ma_sl)) != (dma_roof & (0x7fff0000 << ma_sl)))) {
		PERRF(LOG_DMA, "dma_base = 0x%08x\n", dma_base);
		PERRF(LOG_DMA, "dma_base_count = 0x%08x\n", m_s.dma[ma_sl].chan[channel].base_count);
		PERRF(LOG_DMA, "dma_roof = 0x%08x\n", dma_roof);
		PERRF(LOG_DMA, "request outside %dk boundary\n", 64 << ma_sl);
		return;
	}

	control_HRQ(ma_sl);
}

bool DMA::get_DRQ(uint channel)
{
	uint8_t ma_sl = (channel > 3)?1:0;
	return m_s.dma[ma_sl].DRQ[channel & 0x03];
}

void DMA::control_HRQ(uint8_t ma_sl)
{
	unsigned channel;

	// do nothing if controller is disabled
	if(m_s.dma[ma_sl].ctrl_disabled)
		return;

	// deassert HRQ if no DRQ is pending
	if((m_s.dma[ma_sl].status_reg & 0xf0) == 0) {
		if(ma_sl) {
			g_cpu.set_HRQ(false);
		} else {
			set_DRQ(4, 0);
		}
		return;
	}
	// find highest priority channel
	for(channel=0; channel<4; channel++) {
		if((m_s.dma[ma_sl].status_reg & (1 << (channel+4))) && (m_s.dma[ma_sl].mask[channel]==0)) {
			if(ma_sl) {
				// assert Hold ReQuest line to CPU
				g_cpu.set_HRQ(true);
			} else {
				// send DRQ to cascade channel of the master
				set_DRQ(4, 1);
			}
			break;
		}
	}
}

void DMA::raise_HLDA(void)
{
	unsigned channel;
	uint32_t phy_addr;
	uint8_t ma_sl = 0;
	uint16_t maxlen, len = 1;
	uint8_t buffer[DMA_BUFFER_SIZE];

	m_s.HLDA = true;
	// find highest priority channel
	for(channel=0; channel<4; channel++) {
		if((m_s.dma[1].status_reg & (1 << (channel+4))) && (m_s.dma[1].mask[channel]==0)) {
			ma_sl = 1;
			break;
		}
	}
	if(channel == 0) { // master cascade channel
		m_s.dma[1].DACK[0] = 1;
		for(channel=0; channel<4; channel++) {
			if((m_s.dma[0].status_reg & (1 << (channel+4))) && (m_s.dma[0].mask[channel]==0)) {
				ma_sl = 0;
				break;
			}
		}
	}
	if(channel >= 4) {
		// wait till they're unmasked
		return;
	}

	phy_addr = (m_s.dma[ma_sl].chan[channel].page_reg << 16) |
	           (m_s.dma[ma_sl].chan[channel].current_address << ma_sl);

	if(!m_s.dma[ma_sl].chan[channel].mode.address_decrement) {
		maxlen = (m_s.dma[ma_sl].chan[channel].current_count + 1) << ma_sl;
		m_s.TC = (maxlen <= DMA_BUFFER_SIZE);
		if(maxlen > DMA_BUFFER_SIZE) {
			maxlen = DMA_BUFFER_SIZE;
		}
	} else {
		m_s.TC = (m_s.dma[ma_sl].chan[channel].current_count == 0);
		maxlen = 1 << ma_sl;
	}

	if(m_s.dma[ma_sl].chan[channel].mode.transfer_type == 1) { // write
		// DMA controlled xfer of bytes from I/O to Memory

		if(!ma_sl) {
			if(m_h[channel].dmaWrite8) {
				len = m_h[channel].dmaWrite8(buffer, maxlen);
			} else {
				PERRF(LOG_DMA, "no dmaWrite handler for channel %u\n", channel);
			}
			g_memory.DMA_write(phy_addr, len, buffer);

		} else {
			if(m_h[channel].dmaWrite16) {
				len = m_h[channel].dmaWrite16((uint16_t*)buffer, maxlen / 2);
			} else {
				PERRF(LOG_DMA, "no dmaWrite handler for channel %u\n", channel);
			}
			g_memory.DMA_write(phy_addr, len, buffer);

		}
	} else if(m_s.dma[ma_sl].chan[channel].mode.transfer_type == 2) { // read
		// DMA controlled xfer of bytes from Memory to I/O

		if(!ma_sl) {
			g_memory.DMA_read(phy_addr, maxlen, buffer);

			if(m_h[channel].dmaRead8) {
				len = m_h[channel].dmaRead8(buffer, maxlen);
			}
		} else {
			g_memory.DMA_read(phy_addr, maxlen, buffer);

			if(m_h[channel].dmaRead16){
				len = m_h[channel].dmaRead16((uint16_t*)buffer, maxlen / 2);
			}
		}
	} else if(m_s.dma[ma_sl].chan[channel].mode.transfer_type == 0) {
		// verify

		if(!ma_sl) {
			if(m_h[channel].dmaWrite8) {
				len = m_h[channel].dmaWrite8(buffer, 1);
			} else {
				PERRF(LOG_DMA, "no dmaWrite handler for channel %u\n", channel);
			}
		} else {
			if(m_h[channel].dmaWrite16) {
				len = m_h[channel].dmaWrite16((uint16_t*)buffer, 1);
			} else {
				PERRF(LOG_DMA, "no dmaWrite handler for channel %u.", channel);
			}
		}
	} else {
		PERRF(LOG_DMA, "hlda: transfer_type 3 is undefined\n");
	}

	m_s.dma[ma_sl].DACK[channel] = 1;
	// check for expiration of count, so we can signal TC and DACK(n)
	// at the same time.
	if(!m_s.dma[ma_sl].chan[channel].mode.address_decrement) {
		m_s.dma[ma_sl].chan[channel].current_address += len;
	} else {
		m_s.dma[ma_sl].chan[channel].current_address--;
	}
	m_s.dma[ma_sl].chan[channel].current_count -= len;
	if(m_s.dma[ma_sl].chan[channel].current_count == 0xffff) {
		// count expired, done with transfer
		// assert TC, deassert HRQ & DACK(n) lines
		m_s.dma[ma_sl].status_reg |= (1 << channel); // hold TC in status reg
		if(m_s.dma[ma_sl].chan[channel].mode.autoinit_enable == 0) {
			// set mask bit if not in autoinit mode
			m_s.dma[ma_sl].mask[channel] = 1;
		} else {
			// count expired, but in autoinit mode
			// reload count and base address
			m_s.dma[ma_sl].chan[channel].current_address = m_s.dma[ma_sl].chan[channel].base_address;
			m_s.dma[ma_sl].chan[channel].current_count = m_s.dma[ma_sl].chan[channel].base_count;
		}
		m_s.TC = false;            // clear TC, adapter card already notified
		m_s.HLDA = false;
		g_cpu.set_HRQ(false);           // clear HRQ to CPU
		m_s.dma[ma_sl].DACK[channel] = 0; // clear DACK to adapter card
		if(!ma_sl) {
			set_DRQ(4, 0); // clear DRQ to cascade
			m_s.dma[1].DACK[0] = 0; // clear DACK to cascade
		}
	}
}

unsigned DMA::register_8bit_channel(unsigned channel,
		dma8_fun_t dmaRead, dma8_fun_t dmaWrite, const char *name)
{
	if(channel > 3) {
		PERRF(LOG_DMA, "register_8bit_channel: invalid channel number(%u)\n", channel);
		return 0; // Fail
	}
	if(m_chused[0][channel]) {
		PERRF(LOG_DMA, "register_8bit_channel: channel(%u) already in use\n", channel);
		return 0; // Fail
	}
	PDEBUGF(LOG_V1, LOG_DMA, "channel %u used by '%s'\n", channel, name);
	m_h[channel].dmaRead8  = dmaRead;
	m_h[channel].dmaWrite8 = dmaWrite;
	m_chused[0][channel] = true;
	return 1; // OK
}

unsigned DMA::register_16bit_channel(unsigned channel,
		dma16_fun_t dmaRead, dma16_fun_t dmaWrite, const char *name)
{
	if((channel < 4) || (channel > 7)) {
		PERRF(LOG_DMA, "register_16bit_channel: invalid channel number(%u)\n", channel);
		return 0; // Fail
	}
	if(m_chused[1][channel & 0x03]) {
		PERRF(LOG_DMA, "register_16bit_channel: channel(%u) already in use\n", channel);
		return 0; // Fail
	}
	PDEBUGF(LOG_V1, LOG_DMA, "channel %u used by %s", channel, name);
	channel &= 0x03;
	m_h[channel].dmaRead16  = dmaRead;
	m_h[channel].dmaWrite16 = dmaWrite;
	m_chused[1][channel] = true;
	return 1; // OK
}

unsigned DMA::unregister_channel(unsigned channel)
{
	uint ma_sl = (channel > 3)?1:0;
	m_chused[ma_sl][channel & 0x03] = false;
	PDEBUGF(LOG_V1, LOG_DMA, "channel %u no longer used\n", channel);
	return 1;
}
