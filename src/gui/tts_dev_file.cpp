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

#include "ibmulator.h"
#include "utils.h"
#include "tts_dev.h"
#include "tts_dev_file.h"
#include "tts_format_ssml.h"
#include "tts_format_msxml.h"
#include "filesys.h"
#include "program.h"

void TTSDev_File::open(const std::vector<std::string> &_params)
{
	PINFOF(LOG_V0, LOG_GUI, "TTS: Initializing the File device.\n");

	if(_params.size() < 3) {
		throw std::logic_error("invalid number of parameters");
	}

	if(_params[0].empty()) {
		throw std::runtime_error("output file path not specified");
	}

	auto path = g_program.config().get_file_path(_params[0], FILE_TYPE_USER);

	m_file = FileSys::make_ofstream(path.c_str(), std::ofstream::binary);
	if(!m_file.is_open()) {
		throw std::runtime_error("cannot open file for writing");
	}

	if(_params[1] == "ssml") {
		m_format = std::make_unique<TTSFormat_SSML>(_params[2]);
	} else if(_params[1] == "msxml") {
		m_format = std::make_unique<TTSFormat_MSXML>(_params[2]);
	} else {
		m_format = std::make_unique<TTSFormat>(_params[2]);
	}
}

void TTSDev_File::speak(const std::string &_text, bool)
{
	if(!is_open()) {
		throw std::runtime_error("the device is not open");
	}

	m_file << _text << "\n" << std::flush;
}

void TTSDev_File::close()
{
	if(is_open()) {
		m_file.close();
	}
}
