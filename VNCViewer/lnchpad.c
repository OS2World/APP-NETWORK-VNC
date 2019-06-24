#define INCL_DOSERRORS
#define INCL_WIN
#define INCL_WINDIALOGS
#define INCL_DOSPROCESS
#include <os2.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#include <types.h>
#include <sys\socket.h>
#include <sys\ioctl.h>
#include <net\route.h>
#include <net\if.h>
#include <net\if_arp.h>
#include <arpa\inet.h>
#include <unistd.h>

#include <rfb/rfbclient.h>
#include <utils.h>
#include "resource.h"
#include "clntconn.h"
#include "optionsdlg.h"
#include "lnchpad.h"
#include "progress.h"
#include "tipwin.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <stdint.h>

#include <debug.h>

#define INI_FILE                 "vncviewer.ini"
#define INI_APP_LISTEN           ".Listen"
#define INI_APP_DEFAULT          ".Default"

extern HAB             hab;
extern ULONG           cOpenWin;

typedef struct _DLGINITDATA {
  USHORT             usSize;
  HINI               hIni;
} DLGINITDATA, *PDLGINITDATA;

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

static OPTDLGDATA      stDefOpt =
{
  5,         // ulAttempts
  16,        // ulBPP
  FALSE,     // fRememberPswd
  FALSE,     // fViewOnly
  TRUE,      // fShareDesktop
  "",        // acCharset
  "Tight TRLE ZRLE Ultra CopyRect Hextile Zlib CoRRE RRE RAW", // acEncodings
  5,         // ulCompressLevel
  6          // ulQualityLevel
};


static void _b64Encode(PCHAR pcData, ULONG cbData, PCHAR *ppcBuf, PULONG pcbBuf)
{
  BIO        *bioBuf, *bioB64f;
  BUF_MEM    *pMem;

  bioB64f = BIO_new( BIO_f_base64() );
  bioBuf  = BIO_new( BIO_s_mem() );
  bioBuf  = BIO_push( bioB64f, bioBuf );

  BIO_set_flags( bioBuf, BIO_FLAGS_BASE64_NO_NL );
  BIO_set_close( bioBuf, BIO_CLOSE );
  BIO_write( bioBuf, pcData, cbData );
  BIO_flush( bioBuf );

  BIO_get_mem_ptr( bioBuf, &pMem );
  *pcbBuf = pMem->length;
  *ppcBuf = malloc( *pcbBuf + 1 );
  memcpy( *ppcBuf, pMem->data, *pcbBuf );
  (*ppcBuf)[ *pcbBuf ] = '\0';

  BIO_free_all( bioBuf );
}

static void _b64Decode(PCHAR pcData, ULONG cbData, PCHAR *ppcBuf, PULONG pcbBuf)
{
  BIO        *bioBuf, *bioB64f;

  bioB64f = BIO_new( BIO_f_base64() );
  bioBuf = BIO_new_mem_buf( (void *)pcData, cbData );
  bioBuf = BIO_push( bioB64f, bioBuf );
  *ppcBuf = malloc( cbData );

  BIO_set_flags( bioBuf, BIO_FLAGS_BASE64_NO_NL );
  BIO_set_close( bioBuf, BIO_CLOSE );
  *pcbBuf = BIO_read( bioBuf, *ppcBuf, cbData );
  *ppcBuf = realloc( *ppcBuf, (*pcbBuf) + 1 );
  (*ppcBuf)[ *pcbBuf ] = '\0';

  BIO_free_all( bioBuf );
}


// Open ini-file.
static HINI _iniOpen()
{
  HINI       hIni;
  ULONG      ulRC;
  CHAR       acBuf[CCHMAXPATH];

  ulRC = utilQueryProgPath( sizeof(acBuf), acBuf );
  strcpy( &acBuf[ulRC], INI_FILE );

  hIni = PrfOpenProfile( hab, acBuf );
  if ( hIni == NULLHANDLE )
  {
    debug( "Cannot open INI-file: %s", acBuf );
    utilMessageBox( HWND_DESKTOP, APP_NAME, IDM_INI_OPEN_ERROR,
                    MB_ERROR | MB_MOVEABLE | MB_CANCEL | MB_SYSTEMMODAL );
    return NULLHANDLE;
  }

  return hIni;
}

static VOID _iniWriteOptions(HINI hIni, PSZ pszApp, POPTDLGDATA pData)
{
  utilINIWriteULong( hIni, pszApp, "Attempts",      pData->ulAttempts );
  utilINIWriteULong( hIni, pszApp, "BitsPerPixel",  pData->ulBPP );
  utilINIWriteULong( hIni, pszApp, "RememberPswd",  pData->fRememberPswd );
  utilINIWriteULong( hIni, pszApp, "ViewOnly",      pData->fViewOnly );
  utilINIWriteULong( hIni, pszApp, "ShareDesktop",  pData->fShareDesktop );
  PrfWriteProfileString( hIni, pszApp, "Charset",   pData->acCharset );
  PrfWriteProfileString( hIni, pszApp, "Encodings", pData->acEncodings );
  utilINIWriteULong( hIni, pszApp, "CompressLevel", pData->ulCompressLevel );
  utilINIWriteULong( hIni, pszApp, "QualityLevel",  pData->ulQualityLevel );
}

