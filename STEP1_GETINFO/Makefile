#
#change this makefile for your target...
#

ifeq "$(findstring QNX, $(shell uname -a))" "QNX"
    OS := DZY_QNX
    DEF_TYPES := __QNX__
else
    OS := DZY_LINUX
    DEF_TYPES := __LINUX__
endif

PHONY = clean
TARGET_NAME = getinfo

all: $(TARGET_NAME)

LIBPATH  = ../../bin

DIRS := ../../BRDINC ../../BRDINC/ctrladmpro ../../gipcy/include .

INC := $(addprefix  -I, $(DIRS))

CC = g++
LD = g++

CFLAGS = -g -Wall -D__IPC_LINUX__ -D$(OS) -D$(DEF_TYPES) $(INC)
LFLAGS = -Wl,-rpath $(LIBPATH) -L"$(LIBPATH)" -lbrd -lgipcy

$(TARGET_NAME): $(patsubst %.cpp,%.o, $(wildcard *.cpp))
	$(LD) -o $(TARGET_NAME) $(notdir $^) $(LFLAGS)

	rm -f *.o *~ core

%.o: %.cpp
	$(CC) $(CFLAGS) -c -MD $<
	
include $(wildcard *.d)

clean:
	rm -f *.o *~ core
	rm -f *.d *~ core
	rm -f $(TARGET_NAME)
	
distclean:
	rm -f *.o *~ core
	rm -f *.d *~ core
	rm -f $(TARGET_NAME)
