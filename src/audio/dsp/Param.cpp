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

#include "Common.h"
#include "Params.h"
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace Dsp {


double ParamInfo::clamp (double nativeValue) const
{
  const double minValue = toNativeValue (0);
  const double maxValue = toNativeValue (1);
  if (nativeValue < minValue)
    nativeValue = minValue;
  else if (nativeValue > maxValue)
    nativeValue = maxValue;
  return nativeValue;
}

//------------------------------------------------------------------------------

double ParamInfo::Int_toControlValue (double nativeValue) const
{
  return (nativeValue - m_arg1) / (m_arg2 - m_arg1);
}

double ParamInfo::Int_toNativeValue (double controlValue) const
{
  return std::floor (m_arg1 + controlValue * (m_arg2 - m_arg1) + 0.5);
}

//------------------------------------------------------------------------------

double ParamInfo::Real_toControlValue (double nativeValue) const
{
  return (nativeValue - m_arg1) / (m_arg2 - m_arg1);
}

double ParamInfo::Real_toNativeValue (double controlValue) const
{
  return m_arg1 + controlValue * (m_arg2 - m_arg1);
}

//------------------------------------------------------------------------------

double ParamInfo::Log_toControlValue (double nativeValue) const
{
  const double base = 1.5;
  double l0 = log (m_arg1) / log (base);
  double l1 = log (m_arg2) / log (base);
  return (log (nativeValue) / log(base) - l0) / (l1 - l0);
}

double ParamInfo::Log_toNativeValue (double controlValue) const
{
  const double base = 1.5;
  double l0 = log (m_arg1) / log (base);
  double l1 = log (m_arg2) / log (base);
  return pow (base, l0 + controlValue * (l1 - l0));
}

//------------------------------------------------------------------------------

double ParamInfo::Pow2_toControlValue (double nativeValue) const
{
  return ((log (nativeValue) / log (2.)) - m_arg1) / (m_arg2 - m_arg1);
}

double ParamInfo::Pow2_toNativeValue (double controlValue) const
{
  return pow (2., (controlValue * (m_arg2 - m_arg1)) + m_arg1);
}

//------------------------------------------------------------------------------

std::string ParamInfo::Int_toString (double nativeValue) const
{
  std::ostringstream os;
  os << int (nativeValue);
  return os.str();
}

std::string ParamInfo::Hz_toString (double nativeValue) const
{
  std::ostringstream os;
  os << int (nativeValue) << " Hz";
  return os.str();
}

std::string ParamInfo::Real_toString (double nativeValue) const
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(3) << nativeValue;
  return os.str();
}

std::string ParamInfo::Db_toString (double nativeValue) const
{
  const double af = fabs (nativeValue);
  int prec;
  if (af < 1)
    prec = 3;
  else if (af < 10)
    prec = 2;
  else
    prec = 1;
  std::ostringstream os;
  os << std::fixed << std::setprecision (prec) << nativeValue << " dB";
  return os.str();
}

//------------------------------------------------------------------------------

ParamInfo ParamInfo::ms_defaults[maxParameters] = {
	ParamInfo(idSampleRate, "fs", "Fs", "Sample Rate",
			 11025, 192000, 44100,
			 &ParamInfo::Real_toControlValue,
			 &ParamInfo::Real_toNativeValue,
			 &ParamInfo::Hz_toString),
	ParamInfo(idFrequency, "fc", "Fc", "Frequency (Hz)",
			 10, 22040, 2000,
			 &ParamInfo::Log_toControlValue,
			 &ParamInfo::Log_toNativeValue,
			 &ParamInfo::Hz_toString),
	ParamInfo(idBandwidthHz, "bw", "BW", "Bandwidth (Hz)",
			 10, 22040, 1720,
			 &ParamInfo::Log_toControlValue,
			 &ParamInfo::Log_toNativeValue,
			 &ParamInfo::Hz_toString),
	ParamInfo(idGain, "gain", "Gain", "Gain (dB)",
			 -24, 24, -6,
			 &ParamInfo::Real_toControlValue,
			 &ParamInfo::Real_toNativeValue,
			 &ParamInfo::Db_toString),
	ParamInfo(idOrder, "order", "Order", "Order",
			 1, 50, 3,
			 &ParamInfo::Real_toControlValue,
			 &ParamInfo::Real_toNativeValue,
			 &ParamInfo::Real_toString),
};

} // Dsp

