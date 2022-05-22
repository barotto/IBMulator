/*
 * Copyright (C) 2021-2022  Marco Bortolin
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

#ifndef IBMULATOR_STATERECORD_H
#define IBMULATOR_STATERECORD_H

#include "statebuf.h"
#include "appconfig.h"
#include "filesys.h"
#include <SDL.h>

#define STATE_RECORD_VERSION IBMULATOR_STATE_VERSION
#define STATE_RECORD_BASE    "savestate_"
#define STATE_FILE_BASE      "state"
#define QUICKSAVE_RECORD     STATE_RECORD_BASE "quick"
#define QUICKSAVE_DESC       "QUICKSAVE"

class StateRecord
{
public:
	struct Info {
		std::string name;
		std::string user_desc;
		std::string config_desc;
		time_t mtime;
		unsigned version;
	};
private:
	std::string m_path;
	std::string m_basefile;

	std::string m_info_path;
	std::string m_ini_path;
	std::string m_state_path;
	std::string m_screen_path;

	Info m_info;

	AppConfig m_config;
	StateBuf m_state;
	SDL_Surface *m_framebuffer = nullptr;

public:
	StateRecord(std::string _basepath, std::string _name, bool _initialize=true);
	~StateRecord();

	StateRecord & operator = (const StateRecord &_other) {
		if(this != &_other) {
			m_path = _other.m_path;
			m_basefile = _other.m_basefile;
			m_info_path = _other.m_info_path;
			m_ini_path = _other.m_ini_path;
			m_state_path = _other.m_state_path;
			m_screen_path = _other.m_screen_path;
			m_info = _other.m_info;
		}
		return *this;
	}
	Info & info() { return m_info; }
	const Info & info() const { return m_info; }
	StateBuf & state() { return m_state; }
	AppConfig & config() { return m_config; }
	std::string & screen() { return m_screen_path; }
	const std::string & screen() const { return m_screen_path; }

	const std::string & name() const { return m_info.name; }
	const std::string & user_desc() const { return m_info.user_desc; }
	const std::string & config_desc() const { return m_info.config_desc; }
	time_t mtime() const { return m_info.mtime; }

	void set_framebuffer(SDL_Surface *_fb);

	const char * path() const { return m_path.c_str(); }

	void save();
	void load();
	void remove();
};

#endif
