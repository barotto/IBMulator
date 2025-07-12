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
/*
 * Braille 'n Speak serial device.
 * Emulation logic based on vbns-ao2 (https://github.com/sukiletxe/vbns-ao2)
 */

#include "ibmulator.h"
#include "serialspeech.h"
#include "gui/gui.h"
#include "utils.h"

const std::map<char, SerialSpeech::Command> SerialSpeech::ms_handlers{
	{ 'V', { "volume", std::mem_fn(&SerialSpeech::cmd_volume) } },
	{ 'E', { "rate", std::mem_fn(&SerialSpeech::cmd_rate) } },
	{ 'P', { "pitch", std::mem_fn(&SerialSpeech::cmd_pitch) } },
	{ 'T', { "tone", std::mem_fn(&SerialSpeech::cmd_tone) } },
};


void SerialSpeech::init()
{
	PINFOF(LOG_V0, LOG_COM, "SPEECH: Braille 'n Speak serial device connected.\n");
}

TTS *SerialSpeech::tts() const
{
	return &GUI::instance()->tts();
}

void SerialSpeech::clear()
{
	m_num.clear();
	m_buffer.clear();
	m_lst.clear();
	m_in_command = false;
}

void SerialSpeech::reset(unsigned)
{
	clear();

	m_rx = -1;

	tts()->set_volume(TTS::ChannelID::Guest, 0);
	tts()->set_rate(TTS::ChannelID::Guest, 0);
	tts()->set_pitch(TTS::ChannelID::Guest, 0);
	tts()->stop(TTS::ChannelID::Guest);
}

void SerialSpeech::power_off()
{
	tts()->stop(TTS::ChannelID::Guest);
}

bool SerialSpeech::serial_read_byte(uint8_t *_byte)
{
	assert(_byte);
	if(m_rx >= 0) {
		*_byte = uint8_t(m_rx);
		PDEBUGF(LOG_V2, LOG_COM, "SPEECH: rx: %d\n", m_rx);
		m_rx = -1;
		return true;
	}
	return false;
}

bool SerialSpeech::serial_write_byte(uint8_t _byte)
{
	PDEBUGF(LOG_V2, LOG_COM, "SPEECH: 0x%02x %s", _byte, str_format_special(_byte).c_str());

	if(_byte == 0x18) {
		clear();
		tts()->stop(TTS::ChannelID::Guest);
	} else if(_byte == 0x05) {
		m_in_command = true;
		PDEBUGF(LOG_V2, LOG_COM, " command");
	} else if(_byte == 0x06) {
		// indexing mark?
		// m_rx = 6; software expects something, don't know what ...
	} else if(m_in_command && is_ascii_digit(_byte)) {
		m_num += char(_byte);
	} else if(m_in_command && is_ascii_letter(_byte)) {
		m_in_command = false;
		if(!m_buffer.empty()) {
			m_lst.push_back(std::make_pair(m_buffer, -1));
			m_buffer.clear();
		}
		auto handler = ms_handlers.find(char(_byte));
		if(handler != ms_handlers.end()) {
			PDEBUGF(LOG_V2, LOG_COM, " %s(%s)", handler->second.name, m_num.c_str());
			try {
				int num = str_parse_int_num(m_num);
				m_lst.push_back(std::make_pair(std::string(1, char(_byte)), num));
			} catch(std::exception &) {
				PDEBUGF(LOG_V2, LOG_COM, " invalid argument");
			}
		} else {
			PDEBUGF(LOG_V2, LOG_COM, " ???");
		}
		m_num = "";
	} else if(m_in_command) {
		m_in_command = false;
		PDEBUGF(LOG_V2, LOG_COM, " command off");
	} else if(!m_in_command && (_byte == '\r' || _byte == '\0')) {
		PDEBUGF(LOG_V2, LOG_COM, "\n");
		if(_byte == 0) {
			m_rx = 0;
		}
		if(!m_buffer.empty()) {
			m_lst.push_back(std::make_pair(m_buffer, -1));
		}
		process();
		clear();
		return true;
	} else {
		m_buffer += char(_byte);
	}

	PDEBUGF(LOG_V2, LOG_COM, "\n");

	return true;
}

void SerialSpeech::process()
{
	PDEBUGF(LOG_V2, LOG_COM, "SPEECH: process...\n");
	std::string sentence;
	for(auto &item : m_lst) {
		if(item.second >= 0) {
			auto &cmd = ms_handlers.at(item.first[0]);
			PDEBUGF(LOG_V2, LOG_COM, "  %s(%d)\n", cmd.name, item.second);
			cmd.handler(*this, item.second);
		} else {
			PDEBUGF(LOG_V2, LOG_COM, "  %s\n", item.first.c_str());
			sentence += item.first;
		}
	}
	if(!sentence.empty()) {
		tts()->enqueue(sentence,
			TTS::Priority::Normal,
			TTS::IS_SENTENCE | TTS::NOT_UTF8,
			false,
			TTS::ChannelID::Guest
		);
	}
}

void SerialSpeech::cmd_volume(int _val)
{
	_val = std::clamp(_val, 1, 15) - 1;
	int volume = int(lerp(-10.0, 10.0, double(_val) / 15.0));
	PDEBUGF(LOG_V2, LOG_COM, "SPEECH:   volume=%d\n", volume);
	tts()->set_volume(TTS::ChannelID::Guest, volume);
}

void SerialSpeech::cmd_rate(int _val)
{
	_val = std::clamp(_val, 1, 15) - 1;
	int rate = int(lerp(-10.0, 10.0, double(_val) / 15.0));
	PDEBUGF(LOG_V2, LOG_COM, "SPEECH:   rate=%d\n", rate);
	tts()->set_rate(TTS::ChannelID::Guest, rate);
}

void SerialSpeech::cmd_pitch(int _val)
{
	_val = std::clamp(_val, 1, 29) - 1;
	int pitch = int(lerp(-10.0, 10.0, double(_val) / 29.0));
	PDEBUGF(LOG_V2, LOG_COM, "SPEECH:   pitch=%d\n", pitch);
	tts()->set_pitch(TTS::ChannelID::Guest, pitch);
}

void SerialSpeech::cmd_tone(int)
{
	PDEBUGF(LOG_V1, LOG_COM, "tone is unsupported.\n");
}
