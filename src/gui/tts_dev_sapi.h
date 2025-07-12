/*
 * Copyright (C) 2025  Marco Bortolin
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

#ifndef IBMULATOR_TTS_DEV_SAPI_H
#define IBMULATOR_TTS_DEV_SAPI_H

#ifdef _WIN32

#include "tts_dev.h"

class ISpVoice;

class TTSDev_SAPI : public TTSDev
{
	ISpVoice *m_voice = nullptr;
	int m_default_vol = 0;

public:
	TTSDev_SAPI() : TTSDev(Type::SYNTH, "SAPI") {};
	~TTSDev_SAPI() {}

	void open(const std::vector<std::string> &_conf);
	bool is_open() const { return static_cast<bool>(m_voice); }
	void speak(const std::string &_text, bool _purge = true);
	bool is_speaking() const;
	void stop();
	bool set_volume(int);
	bool set_rate(int);
	bool set_pitch(int) { return false; }
	void close();

private:
	void set_voice(int _num);
	void set_voice(const std::string &_name);
	void check_open() const;
	void display_voices() noexcept;
	int cur_rate() const;
	int cur_vol() const;
};

#endif

#endif