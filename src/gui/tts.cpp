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
#include "program.h"
#include "utils.h"
#include "gui.h"
#include "tts.h"
#include "tts_dev_sapi.h"
#include "tts_dev_nvda.h"
#include "tts_dev_espeak.h"
#include "tts_dev_file.h"


void TTS::init(GUI *_gui)
{
	m_gui = _gui;

	static std::map<std::string, unsigned> modes = {
		{ "synth", SYNTH },
		{ "espeak", ESPEAK },
		{ "sapi", SAPI },
		{ "nvda", NVDA },
		{ "file", FILE  }
	};

	unsigned mode = g_program.config().get_enum(TTS_SECTION, TTS_DEV, modes, SYNTH);

	m_channels[ec_to_i(TTSChannel::ID::GUI)].enabled = false;
	m_channels[ec_to_i(TTSChannel::ID::Guest)].enabled = false;

	std::shared_ptr<TTSDev> dev { create_device((Mode)mode) };
	if(!dev) {
		PINFOF(LOG_V0, LOG_GUI, "TTS disabled.\n");
		return;
	}

	// multiple devices/synths are possible, but are currently not allowed as they would speak on top of each other
	m_devices.push_back({dev, {}});

	m_channels[ec_to_i(TTSChannel::ID::GUI)] = ChannelState(TTSChannel::ID::GUI, "GUI", dev);
	m_channels[ec_to_i(TTSChannel::ID::GUI)].enabled = g_program.config().get_bool_or_default(TTS_SECTION, TTS_GUI_ENABLED);
	m_devices.back().channels.push_back(&m_channels[ec_to_i(TTSChannel::ID::GUI)]);

	m_channels[ec_to_i(TTSChannel::ID::Guest)] = ChannelState(TTSChannel::ID::Guest, "Guest", dev);
	m_channels[ec_to_i(TTSChannel::ID::Guest)].enabled = g_program.config().get_bool_or_default(TTS_SECTION, TTS_GUEST_ENABLED);
	m_devices.back().channels.push_back(&m_channels[ec_to_i(TTSChannel::ID::Guest)]);
}

TTSDev * TTS::create_device(Mode _mode) const
{
	std::vector<std::string> params;
	std::unique_ptr<TTSDev> device;

	auto codepage = g_program.config().get_string_or_default(TTS_SECTION, TTS_CODEPAGE);
	if(!str_convert_is_valid(codepage.c_str(), "UTF-8")) {
		PWARNF(LOG_V0, LOG_GUI, "TTS: code page '%s' is not valid or is not supported.\n", codepage.c_str());
		codepage = "437";
	}

	auto voice = g_program.config().get_string(TTS_SECTION, TTS_VOICE, "");

	switch(_mode) {
		case SYNTH: {
			#ifdef _WIN32
				device = std::make_unique<TTSDev_SAPI>();
			#elif HAVE_LIBESPEAKNG
				device = std::make_unique<TTSDev_eSpeak>();
			#else
				PERRF(LOG_GUI, "TTS synthetizers are not supported on this platform!\n");
				return nullptr;
			#endif
			params = {voice, codepage};
			break;
		}
		case ESPEAK: {
			#if HAVE_LIBESPEAKNG
				device = std::make_unique<TTSDev_eSpeak>();
				params = {voice, codepage};
				break;
			#else
				PERRF(LOG_GUI, "TTS eSpeak synthetizer is not supported.\n");
				return nullptr;
			#endif
		}
		case SAPI: {
			#if _WIN32
				device = std::make_unique<TTSDev_SAPI>();
				params = {voice, codepage};
				break;
			#else
				PERRF(LOG_GUI, "TTS SAPI synthetizer is not supported.\n");
				return nullptr;
			#endif
		}
		case NVDA: {
			#if HAVE_NVDA
				device = std::make_unique<TTSDev_NVDA>();
				params = {codepage};
				if(!dynamic_cast<TTSDev_NVDA*>(device.get())->is_nvda_running()) {
					PERRF(LOG_GUI, "NVDA is not running or cannot be found. Using SAPI instead.\n");
					device = std::make_unique<TTSDev_SAPI>();
					params = {voice, codepage};
				}
				break;
			#else
				PERRF(LOG_GUI, "TTS NVDA Controller is not supported.\n");
				return nullptr;
			#endif
		}
		case FILE: {
			device = std::make_unique<TTSDev_File>();
			auto file = g_program.config().get_string(TTS_SECTION, TTS_FILE, "");
			auto format = g_program.config().get_string(TTS_SECTION, TTS_FORMAT, "");
			params = {file, format, codepage};
			break;
		}
		default:
			break;
	}

	try {
		device->open(params);
	} catch(std::runtime_error &e) {
		PERRF(LOG_GUI, "TTS %s: %s.\n", device->name(), e.what());
		return nullptr;
	}

	if(g_program.config().is_value_set(TTS_SECTION, TTS_VOLUME)) {
		try {
			int volume = g_program.config().get_int(TTS_SECTION, TTS_VOLUME);
			device->set_volume(volume);
		} catch(std::exception &e) {
			PERRF(LOG_GUI, "TTS %s: cannot set the requested volume.\n", device->name());
		}
	}
	/*
	if(g_program.config().is_value_set(TTS_SECTION, TTS_RATE)) {
		try {
			int rate = g_program.config().get_int(TTS_SECTION, TTS_RATE);
			device->set_rate(rate);
		} catch(std::exception &e) {
			PERRF(LOG_GUI, "TTS %s: cannot set the requested rate.\n", device->name());
		}
	}
	*/

	return device.release();
}

