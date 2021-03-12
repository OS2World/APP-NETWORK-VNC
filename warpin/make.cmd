/*
	Building installation package for WARPIN.
*/

/* Optional argument - output WPI file name. */
fileWPIOutput = arg(1)
if fileWPIOutput = "" then
  fileWPIOutput = "vnc.wpi"

fileScriptInput = "script.inp"
fileScriptOutput = "script.wis"


if RxFuncQuery('SysLoadFuncs') then
do
  call RxFuncAdd 'SysLoadFuncs', 'RexxUtil', 'SysLoadFuncs'
  call SysLoadFuncs
end


/* Query WarpIn path. */
warpinPath = strip( SysIni( "USER", "WarpIN", "Path" ), "T", "00"x )
if warpinPath = "ERROR:" | warpinPath = "" then
do
  say "WarpIN is not installed correctly"
  exit 1
end


/* Make the script directory current. */
parse source os cmd scriptPath
savePath = directory()
scriptPath = left( scriptPath, lastpos( "\", scriptPath ) - 1 )
call directory scriptPath


/* Substitution versions of the components (from BLDLEVEL signatures) in   */
/* the script. Reads file fileScriptInput and makes file fileScriptOutput  */
/* Replaces all switches "<!-- BL:D:\path\program.exe -->" with version of */
/* D:\path\program.exe (bldlevel signature uses).                          */
if makeScript( fileScriptInput, fileScriptOutput ) = 0 then
  exit 1


/* Building installation package. */

call SysFileDelete fileWPIOutput

"set beginlibpath=" || warpinPath

warpinPath || "\WIC.EXE " || fileWPIOutput || " -a " || ,
"1 -c..\bin vncviewer.exe vncv.dll " || ,
"2 -c..\bin vncserver.exe vncshook.dll -r webclients\*.* " || ,
"3 -c..\bin vnckbd.sys drvins.cmd " || ,
"4 -c..\bin kbdxkey.exe keysymdef.h " || ,
"100 -c..\libvncserver COPYING " || ,
"100 -c..\bin README os2xkey.dll keysym\*.xk " || ,
"-s " || fileScriptOutput

call SysFileDelete fileScriptOutput
EXIT 0


makeScript: PROCEDURE
  fileInput = arg(1)
  fileOutput = arg(2)

  if RxFuncQuery( "gvLoadFuncs" ) then
  do
    call RxFuncAdd "gvLoadFuncs", "RXGETVER", "gvLoadFuncs"
    call gvLoadFuncs
  end

  if stream( fileInput, "c", "open read" ) \= "READY:" then
  do
    say "Cannot open input file: " || fileInput
    return 0
  end

  call SysFileDelete fileOutput
  if stream( fileOutput, "c", "open write" ) \= "READY:" then
  do
    say "Cannot open output file: " || fileOutput
    return 0
  end

  resOk = 1
  do while lines( fileInput ) \= 0
    parse value linein( fileInput ) with part1 "<!-- BL:" file " -->" part2

    if file \= "" then
    do
      rc = gvGetFromFile( file, "ver." )
      if rc \= "OK" then
      do
        say "File: " || file
        say rc
        resOk = 0
        leave
      end

      parse value ver._BL_REVISION || ".0" with ver.1 "." ver.2 "." ver.3 "." v
      verIns = ver.1 || "\" || ver.2 || "\" || ver.3
    end
    else
      verIns = ""

    call lineout fileOutput, part1 || verIns || part2
  end

  call stream fileInput, "c", "close"
  call stream fileOutput, "c", "close"
  call gvDropFuncs

  if \resOk then
    call SysFileDelete fileOutput
    
  return resOk
