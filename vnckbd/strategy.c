#include "devdefs.h"
#include "devhelp.h"
#include "devreqp.h"
#include "debug.h"
#include "vnckbd.h"
#include <i86.h>

extern VOID drvInit(REQP_INIT FAR *rp);

// ------------------------------------------------------------------

#define KBDFL_ATTACHED           0x01
#define KBDFL_DISABLED           0x02

/******* IDC Values *******************/
#define  KBDQUERYOPEN      0
#define  KBDQUERYCLOSE     1
#define  KBDQUERYCAPS      2
#define  KBDQUERYTYPEMATIC 3
#define  KBDQUERYLEDS      4
#define  KBDQUERYID        5
#define  KBDQUERYDISABLED  6
#define  KBDDISABLE        7
#define  KBDENABLE         8
#define  KBDRESET          9
#define  KBDSETTYPEMATIC   10
#define  KBDSETLEDS        11
#define  KBDSENDGENERAL    14
#define  KBDQUERYREADY     15
#define  KBDFLUSHPARTIAL   16
#define  KBDSAVESTATE      18
#define  KBDRESTORESTATE   19

#define  KBDDD_Open        0
#define  KBDDD_Close       1
#define  KBDDD_ScanCode    2
#define  KBDDD_Reinit      3

typedef struct Attach_DD {
  ULONG    realentry;
  USHORT   realds;
  ULONG    protentry;
  USHORT   protds;
} ATTACHDD;

typedef USHORT PASCAL (FAR* PFIDC) (USHORT cmd, USHORT p1, USHORT p2, USHORT p3);


PFN          Device_Help;  // DevHelp Interface Address
PFIDC        KbdDI_Entry;
USHORT       usFlags = 0;
ATTACHDD     KbdDD;
USHORT       KbdDI_Handle = 0xFFFF;        /* Handle to KBDDI */
USHORT       usScan;
#ifndef __WATCOMC__
USHORT       usVNCKbdCS;
USHORT       usVNCKbdDS;
#endif


USHORT CS(void);
#pragma aux CS = "mov ax,cs"  \
   value [ax];

USHORT DS(void);
#pragma aux DS = "mov ax,ds"  \
   value [ax];

USHORT __cdecl __loadds _far
kbd_idc_entry(USHORT Req, USHORT Req_Off, USHORT Req_Seg);

USHORT Call_KbdDI(USHORT func, void FAR* buffer)
{
  USHORT     usRC;

  if ( KbdDI_Handle == 0xFFFF )
  {
    // call the KBD IDC routine to set up our IDC routine
#ifndef __WATCOMC__
    _asm {
      mov usVNCKbdCS, cs         // Save the code segment value.
      mov usVNCKbdDS, ds         // Save the data segment value.
    }
    KbdDI_Handle = KbdDI_Entry( KBDDD_Open, FP_OFF(&kbd_idc_entry),
                                usVNCKbdDS, usVNCKbdCS );
#else
    KbdDI_Handle = KbdDI_Entry( KBDDD_Open, FP_OFF(&kbd_idc_entry),
                                DS(), CS() );
#endif
  }

  // call the KBD IDC routine with the keyboard scan code
  usRC = KbdDI_Entry( func, FP_OFF(buffer), FP_SEG(buffer), KbdDI_Handle );
/*
  logstr( "VNCKBD: KbdDI_Entry, rc = " );
  logs( usRC );
  logn();
*/

  return usRC;
}

// Returns 0 if error has not occurred.
USHORT KbdPushScancode(USHORT FAR* pusScan)
{
  if ( (usFlags & KBDFL_ATTACHED) == 0 )
  {
    if ( DevHelp_AttachDD( "KBD$    ", (NPBYTE)&KbdDD ) != 0 )
    {
      logn();
      logstr( "VNCKBD: DevHelp_AttachDD() failed\r\n" );
      return 0xFFFF;
    }

    usFlags |= KBDFL_ATTACHED;
    KbdDI_Entry = (PFIDC)(KbdDD.protentry);
  }

  usScan = *pusScan;

  return Call_KbdDI( KBDDD_ScanCode, &usScan );
}

USHORT __cdecl __loadds _far
kbd_idc_entry(USHORT Req, USHORT Req_Off, USHORT Req_Seg)
{
  USHORT usRes;

  switch( Req & 0x7FFF )
  {
    case KBDQUERYOPEN:
    case KBDQUERYCLOSE:
    case KBDQUERYCAPS:
    case KBDQUERYTYPEMATIC:
    case KBDQUERYLEDS:
    case KBDQUERYID:
      usRes = 0;
      break;

    case KBDQUERYDISABLED:
      usRes = ( usFlags & KBDFL_DISABLED );
      break;

    case KBDDISABLE:
      usFlags |= KBDFL_DISABLED;
      usRes = 0;
      break;

    case KBDENABLE:
      usFlags &= ~KBDFL_DISABLED;
      usRes = 0;
      break;

/*    case KBDRESET:
    case KBDSETTYPEMATIC:
    case KBDSETLEDS:
    case KBDSENDGENERAL:
    case KBDQUERYREADY:
    case KBDFLUSHPARTIAL:*/
    default:
      usRes = 0xFFFF;
  }

  return usRes;
}

