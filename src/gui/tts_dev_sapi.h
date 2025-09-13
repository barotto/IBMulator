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
#include "shared_queue.h"

class ISpVoice;

class TTSDev_SAPI : public TTSDev
{
	std::thread m_thread;
	ISpVoice *m_voice = nullptr;
	int m_default_vol = 0;
	std::atomic<bool> m_is_open = false;
	shared_queue<std::function<void()>> m_cmd_queue;

public:
	TTSDev_SAPI() : TTSDev(Type::SYNTH, "SAPI") {};
	~TTSDev_SAPI() {}

	void open(const std::vector<std::string> &_conf) override;
	bool is_open() const override { return m_is_open; }
	void speak(const std::string &_text, bool _purge = true) override;
	void stop() override;
	bool set_volume(int) override;
	bool set_rate(int) override;
	bool set_pitch(int) override { return false; }
	void close() override;

private:
	void thread_init(const std::vector<std::string> &_params, std::promise<bool> _init_promise);
	bool is_speaking() const;
	void set_voice(int _num);
	void set_voice(const std::string &_name);
	void check_open() const;
	void display_voices() noexcept;
	int cur_rate() const;
	int cur_vol() const;
};

#endif

#endif