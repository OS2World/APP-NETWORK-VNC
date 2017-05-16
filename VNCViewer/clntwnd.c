#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_DOSSEMAPHORES
#define INCL_DOSMISC
#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <rfb/rfbclient.h>
#include <utils.h>
#include "clntconn.h"
#include "lnchpad.h"  // lpOpenDlg(), lpStoreWinRect()
#include <vncpm.h>
#include "clntwnd.h"
#include "pmwinmou.h"
#include "resource.h"
#include "vncvipc.h"
#include <debug.h>

#define _WIN_CLIENT_CLASS        "VNCVIEWER"

// One-time message to set client connection object and host for a new window.
#define _WM_INITCLNTWND          (WM_USER + 200)
#define _WM_MENUSHOW             (WM_USER + 201)

// main.c
extern HAB             hab;
extern HMQ             hmq;
extern ULONG           cOpenWin;
BOOL _stdcall (*pfnWinRegisterForWheelMsg)(HWND hwnd, ULONG flWindow);

// fxdlg.c
extern HWND fxdlgOpen(HWND hwndOwner, PSZ pszServer, PCLNTCONN pCC);

static PFNWP           oldWndFrameProc;
static PFNWP           oldWndMenuProc;
static ULONG           ulWMVNCVNotify = 0;

typedef struct _WINDATA {
  PCLNTCONN            pCC;
  PSZ                  pszHost;
  HWND                 hwndNotify;
  SHORT                sHSlider;
  SHORT                sVSlider;
  HPS                  hpsMicro;
  LONG                 lMouseButtonsDown;
  HWND                 hwndChat;
  HWND                 hwndFX;
  BOOL                 fVOBeforeFX;
  ULONG                ulLastChatCmd; // CCCHAT_xxxxx last command from server.
  BOOL                 fActivated;
  PSZ                  pszCBTextRecv;
} WINDATA, *PWINDATA;

// Data for _WM_INITCLNTWND (mp1)
typedef struct _INITCLNTWND {
  PCLNTCONN            pCC;
  PSZ                  pszHost;
  HWND                 hwndNotify;
} INITCLNTWND, *PINITCLNTWND;



MRESULT EXPENTRY _menuWinProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_WINDOWPOSCHANGED:
      if ( ( ((PSWP)mp1)->fl & (SWP_SHOW | SWP_HIDE | SWP_NOREDRAW) ) ==
           SWP_SHOW )
      {
        HWND hwndFrame = WinQueryWindow( hwnd, QW_FRAMEOWNER );
        HWND hwndClient = WinWindowFromID( hwndFrame, FID_CLIENT );

        WinSendMsg( hwndClient, _WM_MENUSHOW, 0, 0 );
      }
      break;
  }

  return oldWndMenuProc( hwnd, msg, mp1, mp2 );
}


static VOID _cwTuneMenu(HWND hwnd)
{
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  HWND       hwndMenu  = WinWindowFromID( hwndFrame, FID_SYSMENU );
  PWINDATA   pWinData  = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  BOOL       fViewOnly = ccQueryViewOnly( pWinData->pCC );
  BOOL       fFXDlg    = pWinData->hwndFX != NULLHANDLE;

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_VIEW_ONLY,TRUE),
              MPFROM2SHORT( MIA_CHECKED, fViewOnly ? MIA_CHECKED : 0 ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_VIEW_ONLY,TRUE),
              MPFROM2SHORT( MIA_DISABLED, fFXDlg ? MIA_DISABLED : 0 ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_SEND_CAD,TRUE),
              MPFROM2SHORT( MIA_DISABLED, fViewOnly ? MIA_DISABLED : 0 ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_SEND_CTRL_ESC,TRUE),
              MPFROM2SHORT( MIA_DISABLED, fViewOnly ? MIA_DISABLED : 0 ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_SEND_ALT_ESC,TRUE),
              MPFROM2SHORT( MIA_DISABLED, fViewOnly ? MIA_DISABLED : 0 ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_SEND_ALT_TAB,TRUE),
              MPFROM2SHORT( MIA_DISABLED, fViewOnly ? MIA_DISABLED : 0 ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_CHAT,TRUE),
              MPFROM2SHORT( MIA_DISABLED, fFXDlg ? MIA_DISABLED : 0 ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_CHAT,TRUE),
              MPFROM2SHORT( MIA_DISABLED,
                !fFXDlg &&
                ccIsRFBMsgSupported( pWinData->pCC, TRUE, rfbTextChat ) ?
                  0 : MIA_DISABLED ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(CWC_FILE_TRANSFER,TRUE),
              MPFROM2SHORT( MIA_DISABLED,
                ccIsRFBMsgSupported( pWinData->pCC, TRUE, rfbFileTransfer ) ?
                  0 : MIA_DISABLED ) );
}

