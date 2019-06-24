#define INCL_WIN
#define INCL_DOSMODULEMGR
#define INCL_DOSERRORS
#define INCL_DOSSEMAPHORES
//#define INCL_DOSDEVIOCTL
//#define INCL_DOS
#include <os2.h>
#include "vncshook.h"

#ifndef DLLNAME
#define DLLNAME        "VNCSHOOK"
#endif

/*
 * video/rel/.../pmwinp.mac and pmwinx.h in the ddk.
 */
typedef struct _KBDEVENT {
  BYTE       monFlags;           // Open, Close and Flush monitor flags.
  BYTE       scancode;           // Original scan code (actually high byte of
                                 // monitor flags.
  BYTE       xlatedchar;         // Output of interrupt level character
                                 // translation table.

  BYTE       xlatedscan;         // Translated scan code.  Different only for
                                 // the enhanced keyboard.

  USHORT     shiftDBCS;          // DBCS shift state and status.
  USHORT     shiftstate;         // Current state of shift keys.
  ULONG      time;               // Millisecond counter time stamp.
  USHORT     ddFlags;            // Keyboard device driver flags.
} KBDEVENT, *PKBDEVENT;

extern KBDEVENT stKbdPacket[2];

// guess MON_* are for the monFlags field.
#define MON_OPEN	EQU	0001H
#define MON_CLOSE	EQU	0002H
#define MON_FLUSH	EQU	0004H
// guess KSS_* is for the shiftstate field.
#define KSS_SYSREQ	EQU	8000H
#define KSS_CAPSLOCK	EQU	4000H
#define KSS_NUMLOCK	EQU	2000H
#define KSS_SCROLLLOCK	EQU	1000H
#define KSS_RIGHTALT	EQU	0800H
#define KSS_RIGHTCTRL	EQU	0400H
#define KSS_LEFTALT	EQU	0200H
#define KSS_LEFTCTRL	EQU	0100H
#define KSS_INSERTON	EQU	0080H
#define KSS_CAPSLOCKON	EQU	0040H
#define KSS_NUMLOCKON	EQU	0020H
#define KSS_SCROLLLOCKON	EQU	0010H
#define KSS_EITHERALT	EQU	0008H
#define KSS_EITHERCTRL	EQU	0004H
#define KSS_LEFTSHIFT	EQU	0002H
#define KSS_RIGHTSHIFT	EQU	0001H
#define KSS_EITHERSHIFT	EQU	0003H
/* Valid values for ddFlags field */
#define KDD_MONFLAG1    0x8000 // bird: conflicts with KDD_KC_LONEKEY - relevant for VIOCHAR?
#define KDD_MONFLAG2	0x4000 // bird: conflicts with KDD_KC_PREVDOWN - relevant for VIOCHAR?
#define KDD_RESERVED	0x3C00
#define KDD_ACCENTED	0x0200
#define KDD_MULTIMAKE	0x0100
#define KDD_SECONDARY	0x0080
#define KDD_BREAK	0x0040
#define KDD_EXTENDEDKEY	0x0020
#define KDD_ACTIONFIELD	0x001F
#define KDD_KC_LONEKEY	0x8000
#define KDD_KC_PREVDOWN	0x4000
#define KDD_KC_KEYUP	0x2000
#define KDD_KC_ALT	0x1000
#define KDD_KC_CTRL	0x0800
#define KDD_KC_SHIFT	0x0400
#define KDD_KC_FLAGS	0x0FC00
/* Valid values for KDD_ACTIONFIELD portion of ddFlags field */
#define KDD_PUTINKIB	0x0000
#define KDD_ACK	0x0001
#define KDD_PREFIXKEY	0x0002
#define KDD_OVERRUN	0x0003
#define KDD_RESEND	0x0004
#define KDD_REBOOTKEY	0x0005
#define KDD_DUMPKEY	0x0006
#define KDD_SHIFTKEY	0x0007
#define KDD_PAUSEKEY	0x0008
#define KDD_PSEUDOPAUSE	0x0009
#define KDD_WAKEUPKEY	0x000A
#define KDD_BADACCENT	0x000B
#define KDD_HOTKEY	0x000C
#define KDD_ACCENTKEY	0x0010
#define KDD_BREAKKEY	0x0011
#define KDD_PSEUDOBREAK	0x0012
#define KDD_PRTSCKEY	0x0013
#define KDD_PRTECHOKEY	0x0014
#define KDD_PSEUDOPRECH	0x0015
#define KDD_STATUSCHG	0x0016
#define KDD_WRITTENKEY	0x0017
#define KDD_UNDEFINED	0x001F



static HMODULE         hmodPMHook      = NULLHANDLE;
static HMQ             hmqApp          = NULLHANDLE;
static HEV             hevMsgInputHook = NULLHANDLE;
static ULONG           ulWMUpdate = 165412;
static struct _POSTMSG {
  ULONG      ulMsg;
  MPARAM     mp1;
  MPARAM     mp2;
}                      stPostMsg = { 0, 0, 0 };

