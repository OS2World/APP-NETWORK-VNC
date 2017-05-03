#define INCL_DOSERRORS
#define INCL_WIN
#define INCL_WINDIALOGS
#include <os2.h>
#include <types.h>
#include "resource.h"
#include "clntconn.h"
#include "lnchpad.h"
#include "pswddlg.h"
#include <debug.h>

static BOOL _wmInitDlg(HWND hwnd)
{
  HWND       hwndOwner = WinQueryWindow( hwnd, QW_OWNER );
  HWND       hwndCtl;
  POINTL     pointl = { 0 };
  RECTL      rectl;

  lpQueryWinPresParam( hwnd );

  WinQueryWindowRect( hwndOwner, &rectl );
  pointl.x = (rectl.xRight - rectl.xLeft) / 4;
  pointl.y = -(rectl.yTop - rectl.yBottom) / 2;
  WinMapWindowPoints( hwndOwner, HWND_DESKTOP, (PPOINTL)&pointl, 1 );

  WinSetWindowPos( hwnd, HWND_TOP, pointl.x, pointl.y, 0, 0,
                   SWP_ACTIVATE | SWP_ZORDER | SWP_SHOW | SWP_MOVE );

  hwndCtl = WinWindowFromID( hwnd, IDEF_USERNAME );
  WinSetFocus( HWND_DESKTOP,
               hwndCtl != NULLHANDLE
                 ? hwndCtl : WinWindowFromID( hwnd, IDEF_PASSWORD ) );

  // Button "Ok" will be enabled when some characters will be entered in
  // username entry field.
  WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ), hwndCtl == NULLHANDLE );

  return TRUE;
}

static VOID _cmdOk(HWND hwnd)
{
  CCLOGONINFO          stLogonInfo;
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDEF_USERNAME );

  if ( hwndCtl != NULLHANDLE )
  {
    stLogonInfo.fCredential = TRUE;
    WinQueryWindowText( hwndCtl, sizeof(stLogonInfo.acUserName),
                        stLogonInfo.acUserName );
    debugCP( "Credential entered, send it to the progress window" );
  }
  else
  {
    stLogonInfo.fCredential = FALSE;
    stLogonInfo.acUserName[0] = '\0';
    debugCP( "Password entered, send it to the progress window" );
  }

  stLogonInfo.acPassword[0] = '\0';
  WinQueryDlgItemText( hwnd, IDEF_PASSWORD,
                       sizeof(stLogonInfo.acPassword), stLogonInfo.acPassword );

  WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_PSWD_ENTER,
              MPFROMP( &stLogonInfo ), MPFROMHWND( hwnd ) );
  memset( &stLogonInfo, 0, sizeof(CCLOGONINFO) );
}

static VOID _cmdCancel(HWND hwnd)
{
  debugCP( "Password enter canceled" );
  WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_PSWD_ENTER,
              MPFROMP( NULL ), MPFROMHWND( hwnd ) );
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmInitDlg( hwnd );

    case WM_DESTROY:
      lpStoreWinPresParam( hwnd );
      break;

    case WM_CONTROL:
      if ( ( SHORT2FROMMP(mp1) == EN_CHANGE ) &&
           ( WinQueryWindowUShort( HWNDFROMMP(mp2), QWS_ID ) ==
               IDEF_USERNAME ) )
      {
        // Button "Ok" enabled when some characters is entered.
        // For user name entry field when user name and password requested or
        // for password entry field when VNC password requested.
        WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ),
                         WinQueryWindowTextLength( HWNDFROMMP(mp2) ) != 0 );
        break;
      }
      return (MRESULT)TRUE;

    case WM_COMMAND:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case MBID_OK:
          _cmdOk( hwnd );
          break;

        case MBID_CANCEL:
          _cmdCancel( hwnd );
          break;
      }
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


HWND pswdlgOpen(PCLNTCONN pCC, HWND hwndOwner, BOOL fCredential)
{
  HWND         hwndDlg;

  hwndDlg = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, NULLHANDLE,
                        fCredential ? IDDDLG_CREDENTIAL : IDDLG_PASSWORD,
                        NULL );
  if ( hwndDlg == NULLHANDLE )
  {
    debug( "WinLoadDlg() failed" );
    return NULLHANDLE;
  }

  debugCP( "Ok, password dialog opened" );
  return hwndDlg;
}