static MRESULT _wmCreate(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  static struct {
    ULONG ulResId;
    ULONG ulCmd;     // String table, ID   Control command
  } aMenuItems[] = { { 0,                  0                  }, // Separator
                     { IDS_NEW_CONNECTION, CWC_NEW_CONNECTION },
                     { IDS_VIEW_ONLY,      CWC_VIEW_ONLY      },
                     { 0,                  0                  }, // Separator
                     { IDS_SEND_CAD,       CWC_SEND_CAD       },
                     { IDS_SEND_CTRL_ESC,  CWC_SEND_CTRL_ESC  },
                     { IDS_SEND_ALT_ESC,   CWC_SEND_ALT_ESC   },
                     { IDS_SEND_ALT_TAB,   CWC_SEND_ALT_TAB   },
                     { IDS_CHAT,           CWC_CHAT           },
                     { IDS_FILE_TRANSFER,  CWC_FILE_TRANSFER  },
                     { IDS_SCREENSHOT,     CWC_SCREENSHOT     } };
  PWINDATA   pWinData = calloc( 1, sizeof(WINDATA) );
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
//  HPOINTER   hptrIcon;
  HWND       hwndMenu;
  ULONG      ulIdx;

  if ( pWinData == NULL )
    return (MRESULT)TRUE; // Fail.

/*
  hptrIcon = WinLoadPointer( HWND_DESKTOP, NULLHANDLE, ID_APPICON );
  if ( hptrIcon != NULLHANDLE )
    WinSendMsg( hwndFrame, WM_SETICON, MPFROMLONG(hptrIcon), 0 );
*/

  WinSetWindowPtr( hwnd, 0, pWinData );

  // Register for wheel messages being sent to window.
  if ( ( pfnWinRegisterForWheelMsg != NULL ) &&
       !pfnWinRegisterForWheelMsg( hwnd, AW_OWNERFRAME ) )
    debug( "WinRegisterForWheelMsg() failed" );

  hwndMenu = WinWindowFromID( hwndFrame, FID_SYSMENU );
  if ( hwndMenu != NULLHANDLE )
  {
    SHORT              sItemId;
    MENUITEM           stMI;
    CHAR               acBuf[64];

    // Get system submenu window handle.
    WinSendMsg( hwndMenu, MM_QUERYITEM, MPFROM2SHORT( SC_SYSMENU, TRUE ),
                MPFROMP( &stMI ) );
    hwndMenu = stMI.hwndSubMenu;

    // Remove unused items.
    WinSendMsg( hwndMenu, MM_DELETEITEM, MPFROM2SHORT( SC_RESTORE, 1 ), 0 );
    WinSendMsg( hwndMenu, MM_DELETEITEM, MPFROM2SHORT( SC_MAXIMIZE, 1 ), 0 );
    WinSendMsg( hwndMenu, MM_DELETEITEM, MPFROM2SHORT( SC_HIDE, 1 ), 0 );
    // Remove separator before "Close".
    WinSendMsg( hwndMenu, MM_DELETEITEM, MPFROM2SHORT( -2, 1 ), 0 );

    stMI.hwndSubMenu = NULLHANDLE;
    stMI.hItem       = NULLHANDLE;
    stMI.afAttribute = 0; // MIA_DISABLED
    stMI.iPosition = SHORT1FROMMR(WinSendMsg( hwndMenu, MM_QUERYITEMCOUNT, 0, 0 ));

    // Add menu items.

    for( ulIdx = 0; ulIdx < ARRAYSIZE(aMenuItems); ulIdx++ )
    {
      stMI.id = aMenuItems[ulIdx].ulCmd;
      if ( stMI.id != 0 )
      {
        if ( WinLoadString( hab, 0, aMenuItems[ulIdx].ulResId,
                            sizeof(acBuf), acBuf ) == 0 )
        {
          debug( "Cannot load string #%u", aMenuItems[ulIdx].ulResId );
          continue;
        }

        stMI.afStyle = MIS_TEXT;
      }
      else
        stMI.afStyle = MIS_SEPARATOR;

      sItemId = SHORT1FROMMR( WinSendMsg( hwndMenu, MM_INSERTITEM,
                              MPFROMP( &stMI ), MPFROMP( acBuf ) ) );
      if ( ( sItemId == MIT_MEMERROR ) || ( sItemId == MIT_ERROR ) )
        debug( "Cannot insert new item, rc = %d", sItemId );
      else
        stMI.iPosition++;
    }

    // Subclass the menu window to cath event when menu is set to appear.
    // We will send window message _WM_MENUSHOW to the client window.
    oldWndMenuProc = WinSubclassWindow( hwndMenu, _menuWinProc );
  } // if ( hwndMenu != NULLHANDLE )

  return (MRESULT)FALSE; // Success code.
}

static VOID _wmDestroy(HWND hwnd)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  HPOINTER   hptrIcon = (HPOINTER)WinSendMsg( WinQueryWindow( hwnd, QW_PARENT ),
                                              WM_QUERYICON, 0, 0 );
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  RECTL      rectlWin;

  if ( ( WinQueryWindowULong( hwndFrame, QWL_STYLE ) & WS_MINIMIZED ) == 0 )
  {
    // Store the window position.

    RECTL    rectlDT;

    WinQueryWindowRect( HWND_DESKTOP, &rectlDT );
    WinQueryWindowRect( hwndFrame, &rectlWin );
    WinMapWindowPoints( hwndFrame, HWND_DESKTOP, (PPOINTL)&rectlWin, 2 );

    // Window can be placed on not current xPager's page, check it...
    if ( ( rectlWin.xLeft < rectlDT.xRight ) &&
         ( rectlWin.xRight > rectlDT.xLeft ) &&
         ( rectlWin.yBottom < rectlDT.yTop ) &&
         ( rectlWin.yTop > rectlDT.yBottom ) )
      lpStoreWinRect( pWinData->pszHost, &rectlWin );
  }

  if ( pWinData->hwndFX != NULLHANDLE )
    WinDestroyWindow( pWinData->hwndFX );

  if ( pWinData->pszHost != NULL )
    free( pWinData->pszHost );

  if ( ( pWinData->hpsMicro != NULLHANDLE ) &&
       !GpiDestroyPS( pWinData->hpsMicro ) )
    debug( "GpiDestroyPS() failed" );

  if ( pWinData->pszCBTextRecv != NULL )
    DosFreeMem( pWinData->pszCBTextRecv );

  if ( hptrIcon != NULLHANDLE )
    WinDestroyPointer( hptrIcon );

  free( pWinData );
}

static VOID _wmClose(HWND hwnd)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

  debugCP();
  // Connection thread will destroy this window when a signal will be received.
  ccDisconnectSignal( pWinData->pCC );
}

static VOID _wmVNCState(HWND hwnd, ULONG ulState)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  CHAR       acBuf[256];

  if ( pWinData == NULL )
    return;

  switch( ulState )
  {
    case RFBSTATE_FINISH:
      ccDestroy( pWinData->pCC );
      pWinData->pCC = NULL;

      debugCP( "WinDestroyWindow()..." );
      WinDestroyWindow( WinQueryWindow( hwnd, QW_PARENT ) );
      break;

    case RFBSTATE_ERROR:
      if ( ccQuerySessionInfo( pWinData->pCC, CCSI_LAST_LOG_REC, sizeof(acBuf),
                               acBuf ) )
      {
        CHAR           acTitle[64];

        WinQueryWindowText( WinQueryWindow( hwnd, QW_PARENT ),
                            sizeof(acTitle) - 1, acTitle );
        // We don't sets our window as owner for the messagebox because we can
        // get RFBSTATE_FINISH immediately after RFBSTATE_ERROR and destroy the
        // window.
        WinMessageBox( HWND_DESKTOP, HWND_DESKTOP, acBuf, acTitle, 0,
                       MB_ERROR | MB_MOVEABLE | MB_OK | MB_SYSTEMMODAL );
      }
      break;
  }
}

static MRESULT _wmInitClntWnd(HWND hwnd, PINITCLNTWND pInitClntWnd)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  SIZEL      sizeFrame;

  if ( pWinData->pszHost != NULL )
    free( pWinData->pszHost );
  
  pWinData->pszHost    = pInitClntWnd->pszHost != NULL ?
                           strdup( pInitClntWnd->pszHost ) : NULL;
  pWinData->pCC        = pInitClntWnd->pCC;
  pWinData->hwndNotify = pInitClntWnd->hwndNotify;

  if ( !ccQueryFrameSize( pInitClntWnd->pCC, &sizeFrame ) )
  {
    debug( "The connection is not yet ready" );
    return (MRESULT)FALSE;
  }

  WinSendMsg( hwnd, WM_VNC_SETCLIENTSIZE,
              MPFROM2SHORT( sizeFrame.cx, sizeFrame.cy ), 0 );

  return (MRESULT)TRUE;
}

