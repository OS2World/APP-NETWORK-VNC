/*
  VNC Server GUI controls thread.

  - Desktop VNC Server presentation:
    - Extended system tray widget (by Dmik) and system tray widget ver. 0.3 (by
      ErOs2) are supported.
    - If both widgets are not available we show small window with dialog boder
      and icon.

  - Opens configuration dialog by (context menu) command.

  - Creates chat windows on WMGUI_CHAT_OPEN message.
*/

#define INCL_WIN
#define INCL_DOSPROCESS
#define INCL_DOSMODULEMGR
#define INCL_DOSERRORS
#include <os2.h>
#include <stdlib.h>
#include <rfb/rfb.h>
#define XSTAPI_FPTRS_STATIC
#include "xsystray.h"
#include "resource.h"
#include <chatwin.h>
#include "srvwin.h"
#include "configdlg.h"
#include "avdlg.h"
#include "clientsdlg.h"
#include "utils.h"
#include "config.h"
#include "gui.h"
#include <debug.h>

#define _WIN_CLASS              "VNCSERVER_gui"

// SysTray support
#define WM_TRAYADDME            (WM_USER + 1)
#define WM_TRAYDELME            (WM_USER + 2)
#define WM_TRAYICON             (WM_USER + 3)
#define WM_TRAYEXIT             0xCD20

typedef struct _WINDATA {
  HWND       hwndConfig;         // "Properties" dialog.
  HWND       hwndAttachViewer;   // "Attach listening viewer" dialog.
  HWND       hwndClients;        // "List of all clients" dialog.
  HWND       hwndTrayServer;
  HWND       hwndMenu;
  HPOINTER   hptrIcon;

  // Extended system tray widget enabled (xsystray.dll loaded & widget created).
  BOOL       fXSTEnable;

  // Extended system tray widget have a bug: no messages from widget after 
  // xstAddSysTrayIcon() and xstRemoveSysTrayIcon(). This flag will be set
  // in _ctrlGUIShow() (future: on widget version check) and we will use
  // transparent icon hptrTransparentIcon instead xstRemoveSysTrayIcon().
  BOOL       fXSTWorkaround;
  HPOINTER   hptrTransparentIcon;

  BOOL       fVisible;
} WINDATA, *PWINDATA;

// hwndSrv uses to send messages from GUI controls to the main thread's hidden
// window.
extern HWND  hwndSrv;               // from main.c
extern ATOM  atomWMVNCServerQuery;  // from main.c

// hwndGUI uses to send messages to the GUI window from an other (main) thread.
HWND         hwndGUI = NULLHANDLE;

static HAB             habGUI  = NULLHANDLE;
static HMQ             hmqGUI  = NULLHANDLE;
static int             tid     = -1;
static HMODULE         hmodXSysTray = NULLHANDLE;
static ULONG           ulWMXSTCreated = 0;
static PFNWP           oldWndFrameProc = NULLHANDLE;


static MRESULT EXPENTRY wndFrameProc(HWND hwnd, ULONG msg, MPARAM mp1,
                                     MPARAM mp2)
{
  if ( msg == atomWMVNCServerQuery )
  {
    return (MRESULT)hwnd;
  }

  switch( msg )
  {
    case WM_BUTTON1DOWN:
    case WM_BUTTON2DOWN:
      WinSendMsg( hwnd, WM_TRACKFRAME, MPFROMSHORT( TF_MOVE | TF_STANDARD ), 0 );
      return (MRESULT)0;
  }

  return oldWndFrameProc( hwnd, msg, mp1, mp2 );
}

