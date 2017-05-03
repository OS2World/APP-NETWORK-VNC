#
# This makefile for Open Watcom will be generate next files:
#
# VNCKBD.SYS
#

# BINPATH - binaries output path.
BINPATH = ..\bin

# Uncomment next line and execute WMAKE to create a debug version.
# DEBUGCODE = YES

# --- Signatures ---
AUTOR = Andrey Vasilkin
DRV_VERSION = 1.0.2
DRV_COMMENT = VNC keyboard support.


DRV_FILE = $(BINPATH)\vnckbd.sys

C16FLAGS = -i=$(%WATCOM)\h;$(%WATCOM)\h\os21x -bt=os2
# -ms -5 -omi -s -zdp -zff -zgf -zu -zl -zq -wx -d0
# C16FLAGS += -ms -d0 -5 -s -zu -zq -zl -omi -zdp -zff -zgf -wx
C16FLAGS +=  -ms -d0 -5 -s -zff -zgf -zu -zq -zl -zld -ox -zp -W4
WC16 = wcc $(C16FLAGS)

AFLAGS = -zq
ASM = wasm $(AFLAGS)

.SUFFIXES:
.SUFFIXES: .obj .c .asm

all:    .SYMBOLIC $(DRV_FILE)


#	VNCKBD.SYS
#	---------

DRVOBJS = devsegs.obj header.obj strategy.obj drvinit.obj

!ifdef DEBUGCODE
C16FLAGS += -dDEBUG_CODE
DRVOBJS += debug.obj
!endif

$(DRV_FILE) : $(DRVOBJS) vnckbd.lnk
  @wlink @vnckbd.lnk

vnckbd.lnk : .ALWAYS
  @%create $@
  @%append $@ name $(DRV_FILE)
  @%append $@ sys os2 dll initglobal
  @%append $@ option protmode
  @%append $@ option quiet
  @%append $@ option stack=0
  @%append $@ lib os2
  @for %f in ($(DRVOBJS)) do @%append $@ file %f
  @%append $@ segment type DATA SHARED PRELOAD
  @%append $@ segment '_TEXT' PRELOAD IOPL
  @%append $@ segment '_INITCODE' PRELOAD IOPL
!ifdef %unixroot
  @$(%unixroot)\usr\libexec\bin\date.exe +"OPTION DESCRIPTION '@$#$(AUTOR):$(DRV_VERSION)$#@$#$#1$#$# %F %T      $(%HOSTNAME)::ru:RUS:::@@$(DRV_COMMENT)'" >>$^@
!else ifdef %osdir
  @$(%osdir)\KLIBC\BIN\date.exe +"OPTION DESCRIPTION '@$#$(AUTOR):$(DRV_VERSION)$#@$#$#1$#$# %F %T      $(%HOSTNAME)::ru:RUS:::@@$(DRV_COMMENT)'" >>$^@
!else
  @%append $@ OPTION DESCRIPTION '@$#$(AUTOR):$(DRV_VERSION)$#@$#$#1$#$#                          $(%HOSTNAME)::ru:RUS:0::@@$(DRV_COMMENT)'
!endif

.c.obj: .AUTODEPEND
  @$(WC16) $*.c

.asm.obj: .AUTODEPEND
  @$(ASM) $*.asm

clean: .SYMBOLIC
  @if exist *.obj @del *.obj
  @if exist *.err @del *.err
  @if exist vnckbd.lnk @del vnckbd.lnk
#  @if exist $(DRV_FILE) @del $(DRV_FILE)
