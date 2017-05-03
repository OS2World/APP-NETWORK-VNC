/*
  Attach Listening Viewer dialog.
*/

#define INCL_WIN
#define INCL_WINDIALOGS
#define INCL_DOSERRORS
#define INCL_DOSPROCESS
#include <os2.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "resource.h"
#include <netinet/in.h>
#define UTIL_INET_ADDR
#include "utils.h"
#include "srvwin.h"
#include "config.h"
#include "gui.h"
#include <rfb/rfb.h>
#include "utils.h"
#include "avdlg.h"
#include <netdb.h>
#include <arpa\inet.h>
#include <types.h>
#include <sys\socket.h>
#include <sys\ioctl.h>
#include <net\route.h>
#include <net\if.h>
#include <net\if_arp.h>
#include <nerrno.h>
#include <debug.h>

#ifdef DEBUG_CODE
// socket() was redefined in debug module but it will be closed in
// libvncserver by "original" function. We should use original socket() to
// avoid report unclosed sockets in the debug file.
#undef socket
#undef soclose
#endif

#define _SEMFL_FINISH            0x00000001
#define _SEMFL_HOST_NOT_FOUND    0x00000002
#define _MAX_HOSTS               16
#define _INISEC_ATTACH           "Attach"

typedef struct _DLGDATA {
  HWND       hwnd;
  CHAR       acHost[256];
  ULONG      ulPort;
  TID        tid;
  int        iSock;
} DLGDATA, *PDLGDATA;


extern HWND  hwndSrv;            // from main.c
extern HAB   habSrv;             // from main.c


static void threadConnect(void *pThreadData)
{
  PDLGDATA             pData = (PDLGDATA)pThreadData;
  struct hostent       *pHostEnt;
  struct sockaddr_in   sSockAddrIn;

  pData->iSock = -1;
  sSockAddrIn.sin_addr.s_addr = inet_addr( pData->acHost );
  if ( sSockAddrIn.sin_addr.s_addr == ((u_long)-1) )
  {
    pHostEnt = gethostbyname( pData->acHost );
    if ( pHostEnt == NULL )
    {
      WinPostMsg( pData->hwnd, WM_SEM1, MPFROMLONG(_SEMFL_HOST_NOT_FOUND), 0 );
      _endthread();
    }
    memcpy( &sSockAddrIn.sin_addr, pHostEnt->h_addr, sizeof(struct in_addr) );
  }

  sSockAddrIn.sin_family = AF_INET;
  sSockAddrIn.sin_port   = htons(pData->ulPort);

  pData->iSock = socket( AF_INET, SOCK_STREAM, 0 );

  if ( pData->iSock == -1 )
    debug( "socket() failed" );
  else if ( connect( pData->iSock, (struct sockaddr *)&sSockAddrIn,
                  sizeof(struct sockaddr_in) ) == -1 )
  {
    soclose( pData->iSock );
    pData->iSock = -1;
  }

  WinPostMsg( pData->hwnd, WM_SEM1, MPFROMLONG(_SEMFL_FINISH), 0 );
  _endthread();
}


static BOOL _queryHost(HWND hwnd, ULONG cbHost, PCHAR pcHost, PULONG pulPort)
{
  CHAR       acStr[256];
  ULONG      cbStr = WinQueryDlgItemText( hwnd, IDCB_ATTACH_HOST,
                                          sizeof(acStr), acStr );
  PCHAR      pcStr = acStr;
  PCHAR      pcColon;

  BUF_SKIP_SPACES( cbStr, pcStr );
  BUF_RTRIM( cbStr, pcStr );
  pcColon = memchr( pcStr, ':', cbStr );

  if ( pcColon != NULL )
  {
    pcColon++;
    if ( !utilStrToULong( cbStr - (pcColon - pcStr), pcColon, 1, 0xFFFF,
                          pulPort ) )
      return FALSE;
    cbStr = pcColon - pcStr - 1;
  }
  else if ( pulPort != NULL )
    *pulPort = 5500;

  if ( ( cbHost != 0 && cbStr >= cbHost ) ||
       !utilVerifyDomainName( cbStr, pcStr ) )
    return FALSE;

  if ( cbHost != 0 )
  {
    memcpy( pcHost, pcStr, cbStr );
    pcHost[cbStr] = '\0';
  }

  return TRUE;
}