std::string TTS::Message::format_words(const TTSFormat *_format, const std::string &_words)
{
	assert(_format);

	auto str = _format->fmt_volume(volume, _words);
	str = _format->fmt_rate(rate, str);
	str = _format->fmt_pitch(pitch, str);

	return str;
}

std::string TTS::Message::format(const TTSFormat *_tts_format)
{
	std::string utf8str;
	if(text_format & NOT_UTF8) {
		utf8str = _tts_format->convert(text);
	} else {
		utf8str = text;
	}

	PDEBUGF(LOG_V0, LOG_GUI, "TTS:   [%d] \"%s%s\"\n",
			ec_to_i(priority), str_replace_all_const(utf8str.substr(0, 50), "\n", "\\n").c_str(),
			utf8str.length()>50 ? "..." : "");

	if(text_format & BREAK_LINES) {
		auto lines = str_parse_tokens(utf8str, "\n");
		std::vector<std::string> sentences;
		for(auto &line : lines) {
			line = str_trim(line);
			if(line.empty()) {
				continue;
			}
			std::string value;
			if(!(text_format & IS_MARKUP)) {
				value = _tts_format->fmt_value(line);
			} else {
				value = line;
			}
			value = format_words(_tts_format, value);
			value = _tts_format->fmt_sentence(value);

			sentences.push_back(value);
		}
		return str_implode(sentences, "\n");
	} else {
		std::string value;
		if(!(text_format & IS_MARKUP)) {
			value = _tts_format->fmt_value(utf8str);
		} else {
			value = utf8str;
		}
		value = format_words(_tts_format, value);
		if(text_format & IS_SENTENCE) {
			value = _tts_format->fmt_sentence( value );
		}
		return value;
	}
}

