#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_DOSMODULEMGR
#define INCL_WIN
#include <os2.h>
#include "clntconn.h"
#include "lnchpad.h"
#include "prbar.h"
#include "resource.h"
#include "clntwnd.h"
#include <debug.h>

#define _AMOUSE_DLL                        "AMouDll.dll"
#define _AMOUSE_WINREGISTERFORWHEELMSG     "WinRegisterForWheelMsg"

HAB          hab = NULLHANDLE;
HMQ          hmq = NULLHANDLE;
ULONG        cOpenWin = 0;     // Opened windows counter for:
                               // 'lnchpad', 'progress' and 'clntwnd'.

BOOL _stdcall (*pfnWinRegisterForWheelMsg)(HWND hwnd, ULONG flWindow) = NULL;

#ifdef USE_AMOUSEREG
static HMODULE         hmAMouDll = NULLHANDLE;
#endif
static PPIB            pib;


BOOL _appInit()
{
  ULONG      ulRC;
  PTIB       tib;
#ifdef USE_AMOUSEREG
  CHAR       acError[256];
#endif

  debugInit();

  // Change process type code for use Win* API from VIO session.
  DosGetInfoBlocks( &tib, &pib );
  if ( pib->pib_ultype == 2 || pib->pib_ultype == 0 )
  {
    // VIO windowable or fullscreen protect-mode session.
    pib->pib_ultype = 3; // Presentation Manager protect-mode session.
    // ...and switch to the desktop (if we are in fullscreen now)?
  }

  // PM stuff...

  hab = WinInitialize( 0 );
  hmq = WinCreateMsgQueue( hab, 0 );
  if ( hmq == NULLHANDLE )
  {
    debug( "WinCreateMsgQueue() failed" );
    return FALSE;
  }

  ulRC = ccInit(); 
  if ( ulRC != CC_OK )
  {
    debug( "ccInit(), rc = %u - exit.", ulRC );
    WinMessageBox( HWND_DESKTOP, HWND_DESKTOP, "Could not load keyboard map.",
                   APP_NAME, 0, MB_ERROR | MB_OK );
    WinDestroyMsgQueue( hmq );
    WinTerminate( hab );
    return FALSE;
  }

#ifdef USE_AMOUSEREG
  // Load AMouse API (AMouDll.dll)
  pfnWinRegisterForWheelMsg = NULL;
  ulRC = DosLoadModule( acError, sizeof(acError), _AMOUSE_DLL, &hmAMouDll );
  if ( ulRC != NO_ERROR )
    debug( _AMOUSE_DLL" not loaded: %s", acError );
  else
  {
    ulRC = DosQueryProcAddr( hmAMouDll, 0, _AMOUSE_WINREGISTERFORWHEELMSG,
                             (PFN *)&pfnWinRegisterForWheelMsg );
    if ( ulRC != NO_ERROR )
    {
      debug( "Cannot find entry "_AMOUSE_WINREGISTERFORWHEELMSG" in "_AMOUSE_DLL );
      DosFreeModule( hmAMouDll );
      hmAMouDll = NULLHANDLE;
    }
    else
      debug( "AMouse API loaded" );
  }
#endif

  prbarRegisterClass( hab );
  cwInit();

  return TRUE;
}

VOID _appDone()
{
  if ( hmq != NULLHANDLE )
    WinDestroyMsgQueue( hmq );

  if ( hab != NULLHANDLE )
    WinTerminate( hab );

#ifdef USE_AMOUSEREG
  if ( hmAMouDll != NULLHANDLE )
    DosFreeModule( hmAMouDll );
#endif

  ccDone();
  cwDone();

  debugPCP( "--- Done ---" );
  debugDone();
}

#if 0
static BOOL _appHaveWindows()
{
  HWND       hwndTop;
  HENUM      henum;
  PID        pidWin;
  TID        tidWin;
  BOOL       fFound = FALSE;

  // Enumerate all top-level windows.           

  henum = WinBeginEnumWindows( HWND_DESKTOP );

  // Loop through all enumerated windows.       
  while( ( hwndTop = WinGetNextWindow( henum ) ) != NULLHANDLE )
  {
    if ( WinQueryWindowProcess( hwndTop, &pidWin, &tidWin ) &&
         ( pidWin == pib->pib_ulpid ) )
    {
      fFound = TRUE;
      break;
    }
  }

  WinEndEnumWindows( henum );

  return fFound;
}
#endif

static BOOL _appGetMsg(PQMSG pqmsg)
{
  if ( WinPeekMsg( hab, pqmsg, NULLHANDLE, 0, 0, PM_REMOVE ) )
    return TRUE;
/*
  We use the counter cOpenWin check instead of fnunc. _appHaveWindows() call.

  if ( !_appHaveWindows() )
    return FALSE;
*/
  ccThreadWatchdog();

  return ( cOpenWin > 0 ) && WinGetMsg( hab, pqmsg, 0, 0, 0 );
}


int main(int argc, char** argv)
{
  QMSG                 qmsg;

  if ( !_appInit() )
    return 1;

  if ( lpCommitArg( argc, argv ) )
  {
    while( _appGetMsg( &qmsg ) )
      WinDispatchMsg( hab, &qmsg );
  }

  _appDone();
  return 0;
}
