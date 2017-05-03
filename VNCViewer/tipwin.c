#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <string.h>
#include <ctype.h>
#include <utils.h>
#include <debug.h>
#include "tipwin.h"

#define _CTL_ARRAY_DELTA         8
#define _TIP_CLASS_NAME          "DigiTip"
#define _TIP_TIMER_ID            1
#define _TIP_TIMER_TIMEOUT       700

// Message from subclassed control to tip window.
#define WMTIPCTL_MOUSEMOVE       (WM_USER + 2)

// Vert. distance from pointer position to tip window when tip below pointer.
#define HINT_BOTTOM_OFFS		17
// Vert. distance from pointer position to tip window when tip above pointer.
#define HINT_TOP_OFFS			10

#define HINT_TEXT_LPAD			2
#define HINT_TEXT_RPAD			3
#define HINT_TEXT_BPAD			1
#define HINT_TEXT_TPAD			0

//#define HINT_USE_STD_COLORS
// Values HINT_CLR_*  will be used only if HINT_USE_STD_COLORS is not specified.
#define HINT_CLR_TEXT			0x00000050
#define HINT_CLR_BACKGROUND		0x00FFFFE0
#define HINT_CLR_BORDER			0x00505050

typedef struct _TIPCTL {
  HWND       hwnd;
  PFNWP      fnWinProcOrg;
} TIPCTL, *PTIPCTL;

typedef struct _TIPWINDATA {
  POINTL     ptMouse;
  HWND       hwndMouse;
  ULONG      ulTimerId;
  BOOL       fReady;
  PSZ        pszText;
} TIPWINDATA, *PTIPWINDATA;

static PTIPCTL         paTipCtl    = NULL;
static ULONG           cTipCtl     = 0;
static ULONG           ulTipCtlMax = 0;
static HWND            hwndTip     = NULLHANDLE;


#define MOUSE_POS_CHANGED(_tipwindata,_ptmouse) \
  ( ( abs( _tipwindata->ptMouse.x - _ptmouse.x ) > \
       (WinQuerySysValue( HWND_DESKTOP, SV_CXDBLCLK ) / 2) ) || \
    ( abs( _tipwindata->ptMouse.y - _ptmouse.y ) > \
       (WinQuerySysValue( HWND_DESKTOP, SV_CYDBLCLK ) / 2) ) )


// Returns frame window handle where control hwnd is placed,
static HWND _tipQueryCtlFrame(HWND hwnd)
{
  CHAR       acBuf[256];

  do
  {
    hwnd = WinQueryWindow( hwnd, QW_OWNER );
    if ( hwnd == NULLHANDLE )
      break;
    WinQueryClassName( hwnd, sizeof(acBuf), acBuf );
  }
  while( strcmp( acBuf, "#1" ) != 0 );

  return hwnd;
}

// Returns window Id for hwnd. If hwnd is entry field of combobox it returns
// combobox Id.
static USHORT _tipQueryCtlId(HWND hwnd)
{
  CHAR       acBuf[256];

  // Window class #6, owner class #2 - entry field in combobox.
  WinQueryClassName( hwnd, sizeof(acBuf), acBuf );
  if ( strcmp( acBuf, "#6" ) == 0 )
  {
    HWND     hwndOwner = WinQueryWindow( hwnd, QW_OWNER );

    WinQueryClassName( hwndOwner, sizeof(acBuf), acBuf );
    if ( strcmp( acBuf, "#2" ) == 0 )
      hwnd = hwndOwner;
  }

  return WinQueryWindowUShort( hwnd, QWS_ID );
}

