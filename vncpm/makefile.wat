#
# OpenWatcom makefile
#

NAME = vncpm
VERSION = 1.0.0
# DEBUGCODE = YES

FILE_DLL = $(NAME).dll
FILE_LIB = $(NAME).a
FILE_LNK = $(NAME).lnk
FILE_RC  = $(NAME).rc
FILE_RES = $(NAME).res
DESCRIPTION = $(NAME) module for VNC server and viewer

CC = wcc386
RC = wrc
AR = ar

SRCS = chatwin.c prbar.c

INCPATH = $(%WATCOM)\H\OS2;$(%WATCOM)\H;D:\dev\OS2TK45\som\include;
INCPATH +=D:\dev\OS2TK45\h;

CFLAGS = -zq -bd -sg -I -i=$(INCPATH) -d0 -w2 -DDLLNAME="$(FILE_DLL)"

!ifdef DEBUGCODE
SRCS += debug.c
CFLAGS += -DDEBUG_FILE="$(NAME).dbg"
DESCRIPTION += (debug)
!endif

OBJS = $(SRCS:.c=.obj)

all: $(FILE_LIB)

$(FILE_LIB): $(FILE_DLL)
	@emximp -o $@ $(FILE_DLL)

$(FILE_DLL): $(OBJS) $(FILE_LNK) $(FILE_RES)
        wlink @$(FILE_LNK)
        $(RC) $(FILE_RES) $@

.c.obj:
    $(CC) $(CFLAGS) $[@

$(FILE_LNK):
  @%create $@
  @%append $@ NAME $(FILE_DLL)
  @%append $@ SYSTEM os2v2_dll INITINSTANCE TERMINSTANCE
  @%append $@ SEGMENT data nonshared loadoncall
  @%append $@ OPTION manyautodata
  @%append $@ OPTION ELIMINATE
  @%append $@ OPTION QUIET
  @%append $@ OPTION OSNAME='OS/2 and eComStation'
!ifdef %unixroot
  @$(%unixroot)\usr\libexec\bin\date.exe +"OPTION DESCRIPTION '@$#$(AUTOR):$(VERSION)$#@$#$#1$#$# %F %T      $(%HOSTNAME)::ru:RUS:::@@$(COMMENT)'" >>$^@
!else
  @%append $@ OPTION DESCRIPTION '@$#$(AUTOR):$(VERSION)$#@$#$#1$#$#                          $(%HOSTNAME)::ru:RUS:0::@@$(COMMENT)'
!endif
!ifdef LIBPATH
  @%append $@ LIBPATH $(LIBPATH)
!endif
!ifdef LIBS
  @for %i in ($(LIBS)) do @%append $@ LIB %i
!endif
  @for %i in ($(OBJS)) do @%append $@ FILE %i
  @%append $@ EXPORT cwCreate

$(FILE_RES): $(FILE_RC) resource.h
  $(RC) -r -q -i=$(INCPATH) $[*

clean: .SYMBOLIC
  @rm -f $(OBJS) $(FILE_DEF) $(FILE_LIB) $(FILE_DLL) $(FILE_RES) $(FILE_LNK) $(NAME).dbg
