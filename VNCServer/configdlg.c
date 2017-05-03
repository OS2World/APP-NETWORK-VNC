#define INCL_WIN
#define INCL_GPI
#define INCL_WINDIALOGS
#define INCL_WINWORKPLACE
#define INCL_DOSERRORS
#define INCL_DOSRESOURCES
#include <os2.h>
#include <stdlib.h>
#include <string.h>
#include <sys\socket.h>
#include <sys\ioctl.h>
#include <arpa\inet.h>
#include "config.h"
#include "configdlg.h"
#include "resource.h"
#define UTIL_INET_ADDR
#include "utils.h"
#include "srvwin.h"
#include "gui.h"
#include "rfbsrv.h"
#include <debug.h>

extern HWND  hwndSrv;  // from main.c
extern HWND  hwndGUI;

#define MAX_STAT_ADDR  64
#pragma pack(1)
typedef struct _IOSTATATADDR {
  ULONG		ulIP;
  USHORT	usIFIdx;
  ULONG		ulMask;
  ULONG		ulBroadcast;
} IOSTATATADDR, *PIOSTATATADDR;

typedef struct _IOSTATAT {
  USHORT		cAddr;
  IOSTATATADDR		aAddr[MAX_STAT_ADDR];
} IOSTATAT, *PIOSTATAT;
#pragma pack()


#define WM_READ_CONFIG           (WM_USER + 1)
#define WM_STORE_CONFIG          (WM_USER + 2)


static MRESULT _commonPageProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_COMMAND:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDPB_UNDO:
          {
            PCONFIG pConfig = (PCONFIG)WinQueryWindowPtr( hwnd, QWL_USER );

            WinSendMsg( hwnd, WM_READ_CONFIG, 0, MPFROMP(pConfig) );
          }
          break;

        case IDPB_DEFAULT:
          {
            PCONFIG    pConfig = cfgGetDefault();

            WinSendMsg( hwnd, WM_READ_CONFIG, 0, MPFROMP(pConfig) );
            cfgFree( pConfig );
          }
          break;
      }
      return (MRESULT)TRUE;

    case WM_CONTROL:
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


// Page 1: General
// ---------------

static VOID _wmPageGeneralInitDlg(HWND hwnd)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDLB_INTERFACES );
  IOSTATAT   stStat;
  int        iSock;
  PSZ        pszIP;
  ULONG      ulIdx;
  SHORT      sIdx;

  sIdx = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_INSERTITEM, MPFROMSHORT(0), "any" ) );
  WinSendMsg( hwndCtl, LM_SETITEMHANDLE, MPFROMSHORT(sIdx), MPFROMLONG(0) );

  // Gets all local IP addresses.

  iSock = socket( AF_INET, SOCK_RAW, 0 );
  if ( iSock == -1 )
    debug( "Cannot create a socket" );
  else
  {
    if ( os2_ioctl( iSock, SIOSTATAT, (caddr_t)&stStat, sizeof(IOSTATAT) )
           == -1 )
      debug( "os2_ioctl(,SIOSTATAT,,) fail, errno = %d", sock_errno() );
    else
    {
      for( ulIdx = 0; ulIdx < stStat.cAddr; ulIdx++ )
      {
        pszIP = stStat.aAddr[ulIdx].ulIP == 0x0100007F         // 127.0.0.1 ?
                  ? "localhost"
                  : inet_ntoa( *((struct in_addr *)&stStat.aAddr[ulIdx].ulIP) );
        sIdx = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_INSERTITEM,
                                         MPFROMSHORT(LIT_END), pszIP ) );
        WinSendMsg( hwndCtl, LM_SETITEMHANDLE, MPFROMSHORT(sIdx),
                    MPFROMLONG(stStat.aAddr[ulIdx].ulIP) );
      }
    }

    soclose( iSock );
  }

  WinSendMsg( hwndCtl, LM_SELECTITEM, 0, MPFROMSHORT(TRUE) );
}

static VOID _wmPageGeneralReadConfig(HWND hwnd, PCONFIG pConfig)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDLB_INTERFACES );
  SHORT      sIdx, cItems;

  // Passwords.
  WinCheckButton( hwnd, IDCB_PRIM_PSWD, pConfig->fPrimaryPassword ? 1 : 0 );
  WinSetDlgItemText( hwnd, IDEF_PRIM_PSWD, pConfig->acPrimaryPassword );
  WinEnableControl( hwnd, IDEF_PRIM_PSWD, pConfig->fPrimaryPassword );
  WinCheckButton( hwnd, IDCB_VO_PSWD, pConfig->fViewOnlyPassword ? 1 : 0 );
  WinSetDlgItemText( hwnd, IDEF_VO_PSWD, pConfig->acViewOnlyPassword );
  WinEnableControl( hwnd, IDEF_VO_PSWD, pConfig->fViewOnlyPassword );

  // Binding: select interface.

  cItems = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_QUERYITEMCOUNT, 0, 0 ) );
  for( sIdx = 0; sIdx < cItems; sIdx++ )
  {
    if ( LONGFROMMR( WinSendMsg( hwndCtl, LM_QUERYITEMHANDLE,
                                 MPFROMSHORT(sIdx), 0 ) ) ==
         pConfig->inaddrListen )
    {
      WinSendMsg( hwndCtl, LM_SELECTITEM, MPFROMSHORT(sIdx), MPFROMSHORT(1) );
      break;
    }
  }

  if ( sIdx == cItems )
  {
    // Nonexistent interface pointed by configuration - insert this one to list.

    sIdx = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_INSERTITEM,
                  MPFROMSHORT(LIT_END),
                  inet_ntoa( *((struct in_addr *)&pConfig->inaddrListen) ) ) );
    WinSendMsg( hwndCtl, LM_SETITEMHANDLE, MPFROMSHORT(sIdx),
                MPFROMLONG(pConfig->inaddrListen) );
    WinSendMsg( hwndCtl, LM_SELECTITEM, MPFROMSHORT(sIdx), MPFROMSHORT(1) );
  }

  // Binding: ports.

  WinSendDlgItemMsg( hwnd, IDSB_PORT, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pConfig->ulPort ), 0 );
  WinSendDlgItemMsg( hwnd, IDSB_HTTP_PORT, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pConfig->ulHTTPPort ), 0 );
}

static VOID _wmPageGeneralStoreConfig(HWND hwnd, PCONFIG pConfig)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDLB_INTERFACES );
  SHORT      sIdx;
  ULONG      ulPort;

  pConfig->fPrimaryPassword =
                       WinQueryButtonCheckstate( hwnd, IDCB_PRIM_PSWD ) != 0;
  WinQueryDlgItemText( hwnd, IDEF_PRIM_PSWD, sizeof(pConfig->acPrimaryPassword),
                       pConfig->acPrimaryPassword );
  pConfig->fViewOnlyPassword =
                       WinQueryButtonCheckstate( hwnd, IDCB_VO_PSWD ) != 0;
  WinQueryDlgItemText( hwnd, IDEF_VO_PSWD, sizeof(pConfig->acViewOnlyPassword),
                       pConfig->acViewOnlyPassword );

  sIdx = (LONG)WinSendMsg( hwndCtl, LM_QUERYSELECTION, MPFROMSHORT(LIT_FIRST),
                           0 );
  pConfig->inaddrListen = (in_addr_t)WinSendMsg( hwndCtl, LM_QUERYITEMHANDLE,
                                                 MPFROMSHORT(sIdx), 0 );

  WinSendDlgItemMsg( hwnd, IDSB_PORT, SPBM_QUERYVALUE,
                     MPFROMP( &ulPort ), MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );
  if ( ( ulPort > 0 ) && ( ulPort <= 0xFFFF ) )
    pConfig->ulPort = ulPort;

  WinSendDlgItemMsg( hwnd, IDSB_HTTP_PORT, SPBM_QUERYVALUE,
                     MPFROMP( &ulPort ), MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );
  if ( ( ulPort > 0 ) && ( ulPort <= 0xFFFF ) )
    pConfig->ulHTTPPort = ulPort;
}

