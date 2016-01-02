AM_CXXFLAGS = @CXX_DEFAULTS@ @BASECFLAGS@ -I$(top_srcdir)/src -DDATA_PATH=\"$(pkgdatadir)\" 

SUBDIRS = hardware gui

bin_PROGRAMS = ibmulator

ibmulator_SOURCES = \
	main.cpp \
	filesys.cpp \
	appconfig.cpp \
	program.cpp \
	machine.cpp \
	statebuf.cpp \
	chrono.cpp \
	bench.cpp \
	hwbench.cpp \
	syslog.cpp \
	ini/ini.cpp \
	md5.cpp \
	mixer.cpp \
	vgm.cpp \
	wav.cpp \
	audio/mixerchannel.cpp \
	ring_buffer.cpp \
	wincompat/mkstemp.cpp \
	wincompat/realpath.cpp \
	utils.cpp
	
noinst_HEADERS = \
	ibmulator.h \
	filesys.h \
	appconfig.h \
	bench.h \
	chrono.h \
	circular_fifo.h \
	hwbench.h \
	interval_tree.h \
	keys.h \
	machine.h \
	md5.h \
	mixer.h \
	program.h \
	ring_buffer.h \
	shared_queue.h \
	statebuf.h \
	syslog.h \
	ini/ini.h \
	vgm.h \
	wav.h \
	audio/mixerchannel.h \
	wincompat.h \
	utils.h
	
ibmulator_LDFLAGS = -Wl,-rpath='$$ORIGIN/../lib'
ibmulator_LDADD = gui/libgui.a hardware/libhardware.a $(BASELIBS) ibmulator.res