static VOID _ctrlGUIShow(HWND hwnd)
{
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );

  if ( pData->fVisible )
    return;
  pData->fVisible = TRUE;

  // Extended system tray widget for XCenter/eCenter.
  if ( hmodXSysTray != NULLHANDLE )
  {
    ULONG    ulMajor, ulMinor, ulRev;
    CHAR     acBuf[128];

    ulWMXSTCreated = xstGetSysTrayCreatedMsgId();

    WinQueryWindowText( hwndFrame, sizeof(acBuf), acBuf );
    xstAddSysTrayIcon( hwnd, 0, pData->hptrIcon, acBuf, WMGUI_XSYSTRAY, 0 );

    pData->fXSTEnable = xstQuerySysTrayVersion( &ulMajor, &ulMinor, &ulRev );
    pData->fXSTWorkaround = TRUE;
      // Future (when bug in widget will be fixed?) - check version.
  }

  WinSendMsg( hwndFrame, WM_SETICON, MPFROMLONG(pData->hptrIcon), 0 );

  if ( !pData->fXSTEnable )
  {
    // System tray widget for XCenter/eCenter (ver. 0.3) support.
    // We will receive WM_DDE_INITIATEACK durig this function call if tray
    // widget is exists.
    WinDdeInitiate( hwnd, "SystrayServer", "TRAY", NULL );
  }

  if ( !pData->fXSTEnable && ( pData->hwndTrayServer == NULLHANDLE ) )
  {
    // Botch tray widgets are not available. Create/show a switch entry and make
    // out small window visible.

    HSWITCH  hSwitch = WinQuerySwitchHandle( hwndFrame, 0 );
    SWCNTRL  stSwCntrl;

    if ( hSwitch == NULLHANDLE )
    {
      // Create a switch entry.
      stSwCntrl.hwnd          = hwndFrame;
      stSwCntrl.hwndIcon      = pData->hptrIcon;
      stSwCntrl.hprog         = NULLHANDLE;
      stSwCntrl.idSession     = 0;
      stSwCntrl.uchVisibility = SWL_VISIBLE;
      stSwCntrl.fbJump        = SWL_JUMPABLE;
      stSwCntrl.bProgType     = PROG_PM;
      strcpy( stSwCntrl.szSwtitle, APP_NAME );
      WinQueryWindowProcess( hwndFrame, &stSwCntrl.idProcess, NULL );
      WinAddSwitchEntry( &stSwCntrl );
    }
    else if ( WinQuerySwitchEntry( hSwitch, &stSwCntrl ) == 0 )
    {
      // Switch entry exsist, make it visible.
      stSwCntrl.uchVisibility = SWL_VISIBLE;
      stSwCntrl.fbJump        = SWL_JUMPABLE;
      WinChangeSwitchEntry( hSwitch, &stSwCntrl );
    }

    WinShowWindow( hwndFrame, TRUE );
  }
}

static VOID _ctrlGUIHide(HWND hwnd, BOOL fOnDestroy)
{
  PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  HSWITCH    hSwitch;
  SWCNTRL    stSwCntrl;

  if ( !pData->fVisible )
    return;

  if ( hmodXSysTray != NULLHANDLE )
  {
    if ( pData->fXSTWorkaround && !fOnDestroy )
    {
      xstReplaceSysTrayIcon( hwnd, 0, pData->hptrTransparentIcon );
      xstSetSysTrayIconToolTip( hwnd, 0, "" );
    }
    else
      xstRemoveSysTrayIcon( hwnd, 0 );
    pData->fXSTEnable = FALSE;
  }

  if ( pData->hwndTrayServer != NULLHANDLE )
  {
    WinPostMsg( pData->hwndTrayServer, WM_TRAYDELME, MPFROMHWND(hwnd), 0 );
    pData->hwndTrayServer = NULLHANDLE;
  }

  // Make switch entry invisible.
  hSwitch = WinQuerySwitchHandle( hwndFrame, 0 );
  if ( ( hSwitch != NULLHANDLE ) &&
       ( WinQuerySwitchEntry( hSwitch, &stSwCntrl ) == 0 ) )
  {
    stSwCntrl.uchVisibility = SWL_INVISIBLE;
    stSwCntrl.fbJump        = SWL_NOTJUMPABLE;
    WinChangeSwitchEntry( hSwitch, &stSwCntrl );
  }

  WinShowWindow( hwndFrame, FALSE );
  pData->fVisible = FALSE;
}


