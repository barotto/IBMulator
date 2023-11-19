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

/*
 * Modified for IBMulator. Don't use this code for your project, instead use
 * the original version from https://github.com/vinniefalco/DSPFilters
 */

#ifndef DSPFILTERS_FILTER_H
#define DSPFILTERS_FILTER_H

#include "Common.h"
#include "MathSupplement.h"
#include "Params.h"
#include "State.h"
#include "Types.h"

namespace Dsp {

/*
 * Filter
 *
 * Full abstraction of a digital IIR filter.
 * Supports run-time introspection and modulation of filter
 * parameters.
 *
 */
class Filter
{
public:
  virtual ~Filter() {}

  virtual Kind getKind() const = 0;
  virtual const std::string getName() const = 0;
  virtual const std::string getSlug() const = 0;
  virtual const std::vector<ParamID> getParamIDs() const = 0;

  double getParam(ParamID param) const { return m_params[param]; }
  const Params& getParams() const { return m_params; }
  
  void setParam(ParamID param, double nativeValue);
  void setParams(const Params& parameters);

  virtual std::string getDefinitionString() const;
  
  virtual std::vector<PoleZeroPair> getPoleZeros() const = 0;
  virtual complex_t response(double normalizedFrequency) const = 0;

  virtual int getNumChannels() = 0;
  virtual void reset() = 0;
  virtual void process(int numSamples, float* data) = 0;
  virtual void process(int numSamples, double* data) = 0;

protected:
  virtual void doSetParams(const Params& parameters) = 0;

private:
  Params m_params;
};

//------------------------------------------------------------------------------

/*
 * FilterDesign
 *
 * This container holds a filter Design (Gui-friendly layer) and
 * optionally combines it with the necessary state information to
 * process channel data.
 *
 */

// Factored to reduce template instantiations
template <class DesignClass>
class FilterDesignBase : public Filter
{
public:
  Kind getKind() const
  {
    return m_design.getKind();
  }

  const std::string getName() const
  {
    return m_design.getName();
  }
  
  const std::string getSlug() const
  {
    return m_design.getSlug();
  }
  
  const std::vector<ParamID> getParamIDs() const
  {
    return m_design.getParamIDs();
  }

  std::vector<PoleZeroPair> getPoleZeros() const
  {
    return m_design.getPoleZeros();
  }
 
  complex_t response(double normalizedFrequency) const
  {
    return m_design.response(normalizedFrequency);
  }

protected:
  void doSetParams(const Params& parameters)
  {
    m_design.setParams(parameters);
  }

protected:
  DesignClass m_design;
};



template <class DesignClass,
          int Channels = 0,
          class StateType = DirectFormII>
class FilterDesign : public FilterDesignBase <DesignClass>
{
public:
  FilterDesign()
  {
  }

  int getNumChannels()
  {
    return Channels;
  }

  void reset()
  {
    m_state.reset();
  }

  void process(int numSamples, float* data)
  {
    m_state.process (numSamples, data,
                     FilterDesignBase<DesignClass>::m_design);
  }

  void process(int numSamples, double* data)
  {
    m_state.process (numSamples, data,
                     FilterDesignBase<DesignClass>::m_design);
  }

protected:
  ChannelsState <Channels,
                 typename DesignClass::template State <StateType> > m_state;
};

//------------------------------------------------------------------------------

/*
 * This container combines a raw filter with state information
 * so it can process channels. In order to set up the filter you
 * must call a setup function directly. Smooth changes are
 * not supported, but this class has a smaller footprint.
 *
 */
template <class FilterClass,
          int Channels = 0,
          class StateType = DirectFormII>
class SimpleFilter : public FilterClass
{
public:
  int getNumChannels()
  {
    return Channels;
  }

  void reset()
  {
    m_state.reset();
  }

  template <typename Sample>
  void process(int numSamples, Sample* data)
  {
    m_state.process (numSamples, data, *((FilterClass*)this));
  }

protected:
  ChannelsState <Channels,
                 typename FilterClass::template State <StateType> > m_state;
};

}

#endif
