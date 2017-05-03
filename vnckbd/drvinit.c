#include "devdefs.h"
#include <i86.h>
#include <conio.h>
#include "devhelp.h"
#include "devreqp.h"
#include "debug.h"

extern ULONG  DevHlp;

// Ensure that the Initialization code located at the end of the driver

#pragma code_seg ( "_INITCODE" ) ;
#pragma data_seg ( "_INITDATA", "INITDATA" ) ;

extern USHORT  OffFinalCS;
extern USHORT  OffFinalDS;

extern VOID drvInit(REQP_INIT FAR *rp)
{
  Device_Help = (PFN)rp->in.devhlp;           // save far pointer to DevHlp

  cominit();
  logstr( "VNCKBD: init" );
  logn();

  rp->out.finalcs = FP_OFF( &OffFinalCS );    // set pointers to
  rp->out.finalds = FP_OFF( &OffFinalDS );    //discardable code/data
  rp->header.status = RPDONE;
}
