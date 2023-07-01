/*
	==============================================================================
	This file is part of Tal-NoiseMaker by Patrick Kunz.

	Copyright(c) 2005-2010 Patrick Kunz, TAL
	Togu Audio Line, Inc.
	http://kunz.corrupt.ch

	This file may be licensed under the terms of of the
	GNU General Public License Version 2 (the ``GPL'').

	Software distributed under the License is distributed
	on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
	express or implied. See the GPL for the specific language
	governing rights and limitations.

	You should have received a copy of the GPL along with this
	program. If not, go to http://www.gnu.org/licenses/gpl.html
	or write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
	==============================================================================
*/
#ifndef CHORUSENGINE_H
#define CHORUSENGINE_H

#include <memory>

#include "Chorus.h"
#include "DCBlock.h"

namespace TAL {

class ChorusEngine
{
public:
	std::unique_ptr<Chorus> chorus1L = {};
	std::unique_ptr<Chorus> chorus1R = {};
	std::unique_ptr<Chorus> chorus2L = {};
	std::unique_ptr<Chorus> chorus2R = {};

	DCBlock dcBlock1L = {};
	DCBlock dcBlock1R = {};
	DCBlock dcBlock2L = {};
	DCBlock dcBlock2R = {};

	bool isChorus1Enabled = false;
	bool isChorus2Enabled = false;
	
	float gain = 1.0f;

	// prevent copying
	ChorusEngine(const ChorusEngine &) = delete;
	// prevent assignment
	ChorusEngine &operator=(const ChorusEngine &) = delete;

	ChorusEngine(float sampleRate)
	{
		setUpChorus(sampleRate);
	}

	void setSampleRate(float sampleRate)
	{
		setUpChorus(sampleRate);
		setEnablesChorus(false, false);
	}

	void setEnablesChorus(bool _isChorus1Enabled, bool _isChorus2Enabled)
	{
		this->isChorus1Enabled = _isChorus1Enabled;
		this->isChorus2Enabled = _isChorus2Enabled;
	}

	void setUpChorus(float sampleRate)
	{
		                                             // phase rate   delay
		chorus1L = std::make_unique<Chorus>(sampleRate, 1.0f, 0.5f,  7.0f);
		chorus1R = std::make_unique<Chorus>(sampleRate, 0.0f, 0.5f,  7.0f);
		chorus2L = std::make_unique<Chorus>(sampleRate, 0.0f, 0.83f, 7.0f);
		chorus2R = std::make_unique<Chorus>(sampleRate, 1.0f, 0.83f, 7.0f);
	}
	
	void setGain(float _gain) { gain = _gain; }

	void process(int _frames, float *_data)
	{
		for(int i=0; i<_frames; i++) {
			float resultR = 0.0f;
			float resultL = 0.0f;

			float *sampleL = &_data[i*2];
			float *sampleR = &_data[i*2 + 1];

			if (isChorus1Enabled)
			{
				resultL += chorus1L->process(sampleL);
				resultR += chorus1R->process(sampleR);
				dcBlock1L.tick(&resultL, 0.01f);
				dcBlock1R.tick(&resultR, 0.01f);
			}

			if (isChorus2Enabled)
			{
				resultL += chorus2L->process(sampleL);
				resultR += chorus2R->process(sampleR);
				dcBlock2L.tick(&resultL, 0.01f);
				dcBlock2R.tick(&resultR, 0.01f);
			}

			*sampleL += (resultL * 1.4f) * gain;
			*sampleR += (resultR * 1.4f) * gain;
		}
	}
};

}

#endif