static VOID _pageGeneralCheckPorts(HWND hwnd)
{
  ULONG      ulPort, ulHTTPPort;
  HWND       hwndPort = WinWindowFromID( hwnd, IDSB_PORT );
  HWND       hwndHTTPPort = WinWindowFromID( hwnd, IDSB_HTTP_PORT );
  HWND       hwndIfAddr = WinWindowFromID( hwnd, IDLB_INTERFACES );
  SHORT      sIdx;
  ULONG      ulColor = CLR_RED;
  ULONG      ulRC;
  in_addr_t  inaddrListen;

  // Query typed ports values.
  WinSendMsg( hwndPort, SPBM_QUERYVALUE, MPFROMP( &ulPort ),
              MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );
  WinSendMsg( hwndHTTPPort, SPBM_QUERYVALUE, MPFROMP( &ulHTTPPort ),
              MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );

  // Query selected listen interface address.
  sIdx = (LONG)WinSendMsg( hwndIfAddr, LM_QUERYSELECTION,
                           MPFROMSHORT(LIT_FIRST), 0 );
  inaddrListen = (in_addr_t)WinSendMsg( hwndIfAddr, LM_QUERYITEMHANDLE,
                                        MPFROMSHORT(sIdx), 0 );
  // Check if ports atre available.
  ulRC = rbfsCheckPorts( inaddrListen, ulPort, ulHTTPPort );
  
  if ( (ulRC & 1) == 0 )
    // Listen port is busy and cannot be used.
    WinSetPresParam( hwndPort, PP_FOREGROUNDCOLORINDEX, sizeof(ULONG),
                     (PVOID)&ulColor );
  
  if ( (ulRC & 2) == 0 )
    // Listen HTTP port is busy and cannot be used.
    WinSetPresParam( hwndHTTPPort, PP_FOREGROUNDCOLORINDEX, sizeof(ULONG),
                     (PVOID)&ulColor );
}

static MRESULT EXPENTRY _dlgPageGeneralProc(HWND hwnd, ULONG msg,
                                            MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      _wmPageGeneralInitDlg( hwnd );

    case WM_READ_CONFIG:
      _wmPageGeneralReadConfig( hwnd, (PCONFIG)mp2 );
      return (MRESULT)TRUE;

    case WM_STORE_CONFIG:
      _wmPageGeneralStoreConfig( hwnd, (PCONFIG)mp1 );
      break;

    case WM_CONTROL:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDCB_PRIM_PSWD:
          if ( SHORT2FROMMP(mp1) == BN_CLICKED )
            WinEnableControl( hwnd, IDEF_PRIM_PSWD,
                      (BOOL)WinQueryButtonCheckstate( hwnd, IDCB_PRIM_PSWD ) );
          return (MRESULT)0;

        case IDCB_VO_PSWD:
          if ( SHORT2FROMMP(mp1) == BN_CLICKED )
            WinEnableControl( hwnd, IDEF_VO_PSWD,
                      (BOOL)WinQueryButtonCheckstate( hwnd, IDCB_VO_PSWD ) );
          return (MRESULT)0;

        default:
          if (
               ( ( SHORT1FROMMP(mp1) == IDSB_PORT ||
                   SHORT1FROMMP(mp1) == IDSB_HTTP_PORT )
               &&
                 ( SHORT2FROMMP(mp1) == SPBN_CHANGE ) )
             ||
               ( ( SHORT1FROMMP(mp1) == IDLB_INTERFACES )
               &&
                 ( SHORT2FROMMP(mp1) == CBN_EFCHANGE ) )
             )
          {
            // Listen interface or one of ports is changed.

            // Reset colors for ports spin buttons.
            WinRemovePresParam( WinWindowFromID( hwnd, IDSB_PORT ),
                                PP_FOREGROUNDCOLORINDEX );
            WinRemovePresParam( WinWindowFromID( hwnd, IDSB_HTTP_PORT ),
                                PP_FOREGROUNDCOLORINDEX );
            // Start/restart timer to check ports values.
            WinStartTimer( WinQueryAnchorBlock( hwnd ), hwnd, 1, 1000 );
          }
          return (MRESULT)0;
      }
      break;

    case WM_TIMER:
      if ( SHORT1FROMMP(mp1) == 1 )
      {
        WinStopTimer( WinQueryAnchorBlock( hwnd ), hwnd, 1 );
        _pageGeneralCheckPorts( hwnd );
      }
      return (MRESULT)0;
  }

  return _commonPageProc( hwnd, msg, mp1, mp2 );
}


// Page 2: Access
// ---------------

#define _ACL_ADDR_BUF_LEN        32

typedef struct _ACLRECORD {
  MINIRECORDCORE       stRecCore;
  HPOINTER             hptrIcon;
  PSZ                  pszAddress;
  PSZ                  pszAccess;
  PSZ                  pszComment;

  CHAR                 szAddress[_ACL_ADDR_BUF_LEN];           // IP/Mask
  ACLITEM              stACLItem;
} ACLRECORD, *PACLRECORD;

typedef struct _CNACLDATA {
  CHAR                 acAllow[24];
  CHAR                 acDeny[24];
  HPOINTER             hptrEnable;
  HPOINTER             hptrDisable;
  HWND                 hwndCtxMenu;
} CNACLDATA, *PCNACLDATA;

#define _IS_REC_VALID(pr) ( ( pr != (PACLRECORD)(-1) ) && ( pr != NULL ) )
#define _IS_FLD_VALID(pf) ( ( pf != (PFIELDINFO)(-1) ) && ( pf != NULL ) )

// Makes the necessary changes in the record when data in pRecord->stACLItem
// changed.
static VOID _aclRecordACLItemChanged(PACLRECORD pRecord, PCNACLDATA pCnACLData)
{
  pRecord->hptrIcon = pRecord->stACLItem.fEnable ?
                         pCnACLData->hptrEnable : pCnACLData->hptrDisable;

  utilInAddrRangeToStr( &pRecord->stACLItem.stInAddr1,
                        &pRecord->stACLItem.stInAddr2,
                        sizeof(pRecord->szAddress), pRecord->szAddress );

  pRecord->pszAccess = pRecord->stACLItem.fAllow ?
                         pCnACLData->acAllow : pCnACLData->acDeny;
}

// Scroll container to the record.
static VOID _aclMakeVisible(HWND hwndCtl, PACLRECORD pRecord)
{
  RECTL                stRectVP;

  if ( (BOOL)WinSendMsg( hwndCtl, CM_QUERYVIEWPORTRECT, MPFROMP(&stRectVP),
                         MPFROM2SHORT(CMA_WINDOW, FALSE) ) )
  {
    RECTL                stRectRec;
    QUERYRECORDRECT      stQueryRect;

    stQueryRect.cb = sizeof(QUERYRECORDRECT);
    stQueryRect.pRecord = (PRECORDCORE)pRecord;
    stQueryRect.fRightSplitWindow = FALSE;
    stQueryRect.fsExtent = CMA_TEXT;

    if ( (BOOL)WinSendMsg( hwndCtl, CM_QUERYRECORDRECT, MPFROMP(&stRectRec),
                           MPFROMP(&stQueryRect) ) )
      WinSendMsg( hwndCtl, CM_SCROLLWINDOW, MPFROMSHORT(CMA_VERTICAL),
                  MPFROMLONG( stRectVP.yTop < stRectRec.yTop
                                ? stRectVP.yTop - stRectRec.yTop
                                : stRectVP.yBottom > stRectRec.yBottom ?
                                   stRectVP.yBottom - stRectRec.yBottom : 0 ) );
  }
}

