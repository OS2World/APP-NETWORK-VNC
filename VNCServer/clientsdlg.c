#define INCL_WIN
#define INCL_WINDIALOGS
#define INCL_GPI
#include <os2.h>
#include "config.h"
#include "rfbsrv.h"
#include "resource.h"
#include "srvwin.h"
#include "utils.h"

#include <chatwin.h>
#include "gui.h"

#include "clientsdlg.h"
#include <debug.h>

#define IDTIMER_UPDATERECORDS    1

extern HWND  hwndSrv;            // from main.c
extern HWND  hwndGUI;


typedef struct _DLGDATA {
  HWND                 hwndCtxMenu;
} DLGDATA, *PDLGDATA;

typedef struct _CLNTRECORD {
  MINIRECORDCORE       stRecCore;
  PSZ                  pszHost;
  PSZ                  pszTime;
  PSZ                  pszVersion;
  PSZ                  pszState;

  CHAR                 acTime[32];
  CHAR                 acVersion[32];
  rfbClientPtr         prfbClient;
} CLNTRECORD, *PCLNTRECORD;


static VOID _fillRecord(PCLNTRECORD pRecord, rfbClientPtr prfbClient)
{
  ULONG      ulSec;

  ulSec = time( NULL ) - ((PCLIENTDATA)prfbClient->clientData)->timeConnect;
  utilSecToStrTime( ulSec, sizeof(pRecord->acTime), pRecord->acTime );

  if ( prfbClient->protocolMajorVersion == 0 )
    pRecord->acVersion[0] = '\0';
  else
    sprintf( pRecord->acVersion, "%u.%u",
             prfbClient->protocolMajorVersion,
             prfbClient->protocolMinorVersion );

  switch( prfbClient->state )
  {
    case RFB_PROTOCOL_VERSION:
      pRecord->pszState = "Establishing protocol version";
      break;
    case RFB_SECURITY_TYPE:
      pRecord->pszState = "Negotiating security (RFB v.3.7)";
      break;
    case RFB_AUTHENTICATION:
      pRecord->pszState = "Authenticating";
      break;
    case RFB_INITIALISATION:
      pRecord->pszState = "Sending initialisation messages";
      break;
    case RFB_NORMAL:
      pRecord->pszState = prfbClient->viewOnly
                            ? "Normal, View-only"
                            : "Normal protocol messages";
      break;
    case RFB_INITIALISATION_SHARED:
      pRecord->pszState = "Sending initialisation messages with implicit shared-flag already true";
      break;
    default:
      pRecord->pszState = "unknown";
  }
}

static VOID _cnrRemoveRecord(HWND hwndCtl, PCLNTRECORD pRecord)
{
  if ( pRecord->pszHost != NULL )
    free( pRecord->pszHost );

  WinSendMsg( hwndCtl, CM_REMOVERECORD, MPFROMP(&pRecord),
              MPFROM2SHORT(1,CMA_FREE) );
}

static BOOL _cnrRemoveClient(HWND hwndCtl, rfbClientPtr prfbClient)
{
  PCLNTRECORD          pRecord = NULL;

  while( TRUE )
  {
    pRecord = (PCLNTRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD, MPFROMP(pRecord),
                pRecord == NULL ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER) );
    if ( pRecord == NULL )
      break;

    if ( pRecord->prfbClient == prfbClient )
    {
      _cnrRemoveRecord( hwndCtl, pRecord );
      break;
    }
  }

  return pRecord != NULL;
}

static VOID _cnrClear(HWND hwndCtl)
{
  PCLNTRECORD          pRecord = NULL;

  while( TRUE )
  {
    pRecord = (PCLNTRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD, MPFROMP(NULL),
                                       MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER) );
    if ( pRecord == NULL )
      break;

    _cnrRemoveRecord( hwndCtl, pRecord );
  }
}

static VOID _cnrAddClient(HWND hwndCtl, rfbClientPtr prfbClient, BOOL fInvalidate)
{
  RECORDINSERT  stRecIns;
  PCLNTRECORD   pRecord;

  // Allocate one record for the container.
  pRecord = (PCLNTRECORD)WinSendMsg( hwndCtl, CM_ALLOCRECORD,
                    MPFROMLONG( sizeof(CLNTRECORD) - sizeof(MINIRECORDCORE) ),
                    MPFROMLONG( 1 ) );
  if ( pRecord == NULL )
  {
    debugCP( "Record was not allocated" );
    return;
  }

  // Fill record.

  pRecord->pszHost    = strdup( prfbClient->host );
  pRecord->pszTime    = pRecord->acTime;
  pRecord->pszVersion = pRecord->acVersion;
  pRecord->prfbClient = prfbClient;
  _fillRecord( pRecord, prfbClient );

  // Insert record into the container.

  stRecIns.cb = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder = (PRECORDCORE)CMA_END;
  stRecIns.pRecordParent = NULL;
  stRecIns.zOrder = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert = 1;
  stRecIns.fInvalidateRecord = fInvalidate;
  WinSendMsg( hwndCtl, CM_INSERTRECORD, (PRECORDCORE)pRecord, &stRecIns );
}