void TTS::speak()
{
	if(!is_open()) {
		return;
	}

	std::lock_guard<std::mutex> lock(m_mutex);

	for(int ch=0; ch<TTSChannel::Count; ch++) {
		if(m_channels[ch].queue.empty()) {
			continue;
		}
		std::vector<std::string> sentences;
		bool purge = false;
		PDEBUGF(LOG_V0, LOG_GUI, "TTS: channel: %s\n", m_channels[ch].name);
		for(auto &mex : m_channels[ch].queue) {
			purge = purge || mex.purge;
			auto fmt = get_format(m_channels[ch].id);
			auto sentence = mex.format(fmt);
			sentences.push_back(sentence);
		}
		m_channels[ch].text_buf = str_implode(sentences, "\n");
		m_channels[ch].purge = purge;
		m_channels[ch].queue.clear();
	}

	for(auto &dev : m_devices) {
		ChannelState *ch_speaking = nullptr;
		for(auto ch : dev.channels) {
			if(!ch->text_buf.empty() && (!ch_speaking || ch->id < ch_speaking->id)) {
				ch_speaking = ch;
			}
		}
		if(ch_speaking) {
			PDEBUGF(LOG_V0, LOG_GUI, "TTS: %s: speak: \"%s%s\"\n",
					dev.device->name(),
					str_replace_all_const(ch_speaking->text_buf.substr(0, 50), "\n", "\\n").c_str(),
					ch_speaking->text_buf.length()>80 ? "..." : "");
			try {
				dev.device->speak(ch_speaking->text_buf, ch_speaking->purge);
				dev.speaking_ch = ch_speaking->id;
			} catch(std::runtime_error &e) {
				PERRF(LOG_GUI, "TTS %s: %s.\n", dev.device->name(), e.what());
			}
		}
	}

	for(int ch=0; ch<TTSChannel::Count; ch++) {
		m_channels[ch].text_buf.clear();
	}
}

void TTS::enqueue(const std::string &_text, Priority _pri, unsigned _fmt, bool _purge, TTSChannel::ID _ch)
{
	if(!m_channels[ec_to_i(_ch)].device) {
		return;
	}

	std::lock_guard<std::mutex> lock(m_mutex);

	if(!m_channels[ec_to_i(_ch)].enabled) {
		return;
	}

	auto &channel = m_channels[ec_to_i(_ch)];

	PDEBUGF(LOG_V0, LOG_GUI, "TTS %s: enqueue, pri:%d, fmt:0x%x: \"%s%s\"\n",
			channel.name,
			ec_to_i(_pri), _fmt,
			str_replace_all_const(_text.substr(0, 50), "\n", "\\n").c_str(),
			_text.length()>50 ? "..." : "");

	for(auto it=channel.queue.begin(); it!=channel.queue.end();) {
		if(_pri > it->priority && it->priority < Priority::High) {
			it = channel.queue.erase(it);
		} else {
			it++;
		}
	}

	if(_pri == Priority::Top) {
		channel.queue.emplace_front(_text, _fmt, _pri, _purge, channel.volume, channel.pitch, channel.rate);
	} else {
		channel.queue.emplace_back(_text, _fmt, _pri, _purge, channel.volume, channel.pitch, channel.rate);
	}
}

int TTS::volume(TTSChannel::ID _ch) const
{
	if(!m_channels[ec_to_i(_ch)].device) {
		return 0;
	}
	return m_channels[ec_to_i(_ch)].volume;
}

int TTS::rate(TTSChannel::ID _ch) const
{
	if(!m_channels[ec_to_i(_ch)].device) {
		return 0;
	}
	return m_channels[ec_to_i(_ch)].rate;
}

int TTS::pitch(TTSChannel::ID _ch) const
{
	if(!m_channels[ec_to_i(_ch)].device) {
		return 0;
	}
	return m_channels[ec_to_i(_ch)].pitch;
}

bool TTS::adj_volume(TTSChannel::ID _ch, int _vol_adj_offset)
{
	// _vol_adj_offset is an offset relative to the current volume value, which is
	// an adjustment in the -10 .. 10 range relative to the system's volume 
	if(!m_channels[ec_to_i(_ch)].device) {
		return false;
	}
	int cur_vol = m_channels[ec_to_i(_ch)].volume;
	int new_vol = get_format(_ch)->get_volume(cur_vol + _vol_adj_offset);
	m_channels[ec_to_i(_ch)].volume = new_vol;
	return (new_vol != cur_vol);
}

bool TTS::adj_rate(TTSChannel::ID _ch, int _rate_adj_offset)
{
	// see comment for adj_volume
	if(!m_channels[ec_to_i(_ch)].device) {
		return false;
	}
	int cur_rate = m_channels[ec_to_i(_ch)].rate;
	int new_rate = get_format(_ch)->get_rate(cur_rate + _rate_adj_offset);
	m_channels[ec_to_i(_ch)].rate = new_rate;
	return (new_rate != cur_rate);
}