// Open editor for the address (address range) at specified record in
// ACL container.
static VOID _aclOpenEdit(HWND hwndCtl, PACLRECORD pRecord, ULONG ulOffStruct)
{
  CNREDITDATA          stCnrEditData;

  _aclMakeVisible( hwndCtl, pRecord );

  memset( &stCnrEditData, '\0', sizeof(CNREDITDATA) );
  stCnrEditData.cb = sizeof(CNREDITDATA);
  stCnrEditData.hwndCnr = hwndCtl;
  stCnrEditData.pRecord = (PRECORDCORE)pRecord;
  stCnrEditData.id = CID_LEFTDVWND;
  stCnrEditData.pFieldInfo = NULL;

  do
    stCnrEditData.pFieldInfo = (PFIELDINFO)
      WinSendMsg( hwndCtl, CM_QUERYDETAILFIELDINFO,
                  MPFROMP(stCnrEditData.pFieldInfo),
                  stCnrEditData.pFieldInfo == NULL
                    ? MPFROMSHORT(CMA_FIRST) : MPFROMSHORT(CMA_NEXT) );
  while( _IS_FLD_VALID( stCnrEditData.pFieldInfo ) &&
         ( stCnrEditData.pFieldInfo->offStruct != ulOffStruct ) );

  if ( _IS_FLD_VALID( stCnrEditData.pFieldInfo ) )
    WinSendMsg( hwndCtl, CM_OPENEDIT, MPFROMP(&stCnrEditData), 0 );
}

// Displays a pop-up menu for the record.
static VOID _aclContextMenu(HWND hwnd, PACLRECORD pRecord)
{
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PCNACLDATA           pCnACLData = (PCNACLDATA)WinQueryWindowPtr( hwndCtl,
                                                                   QWL_USER );
  RECTL                stRectRec;
  RECTL                stRectCnr;
  POINTL               ptPointer;
  QUERYRECORDRECT      stQueryRect;

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
  // Select record.
  WinSendMsg( hwndCtl, CM_SETRECORDEMPHASIS, MPFROMP(pRecord),
              MPFROM2SHORT(TRUE, CRA_CURSORED | CRA_SELECTED) );

  // Check items: Enable/Disable.
  WinSendMsg( pCnACLData->hwndCtxMenu, MM_SETITEMATTR,
              pRecord->stACLItem.fEnable
                ? MPFROM2SHORT( IDM_ENABLE, TRUE )
                : MPFROM2SHORT( IDM_DISABLE, TRUE ),
              MPFROM2SHORT( MIA_CHECKED, MIA_CHECKED ) );
  WinSendMsg( pCnACLData->hwndCtxMenu, MM_SETITEMATTR,
              !pRecord->stACLItem.fEnable
                ? MPFROM2SHORT( IDM_ENABLE, TRUE )
                : MPFROM2SHORT( IDM_DISABLE, TRUE ),
              MPFROM2SHORT( MIA_CHECKED, 0 ) );

  // Check items: Allow/Deny.
  WinSendMsg( pCnACLData->hwndCtxMenu, MM_SETITEMATTR,
              pRecord->stACLItem.fAllow
                ? MPFROM2SHORT( IDM_ALLOW, TRUE )
                : MPFROM2SHORT( IDM_DENY, TRUE ),
              MPFROM2SHORT( MIA_CHECKED, MIA_CHECKED ) );
  WinSendMsg( pCnACLData->hwndCtxMenu, MM_SETITEMATTR,
              !pRecord->stACLItem.fAllow
                ? MPFROM2SHORT( IDM_ALLOW, TRUE )
                : MPFROM2SHORT( IDM_DENY, TRUE ),
              MPFROM2SHORT( MIA_CHECKED, 0 ) );

  // Close editor and show menu.
  WinSendMsg( hwndCtl, CM_CLOSEEDIT, 0, 0 );
  WinPopupMenu( hwndCtl, hwnd, pCnACLData->hwndCtxMenu,
                ptPointer.x, ptPointer.y,
                IDM_ALLOW, PU_HCONSTRAIN | PU_VCONSTRAIN | PU_MOUSEBUTTON1 |
                PU_MOUSEBUTTON2 | PU_KEYBOARD );
}

static VOID _wmPageAccessInitDlg(HWND hwnd)
{
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PFIELDINFO           pFieldInfo;
  PFIELDINFO           pFldInf;
  CNRINFO              stCnrInf = { 0 };
  FIELDINFOINSERT      stFldInfIns = { 0 };
  CHAR                 acBuf[64];
  PCNACLDATA           pCnACLData = malloc( sizeof(CNACLDATA) );

  // Setup ACL container.

  // Container data.

  if ( pCnACLData == NULL )
  {
    debug( "Not enough memory" );
    return;
  }

  WinLoadString( NULLHANDLE, 0, IDS_ALLOW, sizeof(pCnACLData->acAllow),
                 pCnACLData->acAllow );
  WinLoadString( NULLHANDLE, 0, IDS_DENY, sizeof(pCnACLData->acDeny),
                 pCnACLData->acDeny );
  pCnACLData->hptrDisable = WinLoadPointer( HWND_DESKTOP, 0,
                                            IDICON_ACL_DISABLE );
  pCnACLData->hptrEnable  = WinLoadPointer( HWND_DESKTOP, 0,
                                            IDICON_ACL_ENABLE );

  WinSetWindowPtr( hwndCtl, QWL_USER, pCnACLData );

  // Fields.

  pFldInf = (PFIELDINFO)WinSendMsg( hwndCtl, CM_ALLOCDETAILFIELDINFO,
                                    MPFROMLONG( 4 ), NULL );
  if ( pFldInf == NULL )
    debugPCP( "WTF?!" );
  else
  {
    pFieldInfo = pFldInf;

    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_BITMAPORICON | CFA_LEFT | CFA_VCENTER;
    pFieldInfo->flTitle = CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = NULL;
    pFieldInfo->offStruct = FIELDOFFSET( ACLRECORD, hptrIcon );
    pFieldInfo = pFieldInfo->pNextFieldInfo;

    WinLoadString( NULLHANDLE, 0, IDS_SOURCE, sizeof(acBuf), acBuf );
    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT | CFA_VCENTER | CFA_SEPARATOR;
    pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = strdup( acBuf );
    pFieldInfo->offStruct = FIELDOFFSET( ACLRECORD, pszAddress );
    pFieldInfo = pFieldInfo->pNextFieldInfo;

    stCnrInf.pFieldInfoLast = pFieldInfo;
    WinLoadString( NULLHANDLE, 0, IDS_ACCESS, sizeof(acBuf), acBuf );
    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_CENTER | CFA_VCENTER | CFA_SEPARATOR;
    pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = strdup( acBuf );
    pFieldInfo->offStruct = FIELDOFFSET( ACLRECORD, pszAccess );
    pFieldInfo = pFieldInfo->pNextFieldInfo;

    WinLoadString( NULLHANDLE, 0, IDS_COMMENT, sizeof(acBuf), acBuf );
    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT;
    pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = strdup( acBuf );
    pFieldInfo->offStruct = FIELDOFFSET( ACLRECORD, pszComment );

    stFldInfIns.cb = sizeof(FIELDINFOINSERT);
    stFldInfIns.pFieldInfoOrder = (PFIELDINFO)CMA_FIRST;
    stFldInfIns.cFieldInfoInsert = 4;
    WinSendMsg( hwndCtl, CM_INSERTDETAILFIELDINFO, MPFROMP( pFldInf ),
                MPFROMP( &stFldInfIns ) );
  }

  stCnrInf.cb = sizeof(CNRINFO);
  stCnrInf.flWindowAttr = CV_DETAIL | CA_DETAILSVIEWTITLES |
                          CA_TITLEREADONLY | CFA_FITITLEREADONLY;
  stCnrInf.slBitmapOrIcon.cx = 16;
  stCnrInf.slBitmapOrIcon.cy = 16;
  WinSendMsg( hwndCtl, CM_SETCNRINFO, MPFROMP( &stCnrInf ),
              MPFROMLONG( CMA_PFIELDINFOLAST | CMA_FLWINDOWATTR |
                          CMA_SLBITMAPORICON ) );

  // Load context menu.
  pCnACLData->hwndCtxMenu = WinLoadMenu( hwndCtl, 0, IDMNU_ACL );
}

