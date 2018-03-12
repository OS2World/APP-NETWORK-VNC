#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_DOSMODULEMGR
#define INCL_DOSSEMAPHORES
#define INCL_GPI
#define INCL_WIN
#include <os2.h>
#include <../pmhook/vncshook.h>
#include "resource.h"
#include "config.h"
#include "gui.h"
#include "configdlg.h"
#include "srvwin.h"
#include "rfbsrv.h"
#include "utils.h"
#include "log.h"
#include <debug.h>

#define _WIN_CLASS              "VNCSERVER_EvHdl"
#define _ATOM_VNCSERVER_QUERY   "VNCServer_query"

extern HWND            hwndGUI;  // from gui.c

HWND                   hwndSrv      = NULLHANDLE;
HAB                    habSrv       = NULLHANDLE;
HMQ                    hmqSrv       = NULLHANDLE;
PMHINIT                pmhInit      = NULL;
PMHDONE                pmhDone      = NULL;
PMHPOSTEVENT           pmhPostEvent = NULL;
ATOM                   atomWMVNCServerQuery = 0;
HWND                   hwndLastUnderPtr = NULLHANDLE;
CHAR                   acWinUnderPtrClass[128] = { 0 };

static HMODULE         hmodHook     = NULLHANDLE;
static BOOL            fSetClipboardViewer = FALSE;

static VOID _wmPalette()
{
  RGB2       aColors[256];
  ULONG      cColors;
  HPS        hpsDT;

  // Query desktop palette.

  hpsDT = WinGetScreenPS( HWND_DESKTOP );
  cColors = GpiQueryRealColors( hpsDT, 0, 0, 256, (PLONG)aColors );
  WinReleasePS( hpsDT );
  if ( cColors == GPI_ALTERROR )
  {
    debug( "GpiQueryRealColors() failed" );
    return;
  }

  // Send a new color map to the clients.
  rfbsSetPalette( cColors, aColors );
}

// Set our window as the clipboard-viewer on each client (only if we are not
// clipboard viewer). Release the clipboard-viewer window if last client is
// gone.
static VOID _wmSrvClntNumChanged(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  ULONG      cClients = (ULONG)SHORT1FROMMP(mp1);

  fSetClipboardViewer = cClients != 0;
    // This flag will be checked on the message WM_DRAWCLIPBOARD (in
    // _wmDrawClipboard()) to avoid sending clipboard data during
    // WinSetClipbrdViewer(). This is necessary because WMSRV_CLNT_NUM_CHANGED
    // occurs during handshake and clipboard data will interfere with this
    // process.

  if ( ( WinQueryClipbrdViewer( habSrv ) == hwnd) != fSetClipboardViewer )
  {
    // We are clipboard-viewer and have not client(s) or
    // we are not clipboard-viewer and have client(s).

    if ( !WinOpenClipbrd( habSrv ) )
      debug( "WinOpenClipbrd() failed" );
    else
    {
      if ( !WinSetClipbrdViewer( habSrv, fSetClipboardViewer ? hwnd : NULLHANDLE ) )
        debug( "WinSetClipbrdViewer() failed" );

      WinCloseClipbrd( habSrv );
    }
  }
  fSetClipboardViewer = FALSE;

  if ( hwndGUI != NULLHANDLE )        // GUI window may be already destroyed.
    WinSendMsg( hwndGUI, WMGUI_CLNT_NUM_CHANGED, mp1, mp2 );
}

static VOID _wmDrawClipboard(HWND hwnd)
{
  PSZ        pszText = NULL;

  if ( !WinOpenClipbrd( habSrv ) )
    debug( "WinOpenClipbrd() failed" );
  else
  {
    pszText = strdup( (PSZ)WinQueryClipbrdData( habSrv, CF_TEXT ) );
    WinCloseClipbrd( habSrv );
  }

  if ( pszText != NULL )
  {
    rfbsSendClipboardText( pszText );
    free( pszText );
  }
}

