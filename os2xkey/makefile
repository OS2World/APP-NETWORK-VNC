#
# GCC makefile
#

# Name for binary (without path and extension)
NAME = os2xkey

# Binary file extension (dll/exe)
EXT = dll

VERSION = 1.0.0
DESCRIPTION = $(NAME) module for VNC server and viewer

# Uncomment next line to make debug version
# DEBUGCODE = YES

# Sources
SRCS = os2xkey.c utils.c

# Libraries (-Lpath -lname ...)
LIBS = -L../libgiconv -llibgiconv

include ../common/makefile.begin


$(FILE_BIN): msgCompilation $(OBJS) $(FILE_DEF) msgBuilding
	$(CC) $(FILE_DEF) $(LDFLAGS)
	$(LXLITE)

$(FILE_DEF):
	@%unixroot%\usr\libexec\bin\date +"DESCRIPTION '@#Andrey Vasilkin:$(VERSION)#@##1## %F               %HOSTNAME%::::::@@$(DESCRIPTION)'" >$@
	@echo EXPORTS >>$@
	@echo   xkCompMP >>$@
	@echo   xkKeysymFromMPAuto >>$@
	@echo   xkKeysymFromMP >>$@
	@echo   xkKeysymFromMPCheck >>$@
	@echo   xkMPFromKeysymStart >>$@
	@echo   xkMPFromKeysym >>$@
	@echo   xkMakeMPForVIO >>$@
	@echo   xkMapNew >>$@
	@echo   xkMapFree >>$@
	@echo   xkMapInsert >>$@
	@echo   xkMapLoad >>$@
	@echo   xkEventToStr >>$@
	@echo   xkMapSave >>$@


include ../common/makefile.end
