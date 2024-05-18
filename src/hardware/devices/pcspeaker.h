/*
 * Copyright (C) 2015-2024  Marco Bortolin
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

#ifndef IBMULATOR_HW_PCSPEAKER_H
#define IBMULATOR_HW_PCSPEAKER_H

#include "hardware/iodevice.h"
#include "mixer.h"

#define DEFAULT_PCSPEAKER_FILTER "pc-speaker"
#define DEFAULT_PCSPEAKER_REVERB "tiny"


class PCSpeaker : public IODevice
{
	IODEVICE(PCSpeaker, "PC-Speaker")

private:
	struct SpeakerEvent {
		uint64_t ticks;
		bool active;
		bool out;
	};

	std::deque<SpeakerEvent> m_events;

	struct {
		double level;
		size_t events_cnt;
	} m_s;

	SRC_STATE *m_SRC = nullptr;
	AudioBuffer m_pitbuf;
	AudioBuffer m_outbuf;
	std::mutex m_mutex;
	std::shared_ptr<MixerChannel> m_channel;
	uint64_t m_last_time = 0;
	double m_samples_rem = 0.0;
	double m_level = 0.0;

public:
	PCSpeaker(Devices *_dev);
	~PCSpeaker();

	void install();
	void remove();
	void reset(unsigned);
	void power_off();
	void config_changed();

	void add_event(uint64_t _ticks, bool _active, bool _out);
	void activate();
	void create_samples(uint64_t _time_span_ns, bool _first_upd);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