static VOID _wmPageAccessReadConfig(HWND hwnd, PCONFIG pConfig)
{
  HWND          hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PCNACLDATA    pCnACLData = (PCNACLDATA)WinQueryWindowPtr( hwndCtl, QWL_USER );
  RECORDINSERT  stRecIns;
  PACLRECORD    pRecord, pRecords;
  PACLITEM      pACLItem;
  ULONG         ulIdx;

  if ( pCnACLData == NULL )
     return;

  // Clean ACL container.
  WinSendMsg( hwndCtl, CM_CLOSEEDIT, 0, 0 );
  WinSendMsg( hwndCtl, CM_REMOVERECORD, MPFROMP(NULL),
              MPFROM2SHORT(0,CMA_FREE | CMA_INVALIDATE) );

  // Allocate records for the container.
  pRecords = (PACLRECORD)WinSendMsg( hwndCtl, CM_ALLOCRECORD,
                 MPFROMLONG( sizeof(ACLRECORD) - sizeof(MINIRECORDCORE) ),
                 MPFROMLONG( aclCount( &pConfig->stACL ) ) );

  pRecord = pRecords;
  for( ulIdx = 0; ulIdx < aclCount( &pConfig->stACL ); ulIdx++ )
  {
    pACLItem = aclAt( &pConfig->stACL, ulIdx );
    pRecord->pszAddress = pRecord->szAddress;
    pRecord->pszComment = pRecord->stACLItem.acComment;
    pRecord->stACLItem = *pACLItem;
    _aclRecordACLItemChanged( pRecord, pCnACLData );

    pRecord = (PACLRECORD)pRecord->stRecCore.preccNextRecord;
  }

  // Insert records to the container.
  stRecIns.cb = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder = (PRECORDCORE)CMA_END;
  stRecIns.pRecordParent = NULL;
  stRecIns.zOrder = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert = aclCount( &pConfig->stACL );
  stRecIns.fInvalidateRecord = TRUE;
  WinSendMsg( hwndCtl, CM_INSERTRECORD, (PRECORDCORE)pRecords, &stRecIns );

  // Shared sessions. Select radio button.

  if ( !pConfig->fNeverShared && !pConfig->fAlwaysShared &&
       !pConfig->fDontDisconnect )
    ulIdx = IDRB_SHARED_ASIS;
  else if ( !pConfig->fNeverShared && !pConfig->fAlwaysShared &&
            pConfig->fDontDisconnect )
    ulIdx = IDRB_SHARED_ASIS_DONTDISCON;
  else if ( !pConfig->fNeverShared && pConfig->fAlwaysShared )
    ulIdx = IDRB_ALWAYSSHARED;
  else if ( pConfig->fNeverShared && !pConfig->fDontDisconnect )
    ulIdx = IDRB_NEVERSHARED;
  else
    ulIdx = IDRB_NEVERSHARED_DONTDISCON;

  WinCheckButton( hwnd, ulIdx, 1 );
}

static VOID _wmPageAccessDestroy(HWND hwnd)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PCNACLDATA pCnACLData = (PCNACLDATA)WinQueryWindowPtr( hwndCtl, QWL_USER );
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

  if ( pCnACLData != NULL )
  {
    WinDestroyPointer( pCnACLData->hptrDisable );
    WinDestroyPointer( pCnACLData->hptrEnable );
    WinDestroyWindow( pCnACLData->hwndCtxMenu );
    free( pCnACLData );
  }
}

static VOID _wmPageAccessStoreConfig(HWND hwnd, PCONFIG pConfig)
{
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PACLRECORD           pRecord = NULL;

  aclFree( &pConfig->stACL );
  aclInit( &pConfig->stACL );

  while( TRUE )
  {
    pRecord = (PACLRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD, MPFROMP(pRecord),
                        pRecord == NULL ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER) :
                                          MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER) );
    if ( !_IS_REC_VALID( pRecord ) )
      break;

    if ( !aclInsert( &pConfig->stACL, ~0, &pRecord->stACLItem ) )
      debug( "aclInsert() failed" );
  }

  if ( WinQueryButtonCheckstate( hwnd, IDRB_SHARED_ASIS ) )
  {
    pConfig->fNeverShared    = FALSE;
    pConfig->fAlwaysShared   = FALSE;
    pConfig->fDontDisconnect = FALSE;
  }
  else if ( WinQueryButtonCheckstate( hwnd, IDRB_SHARED_ASIS_DONTDISCON ) )
  {
    pConfig->fNeverShared    = FALSE;
    pConfig->fAlwaysShared   = FALSE;
    pConfig->fDontDisconnect = TRUE;
  }
  else if ( WinQueryButtonCheckstate( hwnd, IDRB_ALWAYSSHARED ) )
  {
    pConfig->fNeverShared    = FALSE;
    pConfig->fAlwaysShared   = TRUE;
  }
  else if ( WinQueryButtonCheckstate( hwnd, IDRB_NEVERSHARED ) )
  {
    pConfig->fNeverShared    = TRUE;
    pConfig->fDontDisconnect = FALSE;
  }
  else
  {
    pConfig->fNeverShared    = TRUE;
    pConfig->fDontDisconnect = TRUE;
  }
}

// Creates a new record in the ACL container.
static VOID _wmPageAccessCmdACLNew(HWND hwnd)
{
  HWND          hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PCNACLDATA    pCnACLData = (PCNACLDATA)WinQueryWindowPtr( hwndCtl, QWL_USER );
  PACLRECORD    pRecord, pOtherRec;
  RECORDINSERT  stRecIns;

  if ( pCnACLData == NULL )
    return;

  // Allocate records for the container.
  pRecord = (PACLRECORD)WinSendMsg( hwndCtl, CM_ALLOCRECORD,
                      MPFROMLONG( sizeof(ACLRECORD) - sizeof(MINIRECORDCORE) ),
                      MPFROMLONG( 1 ) );

  // Fill record - set default values.

  *((PULONG)pRecord->szAddress) = 0x00796E61; // "any\0"
  utilStrToInAddrRange( 3, pRecord->szAddress, &pRecord->stACLItem.stInAddr1,
                        &pRecord->stACLItem.stInAddr2 );

  pRecord->stACLItem.acComment[0] = '\0';
  pRecord->stACLItem.fEnable      = TRUE;
  pRecord->stACLItem.fAllow       = TRUE;
  pRecord->hptrIcon               = pCnACLData->hptrEnable;
  pRecord->pszAccess              = pCnACLData->acAllow;

  pRecord->pszAddress = pRecord->szAddress;
  pRecord->pszComment = pRecord->stACLItem.acComment;

  // Insert records to the container (before cursored record).

  pOtherRec = (PACLRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                                    MPFROMLONG(CMA_FIRST),
                                    MPFROMSHORT(CRA_CURSORED) );
  if ( _IS_REC_VALID( pOtherRec ) )
    pOtherRec = (PACLRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD, MPFROMP(pOtherRec),
                                        MPFROM2SHORT(CMA_PREV,CMA_ITEMORDER) );

  stRecIns.cb = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder = _IS_REC_VALID( pOtherRec ) ?
                            (PRECORDCORE)pOtherRec : (PRECORDCORE)CMA_FIRST;
  stRecIns.pRecordParent = NULL;
  stRecIns.zOrder = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert = 1;
  stRecIns.fInvalidateRecord = TRUE;
  WinSendMsg( hwndCtl, CM_INSERTRECORD, MPFROMP(pRecord), MPFROMP(&stRecIns) );

  WinSendMsg( hwndCtl, CM_SETRECORDEMPHASIS, MPFROMP(pRecord),
              MPFROM2SHORT(TRUE, CRA_CURSORED | CRA_SELECTED) );

  _aclOpenEdit( hwndCtl, pRecord, FIELDOFFSET( ACLRECORD, pszAddress ) );
}