static MRESULT _wmVNCSetClientSize(HWND hwnd, USHORT usCX, USHORT usCY)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  RECTL      rectlWin, rectlDT;
  ULONG      ulSWPFlags = SWP_MOVE | SWP_SIZE;

  // Query desktop rectangle.
  WinQueryWindowRect( HWND_DESKTOP, &rectlDT );

  if ( pWinData->hpsMicro == NULLHANDLE )
  {
    // The window size is set for the first time.

    HDC        hdc = WinOpenWindowDC( hwnd );
    SIZEL      sizel;

    sizel.cx = usCX;
    sizel.cy = usCY;
    pWinData->hpsMicro = GpiCreatePS( hab, hdc, &sizel,
                            PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC );
    if ( pWinData->hpsMicro == NULLHANDLE || pWinData->hpsMicro == GPI_ERROR )
      debug( "GpiCreatePS() fail" );

    // Try to load the previous position of the window from INI-file.
    if ( !lpQueryWinRect( pWinData->pszHost, &rectlWin ) )
    {
      // The position was not saved - place new rectangle at the desktop center.
      rectlWin.xLeft   = ( rectlDT.xRight - rectlDT.xLeft - usCX ) / 2;
      rectlWin.yBottom = ( rectlDT.yTop - rectlDT.yBottom - usCY ) / 2;
      rectlWin.xRight  = rectlWin.xLeft + usCX;
      rectlWin.yTop    = rectlWin.yBottom + usCY;
      WinCalcFrameRect( hwndFrame, &rectlWin, FALSE );
    }

    ulSWPFlags |= SWP_ACTIVATE;

    if ( pWinData->hwndNotify != NULLHANDLE )
      WinPostMsg( pWinData->hwndNotify, ulWMVNCVNotify,
                  MPFROMSHORT(VNCVNOTIFY_CLIENTWINDOW),
                  MPFROMHWND(hwnd) );
  }
  else
  {
    // Place a new window rectangle at the center of the existing window.
    WinQueryWindowRect( hwndFrame, &rectlWin );
    WinMapWindowPoints( hwndFrame, HWND_DESKTOP, (PPOINTL)&rectlWin, 2 );
    rectlWin.xLeft   = (rectlWin.xLeft + rectlWin.xRight - usCX) / 2;
    rectlWin.yBottom = (rectlWin.yBottom + rectlWin.yTop - usCY) / 2;
    rectlWin.xRight  = rectlWin.xLeft + usCX;
    rectlWin.yTop    = rectlWin.yBottom + usCY;
    WinCalcFrameRect( hwndFrame, &rectlWin, FALSE );
  }

  // Set new rectangle position and size (fit in desktop).

  // Calc horizontal position/size for the new rectangle.
  if ( ( rectlWin.xRight - rectlWin.xLeft ) > rectlDT.xRight )
  {
    rectlWin.xLeft = 0;
    rectlWin.xRight = rectlDT.xRight;
  }
  else if ( rectlWin.xLeft < rectlDT.xLeft )
    WinOffsetRect( hab, &rectlWin, rectlDT.xLeft - rectlWin.xLeft, 0 );
  else if ( rectlWin.xRight > rectlDT.xRight )
    WinOffsetRect( hab, &rectlWin, rectlDT.xRight - rectlWin.xRight, 0 );

  // Calc vertical position/size for the new rectangle.
  if ( ( rectlWin.yTop - rectlWin.yBottom ) > rectlDT.yTop )
  {
    rectlWin.yBottom = 0;
    rectlWin.yTop = rectlDT.yTop;
  }
  else if ( rectlWin.yBottom < rectlDT.yBottom )
    WinOffsetRect( hab, &rectlWin, 0, rectlDT.yBottom - rectlWin.yBottom );
  else if ( rectlWin.yTop > rectlDT.yTop )
    WinOffsetRect( hab, &rectlWin, 0, rectlDT.yTop - rectlWin.yTop );

  // Set window position and size.
  WinSetWindowPos( hwndFrame, HWND_TOP, rectlWin.xLeft, rectlWin.yBottom,
                   rectlWin.xRight - rectlWin.xLeft,
                   rectlWin.yTop - rectlWin.yBottom,
                   ulSWPFlags );

  if ( pWinData->hwndNotify != NULLHANDLE )
    WinPostMsg( pWinData->hwndNotify, ulWMVNCVNotify,
                MPFROMSHORT(VNCVNOTIFY_SETCLIENTSIZE),
                MPFROM2SHORT(usCX, usCY) );

  return (MRESULT)0;
}

static VOID _wmPaint(HWND hwnd)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  POINTL     aPoints[3];
  HPS        hps, hpsMem;

  hps = WinBeginPaint( hwnd, pWinData->hpsMicro, (PRECTL)&aPoints );
  if ( hps == NULLHANDLE )
  {
    debug( "WinBeginPaint() failed" );
    return;
  }

  aPoints[2].x = aPoints[0].x + pWinData->sHSlider;
  aPoints[2].y = aPoints[0].y + pWinData->sVSlider;

  hpsMem = ccGetHPS( pWinData->pCC );

  if ( GpiBitBlt( hps, hpsMem, 3, aPoints, ROP_SRCCOPY, 0 ) == GPI_ERROR )
    debug( "GpiBitBlt() failed" );

  ccReleaseHPS( pWinData->pCC, hpsMem );
  WinEndPaint( hps );
}

static VOID _wmMouse(HWND hwnd, POINTS ptPos, ULONG ulButton)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  SIZEL      sizeFrame;

  ptPos.x += pWinData->sHSlider;
  ptPos.y += pWinData->sVSlider;

  // Restrict movement of pointer with desktop's dimension.

  ccQueryFrameSize( pWinData->pCC, &sizeFrame );

  if ( ptPos.x < 0 )
    ptPos.x = 0;
  else if ( ptPos.x >= sizeFrame.cx )
    ptPos.x = sizeFrame.cx - 1;

  if ( ptPos.y < 0 )
    ptPos.y = 0;
  else if ( ptPos.y >= sizeFrame.cy )
    ptPos.y = sizeFrame.cy - 1;

  if ( ulButton == 0 )
  {
    // Set pointer when the mouse is moved.
    HPOINTER       hptr = ccQueryPointer( pWinData->pCC );

    if ( hptr != NULLHANDLE )
      WinSetPointer( HWND_DESKTOP, hptr );
  }
  else if ( (ulButton & ~RFBBUTTON_PRESSED) < RFBBUTTON_WHEEL_UP ) // Only "pressed" events for wheel.
  {
    // Capture the mouse while buttons pressed.
    if ( (ulButton & RFBBUTTON_PRESSED) != 0 )
    {
      if ( pWinData->lMouseButtonsDown == 0 )
        WinSetCapture( HWND_DESKTOP, hwnd );
      pWinData->lMouseButtonsDown++;
    }
    else
    {
      pWinData->lMouseButtonsDown--;
      if ( pWinData->lMouseButtonsDown == 0 )
        WinSetCapture( HWND_DESKTOP, NULLHANDLE );
    }
  }

  ccSendMouseEvent( pWinData->pCC, ptPos.x, ptPos.y, ulButton );
}

static VOID _wmChar(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

  ccSendWMCharEvent( pWinData->pCC, mp1, mp2 );
}