static BOOL _wmCreate(HWND hwnd)
{
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  PWINDATA   pData = calloc( 1, sizeof(WINDATA) );
  RECTL      rectl;

  if ( pData == NULL )
    return FALSE;

  if ( !WinSetWindowPtr( hwnd, 0, pData ) )
  {
    free( pData );
    return FALSE;
  }

  // Load main (popup) menu.
  pData->hwndMenu = WinLoadMenu( hwnd, 0, IDMNU_MAIN );

  pData->hptrIcon = WinLoadPointer( HWND_DESKTOP, NULLHANDLE, ID_ICON );
  if ( pData->hptrIcon == NULLHANDLE )
  {
    debug( "Icon #%u could not be loaded" );
    WinDestroyWindow( pData->hwndMenu );
    free( pData );
    return FALSE;
  }
  pData->hptrTransparentIcon = WinLoadPointer( HWND_DESKTOP, NULLHANDLE, IDICON_NULL );

  rectl.xLeft   = 0;
  rectl.yBottom = 0;
  rectl.xRight  = WinQuerySysValue( HWND_DESKTOP, SV_CXICON );
  rectl.yTop    = WinQuerySysValue( HWND_DESKTOP, SV_CYICON );
  WinCalcFrameRect( hwndFrame, &rectl, FALSE );
  WinSetWindowPos( hwndFrame, HWND_TOP,
                   WinQuerySysValue( HWND_DESKTOP, SV_CXFULLSCREEN ) - 
                     2 * rectl.xRight,
                   rectl.yTop,
                   rectl.xRight - rectl.xLeft, rectl.yTop - rectl.yBottom,
                   SWP_SIZE | SWP_MOVE );

  return TRUE;
}

static VOID _wmDestroy(HWND hwnd)
{
  PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );
  HWND       hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  POINTL     pt = { 0, 0 };

  if ( pData->hwndConfig != NULLHANDLE )
    WinDestroyWindow( pData->hwndConfig );

  WinMapWindowPoints( hwndFrame, HWND_DESKTOP, &pt, 1 );
  cfgSaveGUIData( habGUI, pt.x, pt.y, pData->fVisible );

  _ctrlGUIHide( hwnd, TRUE );

  WinDestroyPointer( pData->hptrTransparentIcon );
  WinDestroyPointer( pData->hptrIcon );
  WinDestroyWindow( pData->hwndMenu );

  free( pData );

  WinPostMsg( hwndSrv, WM_CLOSE, 0, 0 );
}

static VOID _wmDDEInitiateAck(HWND hwnd, HWND hwndTrayServer)
{
  PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );

  if ( hwndTrayServer == NULLHANDLE )
    return;

  pData->hwndTrayServer = hwndTrayServer;
  WinPostMsg( hwndTrayServer, WM_TRAYADDME, (MPARAM)hwnd, 0 );
}

static VOID _menuShow(HWND hwnd)
{
  PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );
  POINTL     pointl;

  WinQueryPointerPos( HWND_DESKTOP, &pointl );
  WinPopupMenu( HWND_DESKTOP, hwnd, pData->hwndMenu, pointl.x, pointl.y, 0,
                PU_HCONSTRAIN | PU_VCONSTRAIN | PU_MOUSEBUTTON1 |
                PU_MOUSEBUTTON2 | PU_KEYBOARD );
}

