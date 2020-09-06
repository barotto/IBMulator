AM_CXXFLAGS = @CXX_DEFAULTS@ @BASECFLAGS@ -I$(top_srcdir)/src -DDATA_PATH=\"$(pkgdatadir)\" 

SUBDIRS = audio hardware gui

bin_PROGRAMS = ibmulator

ibmulator_SOURCES = \
	appconfig.cpp \
	bench.cpp \
	chrono.cpp \
	timers.cpp \
	filesys.cpp \
	pacer.cpp \
	hwbench.cpp \
	ini/ini.cpp \
	machine.cpp \
	main.cpp \
	md5.cpp \
	mixer.cpp \
	model.cpp \
	program.cpp \
	riff.cpp \
	statebuf.cpp \
	syslog.cpp \
	utils.cpp \
	tinyfiledialogs/tinyfiledialogs.c \
	wincompat/mkstemp.cpp \
	wincompat/realpath.cpp \
	wincompat/asprintf.cpp
	
noinst_HEADERS = \
	appconfig.h \
	bench.h \
	chrono.h \
	timers.h \
	circular_fifo.h \
	filesys.h \
	pacer.h \
	hwbench.h \
	ibmulator.h \
	interval_tree.h \
	ini/ini.h \
	keys.h \
	machine.h \
	md5.h \
	mixer.h \
	model.h \
	program.h \
	riff.h \
	shared_queue.h \
	shared_deque.h \
	statebuf.h \
	syslog.h \
	utils.h \
	tinyfiledialogs/tinyfiledialogs.h \
	wincompat.h
	
ibmulator_LDFLAGS = -Wl,-rpath='$$ORIGIN/../lib'
ibmulator_LDADD = gui/libgui.a hardware/libhardware.a audio/libaudio.a $(BASELIBS) ibmulator.res