static VOID _iniQueryOptions(HINI hIni, PSZ pszApp, POPTDLGDATA pData)
{
  pData->ulAttempts      = utilINIQueryULong( hIni, pszApp, "Attempts",
                                              stDefOpt.ulAttempts );
  pData->ulBPP           = utilINIQueryULong( hIni, pszApp, "BitsPerPixel",
                                              stDefOpt.ulBPP );
  pData->fViewOnly       = utilINIQueryULong( hIni, pszApp, "ViewOnly",
                                              stDefOpt.fViewOnly );
  pData->fRememberPswd   = utilINIQueryULong( hIni, pszApp, "RememberPswd",
                                              stDefOpt.fRememberPswd );
  pData->fShareDesktop   = utilINIQueryULong( hIni, pszApp, "ShareDesktop",
                                              stDefOpt.fShareDesktop );
  PrfQueryProfileString( hIni, pszApp, "Charset", stDefOpt.acCharset,
                         pData->acCharset, sizeof(pData->acCharset) );
  PrfQueryProfileString( hIni, pszApp, "Encodings", stDefOpt.acEncodings,
                         pData->acEncodings, sizeof(pData->acEncodings) );
  pData->ulCompressLevel = utilINIQueryULong( hIni, pszApp, "CompressLevel",
                                              stDefOpt.ulCompressLevel );
  pData->ulQualityLevel  = utilINIQueryULong( hIni, pszApp, "QualityLevel",
                                              stDefOpt.ulQualityLevel );
}

static VOID _iniQueryHost(HINI hIni, PSZ pszHost, PHOSTDATA pHost)
{
  pHost->ulAttempts    = stDefOpt.ulAttempts;
  pHost->fRememberPswd = stDefOpt.fRememberPswd;
  strlcpy( pHost->stProperties.acHost, pszHost,
           sizeof(pHost->stProperties.acHost) );
  pHost->stProperties.acDestHost[0] = '\0';
  pHost->stProperties.ulQoS_DSCP = 0;

  // We don't store window title and Notification-window handle in INI-file,
  // it uses for cli only (-t, -N).
  pHost->acWinTitle[0] = '\0';
  pHost->hwndNotify = NULLHANDLE;

  pHost->stProperties.lListenPort = -1;

  if ( hIni != NULLHANDLE )
  {
    pHost->stProperties.ulBPP = utilINIQueryULong( hIni, pszHost,
                                             "BitsPerPixel", stDefOpt.ulBPP );
    pHost->stProperties.fViewOnly = utilINIQueryULong( hIni, pszHost,
                                             "ViewOnly", stDefOpt.fViewOnly );
    pHost->stProperties.fShareDesktop = utilINIQueryULong( hIni, pszHost,
                                             "ShareDesktop",
                                             stDefOpt.fShareDesktop );
    PrfQueryProfileString( hIni, pszHost, "Charset", stDefOpt.acCharset,
                           pHost->stProperties.acCharset,
                           sizeof(pHost->stProperties.acCharset) );
    PrfQueryProfileString( hIni, pszHost, "Encodings", stDefOpt.acEncodings,
                           pHost->stProperties.acEncodings,
                           sizeof(pHost->stProperties.acEncodings) );
    pHost->stProperties.ulCompressLevel = utilINIQueryULong( hIni, pszHost,
                                             "CompressLevel",
                                             stDefOpt.ulCompressLevel );
    pHost->stProperties.ulQualityLevel = utilINIQueryULong( hIni, pszHost,
                                             "QualityLevel",
                                             stDefOpt.ulQualityLevel );

    pHost->ulAttempts    = utilINIQueryULong( hIni, pszHost, "Attempts",
                                           stDefOpt.ulAttempts );
    pHost->fRememberPswd = utilINIQueryULong( hIni, pszHost, "RememberPswd",
                                           stDefOpt.fRememberPswd );
  }
  else
  {
    // The INI-file has not been open, use default values.

    pHost->stProperties.ulBPP           = stDefOpt.ulBPP;
    pHost->stProperties.fViewOnly       = stDefOpt.fViewOnly;
    pHost->stProperties.fShareDesktop   = stDefOpt.fShareDesktop;
    strlcpy( pHost->stProperties.acCharset, stDefOpt.acCharset,
             sizeof(pHost->stProperties.acEncodings) );
    strcpy( pHost->stProperties.acEncodings, stDefOpt.acEncodings );
    pHost->stProperties.ulCompressLevel = stDefOpt.ulCompressLevel;
    pHost->stProperties.ulQualityLevel  = stDefOpt.ulQualityLevel;
  }
}


/* ********************************************************** */
/*                                                            */
/*                      Launchpad dialog                      */
/*                                                            */
/* ********************************************************** */


// Page 1: Connect
// ---------------

static BOOL _getHost(HWND hwnd, ULONG cbBuf, PCHAR pcBuf)
{
  if ( WinQueryDlgItemText( hwnd, IDCB_HOST, cbBuf, pcBuf ) == 0 )
    return FALSE;

  utilStrTrim( pcBuf );
  strlwr( pcBuf );
  if ( !utilVerifyHostPort( strlen( pcBuf ), pcBuf ) )
  {
    utilMessageBox( hwnd, NULL, IDM_INVALID_HOST,
                    MB_ERROR | MB_MOVEABLE | MB_OK );
    WinSetFocus( HWND_DESKTOP, WinWindowFromID( hwnd, IDCB_HOST ) );
    return FALSE;
  }

  return TRUE;
}

