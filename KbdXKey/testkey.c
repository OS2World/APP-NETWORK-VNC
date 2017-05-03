#include <string.h>
#define INCL_WIN
#include <os2.h>
#include "resource.h"
#include "os2xkey.h"
#include "namestbl.h"
#include <debug.h>

typedef struct _DLGINITDATA {
  USHORT               usSize;
  HWND                 hwndNamesTbl;
  PXKBDMAP             pMap;
} DLGINITDATA, *PDLGINITDATA;

typedef struct _DLGDATA {
  HWND                 hwndNamesTbl;
  PXKBDMAP             pMap;
  XKFROMKEYSYM         stXKFromKeysym;
  XKFROMMP             stXKFromMP;
  ULONG                ulKeysym;           // Current input keysym for "server".
} DLGDATA, *PDLGDATA;


static VOID _showServerEvent(HWND hwnd, ULONG ulKeysym, PSZ pszKeysym)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  XKEVENTSTR           stEvent;

  WinSetDlgItemText( hwnd, IDST_OUTKEYSYM, pszKeysym );

  // keysym to mp1,mp2.

  if ( ( ulKeysym != 0 ) &&
       xkMPFromKeysym( pData->pMap, ulKeysym, TRUE, &pData->stXKFromKeysym ) )
  {
    xkEventToStr( pData->stXKFromKeysym.aOutput[0].mp1,
                  pData->stXKFromKeysym.aOutput[0].mp2,
                  &stEvent );
  }
  else
    memset( &stEvent, 0, sizeof(XKEVENTSTR) );

  WinSetDlgItemText( hwnd, IDST_OUTFLAGS, stEvent.acFlags );
  WinSetDlgItemText( hwnd, IDST_OUTSCAN, stEvent.acScan );
  WinSetDlgItemText( hwnd, IDST_OUTCHAR, stEvent.acChar );
  WinSetDlgItemText( hwnd, IDST_OUTVK, stEvent.acVK );
  pData->ulKeysym = ulKeysym;
}

static BOOL _wmInitDlg(HWND hwnd, PDLGINITDATA pInitData)
{
  PDLGDATA   pData = malloc( sizeof(DLGDATA) );
  SWP        swp;

  if ( pData == NULL )
    return TRUE;

  pData->hwndNamesTbl = pInitData->hwndNamesTbl;
  pData->pMap = pInitData->pMap;
  xkMPFromKeysymStart( &pData->stXKFromKeysym );
  xkKeysymFromMPStart( &pData->stXKFromMP );
  WinSetWindowULong( hwnd, QWL_USER, (ULONG)pData );

  WinQueryWindowPos( hwnd, &swp );

  WinSetWindowPos( hwnd, HWND_TOP,
    ( WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN ) / 2 ) - (swp.cx / 2),
    ( WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN ) / 2 ) - (swp.cy / 2),
    0, 0, SWP_MOVE | SWP_NOADJUST );

  return FALSE; // Ok, set focus.
}

static VOID _wmDestory(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( pData != NULL )
    free( pData );
}