static VOID _wmSize(HWND hwnd, USHORT usCX, USHORT usCY)
{
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );

  if ( // Window not minimized now (style flag WS_MINIMIZED NOT set now).
       ( WinQueryWindowULong( hwndFrame, QWL_STYLE ) & WS_MINIMIZED ) == 0 )
  {
    // Scroll bars for the main window.

    PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
    HWND       hwndHScroll = WinWindowFromID( hwndFrame, FID_HORZSCROLL );
    HWND       hwndVScroll = WinWindowFromID( hwndFrame, FID_VERTSCROLL );
    RECTL      rectl;
    SIZEL      sizeFrame;

    ccQueryFrameSize( pWinData->pCC, &sizeFrame );

    // Create or destroy scroll bars.

    if ( ( usCX < sizeFrame.cx ) || ( usCY < sizeFrame.cy ) )
    {
      if ( hwndHScroll == NULLHANDLE )
      {
        hwndHScroll = WinCreateWindow( hwndFrame, WC_SCROLLBAR, NULL,
                                       SBS_HORZ | WS_VISIBLE, 0, 0, 10, 10,
                                       hwndFrame, HWND_TOP, FID_HORZSCROLL,
                                       NULL, NULL );

        if ( hwndVScroll == NULLHANDLE )
          hwndVScroll = WinCreateWindow( hwndFrame, WC_SCROLLBAR, NULL,
                                         SBS_VERT | WS_VISIBLE, 0, 0, 10, 10,
                                         hwndFrame, HWND_TOP, FID_VERTSCROLL,
                                         NULL, NULL );

        WinSendMsg( hwndFrame, WM_UPDATEFRAME, MPFROMLONG(FCF_BORDER), 0 );
      }
    }
    else if ( hwndHScroll != NULLHANDLE )
    {
      WinDestroyWindow( hwndHScroll );
      hwndHScroll = NULLHANDLE;
      pWinData->sHSlider = 0;

      WinDestroyWindow( hwndVScroll );
      hwndVScroll = NULLHANDLE;
      pWinData->sVSlider = 0;

      WinSendMsg( hwndFrame, WM_UPDATEFRAME, MPFROMLONG(FCF_BORDER), 0 );

      WinSetRect( hab, &rectl, 0, 0, sizeFrame.cx, sizeFrame.cy );
      WinCalcFrameRect( hwndFrame, &rectl, FALSE );
      WinSetWindowPos( hwndFrame, HWND_TOP, 0, 0,
                       rectl.xRight - rectl.xLeft, rectl.yTop - rectl.yBottom,
                       SWP_SIZE );
    }

    // Setup scroll bars.

    WinQueryWindowRect( hwnd, &rectl );
    usCX = rectl.xRight;
    usCY = rectl.yTop;

    if ( hwndHScroll != NULLHANDLE )
    {
      SHORT    sSBLastPos = sizeFrame.cx - rectl.xRight;

      if ( pWinData->sHSlider > sSBLastPos )
        pWinData->sHSlider = sSBLastPos;

      WinSendMsg( hwndHScroll, SBM_SETSCROLLBAR,
                  MPFROMSHORT(pWinData->sHSlider),
                  MPFROM2SHORT(0, sSBLastPos) );
      WinSendMsg( hwndHScroll, SBM_SETTHUMBSIZE,
                  MPFROM2SHORT(rectl.xRight, sizeFrame.cx), 0 );
    }

    if ( hwndVScroll != NULLHANDLE )
    {
      SHORT    sSBLastPos = sizeFrame.cy - rectl.yTop;

      if ( pWinData->sVSlider > sSBLastPos )
        pWinData->sVSlider = sSBLastPos;

      WinSendMsg( hwndVScroll, SBM_SETSCROLLBAR,
                  MPFROMSHORT(sSBLastPos - pWinData->sVSlider),
                  MPFROM2SHORT(0, sSBLastPos) );
      WinSendMsg( hwndVScroll, SBM_SETTHUMBSIZE,
                  MPFROM2SHORT(rectl.yTop, sizeFrame.cy), 0 );
    }
  } // if ( pSw == NULL )
}

static BOOL _wmScroll(HWND hwnd, USHORT usSBWinID, SHORT sSlider, USHORT usCmd)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  HWND       hwndScroll = WinWindowFromID( WinQueryWindow( hwnd, QW_PARENT ),
                                           usSBWinID );

  switch( usCmd ) // Slider commands.
  {
    case SB_SLIDERTRACK:
      break;

    case SB_ENDSCROLL:
      return TRUE;

    default:
    {
      sSlider = SHORT1FROMMR(WinSendMsg( hwndScroll, SBM_QUERYPOS, 0, 0 ));

      switch( usCmd ) // Line/page offset.
      {
        case SB_LINELEFT:
          sSlider -= 10;
          break;

        case SB_LINERIGHT:
          sSlider += 10;
          break;

        default:
        {
          RECTL   rectl;
          USHORT  usPageSize;

          WinQueryWindowRect( hwnd, &rectl );
          usPageSize = ( usSBWinID == FID_HORZSCROLL ?
                           rectl.xRight : rectl.yTop ) / 2;

          switch( usCmd ) // Page offset.
          {
            case SB_PAGELEFT:
              sSlider -= usPageSize;
              break;

            case SB_PAGERIGHT:
              sSlider += usPageSize;
              break;
          } // switch( usCmd ) - Page offset.
        }
      } // switch( usCmd ) - Line/page offset.
    }
  } // switch( usCmd ) - All commands.

  // Move slider to the new position and update window.
  WinSendMsg( hwndScroll, SBM_SETPOS, MPFROMSHORT(sSlider), 0 );

  // Store horizontal/vertical slider position for drawing in window.
  sSlider = SHORT1FROMMR(WinSendMsg( hwndScroll, SBM_QUERYPOS, 0, 0 ));
  if ( usSBWinID == FID_HORZSCROLL )
    pWinData->sHSlider = sSlider;
  else
    pWinData->sVSlider = SHORT2FROMMR( WinSendMsg( hwndScroll, SBM_QUERYRANGE,
                                       0, 0 ) ) - sSlider;

  // Update window.
  WinInvalidateRect( hwnd, NULL, FALSE );
  WinUpdateWindow( hwnd );
  return FALSE;
}

static VOID _wmVNCUpdate(HWND hwnd, PRECTL prectl)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

  WinOffsetRect( hab, prectl, -pWinData->sHSlider, -pWinData->sVSlider );
  WinInvalidateRect( hwnd, prectl, FALSE );
//  WinUpdateWindow( hwnd );
}

