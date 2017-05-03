/*
  RDVINS.CMD
  ----------

  Adds or removes device driver to all config.EXT files, where EXT is
  "sys" and other extensions listed in os2ldr.ini.

  Usage:
    DRVINS.CMD -F <driver> [-D <bootDrive>] [-I <0|1>]

    -F <driver>            Driver file name.
    -D <bootDrive>         System boot drive (for ex. 'C:')
    -I <0|1>               Remove (0) or add (1) line DEVICE= to config.???

  Example:
    drvins.cmd -d C:\ -f vnckbd.sys -i 1

  Digi, 2017
*/


debugOutput = 1

/* Try to detect default boot drive */
parse value value( "path", , "OS2ENVIRONMENT" ) with ,
            ":\OS2" -1 arglist.D ":" .
if arglist.D = "" then arglist.D = "C"
/* By default "-i1" - install driver */
arglist.I = 1

/* Read command line switches to arglist._sw_? :
   For example, string "-a 'ab cd' -D E: -n1" will be parsed to:
     arglist.a = "ab cd"
     arglist.D = E:
     arglist.n = 1
     arglist._list = "a D n"
*/
argstr = arg( 1 )
arglist._list = ""
do idx = 1 by 1 while argstr \= ""
  argstr = strip( argstr, "L" )
  parse var argstr "-" =2 sw =3 argstr

  argstr = strip( argstr )
  quote = left( argstr, 1 )
  if quote = '"' | quote = "'" then
    parse var argstr (quote)param(quote)argstr
  else
    parse var argstr param" "argstr

  call value "arglist." || sw, param
  arglist._list = arglist._sw_list" "sw
end
arglist._list = strip( arglist._list, "L" )


/*  Check switches -F and -I.  */
if symbol( "arglist.F" ) \= "VAR" | ( arglist.I \= 0 & arglist.I \= 1 ) then
  signal Usage


/*
   Detect filename and full filename for the driver.
*/

drvFile = stream( arglist.F, "c", "query exists" )
if drvFile = "" then call Error "File " || arglist.F || " does not exist."
drvName = translate( filespec( "name", drvFile ) )
call Debug "Driver file: " || drvName
call Debug "Driver file full name: " || drvFile


/*
   Try to read extensions for system configuration files from the sytem
   loader's INI.
   Output: cfgExtList = "SYS OS4"
*/

cfgExtList = "SYS"     /* "SYS" always present in the output list */
arglist.D = left( arglist.D, 1 ) || ":\"
fileLdrIni = stream( arglist.D"os2ldr.ini", "c", "query exists" )
if fileLdrIni = "" then
  call Debug "Loader INI file "arglist.D"os2ldr.ini does not exist"
else
do
  fKernelSect = 0
  do while lines( fileLdrIni ) \= 0
    line = translate( strip( linein( fileLdrIni ) ) )
    if \fKernelSect then
    do
      fKernelSect = line = "[KERNEL]"
      iterate
    end

    parse var line . "CFGEXT=" cfgExt "," .

    if wordpos( cfgExt, cfgExtList ) = 0 then
      cfgExtList = cfgExtList" "cfgExt
  end

  call stream fileLdrIni, "c", "close"
  call Debug "Extensions for config.* from "arglist.D"os2ldr.ini: "cfgExtList
end


/*
   Remove "DRIVER=[.....]driver.sys", than add driver in each config.EXT
*/

bkpExt = "001"
do while cfgExtList \= ""
  parse var cfgExtList cfgExt" "cfgExtList

  configFile = stream( arglist.D"config."cfgExt, "c", "query exists" )
  if configFile = "" then
    iterate

  call Log "Read: " || configFile

  /* Detect extension for the backup copy if config.*. It will be 3 digital
     characters, like 001, 002, 123. */
  do while stream( arglist.D"config."bkpExt, "c", "query exists" ) \= ""
    bkpExt = right( bkpExt + 1, 3, "0" )
  end

  tempFile = arglist.D"config.$$D"
  if stream( tempFile, "c", "query exists" ) \= "" then
    "@del "tempFile" 2>nul"

  call Debug "Read " || configFile || " to " || tempFile
  do while lines( configFile ) \= 0
    line = linein( configFile )

    parse upper value translate( strip(line), " ", "09"x ) ,
                with statment "=" drv
    if statment = "DEVICE" & drv \= "" then
    do
      if filespec( "name", drv ) = drvName then
        iterate
    end

    if lineout( tempFile, line ) = 1 then
      call Error "File write error (" || tempFile || ")"
  end

  if arglist.I = 1 then
    call lineout tempFile, "DEVICE=" || drvFile

  call stream tempFile, "c", "close"
  call stream configFile, "c", "close"

  call Log "Backup " || configFile || " to *."bkpExt
  "@ren "configFile" *."bkpExt
  call Log "Store changes to " || configFile
  "@ren "tempFile" *."cfgExt
end


EXIT


Log:
  say time() || " " || arg( 1 )
  return

Debug:
  if debugOutput = 1 then
    call Log "[Debug] " || arg( 1 )
  return

Error:
  say "Error: " || arg( 1 )
  exit

Usage:
  say "Adds or removes device driver to all config.EXT files, where EXT is "
  say """sys"" and other extensions listed in os2ldr.ini."
  say "Usage:"
  say "  DRVINS.CMD -F <driver> [-D <bootDrive>] [-I <0|1>]"
  say
  say "  -F <driver>            Driver file name."
  say "  -D <bootDrive>         System boot drive (for ex. 'C:')"
  say "  -I <0|1>               Remove (0) or add (1) line DEVICE= to config.???."
  say "                         Default is 1."
  exit
