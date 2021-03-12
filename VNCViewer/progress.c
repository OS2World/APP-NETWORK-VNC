#include <stdlib.h>
#include <types.h>
#include <stdio.h>
#define INCL_DOSERRORS
#define INCL_WIN
#define INCL_WINDIALOGS
#define INCL_GPI
#include <os2.h>
#include <utils.h>
#include <rfb/rfbclient.h>
#include "clntconn.h"
#include "clntwnd.h"
#include "lnchpad.h"
#include "pswddlg.h"
#include "resource.h"
#include "prbar.h"
#include "progress.h"
#include <debug.h>

#define TIMER_POPUP_ID           1
// TIMER_POPUP_TIMEOUT: Delay before appearance of the progress dialog [msec].
#define TIMER_POPUP_TIMEOUT      1500
#define TIMER_RECONNECT_ID       2
// TIMER_RECONNECT_TIMEOUT: Pause before next connection attempt [msec].
#define TIMER_RECONNECT_TIMEOUT  3000

#define WM_PR_SWITCH_TO          (WM_USER + 1)

typedef struct _DLGINITDATA {
  USHORT               usSize;
  PHOSTDATA            pHostData;
} DLGINITDATA, *PDLGINITDATA;

typedef struct _DLGDATA {
  HOSTDATA             stHostData;
  ULONG                ulAttempt;
  PCLNTCONN            pCC;
  PSZ                  pszPrBarText;
  ULONG                ulTimerPopupId;
  ULONG                ulTimerReconnectId;
  HWND                 hwndPswd;
  BOOL                 fUseMemorizedPswd;
  PCCLOGONINFO         pLogonInfo;
} DLGDATA, *PDLGDATA;


extern HAB             hab;
extern ULONG           cOpenWin;

static DLGINITDATA     stInitData;


// Progress dialog
// ---------------

// Creates a new RFB-protocol connection object.
static BOOL _startConnection(HWND hwnd)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND       hwndBar = WinWindowFromID( hwnd, IDPBAR_PROGRESS );
  CHAR       acBuf[128];

  if ( pDlgData->pCC != NULL )
    ccDestroy( pDlgData->pCC );

  pDlgData->pCC = ccCreate( &pDlgData->stHostData.stProperties, hwnd );
  if ( pDlgData->pCC == NULL )
    return FALSE;

  pDlgData->ulAttempt++;

  if ( WinSubstituteStrings( hwnd, pDlgData->pszPrBarText, sizeof(acBuf),
                             acBuf ) != 0 )
    // Substitution process on PogressBar text for key %2 (attempts).
    WinSetWindowText( hwndBar, acBuf );

  return TRUE;
}

static VOID _showProgress(HWND hwnd, PDLGDATA pDlgData)
{
  POINTL          pt;
  RECTL           rectDT, rectWin;

  WinQueryWindowRect( hwnd, &rectWin );
  WinQueryWindowRect( HWND_DESKTOP, &rectDT );
  pt.x = ( rectDT.xRight - ( rectWin.xRight - rectWin.xLeft ) ) / 2;
  pt.y = ( rectDT.yTop -   ( rectWin.yTop - rectWin.yBottom ) ) / 2;

  WinSetWindowPos( hwnd, HWND_TOP, pt.x, pt.y, 0, 0,
                   SWP_ACTIVATE | SWP_ZORDER | SWP_SHOW | SWP_MOVE );
}