static VOID _tipDrawText(HPS hps, PRECTL prctlText, PSZ pszText, BOOL fReqSize)
{
  LONG    	lRC = 1;
  LONG		lSaveYTop = prctlText->yTop;
  ULONG		cbText = strlen( pszText );
  LONG		lMinLeft = prctlText->xRight;
  LONG		lMaxRight = prctlText->xLeft;
  RECTL		rctlDraw;
  FONTMETRICS   stFontMetrics;
  ULONG		flCmd;
  ULONG		ulLineCY;

  GpiQueryFontMetrics( hps, sizeof(FONTMETRICS), &stFontMetrics );
  ulLineCY = stFontMetrics.lMaxBaselineExt + stFontMetrics.lExternalLeading;

  if ( fReqSize )
    flCmd = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_QUERYEXTENT;
  else
#ifdef HINT_USE_STD_COLORS
    flCmd = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_TEXTATTRS;
#else
    flCmd = DT_LEFT | DT_TOP | DT_WORDBREAK;
#endif

  while ( ( lRC != 0 ) && ( cbText > 0 ) )
  {
    memcpy( &rctlDraw, prctlText, sizeof(RECTL) );
    lRC = WinDrawText( hps, cbText, pszText, &rctlDraw,
                       HINT_CLR_TEXT, HINT_CLR_BACKGROUND, flCmd );
    if ( lRC == 0 )
      break;

    pszText += lRC;
    cbText -= lRC;

    if ( rctlDraw.xLeft < lMinLeft )
      lMinLeft = rctlDraw.xLeft;
    if ( rctlDraw.xRight > lMaxRight )
      lMaxRight = rctlDraw.xRight;

    prctlText->yTop -= ulLineCY;
  }

  prctlText->xLeft   = lMinLeft;
  prctlText->xRight  = lMaxRight;
  prctlText->yBottom = prctlText->yTop;
  prctlText->yTop    = lSaveYTop;
}


static BOOL _wmCreate(HWND hwnd)
{
  PTIPWINDATA          pWinData = calloc( 1, sizeof(TIPWINDATA) );

  if ( pWinData == NULL )
    return FALSE;

  WinSetPresParam( hwnd, PP_FONTNAMESIZE, 7, "8.Helv" );
  WinSetWindowPtr( hwnd, 0, pWinData );
  return TRUE;
}

static VOID _wmDestroy(HWND hwnd)
{
  PTIPWINDATA          pWinData = (PTIPWINDATA)WinQueryWindowPtr( hwnd, 0 );

  if ( pWinData == NULL )
    return;

  if ( pWinData->ulTimerId != 0 )
    WinStopTimer( WinQueryAnchorBlock( hwnd ), hwnd, pWinData->ulTimerId );

  if ( pWinData->pszText != NULL )
    free( pWinData->pszText );

  free( pWinData );
}

static VOID _wmTimer(HWND hwnd, SHORT sTimerId)
{
  PTIPWINDATA          pWinData = (PTIPWINDATA)WinQueryWindowPtr( hwnd, 0 );
  HAB                  hab = WinQueryAnchorBlock( hwnd );
  POINTL               ptMouse;
  HWND                 hwndMouse;

  if ( ( pWinData == NULL ) || ( pWinData->ulTimerId != sTimerId ) ||
       ( pWinData->hwndMouse == NULLHANDLE ) )
    return;

  WinQueryPointerPos( HWND_DESKTOP, &ptMouse );
  if ( MOUSE_POS_CHANGED( pWinData, ptMouse ) ||
       ( pWinData->hwndMouse != 
           WinWindowFromPoint( HWND_DESKTOP, &pWinData->ptMouse, TRUE ) ) )
    // Mouse moved or window where mouse stopped dissapeared.
    hwndMouse = NULLHANDLE;
  else
    hwndMouse = pWinData->hwndMouse;

  if ( pWinData->fReady )
  {
    // Tip window have been ready to show or visible.

    if ( hwndMouse == NULLHANDLE )
    {
      // Mouse has left control window.

      // Send "not ready" signal to the control window.
      WinSendMsg( _tipQueryCtlFrame( pWinData->hwndMouse ), WM_TIP,
                  MPFROM2SHORT( _tipQueryCtlId( pWinData->hwndMouse ), 0 ),
                  MPFROMHWND( pWinData->hwndMouse ) );

      pWinData->fReady = FALSE;
      WinStopTimer( hab, hwnd, pWinData->ulTimerId );
      pWinData->ulTimerId = 0;
      WinShowWindow( hwnd, FALSE );
    }
  }
  else
  {
    // Tip window waits timeout for "ready" status.

    if ( hwndMouse == NULLHANDLE )
    {
      // Mouse moved or has left control window. Cancel timeout to "ready"
      // status.
      WinStopTimer( hab, hwnd, pWinData->ulTimerId );
      pWinData->ulTimerId = 0;
    }
    else
    {
      // Timeout passed without mouse moves. Go to "ready" status.
      pWinData->fReady = TRUE;
      // Send "ready" signal to the control window. Now it can set tip text.
      WinSendMsg( _tipQueryCtlFrame( hwndMouse ), WM_TIP,
                  MPFROM2SHORT( _tipQueryCtlId( hwndMouse ), 1 ),
                  MPFROMHWND( hwndMouse ) );
    }
  }

  pWinData->hwndMouse = hwndMouse;
}