bool TTS::adj_pitch(TTSChannel::ID _ch, int _pitch_adj_offset)
{
	// see comment for adj_volume
	if(!m_channels[ec_to_i(_ch)].device) {
		return false;
	}
	int cur_pitch = m_channels[ec_to_i(_ch)].volume;
	int new_pitch = get_format(_ch)->get_pitch(cur_pitch + _pitch_adj_offset);
	m_channels[ec_to_i(_ch)].volume = new_pitch;
	return (new_pitch != cur_pitch);
}

bool TTS::set_volume(TTSChannel::ID _ch, int _vol)
{
	// _vol is an absolute value in the -10 .. +10 range
	_vol = get_format(_ch)->get_volume(_vol);
	if(!m_channels[ec_to_i(_ch)].device || m_channels[ec_to_i(_ch)].volume == _vol) {
		return false;
	}
	m_channels[ec_to_i(_ch)].volume = _vol;
	return true;
}

bool TTS::set_rate(TTSChannel::ID _ch, int _rate)
{
	_rate = get_format(_ch)->get_volume(_rate);
	if(!m_channels[ec_to_i(_ch)].device || m_channels[ec_to_i(_ch)].rate == _rate) {
		return false;
	}
	m_channels[ec_to_i(_ch)].rate = _rate;
	return true;
}

bool TTS::set_pitch(TTSChannel::ID _ch, int _pitch)
{
	_pitch = get_format(_ch)->get_volume(_pitch);
	if(!m_channels[ec_to_i(_ch)].device || m_channels[ec_to_i(_ch)].pitch == _pitch) {
		return false;
	}
	m_channels[ec_to_i(_ch)].pitch = _pitch;
	return true;
}

void TTS::adj_volume(int _vol_adj_offset)
{
	for(auto &dev : m_devices) {
		int cur_vol = dev.device->volume();
		dev.device->set_volume(cur_vol + _vol_adj_offset);
	}
}

void TTS::adj_rate(int _rate_adj_offset)
{
	for(auto &dev : m_devices) {
		int cur_rate = dev.device->rate();
		dev.device->set_rate(cur_rate + _rate_adj_offset);
	}
}

void TTS::adj_pitch(int _pitch_adj_offset)
{
	for(auto &dev : m_devices) {
		int cur_pitch = dev.device->pitch();
		dev.device->set_pitch(cur_pitch + _pitch_adj_offset);
	}
}

void TTS::stop()
{
	for(int ch=0; ch<TTSChannel::Count; ch++) {
		if(m_channels[ch].device && m_channels[ch].device->is_open()) {
			m_channels[ch].device->stop();
		}
	}
}

void TTS::stop(TTSChannel::ID _ch)
{
	for(auto &dev : m_devices) {
		if(dev.speaking_ch == _ch) {
			dev.device->stop();
		}
	}
}

void TTS::close()
{
	for(int ch=0; ch<TTSChannel::Count; ch++) {
		m_channels[ch].device.reset();
	}
	for(auto &dev : m_devices) {
		dev.device->close();
		dev.device.reset();
	}
}

const TTSFormat * TTS::get_format(TTSChannel::ID _ch) const
{
	if(!m_channels[ec_to_i(_ch)].device || !m_channels[ec_to_i(_ch)].device->format(ec_to_i(_ch))) {
		return &m_default_fmt;
	}
	return m_channels[ec_to_i(_ch)].device->format(ec_to_i(_ch));
}

bool TTS::is_channel_open(TTSChannel::ID _ch) const
{
	return m_channels[ec_to_i(_ch)].device && m_channels[ec_to_i(_ch)].device->is_open();
}

bool TTS::is_channel_enabled(TTSChannel::ID _ch) const
{
	return m_channels[ec_to_i(_ch)].enabled;
}

bool TTS::enable_channel(TTSChannel::ID _ch, bool _enabled)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if(m_channels[ec_to_i(_ch)].device) {
		m_channels[ec_to_i(_ch)].enabled = _enabled;
		return _enabled;
	}
	return false;
}