static VOID _wmPageAccessCmdACLRemove(HWND hwnd)
{
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PACLRECORD           pRecord;

  pRecord = (PACLRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                                    MPFROMLONG(CMA_FIRST),
                                    MPFROMSHORT(CRA_CURSORED) );
  if ( !_IS_REC_VALID( pRecord ) )
    return;

  WinSendMsg( hwndCtl, CM_CLOSEEDIT, 0, 0 );
  WinSendMsg( hwndCtl, CM_REMOVERECORD, MPFROMP(&pRecord), 
              MPFROM2SHORT(1,CMA_FREE | CMA_INVALIDATE) );
}

static VOID _wmPageAccessCmdACLMove(HWND hwnd, BOOL fDown)
{
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PACLRECORD           pRecord, pOtherRec, pSelRec;
  RECORDINSERT         stRecIns;

  pRecord = (PACLRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                                    MPFROMLONG(CMA_FIRST),
                                    MPFROMSHORT(CRA_CURSORED) );
  if ( !_IS_REC_VALID( pRecord ) )
    return;

  pOtherRec = (PACLRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD, MPFROMP(pRecord),
                                fDown ? MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER) :
                                        MPFROM2SHORT(CMA_PREV,CMA_ITEMORDER) );
  if ( !_IS_REC_VALID( pOtherRec ) )
    return;

  pSelRec = pRecord;
  if ( fDown )
  {
    pRecord = pOtherRec;
    pOtherRec = pSelRec;
  }

  WinSendMsg( hwndCtl, CM_CLOSEEDIT, 0, 0 );
  WinSendMsg( hwndCtl, CM_REMOVERECORD, MPFROMP(&pOtherRec), MPFROM2SHORT(1,0) );

  stRecIns.cb = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder = (PRECORDCORE)pRecord;
  stRecIns.pRecordParent = NULL;
  stRecIns.zOrder = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert = 1;
  stRecIns.fInvalidateRecord = TRUE;
  WinSendMsg( hwndCtl, CM_INSERTRECORD, MPFROMP(pOtherRec), MPFROMP(&stRecIns) );

  _aclMakeVisible( hwndCtl, pSelRec );

  if ( fDown )
    WinSendMsg( hwndCtl, CM_SETRECORDEMPHASIS, MPFROMP(pSelRec),
                MPFROM2SHORT(TRUE, CRA_CURSORED | CRA_SELECTED) );
  else
  {
    NOTIFYRECORDEMPHASIS  stRecEmph;

    stRecEmph.hwndCnr = hwndCtl;
    WinSendMsg( hwnd, WM_CONTROL, MPFROM2SHORT(IDCN_ACL,CN_EMPHASIS),
                MPFROMP(&stRecEmph) );
  }
}

static BOOL _wmPageAccessMenuCommand(HWND hwnd, USHORT usCmd)
{
  HWND        hwndCtl = WinWindowFromID( hwnd, IDCN_ACL );
  PCNACLDATA  pCnACLData = (PCNACLDATA)WinQueryWindowPtr( hwndCtl, QWL_USER );
  PACLRECORD  pRecord = (PACLRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                                        MPFROMLONG(CMA_FIRST),
                                        MPFROMSHORT(CRA_CURSORED) );

  if ( ( pCnACLData == NULL ) || !_IS_REC_VALID( pRecord ) )
    return FALSE;

  switch( usCmd )
  {
    case IDM_ENABLE:
      pRecord->stACLItem.fEnable = TRUE;
      break;

    case IDM_DISABLE:
      pRecord->stACLItem.fEnable = FALSE;
      break;

    case IDM_ALLOW:
      pRecord->stACLItem.fAllow = TRUE;
      break;

    case IDM_DENY:
      pRecord->stACLItem.fAllow = FALSE;
      break;

    case IDM_COMMENT:
      _aclOpenEdit( hwndCtl, pRecord, FIELDOFFSET( ACLRECORD, pszComment ) );
      return TRUE;

    default:
      return FALSE;
  }

  _aclRecordACLItemChanged( pRecord, pCnACLData );
  WinSendMsg( hwndCtl, CM_INVALIDATERECORD, MPFROMP(&pRecord),
              MPFROM2SHORT(1,CMA_NOREPOSITION ) );
  return TRUE;
}

static MRESULT EXPENTRY _dlgPageAccessProc(HWND hwnd, ULONG msg,
                                           MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      _wmPageAccessInitDlg( hwnd );
      // fall through to WM_READ_CONFIG...

    case WM_READ_CONFIG:
      _wmPageAccessReadConfig( hwnd, (PCONFIG)mp2 );
      return (MRESULT)TRUE;

    case WM_DESTROY:
      _wmPageAccessDestroy( hwnd );
      break;

    case WM_STORE_CONFIG:
      _wmPageAccessStoreConfig( hwnd, (PCONFIG)mp1 );
      break;

    case WM_CONTROL:
      if ( SHORT1FROMMP(mp1) == IDCN_ACL )
      {
        switch( SHORT2FROMMP(mp1) )
        {
          case CN_REALLOCPSZ:
          {
            PCNREDITDATA   pCnrEditData = PVOIDFROMMP(mp2);
            PACLRECORD     pRecord = (PACLRECORD)pCnrEditData->pRecord;

            if ( pCnrEditData->ppszText == &pRecord->pszAccess )
              // Avoid storing text for the field "access".
              return (MRESULT)0;

            if ( pCnrEditData->ppszText == &pRecord->pszAddress )
            {
              if ( pCnrEditData->cbText > _ACL_ADDR_BUF_LEN )
                pCnrEditData->cbText = _ACL_ADDR_BUF_LEN;
            }
            else
            {
              if ( pCnrEditData->cbText > sizeof(pRecord->stACLItem.acComment) )
                pCnrEditData->cbText = sizeof(pRecord->stACLItem.acComment);
            }

            return (MRESULT)TRUE;
          }

          case CN_BEGINEDIT:
          {
            PCNREDITDATA pCnrEditData = PVOIDFROMMP(mp2);
            PACLRECORD   pRecord = (PACLRECORD)pCnrEditData->pRecord;
            PCNACLDATA   pCnACLData = (PCNACLDATA)WinQueryWindowPtr( pCnrEditData->hwndCnr, QWL_USER );

            if ( pCnrEditData->ppszText == &pRecord->pszAccess )
            {
              // Toggle access for the record. Avoid editing.
              pRecord->stACLItem.fAllow = !pRecord->stACLItem.fAllow;
              _aclRecordACLItemChanged( pRecord, pCnACLData );
              WinPostMsg( pCnrEditData->hwndCnr, CM_CLOSEEDIT, 0, 0 );
              return (MRESULT)0;
            }
            break;
          }

          case CN_ENDEDIT:
          {
            PCNREDITDATA   pCnrEditData = PVOIDFROMMP(mp2);
            PACLRECORD     pRecord = (PACLRECORD)pCnrEditData->pRecord;
            struct in_addr stInAddr1, stInAddr2;

            if ( ( pCnrEditData->cbText != 0 ) &&
                 ( pCnrEditData->ppszText == &pRecord->pszAddress ) )
            {
              if ( utilStrToInAddrRange( pCnrEditData->cbText - 1,
                                         *pCnrEditData->ppszText,
                                         &stInAddr1, &stInAddr2 ) )
              {
                pRecord->stACLItem.stInAddr1 = stInAddr1;
                pRecord->stACLItem.stInAddr2 = stInAddr2;
              }

              utilInAddrRangeToStr( &pRecord->stACLItem.stInAddr1,
                                    &pRecord->stACLItem.stInAddr2,
                                    _ACL_ADDR_BUF_LEN,
                                    *pCnrEditData->ppszText );
            }

            return (MRESULT)0;
          }

          case CN_EMPHASIS:
          {
            PNOTIFYRECORDEMPHASIS   pRecEmph = PVOIDFROMMP(mp2);
            PACLRECORD              pRecord, pOtherRec;

            pRecord = (PACLRECORD)WinSendMsg( pRecEmph->hwndCnr,
                                              CM_QUERYRECORDEMPHASIS,
                                              MPFROMLONG(CMA_FIRST),
                                              MPFROMSHORT(CRA_CURSORED) );
            WinEnableControl( hwnd, IDPB_ACL_REMOVE, _IS_REC_VALID(pRecord) );

            pOtherRec = (PACLRECORD)
              WinSendMsg( pRecEmph->hwndCnr, CM_QUERYRECORD, MPFROMP(pRecord),
                          MPFROM2SHORT(CMA_PREV,CMA_ITEMORDER) );

            WinEnableControl( hwnd, IDPB_ACL_UP,
                          _IS_REC_VALID(pRecord) && _IS_REC_VALID(pOtherRec) );

            pOtherRec = (PACLRECORD)
              WinSendMsg( pRecEmph->hwndCnr, CM_QUERYRECORD, MPFROMP(pRecord),
                          MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER) );

            WinEnableControl( hwnd, IDPB_ACL_DOWN,
                          _IS_REC_VALID(pRecord) && _IS_REC_VALID(pOtherRec) );
            return (MRESULT)0;
          }

          case CN_ENTER:
            _aclOpenEdit( ((PNOTIFYRECORDENTER)mp2)->hwndCnr,
                          (PACLRECORD)((PNOTIFYRECORDENTER)mp2)->pRecord,
                          FIELDOFFSET( ACLRECORD, pszAddress ) );
            return (MRESULT)0;

          case CN_CONTEXTMENU:
            _aclContextMenu( hwnd, (PACLRECORD)PVOIDFROMMP(mp2) );
            return (MRESULT)0;
        }
      }
      break;

    case WM_COMMAND:
      if ( ( SHORT1FROMMP(mp2) != CMDSRC_MENU ) ||
           !_wmPageAccessMenuCommand( hwnd, SHORT1FROMMP( mp1 ) ) )
      {
        switch( SHORT1FROMMP( mp1 ) )
        {
          case IDPB_ACL_NEW:
            _wmPageAccessCmdACLNew( hwnd );
            return (MRESULT)TRUE;

          case IDPB_ACL_REMOVE:
            _wmPageAccessCmdACLRemove( hwnd );
            return (MRESULT)TRUE;

          case IDPB_ACL_UP:
            _wmPageAccessCmdACLMove( hwnd, FALSE );
            return (MRESULT)TRUE;

          case IDPB_ACL_DOWN:
            _wmPageAccessCmdACLMove( hwnd, TRUE );
            return (MRESULT)TRUE;
        }
      }
      break;
  }

  return _commonPageProc( hwnd, msg, mp1, mp2 );
}


