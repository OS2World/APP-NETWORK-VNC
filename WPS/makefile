#
# Open Watcom make file
#
# Usage:
#   wmake all       - builds the WPS class DLL
#   wmake install   - builds the WPS class DLL, installs it
#   wmake deinstall - deinstalls the WPS class DLL
#   wmake clean     - executes deinstall and removes compiled files

AUTOR = Andrey Vasilkin
VERSION = 1.0.2
COMMENT = VNC Viewer WPS Class
MSGPREF = [WPS Class]

DLLFILE = ..\bin\vncv.dll
LNKFILE = vncv.lnk

# Uncomment next line to make debug version
# DEBUGCODE = YES


INCPATH = ..\common;$(%WATCOM)\H\OS2;$(%WATCOM)\H;D:\dev\OS2TK45\som\include;
INCPATH +=D:\dev\OS2TK45\h

LIBPATH = %WATCOM%\lib386;%WATCOM%\lib386\os2
LIBS = somtk

SRCS = vncv.c wpsutils.c nbpage.c start.c utils.c

CFLAGS = -zq -bd -sg -I -i=$(INCPATH) -d0 -w2

SCFLAGS = -maddstar

!ifdef DEBUGCODE
SRCS += debug.c
CFLAGS += -DDEBUG_FILE="c:\\vncv.dbg"
!else
.SILENT
!endif

OBJS = $(SRCS:.c=.obj)

.EXTENSIONS : .ih .idl .res .rc .dlg .hlp .ipf

help: .symbolic
    @echo $(MSGPREF) The following pseudotargets are supported:
    @echo.
    @echo  all       - builds the WPS class DLL
    @echo  install   - builds the WPS class DLL, installs it
    @echo  deinstall - deinstalls the WPS class DLL
    @echo  clean     - executes deinstall and removes compiled files

all: $(DLLFILE) .symbolic

install: all .symbolic
    -@call install.cmd I

deinstall: .symbolic
    -@call install.cmd D


$(DLLFILE): infCompiling $(OBJS) vncv.lnk vncv.res
  @echo $(MSGPREF) Linking: $@
  wlink @$(LNKFILE)
  wrc -q vncv.res $@

vncv.res: vncv.rc vncvrc.h

vncv.obj: vncv.c vncv.ih vncv.h vncvrc.h

vncv.ih vncv.h vncv.c: vncv.idl

.c.obj:
  @wcc386 $(CFLAGS) $[@

utils.obj: ..\common\utils.c
  @wcc386 $(CFLAGS) ..\common\utils.c

debug.obj: ..\common\debug.c
  @wcc386 $(CFLAGS) ..\common\debug.c

.idl.ih:
  @sc -s"ih" $(SCFLAGS) $<

.idl.h:
  @sc -s"h" $(SCFLAGS) $< 

.idl.c:
  @sc -s"c" $(SCFLAGS) $< 

.rc.res:
  @echo $(MSGPREF) Compiling resource: $@
  @wrc -r -q -i=$(INCPATH) $[*

.ipf.hlp:
  @echo $(MSGPREF) Compiling help: $@
  ipfc $< $*.hlp /W3 >nul

$(LNKFILE): .ALWAYS
  @echo $(MSGPREF) Creating file: $@
  @%create $@
  @%append $@ NAME $(DLLFILE)
  @%append $@ SYSTEM os2v2_dll INITINSTANCE TERMINSTANCE
  @%append $@ SEGMENT data nonshared loadoncall
  @%append $@ OPTION quiet,alignment=4
  @%append $@ OPTION manyautodata
  @%append $@ OPTION ELIMINATE
  @%append $@ OPTION QUIET
  @%append $@ OPTION OSNAME='OS/2 and eComStation'
!ifdef %unixroot
  @$(%unixroot)\usr\libexec\bin\date.exe +"OPTION DESCRIPTION '@$#$(AUTOR):$(VERSION)$#@$#$#1$#$# %F %T      $(%HOSTNAME)::ru:RU:::@@$(COMMENT)'" >>$^@
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
  @%append $@ EXPORT vncvCClassData
  @%append $@ EXPORT vncvClassData
  @%append $@ EXPORT vncvNewClass
  @%append $@ EXPORT M_vncvCClassData
  @%append $@ EXPORT M_vncvClassData
  @%append $@ EXPORT M_vncvNewClass

infCompiling: .SYMBOLIC
  @echo $(MSGPREF) Compiling: $(COMMENT) $(VERSION) ...

clean: .symbolic
  @echo $(MSGPREF) Cleaning up
  if exist vncv.hlp del vncv.hlp
  @rm -f *.obj vncv.h *.ih *.err *.res
  if exist $(LNKFILE) del $(LNKFILE)
  if exist $(DLLFILE) del $(DLLFILE)
