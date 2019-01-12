#
# options
#

DEBUG   = -d1
MACHINE = -3r
OPT     = -oneilr

#
# implicit compilation rule
#

.cpp.obj:
	wpp386 -zc -fpc $(MACHINE) $(OPT) -mf $(DEBUG) -zq $*.cpp

#
# header dependencies
#

HDRS    =  utils.h  gs.h  ts.h  common.h

OBJS    =  vgatest.obj gs.obj ts.obj utils.obj

all: vgatest.exe

vgatest.exe : $(OBJS)
	wcl386 -k128k -l=dos4g -mf $(DEBUG) $(OBJS)

tt.obj:      ts.cpp      $(HDRS)
tg.obj:      gs.cpp      $(HDRS)
vgatest.obj: vgatest.cpp $(HDRS)
utils.obj:   utils.cpp   $(HDRS)

#
# clean up 
#
clean:
	-rm *.bak
	-rm *.obj
	-rm *.exe
	-rm err
	-rm j