static BOOL _wmPageConnectInitDlg(HWND hwnd, PDLGINITDATA pData)
{
  HINI       hIni = pData->hIni;
  ULONG      cbData;
  PCHAR      pcData;
  ULONG      ulCount = 0;

  // Read default options and list of hosts from the ini-file to COMBOBOX.

  if ( ( hIni != NULLHANDLE ) &&
       PrfQueryProfileSize( hIni, NULL, NULL, &cbData ) )
  {
    cbData++;
    pcData = malloc( cbData + 1 );

    if ( pcData != NULL )
    {
      pcData[0] = '\0';

      if ( PrfQueryProfileData( hIni, NULL, NULL, pcData, &cbData ) )
      {
        // Now pcData contains list of hosts: "Host1\0Host2\0\0",

        PCHAR        pcHost;
        ULONG        ulTime;
        HWND         hwndCtl = WinWindowFromID( hwnd, IDCB_HOST );
        ULONG        ulIdx;

        for( pcHost = pcData; *pcHost != '\0'; pcHost += strlen( pcHost ) + 1 )
        {
          if ( pcHost[0] == '.' )
            continue;

          // Query from the INI-file a time when host was used.
          ulTime = utilINIQueryULong( hIni, pcHost, "time", 0 );

          // Find position in combobox for host - sort by most recent usage.
          // Item handle is host's last using timestamp.
          for( ulIdx = 0; ulIdx < ulCount; ulIdx++ )
          {
            if ( (ULONG)WinSendMsg( hwndCtl, LM_QUERYITEMHANDLE,
                                    MPFROMSHORT(ulIdx), 0 ) < ulTime )
              break;
          }

          // Add the host to combobox.
          ulIdx = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_INSERTITEM,
                                            MPFROMSHORT(ulIdx), pcHost ) );
          if ( ( ulIdx != LIT_ERROR ) && ( ulIdx != LIT_MEMERROR ) )
          {
            WinSendMsg( hwndCtl, LM_SETITEMHANDLE, MPFROMSHORT(ulIdx),
                        MPFROMLONG(ulTime) );
            ulCount++;
          }

        }

        // Select a first host in the combobox.
        if ( ulCount != 0 )
          WinSendMsg( hwndCtl, LM_SELECTITEM, MPFROMSHORT( 0 ),
                      MPFROMLONG( TRUE ) );
      }

      free( pcData );
    } // if ( pcData != NULL )
  }

  // Setup controls.
  if ( ulCount == 0 )
  {
    WinEnableWindow( WinWindowFromID( hwnd, IDPB_OPTIONS ), FALSE );
    WinEnableWindow( WinWindowFromID( hwnd, IDPB_FORGET ), FALSE );
    WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ), FALSE );
  }
  WinSendDlgItemMsg( hwnd, IDCB_HOST, EM_SETTEXTLIMIT,
                     MPFROMSHORT(256), 0 );

  twSet( hwnd );

  return TRUE;
}

static VOID _cmdPageConnectOk(HWND hwnd)
{
  HINI       hIni;
  CHAR       acHost[272];
  HOSTDATA   stHost;

  if ( !_getHost( hwnd, sizeof(acHost), acHost ) )
    return;

  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
    // Store current time to host's record in INI to preform sort on next
    // dialog opening.
    utilINIWriteULong( hIni, acHost, "time", time( NULL ) );

  _iniQueryHost( hIni, acHost, &stHost );

  if ( hIni != NULLHANDLE )
    PrfCloseProfile( hIni );

  prStart( &stHost );
  WinDestroyWindow( WinQueryWindow( WinQueryWindow( hwnd, QW_OWNER ),
                    QW_PARENT ) );
}

static VOID _cmdPageConnectOptions(HWND hwnd)
{
  OPTDLGDATA          stData;
  POPTDLGDATA         pData = NULL;
  CHAR                acHost[272];
  HINI                hIni;

  if ( !_getHost( hwnd, sizeof(acHost), acHost ) )
    return;

  // Read options for host from the ini-file.
  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    _iniQueryOptions( hIni, acHost, &stData );
    PrfCloseProfile( hIni );
    pData = &stData;
  }
  else
    pData = &stDefOpt;

  optdlgOpen( hwnd, acHost, pData );
}

static VOID _cmdPageConnectForget(HWND hwnd)
{
  CHAR                acHost[272];
  HINI                hIni;
  HWND                hwndCtl = WinWindowFromID( hwnd, IDCB_HOST );
  SHORT               sIdx;

  if ( !_getHost( hwnd, sizeof(acHost), acHost ) )
    return;

  if ( utilMessageBox( hwnd, NULL, IDM_FORGET_QUESTION,
                       MB_YESNO | MB_ICONQUESTION ) != MBID_YES )
    return;

  // Remove options for host from the ini-file.
  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    PrfWriteProfileData( hIni, acHost, NULL, NULL, 0 );
    PrfCloseProfile( hIni );
  }

  // Remove host from the combobox.
  sIdx = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_SEARCHSTRING,
                                   MPFROM2SHORT( 0, LIT_FIRST ),
                                   MPFROMP( acHost ) ) );
  if ( sIdx != LIT_NONE )
    WinSendMsg( hwndCtl, LM_DELETEITEM, MPFROMSHORT(sIdx), 0 );

  WinSetWindowText( hwndCtl, "" );
}

static VOID _wmPageConnectTip(HWND hwnd, SHORT sCtlId, BOOL fTipReady)
{
  if ( fTipReady && ( sCtlId == IDCB_HOST ) )
    WinSendMsg( twQueryTipWinHandle(), WM_TIP_TEXT, 0,
                MPFROM2SHORT( IDM_TIP_HOST, 0 ) );
}