static BOOL _wmInitDlg(HWND hwnd, PDLGINITDATA pInitData)
{
  PDLGDATA        pDlgData = calloc( 1, sizeof(DLGDATA) );
  HWND            hwndBar = WinWindowFromID( hwnd, IDPBAR_PROGRESS );
  CHAR            acBuf[256];
  CHAR            acTitle[256];
  PRBARINFO       stPrBarInfo;

  if ( pDlgData == NULL )
  {
    WinDestroyWindow( hwnd );
    return FALSE;
  }

  lpQueryWinPresParam( hwnd );

  WinSetWindowPtr( hwnd, QWL_USER, pDlgData );
  pDlgData->stHostData        = *pInitData->pHostData;
  pDlgData->ulTimerPopupId    = 0;
  pDlgData->fUseMemorizedPswd = pInitData->pHostData->fRememberPswd;

  // Set window title.
  WinQueryWindowText( hwnd, sizeof(acBuf), acBuf );
  WinSubstituteStrings( hwnd, acBuf, sizeof(acTitle), acTitle ); // %0=hostname
  WinSetWindowText( hwnd, acTitle );

  // Store ProgressBar text with key %2. It is not replaced now (see function
  // _wmSubstituteString()) but will be at each connection (_startConnection()).
  if ( WinQueryWindowText( hwndBar, sizeof(acBuf), acBuf ) != 0 )
    pDlgData->pszPrBarText = strdup( acBuf );

  _startConnection( hwnd );

  if ( pDlgData->stHostData.stProperties.lListenPort >= 0 )
  {
    // Progress bar text in "listen mode".

    WinLoadString( hab, 0, IDS_WAIT_SERVER, sizeof(acBuf), acBuf );
    WinSetWindowText( hwndBar, acBuf );
    stPrBarInfo.ulAnimation = PRBARA_RIGHT;

    // Popup immediately
    _showProgress( hwnd, pDlgData );
  }
  else
  {
    pDlgData->ulTimerPopupId = WinStartTimer( hab, hwnd, TIMER_POPUP_ID,
                                              TIMER_POPUP_TIMEOUT );
    stPrBarInfo.ulAnimation = PRBARA_LEFTDBL;
  }

  WinSendMsg( hwndBar, PBM_SETPARAM, MPFROMLONG(PBARSF_ANIMATION),
              MPFROMP(&stPrBarInfo) );
  return TRUE;
}

static VOID _wmDestroy(HWND hwnd)
{
  PDLGDATA        pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );

  lpStoreWinPresParam( hwnd );

  if ( pDlgData == NULL )
    return;

  if ( pDlgData->ulTimerPopupId != 0 )
    WinStopTimer( hab, hwnd, pDlgData->ulTimerPopupId );

  if ( pDlgData->ulTimerReconnectId != 0 )
    WinStopTimer( hab, hwnd, pDlgData->ulTimerReconnectId );

  if ( pDlgData->pszPrBarText != NULL )
    free( pDlgData->pszPrBarText );

  free( pDlgData );
}

static PSZ _wmSubstituteString(HWND hwnd, USHORT usKey)
{
  PDLGDATA             pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  static CHAR          acBuf[32];

  switch( usKey )
  {
    case 0: // %0 - host name.
      if ( pDlgData != NULL )
      {
        // The function called during WM_INITDLG for the window title.

        if ( pDlgData->stHostData.stProperties.lListenPort < 0 )
          return pDlgData->stHostData.stProperties.acHost;

        // "Listening mode"
        sprintf( acBuf, "%s:%u",
                 pDlgData->stHostData.stProperties.acHost[0] == '\0'
                   ? "*" : pDlgData->stHostData.stProperties.acHost,
                 pDlgData->stHostData.stProperties.lListenPort );
        return acBuf;
      }

    case 1: // %1 - maximum connect attempts (ProgressBar text).
      return ultoa( stInitData.pHostData->ulAttempts, acBuf, 10 ); 

    case 2: // %2 - number of connect attempt (ProgressBar text).
      // We don't touch key %2 in dialog loading time. Only when dialog data
      // installed, from _startConnection().
      if ( pDlgData != NULL )
        return ultoa( pDlgData->ulAttempt, acBuf, 10 ); 
  }

  return NULL;
}

static BOOL _wmTimer(HWND hwnd, ULONG ulTimerId)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );

  if ( ulTimerId == TIMER_POPUP_ID ) //pDlgData->ulTimerPopupId )
  {
    // Stop one-event timer for show (popup) window after creation.
    WinStopTimer( hab, hwnd, pDlgData->ulTimerPopupId );
    pDlgData->ulTimerPopupId = 0;

    _showProgress( hwnd, pDlgData );
  }
  else if ( ulTimerId == TIMER_RECONNECT_ID ) //pDlgData->ulTimerReconnectId )
  {
    // Stop one-event timer for reconnect time-out.
    PRBARINFO          stPrBarInfo;

    WinStopTimer( hab, hwnd, TIMER_RECONNECT_ID ); //pDlgData->ulTimerReconnectId );
    pDlgData->ulTimerReconnectId = 0;

    stPrBarInfo.ulImageIdx = 0;
    WinSendDlgItemMsg( hwnd, IDPBAR_PROGRESS, PBM_SETPARAM,
                       MPFROMLONG(PBARSF_IMAGE), MPFROMP(&stPrBarInfo) );

    WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER );
    _startConnection( hwnd );
  }
  else
    return FALSE; // Not our timer.

  return TRUE;
}