static VOID _openChatWin(HWND hwnd, BOOL fSendOpen)
{
  PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  LONG         cBytes;
  CHAR         acBuf[256];

  if ( fSendOpen && !ccSendChat( pWinData->pCC, CCCHAT_OPEN, NULL ) )
    return;

  pWinData->ulLastChatCmd = CCCHAT_OPEN;

  if ( pWinData->hwndChat != NULLHANDLE )
  {
    WinSetWindowPos( pWinData->hwndChat, HWND_TOP, 0, 0, 0, 0,
                     SWP_ZORDER | SWP_ACTIVATE | SWP_RESTORE );
    return;
  }

  pWinData->hwndChat = chatwinCreate( hwnd, WM_CHATWIN, NULL );
  if ( pWinData->hwndChat == NULLHANDLE )
    return;

  // Set local user name for chat.
  WinSendMsg( pWinData->hwndChat, WMCHAT_MESSAGE,
              MPFROMLONG(CWMT_LOCAL_NAME), APP_NAME );

  // Set chat window title.
  strcpy( acBuf, APP_NAME " - " );                           // "VNC Viewer - "
  cBytes = APP_NAME_LENGTH + 3 +
           WinQueryWindowText( pWinData->hwndChat,           // "Chat"
                               sizeof(acBuf) - APP_NAME_LENGTH - 6,
                               &acBuf[APP_NAME_LENGTH + 3] );
  *((PULONG)&acBuf[cBytes]) = 0x00202D20;                    // " - "
  cBytes += 3;
  ccQuerySessionInfo( pWinData->pCC, CCSI_SERVER_HOST,       // ip-address
                      sizeof(acBuf) - cBytes, &acBuf[cBytes] );
  WinSetWindowText( pWinData->hwndChat, acBuf );

  // Show chat window.
  WinShowWindow( pWinData->hwndChat, TRUE );
}

static VOID _wmVNCChat(HWND hwnd, ULONG ulCmd, PSZ pszText)
{
  PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  WMCHATSYS    stWMChatSys;
  WMCHATMSG    stWMChatMsg;
  CHAR         acBuf[255];

  switch( ulCmd )
  {
    case CCCHAT_OPEN:
      _openChatWin( hwnd, FALSE );
      break;

    case CCCHAT_CLOSE:
      if ( ( pWinData->hwndChat == NULLHANDLE ) ||
           ( pWinData->ulLastChatCmd == CCCHAT_CLOSE ) ) // Avoid dup. "closed" messages.
        break;

      stWMChatSys.cbText = WinLoadMessage( hab, NULLHANDLE,
                                       IDM_CHAT_CLOSED, sizeof(acBuf), acBuf );
      stWMChatSys.pcText = acBuf;
      stWMChatSys.fAllow = FALSE;        // Disable user input in chat window.

      WinSendMsg( pWinData->hwndChat, WMCHAT_MESSAGE,
                  MPFROMLONG(CWMT_SYSTEM), MPFROMP(&stWMChatSys) );
      break;

    case CCCHAT_MESSAGE:
      if ( pWinData->hwndChat == NULLHANDLE )
      {
        debugCP( "Chat window not exists" );
        break;
      }

      // Insert server's mssage.
      ccQuerySessionInfo( pWinData->pCC, CCSI_SERVER_HOST,
                          sizeof(acBuf), acBuf );
      stWMChatMsg.pszUser = acBuf;
      stWMChatMsg.pcText  = pszText;
      stWMChatMsg.cbText  = strlen( pszText );

      WinSendMsg( pWinData->hwndChat, WMCHAT_MESSAGE,
                  MPFROMLONG(CWMT_MSG_REMOTE), MPFROMP(&stWMChatMsg) );
      break;
  }

  pWinData->ulLastChatCmd = ulCmd;
}

static VOID _wmChatWin(HWND hwnd, ULONG ulType, PCWMSGDATA pCWMsgData)
{
  PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

  ccSendChat( pWinData->pCC,
              ulType == CWM_MESSAGE ? CCCHAT_MESSAGE : CCCHAT_CLOSE,
              pCWMsgData->pszText );

  if ( ulType == CWM_CLOSE )
    // User has close chat window.
    pWinData->hwndChat = NULLHANDLE;
}

static VOID _wmVNCClipboard(HWND hwnd, ULONG cbText, PSZ pszText)
{
  ULONG      ulRC;
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

  if ( !pWinData->fActivated )
    return;

  if ( pWinData->pszCBTextRecv != NULL )
    DosFreeMem( pWinData->pszCBTextRecv );

  ulRC = DosAllocSharedMem( (PPVOID)&pWinData->pszCBTextRecv, 0, cbText + 1,
                            PAG_COMMIT | PAG_READ | PAG_WRITE |
                            OBJ_GIVEABLE | OBJ_GETTABLE | OBJ_TILE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosAllocSharedMem() failed, rc = %u", ulRC );
    pWinData->pszCBTextRecv = NULL;
    return;
  }

  strcpy( pWinData->pszCBTextRecv, pszText );
}

static VOID _wmVNCFilexfer(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

  switch( SHORT1FROMMP(mp1) )
  {
    case CFX_PERMISSION_DENIED:
      {
        CHAR           acTitle[128];
        CHAR           acBuf[256];

        WinQueryWindowText( WinQueryWindow( hwnd, QW_PARENT ),
                            sizeof(acTitle) - 1, acTitle );
        WinLoadMessage( hab, 0, IDM_PERMISSION_DENIED, sizeof(acBuf), acBuf );
        WinMessageBox( HWND_DESKTOP, hwnd, acBuf, acTitle, 0,
                       MB_ICONHAND | MB_MOVEABLE | MB_OK | MB_APPLMODAL );
      }
      break;

    case CFX_DRIVES:
      if ( pWinData->hwndFX != NULLHANDLE )
        WinSetWindowPos( pWinData->hwndFX, HWND_TOP, 0, 0, 0, 0,
                         SWP_ZORDER | SWP_ACTIVATE | SWP_RESTORE );
      else
      {
        CHAR       acBuf[256];

        ccQuerySessionInfo( pWinData->pCC, CCSI_SERVER_HOST, sizeof(acBuf),
                            acBuf );
        pWinData->hwndFX = fxdlgOpen( hwnd, acBuf, pWinData->pCC );
        pWinData->fVOBeforeFX = !ccSetViewOnly( pWinData->pCC, CCVO_ON );
      }

      // Disable chat messages sending while File Transfer window opened.
      if ( pWinData->hwndChat != NULLHANDLE )
      {
        WMCHATSYS      stWMChatSys;

        stWMChatSys.fAllow = FALSE;
        stWMChatSys.cbText = 0;
        stWMChatSys.pcText = NULL;
        WinSendMsg( pWinData->hwndChat, WMCHAT_MESSAGE,
                    MPFROMLONG(CWMT_SYSTEM), MPFROMP(&stWMChatSys) );
      }
      break;
  }

  if ( pWinData->hwndFX != NULLHANDLE )
    // Redirect WM_VNC_FILEXFER message to the filexfer dialog.
    WinSendMsg( pWinData->hwndFX, WM_VNC_FILEXFER, mp1, mp2 );
}