static VOID _wmPageConnectOptDlgEnter(HWND hwnd, POPTDLGDATA pData,
                                      BOOL fUseAsDefault)
{
  CHAR                acHost[272];
  HINI                hIni;
  HWND                hwndCtl = WinWindowFromID( hwnd, IDCB_HOST );

  if ( !_getHost( hwnd, sizeof(acHost), acHost ) )
    return;

  // Store options for host to the ini-file.
  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    utilINIWriteULong( hIni, acHost, "bwo", 1 );
    _iniWriteOptions( hIni, acHost, pData );
    if ( !pData->fRememberPswd )
    {
      PrfWriteProfileData( hIni, acHost, "UserPassword", NULL, 0 );
      PrfWriteProfileData( hIni, acHost, "Password", NULL, 0 );
    }

    if ( fUseAsDefault )
    {
      stDefOpt = *pData;
      _iniWriteOptions( hIni, INI_APP_DEFAULT, &stDefOpt );
    }

    PrfCloseProfile( hIni );
  }

  // Add a new host to combobox.
  if ( SHORT1FROMMR( WinSendMsg( hwndCtl, LM_SEARCHSTRING,
                                 MPFROM2SHORT( 0, LIT_FIRST ),
                                 MPFROMP( acHost ) ) ) == LIT_NONE )
    WinSendMsg( hwndCtl, LM_INSERTITEM, MPFROMSHORT(0), acHost );
}

static VOID _ctlComboHostChanged(HWND hwnd, HWND hwndCBHost)
{
  CHAR       acHost[272];
  PCHAR      pcHost;

  WinQueryWindowText( hwndCBHost, sizeof(acHost) - 1, acHost );

  WinEnableWindow( WinWindowFromID( hwnd, IDPB_FORGET ),
    (SHORT)SHORT1FROMMR( WinSendMsg( hwndCBHost, LM_SEARCHSTRING,
                              MPFROM2SHORT( 0, LIT_FIRST ),
                              MPFROMP( acHost ) ) ) != (SHORT)LIT_NONE
  );

  pcHost = acHost;
  STR_SKIP_SPACES( pcHost );
  WinEnableWindow( WinWindowFromID( hwnd, IDPB_OPTIONS ), *pcHost != '\0' );
  WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ), *pcHost != '\0' );
}

static PSZ _wmPageConnectSubstituteString(HWND hwnd, USHORT usKey)
{
  static CHAR       acBuf[272];

  if ( usKey != 0 )
    return NULL;

  if ( WinQueryDlgItemText( hwnd, IDCB_HOST, sizeof(acBuf), acBuf ) == 0 )
    return NULL;

  return acBuf;
}

static MRESULT EXPENTRY _dlgPageConnectProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmPageConnectInitDlg( hwnd, (PDLGINITDATA)mp2 );

    case WM_COMMAND:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case MBID_OK:
          _cmdPageConnectOk( hwnd );
          break;

        case IDPB_OPTIONS:
          _cmdPageConnectOptions( hwnd );
          break;

        case IDPB_FORGET:
          _cmdPageConnectForget( hwnd );
          break;
      }
      return (MRESULT)TRUE;

    case WM_CONTROL:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDCB_HOST:
          if ( SHORT2FROMMP( mp1 ) == CBN_EFCHANGE ) // Text has been changed.
            _ctlComboHostChanged( hwnd, HWNDFROMMP( mp2 ) );
      }
      return (MRESULT)TRUE;

    case WM_SUBSTITUTESTRING:
      return (MRESULT)_wmPageConnectSubstituteString( hwnd, SHORT1FROMMP(mp1) );

    case WM_TIP:
      _wmPageConnectTip( hwnd, SHORT1FROMMP(mp1), SHORT2FROMMP(mp1) != 0 );
      return (MRESULT)TRUE;

    case WM_OPTDLG_ENTER:
      _wmPageConnectOptDlgEnter( hwnd, (POPTDLGDATA)mp1, LONGFROMMP(mp2) != 0 );
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


// Page 2: Listen
// --------------

static BOOL _wmPageListenInitDlg(HWND hwnd, PDLGINITDATA pData)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCB_IFADDR );
  IOSTATAT   stStat;
  int        iSock;
  PSZ        pszIP;
  ULONG      ulIdx;

  WinSendMsg( hwndCtl, LM_INSERTITEM, MPFROMSHORT(0), "any" );

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
        pszIP = inet_ntoa( *((struct in_addr *)&stStat.aAddr[ulIdx].ulIP) );
        WinSendMsg( hwndCtl, LM_INSERTITEM, MPFROMSHORT(LIT_END), pszIP );
      }
    }

    soclose( iSock );
  }

  WinSendMsg( hwndCtl, LM_SELECTITEM, 0, MPFROMSHORT(TRUE) );
  WinSendDlgItemMsg( hwnd, IDSB_PORT, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( LISTEN_PORT_OFFSET ), 0 );

  twSet( hwnd );

  return TRUE;
}

static VOID _wmPageListenTip(HWND hwnd, SHORT sCtlId, BOOL fTipReady)
{
  SHORT      sMsgId;

  if ( !fTipReady )
    return;

  switch( sCtlId )
  {
    case IDCB_IFADDR:
      sMsgId = IDM_TIP_IFADDR;
      break;

    case IDSB_PORT:
      sMsgId = IDM_TIP_PORT;
      break;

    default:
      return;
  }

  WinSendMsg( twQueryTipWinHandle(), WM_TIP_TEXT, 0, MPFROM2SHORT(sMsgId, 0) );
}