typedef struct _LISTCLIENTDATA {
  HWND       hwndList;
  ULONG      ulWMList;
} LISTCLIENTDATA, *PLISTCLIENTDATA;

static BOOL _cbListClient(rfbClientPtr prfbClient, PVOID pUser)
{
  PLISTCLIENTDATA      pListClient = (PLISTCLIENTDATA)pUser;

  return (BOOL)WinSendMsg( pListClient->hwndList, pListClient->ulWMList,
                           MPFROMP(prfbClient), 0 );
}

static VOID _wmSrvListClients(HWND hwnd, HWND hwndList, ULONG ulWMList)
{
  LISTCLIENTDATA       stListClient;

  stListClient.hwndList = hwndList;
  stListClient.ulWMList = ulWMList;
  rfbsForEachClient( _cbListClient, &stListClient );
}

static MRESULT EXPENTRY wndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_CREATE:
    case WM_SYSCOLORCHANGE:
    case WM_REALIZEPALETTE:
      _wmPalette();
      break;

    case WM_DRAWCLIPBOARD:
      if ( !fSetClipboardViewer )
        _wmDrawClipboard( hwnd );
      break;

    case WMSRV_CLNT_NUM_CHANGED: // Message from rfbsrv module.
      _wmSrvClntNumChanged( hwnd, mp1, mp2 );
      return (MRESULT)TRUE;

    case WMSRV_RECONFIG:         // Message from configdlg module.
      rfbsSetServer( (PCONFIG)mp1 );
      return (MRESULT)0;

    case WMSRV_CHAT_MESSAGE:     // Message from GUI window.
      return (MRESULT)rbfsSendChatMsg( (rfbClientPtr)mp1, (PSZ)mp2 );

    case WMSRV_CHAT_WINDOW:      // Message from GUI window.
      return (MRESULT)rbfsSetChatWindow( (rfbClientPtr)mp1, (HWND)mp2 );

    case WMSRV_ATTACH:           // Message from avdlg module.
      return (MRESULT)rfbsAttach( (int)LONGFROMMP(mp1), (BOOL)LONGFROMMP(mp2) );

    case WMSRV_LIST_CLIENTS:     // Message from clientsdlg module.
      _wmSrvListClients( hwnd, HWNDFROMMP(mp1), LONGFROMMP(mp2) );
      return (MRESULT)TRUE;

    case WMSRV_DISCONNECT:
      return (MRESULT)rfbsDisconnect( (rfbClientPtr)mp1 );
  }

  return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}


// BlackOut/ScreenSaver support
void unBlank()
{
  HEV        hevSuspend = NULLHANDLE;
  HEV        hevWait    = NULLHANDLE;
  HEV        hevOff     = NULLHANDLE;
  HEV        hevDpmsSs  = NULLHANDLE;
  HEV        hevDpmsBl  = NULLHANDLE;

  // BlackOut
  if ( DosOpenEventSem( "\\SEM32\\BLACKOUT\\SUSPEND", &hevSuspend ) != 0 )
    hevSuspend = NULLHANDLE;

  if ( DosOpenEventSem( "\\SEM32\\BLACKOUT\\WAIT", &hevWait ) != 0 )
    hevWait = NULLHANDLE;

  if ( DosOpenEventSem( "\\SEM32\\BLACKOUT\\OFF", &hevOff ) != 0 )
    hevOff = NULLHANDLE;

  // ScreenSaver
  if ( DosOpenEventSem( "\\SEM32\\SSAVER\\DPMS", &hevDpmsSs ) != 0 )
    hevDpmsSs = NULLHANDLE;

  // Blanker
  if ( DosOpenEventSem( "\\SEM32\\BLANKER\\DPMS", &hevDpmsBl ) != 0 )
    hevDpmsBl = NULLHANDLE;

  if ( hevSuspend != NULLHANDLE )
    DosPostEventSem( hevSuspend );
  if ( hevWait != NULLHANDLE )
    DosPostEventSem( hevWait );
  if ( hevOff != NULLHANDLE )
    DosPostEventSem( hevOff );
  if ( hevDpmsSs != NULLHANDLE )
    DosPostEventSem( hevDpmsSs );
  if ( hevDpmsBl != NULLHANDLE )
    DosPostEventSem( hevDpmsBl );

  if ( hevSuspend != NULLHANDLE )
    DosCloseEventSem( hevSuspend );
  if ( hevWait != NULLHANDLE )
    DosCloseEventSem( hevWait );
  if ( hevOff != NULLHANDLE )
    DosCloseEventSem( hevOff );
  if ( hevDpmsSs != NULLHANDLE )
    DosCloseEventSem( hevDpmsSs );
  if ( hevDpmsBl != NULLHANDLE )
    DosCloseEventSem( hevDpmsBl );
}

