/*
 * Copyright (C) 2002-2009  The Bochs Project
 * Copyright (C) 2015-2022  Marco Bortolin
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

#ifndef IBMULATOR_HW_DMA_H
#define IBMULATOR_HW_DMA_H

#include "hardware/iodevice.h"

/* maximum size of the ISA DMA buffer */
#define DMA_BUFFER_SIZE 512

typedef std::function<uint16_t(uint8_t *data, uint16_t maxlen, bool tc)> dma8_fun_t;
typedef std::function<uint16_t(uint16_t *data, uint16_t maxlen, bool tc)> dma16_fun_t;
typedef std::function<void(bool state)> dmaTC_fun_t;

class DMA : public IODevice
{
	IODEVICE(DMA, "DMA")

private:
	struct {
		struct {
			bool DRQ[4];  // DMA Request
			bool DACK[4]; // DMA Acknowlege

			bool mask[4];
			bool flip_flop;
			uint8_t   status_reg;
			uint8_t   command_reg;
			bool ctrl_disabled;
			struct {
				struct {
					uint8_t mode_type;
					bool address_decrement;
					bool autoinit_enable;
					uint8_t transfer_type;
				} mode;
				uint16_t  base_address;
				uint16_t  current_address;
				uint16_t  base_count;
				uint16_t  current_count;
				uint8_t   page_reg;
			} chan[4]; /* DMA channels 0..3 */
		} dma[2];  // DMA-1 / DMA-2

		bool HLDA;    // Hold Acknowlege
		uint8_t ext_page_reg[16]; // Extra page registers (unused)

	} m_s; // state information

	struct {
		bool used;
		std::string device;
	} m_channels[8];

	struct {
		dma8_fun_t dmaRead8;    // 8-bit Memory to I/O handler
		dma8_fun_t dmaWrite8;   // 8-bit I/O to Memory handler
		dma16_fun_t dmaRead16;  // 16-bit Memory to I/O handler
		dma16_fun_t dmaWrite16; // 16-bit I/O to Memory handler
		dmaTC_fun_t tc_cb;      // Terminal Count line callback
	} m_h[4];

	void control_HRQ(uint8_t ma_sl);
	void reset_controller(unsigned num);

public:
	DMA(Devices* _dev);
	~DMA();

	void install();
	void reset(unsigned type);
	void config_changed();
	uint16_t read (uint16_t address, unsigned io_len);
	void write(uint16_t address, uint16_t   value, unsigned io_len);

	void raise_HLDA();
	void set_DRQ(unsigned channel, bool val);
	bool get_DRQ(uint channel);

	void register_8bit_channel(unsigned channel,
			dma8_fun_t dmaRead, dma8_fun_t dmaWrite, dmaTC_fun_t tc, const char *name);
	void register_16bit_channel(unsigned channel,
			dma16_fun_t dmaRead, dma16_fun_t dmaWrite, dmaTC_fun_t tc, const char *name);
	void unregister_channel(unsigned channel);
	std::string get_device_name(unsigned _channel);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
