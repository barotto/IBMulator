/*
 * Copyright (C) 2020-2022  Marco Bortolin
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
#include "midi.h"

MIDIDev::MIDIDev(MIDI *_instance)
{
	m_instance = _instance;
	
	static std::map<std::string, unsigned> devtypes = {
		{ "",      Type::UNKNOWN },
		{ "mt-32", Type::LA      },
		{ "mt32",  Type::LA      },
		{ "la",    Type::LA      },
		{ "gs",    Type::GS      },
		{ "gm",    Type::GM      },
		{ "xg",    Type::XG      }
	};
	m_type = static_cast<Type>(
		g_program.config().get_enum(MIDI_SECTION, MIDI_DEVTYPE, devtypes, Type::UNKNOWN)
	);
	switch(m_type) {
		case Type::LA: m_name = " LA"; break;
		case Type::GS: m_name = " GS"; break;
		case Type::GM: m_name = " GM"; break;
		case Type::XG: m_name = " XG"; break;
		default: m_name = ""; break;
	}
}

void MIDIDev::reset()
{
	std::vector<std::vector<uint8_t>> reset_syx = {
		// UNKNOWN
		{},
		// LA
		{ 0xf0, 0x41, 0x10, 0x16, 0x12, 0x7f, 0x01, 0xf7 },
		// GS
		{ 0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7 },
		// GM
		{ 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 },
		// XG
		{ 0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7 }
	};
	
	PDEBUGF(LOG_V0, LOG_MIDI, "%s: resetting device\n", name());
	
	// don't send the message directly with a send_sysex because delays must be accounted for.
	if(m_type == Type::UNKNOWN) {
		// send all the reset messages, the attached device will ignore those not relevant
		for(size_t i=1; i<reset_syx.size(); i++) {
			m_instance->put_bytes(reset_syx[i]);
		}
	} else {
		m_instance->put_bytes(reset_syx[m_type]);
	}
}