static VOID _cmdCancel(HWND hwnd)
{
  PDLGDATA          pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );

  if ( pDlgData->pCC != NULL )
  {
    ccDestroy( pDlgData->pCC );
    pDlgData->pCC = NULL;
  }

  WinDestroyWindow( hwnd );
}

static VOID _wmVNCState(HWND hwnd, ULONG ulState)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  PRBARINFO  stPrBarInfo;
  CHAR       acBuf[256];

  switch( ulState )
  {
    case RFBSTATE_READY:
    {
      BOOL   fReady = ccLockReadyState( pDlgData->pCC );

      debug( "RFBSTATE_READY: create main window, destroy progress dialog" );

      if ( // Logon information typed by user (obtained from dialog).
           ( pDlgData->pLogonInfo != NULL ) &&
           // Need to remember password.
           pDlgData->stHostData.fRememberPswd )
      {
        lpStoreLogonInfo( pDlgData->stHostData.stProperties.acHost,
                              pDlgData->pLogonInfo );
        memset( pDlgData->pLogonInfo, 0, sizeof(CCLOGONINFO) );
        free( pDlgData->pLogonInfo );
        pDlgData->pLogonInfo = NULL;
      }

      if ( fReady )
        cwCreate( pDlgData->pCC, pDlgData->stHostData.stProperties.acHost,
                  pDlgData->stHostData.acWinTitle,
                  pDlgData->stHostData.hwndNotify );
      ccUnlockReadyState( pDlgData->pCC );

      WinDestroyWindow( hwnd );
      break;
    }

    case RFBSTATE_WAITPASSWORD:
      debugCP( "RFBSTATE_WAITPASSWORD" );
      if ( pDlgData->hwndPswd != NULLHANDLE )
        debugCP( "WTF? Password dialog already opened?" );

      if ( pDlgData->fUseMemorizedPswd )
      {
        CCLOGONINFO    stLogonInfo;

        if ( !lpQueryLogonInfo( pDlgData->stHostData.stProperties.acHost,
                                &stLogonInfo, FALSE ) )
        {
          debug( "srvdlgQueryHostPswd() failed" );
          pDlgData->fUseMemorizedPswd = FALSE;
        }
        else
        {
          debugCP( "Send stored logon information..." );
          if ( !ccSendLogonInfo( pDlgData->pCC, &stLogonInfo ) )
            debugCP( "ccSendLogonInfo() failed" );
          memset( &stLogonInfo, 0, sizeof(CCLOGONINFO) );
          break;
        }
      }

      stPrBarInfo.ulAnimation = PRBARA_LEFT;
      WinSendDlgItemMsg( hwnd, IDPBAR_PROGRESS, PBM_SETPARAM,
                         MPFROMLONG(PBARSF_ANIMATION), MPFROMP(&stPrBarInfo) );

      _wmTimer( hwnd, pDlgData->ulTimerPopupId );
      WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER );
      pDlgData->hwndPswd = pswdlgOpen( pDlgData->pCC, hwnd, FALSE );
      break;

    case RFBSTATE_FINISH:
      {
        // Keep trying to connect on this messages.
        static PSZ     apszTempErr[] =
        {
          "Unable to connect to VNC server\n",
          "VNC authentication failed\n",
          "read (54: Connection reset by peer)\n",
          "VNC connection failed: "
        };
        ULONG          ulIdx;
        BOOL           fLogRec = ccQuerySessionInfo( pDlgData->pCC,
                                     CCSI_LAST_LOG_REC, sizeof(acBuf), acBuf );

        if ( fLogRec && ( pDlgData->stHostData.stProperties.lListenPort < 0 )
             && ( pDlgData->ulAttempt < pDlgData->stHostData.ulAttempts ) )
        {
          for( ulIdx = 0; ulIdx < ARRAYSIZE(apszTempErr); ulIdx++ )
          {
            if ( memcmp( acBuf, apszTempErr[ulIdx],
                         strlen( apszTempErr[ulIdx] ) ) == 0 )
              break; // end for()
          }

          if ( ulIdx < ARRAYSIZE(apszTempErr) )
          {
            // We don't use stored password for next connection attempts
            // (will ask user) if error other than
            // "Unable to connect to VNC server".
            pDlgData->fUseMemorizedPswd &= ( ulIdx == 0 );

            if ( pDlgData->pLogonInfo != NULL )
            {
              memset( pDlgData->pLogonInfo, 0, sizeof(CCLOGONINFO) );
              free( pDlgData->pLogonInfo );
              pDlgData->pLogonInfo = NULL;
            }

            stPrBarInfo.ulImageIdx = 1;
            WinSendDlgItemMsg( hwnd, IDPBAR_PROGRESS, PBM_SETPARAM,
                              MPFROMLONG(PBARSF_IMAGE), MPFROMP(&stPrBarInfo) );
            WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER );

            // Start reconnect timer.
            pDlgData->ulTimerReconnectId =
                                  WinStartTimer( hab, hwnd, TIMER_RECONNECT_ID,
                                                 TIMER_RECONNECT_TIMEOUT );
            break; // end case RFBSTATE_FINISH:
          }
        }

        // There is some error, after which we can not continue to try to
        // connect or it was last attempt. We show last log message to the user.

        // Stop all timers before show message box with error.

        if ( pDlgData->ulTimerPopupId != 0 )
        {
          WinStopTimer( hab, hwnd, pDlgData->ulTimerPopupId );
          pDlgData->ulTimerPopupId = 0;
        }

        if ( pDlgData->ulTimerReconnectId != 0 )
        {
          WinStopTimer( hab, hwnd, pDlgData->ulTimerReconnectId );
          pDlgData->ulTimerReconnectId = 0;
        }

        // Show a last message from the log.
        if ( fLogRec )
          WinMessageBox( HWND_DESKTOP, hwnd, acBuf, APP_NAME, 0,
                         MB_ERROR | MB_MOVEABLE | MB_OK );

        ccDestroy( pDlgData->pCC );
        WinDestroyWindow( hwnd );
      }

      break;
  }
}