static VOID _updateMessage(HWND hwnd, RECTL *pRect)
{
#if 1
  WinPostQueueMsg( hmqApp, HM_SCREEN,
                   MPFROM2SHORT( pRect->xLeft, pRect->yBottom ),
                   MPFROM2SHORT( pRect->xRight, pRect->yTop ) );
#else
  WinPostMsg( hwnd, ulWMUpdate,
              MPFROM2SHORT( pRect->xLeft, pRect->yBottom ),
              MPFROM2SHORT( pRect->xRight, pRect->yTop ) );
#endif
}

static VOID _windowChanged(HWND hwnd, BOOL fVisibleCheck)
{
  RECTL      rectlWin;

  if ( ( fVisibleCheck && !WinIsWindowVisible( hwnd ) ) ||
       !WinQueryWindowRect( hwnd, &rectlWin ) ||
       !WinMapWindowPoints( hwnd, HWND_DESKTOP, (PPOINTL)&rectlWin, 2 ) )
    return;

  _updateMessage( hwnd, &rectlWin );
}

static VOID _windowPaint(HWND hwnd)
{
  RECTL      rectlWin;

  if ( WinQueryUpdateRect( hwnd, &rectlWin ) &&
       WinMapWindowPoints( hwnd, HWND_DESKTOP, (PPOINTL)&rectlWin, 2 ) )
    _updateMessage( hwnd, &rectlWin );
  else
    _windowChanged( hwnd, TRUE );
}

// Monitor messages that the system does not post to a queue (while processing
// WinSendMsg()).
PMHEXPORT VOID EXPENTRY pmhSendMsgHookProc(HAB hab, PSMHSTRUCT psmh,
                                           BOOL fInterTask)
{
  if ( hmqApp == NULLHANDLE )
    return FALSE;

  switch ( psmh->msg )
  {
    case EM_SETSEL:
    case BM_SETCHECK:
    case WM_ENABLE:
    case WM_REALIZEPALETTE:
    case WM_SETWINDOWPARAMS:
    case WM_MENUEND:
      _windowChanged( psmh->hwnd, TRUE );
      break;

    case WM_WINDOWPOSCHANGED:
      _windowChanged( psmh->hwnd,
                      (((PSWP)psmh->mp1)->fl & (SWP_HIDE | SWP_MINIMIZE)) == 0 );
      break;

    case WM_PAINT:
      _windowPaint( psmh->hwnd );
      break;

    case WM_MENUSELECT:
      if ( (HWND)psmh->mp2 != NULLHANDLE )
        _windowChanged( (HWND)psmh->mp2, TRUE );
  }
}

PMHEXPORT BOOL EXPENTRY pmhInputHookProc(HAB hab, PQMSG pqmsg, ULONG ulOption)
{
  if ( pqmsg->msg == ulWMUpdate )
  {
    if ( ( hmqApp != NULLHANDLE ) &&
         !WinPostQueueMsg( hmqApp, HM_SCREEN, pqmsg->mp1, pqmsg->mp2 ) )
      hmqApp = NULLHANDLE;

    return TRUE;
  } 

  if ( hmqApp == NULLHANDLE )
    return FALSE;

  switch ( pqmsg->msg )
  {
/*    case WM_VIOCHAR:
      WinPostQueueMsg( hmqApp, HM_VIOCHAR, pqmsg->mp1, pqmsg->mp2 );*/
    case WM_CHAR:
    case WM_BUTTON1UP:
    case WM_BUTTON2UP:
    case WM_BUTTON3UP:
    case WM_REALIZEPALETTE:
//    case WM_USER:      // [eros2] handle xCenter pulse widget update
    case WM_TIMER:     // [eros2] Note: may cause high CPU load
    case CM_SCROLLWINDOW:        // In container.
      _windowChanged( pqmsg->hwnd, TRUE );
      break;

    case WM_HSCROLL:
    case WM_VSCROLL:
      if ( ( SHORT2FROMMP(pqmsg->mp2) == SB_SLIDERPOSITION ) ||
           ( SHORT2FROMMP(pqmsg->mp2) == SB_ENDSCROLL ) )
        _windowChanged( pqmsg->hwnd, TRUE );
      break;

    case WM_PAINT:
      _windowPaint( pqmsg->hwnd );
      break;

    case WM_MOUSEMOVE:
      // [eros2] Inform PMVNC that the mouse has moved and pass it the current
      // cursor handle
      if ( !WinPostQueueMsg( hmqApp, HM_MOUSE, MPFROMHWND(pqmsg->hwnd), 0 ) )
        hmqApp = NULLHANDLE;
      break;
  }

  return FALSE;
}