static VOID _wmPaint(HWND hwnd)
{
  PTIPWINDATA          pWinData = (PTIPWINDATA)WinQueryWindowPtr( hwnd, 0 );
  RECTL                rectl;
  HPS                  hps;

  WinQueryWindowRect( hwnd, &rectl );

  hps = WinBeginPaint( hwnd, 0, NULL );
#ifndef HINT_USE_STD_COLORS
  GpiCreateLogColorTable( hps, 0, LCOLF_RGB, 0, 0, NULL );
#endif

  WinDrawBorder( hps, &rectl, 1, 1,
                 HINT_CLR_BORDER, HINT_CLR_BACKGROUND,
                 DB_INTERIOR
#ifdef HINT_USE_STD_COLORS
                 | DB_AREAATTRS
#endif
               );

  if ( pWinData->pszText != NULL )
  {
    // Correct window rectangle with borders and paddings
    rectl.xLeft += 1 + HINT_TEXT_LPAD;
    rectl.xRight -= 1 + HINT_TEXT_RPAD;
    rectl.yBottom += 1 + HINT_TEXT_BPAD;
    rectl.yTop -= 1 + HINT_TEXT_TPAD;
    // Draw text lines
    _tipDrawText( hps, &rectl, pWinData->pszText, FALSE );
  }

  WinEndPaint( hps );
}

static VOID _wmWMTipCtlMouseMove(HWND hwnd, SHORT sX, SHORT sY, HWND hwndCtl)
{
  PTIPWINDATA          pWinData = (PTIPWINDATA)WinQueryWindowPtr( hwnd, 0 );
  HAB                  hab = WinQueryAnchorBlock( hwnd );
  POINTL               ptMouse;

  if ( pWinData == NULL )
    return;

  ptMouse.x = sX;
  ptMouse.y = sY;
  WinMapWindowPoints( hwndCtl, HWND_DESKTOP, &ptMouse, 1 );

  if ( !MOUSE_POS_CHANGED( pWinData, ptMouse ) &&
       ( pWinData->hwndMouse == hwndCtl ) )
    return;

  if ( pWinData->fReady )
  {
    pWinData->fReady = FALSE;
    // Send "not ready" signal to the control window's owner.
    WinSendMsg( _tipQueryCtlFrame( pWinData->hwndMouse ), WM_TIP,
                MPFROM2SHORT( _tipQueryCtlId( pWinData->hwndMouse ), 0 ),
                MPFROMHWND( pWinData->hwndMouse ) );
  }

  pWinData->ptMouse = ptMouse;
  pWinData->hwndMouse = hwndCtl;

  WinShowWindow( hwnd, FALSE );

  if ( pWinData->ulTimerId != 0 )
    WinStopTimer( hab, hwnd, pWinData->ulTimerId );

  pWinData->ulTimerId = WinStartTimer( hab, hwnd, _TIP_TIMER_ID,
                                       _TIP_TIMER_TIMEOUT );
}

// Set a new text.
static BOOL _wmTipText(HWND hwnd, PCHAR pcText, SHORT cbText,
                       BOOL fStringTbl)
