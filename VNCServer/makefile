#
# This makefile for GCC will be generate next files:
#
# ..\bin\vncserver.exe
#

# Name for binary (without path and extension)
NAME = vncserver

# Uncomment next line to make debug version
# DEBUGCODE = YES

# Binary file extension (dll/exe)
EXT = exe

VERSION = 0.1.7
DESCRIPTION = VNC Server

# Compilation flags for ../common/utils.c
UTILS_CFLAGS = -DUTILS_WITH_OPENSSL

# Sources
SRCS = main.c rfbsrv.c config.c gui.c configdlg.c avdlg.c log.c clientsdlg.c \
       utils.c ..\common\chatwin.c

# Libraries (-Lpath -lname ...)
LIBS = -L../libvncserver -L../os2xkey -los2xkey -lvncserver -llibcrypto \
       -llibssl -lz -ljpeg -lpng1616_dll

CFLAGS = -DVERSION=\"$(VERSION)\"

include ../common/makefile.begin


$(FILE_BIN): msgCompilation $(OBJS) $(FILE_DEF) $(FILE_RES) ../libvncserver/vncserver.a msgBuilding
	$(CC) $(FILE_DEF) $(LDFLAGS)
	$(LXLITE)
	$(RC) $(RCLFLAGS)

$(FILE_DEF):
ifneq ($(DEBUGCODE),YES)
	@echo NAME $(NAME) windowapi >$@
endif
	@cmd /c %unixroot%\\usr\\libexec\\bin\\date +"DESCRIPTION '@#Andrey Vasilkin:$(VERSION)#@##1## %F               %HOSTNAME%::::::@@$(DESCRIPTION)'" >>$@


include ../common/makefile.end
