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
#ifndef IBMULATOR_HW_GUIFX_H
#define IBMULATOR_HW_GUIFX_H

#include "mixer.h"
#include "audio/soundfx.h"


class GUIFX : public SoundFX
{
protected:
	std::shared_ptr<MixerChannel> m_channel;

public:
	GUIFX();
	virtual ~GUIFX();

	virtual void init(Mixer *_mixer,
			MixerChannelHandler _channel_fn, const char *_channel_name,
			const AudioSpec &_spec);
};

#endif