// Page 3: Options
// ---------------

// Sends to the owner (GUI window) visible state, i.e. command CMD_GUI_VISIBLE
// when "Enable system tray/floating icon" checkbox is checked or
// CMD_GUI_HIDDEN otherwise .
static VOID _pageOptionsSendVisibleStatusToOwner(HWND hwnd)
{
  HWND           hwndOwner = WinQueryWindow( hwnd, QW_OWNER );

  hwndOwner = WinQueryWindow( hwndOwner, QW_PARENT );
  hwndOwner = WinQueryWindow( hwndOwner, QW_OWNER );

  WinSendMsg( hwndOwner, WM_COMMAND, 
              WinQueryButtonCheckstate( hwnd, IDCB_GUIVISIBLE )
                ? MPFROMSHORT(CMD_GUI_VISIBLE)
                : MPFROMSHORT(CMD_GUI_HIDDEN),
              0 );
}

static VOID _wmPageOptionsInitDlg(HWND hwnd)
{
}

static VOID _wmPageOptionsReadConfig(HWND hwnd, PCONFIG pConfig)
{
  WinSendDlgItemMsg( hwnd, IDSB_DEFUPDTIME, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pConfig->ulDeferUpdateTime ), 0 );
  WinSendDlgItemMsg( hwnd, IDSB_DEFPTRUPDTIME, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pConfig->ulDeferPtrUpdateTime ), 0 );
  WinSendDlgItemMsg( hwnd, IDSB_SLICEHEIGHT, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pConfig->ulProgressiveSliceHeight ), 0 );

  WinCheckButton( hwnd, IDCB_FILETRANSFER, pConfig->fFileTransfer ? 1 : 0 );
  WinCheckButton( hwnd, IDCB_ULTRAVNC, pConfig->fUltraVNCSupport ? 1 : 0 );
  WinCheckButton( hwnd, IDCB_HTTPPROXY, pConfig->fHTTPProxyConnect ? 1 : 0 );

  WinCheckButton( hwnd, IDCB_GUIVISIBLE, pConfig->fGUIVisible ? 1 : 0 );

  _pageOptionsSendVisibleStatusToOwner( hwnd );
}

static VOID _wmPageOptionsStoreConfig(HWND hwnd, PCONFIG pConfig)
{
  WinSendDlgItemMsg( hwnd, IDSB_DEFUPDTIME, SPBM_QUERYVALUE,
                     MPFROMP( &pConfig->ulDeferUpdateTime ),
                     MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );
  WinSendDlgItemMsg( hwnd, IDSB_DEFPTRUPDTIME, SPBM_QUERYVALUE,
                     MPFROMP( &pConfig->ulDeferPtrUpdateTime ),
                     MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );
  WinSendDlgItemMsg( hwnd, IDSB_SLICEHEIGHT, SPBM_QUERYVALUE,
                     MPFROMP( &pConfig->ulProgressiveSliceHeight ),
                     MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );

  pConfig->fFileTransfer =
                      WinQueryButtonCheckstate( hwnd, IDCB_FILETRANSFER ) == 1;
  pConfig->fUltraVNCSupport =
                      WinQueryButtonCheckstate( hwnd, IDCB_ULTRAVNC ) == 1;
  pConfig->fHTTPProxyConnect =
                      WinQueryButtonCheckstate( hwnd, IDCB_HTTPPROXY ) == 1;
  pConfig->fGUIVisible = WinQueryButtonCheckstate( hwnd, IDCB_GUIVISIBLE ) == 1;
}

static MRESULT EXPENTRY _dlgPageOptionsProc(HWND hwnd, ULONG msg,
                                            MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      _wmPageOptionsInitDlg( hwnd );

    case WM_READ_CONFIG:
      _wmPageOptionsReadConfig( hwnd, (PCONFIG)mp2 );
      return (MRESULT)TRUE;

    case WM_STORE_CONFIG:
      _wmPageOptionsStoreConfig( hwnd, (PCONFIG)mp1 );
      break;

    case WM_CONTROL:
      if ( SHORT1FROMMP(mp1) == IDCB_GUIVISIBLE &&
           SHORT2FROMMP(mp1) == BN_CLICKED )
        // "Enable system tray/floating icon" checkbox was changed.
        _pageOptionsSendVisibleStatusToOwner( hwnd );
      return (MRESULT)0;
  }

  return _commonPageProc( hwnd, msg, mp1, mp2 );
}


// Page 4: Keyboard
// ----------------

static VOID _wmPageKbdInitDlg(HWND hwnd)
{
  static BOOL          fDriverVNCKBDChecked = FALSE;
  static BOOL          fDriverVNCKBDAllowed = FALSE;
  HFILE                hDriver;
  ULONG                ulRC, ulAction;

  if ( !fDriverVNCKBDChecked )
  {
    // Check driver VNCKBD$ only once.
    ulRC = DosOpen( "VNCKBD$", &hDriver, &ulAction, 0, 0, FILE_OPEN,
                    OPEN_SHARE_DENYREADWRITE | OPEN_ACCESS_READWRITE, NULL );
    if ( ulRC == NO_ERROR )
    {
      fDriverVNCKBDAllowed = TRUE;         // Driver loaded.
      DosClose( hDriver );
    }
    else
      fDriverVNCKBDAllowed = FALSE;        // Driver is missing.

    fDriverVNCKBDChecked = TRUE;           // Do not check any more.
  }

  WinEnableWindow( WinWindowFromID( hwnd, IDCB_DRIVER_VNCKBD ),
                   fDriverVNCKBDAllowed );
}

