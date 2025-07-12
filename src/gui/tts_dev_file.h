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

#ifndef IBMULATOR_TTS_DEV_FILE_H
#define IBMULATOR_TTS_DEV_FILE_H

#include "tts_dev.h"
#include <fstream>

class TTSDev_File : public TTSDev
{
	std::ofstream m_file;

public:
	TTSDev_File() : TTSDev(Type::FILE, "FILE") {};
	~TTSDev_File() {}

	void open(const std::vector<std::string> &_conf);
	bool is_open() const { return m_file.is_open(); }
	void speak(const std::string &_text, bool _purge = true);
	void close();
};


#endif