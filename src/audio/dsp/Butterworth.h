/*******************************************************************************

"A Collection of Useful C++ Classes for Digital Signal Processing"
 By Vinnie Falco

Official project location:
https://github.com/vinniefalco/DSPFilters

See Documentation.cpp for contact information, notes, and bibliography.

--------------------------------------------------------------------------------

License: MIT License (http://www.opensource.org/licenses/mit-license.php)
Copyright (c) 2009 by Vinnie Falco

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*******************************************************************************/

#ifndef DSPFILTERS_BUTTERWORTH_H
#define DSPFILTERS_BUTTERWORTH_H

#include "Cascade.h"
#include "Common.h"
#include "Filter.h"
#include "PoleFilter.h"

namespace Dsp {

/*
 * Filters with Butterworth response characteristics
 *
 */

namespace Butterworth {

// Half-band analog prototypes (s-plane)

class AnalogLowPass : public LayoutBase
{
public:
  AnalogLowPass ();

  void design (const int numPoles);

private:
  int m_numPoles;
};

//------------------------------------------------------------------------------

class AnalogLowShelf : public LayoutBase
{
public:
  AnalogLowShelf ();

  void design (int numPoles, double gainDb);

private:
  int m_numPoles;
  double m_gainDb;
};

//------------------------------------------------------------------------------

// Factored implementations to reduce template instantiations

struct LowPassBase : PoleFilterBase <AnalogLowPass>
{
  void setup (int order,
              double sampleRate,
              double cutoffFrequency);
};

struct HighPassBase : PoleFilterBase <AnalogLowPass>
{
  void setup (int order,
              double sampleRate,
              double cutoffFrequency);
};

struct BandPassBase : PoleFilterBase <AnalogLowPass>
{
  void setup (int order,
              double sampleRate,
              double centerFrequency,
              double widthFrequency);
};

struct BandStopBase : PoleFilterBase <AnalogLowPass>
{
  void setup (int order,
              double sampleRate,
              double centerFrequency,
              double widthFrequency);
};

struct LowShelfBase : PoleFilterBase <AnalogLowShelf>
{
  void setup (int order,
              double sampleRate,
              double cutoffFrequency,
              double gainDb);
};

struct HighShelfBase : PoleFilterBase <AnalogLowShelf>
{
  void setup (int order,
              double sampleRate,
              double cutoffFrequency,
              double gainDb);
};

struct BandShelfBase : PoleFilterBase <AnalogLowShelf>
{
  void setup (int order,
              double sampleRate,
              double centerFrequency,
              double widthFrequency,
              double gainDb);
};

//------------------------------------------------------------------------------

//
// Raw filters
//

template <int MaxOrder>
struct LowPass : PoleFilter <LowPassBase, MaxOrder>
{
};

template <int MaxOrder>
struct HighPass : PoleFilter <HighPassBase, MaxOrder>
{
};

template <int MaxOrder>
struct BandPass : PoleFilter <BandPassBase, MaxOrder, MaxOrder*2>
{
};

template <int MaxOrder>
struct BandStop : PoleFilter <BandStopBase, MaxOrder, MaxOrder*2>
{
};

template <int MaxOrder>
struct LowShelf : PoleFilter <LowShelfBase, MaxOrder>
{
};

template <int MaxOrder>
struct HighShelf : PoleFilter <HighShelfBase, MaxOrder>
{
};

template <int MaxOrder>
struct BandShelf : PoleFilter <BandShelfBase, MaxOrder, MaxOrder*2>
{
};

//------------------------------------------------------------------------------

//
// Gui-friendly Design layer
//

namespace Design {

template <class FilterClass>
struct TypeI : FilterClass
{
  void setParams (const Params& params)
  {
    FilterClass::setup (int(params[idOrder]),
                        params[idSampleRate],
                        params[idFrequency]);
  }
  
  const std::vector<ParamID> getParamIDs() const {
    return { idOrder, idSampleRate, idFrequency };
  }
};

template <class FilterClass>
struct TypeII : FilterClass
{
  void setParams (const Params& params)
  {
    FilterClass::setup (int(params[idOrder]),
                        params[idSampleRate],
                        params[idFrequency], 
                        params[idBandwidthHz]);
  }
  
  const std::vector<ParamID> getParamIDs() const {
    return { idOrder, idSampleRate, idFrequency, idBandwidthHz };
  }
};

template <class FilterClass>
struct TypeIII : FilterClass
{
  void setParams (const Params& params)
  {
    FilterClass::setup (int(params[idOrder]),
                        params[idSampleRate],
                        params[idFrequency],
                        params[idGain]);
  }
  
  const std::vector<ParamID> getParamIDs() const {
    return { idOrder, idSampleRate, idFrequency, idGain };
  }
};

template <class FilterClass>
struct TypeIV : FilterClass
{
  void setParams (const Params& params)
  {
    FilterClass::setup (int(params[idOrder]),
                        params[idSampleRate],
                        params[idFrequency],
                        params[idBandwidthHz],
                        params[idGain]);
  }
  
  const std::vector<ParamID> getParamIDs() const {
    return { idOrder, idSampleRate, idFrequency, idBandwidthHz, idGain };
  }
};


//
// Design filters
//

template <int MaxOrder>
struct LowPass : TypeI<Butterworth::LowPass<MaxOrder>>
{
  static Kind getKind () { return kindLowPass; }
  static const char* getName() { return "Low Pass"; }
  static const char* getSlug() { return "lowpass"; }
};

template <int MaxOrder>
struct HighPass : TypeI<Butterworth::HighPass<MaxOrder>>
{
  static Kind getKind () { return kindHighPass; }
  static const char* getName() { return "High Pass"; }
  static const char* getSlug() { return "highpass"; }
};

template <int MaxOrder>
struct BandPass : TypeII<Butterworth::BandPass<MaxOrder>>
{
  static Kind getKind () { return kindHighPass; }
  static const char* getName() { return "Band Pass"; }
  static const char* getSlug() { return "bandpass"; }
};

template <int MaxOrder>
struct BandStop : TypeII<Butterworth::BandStop<MaxOrder>>
{
  static Kind getKind () { return kindHighPass; }
  static const char* getName() { return "Band Stop"; }
  static const char* getSlug() { return "bandstop"; }
};

template <int MaxOrder>
struct LowShelf : TypeIII<Butterworth::LowShelf<MaxOrder>>
{
  static Kind getKind () { return kindLowShelf; }
  static const char* getName() { return "Low Shelf"; }
  static const char* getSlug() { return "lowshelf"; }
};

template <int MaxOrder>
struct HighShelf : TypeIII<Butterworth::HighShelf<MaxOrder>>
{
  static Kind getKind () { return kindHighShelf; }
  static const char* getName() { return "High Shelf"; }
  static const char* getSlug() { return "highshelf"; }
};

template <int MaxOrder>
struct BandShelf : TypeIV<Butterworth::BandShelf<MaxOrder>>
{
  static Kind getKind () { return kindBandShelf; }
  static const char* getName() { return "Band Shelf"; }
  static const char* getSlug() { return "bandshelf"; }
};

} // Design

} // Butterworth

} // Dsp

#endif

