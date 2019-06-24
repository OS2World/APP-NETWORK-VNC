#
# GCC makefile
#

# Name for binary (without path and extension)
NAME = vncviewer

# Uncomment next line to make debug version
# DEBUGCODE = YES

# Binary file extension (dll/exe)
EXT = exe

VERSION = 0.1.8
DESCRIPTION = VNC Viewer

# Compilation flags for ../common/utils.c
UTILS_CFLAGS = -DUTILS_WITH_OPENSSL

# Sources
SRCS = main.c lnchpad.c clntconn.c progress.c pswddlg.c clntwnd.c \
       optionsdlg.c tipwin.c filexfer.c fxdlg.c csconv.c utils.c

# Libraries (-Lpath -lname ...)
LIBS = -L../libvncserver -L../os2xkey -L../libgiconv -los2xkey \
       -L../vncpm -lvncpm -llibgiconv \
       -lvncclient -llibcrypto -llibssl -lz -ljpeg

CFLAGS = -DLIBVNCSERVER_HAVE_LIBSSL -DLIBVNCSERVER_HAVE_LIBZ \
         -DLIBVNCSERVER_HAVE_LIBJPEG

include ../common/makefile.begin


$(FILE_BIN): msgCompilation $(OBJS) $(FILE_DEF) $(FILE_RES)  ../libvncserver/vncclient.a msgBuilding
	$(CC) $(FILE_DEF) $(LDFLAGS)
	$(LXLITE)
	$(RC) $(RCLFLAGS)

$(FILE_DEF):
ifneq ($(DEBUGCODE),YES)
	@echo NAME $(NAME) windowapi >$@
endif
	@cmd /c %unixroot%\\usr\\libexec\\bin\\date +"DESCRIPTION '@#Andrey Vasilkin:$(VERSION)#@##1## %F               %HOSTNAME%::::::@@$(DESCRIPTION)'" >>$@


include ../common/makefile.end
