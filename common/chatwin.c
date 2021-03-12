#define INCL_WIN
#define INCL_WINDIALOGS
#define INCL_DOSERRORS
#define INCL_DOSMODULEMGR
#include <os2.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "chatwin.h"
#include "debug.h"

typedef struct _DLGINITDATA {
  ULONG                ulSize;
  ULONG                ulWinMsg;
  PVOID                pUser;
} DLGINITDATA, *PDLGINITDATA;

typedef struct _DLGDATA {
  ULONG                ulWinMsg;
  PVOID                pUser;
  PSZ                  pszLocalUserName; 

  LONG                 lHistoryWDiff;
  LONG                 lHistoryHDiff;
  LONG                 lMsgWDiff;
  LONG                 lMsgHeight;
  LONG                 lSendRightOffs;
  LONG                 lSendBottom;
  PCHAR                pcMLEImpBuf;
  IPT                  iptOffset;
} DLGDATA, *PDLGDATA;


static BOOL _wmInitDlg(HWND hwnd, PDLGINITDATA pInitData)
{
  ULONG      ulRC;
  HWND       hwndCtl = WinWindowFromID( hwnd, IDMLE_HISTORY );
  PDLGDATA   pData = calloc( 1, sizeof(DLGDATA) );

  if ( pData == NULL )
    return FALSE;

  pData->ulWinMsg = pInitData->ulWinMsg;
  pData->pUser    = pInitData->pUser;

  // Allocate buffer for chat messages.
  ulRC = DosAllocMem( (PVOID)&pData->pcMLEImpBuf, 4096, PAG_COMMIT+PAG_WRITE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosAllocMem(), rc = %u", ulRC );
    free( pData );
    return FALSE;
  }
  pData->pcMLEImpBuf[0] = '\0';

  WinSetWindowULong( hwnd, QWL_USER, (ULONG)pData );

  // MLE: set format for buffer importing.
  WinSendMsg( hwndCtl, MLM_FORMAT, MPFROMLONG( MLFIE_NOTRANS ), 0 );
  // MLE: set import buffer. 
  WinSendMsg( hwndCtl, MLM_SETIMPORTEXPORT, MPFROMP( pData->pcMLEImpBuf ),
              MPFROMLONG( 4096 ) );

  WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ACTIVATE );
  WinSetFocus( HWND_DESKTOP, WinWindowFromID( hwnd, IDEF_MESSAGE ) );

  return TRUE;
}

static VOID _wmDestory(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( pData != NULL )
  {
    if ( pData->pcMLEImpBuf != NULL )
      DosFreeMem( pData->pcMLEImpBuf );

    if ( pData->pszLocalUserName != NULL )
      free( pData->pszLocalUserName );

    free( pData );
  }

  debugDone();
}

static VOID _wmWindowPosChanged(HWND hwnd, PSWP pswp, ULONG flAwp)
{
  RECTL      rectlDlg;
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( ( pswp->fl & SWP_SHOW ) != 0 )
  {
    HWND     hwndCtl = WinWindowFromID( hwnd, MBID_OK );
    RECTL    rectlCtl;

    WinQueryWindowRect( hwnd, &rectlDlg );

    WinQueryWindowRect( WinWindowFromID( hwnd, IDMLE_HISTORY ), &rectlCtl );
    pData->lHistoryWDiff = rectlDlg.xRight - rectlCtl.xRight;
    pData->lHistoryHDiff = rectlDlg.yTop - rectlCtl.yTop;

    WinQueryWindowRect( WinWindowFromID( hwnd, IDEF_MESSAGE ), &rectlCtl );
    pData->lMsgWDiff = rectlDlg.xRight - rectlCtl.xRight;
    pData->lMsgHeight = rectlCtl.yTop;

    if ( hwndCtl == NULLHANDLE )
      debug( "Control id #%u not found", MBID_OK );
    else
    {
      WinQueryWindowRect( hwndCtl, &rectlCtl );
      WinMapWindowPoints( hwndCtl, hwnd, (PPOINTL)&rectlCtl, 1 );
    }
    pData->lSendRightOffs = rectlDlg.xRight - rectlCtl.xLeft;
    pData->lSendBottom = rectlCtl.yBottom;

    return;
  }

  if ( ( pswp->fl & (SWP_SIZE | SWP_NOREDRAW | SWP_MINIMIZE | SWP_RESTORE) )
         == SWP_SIZE &&
       ( pData != NULL ) )
  {
    WinQueryWindowRect( hwnd, &rectlDlg );

    WinSetWindowPos( WinWindowFromID( hwnd, IDMLE_HISTORY ), HWND_TOP, 0, 0,
                     rectlDlg.xRight - pData->lHistoryWDiff,
                     rectlDlg.yTop - pData->lHistoryHDiff,
                     SWP_SIZE );
    WinSetWindowPos( WinWindowFromID( hwnd, IDEF_MESSAGE ), HWND_TOP, 0, 0,
                     rectlDlg.xRight - pData->lMsgWDiff,
                     pData->lMsgHeight, SWP_SIZE );
    WinSetWindowPos( WinWindowFromID( hwnd, MBID_OK ), HWND_TOP,
                     rectlDlg.xRight - pData->lSendRightOffs,
                     pData->lSendBottom, 0, 0, SWP_MOVE );
  }
}

