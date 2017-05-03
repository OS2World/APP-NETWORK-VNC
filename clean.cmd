@echo off

rem  This bath file will clean entire project.


rem Cleaning in sources directories.

set components=libvncserver libgiconv vncpm os2xkey KbdXKey VNCServer VNCViewer vnckbd
for %%i in (%components%) do echo Cleaning: %%i & cd %%i & cmd /c make clean & cd ..


rem Cleaning in binaries directory.

set in_bin=*.dbg *.log *.!!! vncpm.dll os2xkey.dll vncserver.exe
set in_bin=%in_bin% vncviewer.exe kbdxkey.exe VNCSERVER.INI VNCVIEWER.INI


echo Remove files from ./bin
cd .\bin
rm -f %in_bin%
cd..
echo Remove warpin package
rm -f ./warpin/vnc.wpi