static VOID _wmGUIClntNumChanged(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  PWINDATA             pData = WinQueryWindowPtr( hwnd, 0 );
  HWND                 hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  ULONG                cClients = (ULONG)SHORT1FROMMP(mp1);
  BOOL                 fNewClient = SHORT2FROMMP(mp1) != 0;
//  rfbClientPtr         pClient = (rfbClientPtr)PVOIDFROMMP(mp2);
  ULONG                ulIconId;
  HPOINTER             hptrIcon, hptrIconOld;
  CHAR                 acBuf[64];

/*  debug( "Client %s has %s server (total clients: %u)",
         pClient->host, fNewClient ? "join" : "left", cClients );*/

  if ( pData->hwndClients != NULLHANDLE )
    WinSendMsg( pData->hwndClients, WMCLNT_CLNTNUMCHANGED, mp1, mp2 );

  if ( fNewClient && ( cClients == 1 ) )
    ulIconId = ID_ICON_ONLINE;
  else if ( !fNewClient && ( cClients == 0 ) )
    ulIconId = ID_ICON;
  else
    return;

  hptrIcon = WinLoadPointer( HWND_DESKTOP, NULLHANDLE, ulIconId );
  if ( hptrIcon == NULLHANDLE )
  {
    debug( "Icon #%u could not be loaded" );
    return;
  }

  hptrIconOld = pData->hptrIcon;
  pData->hptrIcon = hptrIcon;

  WinSendMsg( hwndFrame, WM_SETICON, MPFROMLONG(pData->hptrIcon), 0 );

  strcpy( acBuf, APP_NAME );
  if ( cClients != 0 )
  {
    PCHAR    pcBuf = &acBuf[APP_NAME_LENGTH];

    *((PULONG)pcBuf) = 0x00202D20; // ' - \0'
    pcBuf += 3;
    pcBuf += WinLoadString( habGUI, 0, IDS_CLIENTS, sizeof(acBuf) - 24, pcBuf );
    sprintf( pcBuf, ": %u", cClients );
  }

  WinSetWindowText( hwndFrame, acBuf );

  if ( pData->fVisible )
  {
    if ( hmodXSysTray != NULLHANDLE )
    {
      xstReplaceSysTrayIcon( hwnd, 0, pData->hptrIcon );
      xstSetSysTrayIconToolTip( hwnd, 0, acBuf );
    }

    if ( pData->hwndTrayServer != NULLHANDLE )
      WinPostMsg( pData->hwndTrayServer, WM_TRAYICON, MPFROMHWND(hwnd), 0 );

    if ( !pData->fXSTEnable && ( pData->hwndTrayServer == NULLHANDLE ) )
    {
      SWCNTRL  stSwCntrl;
      HSWITCH  hSwitch = WinQuerySwitchHandle( hwndFrame, 0 );

      if ( WinQuerySwitchEntry( hSwitch, &stSwCntrl ) == 0 )
      {
        pData->hptrIcon = hptrIcon;
        strlcpy( stSwCntrl.szSwtitle, acBuf, MAXNAMEL );
        WinChangeSwitchEntry( hSwitch, &stSwCntrl );
      }

      WinInvalidateRect( hwnd, NULL, FALSE );
    }
  }

  if ( ( hptrIconOld != NULLHANDLE ) && ( hptrIconOld != pData->hptrIcon ) )
    WinDestroyPointer( hptrIconOld );
}

static VOID _wmXSTCreated(HWND hwnd)
{
  HWND                 hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  PWINDATA             pData = WinQueryWindowPtr( hwnd, 0 );
  HSWITCH              hSwitch;
  CHAR                 acBuf[128];

  if ( !pData->fVisible )
    return;

  // Add icon to the extended system tray widget.
  WinQueryWindowText( hwndFrame, sizeof(acBuf), acBuf );
  xstAddSysTrayIcon( hwnd, 0, pData->hptrIcon, acBuf, WMGUI_XSYSTRAY, 0 );
  WinSendMsg( hwndFrame, WM_SETICON, MPFROMLONG(pData->hptrIcon), 0 );
  pData->fXSTEnable = TRUE;

  // Remove icon from the system tray widget ver. 0.3.
  if ( pData->hwndTrayServer != NULLHANDLE )
    WinPostMsg( pData->hwndTrayServer, WM_TRAYDELME, MPFROMHWND(hwnd), 0 );

  // Hide window (it visible when system tray widgets are not available).
  WinShowWindow( hwndFrame, FALSE );
  // Remove switch entry.
  hSwitch = WinQuerySwitchHandle( hwndFrame, 0 );
  if ( hSwitch != NULLHANDLE )
    WinRemoveSwitchEntry( hSwitch );
}

