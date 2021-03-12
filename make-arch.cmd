@echo off
rem
rem  This batch file will clean up the project and archive the sources to
rem  vnc-src-<date>.zip
rem  Then the project will be fully built, a WPI package will be created which
rem  will be zipped into the vnc-wpi-<date>.zip file.
rem
rem  Optional arguments: 
rem    nopack   - make binaries at .\bin without warpin package and archiving,
rem    packsrc  - clear project and archive sources,
rem    clean    - clear project.
rem


rem Checking the environment
rem ------------------------

if not .%unixroot%.==.. goto l00
echo Environment variable UNIXROOT is not set.
exit 1
:l00

set DateUtility=%unixroot%\usr\libexec\bin\date.exe
if exist %DateUtility% goto l01
set DateUtility=%unixroot%\usr\bin\date.exe
if exist %DateUtility% goto l01
set DateUtility=%OSDIR%\bin\date.exe
if exist %DateUtility% goto l01
echo %unixroot%\usr\libexec\bin\date.exe utility not found.
exit 1
:l01

cmd /C rm.exe --help >nul
if not errorlevel 1041 goto l02
echo rm.exe utility not found.
exit 1
:l02

set MakeUtility=%unixroot%\usr\bin\make.exe
if exist %MakeUtility% goto l03
echo %MakeUtility% utility not found.
exit 1
:l03

cmd /C lxlite.exe /H >nul
if not errorlevel 1041 goto l04
echo lxlite.exe utility not found.
exit 1
:l04

cmd /C highmem.exe -? >nul
if not errorlevel 1041 goto l05
echo highmem.exe utility not found.
exit 1
:l05

if not .%watcom%.==.. goto l06
echo Environment variable WATCOM is not set. Do you have Open Watcom compiler installed?
exit 1
:l06

cmd /C 7za.exe >nul
if not errorlevel 1041 goto l07
echo 7za.exe archiver not found.
exit 1
:l07


set WPIFile=vnc.wpi

rem Cleaning
rem --------

rem Cleaning source directories
set components=libvncserver libgiconv os2xkey KbdXKey VNCServer VNCViewer vnckbd WPS
for %%i in (%components%) do cd %%i & cmd /c make clean & cd ..

rem Remove files from ./bin
cd .\bin
rm -f *.dbg *.log *.!!! os2xkey.dll vncserver.exe vnckbd.sys
rm -f vncviewer.exe kbdxkey.exe VNCSERVER.INI VNCVIEWER.INI
cd..

rem Remove warpin package
rm -f ./warpin/%WPIFile%

if .%1.==.clean. goto _exit



rem Make archive file names
rem -----------------------

rem Query current date (variable %archdate%).
%DateUtility% +"set archdate=-%%Y%%m%%d" >archdate.cmd
call archdate.cmd
rm -f archdate.cmd

set archiveSrc=vnc-src%archdate%
set archiveWPI=vnc-wpi%archdate%


rem Pack source codes
rem -----------------

if .%1.==.nopack. goto _endPackSrc

rm -f %archiveSrc%.zip
7za.exe a -tzip -mx7 -r0 -x!*.zip %archiveSrc%
if errorlevel 1 goto _exit

if .%1.==.packsrc. goto _exit

:_endPackSrc


rem Building a project
rem ------------------

set components=libvncserver libgiconv os2xkey KbdXKey VNCServer VNCViewer
for %%i in (%components%) do @echo Make: %%i & make -s -C ./%%i & if errorlevel 1 goto _exit

echo Make: driver vnckbd.sys
cd .\vnckbd
wmake -h
if errorlevel 1 goto _exit
cd..

echo Make: WPS Class
cd .\WPS
wmake -h all
if errorlevel 1 goto _exit
cd..


rem Make WarpIn package
rem -------------------

if .%1.==.nopack. goto _endPackWPI

cd .\WarpIn
call make.cmd %WPIFile%
if not exist %WPIFile% goto _exit
cd..

rem Pack WPI
rm -f %archiveWPI%.zip
7za.exe a -tzip -mx7 -r0 -x!*.zip %archiveWPI% ./WarpIn/%WPIFile% ./bin/README
if errorlevel 1 goto _exit

:_endPackWPI


:_exit
EXIT
