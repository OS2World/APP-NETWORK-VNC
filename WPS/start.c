#define INCL_WIN
#define INCL_WINSWITCHLIST
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#define INCL_GPI
#define INCL_DOSPROCESS
#define INCL_DOSSEMAPHORES
#define INCL_DOSEXCEPTIONS
#define INCL_DOSMODULEMGR
#include "vncv.ih"
#include <ctype.h>
#include <utils.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <io.h>

#include <debug.h>

#define _WAITAPP_CLASS_NAME      "VNCV_WaitApp"
#define _WM_DESTROY              (WM_USER + 1)

#define _YESNO(fl) (fl ? "YES" : "NO")

typedef struct _APPDATA {
  USEITEM              stUseItem;
  VIEWITEM             stViewItem;
} APPDATA, *PAPPDATA;

typedef HSWITCH APIENTRY WINHSWITCHFROMHAPP(HAPP happ);

// '"' -> '\"', escape all previous '\' with '\'; last '\' -> '\\'; '\n' -> ' '
static PSZ _runSafeStr(ULONG cbBuf, PCHAR pcBuf, PSZ pszSrc)
{
  PCHAR      pcDst = pcBuf;
  CHAR       chSrc;
  BOOL       fLastIsSpace, fIsSpace;

  if ( cbBuf == 0 )
    return NULL;

  while( isspace( *pszSrc ) )
    pszSrc++;

  while( ( *pszSrc != '\0' ) && ( cbBuf > 1 ) )
  {
    fIsSpace = isspace( *pszSrc );
    chSrc = fIsSpace ? ' ' : *pszSrc; // EOL, TAB -> SPACE

    if ( *pszSrc == '"' )
    {
      // '"' -> '\"'; '\\"' -> '\\\\\"' (escape all prev. \)
      PCHAR  pcScanBack = pcDst - 1;

      while( ( pcScanBack >= pcBuf ) && ( *pcScanBack == '\\' ) && ( cbBuf > 1 ) )
      {
        *pcDst = '\\';
        pcDst++;
        cbBuf--;

        pcScanBack--;
      }

      if ( cbBuf > 2 )
      {
        *(PUSHORT)pcDst = (USHORT)'\"\\';
        pcDst += 2;
        cbBuf -= 2;
      }
    }
    else if ( !fIsSpace || !fLastIsSpace )
    {
      *pcDst = *pszSrc;
      pcDst++;
      cbBuf--;
    }

    fLastIsSpace = fIsSpace;
    pszSrc++;
  }

  if ( ( pcDst != pcBuf ) && ( *(pcDst - 1) == '\\' ) ) // Last '\' -> '\\'.
  {
    if ( cbBuf > 1 )  // Add '\'.
    {
      *pcDst = '\\';
      pcDst++;
      cbBuf--;
    }
    else              // No buffer space to escape '\' - remove last '\'.
    {
      pcDst--;
      cbBuf++;
    }
  }
  *pcDst = '\0';

  return pcBuf;
}

// Function doshQueryProcAddr() from XWP (xwphelpers\src\helpers\dosh.c).
static APIRET doshQueryProcAddr(PCSZ pcszModuleName,       // in: module name (e.g. "PMMERGE")
                                ULONG ulOrdinal,           // in: proc ordinal
                                PFN *ppfn)                 // out: proc address
{
    HMODULE hmod;
    APIRET  arc;

    if (!(arc = DosQueryModuleHandle((PSZ)pcszModuleName,
                                     &hmod)))
    {
        if ((arc = DosQueryProcAddr(hmod,
                                    ulOrdinal,
                                    NULL,
                                    ppfn)))
        {
            // the CP programming guide and reference says use
            // DosLoadModule if DosQueryProcAddr fails with this error
            if (arc == ERROR_INVALID_HANDLE)
            {
                if (!(arc = DosLoadModule(NULL,
                                          0,
                                          (PSZ)pcszModuleName,
                                          &hmod)))
                {
                    arc = DosQueryProcAddr(hmod,
                                           ulOrdinal,
                                           NULL,
                                           ppfn);
                }
            }
        }
    }

    return arc;
}