BOOL EXPENTRY pmhMsgInputHookProc(HAB hab, PQMSG pQmsg, BOOL fSkip,
                                  PBOOL pfNoRecord)
{
  if ( fSkip )
  {
    // OS2TK: When all messages have been passed in the application must
    // remove the MsgInputHook hook using WinReleaseHook.
    WinReleaseHook( hab, NULLHANDLE, HK_MSGINPUT, (PFN)pmhMsgInputHookProc,
                    hmodPMHook );

    stPostMsg.ulMsg = 0;
    if ( DosOpenEventSem( NULL , &hevMsgInputHook ) == NO_ERROR )
    {
      DosPostEventSem( hevMsgInputHook );
      DosCloseEventSem( hevMsgInputHook );
    }
    return FALSE;
  }

  if ( stPostMsg.ulMsg == 0 )
    return FALSE;

  pQmsg->time = WinGetCurrentTime( hab );
  pQmsg->msg  = stPostMsg.ulMsg;
  pQmsg->mp1  = stPostMsg.mp1;
  pQmsg->mp2  = stPostMsg.mp2;

  *pfNoRecord = FALSE;

  // Dos sessions support.

  if ( pQmsg->msg == WM_VIOCHAR )
  {
    /* mp1.s1: KC_ flags.
     * mp1.c3: repeat
     * mp1.c4: scancode
     * mp2.c1: translated char
     * mp2.c2: translated scancode
     * mp2.s2: KDD_ flags.
     */
    stKbdPacket[0].monFlags   = 0; // no monitor flag
    stKbdPacket[0].scancode   = CHAR4FROMMP(pQmsg->mp1);
    stKbdPacket[0].xlatedchar = CHAR1FROMMP(pQmsg->mp2);
    stKbdPacket[0].xlatedscan = CHAR2FROMMP(pQmsg->mp2);
    stKbdPacket[0].time       = pQmsg->time;
    stKbdPacket[0].ddFlags    = SHORT2FROMMP(pQmsg->mp2);
  }

  return TRUE;
}



// Public routines
// ---------------

PMHEXPORT BOOL APIENTRY pmhInit(HAB hab, HMQ hmq)
{
  ULONG      ulRC;
  HATOMTBL   hSysAtomTable;
  SEL        globalSeg, localSeg;

  if ( ( hmqApp != NULLHANDLE ) || ( hab == NULLHANDLE ) ||
       ( hmq == NULLHANDLE ) )
    return FALSE;

  ulRC = DosQueryModuleHandle( DLLNAME, &hmodPMHook );
  if ( ulRC != NO_ERROR )
    return FALSE;

  if ( !WinSetHook( hab, NULLHANDLE, HK_SENDMSG, (PFN)pmhSendMsgHookProc,
                    hmodPMHook ) )
    return FALSE;

  if ( !WinSetHook( hab, NULLHANDLE, HK_INPUT, (PFN)pmhInputHookProc,
                    hmodPMHook ) )
  {
    WinReleaseHook( hab, NULLHANDLE, HK_SENDMSG, (PFN)pmhSendMsgHookProc,
                    hmodPMHook );
    return FALSE;
  }

  ulRC = DosCreateEventSem( NULL, &hevMsgInputHook,
                            DC_SEM_SHARED | DCE_AUTORESET, FALSE );
  if ( ulRC != NO_ERROR )
  {
    WinReleaseHook( hab, NULLHANDLE, HK_SENDMSG, (PFN)pmhSendMsgHookProc,
                    hmodPMHook );
    WinReleaseHook( hab, NULLHANDLE, HK_INPUT, (PFN)pmhInputHookProc,
                    hmodPMHook );
    return FALSE;
  }

  hmqApp = hmq;

  return TRUE;
}

PMHEXPORT VOID APIENTRY pmhDone(HAB hab)
{
  if ( hmqApp == NULLHANDLE )
    return;

  WinReleaseHook( hab, NULLHANDLE, HK_SENDMSG, (PFN)pmhSendMsgHookProc,
                  hmodPMHook );
  WinReleaseHook( hab, NULLHANDLE, HK_INPUT, (PFN)pmhInputHookProc,
                  hmodPMHook );
  DosCloseEventSem( hevMsgInputHook );

  hmqApp = NULLHANDLE;
}

PMHEXPORT BOOL APIENTRY pmhPostEvent(HAB hab, ULONG ulMsg,
                                     MPARAM mp1, MPARAM mp2)
{
  ULONG      ulPost;

  if ( hmqApp == NULLHANDLE )
    return FALSE;

  DosResetEventSem( hevMsgInputHook, &ulPost );

  stPostMsg.ulMsg = ulMsg;
  stPostMsg.mp1   = mp1;
  stPostMsg.mp2   = mp2;
  if ( !WinSetHook( hab, NULLHANDLE, HK_MSGINPUT, (PFN)pmhMsgInputHookProc,
                    hmodPMHook ) )
    return FALSE;

  WinCheckInput( hab );

  return DosWaitEventSem( hevMsgInputHook, 3000 ) == NO_ERROR;
}
