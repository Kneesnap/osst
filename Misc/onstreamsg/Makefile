HOST=LINUX
DEBUG=yes
PROFILE=no

CPP_PROJ=yes
EXTRAS=
HEADERS=
SRCS=onstreamsg.cpp
TARGET=osg

#==============================================================================
# End of customisable section of Makefile
#==============================================================================

CFLAGS=-fno-exceptions $(OPTFLAGS) $(WFLAGS) $(DEFS)
LFLAGS=$(LIBPATH)
CC=gcc
CPP=g++
FLEX=flex
YACC=bison
INCPATH=
LIBPATH=
LIBS=
DEFS=-D$(HOST)

# Autoconfiguration crap:

ifeq ($(DEBUG),yes)
DEFS+=-DDEBUG
OPTFLAGS=-g -O
WFLAGS=-Wall
ifeq ($(HOST),LINUX)
LFLAGS+=-g
endif
ifeq ($(HOST),SUNOS4)
WFLAGS+=-Wno-implicit -Wno-cast-qual
endif
ifdef ($(PROFILE),yes)
OPTFLAGS+=-pg
LFLAGS+=-pg
endif
else
OPTFLAGS=-O6
WFLAGS=
endif

ifeq ($(CPP_PROJ),yes)
OBJS=$(SRCS:.cpp=.o)
else
OBJS=$(SRCS:.c=.o)
endif
RCS=$(SRCS) $(HEADERS) $(EXTRAS)

.PHONY: all

%.o: %.cpp
	$(CPP) -g -c $(CFLAGS) $<

%.o: %.c
	$(CC) -g -c $(CFLAGS) $<

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LFLAGS) -o $@ $^ $(LIBS)

clean:
	-rm $(OBJS) $(TARGET)

in:
	-ci $(RCS)

out:
	-co -l $(RCS)

check:
	-ci -l $(RCS)

# Dependancies -- do NOT mess with anything past this point!
osg.o: onstreamsg.cpp
