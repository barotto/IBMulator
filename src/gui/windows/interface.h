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

#ifndef IBMULATOR_GUI_INTERFACE_H
#define IBMULATOR_GUI_INTERFACE_H

#include "fileselect.h"
#include <Rocket/Core/EventListener.h>

class Machine;
class GUI;

class Interface : public Window
{
protected:

	struct {
		RC::Element *power;
		RC::Element *fdd_select;
	} m_buttons;

	struct {
		RC::Element *fdd_led, *hdd_led;
		RC::Element *fdd_disk;
	} m_status;

	struct {
		bool power, fdd, hdd;
	} m_leds;

	RC::Element * m_warning;
	RC::Element * m_message;

	bool m_drive_b;
	uint m_curr_drive;
	bool m_floppy_present;
	bool m_floppy_changed;

	Machine *m_machine;
	FileSelect *m_fs;

	void update_floppy_disk(std::string _filename);

public:

	Interface(Machine *_machine, GUI * _gui, const char *_rml);
	~Interface();

	virtual void update();
	virtual void update_size(uint _width, uint _height);

	void show_warning(bool _show);
	void show_message(const char* _mex);

	void on_power(RC::Event &);
	void on_fdd_select(RC::Event &);
	void on_fdd_eject(RC::Event &);
	void on_fdd_mount(RC::Event &);
	void on_floppy_mount(std::string _img_path, bool _write_protect);

	virtual void set_audio_volume(float) {}
	virtual void set_video_brightness(float) {}
	virtual void set_video_contrast(float) {}
};

class LogMessage : public Logdev
{
private:
	Interface *m_iface;
public:
	LogMessage(Interface* _iface);
	~LogMessage();

	void log_put(const char* _prefix, const char* _message);
};

#endif