static VOID _wmPageKbdReadConfig(HWND hwnd, PCONFIG pConfig)
{
  WinCheckButton( hwnd, IDCB_DRIVER_VNCKBD, pConfig->fUseDriverVNCKBD ? 1 : 0 );
  WinCheckButton( hwnd, IDCB_DRIVER_KBD, pConfig->fUseDriverKBD ? 1 : 0 );
}

static VOID _wmPageKbdStoreConfig(HWND hwnd, PCONFIG pConfig)
{
  pConfig->fUseDriverVNCKBD =
                     WinQueryButtonCheckstate( hwnd, IDCB_DRIVER_VNCKBD ) == 1;
  pConfig->fUseDriverKBD =
                     WinQueryButtonCheckstate( hwnd, IDCB_DRIVER_KBD ) == 1;
}

static MRESULT EXPENTRY _dlgPageKbdProc(HWND hwnd, ULONG msg,
                                        MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      _wmPageKbdInitDlg( hwnd );

    case WM_READ_CONFIG:
      _wmPageKbdReadConfig( hwnd, (PCONFIG)mp2 );
      return (MRESULT)TRUE;

    case WM_STORE_CONFIG:
      _wmPageKbdStoreConfig( hwnd, (PCONFIG)mp1 );
      break;
  }

  return _commonPageProc( hwnd, msg, mp1, mp2 );
}


// Page 5: Logging
// ---------------

static VOID _wmPageLogInitDlg(HWND hwnd)
{
}

static VOID _wmPageLogReadConfig(HWND hwnd, PCONFIG pConfig)
{
  WinSetDlgItemText( hwnd, IDEF_LOG_FILE, pConfig->stLogData.acFile );
  WinSendDlgItemMsg( hwnd, IDSB_LOG_LEVEL, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pConfig->stLogData.ulLevel ), 0 );
  WinSendDlgItemMsg( hwnd, IDSB_LOG_SIZE, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pConfig->stLogData.ulMaxSize / 1024 ), 0 );
  WinSendDlgItemMsg( hwnd, IDSB_LOG_FILES, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pConfig->stLogData.ulFiles ), 0 );
}

static VOID _wmPageLogStoreConfig(HWND hwnd, PCONFIG pConfig)
{
  ULONG      ulVal;

  WinQueryDlgItemText( hwnd, IDEF_LOG_FILE, sizeof(pConfig->stLogData.acFile),
                       pConfig->stLogData.acFile );

  WinSendDlgItemMsg( hwnd, IDSB_LOG_LEVEL, SPBM_QUERYVALUE,
                     MPFROMP( &ulVal ), MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );
  pConfig->stLogData.ulLevel = ulVal > 3 ? 3 : ulVal;

  WinSendDlgItemMsg( hwnd, IDSB_LOG_SIZE, SPBM_QUERYVALUE,
                     MPFROMP( &ulVal ), MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );
  pConfig->stLogData.ulMaxSize = ulVal * 1024;

  WinSendDlgItemMsg( hwnd, IDSB_LOG_FILES, SPBM_QUERYVALUE,
                     MPFROMP( &ulVal ), MPFROM2SHORT( 0, SPBQ_DONOTUPDATE ) );
  pConfig->stLogData.ulFiles = ulVal > 10 ? 10 : ulVal;
}

static VOID _wmPageLogCmdFind(HWND hwnd)
{
  FILEDLG    stFileDlg = { 0 };
  HWND       hwndFileDlg;

  stFileDlg.cbSize = sizeof(FILEDLG);
  stFileDlg.fl     = FDS_SAVEAS_DIALOG | FDS_CENTER | FDS_ENABLEFILELB;

  WinQueryDlgItemText( hwnd, IDEF_LOG_FILE, sizeof(stFileDlg.szFullFile),
                       stFileDlg.szFullFile );

  hwndFileDlg = WinFileDlg( HWND_DESKTOP, hwnd, &stFileDlg );

  if ( ( hwndFileDlg != NULLHANDLE ) && ( stFileDlg.lReturn == DID_OK ) )
    WinSetDlgItemText( hwnd, IDEF_LOG_FILE, stFileDlg.szFullFile );
}

static VOID _wmPageLogCmdFolder(HWND hwnd)
{
#ifndef OPEN_DEFAULT
#define OPEN_DEFAULT   0
#endif
  CHAR       acPath[CCHMAXPATH];
  PCHAR      pcSlash;
  HOBJECT    hObject;

  utilPathOS2Slashes(
    WinQueryDlgItemText( hwnd, IDEF_LOG_FILE, sizeof(acPath), acPath ),
    acPath );

  pcSlash = strrchr( acPath, '\\' );
  if ( pcSlash != NULL )
    *pcSlash = '\0';
  else if ( acPath[1] == ':' )
    return;
  else
  {
    ULONG    cbPath = utilQueryProgPath( sizeof(acPath), acPath );

    if ( cbPath == 0 )
      return;
    acPath[cbPath - 1] = '\0';   // Remove trailing slash.
  }

  hObject = WinQueryObject( acPath );

  if ( ( hObject != NULLHANDLE ) &&
       WinOpenObject( hObject, OPEN_DEFAULT, 1 ) )
    WinOpenObject( hObject, OPEN_DEFAULT, 1 );
}

static MRESULT EXPENTRY _dlgPageLogProc(HWND hwnd, ULONG msg,
                                            MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      _wmPageLogInitDlg( hwnd );

    case WM_READ_CONFIG:
      _wmPageLogReadConfig( hwnd, (PCONFIG)mp2 );
      return (MRESULT)TRUE;

    case WM_STORE_CONFIG:
      _wmPageLogStoreConfig( hwnd, (PCONFIG)mp1 );
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDPN_LOG_FIND:
          _wmPageLogCmdFind( hwnd );
          return (MRESULT)TRUE;

        case IDPB_LOG_FOLDER:
          _wmPageLogCmdFolder( hwnd );
          return (MRESULT)TRUE;
      }
      break;
  }

  return _commonPageProc( hwnd, msg, mp1, mp2 );
}


// Page 6: Programs
// ----------------

static VOID _wmPageProgInitDlg(HWND hwnd)
{
  IPT        iptOffset = NULL;
  HWND       hwndCtl = WinWindowFromID( hwnd, IDMLE_PROGRAMS );
  ULONG      cbText;
  PCHAR      pcText;

  // MLE: set format for buffer importing.
  WinSendMsg( hwndCtl, MLM_FORMAT, MPFROMLONG( MLFIE_NOTRANS ), 0 );

  if ( ( DosQueryResourceSize( NULLHANDLE, 300/*RT_RCDATA*/, IDDATA_PROGRAMS_PAGE_HELP,
                               &cbText ) == NO_ERROR ) &&
       ( DosGetResource( NULLHANDLE, 300/*RT_RCDATA*/, IDDATA_PROGRAMS_PAGE_HELP,
                         (PVOID *)&pcText ) == NO_ERROR ) )
  {
    // MLE: set import buffer. 
    WinSendMsg( hwndCtl, MLM_SETIMPORTEXPORT, MPFROMP(pcText), MPFROMLONG(cbText) );
    // MLE: Insert text.
    WinSendMsg( hwndCtl, MLM_IMPORT, MPFROMP(&iptOffset), MPFROMLONG(cbText) );
    WinSendMsg( hwndCtl, MLM_SETIMPORTEXPORT, 0, 0 );

    DosFreeResource( (PVOID)pcText );
  }
}