// pcText - tip text.
// cbText - tip text length if pcText not a NULL or resource message table ID.
// fStringTbl - use resource string table instead message table.
{
  PTIPWINDATA          pWinData = (PTIPWINDATA)WinQueryWindowPtr( hwnd, 0 );
  POINTL               ptMouse;
  RECTL                rclWnd = { 0, 0, 0, 0 };
  HPS                  hps;
  LONG                 lX, lY;
  ULONG                ulCX, ulCY;
  CHAR                 acBuf[256];
  CHAR                 acText[320];
  PCHAR                pCh;

  if ( ( pWinData == NULL ) || !pWinData->fReady )
    return FALSE;

  if ( pcText != NULL )
  {
    // Get text from message.
    BUF_SKIP_SPACES( cbText, pcText );
    BUF_RTRIM( cbText, pcText );
    if ( cbText >= sizeof(acBuf) )
      cbText = sizeof(acBuf) - 1;

    memcpy( acBuf, pcText, cbText );
    acBuf[cbText] = '\0';
  }
  else
  {
    // Load text from the resource.
    HAB      hab = WinQueryAnchorBlock( hwnd );

    cbText = fStringTbl
               ? WinLoadString( hab, 0, cbText, sizeof(acBuf), acBuf )
               : WinLoadMessage( hab, 0, cbText, sizeof(acBuf), acBuf );
  }

  pcText = acBuf;
  BUF_RTRIM( cbText, pcText );
  if ( cbText == 0 )
  {
    debug( "Text an empty" );
    return FALSE;
  }

  acBuf[cbText] = '\0';
  pCh = acBuf;
  while( (pCh = strchr( pCh, '\1' )) != NULL )
    *pCh = '\n';

  cbText = WinSubstituteStrings( _tipQueryCtlFrame( pWinData->hwndMouse ),
                                 acBuf, sizeof(acText), acText );
  if ( cbText == 0 )
  {
    debug( "WinSubstituteStrings() failed" );
    return FALSE;
  }

  // Store user's text.

  if ( pWinData->pszText != NULL )
    free( pWinData->pszText );

  pWinData->pszText = strdup( acText );
  if ( pWinData->pszText == NULL )
    return FALSE;
  
  // Calculating size of the tip window.

  WinQueryPointerPos( HWND_DESKTOP, &ptMouse );

  // Request text size
  rclWnd.xRight = ( WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN ) / 2 ) - 1;
  rclWnd.yTop   = WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN ) - 10;
  hps = WinGetPS( hwnd );
  _tipDrawText( hps, &rclWnd, acText, TRUE );
  WinReleasePS( hps );
  // Window size is text size + paddings + border
  ulCX = (rclWnd.xRight - rclWnd.xLeft) + HINT_TEXT_LPAD + HINT_TEXT_RPAD + 2;
  ulCY = (rclWnd.yTop - rclWnd.yBottom) + HINT_TEXT_BPAD + HINT_TEXT_TPAD + 2;

  // Calc window position

  lX = pWinData->ptMouse.x - ulCX / 2;
  if ( lX <= 0 )
    lX = 1;
  else
  {
    LONG	ulSrcCX = WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN );

    if ( (lX + ulCX) >= ulSrcCX )
      lX = ulSrcCX - ulCX;
  }

  lY = (pWinData->ptMouse.y - ulCY) - HINT_BOTTOM_OFFS;
  if ( lY <= 0 )
    lY = pWinData->ptMouse.y + HINT_TOP_OFFS;

  return WinSetWindowPos( hwnd, HWND_TOP, lX, lY, ulCX, ulCY,
                          SWP_ZORDER | SWP_SHOW | SWP_MOVE | SWP_SIZE );
}

static MRESULT EXPENTRY _tipWinProc(HWND hwnd, ULONG msg,
                                    MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_CREATE:
      return (MRESULT)!_wmCreate( hwnd );

    case WM_DESTROY:
      _wmDestroy( hwnd );
      break;

    case WM_TIMER:
      _wmTimer( hwnd, SHORT1FROMMP(mp1) );
      return (MRESULT)0;

    case WM_PAINT:
      _wmPaint( hwnd );
      return (MRESULT)0;

    case WM_HITTEST:
      return (MRESULT)HT_TRANSPARENT;

    case WMTIPCTL_MOUSEMOVE:
      // Mouse moves over the control.
      _wmWMTipCtlMouseMove( hwnd, SHORT1FROMMP(mp1), SHORT2FROMMP(mp1),
                            HWNDFROMMP(mp2) );
      return (MRESULT)TRUE;

    case WM_TIP_TEXT:
      // Usert sets a new tip text.
      // mp1: PCHAR - tip text or NULL.
      // mp2:
      //   SHORT - mp1 not a NULL - tip text length,
      //           mp1 is NULL - resource message table ID (mp1 != NULL).
      //   SHORT - NOT ZERO - use resource string table instead message table.
      return (MRESULT)_wmTipText( hwnd, (PCHAR)mp1, SHORT1FROMMP(mp2),
                                  SHORT2FROMMP(mp2) != 0 );
  }

  return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}

