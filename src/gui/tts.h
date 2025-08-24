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

#ifndef IBMULATOR_TTS_H
#define IBMULATOR_TTS_H

#include "tts_dev.h"
#include "timers.h"
#include "tts_format.h"
class GUI;

class TTS
{
public:
	enum class ChannelID : int {
		GUI, Guest
	};
	static constexpr int Channels = 2;

	enum class Priority {
		Low,    // will append
		Normal, // will append, discards all Low messages
		High,   // will append, discards all Normal,Low messages
		Top     // will be moved on top, discards all Normal,Low messages
	};

	enum TextFormat {
		NOT_UTF8 = 0x1,    // text shall be converted to UTF-8
		IS_MARKUP = 0x2,   // value shall not be translated for markup
		IS_SENTENCE = 0x4, // text shall be in a sentence
		BREAK_LINES = 0x8, // newlines mark sentences (IS_SENTENCE in implied for every line)
	};

private:
	enum Mode {
		SYNTH, ESPEAK, SAPI, NVDA, FILE
	};
	struct Message {
		std::string text;
		unsigned text_format;
		Priority priority;
		bool purge;
		int volume;
		int pitch;
		int rate;

		Message(std::string _text, unsigned _text_fmt, Priority _pri, bool _purge,
			int _volume, int _pitch, int _rate) :
			text(_text), text_format(_text_fmt), priority(_pri), purge(_purge),
			volume(_volume), pitch(_pitch), rate(_rate)
			{}
		std::string format(const TTSFormat *);
		std::string format_words(const TTSFormat *, const std::string &);
	};
	struct ChannelState {
		ChannelID id = ChannelID::GUI;
		const char *name = "GUI";
		bool enabled = false;
		std::list<Message> queue;
		std::shared_ptr<TTSDev> device;
		std::string text_buf;
		bool purge = false;
		int volume = 0; // -10 .. +10
		int pitch = 0; // -10 .. +10
		int rate = 0; // -10 .. +10

		ChannelState() {}
		ChannelState(ChannelID _id, const char *_name, std::shared_ptr<TTSDev> _device) :
			id(_id), name(_name), enabled(true), device(_device) {}
	};
	struct DeviceData {
		std::shared_ptr<TTSDev> device;
		std::vector<ChannelState*> channels;
		ChannelID speaking_ch = ChannelID::GUI;
	};

	std::vector<DeviceData> m_devices;
	ChannelState m_channels[Channels];
	TTSFormat m_default_fmt;
	GUI *m_gui;
	std::mutex m_mutex;

public:
	TTS() : m_gui(nullptr) {}

	void init(GUI *_gui);
	void enqueue(const std::string &_text,
			Priority _pri = Priority::Normal, unsigned _fmt = IS_SENTENCE,
			bool _purge = true, ChannelID _ch = ChannelID::GUI);
	void stop();
	void stop(ChannelID);
	int volume(ChannelID) const;
	int rate(ChannelID) const;
	int pitch(ChannelID) const;
	bool adj_volume(ChannelID, int);
	bool adj_rate(ChannelID, int);
	bool adj_pitch(ChannelID, int);
	bool set_volume(ChannelID, int);
	bool set_rate(ChannelID, int);
	bool set_pitch(ChannelID, int);
	void adj_volume(int);
	void adj_rate(int);
	void adj_pitch(int);
	void close();
	bool is_open() const { return !m_devices.empty(); }
	bool is_channel_open(ChannelID) const;
	bool is_channel_enabled(ChannelID) const;
	bool enable_channel(ChannelID, bool);
	void speak();

	const TTSFormat * get_format(ChannelID _ch) const;
	const TTSFormat * get_format() const { return get_format(ChannelID::GUI); }

private:
	TTSDev * create_device(Mode _mode) const;
};

#endif