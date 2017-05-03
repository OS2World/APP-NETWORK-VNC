#include "devdefs.h"
#include "devhdr.h"

#pragma data_seg ( "_HEADER", "DATA" ) ;

extern VOID Strategy();

// Device driver header.

DEVHEADER DevHeader =
{
  FENCE,
  DAW_CHARACTER/* | DAW_OPENCLOSE*/ | DAW_LEVEL1,
  Strategy,
  0,
  { "VNCKBD$ " },
  "",
  CAP_NULL
};
