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

#if HAVE_NVDA

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <nvdaController.h>

#include "utils.h"
#include "wincompat.h"
#include "tts_dev.h"
#include "tts_dev_nvda.h"
#include "tts_format_ssml.h"


void TTSDev_NVDA::open(const std::vector<std::string> &_params)
{
	PINFOF(LOG_V0, LOG_GUI, "TTS: Initializing NVDA device.\n");

	unsigned long pid = 0;
	nvdaController_getProcessId(&pid);

	if(!is_open() || !pid) {
		throw std::runtime_error("error communicating with NVDA");
	} else {
		PINFOF(LOG_V0, LOG_GUI, "%s: process id: %ul\n", name(), pid);
	}

	m_format[ec_to_i(TTSChannel::ID::GUI)] = std::make_unique<TTSFormat_SSML>(_params[0], true);
	m_format[ec_to_i(TTSChannel::ID::Guest)] = std::make_unique<TTSFormat_SSML>(_params[0], false);
}

bool TTSDev_NVDA::is_open() const
{
	return is_nvda_running();
}

void TTSDev_NVDA::speak(const std::string &_text, bool _purge)
{
	check_open();

	std::string text = "<speak>" + _text + "</speak>";

	PDEBUGF(LOG_V1, LOG_GUI, "%s:\n%s\n", name(), text.c_str());

	std::wstring wtext = utf8::widen(text);

	SPEECH_PRIORITY pri = SPEECH_PRIORITY_NORMAL;
	if(_purge) {
		nvdaController_cancelSpeech();
		pri = SPEECH_PRIORITY_NOW;
	}

	if(nvdaController_speakSsml(wtext.data(), SYMBOL_LEVEL_UNCHANGED, pri, TRUE) != 0) {
		throw std::runtime_error(str_format("cannot speak"));
	}
}

void TTSDev_NVDA::stop()
{
	check_open();

	nvdaController_cancelSpeech();
}

void TTSDev_NVDA::check_open() const
{
	if(!is_open()) {
		throw std::runtime_error("the device is not open");
	}
}

bool TTSDev_NVDA::is_nvda_running() const
{
	return (nvdaController_testIfRunning() == 0);
}


#endif