static VOID _wmPageListenOptDlgEnter(HWND hwnd, POPTDLGDATA pData)
{
  HINI       hIni;

  // Store options for "listening mode" to the ini-file.
  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    utilINIWriteULong( hIni, INI_APP_LISTEN, "bwo", 1 );
    _iniWriteOptions( hIni, INI_APP_LISTEN, pData );
    if ( !pData->fRememberPswd )
    {
      PrfWriteProfileData( hIni, INI_APP_LISTEN, "UserPassword", NULL, 0 );
      PrfWriteProfileData( hIni, INI_APP_LISTEN, "Password", NULL, 0 );
    }

    PrfCloseProfile( hIni );
  }
}

static VOID _cmdPageListenOk(HWND hwnd)
{
  HINI       hIni;
  HOSTDATA   stHost;
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCB_IFADDR );
  CHAR       acBuf[16];
  SHORT      sSelect;

  hIni = _iniOpen();

  _iniQueryHost( hIni, INI_APP_LISTEN, &stHost );

  if ( hIni != NULLHANDLE )
    PrfCloseProfile( hIni );

  // Get interface address.

  sSelect = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_QUERYSELECTION,
                                      MPFROMSHORT(LIT_FIRST), 0 ) );
  if ( sSelect == 0 )
    stHost.stProperties.acHost[0] = '\0';
  else
    WinQueryDlgItemText( hwnd, IDCB_IFADDR,
                         sizeof(stHost.stProperties.acHost),
                         stHost.stProperties.acHost );

  // Get Port value.

  WinSendDlgItemMsg( hwnd, IDSB_PORT, SPBM_QUERYVALUE, MPFROMP( acBuf ),
                     MPFROM2SHORT( sizeof(acBuf), SPBQ_DONOTUPDATE ) );
  stHost.stProperties.lListenPort = atol( acBuf );

  if ( ccSearchListener( stHost.stProperties.acHost,
                         stHost.stProperties.lListenPort ) )
  {
    utilMessageBox( hwnd, NULL, IDM_ALREADY_BINDED,
                    MB_ICONHAND | MB_MOVEABLE | MB_OK );
  }
  else
  {
    prStart( &stHost );

    // Destroy launchpad dialog.
    WinDestroyWindow( WinQueryWindow( WinQueryWindow( hwnd, QW_OWNER ),
                      QW_PARENT ) );
  }
}

static VOID _cmdPageListenOptions(HWND hwnd)
{
  OPTDLGDATA          stData;
  POPTDLGDATA         pData;
  HINI                hIni;

  // Read options for listening from the ini-file.
  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    _iniQueryOptions( hIni, INI_APP_LISTEN, &stData );
    PrfCloseProfile( hIni );
    pData = &stData;
  }
  else
    pData = &stDefOpt;

  // Show options dialog. Host name is NULL - for "listening mode" options.
  optdlgOpen( hwnd, NULL, pData );
}

static MRESULT EXPENTRY _dlgPageListenProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmPageListenInitDlg( hwnd, (PDLGINITDATA)mp2 );

    case WM_COMMAND:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case MBID_OK:
          _cmdPageListenOk( hwnd );
          break;

        case IDPB_OPTIONS:
          _cmdPageListenOptions( hwnd );
          break;
      }
      return (MRESULT)TRUE;

    case WM_TIP:
      _wmPageListenTip( hwnd, SHORT1FROMMP(mp1), SHORT2FROMMP(mp1) != 0 );
      return (MRESULT)TRUE;

    case WM_OPTDLG_ENTER:
      _wmPageListenOptDlgEnter( hwnd, (POPTDLGDATA)mp1 );
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


// Dialog window
// -------------

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      cOpenWin++;
      break;

    case WM_DESTROY:
      lpStoreWinPresParam( hwnd );
      cOpenWin--;
      break;

    case WM_CLOSE:
      WinDestroyWindow( hwnd );
      break;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}

// Look for an existing server dialog window.
HWND _queryOpenedDlg()
{
  PPIB       pib;
  PTIB       tib;
  HWND       hwndTop;
  HENUM      henum;
  PID        pidWin;
  TID        tidWin;

  DosGetInfoBlocks( &tib, &pib );

  // Enumerate all top-level windows.           
  henum = WinBeginEnumWindows( HWND_DESKTOP );

  // Loop through all enumerated windows.       
  while( ( hwndTop = WinGetNextWindow( henum ) ) != NULLHANDLE )
  {
    if ( WinQueryWindowProcess( hwndTop, &pidWin, &tidWin ) &&
         ( pidWin == pib->pib_ulpid ) &&
         ( WinQueryWindowUShort( hwndTop, QWS_ID ) == IDDLG_LAUNCHPAD ) )
      break;
  }

  WinEndEnumWindows( henum );
  return hwndTop;
}


