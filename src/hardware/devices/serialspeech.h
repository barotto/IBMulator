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

#ifndef IBMULATOR_SERIALSPEECH_H
#define IBMULATOR_SERIALSPEECH_H

class TTS;
class TTSFormat;

class SerialSpeech
{
private:
	using Handler = std::function<void(SerialSpeech&, int)>;
	struct Command {
		const char *name;
		Handler handler;
	};
	static const std::map<char, Command> ms_handlers;

	std::string m_num;
	std::string m_buffer;
	std::vector<std::pair<std::string, int>> m_lst;
	bool m_in_command;
	int m_rx;

public:
	void init();
	void reset(unsigned _type);
	void power_off();

	bool serial_read_byte(uint8_t *_byte);
	bool serial_write_byte(uint8_t _byte);

private:
	TTS *tts() const;
	void clear();
	void process();
	void cmd_volume(int);
	void cmd_rate(int);
	void cmd_pitch(int);
	void cmd_tone(int);
};

#endif
