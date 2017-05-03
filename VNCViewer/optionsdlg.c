#define INCL_DOSERRORS
#define INCL_WIN
#define INCL_WINDIALOGS
#define INCL_DOSMISC
#include <os2.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <utils.h>
#include "clntconn.h"
#include "resource.h"
#include "utils.h"
#include "tipwin.h"
#include "optionsdlg.h"
#include "lnchpad.h"
#include <debug.h>

#define _WMLB_CHECK_SELECTION    (WM_USER + 1)

typedef struct _DLGINITDATA {
  USHORT               usSize;
  POPTDLGDATA          pData;
  PSZ                  pszHost;  // NULL - options for "listening mode".
  HWND                 hwndRealOwner;
} DLGINITDATA, *PDLGINITDATA;

typedef struct _WINDATA {
  POPTDLGDATA          pOptDlgData;
  PSZ                  pszHost;  // NULL - options for "listening mode".
  HWND                 hwndRealOwner;
} WINDATA, *PWINDATA;


extern HAB             hab;

// All available encodings, see ibvncclient\rfbproto.c, SetFormatAndEncodings().
static PSZ             pszEncodings =
        "RAW CopyRect Tight Hextile Zlib ZlibHex ZRLE ZYWRLE Ultra CoRRE RRE";

static PFNWP           fnOldLBProc;


// Window procedure for listboxes to inform owner about possible selection
// changes.
static MRESULT EXPENTRY _lbEncProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_CHAR:
    case WM_BUTTON1DOWN:
    case LM_DELETEALL:
    case LM_SELECTITEM:
    case LM_DELETEITEM:
      WinPostMsg( WinQueryWindow( hwnd, QW_OWNER ), _WMLB_CHECK_SELECTION,
                  MPFROMSHORT( WinQueryWindowUShort( hwnd, QWS_ID ) ), 0 );
      break;
  }

  return fnOldLBProc( hwnd, msg, mp1, mp2 );
}


// Read selected encodings into the buffer as ASCIIZ string.
static VOID _selEncQuery(HWND hwnd, ULONG cbBuf, PCHAR pcBuf)
{
  HWND       hwndLB = WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS );
  SHORT      cbEncoding, sIdx = 0;

  while( cbBuf > 0 )  
  {
    if ( sIdx != 0 )
    {
      *pcBuf = ' ';
      pcBuf++;
      cbBuf--;
    }

    cbEncoding = SHORT1FROMMR( WinSendMsg( hwndLB, LM_QUERYITEMTEXT,
                                   MPFROM2SHORT(sIdx,cbBuf), MPFROMP(pcBuf) ) );
    if ( cbEncoding <= 0 )
    {
      if ( sIdx != 0 )
      {
        pcBuf--;
        *pcBuf = '\0';
      }
      break;
    }

    pcBuf += cbEncoding;
    cbBuf -= cbEncoding;
    sIdx++;
  }
}

// Enables/disables "quality" and "compress" sliders controls.
// Also, disable Ok if no one encoding is selected.
static VOID _selEncCheck(HWND hwnd)
{
  CHAR       acEncodings[ENC_LIST_MAX_LEN];
  BOOL       fTight;

  _selEncQuery( hwnd, sizeof(acEncodings), acEncodings );
  fTight = utilStrWordIndex( acEncodings, 5, "Tight" ) != -1;

  WinEnableWindow( WinWindowFromID( hwnd, IDSLID_COMPRESS ),
                   fTight ||
                   ( utilStrWordIndex( acEncodings, 4, "Zlib" ) != -1 ) ||
                   ( utilStrWordIndex( acEncodings, 7, "ZlibHex" ) != -1 ) );
  WinEnableWindow( WinWindowFromID( hwnd, IDSLID_QUALITY ),
          fTight || ( utilStrWordIndex( acEncodings, 6, "ZYWRLE" ) != -1 ) );
  WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ), acEncodings[0] != '\0' );
}

