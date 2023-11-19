/*
 * Copyright (C) 2023  Marco Bortolin
 *
 * This file is part of IBMulator
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
#ifndef IBMULATOR_HW_SERIALMODEMFX_H
#define IBMULATOR_HW_SERIALMODEMFX_H

#include "mixer.h"
#include "shared_deque.h"
#include "audio/soundfx.h"

#define MODEM_DIAL_TONE_US SEC_TO_USEC(1.0)
#define MODEM_DTMF_US SEC_TO_USEC(0.1)
#define MODEM_NO_TONE_US SEC_TO_USEC(1.0)
#define MODEM_RESULT_TONE_REPEATS 3
#define MODEM_RINGINTERVAL_S 3.0
#define MODEM_RINGINTERVAL_US SEC_TO_USEC(MODEM_RINGINTERVAL_S)
#define MODEM_RINGING_MAX (30.0 / MODEM_RINGINTERVAL_S)

class SerialModemFX : public SoundFX
{
private:
	enum ToneType {
		DIAL_TONE,
		BUSY_TONE,
		REORDER_TONE,
		RINGING_TONE,
		DISCONNECT_TONE,
		INCOMING_RING,
		HANDSHAKE,

		NO_TONE,
	};
	enum DTMFType {
		DTMF_POUND, DTMF_STAR,
		DTMF_0, DTMF_1, DTMF_2, DTMF_3, DTMF_4,
		DTMF_5, DTMF_6, DTMF_7, DTMF_8, DTMF_9,
		DTMF_A, DTMF_B, DTMF_C, DTMF_D,
	};

	static std::vector<AudioBuffer> ms_tones;
	static std::vector<AudioBuffer> ms_dtmf;
	static AudioBuffer ms_incoming;
	std::shared_ptr<MixerChannel> m_channel;
	
	struct ModemSound {
		uint64_t time;
		char code;
		double duration;
	};
	shared_deque<ModemSound> m_events;
	bool m_enabled = true;

public:
	SerialModemFX() : SoundFX() {}

	void install(unsigned _baud_rate);
	void auto_filters_cb();
	void remove();
	void silence();
	uint64_t dial(const std::string _str, int _ringing_ms);
	uint64_t busy();
	uint64_t disconnect();
	uint64_t reorder();
	uint64_t incoming();
	uint64_t handshake();
	void set_volume(int _level);
	void enable(bool _enabled);

	bool create_samples(uint64_t _time_span_ns, bool _prebuf, bool _first_upd);
	
private:
	uint64_t enqueue(ToneType _tone, double _duration, int _repeats);
};

#endif
