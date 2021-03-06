/*
 * Copyright (c) 2002-2009  The Bochs Project
 * Copyright (C) 2015-2021  Marco Bortolin
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

#ifndef IBMULATOR_HW_KEYBOARD_H
#define IBMULATOR_HW_KEYBOARD_H

#include "hardware/iodevice.h"
#include "keys.h"
#include <mutex>


#define KBD_SERIAL_DELAY  250
#define KBD_ELEMENTS         16
#define KBD_CONTROLLER_QSIZE  5


#define MOUSE_MODE_RESET  10
#define MOUSE_MODE_STREAM 11
#define MOUSE_MODE_REMOTE 12
#define MOUSE_MODE_WRAP   13
#define MOUSE_BUFF_SIZE   48

enum {
	KBD_TYPE_XT,
	KBD_TYPE_AT,
	KBD_TYPE_MF
};

#define KBD_TYPE KBD_TYPE_MF


class Keyboard : public IODevice
{
	IODEVICE(Keyboard, "Keyboard Controller");
private:
	struct State {
		struct {
			/* status bits matching the status port*/
			bool pare; // Bit7, 1=parity error from keyboard/mouse - ignored.
			bool tim;  // Bit6, 1=timeout from keyboard - ignored.
			bool auxb; // Bit5, 1=mouse data waiting for CPU to read.
			bool keyl; // Bit4, 1=keyswitch in lock position - ignored.
			bool c_d;  // Bit3, 1=command to port 64h, 0=data to port 60h
			bool sysf; // Bit2, System Flag
			bool inpb; // Bit1, Input Buffer Full
			bool outb; // Bit0, 1=keyboard data or mouse data ready for CPU
					   //       check aux to see which. Or just keyboard
					   //       data before AT style machines

			/* internal to our version of the keyboard controller */
			bool     kbd_clock_enabled;
			bool     aux_clock_enabled;
			bool     allow_irq1;
			bool     allow_irq12;
			uint8_t  kbd_output_buffer;
			uint8_t  aux_output_buffer;
			uint8_t  last_comm;
			uint8_t  expecting_port60h;
			uint8_t  expecting_mouse_parameter;
			uint8_t  last_mouse_command;
			uint32_t timer_pending;
			bool     irq1_requested;
			bool     irq12_requested;
			bool     scancodes_translate;
			bool     expecting_scancodes_set;
			uint8_t  current_scancodes_set;
			bool     bat_in_progress;
			bool     self_test_in_progress;
			bool     self_test_completed;

			uint8_t  Q[KBD_CONTROLLER_QSIZE];
			unsigned Qsize;
			unsigned Qsource; // 0=keyboard, 1=mouse

		} kbd_ctrl;

		struct Mouse {
			uint8_t type;
			uint8_t sample_rate;
			uint8_t resolution_cpmm; // resolution in counts per mm
			uint8_t scaling;
			uint8_t mode;
			uint8_t saved_mode;  // the mode prior to entering wrap mode
			bool    enable;
			uint8_t buttons_state = 0;
			int16_t delayed_dx;
			int16_t delayed_dy;
			int16_t delayed_dz;
			uint8_t im_request;
			bool    im_mode;

			uint8_t get_status_byte();
			uint8_t get_resolution_byte();

		} mouse;

		/*
		 * MT: the internal buffers are used by the Machine and GUI threads
		 */
		/* keyboard internal buffer usage:
		 * T1 GUI::dispatch_hw_event->Machine::send_key_to_kb->gen_scancodes->kbd_enQ
		 * T2 CPU->Devices::write_byte/word->write->kbd_ctrl_to_kbd->kbd_enQ
		 * T2 CPU->Devices::write_byte/word->write->kbd_ctrl_to_kbd->kbd_enQ_imm
		 * T2 CPU->Devices::write_byte/word->write->kbd_ctrl_to_kbd->reset_internals
		 * T2 Machine->kbdtimer->timer_handler->periodic (with mouse buffer)
		 * the init is a one time only procedure started before the machine
		 * is on it's own thread:
		 * T1 Machine::init->Devices::init->init->reset_internals
		 *
		 */
		struct {
			int     num_elements;
			uint8_t buffer[KBD_ELEMENTS];
			int     head;

			bool    expecting_typematic;
			bool    expecting_led_write;
			uint8_t delay;
			uint8_t repeat_rate;
			uint8_t led_status;
			bool    scanning_enabled;
		} kbd_buffer;


		/* mouse internal buffer usage:
		 * T1 GUI->Machine::mouse_motion->mouse_motion->create_mouse_packet->mouse_enQ_packet->mouse_enQ
		 * T2 Machine->kbdtimer->timer_handler->periodic (with kb buffer)
		 */
		struct {
			int     num_elements;
			uint8_t buffer[MOUSE_BUFF_SIZE];
			int     head;
		} mouse_buffer;

		float screen_mmpd; // mm per dot

	} m_s;

	std::mutex m_kbd_lock;
	std::mutex m_mouse_lock;

	uint8_t  get_kbd_enable();
	void     service_paste_buf();
	void     create_mouse_packet(bool force_enq);
	unsigned periodic(uint32_t usec_delta);

	void reset_internals(bool powerup);
	void set_kbd_clock_enable(bool value);
	void set_aux_clock_enable(bool value);
	void kbd_ctrl_to_kbd(uint8_t value);
	void kbd_ctrl_to_mouse(uint8_t value);
	void kbd_enQ(uint8_t scancode);
	void kbd_enQ_imm(uint8_t val);
	void activate_timer(uint32_t _usec_delta=1);
	void controller_enQ(uint8_t data, unsigned source);
	void update_controller_Q();
	bool mouse_enQ_packet(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4);
	void mouse_enQ(uint8_t mouse_data);

	void timer_handler(uint64_t);
	int m_timer;

	bool m_mouse_acc;

public:
	Keyboard(Devices *_dev);
	~Keyboard();

	void install();
	void remove();
	void reset(unsigned _signal);
	void power_off();
	void config_changed();
	uint16_t read(uint16_t _address, unsigned _io_len);
	void write(uint16_t _address, uint16_t _value, unsigned _io_len);

	void gen_scancode(Keys _key, uint32_t _event);
	void mouse_button(MouseButton _button, bool _state);
	void mouse_motion(int delta_x, int delta_y, int delta_z);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