static BOOL _wmApp2VNCVScreenshot(HWND hwnd, ULONG cbStorage, PVOID pStorage)
{
  PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  HPS          hpsMem;
  HBITMAP      hbmMem;
  PBITMAPINFO2 pDstBmInfo;
  PVOID        pDstBmBits;
  ULONG        ulRC;

  ulRC = DosGetSharedMem( pStorage, PAG_READ | PAG_WRITE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosGetSharedMem(), rc = %u", ulRC );
    return FALSE;
  }

  hpsMem = ccGetHPS( pWinData->pCC );
  hbmMem = GpiSetBitmap( hpsMem, NULLHANDLE );
  pDstBmInfo = (PBITMAPINFO2)pStorage;
  pDstBmBits = &((PBYTE)pStorage)[sizeof(BITMAPINFO2)];

  if ( hbmMem == HBM_ERROR )
  {
    debugCP( "GpiSetBitmap() failed" );
    ccReleaseHPS( pWinData->pCC, hpsMem );
    return FALSE;
  }

  GpiSetBitmap( hpsMem, hbmMem );

  memset( pDstBmInfo, 0, sizeof(BITMAPINFO2) );
  pDstBmInfo->cbFix = sizeof(BITMAPINFOHEADER2);
  if ( !GpiQueryBitmapInfoHeader( hbmMem, (PBITMAPINFOHEADER2)pDstBmInfo ) )
  {
    debugCP( "GpiQueryBitmapInfoHeader() failed" );
    ccReleaseHPS( pWinData->pCC, hpsMem );
    return FALSE;
  }

  if ( GpiQueryBitmapBits( hpsMem, 0, pDstBmInfo->cy, pDstBmBits, pDstBmInfo )
       == GPI_ALTERROR )
  {
    debugCP( "GpiQueryBitmapBits() failed" );
    ccReleaseHPS( pWinData->pCC, hpsMem );
    return FALSE;
  }

  ccReleaseHPS( pWinData->pCC, hpsMem );
  ulRC = DosFreeMem( pStorage );
  if ( ulRC != NO_ERROR )
    debug( "DosFreeMem(), rc = %u", ulRC );

  return TRUE;
}

static VOID _cmdScreenshot(HWND hwnd)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
  HDC        hdcSS = DevOpenDC( hab, OD_MEMORY, "*", 0, NULL, NULLHANDLE );
  HPS        hpsMem, hpsSS = NULLHANDLE;
  HBITMAP    hbmSS = NULLHANDLE;
  POINTL     aPoints[3];
  BOOL       fSuccess = FALSE;
  struct {
    BITMAPINFOHEADER2  stHdr;
    RGB2               argb2Color[0x100];
  }                  stBmInfo;
  PBITMAPINFO2       pbmi = NULL;

  hpsMem = ccGetHPS( pWinData->pCC );

  do
  {
    // Get current content (remote desktop) bitmap information.
    hbmSS = GpiSetBitmap( hpsMem, NULLHANDLE );
    GpiSetBitmap( hpsMem, hbmSS );
    memset( &stBmInfo, 0, sizeof(stBmInfo) );
    stBmInfo.stHdr.cbFix = sizeof(BITMAPINFOHEADER2);
    if ( !GpiQueryBitmapInfoHeader( hbmSS, &stBmInfo.stHdr ) )
    {
      debugCP( "GpiQueryBitmapInfoHeader()" );
      break;
    }
    hbmSS = NULLHANDLE;

    // Create a new memory presentation space.
    aPoints[1].x = stBmInfo.stHdr.cx;
    aPoints[1].y = stBmInfo.stHdr.cy;
    hpsSS = GpiCreatePS( hab, hdcSS, (PSIZEL)&aPoints[1],
                          PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC );
    if ( hpsSS == NULLHANDLE )
    {
      debug( "GpiCreatePS() failed. Memory PS was not created." );
      break;
    }

    // Create a system bitmap object

    stBmInfo.stHdr.cbFix   = sizeof(BITMAPINFOHEADER2);
    pbmi = (PBITMAPINFO2)&stBmInfo.stHdr;

    hbmSS = GpiCreateBitmap( hpsSS, &stBmInfo.stHdr, 0, NULL, pbmi );
    if ( ( hbmSS == GPI_ERROR ) || ( hbmSS == NULLHANDLE ) )
    {
      debugCP( "GpiCreateBitmap() failed" );
      hbmSS = NULLHANDLE;
      break;
    }

    if ( GpiSetBitmap( hpsSS, hbmSS ) == HBM_ERROR )
    {
      debug( "GpiSetBitmap(%u, %u) failed", hpsSS, hbmSS );
      return;
    }

    aPoints[0].x = 0;
    aPoints[0].y = 0;
    aPoints[2].x = 0;
    aPoints[2].y = 0;
    fSuccess = GpiBitBlt( hpsSS, hpsMem, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE )
                 != GPI_ERROR;
    ccReleaseHPS( pWinData->pCC, hpsMem );
    if ( !fSuccess )
      debug( "GpiBitBlt() failed" );
  }
  while( FALSE );

  // Release client's presentation space.
  ccReleaseHPS( pWinData->pCC, hpsMem );
  // Destroy temporary presentation space and close a memory device context.
  if ( hpsSS != NULLHANDLE )
  {
    GpiSetBitmap( hpsSS, NULLHANDLE );
    GpiDestroyPS( hpsSS );
  }
  DevCloseDC( hdcSS );


  if ( fSuccess )
  {
    // We have a bitmap hbmSS and now we put it in the clipboard.

    fSuccess = WinOpenClipbrd( hab );
    if ( !fSuccess )
      debug( "WinOpenClipbrd() failed." );
    else
    {
      WinEmptyClipbrd( hab );
      fSuccess = WinSetClipbrdData( hab, hbmSS, CF_BITMAP, CFI_HANDLE );
      if ( !fSuccess )
        debug( "WinSetClipbrdData() failed." );
      WinCloseClipbrd( hab );
    }
  }

  if ( !fSuccess && !GpiDeleteBitmap( hbmSS ) )
    debug( "GpiDeleteBitmap() failed" );
}

MRESULT EXPENTRY wndFrameProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_QUERYTRACKINFO:
      {
        HWND           hwndClient = WinWindowFromID( hwnd, FID_CLIENT );
        MRESULT        mr = oldWndFrameProc( hwnd, msg, mp1, mp2 );
        PTRACKINFO     pTrackInfo = (PTRACKINFO)PVOIDFROMMP(mp2);
        RECTL          rectl;
        PWINDATA       pWinData;

        if ( hwndClient == NULLHANDLE )
        {
          debug( "hwndClient == NULLHANDLE" );
          break;
        }

        pWinData = (PWINDATA)WinQueryWindowPtr( hwndClient, 0 );
        if ( pWinData == NULL )
        {
          debug( "pWinData == NULL" );
          break;
        }

        rectl.xLeft   = 0;
        rectl.yBottom = 0;
        ccQueryFrameSize( pWinData->pCC, (PSIZEL)&rectl.xRight );
        WinCalcFrameRect( hwnd, &rectl, FALSE );

        pTrackInfo->ptlMaxTrackSize.x = rectl.xRight - rectl.xLeft;
        pTrackInfo->ptlMaxTrackSize.y = rectl.yTop - rectl.yBottom;
        return mr;
      }
  }

  return oldWndFrameProc( hwnd, msg, mp1, mp2 );
}

MRESULT EXPENTRY wndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_CREATE:
      cOpenWin++;
      return _wmCreate( hwnd, mp1, mp2 );

    case WM_DESTROY:
      _wmDestroy( hwnd );
      cOpenWin--;
      break;

    case WM_CLOSE:
      _wmClose( hwnd );
      break;
