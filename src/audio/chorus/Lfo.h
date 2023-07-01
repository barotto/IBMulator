//==============================================================================
/**
	This class implements a LUT-based LFO with various waveforms and linear
	interpolation.
	
	It uses 32-bit fixed-point phase and increment, where the 8 MSB
	represent the integer part of the number and the 24 LSB the fractionnal part
	
	@author Remy Muller
	@date   20030822
*/
//==============================================================================
#ifndef LFO_H
#define LFO_H

#include <string>
#include "OscNoise.h"

namespace TAL {

class Lfo
{
public:
	/** phase type */
	float phase = {};
	float result = {};
	float resultSmooth = {};
	
	float inc = {};
	
	float samplerate = {};
	float randomValue = {};
	float randomValueOld = {};

public:
	// prevent copying
	Lfo(const Lfo &) = delete;
	// prevent assignment
	Lfo &operator=(const Lfo &) = delete;
	
	/**  @param samplerate the samplerate in Hz */
	Lfo(float _samplerate);
	
	/** increments the phase and outputs the new LFO value.
	  @return the new LFO value between [-1;+1] */
	float tick(int waveform);
	
	void resetPhase(float _phase);
	
	/** change the current rate
	  @param rate new rate in Hz */
	void setRate(const float rate);
	
	/** change the current samplerate
	  @param samplerate new samplerate in Hz */
	void setSampleRate(float _samplerate) {
		samplerate = (_samplerate > 0.0) ? _samplerate : 44100.0f;
	}
	
	/** select the desired waveform for the LFO
	  @param index tag of the waveform
	*/
	void setWaveform(int index);


private:
	OscNoise noiseOsc = {};
	bool freqWrap = {};
	
	/** table length is 256+1, with table[0] = table[256]
	  that way we can perform linear interpolation:
	  \f[ val = (1-frac)*u[n] + frac*u[n+1] \f]
	  even with n = 255.
	  For n higher than 255, n is automatically  wrapped to 0-255*/
	float tableSin[257];
	float tableTri[257];
	float tableSaw[257];
	float tableRec[257];
	float tableExp[257];

	float generate_random_fraction() {
		// -1.0 .. 1.0
		return float(rand() % 200 - 100) / 100.f;
	}
};

}

#endif	// #ifndef LFO_H