static VOID _wmChar(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  static PSZ apszMethod[] = { "unknown", "Auto-detected", "Exact match",
                              "Heuristically-detected" };
  PDLGDATA      pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  ULONG         ulMethod;
  CHAR          acKeysymCode[64];
  CHAR          acKeysym[64];
  XKEVENTSTR    stEvent;
  ULONG         ulKeysym = 0;

  if ( (SHORT1FROMMP(mp1) & KC_KEYUP) != 0 )
  {
    if ( xkKeysymFromMP( pData->pMap, mp1, mp2, &pData->stXKFromMP ) !=
         XKEYMETHOD_NOTFOUND )
      xkMPFromKeysym( pData->pMap, pData->stXKFromMP.aOutput[0].ulKeysym,
                      FALSE, &pData->stXKFromKeysym );
    return;
  }

  xkEventToStr( mp1, mp2, &stEvent );
  WinSetDlgItemText( hwnd, IDST_FLAGS, stEvent.acFlags );
  WinSetDlgItemText( hwnd, IDST_SCAN, stEvent.acScan );
  WinSetDlgItemText( hwnd, IDST_CHAR, stEvent.acChar );
  WinSetDlgItemText( hwnd, IDST_VK, stEvent.acVK );

  ulMethod = xkKeysymFromMP( pData->pMap, mp1, mp2, &pData->stXKFromMP );

  acKeysymCode[0] = '\0';
  acKeysym[0] = '\0';
  if ( ulMethod != XKEYMETHOD_NOTFOUND )
  {
    PSZ      pszName;

    ulKeysym = pData->stXKFromMP.aOutput[0].ulKeysym;
    pszName = (PSZ)WinSendMsg( pData->hwndNamesTbl, WM_NTQUERYNAME,
                               MPFROMLONG(ulKeysym), 0 );

    if ( pszName != NULL )
      strlcpy( acKeysym, pszName, sizeof(acKeysym) );

    sprintf( acKeysymCode, "0x%X", ulKeysym );
  }

  WinSetDlgItemText( hwnd, IDEF_KEYSYMCODE, acKeysymCode );
  WinSetDlgItemText( hwnd, IDEF_KEYSYM, acKeysym );
  WinSetDlgItemText( hwnd, IDST_METHOD, apszMethod[ulMethod] );
  _showServerEvent( hwnd, ulKeysym, acKeysym );
/*
  WinSetDlgItemText( hwnd, IDST_OUTKEYSYM, acKeysym );

  // keysym to mp1,mp2.

  xkMPFromKeysymStart( &stXKFromKeysym );
  if ( ( ulKeysym != 0 ) &&
       xkMPFromKeysym( pData->pMap, ulKeysym, TRUE, &stXKFromKeysym ) )
    xkEventToStr( stXKFromKeysym.aOutput[0].mp1, stXKFromKeysym.aOutput[0].mp2,
                  &stEvent );
  else
    memset( &stEvent, 0, sizeof(XKEVENTSTR) );

  WinSetDlgItemText( hwnd, IDST_OUTFLAGS, stEvent.acFlags );
  WinSetDlgItemText( hwnd, IDST_OUTSCAN, stEvent.acScan );
  WinSetDlgItemText( hwnd, IDST_OUTCHAR, stEvent.acChar );
  WinSetDlgItemText( hwnd, IDST_OUTVK, stEvent.acVK );
*/
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmInitDlg( hwnd, (PDLGINITDATA)mp2 );

    case WM_DESTROY:
      _wmDestory( hwnd );
      break;

    case WM_CONTROL:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDDLG_NAMESTBL:
          // Keysym has been selected by user.
          if ( SHORT2FROMMP(mp1) == NC_ENTER )
          {
            PNOTIFYNTENTER pNotify = (PNOTIFYNTENTER)mp2;

            _showServerEvent( hwnd, pNotify->ulValue, pNotify->pszName );
          }
          break;
      }
      return (MRESULT)TRUE;

    case WM_COMMAND:
      if ( SHORT1FROMMP(mp1) == IDPB_SELKEYSYM )
      {
        PDLGDATA       pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

        // Remove focus from the button to avoid loss of key SPACE presses.
        WinSetFocus( HWND_DESKTOP, WinWindowFromID( hwnd, IDST_FLAGS ) );

        WinSendMsg( pData->hwndNamesTbl, WM_NTSHOW, MPFROMLONG(pData->ulKeysym),
                    MPFROMLONG(TRUE) );
      }
      return (MRESULT)FALSE;

    case WM_CHAR:
      _wmChar( hwnd, mp1, mp2 );
      return (MRESULT)TRUE;

    case WM_TRANSLATEACCEL:
      return (MRESULT)FALSE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


VOID tkdlgShow(HAB hab, HWND hwndOwner, HWND hwndNamesTbl, PXKBDMAP pMap)
{
  HWND                 hwndDlg;
  DLGINITDATA          stInitData;

  stInitData.usSize       = sizeof(DLGINITDATA);
  stInitData.hwndNamesTbl = hwndNamesTbl;
  stInitData.pMap         = pMap;

  hwndDlg = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, NULLHANDLE,
                        IDDLG_TESTKEY, &stInitData );
  if ( hwndDlg == NULLHANDLE )
  {
    debug( "WinLoadDlg(,,,,IDDLG_TESTKEY,) failed" );
    return;
  }

  WinSetOwner( hwndNamesTbl, hwndDlg );
  WinProcessDlg( hwndDlg );
  WinSetOwner( hwndNamesTbl, hwndOwner );
  WinDestroyWindow( hwndDlg );
}