/* ****************************************************************** */
/* *                     Unvisible window                           * */
/* ****************************************************************** */

/*
  Timers for set screenshot to icon. We don't whant do it to often. But we do
  not want to make a big pause before first screenshot. We do not know how soon
  will receive the image of the remote desktop. Therefore, we gradually
  increase the period of installation of the screenshot after a notification
  "VNC Viewer window was open" is received.
*/

// Window creation time data.
typedef struct _WINCTLDATA {
  USHORT     cbData;
  vncv       *somSelf;
} WINCTLDATA, *PWINCTLDATA;

typedef struct _WINDATA {
  vncv       *somSelf;
  HWND       hwndVNCViewer;
  ULONG      ulSSCx;
  ULONG      ulSSCy;
  ULONG      ulTimerId;
} WINDATA, *PWINDATA;

static VOID _winSetSSBitmap(HWND hwnd)
{
  PWINDATA       pData = WinQueryWindowPtr( hwnd, 0 );
  vncvData       *somThis = vncvGetData( pData->somSelf );
  ULONG          cbStorage = sizeof(BITMAPINFO2) +
                             ( pData->ulSSCx * 4 * pData->ulSSCy );
  PVOID          pStorage;
  ULONG          ulRC;

  if ( !_fDynamicIcon )
    return;

  // Allocate memory for the screenshot.
  ulRC = DosAllocSharedMem( &pStorage, NULL, cbStorage,
         PAG_COMMIT | OBJ_GETTABLE | PAG_READ | PAG_WRITE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosAllocSharedMem(), rc = %u", ulRC );
    return;
  }

  // Query screenshot.
  ulRC = (BOOL)WinSendMsg( pData->hwndVNCViewer, WM_APP2VNCV_SCREENSHOT,
                           MPFROMLONG(cbStorage), MPFROMP(pStorage) );
  if ( !ulRC )
    debugCP( "VNC Viewer - screenshot creation failed" );
  else
  {
    PBITMAPINFO2       pBmInfo = (PBITMAPINFO2)pStorage;
    PVOID              pBmBits = &((PBYTE)pStorage)[sizeof(BITMAPINFO2)];
    HBITMAP            hbmSS;
    HPS                hps = WinGetPS( hwnd );

    // Create the system bitmap object from the obtained data.
    hbmSS = GpiCreateBitmap( hps, (PBITMAPINFOHEADER2)pBmInfo, CBM_INIT,
                             pBmBits, pBmInfo );
    WinReleasePS( hps );

    if ( ( hbmSS == GPI_ERROR ) || ( hbmSS == 0 ) )
    {
      debugCP( "GpiCreateBitmap() failed" );
    }
    else
    {
      ICONINFO       stIconInfo;

      // Create pointer (icon) from the bitmap.
      stIconInfo.pIconData = wpsutilIconFromBitmap( hbmSS,
                                                    &stIconInfo.cbIconData );
      if ( !GpiDeleteBitmap( hbmSS ) )
        debugCP( "GpiDeleteBitmap() failed" );

      if ( stIconInfo.pIconData == NULL )
        debugCP( "wpsutilIconFromBitmap() failed" );
      else
      {
        // Set new icon for the object.
        stIconInfo.cb = sizeof(ICONINFO);
        stIconInfo.fFormat = ICON_DATA;
        stIconInfo.pszFileName = NULL;
        stIconInfo.hmod = NULLHANDLE;
        stIconInfo.resid = 0;

        _fSetDynamicIconTime = TRUE;
        if ( !_wpSetIconData( pData->somSelf, &stIconInfo ) )
          debugCP( "wpSetIconData() failed" );
        _fSetDynamicIconTime = FALSE;
        free( stIconInfo.pIconData );
      }
    }
  }

  ulRC = DosFreeMem( pStorage );
  if ( ulRC != NO_ERROR )
    debug( "DosFreeMem(), rc = %u", ulRC );
}