static VOID _wmPswdEnter(HWND hwnd, PCCLOGONINFO pLogonInfo)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  PRBARINFO  stPrBarInfo;

  if ( pLogonInfo == NULL )
  {
    debugCP( "Password dialog has been canceled" );
    WinPostMsg( hwnd, WM_COMMAND, MPFROMSHORT(MBID_CANCEL), 0 );
    return;
  }

  stPrBarInfo.ulAnimation = PRBARA_LEFTDBL;
  WinSendDlgItemMsg( hwnd, IDPBAR_PROGRESS, PBM_SETPARAM,
                     MPFROMLONG(PBARSF_ANIMATION), MPFROMP(&stPrBarInfo) );

  debug( "%s received from password dialog, send it to the connection object...",
         pLogonInfo->fCredential ? "Credential" : "Password" );
  if ( !ccSendLogonInfo( pDlgData->pCC, pLogonInfo ) )
    debugCP( "ccSendLogonInfo() failed" );

  if ( pDlgData->stHostData.fRememberPswd )
  {
    if ( pDlgData->pLogonInfo )
      free( pDlgData->pLogonInfo );
    pDlgData->pLogonInfo = malloc( sizeof(CCLOGONINFO) );

    if ( pDlgData->pLogonInfo != NULL )
      memcpy( pDlgData->pLogonInfo, pLogonInfo, sizeof(CCLOGONINFO) );
  }

  WinDestroyWindow( pDlgData->hwndPswd );
  pDlgData->hwndPswd = NULLHANDLE;
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      cOpenWin++;
      return (MRESULT)_wmInitDlg( hwnd, (PDLGINITDATA)mp2 );

    case WM_DESTROY:
      _wmDestroy( hwnd );
      cOpenWin--;
      break;

    case WM_SUBSTITUTESTRING:
      return (MRESULT)_wmSubstituteString( hwnd, SHORT1FROMMP(mp1) );

    case WM_TIMER:
      if ( _wmTimer( hwnd, SHORT1FROMMP(mp1) ) )
        return (MRESULT)TRUE;
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case MBID_CANCEL:
          _cmdCancel( hwnd );
          break;
      }
      return (MRESULT)0;

    case WM_VNC_STATE:
      _wmVNCState( hwnd, LONGFROMMP(mp2) );
      return (MRESULT)TRUE;

    case WM_PSWD_ENTER:
      _wmPswdEnter( hwnd, (PCCLOGONINFO)PVOIDFROMMP(mp1) );
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


// Should be called from the main thread only.
BOOL prStart(PHOSTDATA pHostData)
{
  HWND         hwndDlg;

  stInitData.usSize     = sizeof(DLGINITDATA);
  stInitData.pHostData  = pHostData;

  hwndDlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, _dlgProc, NULLHANDLE,
                        IDDLG_PROGRESS, &stInitData );
  if ( hwndDlg == NULLHANDLE )
  {
    debug( "WinLoadDlg() failed" );
    return FALSE;
  }

  return TRUE;
}