static BOOL _wmInitDlg(HWND hwnd)
{
  PDLGDATA   pData = calloc( 1, sizeof(DLGDATA) );
  HINI       hIni;
  ULONG      cbList;
  PCHAR      pcList, pcHost;
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCB_ATTACH_HOST );

  if ( !pData )
    return FALSE;

  WinSetWindowULong( hwnd, QWL_USER, (ULONG)pData );
  pData->hwnd  = hwnd;
  pData->tid   = -1;
  pData->iSock = -1;

  // Read stored hosts list.

  hIni = iniOpen( habSrv );
  if ( hIni != NULLHANDLE )
  {
    PrfQueryProfileSize( hIni, _INISEC_ATTACH, "Hosts", &cbList );
    cbList++;
    pcList = malloc( cbList );

    if ( pcList != NULL )
    {
      if ( PrfQueryProfileData( hIni, _INISEC_ATTACH, "Hosts",
                                pcList, &cbList ) )
      {
        // Inserts hosts to the combobox.
        for( pcHost = pcList; *pcHost != '\0'; pcHost += strlen( pcHost ) + 1 )
          WinSendMsg( hwndCtl, LM_INSERTITEM, MPFROMSHORT(LIT_END), pcHost );

        // Select first host.
        WinSendMsg( hwndCtl, LM_SELECTITEM, MPFROMSHORT(0), MPFROMLONG(TRUE) );
      }

      free( pcList );
    }

    iniClose( hIni );
  }

  WinSetFocus( HWND_DESKTOP, hwndCtl );

  return TRUE;
}

static VOID _wmDestory(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  // Shutdown thread.

  if ( pData->tid != -1 )
  {
    // Thread runned, let it chance to normal exit.
    ULONG    ulIdx;

    if ( pData->iSock != -1 )
      so_cancel( pData->iSock );

    for( ulIdx = 0; ulIdx < 20; ulIdx++ )
    {
      if ( DosWaitThread( &pData->tid, DCWW_NOWAIT ) !=
             ERROR_THREAD_NOT_TERMINATED )
      {
        debug( "Thread ended" );
        pData->tid = -1;
        break;
      }
      DosSleep( 1 );
    }

    if ( pData->tid != -1 )
    {
      // Thread still runned, kill it.
      debug( "Kill thread..." );
      DosKillThread( pData->tid );
    }
  }

  if ( pData->iSock != -1 )
    soclose( pData->iSock );

  free( pData );
}

static VOID _attachBtn(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCB_ATTACH_HOST );
  ULONG      ulCount, ulIdx, cbList;
  PCHAR      pcList;

  // Store clients hosts list.

  cbList = WinQueryWindowTextLength( hwndCtl ) + 1;
  ulCount = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_QUERYITEMCOUNT, 0, 0 ) );
  if ( ulCount > (_MAX_HOSTS - 1) ) // -1 - one host from CB's text.
    ulCount = (_MAX_HOSTS - 1);

  for( ulIdx = 0; ulIdx < ulCount; ulIdx++ )
  {
    cbList += SHORT1FROMMR( WinSendMsg( hwndCtl, LM_QUERYITEMTEXTLENGTH,
                                        MPFROMSHORT( ulIdx ), 0 ) ) + 1;
  }

  pcList = malloc( cbList );
  if ( pcList == NULL )
    debugCP( "Not enough memory" );
  else
  {
    PCHAR    pcHost;
    CHAR     acBuf[256];
    HINI     hIni;

    // Query current selected in combobox (typed) host.
    if ( WinQueryWindowText( hwndCtl, 256, pcList ) == 0 )
      *pcList = '\0';
    utilStrTrim( pcList );
    pcHost = strchr( pcList, '\0' ) + 1;

    // Read list from combobox except host obtained above.
    for( ulIdx = 0; ulIdx < ulCount; ulIdx++ )
    {
      if ( WinSendMsg( hwndCtl, LM_QUERYITEMTEXT,
                       MPFROM2SHORT( ulIdx, sizeof(acBuf) ),
                       MPFROMP( acBuf ) ) == 0 )
        continue;

      utilStrTrim( acBuf );
      if ( stricmp( acBuf, pcList ) != 0 )
      {
        strcpy( pcHost, acBuf );
        pcHost = strchr( pcHost, '\0' ) + 1;
      }
    }
    *pcHost = '\0';

#if 0
    for( pcHost = pcList; *pcHost != '\0'; pcHost += strlen( pcHost ) + 1 )
      printf( "Host: %s\n", pcHost );
#endif

    hIni = iniOpen( habSrv );
    if ( hIni != NULLHANDLE )
    {
      PrfWriteProfileData( hIni, _INISEC_ATTACH, "Hosts", pcList,
                           (pcHost - pcList) + 1 );
      iniClose( hIni );
    }

    free( pcList );
  }

  // Begin the connection.

  if ( !_queryHost( hwnd, sizeof(pData->acHost), pData->acHost,
                    &pData->ulPort ) ||
       ( pData->tid != -1 ) )
    return;

  // Start thread to connect to the viewer.
  pData->tid = _beginthread( threadConnect, NULL, 65535, (PVOID)pData );
  if ( pData->tid == -1 )
  {
    debug( "_beginthread() failed" );
    return;
  }

  WinSetPointer( HWND_DESKTOP,
                 WinQuerySysPointer( HWND_DESKTOP, SPTR_WAIT, FALSE ) );
  WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ), FALSE );
  WinEnableWindow( WinWindowFromID( hwnd, IDCB_ATTACH_HOST ), FALSE );
}