// Stops the current timer and starts a new one (increasing the delay). When a
// last timeout from aulTimeouts[] is set keeps it while VNC Viewer is running.
static VOID _winSetNextTimer(HWND hwnd)
{
  static ULONG   aulTimeouts[] = { 2000, 2000, 5000, 10000, 25000 };
  PWINDATA       pData = WinQueryWindowPtr( hwnd, 0 );
  HAB            hab = WinQueryAnchorBlock( hwnd );
//  vncvData       *somThis = vncvGetData( pData->somSelf );

  if ( pData->ulTimerId >= ARRAYSIZE(aulTimeouts) )
    // Do not restart timer. Keep last listed timeout.
    return;

  if ( pData->ulTimerId != 0 )
    // Stop previous timer.
    WinStopTimer( hab, hwnd, pData->ulTimerId );

  pData->ulTimerId++;
  WinStartTimer( hab, hwnd, pData->ulTimerId,
                 aulTimeouts[pData->ulTimerId - 1] );
  debug( "Run timer %u for %u msec.",
         pData->ulTimerId, aulTimeouts[pData->ulTimerId - 1] );
}

static BOOL _wmCreate(HWND hwnd, PWINCTLDATA pWinCtlData)
{
  PWINDATA   pData = calloc( 1, sizeof(WINDATA) );

  if ( pData == NULL )
    return TRUE;       // Discontinue window creation.

  pData->somSelf = pWinCtlData->somSelf;
  WinSetWindowPtr( hwnd, 0, pData );

  return FALSE;        // Continue window creation.
}

static VOID _wmDestroy(HWND hwnd)
{
  // Window will be destroyed from VNCV_wpUnInitData().
  PWINDATA   pData = WinQueryWindowPtr( hwnd, 0 );
  vncv       *somSelf;
  PUSEITEM   pItem;

  if ( pData == NULL )
    return;

  WinStopTimer( WinQueryAnchorBlock( hwnd ), hwnd, pData->ulTimerId );

  // Shutdown all runned instances of vncviewer.exe.
  somSelf = pData->somSelf;
  while( (pItem = _wpFindUseItem( somSelf, USAGE_OPENVIEW, NULL )) != NULL )
  {
    if ( !WinTerminateApp( ((PAPPDATA)pItem)->stViewItem.handle ) )
      debug( "WinTerminateApp(%u) failed", ((PAPPDATA)pItem)->stViewItem.handle );

    _wpDeleteFromObjUseList( somSelf, pItem );
    _wpFreeMem( somSelf, (PBYTE)pItem );
  }

  free( pData );
}

static VOID _wmVNCVNotify(HWND hwnd, PWINDATA pData, ULONG ulNotify, MPARAM mp2)
{
  switch( ulNotify )
  {
    case VNCVNOTIFY_CLIENTWINDOW:
      // VNC Viewer reports its window handle.
      pData->hwndVNCViewer = HWNDFROMMP(mp2);
      break;

    case VNCVNOTIFY_SETCLIENTSIZE:
      // VNC Viewer reports desktop size. The first time this means that the
      // window for the remote desktop is open.
      {
        pData->ulSSCx = SHORT1FROMMP(mp2);
        pData->ulSSCy = SHORT2FROMMP(mp2);
        _winSetNextTimer( hwnd );
      }
      break;
  }
}