// Fill "available encodings" list box (all not selected encodings).
static VOID _availEncFill(HWND hwnd, PSZ pszSelEncodings)
{
  HWND       hwndLBAllEnc = WinWindowFromID( hwnd, IDLB_ALL_ENCODINGS );
  PCHAR      pcEncoding, pcEncodings = pszEncodings;
  ULONG      cbEncoding, cbEncodings = strlen( pszEncodings );
  CHAR       acEncoding[ENC_NAME_MAX_LEN];

  WinSendMsg( hwndLBAllEnc, LM_DELETEALL, 0, 0 );

  while( utilStrCutWord( &cbEncodings, &pcEncodings, &cbEncoding, &pcEncoding ) )
  {
    if ( utilStrWordIndex( pszSelEncodings, cbEncoding, pcEncoding ) != -1 )
      // Encoding is selected.
      continue;

    memcpy( acEncoding, pcEncoding, cbEncoding );
    acEncoding[cbEncoding] = '\0';
    WinSendMsg( hwndLBAllEnc, LM_INSERTITEM, MPFROMLONG(LIT_END),
                MPFROMP(acEncoding) );
  }
}

static VOID _optdlgSetupSlider(HWND hwnd, ULONG ulId)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, ulId );
  ULONG      ulIdx;

  if ( hwndCtl == NULLHANDLE )
  {
    printf( "Slider #%u not exist\n", ulId );
    return;
  }

  // Shaft Height 
  WinSendMsg( hwndCtl, SLM_SETSLIDERINFO,
              MPFROM2SHORT( SMA_SHAFTDIMENSIONS, 0 ), MPFROMLONG( 12 ) );
  // Arm width and height
  WinSendMsg( hwndCtl, SLM_SETSLIDERINFO,
              MPFROM2SHORT( SMA_SLIDERARMDIMENSIONS, 10 ),
              MPFROM2SHORT( 10, 20 ) );
  // Size of a tick mark
  for( ulIdx = 0; ulIdx < 10; ulIdx += 1 )
    WinSendMsg( hwndCtl, SLM_SETTICKSIZE, MPFROM2SHORT( ulIdx, 5 ), 0 );
  // Text above a tick mark
  WinSendMsg( hwndCtl, SLM_SETSCALETEXT, MPFROMSHORT( 1 ), MPFROMP( "Low" ) );
  WinSendMsg( hwndCtl, SLM_SETSCALETEXT, MPFROMSHORT( 8 ), MPFROMP( "High" ) );
}

static BOOL _wmInitDlg(HWND hwnd, PDLGINITDATA pInitData)
{
  PSZ                  apszBPP[3] = { "8", "16", "32" };
  PWINDATA             pWinData = calloc( 1, sizeof(WINDATA) );
  POPTDLGDATA          pDataIn = pInitData->pData;
  CHAR                 acTitle[64];

  if ( pWinData == NULL )
    return FALSE;

  lpQueryWinPresParam( hwnd );

  WinSetWindowPtr( hwnd, QWL_USER, pWinData );

  pWinData->pszHost = pInitData->pszHost == NULL
                        ? NULL : strdup( pInitData->pszHost );
  pWinData->hwndRealOwner = pInitData->hwndRealOwner;

  if ( pDataIn != NULL )
  {
    ULONG              cbDlgData = sizeof(OPTDLGDATA);
    POPTDLGDATA        pData;

    // Copy optinons.

    pData = malloc( cbDlgData );
    if ( pData != NULL )
    {
      memcpy( pData, pDataIn, cbDlgData );
      pWinData->pOptDlgData = pData;
    }
  } // if ( pDataIn != NULL )

  // Setup controls.

  // No "attempts" spin button and "Use as default options for new hosts"
  // checkbox when options for "listening mode".
  WinEnableWindow( WinWindowFromID( hwnd, IDSB_ATTEMPTS ),
                   pInitData->pszHost != NULL );
  WinEnableWindow( WinWindowFromID( hwnd, IDCB_USE_AS_DEFAULT ),
                   pInitData->pszHost != NULL );

  WinSendDlgItemMsg( hwnd, IDSB_BPP, SPBM_SETARRAY,
                     MPFROMP( &apszBPP ), MPFROMLONG( 3 ) );

  _optdlgSetupSlider( hwnd, IDSLID_QUALITY );
  _optdlgSetupSlider( hwnd, IDSLID_COMPRESS );

  WinSetFocus( HWND_DESKTOP,
               WinWindowFromID( hwnd,
                                pInitData->pszHost != NULL
                                  ? IDSB_ATTEMPTS
                                  : IDSB_BPP ) );

  // Disable button "Undo" if options was not specified for this dialog.
  WinEnableWindow( WinWindowFromID( hwnd, IDPB_UNDO ), pDataIn != NULL );

  // Subclass function for listboxes to control selection changes.
  fnOldLBProc = WinSubclassWindow( WinWindowFromID( hwnd, IDLB_ALL_ENCODINGS ),
                                   _lbEncProc );
  WinSubclassWindow( WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS ),
                     _lbEncProc );

  // Set state of controls according to the options.
  WinSendMsg( hwnd, WM_COMMAND, MPFROMLONG(IDPB_UNDO), 0 );

  // Substitute for the window title.
  if ( WinQueryWindowText( hwnd, sizeof(acTitle), acTitle ) != 0 )
  {
    CHAR     acNewTitle[320];

    if ( WinSubstituteStrings( hwnd, acTitle, sizeof(acNewTitle),
                               acNewTitle ) != 0 )
      WinSetWindowText( hwnd, acNewTitle );
  }

  // Show dialog window.
  WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0,
                   SWP_ACTIVATE | SWP_ZORDER | SWP_SHOW );

  WinEnableWindow( WinQueryWindow( hwnd, QW_OWNER ), FALSE );

  return TRUE;
}

