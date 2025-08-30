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

#if HAVE_LIBESPEAKNG

#include "utils.h"
#include "tts_dev.h"
#include "tts_dev_espeak.h"
#include "tts_format_ssml.h"
#include <cstring>
#include <cmath>

void TTSDev_eSpeak::open(const std::vector<std::string> &_params)
{
	PINFOF(LOG_V0, LOG_GUI, "TTS: Initializing eSpeak NG.\n");

	if(espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, 0, NULL, 0) == EE_INTERNAL_ERROR) {
		throw std::runtime_error("cannot initialize the library");
	}

	espeak_ERROR result = EE_OK;

	if(_params.size() < 2) {
		throw std::logic_error("invalid number of parameters");
	}

	if(_params[0].empty() || _params[0] == "default" || _params[0] == "auto") {
		PINFOF(LOG_V0, LOG_GUI, "%s: Using the default voice.\n", name());
		result = use_default_voice();
	} else {
		result = espeak_SetVoiceByName(_params[0].c_str());
	}

	if(result != EE_OK) {
		if(result == EE_NOT_FOUND) {
			PERRF(LOG_GUI, "%s: The specified voice was not found.\n", name());
		}
		throw std::runtime_error(str_format("cannot set the voice (error: %d)", result));
	}

	auto cur_voice = espeak_GetCurrentVoice();

	PINFOF(LOG_V0, LOG_GUI, "%s: Current voice: \"%s\"\n", name(), cur_voice->name);

	m_format[ec_to_i(TTSChannel::ID::GUI)] = std::make_unique<TTSFormat_SSML>(_params[1]);

	m_initialized = true;
}

void TTSDev_eSpeak::speak(const std::string &_text, bool _purge)
{
	check_open();

	if(_purge) {
		stop();
	}

	std::string text = _text; // "<p>" + _text + "</p>";
	PDEBUGF(LOG_V1, LOG_GUI, "%s%s:\n%s\n", name(), _purge ? " (purge)" : "", text.c_str());
	espeak_Synth(text.c_str(), text.size()+1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8|espeakSSML, NULL, NULL);
}

bool TTSDev_eSpeak::is_speaking() const
{
	return espeak_IsPlaying();
}

void TTSDev_eSpeak::stop()
{
	check_open();

	if(is_speaking()) {
		espeak_Cancel();
	}
}

bool TTSDev_eSpeak::set_volume(int _volume)
{
	_volume = std::clamp(_volume, -10, 10);
	if(_volume == m_volume) {
		return false;
	}

	stop();

	int default_vol = espeak_GetParameter(espeakVOLUME, 0);
	const double vol_step = 200.0 / 20.0;
	int new_vol = default_vol + round(double(_volume) * vol_step);
	m_volume = _volume;

	espeak_SetParameter(espeakVOLUME, new_vol, 0);

	PDEBUGF(LOG_V1, LOG_GUI, "%s: def.vol.=%d, vol.adj.=%d, new.vol.=%d\n", name(),
			default_vol, _volume, new_vol);

	return true;
}

bool TTSDev_eSpeak::set_rate(int _rate)
{
	_rate = std::clamp(_rate, -10, 10);
	if(_rate == m_rate) {
		return false;
	}

	stop();

	int default_rate = espeak_GetParameter(espeakRATE, 0);
	const double rate_step = double(espeakRATE_MAXIMUM - espeakRATE_MINIMUM) / 20.0;
	int new_rate = default_rate + round(double(_rate) * rate_step);
	m_rate = _rate;

	espeak_SetParameter(espeakRATE, new_rate, 0);

	PDEBUGF(LOG_V1, LOG_GUI, "%s: def.rate=%d, rate adj.=%d, new.rate=%d\n", name(),
			default_rate, _rate, new_rate);

	return true;
}

bool TTSDev_eSpeak::set_pitch(int _pitch)
{
	_pitch = std::clamp(_pitch, -10, 10);
	if(_pitch == m_pitch) {
		return false;
	}

	stop();

	int default_pitch = espeak_GetParameter(espeakPITCH, 0);
	const double pitch_step = 100.0 / 20.0;
	int new_pitch = default_pitch + round(double(_pitch) * pitch_step);
	m_rate = _pitch;

	espeak_SetParameter(espeakRATE, new_pitch, 0);

	PDEBUGF(LOG_V1, LOG_GUI, "%s: def.pitch=%d, pitch adj.=%d, new.pitch=%d\n", name(),
			default_pitch, _pitch, new_pitch);

	return true;
}

void TTSDev_eSpeak::close()
{
	if(is_open()) {
		espeak_Terminate();
	}
}

espeak_ERROR TTSDev_eSpeak::use_default_voice()
{
	espeak_VOICE voice;
	memset(&voice, 0, sizeof(espeak_VOICE));
	voice.languages = "en";
	return espeak_SetVoiceByProperties(&voice);
}

void TTSDev_eSpeak::display_voices(const char *_language, bool _verbose) const
{
	const espeak_VOICE **voices;
	espeak_VOICE voice_select;

	static const char genders[4] = { '-', 'M', 'F', '-' };

	if((_language != NULL) && (_language[0] != 0)) {
		// display only voices for the specified language, in order of priority
		voice_select.languages = _language;
		voice_select.age = 0;
		voice_select.gender = 0;
		voice_select.name = NULL;
		voices = espeak_ListVoices(&voice_select);
	} else {
		voices = espeak_ListVoices(NULL);
	}

	PINFOF(LOG_V0, LOG_GUI, "%s: List of available voices:\n", name());
	if(_verbose) {
		PINFOF(LOG_V0, LOG_GUI, "Pty Language        Age/Gender Name                             File                 Other Languages\n");
	} else {
		PINFOF(LOG_V0, LOG_GUI, "Gen.  Name\n");
	}
	const espeak_VOICE *v;
	for(int ix = 0; (v = voices[ix]) != NULL; ix++) {
		int count = 0;
		const char *p = v->languages;
		while(*p != 0) {
			const char *lang_name = p+1;
			int len = strlen(lang_name);
			if(_verbose) {
				if(count == 0) {
					PINFOF(LOG_V0, LOG_GUI, "%2d  %-15s %3d/%c      %-32s %-20s ",
							p[0], lang_name, v->age, genders[v->gender], v->name, v->identifier);
				} else {
					PINFOF(LOG_V0, LOG_GUI, "(%s %d)", lang_name, p[0]);
				}
			} else if(count == 0) {
				PINFOF(LOG_V0, LOG_GUI, "%c     %s", genders[v->gender], v->name);
			}
			count++;
			p += len+2;
		}
		PINFOF(LOG_V0, LOG_GUI, "\n");
	}
}

void TTSDev_eSpeak::check_open() const
{
	if(!is_open()) {
		throw std::runtime_error("the device is not open");
	}
}

#endif