static BOOL _wmInitDlg(HWND hwnd)
{
  PFIELDINFO           pFieldInfo;
  PFIELDINFO           pFldInf;
  CNRINFO              stCnrInf = { 0 };
  FIELDINFOINSERT      stFldInfIns = { 0 };
  CHAR                 acBuf[64];
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_CLIENTS );
  PDLGDATA             pData = calloc( 1, sizeof(DLGDATA) );

  if ( pData == NULL )
    return FALSE;

  // Make fields.

  pFldInf = (PFIELDINFO)WinSendMsg( hwndCtl, CM_ALLOCDETAILFIELDINFO,
                                    MPFROMLONG( 4 ), NULL );
  if ( pFldInf == NULL )
  {
    debugCP( "WTF?!" );
    free( pData );
    return FALSE;
  }
  pFieldInfo = pFldInf;

  WinLoadString( NULLHANDLE, 0, IDS_HOST, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT | CFA_VCENTER | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( CLNTRECORD, pszHost );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  stCnrInf.pFieldInfoLast = pFieldInfo;
  WinLoadString( NULLHANDLE, 0, IDS_TIME, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_CENTER | CFA_VCENTER | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( CLNTRECORD, pszTime );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  WinLoadString( NULLHANDLE, 0, IDS_VERSION, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_CENTER | CFA_VCENTER | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( CLNTRECORD, pszVersion );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  WinLoadString( NULLHANDLE, 0, IDS_STATE, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT | CFA_VCENTER | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( CLNTRECORD, pszState );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  stFldInfIns.cb = sizeof(FIELDINFOINSERT);
  stFldInfIns.pFieldInfoOrder = (PFIELDINFO)CMA_FIRST;
  stFldInfIns.cFieldInfoInsert = 4;
  WinSendMsg( hwndCtl, CM_INSERTDETAILFIELDINFO, MPFROMP( pFldInf ),
              MPFROMP( &stFldInfIns ) );

  stCnrInf.cb = sizeof(CNRINFO);
  stCnrInf.flWindowAttr = CV_DETAIL | CA_DETAILSVIEWTITLES |
                          CA_TITLEREADONLY | CFA_FITITLEREADONLY;
  WinSendMsg( hwndCtl, CM_SETCNRINFO, MPFROMP( &stCnrInf ),
              MPFROMLONG( CMA_PFIELDINFOLAST | CMA_FLWINDOWATTR ) );

  // Load context menu.
  pData->hwndCtxMenu = WinLoadMenu( HWND_DESKTOP, 0, IDMNU_CLIENTS );

  // Ask hidden server window to list all clients with command
  // WMCLNT_LISTCLIENT to our window.
  WinSendMsg( hwndSrv, WMSRV_LIST_CLIENTS, MPFROMHWND(hwnd),
              MPFROMLONG(WMCLNT_LISTCLIENT) );
  WinSendMsg( hwndCtl, CM_INVALIDATERECORD, 0, 0 );

  WinSetWindowULong( hwnd, QWL_USER, (ULONG)pData );
  WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ACTIVATE | SWP_SHOW );
  WinStartTimer( WinQueryAnchorBlock( hwnd ), hwnd, IDTIMER_UPDATERECORDS,
                 1000 );

  return TRUE;
}

static VOID _wmDestory(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCN_CLIENTS );
  PFIELDINFO pFieldInfo;

  // Free titles strings of details view.
  pFieldInfo = (PFIELDINFO)WinSendMsg( hwndCtl, CM_QUERYDETAILFIELDINFO, 0,
                                       MPFROMSHORT( CMA_FIRST ) );
  while( ( pFieldInfo != NULL ) && ( (LONG)pFieldInfo != -1 ) )
  {
    if ( pFieldInfo->pTitleData != NULL )
      free( pFieldInfo->pTitleData );

    pFieldInfo = (PFIELDINFO)WinSendMsg( hwndCtl, CM_QUERYDETAILFIELDINFO,
                    MPFROMP( pFieldInfo ), MPFROMSHORT( CMA_NEXT ) );
  }

  _cnrClear( hwndCtl );

  if ( pData != NULL )
  {
    WinDestroyWindow( pData->hwndCtxMenu );
    free( pData );
  }
}

static VOID _wmWindowPosChanged(HWND hwnd, PSWP pswp, ULONG flAwp)
{
  if ( ( (pswp->fl & SWP_SHOW) != 0 ) ||
       ( (pswp->fl & (SWP_SIZE | SWP_NOREDRAW | SWP_MINIMIZE | SWP_RESTORE))
           == SWP_SIZE ) )
  {
    // Resize container to fit dialog window.
    HWND     hwndCtl = WinWindowFromID( hwnd, IDCN_CLIENTS );
    RECTL    rectl;

    WinQueryWindowRect( hwnd, &rectl );
    WinCalcFrameRect( hwnd, &rectl, TRUE );
    WinSetWindowPos( hwndCtl, HWND_TOP,
                     rectl.xLeft, rectl.yBottom,
                     rectl.xRight - rectl.xLeft, rectl.yTop - rectl.yBottom,
                     SWP_SIZE | SWP_MOVE );
  }
}

static VOID _cmdContextMenu(HWND hwnd, PCLNTRECORD pRecord)
{
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_CLIENTS );
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  RECTL                stRectRec;
  RECTL                stRectCnr;
  POINTL               ptPointer;
  QUERYRECORDRECT      stQueryRect;
  rfbClientPtr         prfbClient = pRecord->prfbClient;
  BOOL                 fEnable;

  stQueryRect.cb                = sizeof(QUERYRECORDRECT);
  stQueryRect.pRecord           = (PRECORDCORE)pRecord;
  stQueryRect.fRightSplitWindow = FALSE;
  stQueryRect.fsExtent          = CMA_TEXT;

  if ( !(BOOL)WinSendMsg( hwndCtl, CM_QUERYRECORDRECT, MPFROMP(&stRectRec),
                         MPFROMP(&stQueryRect) ) )
    return;

  // Detect menu position.
  WinQueryWindowRect( hwndCtl, &stRectCnr );
  WinQueryMsgPos( WinQueryAnchorBlock( hwnd ), &ptPointer );
  WinMapWindowPoints( HWND_DESKTOP, hwndCtl, &ptPointer, 1 );
  if ( ptPointer.x < stRectCnr.xLeft || ptPointer.x > stRectCnr.xRight )
    ptPointer.x = ( stRectCnr.xLeft + stRectCnr.xRight ) / 2;
  ptPointer.y = (stRectRec.yBottom + stRectRec.yTop) / 2;

  // Select record if no records has been selected.
  if ( (PCLNTRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                                MPFROMLONG(CMA_FIRST),
                                MPFROMSHORT(CRA_SELECTED) ) == NULL )
    WinSendMsg( hwndCtl, CM_SETRECORDEMPHASIS, MPFROMP(pRecord),
                MPFROM2SHORT(TRUE, CRA_CURSORED | CRA_SELECTED) );

  // Enable/Disable items.

  fEnable = !prfbClient->viewOnly &&
            ( prfbClient->state == RFB_NORMAL ) &&
            ( ( prfbClient->protocolMajorVersion != 3 ) ||
              ( prfbClient->protocolMinorVersion != 3 ) );

  WinSendMsg( pData->hwndCtxMenu, MM_SETITEMATTR,
              MPFROM2SHORT(IDM_CHAT,FALSE),
              fEnable ? MPFROM2SHORT( MIA_DISABLED, 0 )
                      : MPFROM2SHORT( MIA_DISABLED, MIA_DISABLED ) );

  // Show context menu.
  WinPopupMenu( hwndCtl, hwnd, pData->hwndCtxMenu, ptPointer.x, ptPointer.y,
                IDM_ALLOW, PU_HCONSTRAIN | PU_VCONSTRAIN | PU_MOUSEBUTTON1 |
                PU_MOUSEBUTTON2 | PU_KEYBOARD );
}