VOID APIENTRY ExitProc(ULONG ulCode)
{
  ULONG      ulRC;

  if ( habSrv != NULLHANDLE )
  {
    if ( pmhDone != NULL )
      pmhDone( habSrv );

    WinTerminate( habSrv );
  }

  if ( hmodHook != NULLHANDLE )
  {
    ulRC = DosFreeModule( hmodHook );
    if ( ulRC != NO_ERROR )
      debug( "DosFreeModule(hmodHook), rc = %u", ulRC );
  }

  DosExitList( EXLST_EXIT, NULL );
}

static VOID _appDone()
{
  ULONG      ulRC;

  rfbsDone();
  guiDone();

  if ( pmhDone != NULL )
  {
    if ( habSrv != NULLHANDLE )
      pmhDone( habSrv );
    pmhDone = NULL;
  }

  if ( hmqSrv != NULLHANDLE )
  {
    WinDestroyMsgQueue( hmqSrv );
    hmqSrv = NULLHANDLE;
  }

  if ( habSrv != NULLHANDLE )
  {
    WinTerminate( habSrv );
    habSrv = NULLHANDLE;
  }

  if ( hmodHook != NULLHANDLE )
  {
    ulRC = DosFreeModule( hmodHook );
    if ( ulRC != NO_ERROR )
      debug( "DosFreeModule(hmodHook), rc = %u", ulRC );
    hmodHook = NULLHANDLE;
  }

  WinDeleteAtom( WinQuerySystemAtomTable(), atomWMVNCServerQuery );

  debugPCP( "--- Done ---" );
  debugDone();
}

static HWND _appGetGUIWindow()
{
  HWND       hwndTop;
  HENUM      henum;

  // Enumerate all top-level windows.           

  henum = WinBeginEnumWindows( HWND_DESKTOP );

  // Loop through all enumerated windows.       
  while( ( hwndTop = WinGetNextWindow( henum ) ) != NULLHANDLE )
  {
    if ( (HWND)WinSendMsg( hwndTop, atomWMVNCServerQuery, 0, 0 ) != NULLHANDLE )
      break;
  }

  WinEndEnumWindows( henum );

  return hwndTop;
}