static VOID _wmPaint(HWND hwnd)
{
  PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );
  RECTL      rectl;
  HPS        hps = WinBeginPaint( hwnd, 0, &rectl );

  WinFillRect( hps, &rectl, SYSCLR_DIALOGBACKGROUND );
  WinDrawPointer( hps, 0, 0, pData->hptrIcon, DP_NORMAL );
  WinEndPaint( hps );
}

static MRESULT EXPENTRY wndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  if ( ( ulWMXSTCreated != 0 ) && ( msg == ulWMXSTCreated ) )
  {
    // The extended system tray widget was created by user and ask us to add
    // icon.
    _wmXSTCreated( hwnd );
    return (MRESULT)0;
  }

  switch( msg )
  {
    case WM_CREATE:
      return (MRESULT)!_wmCreate( hwnd ); // FALSE is success code.

    case WM_DESTROY:
      _wmDestroy( hwnd );
      break;

    case WM_COMMAND:
    {
      PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );

      switch( SHORT1FROMMP(mp1) )
      {
        case CMD_CONFIG_OPEN:
          if ( pData->hwndConfig == NULLHANDLE )
            pData->hwndConfig = cfgdlgCreate( habGUI, hwnd );
          else
            WinSetWindowPos( pData->hwndConfig, HWND_TOP, 0, 0, 0, 0,
                             SWP_ACTIVATE | SWP_ZORDER );
          return (MRESULT)TRUE;

        case CMD_CONFIG_CLOSE:
          if ( pData->hwndConfig != NULLHANDLE )
          {
            WinDestroyWindow( pData->hwndConfig );
            pData->hwndConfig = NULLHANDLE;
          }
          return (MRESULT)TRUE;

        case CMD_ATTACH_VIEWER_OPEN:
          if ( pData->hwndAttachViewer == NULLHANDLE )
            pData->hwndAttachViewer = avdlgCreate( habGUI, hwnd );
          else
            WinSetWindowPos( pData->hwndAttachViewer, HWND_TOP, 0, 0, 0, 0,
                             SWP_ACTIVATE | SWP_ZORDER );
          return (MRESULT)TRUE;

        case CMD_ATTACH_VIEWER_CLOSE:
          if ( pData->hwndAttachViewer != NULLHANDLE )
          {
            WinDestroyWindow( pData->hwndAttachViewer );
            pData->hwndAttachViewer = NULLHANDLE;
          }
          return (MRESULT)TRUE;

        case CMD_CLIENTS_OPEN:
          if ( pData->hwndClients == NULLHANDLE )
            pData->hwndClients = clientsCreate( habGUI, hwnd );
          else
            WinSetWindowPos( pData->hwndClients, HWND_TOP, 0, 0, 0, 0,
                             SWP_ACTIVATE | SWP_ZORDER );
          return (MRESULT)TRUE;

        case CMD_CLIENTS_CLOSE:
          if ( pData->hwndClients != NULLHANDLE )
          {
            WinDestroyWindow( pData->hwndClients );
            pData->hwndClients = NULLHANDLE;
          }
          return (MRESULT)TRUE;

        case CMD_GUI_VISIBLE:
          _ctrlGUIShow( hwnd );
          return (MRESULT)TRUE;

        case CMD_GUI_HIDDEN:
          _ctrlGUIHide( hwnd, FALSE );
          return (MRESULT)TRUE;

        case CMD_QUIT:
          WinPostMsg( hwnd, WM_CLOSE, 0, 0 );
          return (MRESULT)TRUE;
      }

      return (MRESULT)0;
    }

    case WM_DDE_INITIATEACK:
      _wmDDEInitiateAck( hwnd, (HWND)mp1 );
      return (MRESULT)TRUE;

    case WMGUI_XSYSTRAY:                // Extended system tray widget message.
    {
      PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );

      if ( ( SHORT2FROMMP(mp1) != XST_IN_CONTEXT ) || !pData->fXSTEnable )
        return (MRESULT)0;
    }
    case (WM_BUTTON2CLICK | 0x2000):    // System tray widget ver. 0.3 message.
    case WM_BUTTON2CLICK:
      _menuShow( hwnd );
      return (MRESULT)TRUE;

    case WM_BUTTON1DOWN:
    case WM_BUTTON2MOTIONSTART:
      WinSendMsg( WinQueryWindow( hwnd, QW_PARENT ), WM_TRACKFRAME,
                  MPFROMSHORT( TF_MOVE | TF_STANDARD ), 0 );
      return (MRESULT)0;

    case WMGUI_CLNT_NUM_CHANGED:
      _wmGUIClntNumChanged( hwnd, mp1, mp2 );
      return (MRESULT)0;

    case WMGUI_CHAT_OPEN:        // Message from frbsrv module.
      return (MRESULT)chatwinCreate( hwnd, WMGUI_CHATWIN, PVOIDFROMMP(mp1) );

    case WMGUI_CHATWIN:          // Message from chatwin module.
    {
      PCWMSGDATA       pCWMsgData = (PCWMSGDATA)mp2;
      rfbClientPtr     prfbClient = (rfbClientPtr)pCWMsgData->pUser;

      switch( LONGFROMMP(mp1) )
      {
        case CWM_MESSAGE:
          return WinSendMsg( hwndSrv, WMSRV_CHAT_MESSAGE,
                             MPFROMP(prfbClient),
                             MPFROMP(pCWMsgData->pszText) );

        case CWM_CLOSE:
        {
          return WinSendMsg( hwndSrv, WMSRV_CHAT_WINDOW,
                             MPFROMP(prfbClient), 0 );
        }
      }

      return (MRESULT)TRUE;
    }

    case WM_PAINT:
      _wmPaint( hwnd );
      return (MRESULT)0;
  }

  return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}