BOOL lpOpenDlg()
{
  HWND         hwndDlg = _queryOpenedDlg();
  HWND         hwndNB, hwndPage, hwndCBHost;
  LONG         lPageId;
  DLGINITDATA  stDlgInitData;
  CHAR         acBuf[256];

  if ( hwndDlg != NULLHANDLE )
  {
    // Window already exists. Bring it on top and return.
    WinSetWindowPos( hwndDlg, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER | SWP_ACTIVATE );
    return TRUE;
  }

  stDlgInitData.usSize = sizeof(DLGINITDATA);
  stDlgInitData.hIni = _iniOpen();
  if ( stDlgInitData.hIni != NULLHANDLE )
    _iniQueryOptions( stDlgInitData.hIni, INI_APP_DEFAULT, &stDefOpt );
  do
  {
    hwndDlg = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, _dlgProc, NULLHANDLE,
                          IDDLG_LAUNCHPAD, &stDlgInitData );
    if ( hwndDlg == NULLHANDLE )
    {
      debug( "WinLoadDlg(,,,,IDDLG_LAUNCHPAD,) failed" );
      break;
    }

    hwndNB = WinWindowFromID( hwndDlg, IDD_NOTEBOOK );
    if ( hwndNB == NULLHANDLE )
    {
      debug( "Cannot find notebook, id: %u", IDD_NOTEBOOK );
      break;
    }

    // Page 1 "Connect".

    hwndPage = WinLoadDlg( hwndNB, hwndNB, _dlgPageConnectProc, NULLHANDLE,
                           IDDLG_PAGE_CONNECT, &stDlgInitData );
    if ( hwndPage == NULLHANDLE )
    {
      debug( "WinLoadDlg(,,,,IDDLG_PAGE_CONNECT,) failed" );
      WinDestroyWindow( hwndDlg );
      return FALSE;
    }

    lPageId = (LONG)WinSendMsg( hwndNB, BKM_INSERTPAGE, NULL,
                        MPFROM2SHORT( BKA_AUTOPAGESIZE | BKA_MAJOR, BKA_LAST ) );
    WinLoadString( hab, 0, IDS_CONNECT, sizeof(acBuf), acBuf );
    WinSendMsg( hwndNB, BKM_SETTABTEXT, MPFROMLONG( lPageId ), MPFROMP(acBuf) );
    WinSendMsg( hwndNB, BKM_SETPAGEWINDOWHWND, MPFROMLONG( lPageId ),
                MPFROMLONG( hwndPage ) );
    WinSetOwner( hwndPage, hwndNB );
    hwndCBHost = WinWindowFromID( hwndPage, IDCB_HOST );

    // Page 2 "Listen".

    hwndPage = WinLoadDlg( hwndNB, hwndNB, _dlgPageListenProc, NULLHANDLE,
                           IDDLG_PAGE_LISTEN, &stDlgInitData );
    if ( hwndPage == NULLHANDLE )
    {
      debug( "WinLoadDlg(,,,,IDDLG_PAGE_LISTEN,) failed" );
      break;
    }

    lPageId = (LONG)WinSendMsg( hwndNB, BKM_INSERTPAGE, NULL,
                        MPFROM2SHORT( BKA_AUTOPAGESIZE | BKA_MAJOR, BKA_LAST ) );
    WinLoadString( hab, 0, IDS_LISTEN, sizeof(acBuf), acBuf );
    WinSendMsg( hwndNB, BKM_SETTABTEXT, MPFROMLONG( lPageId ), MPFROMP(acBuf) );
    WinSendMsg( hwndNB, BKM_SETPAGEWINDOWHWND, MPFROMLONG( lPageId ),
                MPFROMLONG( hwndPage ) );
    WinSetOwner( hwndPage, hwndNB );

    if ( stDlgInitData.hIni != NULLHANDLE )
      utilINIQueryWinPresParam( hwndDlg, stDlgInitData.hIni, ".PresParam" );

    // Show dialog window.
    WinSetWindowPos( hwndDlg, HWND_TOP, 0, 0, 0, 0,
                     SWP_ACTIVATE | SWP_ZORDER | SWP_SHOW );
    WinSetFocus( HWND_DESKTOP, hwndCBHost );

    if ( stDlgInitData.hIni != NULLHANDLE )
      PrfCloseProfile( stDlgInitData.hIni );

    return TRUE;
  }
  while( FALSE );

  if ( hwndDlg != NULLHANDLE )
    WinDestroyWindow( hwndDlg );

  if ( stDlgInitData.hIni != NULLHANDLE )
    PrfCloseProfile( stDlgInitData.hIni );

  return FALSE;
}


/* ********************************************************** */
/*                                                            */
/*                 Public INI-related functions               */
/*                                                            */
/* ********************************************************** */

BOOL lpStoreLogonInfo(PSZ pszHost, PCCLOGONINFO pLogonInfo)
{
  HINI       hIni;
  BOOL       fSuccess = FALSE;

  debug( "Store %s...", pLogonInfo->fCredential ? "credential" : "password" );

  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    PCHAR    pcPassword;
    ULONG    cbPassword;

    _b64Encode( pLogonInfo->acPassword, strlen( pLogonInfo->acPassword ),
                &pcPassword, &cbPassword );

    fSuccess = ( !pLogonInfo->fCredential ||
                 PrfWriteProfileString( hIni, pszHost, "UserName",
                                        pLogonInfo->acUserName ) ) &&
               PrfWriteProfileData( hIni, pszHost,
                                    pLogonInfo->fCredential
                                      ? "UserPassword" : "Password",
                                    pcPassword, cbPassword );

    memset( pcPassword, 0, cbPassword );
    free( pcPassword );
    PrfCloseProfile( hIni );
  }

  if ( !fSuccess )
    debug( "Failed" );

  return fSuccess;
}

BOOL lpQueryLogonInfo(PSZ pszHost, PCCLOGONINFO pLogonInfo,
                          BOOL fCredential)
{
  HINI       hIni;
  BOOL       fSuccess;
  ULONG      cbPassword = sizeof(pLogonInfo->acPassword);

  debug( "Load %s...", fCredential ? "credential" : "password" );

  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    pLogonInfo->fCredential = fCredential;

    fSuccess = ( !fCredential ||
                 PrfQueryProfileString( hIni, pszHost, "UserName", NULL,
                                        pLogonInfo->acUserName,
                                        sizeof(pLogonInfo->acUserName) ) > 1 )
            && PrfQueryProfileData( hIni, pszHost,
                                    fCredential ? "UserPassword" : "Password",
                                    pLogonInfo->acPassword, &cbPassword );

    if ( fSuccess )
    {
      PCHAR  pcDecPswd;
      ULONG  cbDecPswd;

      _b64Decode( pLogonInfo->acPassword, cbPassword,
                  &pcDecPswd, &cbDecPswd );
      strlcpy( pLogonInfo->acPassword, pcDecPswd,
               sizeof(pLogonInfo->acPassword) );
      memset( pcDecPswd, 0, cbDecPswd );
      free( pcDecPswd );
    }

    PrfCloseProfile( hIni );
  }
  else
    fSuccess = FALSE;

  if ( !fSuccess )
    debug( "Failed" );

  return fSuccess;
}