static MRESULT EXPENTRY _waitappWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_CREATE:
      return (MRESULT)_wmCreate( hwnd, PVOIDFROMMP(mp1) );

    case WM_DESTROY:
      _wmDestroy( hwnd );
      break;

    case _WM_DESTROY:
      WinDestroyWindow( hwnd );
      return (MRESULT)0;

    case WM_APPTERMINATENOTIFY:
      // PM message. vncviewer.exe terminated.
      // Remove record from UseList.
      {
        PWINDATA       pData = WinQueryWindowPtr( hwnd, 0 );
        vncv           *somSelf = pData->somSelf;
        HAPP           hApp = (HAPP)mp1;
        PUSEITEM       pItem = NULL;

        WinStopTimer( WinQueryAnchorBlock( hwnd ), hwnd, pData->ulTimerId );
        pData->ulTimerId = 0;

        while( (pItem = _wpFindUseItem( somSelf, USAGE_OPENVIEW, pItem )) !=
               NULL )
        {
          if ( ((PAPPDATA)pItem)->stViewItem.handle == hApp )
          {
            _wpDeleteFromObjUseList( somSelf, pItem );
            _wpFreeMem( somSelf, (PBYTE)pItem );
            break;
          }
        }

        if ( pItem == NULL )
          debug( "Application handle %u not found", hApp );
      }
      break;

    case WM_TIMER:
      {
        PWINDATA       pData = WinQueryWindowPtr( hwnd, 0 );

        if ( SHORT1FROMMP(mp1) != pData->ulTimerId )
          break;
      }
      _winSetNextTimer( hwnd );

    case WM_WA_UPDATEICON:
      _winSetSSBitmap( hwnd );
      break;

    default:
      {
        PWINDATA       pData = WinQueryWindowPtr( hwnd, 0 );

        if ( pData != NULL )
        {
          vncvData       *somThis = vncvGetData( pData->somSelf );

          if ( msg == _ulWMVNCVNotify )
          {
            _wmVNCVNotify( hwnd, pData, LONGFROMMP(mp1), mp2 );
            return (MRESULT)0;
          }
        }
      }
  }

  return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}


/* ****************************************************************** */

BOOL startViewer(vncv *somSelf, PSZ pszMClass)
{
  vncvData             *somThis = vncvGetData( somSelf );
  somId                Id;
  CHAR                 acExe[CCHMAXPATH];
  CHAR                 acDir[CCHMAXPATH];
  PCHAR                pcPos;
  PROGDETAILS          stProgDetails = { 0 };
  HAPP                 hApp;
  CHAR                 acParam[256];
  CHAR                 acTitle[128];
  int                  rc;
  PAPPDATA             pAddData;

  if ( _hwndWaitApp == NULLHANDLE )
  {
    WINCTLDATA         stWinCtlData;

    stWinCtlData.cbData = sizeof(WINCTLDATA);
    stWinCtlData.somSelf = somSelf;

    WinRegisterClass( WinQueryAnchorBlock( HWND_DESKTOP ), _WAITAPP_CLASS_NAME,
                      _waitappWndProc, 0, sizeof(PWINDATA) );
    _hwndWaitApp = WinCreateWindow( HWND_DESKTOP, _WAITAPP_CLASS_NAME, NULL, 0,
                                    0, 0, 0, 0, HWND_DESKTOP, HWND_TOP, 0,
                                    &stWinCtlData, NULL );
    debug( "Wait-Window created: %u", _hwndWaitApp );
  }

  // Get class DLL's full name.
  Id = somIdFromString( pszMClass );
  strcpy( acDir, _somLocateClassFile( SOMClassMgrObject, Id,
                                      vncv_MajorVersion, vncv_MinorVersion ) );
  SOMFree( Id );

  // Cut off a DLL name from the full name.
  pcPos = strrchr( acDir, '\\' );
  if ( pcPos == NULL )
    pcPos = acDir;
  *pcPos = '\0';

  // Make full name for .exe
  if ( _snprintf( acExe, sizeof(acExe), "%s\\vncviewer.exe", acDir ) == -1 )
  {
    debugCP( "WTF?!" );
    return FALSE;
  }

  // Build switches.

  rc = _snprintf( acParam, sizeof(acParam),
                  "-h %s -r %s -a %u -v %s -c %u -s %s -o %u -q %u -t \"%s\" -N %u",
                  _pszHostDisplay, _YESNO( _fRememberPswd ), _ulAttempts,
                  _YESNO( _fViewOnly ), _ulBPP, _YESNO( _fShareDesktop ),
                  _ulCompressLevel, _ulQualityLevel,
                  _runSafeStr( sizeof(acTitle), &acTitle,
                               _wpQueryTitle( somSelf ) ),
                  _hwndWaitApp );
  if ( rc == -1 )
    return FALSE;

  pcPos = &acParam[rc];
  if ( _pszEncodings != NULL )
  {
    rc = _snprintf( pcPos, sizeof(acParam) - (pcPos - acParam),
                    " -e \"%s\"", _pszEncodings );
    if ( rc == -1 )
      return FALSE;
    pcPos = &pcPos[rc];
  }

  if ( _acCharset[0] != '\0' )
  {
    rc = _snprintf( pcPos, sizeof(acParam) - (pcPos - acParam),
                    " -E \"%s\"", _acCharset );
    if ( rc == -1 )
      return FALSE;
  }

  // Start application.

  stProgDetails.Length = sizeof(PROGDETAILS);
  stProgDetails.progt.progc = PROG_PM;
  stProgDetails.progt.fbVisible = TRUE;
  stProgDetails.pszExecutable = acExe;
  stProgDetails.pszParameters = acParam;
  stProgDetails.pszStartupDir = acDir;

  debug( "Run: %s, Working dir.: %s, Paramethers: %s",
         stProgDetails.pszExecutable, acDir, acParam );
  hApp = WinStartApp( _hwndWaitApp, &stProgDetails, acParam, NULL,
                      SAF_INSTALLEDCMDLINE | SAF_STARTCHILDAPP );
  if ( hApp == NULLHANDLE )
  {
    debug( "Run failed. Path: %s, Paramethers: %s", acDir, acParam );
    return FALSE;
  }

  // Set "open" (in-use) state for the desktop icon.

  pAddData = (PAPPDATA)_wpAllocMem( somSelf, sizeof(APPDATA), NULL );
  if ( pAddData != NULL )
  {
    pAddData->stUseItem.type         = USAGE_OPENVIEW;
    pAddData->stViewItem.view        = OPEN_VNCV;
    pAddData->stViewItem.handle      = hApp;
    pAddData->stViewItem.ulViewState = VIEWSTATE_OPENING;
    _wpAddToObjUseList( somSelf, &pAddData->stUseItem );
  }

  return TRUE;
}


