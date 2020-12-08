AM_CXXFLAGS = @CXX_DEFAULTS@ @BASECFLAGS@ -I$(top_srcdir)/src
	
noinst_LIBRARIES = libaudio.a
libaudio_a_SOURCES = \
	audiobuffer.cpp \
	audiospec.cpp \
	convert.cpp \
	midi.cpp \
	mididev.cpp \
	mididev_alsa.cpp \
	mididev_win32.cpp \
	mixerchannel.cpp \
	ring_buffer.cpp \
	soundfx.cpp \
	synth.cpp \
	vgm.cpp \
	wav.cpp \
	dsp/Biquad.cpp \
	dsp/Butterworth.cpp \
	dsp/Cascade.cpp \
	dsp/Design.cpp \
	dsp/Filter.cpp \
	dsp/Param.cpp \
	dsp/PoleFilter.cpp \
	dsp/RootFinder.cpp \
	dsp/State.cpp

noinst_HEADERS = \
	audiobuffer.h \
	audiospec.h \
	convert.h \
	midi.h \
	mididev.h \
	mididev_alsa.h \
	mixerchannel.h \
	ring_buffer.h \
	soundfx.h \
	synth.h \
	vgm.h \
	wav.h \
	dsp/Biquad.h \
	dsp/Butterworth.h \
	dsp/Cascade.h \
	dsp/Common.h \
	dsp/Design.h \
	dsp/Dsp.h \
	dsp/Filter.h \
	dsp/Layout.h \
	dsp/MathSupplement.h \
	dsp/Params.h \
	dsp/PoleFilter.h \
	dsp/RootFinder.h \
	dsp/State.h \
	dsp/Types.h \
	dsp/Utilities.h
