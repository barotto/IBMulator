/*
 * Copyright (C) 2015-2024  Marco Bortolin
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
#ifndef IBMULATOR_HW_DRIVEFX_H
#define IBMULATOR_HW_DRIVEFX_H

#include "mixer.h"
#include "shared_deque.h"
#include "audio/soundfx.h"

#define HDD_SAMPLES_DIR "sounds" FS_SEP "hdd" FS_SEP
#define FDD_SAMPLES_DIR "sounds" FS_SEP "floppy" FS_SEP
#define CDROM_SAMPLES_DIR "sounds" FS_SEP "cdrom" FS_SEP

class DriveFX : public SoundFX
{
protected:
	std::mutex m_clear_mutex;
	struct SeekEvent {
		uint64_t time;
		double distance;
		uint32_t userdata;
	};
	shared_deque<SeekEvent> m_seek_events;
	std::atomic<bool> m_spinning, m_spin_change;
	struct {
		std::shared_ptr<MixerChannel> seek;
		std::shared_ptr<MixerChannel> spin;
	} m_channels;

public:
	DriveFX();
	virtual ~DriveFX();

	virtual void install(MixerChannelHandler _spin_channel, const char *_spin_name,
			MixerChannelHandler _seek_channel, const char *_seek_name,
			const AudioSpec &_spec);
	virtual void remove();
	virtual void seek(int _c0, int _c1, int _tot_cyls);
	virtual void seek(uint64_t _at_time, int _c0, int _c1, int _tot_cyls);
	virtual void spin(bool _spinning, bool _change_state);
	virtual void clear_seek_events();
};

#endif
