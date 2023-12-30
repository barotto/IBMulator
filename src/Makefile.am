AM_CXXFLAGS = @CXX_DEFAULTS@ -I$(top_srcdir)/src @BASECFLAGS@ -DDATA_PATH=\"$(pkgdatadir)\" 

SUBDIRS = audio hardware gui

bin_PROGRAMS = ibmulator

ibmulator_SOURCES = \
	appconfig.cpp \
	bench.cpp \
	chrono.cpp \
	timers.cpp \
	filesys.cpp \
	fatreader.cpp \
	pacer.cpp \
	hwbench.cpp \
	ini.cpp \
	ini/ini.cpp \
	machine.cpp \
	main.cpp \
	md5.cpp \
	mixer.cpp \
	model.cpp \
	program.cpp \
	riff.cpp \
	ring_buffer.cpp \
	state_record.cpp \
	statebuf.cpp \
	syslog.cpp \
	utils.cpp \
	zip.cpp \
	miniz/miniz.c \
	tinyfiledialogs/tinyfiledialogs.c \
	wincompat/mkstemp.cpp \
	wincompat/realpath.cpp \
	wincompat/asprintf.cpp \
	wincompat/utf8.cpp \
	stb/stb.cpp \
	lodepng/lodepng.cpp
	
noinst_HEADERS = \
	appconfig.h \
	bench.h \
	chrono.h \
	timers.h \
	circular_fifo.h \
	filesys.h \
	fatreader.h \
	pacer.h \
	hwbench.h \
	ibmulator.h \
	interval_tree.h \
	ini.h \
	ini/ini.h \
	keys.h \
	machine.h \
	md5.h \
	mixer.h \
	model.h \
	program.h \
	riff.h \
	ring_buffer.h \
	shared_queue.h \
	shared_deque.h \
	shared_fifo.h \
	state_record.h \
	statebuf.h \
	syslog.h \
	utils.h \
	zip.h \
	miniz/miniz.h \
	tinyfiledialogs/tinyfiledialogs.h \
	wincompat.h \
	wincompat/utf8.h \
	stb/stb.h \
	stb/stb_image.h \
	stb/stb_image_write.h \
	lodepng/lodepng.h
	
ibmulator_LDFLAGS = -Wl,-rpath='$$ORIGIN/../lib'
ibmulator_LDADD = gui/libgui.a hardware/libhardware.a audio/libaudio.a $(BASELIBS)