static BOOL _appInit(int argc, char** argv)
{
  PTIB       tib;
  PPIB       pib;
  ULONG      ulRC;
  PCONFIG    pConfig = NULL;
  HWND       hwndGUIRunned;
  LONG       lSignal = -1;
  ULONG      ulGUIShowTimeout = 0;

  debugInit();

  // Read command line switches.

  while( TRUE )
  {
    argv++;
    argc--;
    if ( argc <= 0 )
      break;

    switch( utilStrWordIndex( "-s -t", -1, *argv ) )
    {
      case 0:          // -s <signal> - signal for runned instance.
        argv++;
        argc--;
        if ( argc > 0 )
          lSignal = utilStrWordIndex( "properties-open properties-close show "
                                      "hide shutdown",
                                      -1, *argv );
        break;

      case 1:          // -t <seconds> - GUI (icon) show timeout.
        argv++;
        argc--;
        if ( !utilStrToULong( -1, *argv, 0, ULONG_MAX / 1000,
                              &ulGUIShowTimeout ) )
          ulGUIShowTimeout = 0;
        break;
    }
  }

  // Change process type code for use Win* API from VIO session.
  DosGetInfoBlocks( &tib, &pib );
  if ( pib->pib_ultype == 2 || pib->pib_ultype == 0 )
  {
    // VIO windowable or fullscreen protect-mode session.
    pib->pib_ultype = 3; // Presentation Manager protect-mode session.
    // ...and switch to the desktop (if we are in fullscreen now)?
  }

  // PM stuff...
  habSrv = WinInitialize( 0 );
  hmqSrv = WinCreateMsgQueue( habSrv, 0 );
  if ( hmqSrv == NULLHANDLE )
  {
    debug( "WinCreateMsgQueue() failed" );
    return FALSE;
  }

  // Atom to send messages in runned instance GUI window.
  atomWMVNCServerQuery = WinAddAtom( WinQuerySystemAtomTable(),
                                     _ATOM_VNCSERVER_QUERY );

  do
  {
    // Interaction with runned instance.

    hwndGUIRunned = _appGetGUIWindow();
    if ( hwndGUIRunned != NULLHANDLE )
    {
      // Signal for the runned instance is specified with command line switch.

      USHORT   usCommand = 0;

      switch( lSignal )
      {
        case 0:  usCommand = CMD_CONFIG_OPEN;  break; // properties-open
        case 1:  usCommand = CMD_CONFIG_CLOSE; break; // properties-close
        case 2:  usCommand = CMD_GUI_VISIBLE;  break; // show
        case 3:  usCommand = CMD_GUI_HIDDEN;   break; // hide
        case 4:  usCommand = CMD_QUIT;         break; // shutdown
        default:                                      // lSignal == -1
          if ( utilMessageBox( HWND_DESKTOP, APP_NAME, IDMSG_ALREADY_RUNNING,
                               MB_ICONASTERISK | MB_YESNO ) == MBID_YES )
            usCommand = CMD_CONFIG_OPEN;
      }

      if ( usCommand != 0 )
        WinSendMsg( hwndGUIRunned, WM_COMMAND, MPFROMSHORT(usCommand),
                    MPFROM2SHORT(CMDSRC_OTHER,FALSE) );
      break;
    }

    if ( lSignal != -1 )
    {
      utilMessageBox( HWND_DESKTOP, APP_NAME, IDMSG_NOT_RUNNING, MB_OK );
      break;
    }

    // Load hook DLL.

    ulRC = DosLoadModule( NULL, 0, "VNCSHOOK", &hmodHook );
    if ( ulRC != NO_ERROR )
    {
      debug( "Error loading PMHOOK, rc = %u\n", ulRC );
      break;
    }

    ulRC = DosQueryProcAddr( hmodHook, 0, "pmhInit", (PFN *)&pmhInit );
    if ( ulRC != NO_ERROR )
    {
      debugCP( "pmhInit() not found in VNCSHOOK" );
      break;
    }
    ulRC = DosQueryProcAddr( hmodHook, 0, "pmhDone", (PFN *)&pmhDone );
    if ( ulRC != NO_ERROR )
    {
      debugCP( "pmhDone() not found in VNCSHOOK" );
      break;
    }
    ulRC = DosQueryProcAddr( hmodHook, 0, "pmhPostEvent", (PFN *)&pmhPostEvent );
    if ( ulRC != NO_ERROR )
    {
      debugCP( "pmhPostEvent() not found in VNCSHOOK" );
      break;
    }

    DosExitList( EXLST_ADD, ExitProc );

    pConfig = cfgGet();
    if ( pConfig == NULL )
    {
      debug( "cfgQuery() failed" );
      break;
    }

    // Initialize the RFB server.
    ulRC = rfbsInit( pConfig );
    if ( !ulRC )
      break;

    // Create server hidden window.

    WinRegisterClass( habSrv, _WIN_CLASS, wndProc, 0, 0 );
    hwndSrv = WinCreateWindow( HWND_DESKTOP, _WIN_CLASS, "VNCServer", 0,
                            0, 0, 0, 0, NULLHANDLE, HWND_TOP, 0, NULL, NULL );
    if ( hwndSrv == NULLHANDLE )
      break;

    guiInit( pConfig, ulGUIShowTimeout );

    if ( pConfig != NULL )
      cfgFree( pConfig );

    return TRUE;
  }
  while(FALSE);

  if ( pConfig != NULL )
    cfgFree( pConfig );

  _appDone();

  return FALSE;
}


