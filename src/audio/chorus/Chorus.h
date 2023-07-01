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
#ifndef CHORUS_H
#define CHORUS_H

namespace TAL {

class Lfo;
class OnePoleLP;

class Chorus
{
public:
	float sampleRate = {};
	float delayTime = {};

	Lfo *lfo = {};
	OnePoleLP *lp = {};

	int delayLineLength = {};
	float *delayLineStart = {};
	float *delayLineEnd = {};
	float *writePtr = {};
	float delayLineOutput = {};

	float rate;

	// Runtime variables
	float offset = {};
	float diff = {};
	float frac = {};
	float *ptr = {};
	float *ptr2 = {};

	int readPos = {};

	float z1 = {};
	float z2 = {};
	float mult = {};
	float sign = {};

	// lfo
	float lfoPhase = {};
	float lfoStepSize = {};
	float lfoSign = {};

	// prevent copying
	Chorus(const Chorus &) = delete;
	// prevent assignment
	Chorus &operator=(const Chorus &) = delete;

	Chorus(float _sampleRate, float _phase, float _rate, float _delayTime);
	~Chorus();

	float process(float *sample);

	float nextLFO();
};

}

#endif