// ------------------------------------------------------------------

// VOID drvIOCtl(REQP_IOCTL FAR *rp)
//
// IOCtl requests ( DosDevIOCtl() ) handler.

static VOID drvIOCtl(REQP_IOCTL FAR *rp)
{
  UCHAR      ucScrnGrp = 0;
  PUSHORT    pusScan, pusEnd;

  if ( rp->category != VNCKBDIOCTL_CATECORY )
  {
    rp->header.status = RPDONE | RPERR | RPERR_COMMAND;
    return;
  }

  switch( rp->function )
  {
    case VNCKBDIOCTL_FN_SENDSCAN:
      if ( ( rp->paramlen < sizeof(USHORT) ) ||
           DevHelp_VerifyAccess( FP_SEG(rp->param), rp->paramlen,
                                 FP_OFF(rp->param), VERIFY_READONLY ) )
      {
        logstr( "VNCKBD: VNCKBDIOCTL_FN_QUERYSCRGR. Verify param. failed\r\n" );
        rp->header.status = RPDONE | RPERR | RPERR_PARAMETER;
        break;
      }

      pusScan = (PUSHORT)rp->param;
      pusEnd = (PUSHORT)( ((PUCHAR)rp->param) + (rp->paramlen & ~0x01) );

      logstr( "VNCKBD: Send scan code:" );
      for( ; pusScan != pusEnd; pusScan++ )
      {
        logstr( " " );
        logs( *pusScan );
        KbdPushScancode( pusScan );
      }
      logn();

      rp->header.status = RPDONE;
      break;

    case VNCKBDIOCTL_FN_QUERYSCRGR:
      if ( ( rp->datalen == 0 ) ||
           DevHelp_VerifyAccess( FP_SEG(rp->data), 1,
                                 FP_OFF(rp->data), VERIFY_READWRITE ) )
      {
        logstr( "VNCKBD: VNCKBDIOCTL_FN_QUERYSCRGR. Verify data buf. failed\r\n" );
        rp->header.status = RPDONE | RPERR | RPERR_PARAMETER;
        break;
      }

      _asm {
        pusha
        mov   al, 1                ; Query GlobalInfoSeg.
        xor   dx, dx
        mov   dl, 0x24;            ; DevHlp_GetDOSVar
      }
      (*Device_Help)();
      _asm {
        mov   es, ax               ; Make ES:BX point to InfoSeg address variable.
        mov   ax, es:[bx]          ; Get the segment.
        mov   es, ax               ; Make ES point to SInfoSeg.
        mov   al, es:[18h]         ; Foreground screen group ID.
        mov   ucScrnGrp, al
        popa
      }
      *((PUCHAR)rp->data) = ucScrnGrp;
      rp->header.status = RPDONE;
      break;

    default:
      logstr( "VNCKBD: Invalid IOCtl request. Unknown function: " );
      logc( rp->function );
      logn();
      rp->header.status = RPDONE | RPERR | RPERR_COMMAND;
  }          // switch()
}

// Strategy entry point.

#pragma aux STRATEGY far parm [es bx];
#pragma aux (STRATEGY) Strategy;

VOID Strategy(REQP_ANY FAR *rp)
{
  if ( rp->header.command < RP_END )
  {
    switch( rp->header.command )
    {
      case RP_INIT:
        drvInit( (REQP_INIT FAR *)rp );
        break;

      case RP_IOCTL:
        drvIOCtl( (REQP_IOCTL FAR *)rp );
        break;

      case RP_READ:
      case RP_WRITE:
        rp->header.status = RPERR_COMMAND | RPDONE;
        break;

      case RP_READ_NO_WAIT:
      case RP_INPUT_STATUS:
      case RP_INPUT_FLUSH:
      case RP_WRITE_VERIFY:
      case RP_OUTPUT_STATUS:
      case RP_OUTPUT_FLUSH:
      case RP_OPEN:
      case RP_CLOSE:
        rp->header.status = RPDONE;
        break;

      default:
        rp->header.status = RPERR_COMMAND | RPDONE;
    }
  }
  else
    rp->header.status = RPERR_COMMAND | RPDONE;
}