#define _WINUNDPTR_ODIN          1
#define _WINUNDPTR_NOTEBOOK      2

int main(int argc, char** argv)
{
  QMSG       msg;
  ULONG      ulWinUnderPtr     = 0;
  RECTL      rectlWinUnderPtr;

  if ( !_appInit( argc, argv ) )
    return 1;

  while( TRUE )
  {
    if ( !WinPeekMsg( habSrv, &msg, NULLHANDLE, 0, 0, PM_REMOVE ) )
    {
      rfbsProcessEvents( 50 );
      continue;
    }

    if ( msg.msg == WM_QUIT )
      break;

    switch( msg.msg )
    {
      // Messages from hook module.

      case HM_SCREEN:
        {
          RECTL      rectlUpdate;

          rectlUpdate.xLeft   = (SHORT)SHORT1FROMMP(msg.mp1);
          rectlUpdate.yBottom = (SHORT)SHORT2FROMMP(msg.mp1);
          rectlUpdate.xRight  = (SHORT)SHORT1FROMMP(msg.mp2);
          rectlUpdate.yTop    = (SHORT)SHORT2FROMMP(msg.mp2);
          rfbsUpdateScreen( rectlUpdate );
        }
        break;

      case HM_MOUSE:
        {
          // Additional updates for Odin applications and notebook bookmarks.

          HWND         hwnd = HWNDFROMMP(msg.mp1);

          if ( hwnd != hwndLastUnderPtr )
          {
            LONG       cbBuf = WinQueryClassName( hwnd,
                                                  sizeof(acWinUnderPtrClass),
                                                  acWinUnderPtrClass );

            hwndLastUnderPtr  = hwnd;
            ulWinUnderPtr     = 0;

            if ( cbBuf == 0 )
              acWinUnderPtrClass[0] = '\0';
            else
            {
              if ( strcmp( acWinUnderPtrClass, "Win32WindowClass" ) == 0 )
                ulWinUnderPtr = _WINUNDPTR_ODIN;
              else if ( strcmp( acWinUnderPtrClass, "#40" ) == 0 )
                ulWinUnderPtr = _WINUNDPTR_NOTEBOOK;
              else
              {
                hwnd = WinQueryWindow( hwnd, QW_PARENT );
                if ( ( hwnd != NULLHANDLE ) &&
                     ( WinQueryClassName( hwnd, sizeof(acWinUnderPtrClass),
                                                acWinUnderPtrClass ) != 0 ) &&
                     ( strcmp( acWinUnderPtrClass, "Win32WindowClass" ) == 0 ) )
                  ulWinUnderPtr = _WINUNDPTR_ODIN;
              }

              if ( ulWinUnderPtr != 0 )
              {
                // Odin appliation or notebook window detected.
                WinQueryWindowRect( HWNDFROMMP(msg.mp1), &rectlWinUnderPtr );
                WinMapWindowPoints( HWNDFROMMP(msg.mp1), HWND_DESKTOP,
                                    (PPOINTL)&rectlWinUnderPtr, 2 );

                if ( ulWinUnderPtr == _WINUNDPTR_NOTEBOOK )
                  // Top part with bookmarks only.
                  rectlWinUnderPtr.yBottom = rectlWinUnderPtr.yTop - 27;
              }
            }
          }  // if ( hwnd != hwndLastUnderPtr )

          if ( ulWinUnderPtr != 0 )
            // Odin appliation or notebook window under pointer.
            rfbsUpdateScreen( rectlWinUnderPtr );

          // Send mouse event to clients.
          rfbsSetMouse();
        }
        break;

      default:
        WinDispatchMsg( habSrv, &msg );
    }
  }

  _appDone();
  return 0;
}
