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
#include "Design.h"
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

struct TypeIBase : DesignBase
{
  enum
  {
    NumParams = 3
  };

  static int getNumParams ()
  {
    return 3;
  }

  static const ParamInfo getParamInfo_2 ()
  {
    return ParamInfo::defaultFrequencyParam ();
  }
};

template <class FilterClass>
struct TypeI : TypeIBase, FilterClass
{
  void setParams (const Params& params)
  {
    FilterClass::setup (int(params[idOrder]), params[idSampleRate], params[idFrequency]);
  }
};

struct TypeIIBase : DesignBase
{
  enum
  {
    NumParams = 4
  };

  static int getNumParams ()
  {
    return 4;
  }

  static const ParamInfo getParamInfo_2 ()
  {
    return ParamInfo::defaultFrequencyParam ();
  }

  static const ParamInfo getParamInfo_3 ()
  {
    return ParamInfo::defaultBandwidthHzParam ();
  }
};

template <class FilterClass>
struct TypeII : TypeIIBase, FilterClass
{
  void setParams (const Params& params)
  {
    FilterClass::setup (int(params[idOrder]), params[idSampleRate], params[idFrequency], params[idBandwidthHz]);
  }
};

struct TypeIIIBase : DesignBase
{
  enum
  {
    NumParams = 4
  };

  static int getNumParams ()
  {
    return 4;
  }

  static const ParamInfo getParamInfo_2 ()
  {
    return ParamInfo::defaultFrequencyParam ();
  }

  static const ParamInfo getParamInfo_3 ()
  {
    return ParamInfo::defaultGainParam ();
  }
};

template <class FilterClass>
struct TypeIII : TypeIIIBase, FilterClass
{
  void setParams (const Params& params)
  {
    FilterClass::setup (int(params[idOrder]),
                        params[idSampleRate],
                        params[idFrequency],
                        params[idGain]);
  }
};

struct TypeIVBase : DesignBase
{
  enum
  {
    NumParams = 5
  };

  static int getNumParams ()
  {
    return 5;
  }

  static const ParamInfo getParamInfo_2 ()
  {
    return ParamInfo::defaultFrequencyParam ();
  }

  static const ParamInfo getParamInfo_3 ()
  {
    return ParamInfo::defaultBandwidthHzParam ();
  }

  static const ParamInfo getParamInfo_4 ()
  {
    return ParamInfo::defaultGainParam ();
  }
};

template <class FilterClass>
struct TypeIV : TypeIVBase, FilterClass
{
  void setParams (const Params& params)
  {
    FilterClass::setup (int(params[idOrder]), params[idSampleRate], params[idFrequency], params[idBandwidthHz], params[idGain]);
  }
};

// Factored kind and name

struct LowPassDescription
{
  static Kind getKind () { return kindLowPass; }
  static const char* getName() { return "Butterworth Low Pass"; }
};

struct HighPassDescription
{
  static Kind getKind () { return kindHighPass; }
  static const char* getName() { return "Butterworth High Pass"; }
};

struct BandPassDescription
{
  static Kind getKind () { return kindHighPass; }
  static const char* getName() { return "Butterworth Band Pass"; }
};

struct BandStopDescription
{
  static Kind getKind () { return kindHighPass; }
  static const char* getName() { return "Butterworth Band Stop"; }
};

struct LowShelfDescription
{
  static Kind getKind () { return kindLowShelf; }
  static const char* getName() { return "Butterworth Low Shelf"; }
};

struct HighShelfDescription
{
  static Kind getKind () { return kindHighShelf; }
  static const char* getName() { return "Butterworth High Shelf"; }
};

struct BandShelfDescription
{
  static Kind getKind () { return kindBandShelf; }
  static const char* getName() { return "Butterworth Band Shelf"; }
};

// This glues on the Order parameter
template <int MaxOrder,
          template <class> class TypeClass,
          template <int> class FilterClass>
struct OrderBase : TypeClass <FilterClass <MaxOrder> >
{
  const ParamInfo getParamInfo_1 () const
  {
    return ParamInfo (idOrder, "order", "Order", "Order",
                       1, MaxOrder, 2,
                       &ParamInfo::Int_toControlValue,
                       &ParamInfo::Int_toNativeValue,
                       &ParamInfo::Int_toString);

  }
};

//------------------------------------------------------------------------------

//
// Design filters
//

template <int MaxOrder>
struct LowPass : OrderBase <MaxOrder, TypeI, Butterworth::LowPass>,
                 LowPassDescription
{
};

template <int MaxOrder>
struct HighPass : OrderBase <MaxOrder, TypeI, Butterworth::HighPass>,
                  HighPassDescription
{
};

template <int MaxOrder>
struct BandPass : OrderBase <MaxOrder, TypeII, Butterworth::BandPass>,
                  BandPassDescription
{
};

template <int MaxOrder>
struct BandStop : OrderBase <MaxOrder, TypeII, Butterworth::BandStop>,
                  BandStopDescription
{
};

template <int MaxOrder>
struct LowShelf : OrderBase <MaxOrder, TypeIII, Butterworth::LowShelf>,
                  LowShelfDescription
{
};

template <int MaxOrder>
struct HighShelf : OrderBase <MaxOrder, TypeIII, Butterworth::HighShelf>,
                   HighShelfDescription
{
};

template <int MaxOrder>
struct BandShelf : OrderBase <MaxOrder, TypeIV, Butterworth::BandShelf>,
                   BandShelfDescription
{
};

}

}

}

#endif