static VOID _hostChanged(HWND hwnd)
{
  WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ),
                   _queryHost( hwnd, 0, NULL, NULL ) );
}

static VOID _wmSem1(HWND hwnd, ULONG ulFlags)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  pData->tid   = -1;

  if ( pData->iSock != -1 )
  {
    // Socket successfuly created and connected.

    // Send socket to RFB server.
    WinSendMsg( hwndSrv, WMSRV_ATTACH, MPFROMLONG(pData->iSock), 0 );

    pData->iSock = -1;
    // Send dialog close command to the main gui window.
    WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_COMMAND, 
                MPFROMSHORT(CMD_ATTACH_VIEWER_CLOSE),
                MPFROM2SHORT(CMDSRC_OTHER,FALSE) );
    return;
  }

  WinSetPointer( HWND_DESKTOP,
                 WinQuerySysPointer( HWND_DESKTOP, SPTR_ARROW, FALSE ) );
  WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ), TRUE );
  WinEnableWindow( WinWindowFromID( hwnd, IDCB_ATTACH_HOST ), TRUE );
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

    case WM_COMMAND:
      if ( SHORT1FROMMP(mp1) == MBID_OK )
        _attachBtn( hwnd );
      return (MRESULT)0;

    case WM_CLOSE:
      // Send dialog close command to the main gui window.
      WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_COMMAND, 
                  MPFROMSHORT(CMD_ATTACH_VIEWER_CLOSE),
                  MPFROM2SHORT(CMDSRC_OTHER,FALSE) );
      break;

    case WM_CONTROL:
      if ( ( SHORT1FROMMP(mp1) == IDCB_ATTACH_HOST ) &&
           ( SHORT2FROMMP(mp1) == CBN_EFCHANGE ) )
        _hostChanged( hwnd );
      return (MRESULT)0;

    case WM_CONTROLPOINTER:
      {
        PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

        if ( pData->tid != -1 )
          return (MRESULT)WinQuerySysPointer( HWND_DESKTOP, SPTR_WAIT, FALSE );
      }
      break;

    case WM_MOUSEMOVE:
      {
        PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

        if ( pData->tid != -1 )
        {
          WinSetPointer( HWND_DESKTOP,
                         WinQuerySysPointer( HWND_DESKTOP, SPTR_WAIT, FALSE ) );
          return (MRESULT)0;
        }
      }
      break;

    case WM_SEM1:
      _wmSem1( hwnd, LONGFROMMP(mp1) );
      return (MRESULT)0;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}

HWND avdlgCreate(HAB hab, HWND hwndOwner)
{
  HWND                 hwndDlg;

  hwndDlg = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, NULLHANDLE,
                        IDDLG_ATTACH_VIEWER, NULL );
  if ( hwndDlg == NULLHANDLE )
  {
    debug( "WinLoadDlg(,,,,IDDLG_ATTACH_VIEWER,) failed" );
  }

  return hwndDlg;
}
