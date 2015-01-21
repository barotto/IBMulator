/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_HW_PCSPEAKER_H
#define IBMULATOR_HW_PCSPEAKER_H

#include "hardware/iodevice.h"
#include "mixer.h"

class PCSpeaker;
extern PCSpeaker g_pcspeaker;

class PCSpeaker : public IODevice
{
private:

	struct SpeakerEvent {
		uint64_t time;
		bool active;
		bool out;
	};

	std::deque<SpeakerEvent> m_events;

	struct {
		double level;
		double velocity;
		double samples;
		size_t events_cnt;
	} m_s;

	std::vector<int16_t> m_samples_buffer;
	uint32_t m_rate;
	double m_nsec_per_sample;
	double m_samples_per_nsec;
	std::mutex m_lock;
	std::shared_ptr<MixerChannel> m_channel;

public:

	PCSpeaker();
	~PCSpeaker();

	void init();
	void reset(unsigned);
	void power_off();
	void config_changed();
	const char* get_name() { return "PCSpeaker"; }

	void add_event(uint64_t _time, bool _active, bool _out);
	void activate();
	void create_samples(uint64_t _time);

	void save_state(StateBuf &_state);
	void restore_state(StateBuf &_state);
};

#endif