static VOID _wmDestroy(HWND hwnd)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND       hwndOwner = WinQueryWindow( hwnd, QW_OWNER );

  lpStoreWinPresParam( hwnd );

  if ( pWinData != NULL )
  {
    if ( pWinData->pszHost != NULL )
      free( pWinData->pszHost );

    if ( pWinData->pOptDlgData != NULL )
      free( pWinData->pOptDlgData );
    free( pWinData );
  }

  WinEnableWindow( hwndOwner, TRUE );
  WinSetWindowPos( hwndOwner, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER | SWP_ACTIVATE );
}

static VOID _cmdEncAdd(HWND hwnd)
{
  HWND       hwndLBAllEnc = WinWindowFromID( hwnd, IDLB_ALL_ENCODINGS );
  HWND       hwndLBSelEnc = WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS );
  SHORT      sIdx, sInsIdx;
  CHAR       acEncoding[ENC_NAME_MAX_LEN];

  sInsIdx = SHORT1FROMMR( WinSendMsg( hwndLBSelEnc, LM_QUERYSELECTION,
                                   MPFROMSHORT(LIT_FIRST), 0 ) );
  if ( sInsIdx == LIT_NONE )
    sInsIdx = LIT_END;

  WinSendMsg( hwndLBSelEnc, LM_SELECTITEM, MPFROMSHORT(LIT_NONE), 0 );

  while( TRUE )
  {
    sIdx = SHORT1FROMMR( WinSendMsg( hwndLBAllEnc, LM_QUERYSELECTION,
                                     MPFROMSHORT(LIT_FIRST), 0 ) );
    if ( sIdx == LIT_NONE )
      break;

    WinSendMsg( hwndLBAllEnc, LM_QUERYITEMTEXT,
                MPFROM2SHORT(sIdx,sizeof(acEncoding)),
                MPFROMP(acEncoding) );
    WinSendMsg( hwndLBAllEnc, LM_DELETEITEM, MPFROMSHORT(sIdx), 0 );

    sIdx = SHORT1FROMMR( WinSendMsg( hwndLBSelEnc, LM_INSERTITEM,
                                 MPFROMSHORT(sInsIdx), MPFROMP(acEncoding) ) );
    if ( ( sIdx == LIT_MEMERROR ) || ( sIdx == LIT_ERROR ) )
    {
      debugCP( "Item insert error" );
      continue;
    }

    WinSendMsg( hwndLBSelEnc, LM_SELECTITEM, MPFROMSHORT(sIdx),
                MPFROMSHORT(0xFFFF) );

    if ( sInsIdx != LIT_END )
      sInsIdx++;
  }

  _selEncCheck( hwnd );
}

static VOID _cmdEncDelete(HWND hwnd)
{
  HWND       hwndLBSelEnc = WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS );
  SHORT      sIdx;
  CHAR       acEncodings[ENC_LIST_MAX_LEN];

  while( TRUE )
  {
    sIdx = SHORT1FROMMR( WinSendMsg( hwndLBSelEnc, LM_QUERYSELECTION,
                                     MPFROMSHORT(LIT_FIRST), 0 ) );
    if ( sIdx == LIT_NONE )
      break;

    WinSendMsg( hwndLBSelEnc, LM_DELETEITEM, MPFROMSHORT(sIdx), 0 );
  }

  _selEncQuery( hwnd, sizeof(acEncodings), acEncodings );
  _availEncFill( hwnd, acEncodings );
  _selEncCheck( hwnd );
}