static VOID _sendBtn(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  CHAR       acText[1024];
  HWND       hwndCtl = WinWindowFromID( hwnd, IDEF_MESSAGE );
  ULONG      cbText = WinQueryWindowText( hwndCtl, sizeof(acText) - 1, acText );
  CWMSGDATA  stCWMsgData;
  WMCHATMSG  stWMChatMsg;

  if ( cbText == 0 )
    return;

  *((PUSHORT)&acText[cbText]) = 0x000A;
  cbText++;

  WinSetWindowText( hwndCtl, "" );
  WinSetFocus( HWND_DESKTOP, hwndCtl );

  // Message for owner.
  stCWMsgData.cbText   = cbText;
  stCWMsgData.pszText  = acText;
  stCWMsgData.pUser    = pData->pUser;
  stCWMsgData.hwndChat = hwnd;
  if ( (BOOL)WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), pData->ulWinMsg,
                         MPFROMP(CWM_MESSAGE), MPFROMP(&stCWMsgData) ) )
  {
    // Insert message into MLE.
    stWMChatMsg.pszUser = pData->pszLocalUserName;
    stWMChatMsg.cbText  = cbText;
    stWMChatMsg.pcText  = acText;
    WinSendMsg( hwnd, WMCHAT_MESSAGE, MPFROMLONG(CWMT_MSG_LOCAL),
                MPFROMP(&stWMChatMsg) );
  }
}

static VOID _wmChatMessage(HWND hwnd, ULONG ulType, PVOID pWMChatData)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  ULONG      cbText;
  PCHAR      pcText;
  LONG       cbStored;

  if ( ulType == CWMT_SYSTEM )
  {
    WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ),
                     ((PWMCHATSYS)pWMChatData)->fAllow );

    cbStored = 0;
    cbText = ((PWMCHATSYS)pWMChatData)->cbText;
    pcText = ((PWMCHATSYS)pWMChatData)->pcText;
    if ( cbText == 0 )
      return;
  }
  else if ( ulType == CWMT_LOCAL_NAME )
  {
    if ( pData->pszLocalUserName != NULL )
      free( pData->pszLocalUserName );

    pData->pszLocalUserName = pWMChatData == NULL
                                ? NULL : strdup( (PSZ)pWMChatData );
    return;
  }
  else       // usType is CWMT_MSG_REMOTE or CWMT_MSG_LOCAL
  {
    cbStored = sprintf( pData->pcMLEImpBuf, "<%s> ",
                        ((PWMCHATMSG)pWMChatData)->pszUser == NULL
                          ? "" : ((PWMCHATMSG)pWMChatData)->pszUser );

    cbText = ((PWMCHATMSG)pWMChatData)->cbText;
    pcText = ((PWMCHATMSG)pWMChatData)->pcText;
  }

  while( (cbText > 0) && ( isspace( pcText[cbText - 1] ) ) )
    cbText--;

  if ( cbText > (4096 - cbStored - 2) )
    cbText = 4096 - cbStored - 2;

  memcpy( &pData->pcMLEImpBuf[cbStored], pcText, cbText );
  cbText += cbStored;

  pData->pcMLEImpBuf[cbText] = '\n';
  cbText++;

  WinSendDlgItemMsg( hwnd, IDMLE_HISTORY, MLM_IMPORT,
                     MPFROMP( &pData->iptOffset ), MPFROMLONG(cbText) );
  // Scroll text.
  WinSendDlgItemMsg( hwnd, IDMLE_HISTORY, MLM_SETSEL,
                     MPFROMLONG( 255 ), MPFROMLONG( 255 ) );
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

    case WM_CLOSE:
    {
      PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
      CWMSGDATA  stCWMsgData;

      stCWMsgData.cbText   = 0;
      stCWMsgData.pszText  = NULL;
      stCWMsgData.pUser    = pData->pUser;
      stCWMsgData.hwndChat = hwnd;
      WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), pData->ulWinMsg,
                  MPFROMLONG(CWM_CLOSE), MPFROMP(&stCWMsgData) );
      WinDestroyWindow( hwnd );
      break;
    }

    case WM_WINDOWPOSCHANGED:
      _wmWindowPosChanged( hwnd, (PSWP)mp1, LONGFROMMP(mp2) );
      break;

    case WM_COMMAND:
      if ( SHORT1FROMMP(mp1) == MBID_OK )
        _sendBtn( hwnd );
      return (MRESULT)0;

    case WMCHAT_MESSAGE:
      // The message has been received.
      _wmChatMessage( hwnd, LONGFROMMP(mp1), PVOIDFROMMP(mp2) );
      return (MRESULT)0;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


HWND EXPENTRY chatwinCreate(HWND hwndOwner, ULONG ulWinMsg, PVOID pUser)
{
  HWND                 hwndChat;
  DLGINITDATA          stInitData;
  HMODULE              hMod = NULLHANDLE;
//  ULONG                ulRC;

  debugInit();

/*
  ulRC = DosQueryModuleHandle( VNCPMDLLNAME, &hMod );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosQueryModuleHandle(), rc = %u", ulRC );
    return NULLHANDLE;
  }
*/

  stInitData.ulSize    = sizeof(DLGINITDATA);
  stInitData.ulWinMsg  = ulWinMsg;
  stInitData.pUser     = pUser;

  hwndChat = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, hMod,
                         IDDLG_CHAT, &stInitData );
  if ( hwndChat == NULLHANDLE )
    debug( "WinLoadDlg(,,,,IDDLG_CHAT,) failed (IDDLG_CHAT=%u)", IDDLG_CHAT );

  return hwndChat;
}