BOOL startSwithTo(vncv *somSelf)
{
  static WINHSWITCHFROMHAPP      *pWinHSWITCHfromHAPP = NULL;
  HSWITCH    hSwitch;
  PUSEITEM   pItem = _wpFindUseItem( somSelf, USAGE_OPENVIEW, NULL );

  if ( pItem == NULL )
  {
    debugCP( "UseItem not found" );
    return FALSE;
  }

  // Get address of the undocumented function WinHSWITCHfromHAPP.
  // Copypasted from xwphelpers\src\helpers\winh.c , function
  // HSWITCH winhHSWITCHfromHAPP(HAPP happ).
  if ( pWinHSWITCHfromHAPP == NULL )
    // first call: import WinHSWITCHfromHAPP
    // WinHSWITCHfromHAPP PMMERGE.5199
    doshQueryProcAddr( "PMMERGE", 5199, (PFN*)&pWinHSWITCHfromHAPP );

  if ( pWinHSWITCHfromHAPP == NULL )
  {
    debugCP( "WinHSWITCHfromHAPP routine address not found" );
    return FALSE;
  }

  // Query switch handle for the application handle.
  hSwitch = pWinHSWITCHfromHAPP( ((PAPPDATA)pItem)->stViewItem.handle );
  if ( hSwitch == NULLHANDLE )
  {
    debug( "Switch handle not found for happ=%u",
           ((PAPPDATA)pItem)->stViewItem.handle );
    return FALSE;
  }

  if ( WinSwitchToProgram( hSwitch ) != 0 )
  {
    debugCP( "WinSwitchToProgram() failed" );
    return FALSE;
  }

  return TRUE;
}

VOID startDestroy(vncv *somSelf)
{
  vncvData             *somThis = vncvGetData( somSelf );

  if ( _hwndWaitApp != NULLHANDLE )
  {
    // We use _WM_DESTROY message to destroy window from any thread.
    debug( "Destroy Wait-Window: %u", _hwndWaitApp );
    WinSendMsg( _hwndWaitApp, _WM_DESTROY, 0, 0 );
  }
}