static VOID _tipCreateWin(HAB hab)
{
  WinRegisterClass( hab, _TIP_CLASS_NAME, _tipWinProc, CS_HITTEST,
                    sizeof(PTIPWINDATA) );
  hwndTip = WinCreateWindow( HWND_DESKTOP, _TIP_CLASS_NAME, NULL, 0,
                   10, 10, 50, 50, HWND_DESKTOP, HWND_TOP, 0, NULL, NULL );
}

static VOID _tipDestroyWin()
{
  if ( hwndTip != NULLHANDLE )
  {
    WinDestroyWindow( hwndTip );
    hwndTip = NULLHANDLE;
  }
}

static MRESULT EXPENTRY _ctlWinProc(HWND hwnd, ULONG msg,
                                    MPARAM mp1, MPARAM mp2)
{
  ULONG    ulTipIdx;
  PTIPCTL  pTipCtl = NULL;
  PFNWP    fnWinProcOrg = WinDefWindowProc;

  for( ulTipIdx = 0; ulTipIdx < cTipCtl; ulTipIdx++ )
  {
    if ( paTipCtl[ulTipIdx].hwnd == hwnd )
    {
      pTipCtl = &paTipCtl[ulTipIdx];
      fnWinProcOrg = pTipCtl->fnWinProcOrg;
      break;
    }
  }

  switch( msg )
  {
    case WM_DESTROY:
      if ( pTipCtl != NULL )
      {
        cTipCtl--;

        if ( cTipCtl == 0 )
        {
          // No registered controls left.
          free( paTipCtl );
          paTipCtl = NULL;
          cTipCtl = 0;
          ulTipCtlMax = 0;
          _tipDestroyWin();
        }
        else
          paTipCtl[ulTipIdx] = paTipCtl[cTipCtl];
      }
      break;

    case WM_MOUSEMOVE:
      if ( hwndTip != NULLHANDLE )
        WinSendMsg( hwndTip, WMTIPCTL_MOUSEMOVE, mp1, MPFROMHWND(hwnd) );
      break;

    case WM_HITTEST:
      break;
  }


  return fnWinProcOrg( hwnd, msg, mp1, mp2 );
}

// VOID twSet(HWND hwnd)
// Sets tip support for all controls in window (hwnd is parent for controls).
// Controls owner will receive messages WM_TIP and can set tip text with
// WM_TIP_TEXT message to the twQueryTipWinHandle() window to show the tip.
VOID twSet(HWND hwnd)
{
  HENUM                henum;
  HWND                 hwndEnum;
  CHAR                 acBuf[64];

  henum = WinBeginEnumWindows( hwnd );
  while( ( hwndEnum = WinGetNextWindow( henum ) ) != NULLHANDLE )
  {
    WinQueryClassName( hwndEnum, sizeof(acBuf), acBuf );
    if ( ( strcmp( acBuf, "#4" ) == 0 ) ||  // Window button.
         ( strcmp( acBuf, "#9" ) == 0 ) )   // Window title.
      continue;

    if ( cTipCtl == ulTipCtlMax )
    {
      PTIPCTL    paTipCtlNew =
        realloc( paTipCtl, (ulTipCtlMax + _CTL_ARRAY_DELTA) * sizeof(TIPCTL) );

      if ( paTipCtlNew == NULL )
        break;

      ulTipCtlMax += _CTL_ARRAY_DELTA;
      paTipCtl = paTipCtlNew;

      if ( cTipCtl == 0 )
        _tipCreateWin( WinQueryAnchorBlock( hwnd ) );
    }

    paTipCtl[cTipCtl].hwnd = hwndEnum;
    paTipCtl[cTipCtl].fnWinProcOrg = WinSubclassWindow( hwndEnum, _ctlWinProc );
    cTipCtl++;

    twSet( hwndEnum );
  }
  WinEndEnumWindows( henum );
}

HWND twQueryTipWinHandle()
{
  return hwndTip;
}