//      return (MRESULT)FALSE;

    case WM_MOUSEMOVE:
      _wmMouse( hwnd, *((POINTS *)&mp1), 0 );
      return (MRESULT)TRUE;

    case WM_TRANSLATEACCEL:
      // ALT and acceleration keys not allowed while not view-only mode (must
      // be processed in WM_CHAR).
      if ( ( mp1 != NULL ) && ( ((PQMSG)mp1)->msg == WM_CHAR ) )
      {
        PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

        if ( !ccQueryViewOnly( pWinData->pCC ) )
          return (MRESULT)FALSE;
      }
      break;

    case WM_CHAR:
      _wmChar( hwnd, mp1, mp2 );
      break;

    case WM_ACTIVATE:
      {
        PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

        pWinData->fActivated = SHORT1FROMMP(mp1) != 0;

        if ( SHORT1FROMMP(mp1) != 0 )
        {
          // The window is activated. Check local clipboard and send text to
          // the server if necessary.

          // Send events for keys released while the window has been inactive.
          ccSendWMCharEvent( pWinData->pCC, 0, 0 );

          if ( WinQueryClipbrdOwner( hab ) != hwnd )
          {
            if ( WinIsWindowVisible( hwnd ) )
            {
              // It seems the clipboard data was changed...

              if ( !WinOpenClipbrd( hab ) )
                debug( "WinOpenClipbrd() failed" );
              else
              {
                PSZ    pszText = (PSZ)WinQueryClipbrdData( hab, CF_TEXT );

                if ( pszText != NULL )
                  pszText = strdup( pszText );
                WinCloseClipbrd( hab );

                if ( pszText != NULL )
                {
                  ccSendClipboardText( pWinData->pCC, pszText );
                  free( pszText );
                }
              }
            }
          }
        }
        else if ( pWinData->pszCBTextRecv != NULL )
        {
          // The window is deactivated and we have a text for local clipboard.

          BOOL                 fSuccess;

          if ( !WinOpenClipbrd( hab ) )
          {
            debug( "WinOpenClipbrd() failed" );
            fSuccess = FALSE;
          }
          else
          {    
            WinEmptyClipbrd( hab );

            fSuccess = WinSetClipbrdData( hab, (ULONG)pWinData->pszCBTextRecv,
                                          CF_TEXT, CFI_POINTER );
            if ( !fSuccess )
              debug( "WinOpenClipbrd() failed" );

            WinCloseClipbrd( hab );
          }

          if ( !fSuccess )
            DosFreeMem( pWinData->pszCBTextRecv );
          pWinData->pszCBTextRecv = NULL;

          WinSetClipbrdOwner( hab, hwnd );
        }
      }
      break;

    case WM_SIZE:
      _wmSize( hwnd, SHORT1FROMMP(mp2), SHORT2FROMMP(mp2) );
      break;

    case WM_BUTTON1DOWN:
    case WM_BUTTON1DBLCLK:
      _wmMouse( hwnd, *((POINTS *)&mp1), RFBBUTTON_LEFT | RFBBUTTON_PRESSED );
      break;

    case WM_BUTTON1UP:
      _wmMouse( hwnd, *((POINTS *)&mp1), RFBBUTTON_LEFT );
      break;

    case WM_BUTTON2DOWN:
    case WM_BUTTON2DBLCLK:
      _wmMouse( hwnd, *((POINTS *)&mp1), RFBBUTTON_RIGHT | RFBBUTTON_PRESSED );
      break;

    case WM_BUTTON2UP:
      _wmMouse( hwnd, *((POINTS *)&mp1), RFBBUTTON_RIGHT );
      break;

    case WM_BUTTON3DOWN:
    case WM_BUTTON3DBLCLK:
      _wmMouse( hwnd, *((POINTS *)&mp1), RFBBUTTON_MIDDLE | RFBBUTTON_PRESSED );
      break;

    case WM_BUTTON3UP:
      _wmMouse( hwnd, *((POINTS *)&mp1), RFBBUTTON_MIDDLE );
      break;

    case WM_MOUSEWHEEL_VERT:     // Amouse event.
      {
        POINTL         pointl;

        // Map pointer position from desktop coordinates to window coordinates.
        pointl.x = SHORT1FROMMP(mp2);
        pointl.y = SHORT2FROMMP(mp2);
        WinMapWindowPoints( HWND_DESKTOP, hwnd, &pointl, 1 );
        ((POINTS *)&mp2)->x = pointl.x;
        ((POINTS *)&mp2)->y = pointl.y;

        /*debug( "WM_MOUSEWHEEL_VERT fwKey: %x; turns: %d, x=%d, y=%d",
               SHORT1FROMMP(mp1), SHORT2FROMMP(mp1),
               SHORT1FROMMP(mp2), SHORT2FROMMP(mp2) );*/
        _wmMouse( hwnd, *((POINTS *)&mp2),
                    (SHORT2FROMMP(mp1) & 0x8000) != 0
                      ? (RFBBUTTON_WHEEL_UP | RFBBUTTON_PRESSED)
                      : (RFBBUTTON_WHEEL_DOWN | RFBBUTTON_PRESSED) );
      }
      return (MRESULT)FALSE;

    case WM_PAINT:
      _wmPaint( hwnd );
      return (MRESULT)FALSE;

    case WM_VSCROLL:
    case WM_HSCROLL:
      if ( _wmScroll( hwnd, SHORT1FROMMP(mp1), SHORT1FROMMP(mp2),
                      SHORT2FROMMP(mp2) ) )
        return (MRESULT)TRUE;
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case CWC_NEW_CONNECTION:
          lpOpenDlg();
          return (MRESULT)TRUE;

        case CWC_VIEW_ONLY:
          {
            PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

            ccSetViewOnly( pWinData->pCC, CCVO_TOGGLE );
          }
          return (MRESULT)TRUE;

        case CWC_SEND_CAD:
          {
            PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
            static ULONG           aulKeySeq[] =
              { XK_Control_L | CCKEY_PRESS, XK_Alt_L | CCKEY_PRESS,
                XK_Delete | CCKEY_PRESS, XK_Delete, XK_Alt_L, XK_Control_L };

            ccSendKeySequence( pWinData->pCC, ARRAYSIZE(aulKeySeq), aulKeySeq );
          }
          return (MRESULT)TRUE;

        case CWC_SEND_CTRL_ESC:
          {
            PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
            static ULONG           aulKeySeq[] =
              { XK_Control_L | CCKEY_PRESS, XK_Escape | CCKEY_PRESS,
                XK_Escape, XK_Control_L };

            ccSendKeySequence( pWinData->pCC, ARRAYSIZE(aulKeySeq), aulKeySeq );
          }
          return (MRESULT)TRUE;

        case CWC_SEND_ALT_ESC:
          {
            PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
            static ULONG           aulKeySeq[] =
              { XK_Alt_L | CCKEY_PRESS, XK_Escape | CCKEY_PRESS,
                XK_Escape, XK_Alt_L };

            ccSendKeySequence( pWinData->pCC, ARRAYSIZE(aulKeySeq), aulKeySeq );
          }
          return (MRESULT)TRUE;

        case CWC_SEND_ALT_TAB:
          {
            PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );
            static ULONG           aulKeySeq[] =
              { XK_Alt_L | CCKEY_PRESS, XK_Tab | CCKEY_PRESS,
                XK_Tab, XK_Alt_L };

            ccSendKeySequence( pWinData->pCC, ARRAYSIZE(aulKeySeq), aulKeySeq );
          }
          return (MRESULT)TRUE;

        case CWC_CHAT:
          _openChatWin( hwnd, TRUE );
          return (MRESULT)TRUE;

        case CWC_FILE_TRANSFER:
          {
            PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

            if ( pWinData->hwndFX != NULLHANDLE )
              WinSetWindowPos( pWinData->hwndFX, HWND_TOP, 0, 0, 0, 0,
                               SWP_ZORDER | SWP_ACTIVATE | SWP_RESTORE );
            else
            {
              // Drive list request. Result will be returned in WM_VNC_FILEXFER:
              // mp1, SHORT 1 = CFX_DRIVES .
              ccFXRequestFileList( pWinData->pCC, NULL );
            }
          }
          return (MRESULT)TRUE;

        case CWC_FILE_TRANSFER_CLOSE:
          {
            // Command from file transfer dialog.
            PWINDATA     pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, 0 );

            if ( pWinData->hwndFX != NULLHANDLE )
            {
              WinDestroyWindow( pWinData->hwndFX );
              pWinData->hwndFX = NULLHANDLE;
              ccSetViewOnly( pWinData->pCC,
                             pWinData->fVOBeforeFX ? CCVO_ON : CCVO_OFF );
            }

            // Disable chat messages sending.
            if ( pWinData->hwndChat != NULLHANDLE )
            {
              WMCHATSYS          stWMChatSys;

              stWMChatSys.fAllow = TRUE;
              stWMChatSys.cbText = 0;
              stWMChatSys.pcText = NULL;
              WinSendMsg( pWinData->hwndChat, WMCHAT_MESSAGE,
                          MPFROMLONG(CWMT_SYSTEM), MPFROMP(&stWMChatSys) );
            }
          }
          return (MRESULT)TRUE;

        case CWC_SCREENSHOT:
          _cmdScreenshot( hwnd );
          return (MRESULT)TRUE;
      }
      break;

    case _WM_MENUSHOW:           // Window menu becomes visible,
      _cwTuneMenu( hwnd );       // posted from _menuWinProc().
      return (MRESULT)0;

    case WM_VNC_STATE:
      _wmVNCState( hwnd, LONGFROMMP(mp2) );
      return (MRESULT)TRUE;

    case _WM_INITCLNTWND:
      return _wmInitClntWnd( hwnd, (PINITCLNTWND)mp1 );

    case WM_VNC_SETCLIENTSIZE:
      return _wmVNCSetClientSize( hwnd, SHORT1FROMMP(mp1), SHORT2FROMMP(mp1) );

    case WM_VNC_UPDATE:
      {
        RECTL          rectl;

        rectl.xLeft   = SHORT1FROMMP(mp1);
        rectl.yBottom = SHORT2FROMMP(mp1);
        rectl.xRight  = SHORT1FROMMP(mp2);
        rectl.yTop    = SHORT2FROMMP(mp2);
        _wmVNCUpdate( hwnd, &rectl );
      }
      return (MRESULT)TRUE;

    case WM_VNC_CHAT:
      // Chat message from server.
      _wmVNCChat( hwnd, LONGFROMMP(mp1), (PSZ)mp2 );
      return (MRESULT)TRUE;

    case WM_CHATWIN:
      // Chat message from chatwin module.
      _wmChatWin( hwnd, LONGFROMMP(mp1), (PCWMSGDATA)mp2 );
      return (MRESULT)TRUE;

    case WM_VNC_CLIPBOARD:
      _wmVNCClipboard( hwnd, LONGFROMMP(mp1), (PSZ)mp2 );
      return (MRESULT)TRUE;

    case WM_VNC_FILEXFER:
      _wmVNCFilexfer( hwnd, mp1, mp2 );
      return (MRESULT)TRUE;

    case WM_APP2VNCV_SCREENSHOT:
      return (MRESULT)_wmApp2VNCVScreenshot( hwnd, LONGFROMMP(mp1), PVOIDFROMMP(mp2) );
