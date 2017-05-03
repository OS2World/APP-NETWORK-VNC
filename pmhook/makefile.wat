#
# Open Watcom make file
#

AUTOR = Andrey Vasilkin
VERSION = 1.0.0
DEBUGCODE = YES
COMMENT = Presentation Manager hook library
MSGPREF = [pmhook]

DLLFILE = vncshook.dll
LNKFILE = vncshook.lnk

INCPATH = $(%WATCOM)\H\OS2;$(%WATCOM)\H;D:\dev\OS2TK45\h;

LIBPATH = %WATCOM%\lib386;%WATCOM%\lib386\os2
LIBS = 

SRCS = vncshook.c

CFLAGS = -zq -bd -I -i=$(INCPATH) -d0 -w2 -s

!ifdef DEBUGCODE
CFLAGS += -DDEBUG_FILE="$(DBGFILE)"
SRCS += debug.c
COMMENT += (debug)
!endif

OBJS = $(SRCS:.c=.obj)

.SILENT

all: $(DLLFILE)

$(DLLFILE): infCompiling $(OBJS) $(LNKFILE)
  @echo $(MSGPREF) Linking: $@
  wlink @$(LNKFILE)

.c.obj:
  wcc386 $(CFLAGS) $[@

$(LNKFILE): .ALWAYS
  @echo $(MSGPREF) Creating file: $@
  @%create $@
  @%append $@ NAME $(DLLFILE)
  @%append $@ SYSTEM os2v2_dll INITGLOBAL TERMGLOBAL
  @%append $@ SEGMENT data nonshared
  @%append $@ OPTION QUIET
  @%append $@ OPTION ONEAUTODATA
  @%append $@ OPTION ELIMINATE
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

#  @%append $@ EXPORT      ER_SetHooksProps
#  @%append $@ EXPORT      ER_mouse_event

infCompiling: .SYMBOLIC
  @echo $(MSGPREF) Compiling: $(COMMENT) $(VERSION) ...

clean: .symbolic
  @echo $(MSGPREF) Cleaning up
  if exist $(LNKFILE) del $(LNKFILE)
  if exist $(DLLFILE) del $(DLLFILE)
  if exist *.obj del *.obj
