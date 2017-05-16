#
# GCC makefile
#

NAME = vncshook
VERSION = 1.0.0
DLLFILE = $(NAME).dll
# DEBUGCODE = YES

DESCRIPTION = $(NAME) $(VERSION)
DEFFILE = $(NAME).def

CC = @gcc
RC = @rc

SRCS = vncshook.c

LIBS = 

CFLAGS = $(LIBS) -Wall -idirafter /@unixroot/usr/include/os2tk45 -I. \
         -DUSE_OS2_TOOLKIT_HEADERS -DDLLNAME=\"$(NAME)\" -Wno-format \
         -c -O3 -Zmtd -Zomf -nostdlib -los2

ifeq ($(DEBUGCODE),YES)
SRCS += debug.c
CFLAGS += -DDEBUG_FILE=\"$(NAME).dbg\"
DESCRIPTION += (debug)
endif

OBJS = $(SRCS:.c=.o)

all: $(DLLFILE)

$(DLLFILE): $(OBJS) $(DEFFILE)
	$(CC) $(DEFFILE) -Zdll -Zomf -Zmtd $(OBJS) $(LIBS)
#$@
#	@lxlite /CS $@ >nul

.c.obj: .AUTODEPEND
	$(CC) $(CFLAGS) -c $<

$(DEFFILE):
	@echo LIBRARY $(NAME) INITGLOBAL TERMGLOBAL >$@
	@echo EXETYPE OS2 >>$@
	@%unixroot%\usr\libexec\bin\date +"DESCRIPTION '@#Andrey Vasilkin:$(VERSION)#@##1## %F               %HOSTNAME%::::::@@$(DESCRIPTION)'" >>$@
	@echo CODE LOADONCALL EXECUTEREAD NOIOPL NONCONFORMING >>$@
	@echo DATA LOADONCALL READWRITE NOIOPL SINGLE SHARED >>$@
#	@echo EXPORTS >>$@
#	@echo   pmhInputHookProc >>$@
#	@echo   pmhInit >>$@
#	@echo   pmhDone >>$@


clean:
	@rm -f $(OBJS) $(DEFFILE) $(DLLFILE) $(NAME).dbg