static VOID _wmClntListClients(HWND hwnd, rfbClientPtr prfbClient)
{
  HWND          hwndCtl = WinWindowFromID( hwnd, IDCN_CLIENTS );
  PCLNTRECORD   pRecord = NULL;

  while( TRUE )
  {
    pRecord = (PCLNTRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD, MPFROMP(pRecord),
                pRecord == NULL ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER) );
    if ( pRecord == NULL )
    {
      // Record for client prfbClient not exists - create a new.
      _cnrAddClient( hwndCtl, prfbClient, FALSE );
      break;
    }

    if ( pRecord->prfbClient == prfbClient )
    {
      // Record for client prfbClient found - update it.
      _fillRecord( pRecord, prfbClient );
      WinSendMsg( hwndCtl, CM_INVALIDATERECORD, MPFROMP(&pRecord),
                  MPFROM2SHORT(1,CMA_TEXTCHANGED) );
      break;
    }
  }
}

static VOID _wmClntClntNumChanged(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  BOOL          fNewClient = SHORT2FROMMP(mp1) != 0;
  rfbClientPtr  prfbClient = (rfbClientPtr)PVOIDFROMMP(mp2);
  HWND          hwndCtl = WinWindowFromID( hwnd, IDCN_CLIENTS );

  if ( fNewClient )
  {
    PCLNTRECORD   pRecord = NULL;

    // Insert a new record for the client if it not exits.
    do
    {
      pRecord = (PCLNTRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD, MPFROMP(pRecord),
                  pRecord == NULL ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                  : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER) );
      if ( pRecord == NULL )
      {
        _cnrAddClient( hwndCtl, prfbClient, TRUE );
        break;
      }
    }
    while( pRecord->prfbClient != prfbClient );

    return;
  }

  // Search client's record and remove it.
  if ( _cnrRemoveClient( hwndCtl, prfbClient ) )
  {
    WinSendMsg( hwndCtl, CM_INVALIDATERECORD, 0, 0 );
    WinUpdateWindow( hwndCtl );
  }
}

