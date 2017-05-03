/*
   Removes all 0x0D bytes from programs.txt
   File programs.txt includes in ..\vncserver.rc
*/

InFName = "programs.txt"
TmpFName = "programs.$$$"

"del " || TmpFName

do while lines( InFName ) \= 0
  call charout TmpFName, linein( InFName ) || x2c("0A")
end
call stream TmpFName, "c", "close"
call stream InFName, "c", "close"

"del " || InFName
"ren " || TmpFName || " " || InFName