BOOL lpStoreWinRect(PSZ pszHost, PRECTL pRect)
{
  HINI       hIni;
  BOOL       fSuccess = FALSE;

  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    fSuccess = PrfWriteProfileData( hIni, pszHost, "WinRect",
                                    pRect, sizeof(RECTL) );

    PrfCloseProfile( hIni );
  }

  if ( !fSuccess )
    debug( "Failed" );

  return fSuccess;
}

BOOL lpQueryWinRect(PSZ pszHost, PRECTL pRect)
{
  HINI       hIni;
  BOOL       fSuccess = FALSE;
  ULONG      cbRect = sizeof(RECTL);

  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    fSuccess = PrfQueryProfileData( hIni, pszHost, "WinRect",
                                    pRect, &cbRect ) &&
               ( cbRect == sizeof(RECTL) );

    PrfCloseProfile( hIni );
  }

  if ( !fSuccess )
    debug( "Failed" );

  return fSuccess;
}

BOOL lpCommitArg(int argc, char** argv)
{
  PHOSTDATA    pHost = NULL, pHostsNew, pHosts = NULL;
  ULONG        cHosts = 0;
  HINI         hIni;
  CHAR         chSw;
  PSZ          pszVal;
  ULONG        cbVal;
  LONG         lErrorMsgId = -1; // Error message id in rosource message table.

  if ( argc <= 1 )
  {
    // No arguments - show dialog.
    lpOpenDlg();
    return TRUE;
  }

  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
    _iniQueryOptions( hIni, INI_APP_DEFAULT, &stDefOpt );

  do
  {
    argc--;
    if ( argc == 0 )
      // End of switch list.
      break;

    argv++;
    if ( ( (*argv)[0] != '-' ) || ( (*argv)[1] == '\0' ) )
    {
      // Switch does not begin with character '-' or has no character after '-'.
      lErrorMsgId = IDM_SW_INVALID;
      chSw = ' ';
      break;
    }

    chSw = (*argv)[1]; // The switch character after '-'.

    if ( strchr( "hlravcdseoqtEN", chSw ) != NULL )
    {
      // The switch must have value (like: "-a123" or "-a 123" ).

      pszVal = &(*argv)[2];
      if ( *pszVal == '\0' )
      {
        // Value in next argument (like: -a<SPACE>123).
        argc--;
        argv++;
        if ( argc == 0 )
        {
          lErrorMsgId = IDM_SW_NO_VALUE;
          break;
        }

        pszVal = *argv;
        STR_SKIP_SPACES( pszVal );
        if ( *pszVal == '\0' )
        {
          lErrorMsgId = IDM_SW_NO_VALUE;
          break;
        }
      }

      cbVal = strlen( pszVal );
    }
    else
    {
      pszVal = NULL;
      cbVal = 0;
    }

    // Keys except -h, -l and -R must be specified after the key -h.
    if ( ( strchr( "hlR", chSw ) == NULL ) && ( cHosts == 0 ) )
    {
      lErrorMsgId = IDM_SW_NO_HOST;
      break;
    }

    // Now, we have: chSw - switch character, pszVal/cbVal - switch value.

    switch( chSw )
    {
      case 'h':        // "-h host"
        pHostsNew = realloc( pHosts, sizeof(HOSTDATA) * (cHosts + 1) );
        if ( pHostsNew == NULL )
          break;

        pHosts = pHostsNew;
        pHost = &pHosts[cHosts];
        _iniQueryHost( hIni, pszVal, pHost );
        cHosts++;
        break;

      case 'l':        // "-l ip-address:port"
        {
          PCHAR        pcColon = strchr( pszVal, ':' );
          ULONG        ulPort;

          if ( pcColon == NULL )
            ulPort = LISTEN_PORT_OFFSET;
          else
          {
            cbVal = pcColon - pszVal; // IP-address length.
            pcColon++;

            if ( !utilStrToULong( strlen( pcColon ), pcColon, 1, 0xFFFF,
                                  &ulPort ) )
            {
              lErrorMsgId = IDM_SW_INVALID_VALUE;
              break;
            }
          }

          pHostsNew = realloc( pHosts, sizeof(HOSTDATA) * (cHosts + 1) );
          if ( pHostsNew == NULL )
            break;

          pHosts = pHostsNew;
          pHost = &pHosts[cHosts];
          _iniQueryHost( hIni, INI_APP_LISTEN, pHost );

          if ( ( cbVal == 1 && pszVal[0] == '*' ) ||
               ( cbVal == 3 && memicmp( pszVal, "any", 3 ) == 0 ) )
            // '*' or 'any' instead ip-address.
//            [Digi] 7.05.2017 Next comented line changed on "cbVal = 0;"
//            pHost->stProperties.acHost[0] = '\0';
            cbVal = 0;
          else
          {
            if ( cbVal >= sizeof(pHost->stProperties.acHost) )
              cbVal = sizeof(pHost->stProperties.acHost) - 1;
            memcpy( pHost->stProperties.acHost, pszVal, cbVal );
          }

          pHost->stProperties.acHost[cbVal] = '\0';
          pHost->stProperties.lListenPort   = ulPort;
          cHosts++;
        }
        break;

      case 'r':        // -r <1|Y|YES|ON|0|N|NO|OFF> - Remember password.
        if ( !utilStrToBool( cbVal, pszVal, &pHost->fRememberPswd ) )
          lErrorMsgId = IDM_SW_INVALID_VALUE;
        else if ( !pHost->fRememberPswd )
        {
          // On "-r N" we remove password from INI immediately.
          PrfWriteProfileData( hIni, pHost->stProperties.acHost,
                               "UserPassword", NULL, 0 );
          PrfWriteProfileData( hIni, pHost->stProperties.acHost,
                               "Password", NULL, 0 );
        }
        break;

      case 'a':        // -a NN                      - Connection attempts.
        if ( !utilStrToULong( cbVal, pszVal, 1, 10, &pHost->ulAttempts ) )
          lErrorMsgId = IDM_SW_INVALID_VALUE;
        break;

      case 'v':        // -v <1|Y|YES|ON|0|N|NO|OFF> - View-only mode.
        if ( !utilStrToBool( cbVal, pszVal,
                             &pHost->stProperties.fViewOnly ) )
          lErrorMsgId = IDM_SW_INVALID_VALUE;
        break;

      case 'c':        // -c <8|16|32|TrueColor>     - Color depth.
        switch( utilStrWordIndex( "8 16 32 TRUECOLOR", cbVal, pszVal ) )
        {
          case 0:  pHost->stProperties.ulBPP = 8;  break;
          case 1:  pHost->stProperties.ulBPP = 16; break;
          case 2:
          case 3:  pHost->stProperties.ulBPP = 32; break;
          default:
            lErrorMsgId = IDM_SW_INVALID_DEPTH;
        }
        break;

      case 'd':        // -d host                    - Destination host.
        strlcpy( pHost->stProperties.acDestHost, pszVal,
                 sizeof(pHost->stProperties.acDestHost) );
        break;

      case 's':        // -s  <1|Y|YES|ON|0|N|NO|OFF> - Request shared session.
        if ( !utilStrToBool( cbVal, pszVal,
                             &pHost->stProperties.fShareDesktop ) )
          lErrorMsgId = IDM_SW_INVALID_VALUE;
        break;

      case 'e':        // -e "enc1 enc2 ..."         - Encodings.
        strlcpy( pHost->stProperties.acEncodings, pszVal,
                 sizeof(pHost->stProperties.acEncodings) );
        break;

      case 'o':        // -o N                       - Compress level.
        if ( !utilStrToULong( cbVal, pszVal, 0, 9,
                              &pHost->stProperties.ulCompressLevel ) )
          lErrorMsgId = IDM_SW_INVALID_VALUE;
        break;


      case 'q':        // -q N                       - Quality level.
        if ( !utilStrToULong( cbVal, pszVal, 0, 9,
                              &pHost->stProperties.ulQualityLevel ) )
          lErrorMsgId = IDM_SW_INVALID_VALUE;
        break;

      case 't':        // -t "Window title"          - Window title.
        strlcpy( pHost->acWinTitle, pszVal, sizeof(pHost->acWinTitle) );
        break;

      case 'E':        // -E <charset>               - Character encoding.
        strlcpy( pHost->stProperties.acCharset, pszVal, CHARSET_NAME_MAX_LEN );
        break;

      case 'N':        // -N N                       - Notification-window hdl.
        if ( !utilStrToULong( cbVal, pszVal, 1, ~0, &pHost->hwndNotify ) )
          lErrorMsgId = IDM_SW_INVALID_VALUE;
        break;

      case 'R':        // -R                         - Reset GUI fonts & colors.
        PrfWriteProfileData( hIni, ".PresParam", NULL, NULL, 0 );
        break;

      default:
        lErrorMsgId = IDM_SW_UNKNOWN;

    } // switch( chSw )
  }
  while( lErrorMsgId == -1 );

  if ( hIni != NULLHANDLE )
    PrfCloseProfile( hIni );

  if ( lErrorMsgId != -1 )
  {
    // Error occurred. Load text from message table, append it with " (-?)" and
    // show in the message box.

    CHAR     acBuf[256];
    ULONG    cbBuf;

    cbBuf = WinLoadMessage( hab, 0, lErrorMsgId, sizeof(acBuf) - 8, acBuf );
    sprintf( &acBuf[cbBuf], " (-%c).", chSw );

    WinMessageBox( HWND_DESKTOP, HWND_DESKTOP, acBuf, APP_NAME, 0,
                   MB_OK | MB_ERROR | MB_SYSTEMMODAL | MB_MOVEABLE );
  }
  else
  {
    // Start sessions.

    for( pHost = pHosts; cHosts > 0; cHosts--, pHost++ )
      prStart( pHost );
  }

  free( pHosts );

  return lErrorMsgId == -1; // FALSE on error.
}

VOID lpStoreWinPresParam(HWND hwnd)
{
  HINI       hIni;

  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    utilINIWriteWinPresParam( hwnd, hIni, ".PresParam" );
    PrfCloseProfile( hIni );
  }
}

VOID lpQueryWinPresParam(HWND hwnd)
{
  HINI       hIni;

  hIni = _iniOpen();
  if ( hIni != NULLHANDLE )
  {
    utilINIQueryWinPresParam( hwnd, hIni, ".PresParam" );
    PrfCloseProfile( hIni );
  }
}