/*
    case WM_APP2VNCV_SCREENSHOT:
      return (MRESULT)_wmApp2VNCVScreenshot( hwnd, SHORT1FROMMP(mp2), SHORT2FROMMP(mp2) );
*/
  }

  return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}


BOOL cwCreate(PCLNTCONN pCC, PSZ pszHost, PSZ pszWinTitle, HWND hwndNotify)
{
  HWND                 hwnd;
  HWND                 hwndFrame;
  ULONG	               ulFrameFlags = FCF_TITLEBAR | FCF_SYSMENU | FCF_ICON |
                                 FCF_MINBUTTON | FCF_SIZEBORDER | FCF_TASKLIST;
  CHAR                 acBuf[128];
  INITCLNTWND          stInitClntWnd;

  WinRegisterClass( hab, _WIN_CLIENT_CLASS, wndProc,
                    CS_SIZEREDRAW/* | CS_SYNCPAINT*/,
                    sizeof(PWINDATA) );

  if ( ( pszWinTitle == NULL ) || ( pszWinTitle[0] == '\0' ) )
  {
    // Make window title.
    strcpy( acBuf, APP_NAME " - " );
    ccQuerySessionInfo( pCC, CCSI_DESKTOP_NAME,
                        sizeof(acBuf) - APP_NAME_LENGTH - 3,
                        &acBuf[APP_NAME_LENGTH + 3] );
    pszWinTitle = acBuf;
  }

  hwndFrame = WinCreateStdWindow( HWND_DESKTOP, 0, &ulFrameFlags,
                                  _WIN_CLIENT_CLASS, pszWinTitle, 0, 0,
                                  IDICON_VIEWWIN, &hwnd );
  if ( hwndFrame == NULLHANDLE )
  {
    debug( "WinCreateStdWindow() failed" );
    return FALSE;
  }

  // Subclass frame window to control size changes.
  oldWndFrameProc = WinSubclassWindow( hwndFrame, wndFrameProc );

  stInitClntWnd.pCC        = pCC;
  stInitClntWnd.pszHost    = pszHost;
  stInitClntWnd.hwndNotify = hwndNotify;
  if ( !(BOOL)WinSendMsg( hwnd, _WM_INITCLNTWND, MPFROMP(&stInitClntWnd), 0 ) )
  {
    WinDestroyWindow( hwndFrame );
    return FALSE;
  }

  ccSetWindow( pCC, hwnd );
  WinShowWindow( hwndFrame, TRUE );
  return TRUE;
}

VOID cwInit()
{
  ulWMVNCVNotify = WinAddAtom( WinQuerySystemAtomTable(),
                               VNCVI_ATOM_WMVNCVNOTIFY );
}

VOID cwDone()
{
  WinDeleteAtom( WinQuerySystemAtomTable(), ulWMVNCVNotify );
}