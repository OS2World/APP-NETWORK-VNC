#
# GCC makefile
#

# Name for binary (without path and extension)
NAME = vncserver

# Uncomment next line to make debug version
# DEBUGCODE = YES

# Binary file extension (dll/exe)
EXT = exe

VERSION = 0.1.5
DESCRIPTION = VNC Server

# Compilation flags for ../common/utils.c
UTILS_CFLAGS = -DUTILS_WITH_OPENSSL

# Sources
SRCS = main.c rfbsrv.c config.c gui.c configdlg.c avdlg.c log.c clientsdlg.c \
       utils.c

# Libraries (-Lpath -lname ...)
LIBS = -L../libvncserver -L../os2xkey -los2xkey -L../vncpm \
       -lvncpm -lvncserver -llibcrypto -llibssl -lz -ljpeg -lpng1616_dll

CFLAGS = -DVERSION=\"$(VERSION)\"

include ../common/makefile.begin


$(FILE_BIN): msgCompilation $(OBJS) $(FILE_DEF) $(FILE_RES) msgBuilding
	$(CC) $(FILE_DEF) $(LDFLAGS)
	$(RC) $(RCLFLAGS)
	$(LXLITE)

$(FILE_DEF):
ifneq ($(DEBUGCODE),YES)
	@echo NAME $(NAME) windowapi >$@
endif
	@cmd /c %unixroot%\\usr\\libexec\\bin\\date +"DESCRIPTION '@#Andrey Vasilkin:$(VERSION)#@##1## %F               %HOSTNAME%::::::@@$(DESCRIPTION)'" >>$@


include ../common/makefile.end
