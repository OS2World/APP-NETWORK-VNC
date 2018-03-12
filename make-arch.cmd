@echo off
rem
rem  This bath file will make all components, build warpin package and put in
rem  vnc-<date>.zip
rem  Then the entire project will be cleaned and the source files are packaged
rem  in vnc-src-<date>.zip
rem
rem  Optional arguments: 
rem    nopack - make binaries at .\bin without warpin package and archiving,
rem    packsrc - clean project and archive sources.
rem

rem -------------------------

rem Ouput warpin package file name. It will be placed in vnc-<date>.zip
set WPIFile=vnc.wpi

rem -------------------------

rem Query current date (variable %archdate%).
%unixroot%\usr\libexec\bin\date +"set archdate=-%%Y%%m%%d" >archdate.cmd
call archdate.cmd
del archdate.cmd

if .%1.==.packsrc. goto l01
if .%1.==.nopack. goto l03
if .%1.==.. goto l03

rem Unknown switch - print help and exit.

echo Switches:
echo   nopack  - make binaries at .\bin without warpin package and archives,
echo   packsrc - cleaning project and pack sources in vnc-src%archdate%.zip.
echo.
echo   Without switches: build all components, build warpin package and put in
echo   vnc%archdate%.zip. Then the entire project will be cleaned and the source
echo   files are packaged in vnc-src%archdate%.zip

EXIT

:l03
rem Build components in sources directories.

set components=libvncserver libgiconv vncpm os2xkey KbdXKey VNCServer VNCViewer
for %%i in (%components%) do @echo Make: %%i & make -s -C ./%%i

echo Make: driver vnckbd.sys
cd .\vnckbd
cmd /C make.cmd
cd..

echo Make: WPS Class
cd .\WPS
wmake all
cd..

if .%1.==.nopack. goto l02

echo.
echo Make warpin package...
echo.
cd .\warpin
call make.cmd %WPIFile%
echo Warpin package %WPIFile% was not created.
if exist %WPIFile% goto l00
EXIT
:l00
cd..


rem Make archives.

7za.exe a -tzip -mx7 -r0 -x!*.zip vnc%archdate% ./warpin/vnc.wpi

:l01
call clean.cmd

7za.exe a -tzip -mx7 -r0 -x!*.zip vnc-src%archdate%

:l02
echo Done.
EXIT