static VOID _cmdEncMove(HWND hwnd, BOOL fForward)
{
  HWND       hwndLBSelEnc = WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS );
  SHORT      sIdx, sCount;
  CHAR       acEncoding[ENC_NAME_MAX_LEN];

  sIdx = SHORT1FROMMR( WinSendMsg( hwndLBSelEnc, LM_QUERYSELECTION,
                                   MPFROMSHORT(LIT_FIRST), 0 ) );
  sCount = SHORT1FROMMR( WinSendMsg( hwndLBSelEnc, LM_QUERYITEMCOUNT, 0, 0 ) );

  if ( ( sIdx == LIT_NONE ) || ( fForward && sIdx == (sCount - 1) ) ||
       ( !fForward && sIdx == 0 ) )
    return;

  WinSendMsg( hwndLBSelEnc, LM_QUERYITEMTEXT,
              MPFROM2SHORT(sIdx,sizeof(acEncoding)), MPFROMP(acEncoding) );

  WinSendMsg( hwndLBSelEnc, LM_DELETEITEM, MPFROMSHORT(sIdx), 0 );
  sIdx += fForward ? 1 : -1;
  WinSendMsg( hwndLBSelEnc, LM_INSERTITEM, MPFROMSHORT(sIdx),
              MPFROMP(acEncoding) );
  WinSendMsg( hwndLBSelEnc, LM_SELECTITEM, MPFROMSHORT(LIT_NONE), 0 );
  WinSendMsg( hwndLBSelEnc, LM_SELECTITEM, MPFROMSHORT(sIdx),
              MPFROMSHORT(0xFFFF) );
}

static BOOL _cmdOk(HWND hwnd)
{
  PWINDATA             pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  OPTDLGDATA           stData;
  CHAR                 acBuf[64];
  PCHAR                pcPos;

  WinSendDlgItemMsg( hwnd, IDSB_ATTEMPTS, SPBM_QUERYVALUE, MPFROMP( acBuf ),
                     MPFROM2SHORT( sizeof(acBuf), SPBQ_DONOTUPDATE ) );
  stData.ulAttempts = atol( acBuf );

  WinSendDlgItemMsg( hwnd, IDSB_BPP, SPBM_QUERYVALUE, MPFROMP( acBuf ),
                     MPFROM2SHORT( sizeof(acBuf), SPBQ_DONOTUPDATE ) );
  stData.ulBPP = atol( acBuf );

  stData.fRememberPswd = WinQueryButtonCheckstate( hwnd,
                                                   IDCB_REMEMBER_PSWD ) != 0;
  stData.fViewOnly = WinQueryButtonCheckstate( hwnd, IDCB_VIEW_ONLY ) != 0;

  stData.fShareDesktop =
                   WinQueryButtonCheckstate( hwnd, IDCB_SHARE_DESKTOP ) != 0;

  WinQueryDlgItemText( hwnd, IDEF_CHARSET, sizeof(acBuf), acBuf );
  pcPos = acBuf;
  STR_SKIP_SPACES( pcPos );
  STR_RTRIM( pcPos );
  strlcpy( stData.acCharset, pcPos, sizeof(stData.acCharset) );

  _selEncQuery( hwnd, sizeof(stData.acEncodings), stData.acEncodings );

  stData.ulCompressLevel =
    LONGFROMMR( WinSendDlgItemMsg( hwnd, IDSLID_COMPRESS, SLM_QUERYSLIDERINFO,
                                   MPFROM2SHORT( SMA_SLIDERARMPOSITION,
                                                 SMA_INCREMENTVALUE ), 0 ) );
  stData.ulQualityLevel =
    LONGFROMMR( WinSendDlgItemMsg( hwnd, IDSLID_QUALITY, SLM_QUERYSLIDERINFO,
                                   MPFROM2SHORT( SMA_SLIDERARMPOSITION,
                                                 SMA_INCREMENTVALUE ), 0 ) );

#if 0
  printf( "BPP: %d\n", stData.ulBPP );
  printf( "Remember password: %s\n", stData.fRememberPswd ? "YES" : "NO" );
  printf( "View only: %s\n", stData.fViewOnly ? "YES" : "NO" );
  printf( "Share desktop: %s\n", stData.fShareDesktop ? "YES" : "NO" );
  printf( "Encodings: %s\n", stData.acEncodings );
  printf( "Compress level: %u\n", stData.ulCompressLevel );
  printf( "Quality level: %u\n", stData.ulQualityLevel );
#endif

  WinSendMsg( pWinData->hwndRealOwner, WM_OPTDLG_ENTER, MPFROMP( &stData ),
              MPFROMLONG( WinQueryButtonCheckstate( hwnd,
                                                    IDCB_USE_AS_DEFAULT ) ) );

  return TRUE;
}

