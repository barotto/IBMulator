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

#ifndef IBMULATOR_TTS_DEV_NVDA_H
#define IBMULATOR_TTS_DEV_NVDA_H

#if HAVE_NVDA

#include "tts_dev.h"

class TTSDev_NVDA : public TTSDev
{
	std::unique_ptr<TTSFormat> m_format_guest;

public:
	TTSDev_NVDA() : TTSDev(Type::SYNTH, "NVDA") {}
	~TTSDev_NVDA() {}

	void open(const std::vector<std::string> &) override;
	bool is_open() const override;
	void speak(const std::string &_text, bool _purge = true) override;
	void stop() override;
	const TTSFormat * format(int _ch) const override;
	bool is_nvda_running() const;

private:
	void check_open() const;
};

#endif

#endif