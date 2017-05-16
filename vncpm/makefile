#
# GCC makefile
#

# Name for binary (without path and extension)
NAME = vncpm

# Binary file extension (dll/exe)
EXT = dll

VERSION = 0.1.0
DESCRIPTION = $(NAME) module for VNC server and viewer

# Uncomment next line to make debug version
# DEBUGCODE = YES

# Sources
SRCS = chatwin.c prbar.c pmhelpers.c

# Libraries (-Lpath -lname ...)
LIBS =

CFLAGS = -DVNCPMDLLNAME=\"$(NAME).dll\"

include ../common/makefile.begin

# --------------------

$(FILE_BIN): msgCompilation $(OBJS) $(FILE_DEF) $(FILE_RES) msgBuilding
	$(CC) $(FILE_DEF) $(LDFLAGS)
	$(RC) $(RCLFLAGS)
	$(LXLITE)

$(FILE_DEF):
	@cmd /c %unixroot%\\usr\\libexec\\bin\\date +"DESCRIPTION '@#Andrey Vasilkin:$(VERSION)#@##1## %F               %HOSTNAME%::::::@@$(DESCRIPTION)'" >$@
	@echo EXPORTS >>$@
	@echo   chatwinCreate >>$@
	@echo   prbarRegisterClass >>$@
	@echo   cnrhScrollToRecord >>$@

# --------------------

include ../common/makefile.end