// Applies the user's data for dialog to dialog's controls.
static VOID _cmdUndo(HWND hwnd)
{
  PWINDATA             pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  POPTDLGDATA          pData;
  ULONG                ulIdx;
  HWND                 hwndLBSelEnc = WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS );
  PCHAR                pcEncodings, pcEncoding;
  ULONG                cbEncodings, cbEncoding;
  CHAR                 acEncoding[ENC_NAME_MAX_LEN];

  if ( ( pWinData == NULL ) || ( pWinData->pOptDlgData == NULL ) )
    return;

  pData = pWinData->pOptDlgData;

  WinSendDlgItemMsg( hwnd, IDSB_ATTEMPTS, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( pData->ulAttempts ), 0 );
  ulIdx = pData->ulBPP == 8 ? 0 : ( pData->ulBPP == 16 ? 1 : 2 );
  WinSendDlgItemMsg( hwnd, IDSB_BPP, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( ulIdx ), 0 );
  WinCheckButton( hwnd, IDCB_REMEMBER_PSWD, pData->fRememberPswd ? 1 : 0 );
  WinCheckButton( hwnd, IDCB_VIEW_ONLY, pData->fViewOnly ? 1 : 0 );
  WinCheckButton( hwnd, IDCB_SHARE_DESKTOP, pData->fShareDesktop ? 1 : 0 );
  WinSetDlgItemText( hwnd, IDEF_CHARSET, pData->acCharset );

  // Selected encodings.
  pcEncodings = pData->acEncodings;
  cbEncodings = strlen( pData->acEncodings );
  WinSendMsg( hwndLBSelEnc, LM_DELETEALL, 0, 0 );
  while( utilStrCutWord( &cbEncodings, &pcEncodings, &cbEncoding, &pcEncoding ) )
  {
    if ( utilStrWordIndex( pszEncodings, cbEncoding, pcEncoding ) == -1 )
      // Invalid encoding.
      continue;

    memcpy( acEncoding, pcEncoding, cbEncoding );
    acEncoding[cbEncoding] = '\0';
    WinSendMsg( hwndLBSelEnc, LM_INSERTITEM, MPFROMLONG(LIT_END),
                MPFROMP(acEncoding) );
  }

  // Available encodings (all not selected encodings).
  _availEncFill( hwnd, pData->acEncodings );

  WinSendDlgItemMsg( hwnd, IDSLID_COMPRESS, SLM_SETSLIDERINFO,
                     MPFROM2SHORT( SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE ),
                     MPFROMSHORT( pData->ulCompressLevel ) );
  WinSendDlgItemMsg( hwnd, IDSLID_QUALITY, SLM_SETSLIDERINFO,
                     MPFROM2SHORT( SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE ),
                     MPFROMSHORT( pData->ulQualityLevel ) );

  _selEncCheck( hwnd );
}

static VOID _wmTip(HWND hwnd, SHORT sCtlId, BOOL fTipReady)
{
  ULONG      ulMsgId;

  if ( !fTipReady )
    return;

  switch( sCtlId )
  {
    case IDLB_ALL_ENCODINGS:
      ulMsgId = IDM_TIP_ALL_ENCODINGS;
      break;
    case IDEF_CHARSET:
      ulMsgId = IDM_TIP_CHARSET;
      break;

    case IDLB_SEL_ENCODINGS:
      ulMsgId = IDM_TIP_SEL_ENCODINGS;
      break;

    case IDSLID_COMPRESS:
      ulMsgId = IDM_TIP_COMPRESS_LEVEL;
      break;

    case IDSLID_QUALITY:
      ulMsgId = IDM_TIP_QUALITY_LEVEL;
      break;

    default:
      return;
  }

  WinSendMsg( twQueryTipWinHandle(), WM_TIP_TEXT, 0, MPFROM2SHORT(ulMsgId,0) );
}

