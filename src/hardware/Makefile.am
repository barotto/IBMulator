AM_CXXFLAGS = @CXX_DEFAULTS@ @BASECFLAGS@ -I$(top_srcdir)/src
	
noinst_LIBRARIES = libhardware.a
libhardware_a_SOURCES = \
	cpu.cpp \
	cpu/core.cpp \
	cpu/bus.cpp \
	cpu/decoder/cycles.cpp \
	cpu/decoder/prefix_none.cpp \
	cpu/decoder/prefix_0F.cpp \
	cpu/decoder.cpp \
	cpu/executor.cpp \
	cpu/logger.cpp \
	cpu/disasm.cpp \
	cpu/debugger.cpp \
	memory.cpp \
	iodevice.cpp \
	devices.cpp \
	devices/systemboard.cpp \
	devices/keyboard.cpp \
	devices/scancodes.cpp \
	devices/pic.cpp \
	devices/cmos.cpp \
	devices/dma.cpp \
	devices/pit.cpp \
	devices/pit82c54.cpp \
	devices/vga.cpp \
	devices/vgadisplay.cpp \
	devices/drivefx.cpp \
	devices/floppy.cpp \
	devices/floppyfx.cpp \
	devices/harddrv.cpp \
	devices/harddrvfx.cpp \
	devices/mediaimage.cpp \
	devices/vvfat.cpp \
	devices/serial.cpp \
	devices/parallel.cpp \
	devices/pcspeaker.cpp \
	devices/ps1audio.cpp \
	devices/sn76496.cpp \
	devices/gameport.cpp \
	devices/opl.cpp \
	devices/adlib.cpp

noinst_HEADERS = \
	cpu.h \
	cpu/state.h \
	cpu/core.h \
	cpu/bus.h \
	cpu/decoder.h \
	cpu/descriptor.h \
	cpu/selector.h \
	cpu/executor.h \
	cpu/logger.h \
	cpu/disasm.h \
	cpu/debugger.h \
	memory.h \
	iodevice.h \
	devices.h \
	devices/systemboard.h \
	devices/keyboard.h \
	devices/scancodes.h \
	devices/pic.h \
	devices/cmos.h \
	devices/dma.h \
	devices/pit.h \
	devices/pit82c54.h \
	devices/vga.h \
	devices/vgadisplay.h \
	devices/drivefx.h \
	devices/floppy.h \
	devices/floppyfx.h \
	devices/harddrv.h \
	devices/harddrvfx.h \
	devices/hddparams.h \
	devices/mediaimage.h \
	devices/vvfat.h \
	devices/serial.h \
	devices/parallel.h \
	devices/pcspeaker.h \
	devices/ps1audio.h \
	devices/sn76496.h \
	devices/gameport.h \
	devices/opl.h \
	devices/adlib.h