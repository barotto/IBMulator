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

#ifndef DSPFILTERS_PARAMS_H
#define DSPFILTERS_PARAMS_H

#include "Common.h"
#include "Types.h"

namespace Dsp {

/*
 * System for abstracting parameterizable filter specifications.
 *
 * This provides a "GUI-friendly" layer to the filters. Note that
 * it is not necessary to use this layer, it is possible to instantiate
 * the filters and their associated processing state directly,
 * and bypass the overhead for this API if it is not needed.
 *
 */

// Unique IDs to help identify parameters
enum ParamID
{
  idSampleRate,
  idFrequency,
  idBandwidthHz,
  idGain,
  idOrder,
  
  maxParameters
};

//
// Provides meta-information about a filter parameter
// to achieve run-time introspection.
//
class ParamInfo
{
public:
  typedef double (ParamInfo::*toControlValue_t) (double) const;
  typedef double (ParamInfo::*toNativeValue_t) (double) const;
  typedef std::string (ParamInfo::*toString_t) (double) const;

private:
  ParamID m_id;
  const char* m_szSlug;
  const char* m_szLabel;
  const char* m_szName;
  double m_arg1;
  double m_arg2;
  double m_defaultNativeValue;
  toControlValue_t m_toControlValue;
  toNativeValue_t m_toNativeValue;
  toString_t m_toString;
  
  static ParamInfo ms_defaults[maxParameters];
  
public:

  ParamInfo() = delete;

  ParamInfo(ParamID id,
              const char* szSlug,
              const char* szLabel,
              const char* szName,
              double arg1,
              double arg2,
              double defaultNativeValue,
              toControlValue_t toControlValue_proc,
              toNativeValue_t toNativeValue_proc,
              toString_t toString_proc)
    : m_id (id)
    , m_szSlug (szSlug)
    , m_szLabel (szLabel)
    , m_szName (szName)
    , m_arg1 (arg1)
    , m_arg2 (arg2)
    , m_defaultNativeValue (defaultNativeValue)
    , m_toControlValue (toControlValue_proc)
    , m_toNativeValue (toNativeValue_proc)
    , m_toString (toString_proc)
  {
  }

  // Used to identify well-known parameters (like cutoff frequency)
  ParamID getId() const
  {
    return m_id;
  }
  
  // Returns a short name suitable for comparing with passed in name read from ini files
  const char* getSlug() const
  {
    return m_szSlug;
  }

  // Returns a short label suitable for placement on a control
  const char* getLabel() const
  {
    return m_szLabel;
  }

  // Returns the full name
  const char* getName() const
  {
    return m_szName;
  }
  
  double getMin() const
  {
    return m_arg1;
  }
  
  double getMax() const
  {
    return m_arg2;
  }
  
  double getDefaultValue() const
  {
    return m_defaultNativeValue;
  }

  //
  // Control value is always in the range [0..1]
  //
  double toControlValue(double nativeValue) const
  {
    return (this->*m_toControlValue) (nativeValue);
  }

  //
  // Native value is in filter-specific units. For example,
  // cutoff frequency would probably be in Hertz.
  //
  double toNativeValue(double controlValue) const
  {
    return (this->*m_toNativeValue) (controlValue);
  }

  std::string toString(double nativeValue) const
  {
    return (this->*m_toString) (nativeValue);
  }

  double clamp(double nativeValue) const;

  //
  // These routines are used as function pointers when
  // constructing the various ParamInfo used by filters
  //

  double Int_toControlValue(double nativeValue) const;
  double Int_toNativeValue(double controlValue) const;

  double Real_toControlValue(double nativeValue) const;
  double Real_toNativeValue(double controlValue) const;

  double Log_toControlValue(double nativeValue) const;
  double Log_toNativeValue(double controlValue) const;

  double Pow2_toControlValue(double nativeValue) const;
  double Pow2_toNativeValue(double controlValue) const;

  std::string Int_toString(double nativeValue) const;
  std::string Hz_toString(double nativeValue) const;
  std::string Real_toString(double nativeValue) const;
  std::string Db_toString(double nativeValue) const;
  
  static const ParamInfo & defaults(ParamID _id)
  {
    return ms_defaults[_id];
  }
};

struct Params
{
  Params()
  {
    clear();
  }
  
  void clear()
  {
    for (int i = 0; i < ParamID::maxParameters; ++i) {
      value[i] = 0.0;
    }
  }
  
  void setToDefaults()
  {
    for(int i = 0; i < ParamID::maxParameters; i++) {
      value[i] = ParamInfo::defaults(static_cast<ParamID>(i)).getDefaultValue();
    }
  }

  double& operator[](ParamID index)
  {
    assert(index < maxParameters);
    return value[index];
  }

  const double& operator[](ParamID index) const
  {
    assert(index < maxParameters);
    return value[index];
  }

  double value[maxParameters];
};

}

#endif