static VOID _wmLBCheckSelection(HWND hwnd, USHORT usCtrlId)
{
  HWND       hwndLB = WinWindowFromID( hwnd, usCtrlId );
  SHORT      sIdx = SHORT1FROMMR( WinSendMsg( hwndLB, LM_QUERYSELECTION,
                                              MPFROMSHORT(LIT_FIRST), 0 ) );
  SHORT      sCount;

  if ( usCtrlId == IDLB_ALL_ENCODINGS )
  {
    WinEnableWindow( WinWindowFromID( hwnd, IDPB_ENC_ADD ),
                     sIdx != LIT_NONE );
    return;
  }

  WinEnableWindow( WinWindowFromID( hwnd, IDPB_ENC_DELETE ),
                   sIdx != LIT_NONE );
  WinEnableWindow( WinWindowFromID( hwnd, IDPB_ENC_UP ),
                   ( sIdx != LIT_NONE ) && ( sIdx != 0 ) );
  sCount = SHORT1FROMMR( WinSendMsg( hwndLB, LM_QUERYITEMCOUNT, 0, 0 ) );
  WinEnableWindow( WinWindowFromID( hwnd, IDPB_ENC_DOWN ),
                   ( sIdx != LIT_NONE ) && ( sIdx != (sCount - 1) ) );
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmInitDlg( hwnd, (PDLGINITDATA)mp2 );

    case WM_DESTROY:
      _wmDestroy( hwnd );
      break;

    case WM_CLOSE:
      WinDestroyWindow( hwnd );
      return (MRESULT)FALSE;

    case WM_CONTROL:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDSB_ATTEMPTS:
          if ( SHORT2FROMMP( mp1 ) == SPBN_KILLFOCUS )
          {
            // Avoid manual typed invalid values for connection attempts.
            CHAR       acBuf[32];
            LONG       lValue;

            WinSendDlgItemMsg( hwnd, IDSB_ATTEMPTS, SPBM_QUERYVALUE,
                               MPFROMP( acBuf ), MPFROM2SHORT( sizeof(acBuf),
                                                          SPBQ_DONOTUPDATE ) );
            lValue = atol( acBuf );
            if ( lValue < 1 )
              lValue = 1;
            else if ( lValue > 10 )
              lValue = 10;
            else
              break;

            WinSendDlgItemMsg( hwnd, IDSB_ATTEMPTS, SPBM_SETCURRENTVALUE,
                               MPFROMLONG( lValue ), 0 );
          }
          break;
      }
      return (MRESULT)TRUE;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDPB_ENC_ADD:
          _cmdEncAdd( hwnd );
          break;

        case IDPB_ENC_DELETE:
          _cmdEncDelete( hwnd );
          break;

        case IDPB_ENC_UP:
          _cmdEncMove( hwnd, FALSE );
          break;

        case IDPB_ENC_DOWN:
          _cmdEncMove( hwnd, TRUE );
          break;

        case MBID_OK:
          if ( _cmdOk( hwnd ) )
            WinDestroyWindow( hwnd );
          break;

        case MBID_CANCEL:
          WinDestroyWindow( hwnd );
          break;

        case IDPB_UNDO:
          _cmdUndo( hwnd );
          break;
      }
      return (MRESULT)TRUE;

    case WM_SUBSTITUTESTRING:
      if ( SHORT1FROMMP(mp1) == 0 ) // For key %0
      {
        PWINDATA   pWinData = (PWINDATA)WinQueryWindowPtr( hwnd, QWL_USER );

        if ( pWinData->pszHost == NULL )
        {
          static CHAR  acBuf[32];

          WinLoadString( hab, 0, IDS_LISTENING, sizeof(acBuf), acBuf );
          return (MRESULT)acBuf;
        }

        return (MRESULT)pWinData->pszHost;
      }

    case WM_TIP:
      _wmTip( hwnd, SHORT1FROMMP(mp1), SHORT2FROMMP(mp1) != 0 );
      return (MRESULT)TRUE;

    case _WMLB_CHECK_SELECTION:
      _wmLBCheckSelection( hwnd, SHORT1FROMMP(mp1) );
      return (MRESULT)0;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


HWND optdlgOpen(HWND hwndOwner, PSZ pszHost, POPTDLGDATA pData)
{
  HWND         hwndDlg;
  DLGINITDATA  stInitData;

  stInitData.usSize        = sizeof(DLGINITDATA);
  stInitData.pData         = pData;
  stInitData.pszHost       = pszHost;
  stInitData.hwndRealOwner = hwndOwner;

  hwndDlg = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc,
                         NULLHANDLE, IDDLG_OPTIONS, &stInitData );
  if ( hwndDlg == NULLHANDLE )
  {
    debug( "WinLoadDlg() failed" );
    return NULLHANDLE;
  }

  twSet( hwndDlg );
  debugCP( "Ok, options dialog opened" );
  return hwndDlg;
}