static VOID _wmPageProgReadConfig(HWND hwnd, PCONFIG pConfig)
{
  WinCheckButton( hwnd, IDCB_ONLOGON, pConfig->fProgOnLogon ? 1 : 0 );
  WinSetDlgItemText( hwnd, IDEF_ONLOGON, pConfig->acProgOnLogon );
  WinCheckButton( hwnd, IDCB_ONGONE, pConfig->fProgOnGone ? 1 : 0 );
  WinSetDlgItemText( hwnd, IDEF_ONGONE, pConfig->acProgOnGone );
  WinCheckButton( hwnd, IDCB_ONCAD, pConfig->fProgOnCAD ? 1 : 0 );
  WinSetDlgItemText( hwnd, IDEF_ONCAD, pConfig->acProgOnCAD );
}

static VOID _wmPageProgStoreConfig(HWND hwnd, PCONFIG pConfig)
{
  pConfig->fProgOnLogon =
                       WinQueryButtonCheckstate( hwnd, IDCB_ONLOGON ) != 0;
  WinQueryDlgItemText( hwnd, IDEF_ONLOGON, sizeof(pConfig->acProgOnLogon),
                       pConfig->acProgOnLogon );
  pConfig->fProgOnGone =
                       WinQueryButtonCheckstate( hwnd, IDCB_ONGONE ) != 0;
  WinQueryDlgItemText( hwnd, IDEF_ONGONE, sizeof(pConfig->acProgOnGone),
                       pConfig->acProgOnGone );
  pConfig->fProgOnCAD =
                       WinQueryButtonCheckstate( hwnd, IDCB_ONCAD ) != 0;
  WinQueryDlgItemText( hwnd, IDEF_ONCAD, sizeof(pConfig->acProgOnCAD),
                       pConfig->acProgOnCAD );
}

static MRESULT EXPENTRY _dlgPageProgProc(HWND hwnd, ULONG msg,
                                            MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      _wmPageProgInitDlg( hwnd );

    case WM_READ_CONFIG:
      _wmPageProgReadConfig( hwnd, (PCONFIG)mp2 );
      return (MRESULT)TRUE;

    case WM_STORE_CONFIG:
      _wmPageProgStoreConfig( hwnd, (PCONFIG)mp1 );
      break;
  }

  return _commonPageProc( hwnd, msg, mp1, mp2 );
}


// Dialog window
// -------------

static BOOL _wmInitDlg(HWND hwnd)
{
  CHAR                 acUndo[32];
  CHAR                 acDefault[32];
  NOTEBOOKBUTTON       aButtons[2] =
   { { acUndo,    IDPB_UNDO,    NULLHANDLE, WS_VISIBLE | WS_TABSTOP | WS_GROUP },
     { acDefault, IDPB_DEFAULT, NULLHANDLE, WS_VISIBLE } };
  struct { ULONG ulDlgId; PFNWP pfnDlgProc; ULONG ulTitleStrId; }
                       aPages[6] =
   { { IDDLG_PAGE_GENERAL,  _dlgPageGeneralProc, IDS_GENERAL },
     { IDDLG_PAGE_ACCESS,   _dlgPageAccessProc,  IDS_ACCESS  },
     { IDDLG_PAGE_OPTIONS,  _dlgPageOptionsProc, IDS_OPTIONS },
     { IDDLG_PAGE_KBD,      _dlgPageKbdProc,     IDS_KEYBOARD },
     { IDDLG_PAGE_LOG,      _dlgPageLogProc,     IDS_LOGGING },
     { IDDLG_PAGE_PROGRAMS, _dlgPageProgProc,    IDS_PROGRAMS } };
  HAB                  hab = WinQueryAnchorBlock( hwnd );
  HWND                 hwndPage, hwndNB = WinWindowFromID( hwnd, IDNB_CONFIG );
  LONG                 lPageId;
  CHAR                 acBuf[64];
  ULONG                ulIdx;
  PCONFIG              pConfig;

  pConfig = cfgGet();
  WinSetWindowPtr( hwnd, QWL_USER, pConfig );

  // Create buttons in the common area of the notebook page.
  WinLoadString( hab, 0, IDS_UNDO, sizeof(acUndo), acUndo );
  WinLoadString( hab, 0, IDS_DEFAULT, sizeof(acDefault), acDefault );
  WinSendDlgItemMsg( hwnd, IDNB_CONFIG, BKM_SETNOTEBOOKBUTTONS,
                     MPFROMLONG( 2 ), MPFROMP(aButtons) );

  // Insert pages.

  for( ulIdx = 0; ulIdx < 6; ulIdx++ )
  {
    hwndPage = WinLoadDlg( hwndNB, hwndNB, aPages[ulIdx].pfnDlgProc, NULLHANDLE,
                           aPages[ulIdx].ulDlgId, pConfig );
    if ( hwndPage == NULLHANDLE )
    {
      debug( "WinLoadDlg(,,,,%u,) failed", aPages[ulIdx].ulDlgId );
      continue;
    }
    WinSetWindowPtr( hwndPage, QWL_USER, pConfig );

    lPageId = (LONG)WinSendMsg( hwndNB, BKM_INSERTPAGE, NULL,
                      MPFROM2SHORT( BKA_AUTOPAGESIZE | BKA_MAJOR, BKA_LAST ) );
    WinLoadString( hab, 0, aPages[ulIdx].ulTitleStrId, sizeof(acBuf), acBuf );
    WinSendMsg( hwndNB, BKM_SETTABTEXT, MPFROMLONG( lPageId ), MPFROMP(acBuf) );
    WinSendMsg( hwndNB, BKM_SETPAGEWINDOWHWND, MPFROMLONG( lPageId ),
                MPFROMLONG( hwndPage ) );
    WinSetOwner( hwndPage, hwndNB );
  }

  // Show dialog window.
  WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ACTIVATE | SWP_SHOW );

  return TRUE;
}

static VOID _wmDestory(HWND hwnd)
{
  PCONFIG    pConfig = (PCONFIG)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND       hwndPage, hwndNB = WinWindowFromID( hwnd, IDNB_CONFIG );
  ULONG      ulPageId = 0;

  // Send message WM_STORE_CONFIG to each page.
  while( TRUE )
  {
    ulPageId = (ULONG)WinSendMsg( hwndNB, BKM_QUERYPAGEID, MPFROMLONG(ulPageId),
                            MPFROM2SHORT( ulPageId == 0 ? BKA_FIRST : BKA_NEXT,
                                          BKA_MAJOR ) );
    if ( ulPageId == 0 )
      break;

    hwndPage = (HWND)WinSendMsg( hwndNB, BKM_QUERYPAGEWINDOWHWND,
                                 MPFROMLONG(ulPageId), 0 );
    WinSendMsg( hwndPage, WM_STORE_CONFIG, MPFROMP(pConfig), 0 );
  }

  // Save configuration, apply changes and destroy configuration memory storage.

  // First step - writing configuration. "First" bacause ACL will be reset when
  // the configuration will be applied ( in rfbsSetServer() ).
  cfgStore( pConfig );

  // We use message to the hidden window in the main thread instead direct
  // call rfbsSetServer( pConfig ).
  //
  // WMSRV_RECONFIG will be processed in the hidden window from main thread
  // during WinPeekMsg(). So it will be in sync with the calls to libvncserver.
  // This message causes rfbsSetServer() function call. ACL will be reset there.
  // Remember, this dialog works in GUI thread (see gui.c).
  WinSendMsg( hwndSrv, WMSRV_RECONFIG, MPFROMP(pConfig), 0 );

  // Destroy the configuration structure.
  cfgFree( pConfig );
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

    case WM_CLOSE:
      // Send dialog close command to the main gui window.
      WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_COMMAND, 
                  MPFROMSHORT(CMD_CONFIG_CLOSE),
                  MPFROM2SHORT(CMDSRC_OTHER,FALSE) );
      break;

    case WM_CONTROL:
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}

HWND cfgdlgCreate(HAB hab, HWND hwndOwner)
{
  HWND       hwndDlg;

  hwndDlg = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, NULLHANDLE,
                        IDDLG_CONFIG, NULL );
  if ( hwndDlg == NULLHANDLE )
  {
    debug( "WinLoadDlg(,,,,IDDLG_CONFIG,) failed" );
  }

  return hwndDlg;
}