static VOID _cmdDisconnect(HWND hwnd)
{
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_CLIENTS );
  PCLNTRECORD          pRecord = NULL;

  while( TRUE )
  {
    pRecord = (PCLNTRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                                       pRecord == NULL ? MPFROMLONG(CMA_FIRST)
                                                       : MPFROMP(pRecord),
                                       MPFROMSHORT(CRA_SELECTED) );
    if ( pRecord == NULL )
      break;

    WinPostMsg( hwndSrv, WMSRV_DISCONNECT, MPFROMP(pRecord->prfbClient), 0 );
  }
}

static VOID _cmdChat(HWND hwnd)
{
  rfbClientPtr prfbClient;
  PCLNTRECORD  pRecord = (PCLNTRECORD)WinSendDlgItemMsg( hwnd, IDCN_CLIENTS,
                                 CM_QUERYRECORDEMPHASIS, MPFROMLONG(CMA_FIRST),
                                 MPFROMSHORT(CRA_CURSORED) );

  if ( pRecord == NULL )
    return;

  prfbClient = pRecord->prfbClient;
  if ( ( ( prfbClient->protocolMajorVersion != 3 ) ||
         ( prfbClient->protocolMinorVersion != 6 ) ) &&
       ( utilMessageBox( hwnd, NULL, IDMSG_CHAT_NOT_DETECTED,
                         MB_YESNO | MB_ICONEXCLAMATION ) != MBID_YES ) )
    return;

  WinSendMsg( hwndSrv, WMSRV_CHAT_MESSAGE, MPFROMP(pRecord->prfbClient),
              MPFROMP(NULL) );
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmInitDlg( hwnd );

    case WM_DESTROY:
      _wmDestory( hwnd );
      break;

    case WM_WINDOWPOSCHANGED:
      _wmWindowPosChanged( hwnd, (PSWP)mp1, LONGFROMMP(mp2) );
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDM_DISCONNECT:
          _cmdDisconnect( hwnd );
          break;

        case IDM_CHAT:
          _cmdChat( hwnd );
          break;
      }
      return (MRESULT)0;

    case WM_CONTROL:
      if ( SHORT1FROMMP(mp1) == IDCN_CLIENTS )
      {
        switch( SHORT2FROMMP(mp1) )
        {
          case CN_CONTEXTMENU:
            if ( PVOIDFROMMP(mp2) != NULL )
              _cmdContextMenu( hwnd, (PCLNTRECORD)PVOIDFROMMP(mp2) );
            return (MRESULT)0;
        }
      }
      return (MRESULT)0;

    case WM_CLOSE:
      // Send dialog close command to the main gui window.
      WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_COMMAND, 
                  MPFROMSHORT(CMD_CLIENTS_CLOSE),
                  MPFROM2SHORT(CMDSRC_OTHER,FALSE) );
      break;

    case WM_TIMER:
      if ( LONGFROMMP(mp1) == IDTIMER_UPDATERECORDS )
      {
        HWND           hwndCtl = WinWindowFromID( hwnd, IDCN_CLIENTS );

        WinSendMsg( hwndSrv, WMSRV_LIST_CLIENTS, MPFROMHWND(hwnd),
                    MPFROMLONG(WMCLNT_LISTCLIENT) );
        WinSendMsg( hwndCtl, CM_INVALIDATERECORD, 0, 0 );
        WinUpdateWindow( hwndCtl );
      }
      return (MRESULT)0;

    case WMCLNT_LISTCLIENT:
      _wmClntListClients( hwnd, (rfbClientPtr)mp1 );
      return (MRESULT)TRUE;

    case WMCLNT_CLNTNUMCHANGED:
      _wmClntClntNumChanged( hwnd, mp1, mp2 );
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


HWND clientsCreate(HAB hab, HWND hwndOwner)
{
  HWND                 hwndDlg;

  hwndDlg = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, NULLHANDLE,
                        IDDLG_CLIENTS, NULL );
  if ( hwndDlg == NULLHANDLE )
  {
    debug( "WinLoadDlg(,,,,IDDLG_CLIENTS,) failed" );
  }

  return hwndDlg;
}