static void threadGUI(void *pData)
{
  static struct _XSYSTRAYENT {   // xsystray.dll exports list.
    PSZ      pszName;
    PFN      *ppFn;
  } aXSysTrayEnt[] =
    { { "xstQuerySysTrayVersion", (PFN *)&xstQuerySysTrayVersion },
      { "xstAddSysTrayIcon", (PFN *)&xstAddSysTrayIcon },
      { "xstReplaceSysTrayIcon", (PFN *)&xstReplaceSysTrayIcon },
      { "xstRemoveSysTrayIcon", (PFN *)&xstRemoveSysTrayIcon },
      { "xstSetSysTrayIconToolTip", (PFN *)&xstSetSysTrayIconToolTip },
      { "xstShowSysTrayIconBalloon", (PFN *)&xstShowSysTrayIconBalloon },
      { "xstHideSysTrayIconBalloon", (PFN *)&xstHideSysTrayIconBalloon },
      { "xstQuerySysTrayIconRect", (PFN *)&xstQuerySysTrayIconRect },
      { "xstGetSysTrayCreatedMsgId", (PFN *)&xstGetSysTrayCreatedMsgId },
      { "xstGetSysTrayMaxTextLen", (PFN *)&xstGetSysTrayMaxTextLen } };

  QMSG       msg;
  ULONG      ulCreateFlags = FCF_DLGBORDER;
  HWND       hwndFrame;

  habGUI = WinInitialize( 0 );
  hmqGUI = WinCreateMsgQueue( habGUI, 0 );
  if ( hmqGUI == NULLHANDLE )
  {
    debug( "WinCreateMsgQueue() failed" );
    WinTerminate( habGUI );
    _endthread();
  }

  // Load xsystray.dll and query entries.

  if ( DosLoadModule( NULL, 0, "xsystray", &hmodXSysTray ) != NO_ERROR )
  {
    debug( "DosLoadModule(,,\"xsystray\",) failed" );
    hmodXSysTray = NULLHANDLE;
  }
  else
  {
    ULONG    ulIdx;

    for( ulIdx = 0; ulIdx < ARRAYSIZE( aXSysTrayEnt ); ulIdx++ )
    {
      if ( DosQueryProcAddr( hmodXSysTray, 0, aXSysTrayEnt[ulIdx].pszName,
                             aXSysTrayEnt[ulIdx].ppFn ) != NO_ERROR )
      {
        debug( "Function %s not found in xsystray.dll",
               aXSysTrayEnt[ulIdx].pszName );
        DosFreeModule( hmodXSysTray );
        hmodXSysTray = NULLHANDLE;
        break;
      }
    }
  }

  WinRegisterClass( habGUI, _WIN_CLASS, wndProc, 0, sizeof(PWINDATA) );
  hwndFrame = WinCreateStdWindow( HWND_DESKTOP, 0, &ulCreateFlags, _WIN_CLASS,
                              APP_NAME, WS_VISIBLE, 0, ID_ICON, &hwndGUI );
  if ( hwndFrame != NULLHANDLE )
  {
    // Restore position from configuration data.
    if ( ( ((PCONFIG)pData)->lGUIx != LONG_MIN ) &&
         ( ((PCONFIG)pData)->lGUIy != LONG_MIN ) )
    {
      RECTL  rectlDT, rectlWin;

      WinQueryWindowRect( HWND_DESKTOP, &rectlDT );
      WinQueryWindowRect( hwndFrame, &rectlWin );

      if ( ((PCONFIG)pData)->lGUIx < rectlDT.xLeft )
        ((PCONFIG)pData)->lGUIx = rectlDT.xLeft;

      if ( ((PCONFIG)pData)->lGUIy < rectlDT.yBottom )
        ((PCONFIG)pData)->lGUIy = rectlDT.yBottom;

      if ( (((PCONFIG)pData)->lGUIx + rectlWin.xRight) > rectlDT.xRight )
        ((PCONFIG)pData)->lGUIx = rectlDT.xRight - rectlWin.xRight;

      if ( (((PCONFIG)pData)->lGUIy + rectlWin.yTop) > rectlDT.yTop )
        ((PCONFIG)pData)->lGUIy = rectlDT.yTop - rectlWin.yTop;

      WinSetWindowPos( hwndFrame, HWND_TOP,
                       ((PCONFIG)pData)->lGUIx, ((PCONFIG)pData)->lGUIy, 0, 0,
                       SWP_MOVE );
    }

    if ( ((PCONFIG)pData)->fGUIVisible )
      WinSendMsg( hwndGUI, WM_COMMAND, MPFROMSHORT(CMD_GUI_VISIBLE),
                  MPFROM2SHORT(CMDSRC_OTHER,FALSE) );

    oldWndFrameProc = WinSubclassWindow( hwndFrame, wndFrameProc );
    // oldWndFrameProc is not NULL now and it unblocks guiInit() function.

    while( WinGetMsg( habGUI, &msg, NULLHANDLE, 0, 0 ) )
      WinDispatchMsg( habGUI, &msg );

    WinDestroyWindow( hwndFrame );
    hwndGUI = NULLHANDLE;
  }

  if ( hmodXSysTray != NULLHANDLE )
    DosFreeModule( hmodXSysTray );

  WinDestroyMsgQueue( hmqGUI );
  WinTerminate( habGUI );
  _endthread();
}


BOOL guiInit(PCONFIG pConfig)
{
  if ( hwndGUI != NULLHANDLE )
  {
    debugPCP( "Already initialized" );
    return FALSE;
  }

  // Start thread for visible windows (controls, dialogs, menu, e.t.c.).
  tid = _beginthread( threadGUI, NULL, 65535, (PVOID)pConfig );
  if ( tid == -1 )
  {
    debug( "_beginthread() failed" );
    return FALSE;
  }

  // Wait while the thread is runned and PM stuff initialized.
  while( ( DosWaitThread( (PTID)&tid, DCWW_NOWAIT ) ==
             ERROR_THREAD_NOT_TERMINATED ) &&
         ( oldWndFrameProc == NULLHANDLE ) )
    DosSleep( 1 );

  return hmqGUI != NULLHANDLE;
}

VOID guiDone()
{
  if ( ( tid != -1 ) && ( hwndGUI != NULLHANDLE ) )
  {
    WinSendMsg( hwndGUI, WM_CLOSE, 0, 0 );
    DosWaitThread( (PTID)&tid, DCWW_WAIT );
  }
}
