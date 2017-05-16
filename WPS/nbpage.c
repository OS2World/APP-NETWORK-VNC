#define INCL_WIN
#define INCL_ERRORS
#include <os2.h>

// Make access to member vars available.
#include <vncv.ih>

#include <vncvrc.h>
#include <utils.h>
#include <debug.h>

#define _ENC_NAME_MAX_LEN        12
#define _ENC_LIST_MAX_LEN        70

/* ****************************************************************** */

static BOOL _nbpServerInitDlg(HWND hwnd, MPARAM mp2)
{
  static PSZ           apszBPP[3] = { " 8", "16", "32" };
  vncv                 *somSelf = PVOIDFROMMP(mp2);
  vncvData             *somThis = somSelf == NULL ?
                                    NULL : vncvGetData( somSelf );

  // Store object to window pointer.
  WinSetWindowPtr( hwnd, QWL_USER, somSelf );

  WinSendDlgItemMsg( hwnd, IDSB_BPP, SPBM_SETARRAY,
                     MPFROMP( &apszBPP ), MPFROMLONG( 3 ) );

  WinSetFocus( HWND_DESKTOP, WinWindowFromID( hwnd, IDEF_HOSTDISPLAY ) );

  // Set state of controls according to the options.
  WinSendMsg( hwnd, WM_COMMAND, MPFROMLONG(IDPB_UNDO), 0 );

  if ( somThis != NULL )
    _hwndNotebookServerPage = hwnd;

  return TRUE;
}

static VOID _nbpServerUndo(HWND hwnd, vncv *somSelf)
{
  vncvData   *somThis = vncvGetData( somSelf );

  WinSetDlgItemText( hwnd, IDEF_HOSTDISPLAY,
                     _pszHostDisplay == NULL ? "" : _pszHostDisplay );
  WinSendDlgItemMsg( hwnd, IDSB_ATTEMPTS, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(_ulAttempts), MPFROMLONG(0) );
  WinSendDlgItemMsg( hwnd, IDSB_BPP, SPBM_SETCURRENTVALUE,
                     MPFROMLONG( _ulBPP == 8 ? 0
                                 : _ulBPP == 16 ? 1 : 2 ), 0 );
  WinSetDlgItemText( hwnd, IDEF_CHARSET, _acCharset );
  WinCheckButton( hwnd, IDCB_REMEMBER_PSWD, _fRememberPswd );
  WinCheckButton( hwnd, IDCB_VIEW_ONLY, _fViewOnly );
  WinCheckButton( hwnd, IDCB_SHARE_DESKTOP, _fShareDesktop );
  WinCheckButton( hwnd, IDCB_DYNAMICICON, _fDynamicIcon );
}

static VOID _nbpServerDefault(HWND hwnd, vncv *somSelf)
{
  WinSetDlgItemText( hwnd, IDEF_HOSTDISPLAY, "" );
  WinSendDlgItemMsg( hwnd, IDSB_ATTEMPTS, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(DEF_ATTEMPTS), MPFROMLONG(0) );
  WinSendDlgItemMsg( hwnd, IDSB_BPP, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(DEF_BPP), 0 );
  WinSetDlgItemText( hwnd, IDEF_CHARSET, "" );
  WinCheckButton( hwnd, IDCB_REMEMBER_PSWD, DEF_REMEMBER_PSWD );
  WinCheckButton( hwnd, IDCB_VIEW_ONLY, DEF_VIEW_ONLY );
  WinCheckButton( hwnd, IDCB_SHARE_DESKTOP, DEF_SHARE_DESKTOP );
  WinCheckButton( hwnd, IDCB_DYNAMICICON, DEF_DYNAMIC_ICON );
}

static VOID _nbpServerDestroy(HWND hwnd, vncv *somSelf)
{
  vncvData   *somThis = vncvGetData( somSelf );
  HWND       hwndCtl = WinWindowFromID( hwnd, IDEF_HOSTDISPLAY );
  ULONG      ulLength = WinQueryWindowTextLength( hwndCtl );
  CHAR       acBuf[32];

  // Store hostName:display
  if ( _pszHostDisplay != NULL )
    free( _pszHostDisplay );
  if ( ulLength == 0 )
    _pszHostDisplay = NULL;
  else
  {
    ulLength++;
    _pszHostDisplay = malloc( ulLength );
    if ( _pszHostDisplay != NULL )
    {
      WinQueryWindowText( hwndCtl, ulLength, _pszHostDisplay );
      utilStrTrim( _pszHostDisplay );
    }
  }

  if ( WinSendDlgItemMsg( hwnd, IDSB_ATTEMPTS, SPBM_QUERYVALUE,
              MPFROMP(acBuf), MPFROM2SHORT(sizeof(acBuf),SPBQ_DONOTUPDATE) ) )
    _ulAttempts = atol( acBuf ); 

  if ( WinSendDlgItemMsg( hwnd, IDSB_BPP, SPBM_QUERYVALUE,
               MPFROMP(acBuf), MPFROM2SHORT(sizeof(acBuf),SPBQ_DONOTUPDATE) ) )
  {
    switch( *((PUSHORT)&acBuf[0]) )
    {
      case (USHORT)'8 ': _ulBPP = 8;  break;         //  8
      case (USHORT)'61': _ulBPP = 16; break;         // 16
      case (USHORT)'23': _ulBPP = 32; break;         // 32
    };
  }

  _fRememberPswd   = WinQueryButtonCheckstate( hwnd, IDCB_REMEMBER_PSWD ) != 0;
  _fViewOnly       = WinQueryButtonCheckstate( hwnd, IDCB_VIEW_ONLY ) != 0;
  _fShareDesktop   = WinQueryButtonCheckstate( hwnd, IDCB_SHARE_DESKTOP ) != 0;
  WinQueryDlgItemText( hwnd, IDEF_CHARSET, sizeof(_acCharset), _acCharset );
  utilStrTrim( _acCharset );
  _vncvSetDynamicIcon( somSelf,
                     WinQueryButtonCheckstate( hwnd, IDCB_DYNAMICICON ) != 0 );

  _wpSaveDeferred( somSelf );
  _hwndNotebookServerPage = NULLHANDLE;
}

MRESULT EXPENTRY nbPageServer(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  vncv       *somSelf = WinQueryWindowPtr( hwnd, QWL_USER );
  vncvData   *somThis = somSelf == NULL ? NULL : vncvGetData( somSelf );

  switch( msg )
  {
    case WM_INITDLG:
      if ( _nbpServerInitDlg( hwnd, mp2 ) )
        return (MRESULT)FALSE;
      break;

    case WM_DESTROY:
      if ( somThis != NULL )
        _nbpServerDestroy( hwnd, somSelf );
      break;

    case WM_COMMAND:
      if ( somThis != NULL )
      switch( SHORT1FROMMP(mp1) )
      {
        case IDPB_UNDO:
          _nbpServerUndo( hwnd, somSelf );
          break;

        case IDPB_DEFAULT:
          _nbpServerDefault( hwnd, somSelf );
          break;
      }
      return (MRESULT)FALSE;

    case WM_CONTROL:
      if ( somThis != NULL )
      switch( SHORT1FROMMP(mp1) )
      {
        case IDCB_DYNAMICICON:
          // _vncvSetDynamicIcon() will set _fDynamicIcon and send
          // WM_NBSERVERPAGE_UPDDYNICONCB back to this dialog.
          // Our checkbox is not "auto". State of checkbox will be changed at
          // WM_NBSERVERPAGE_UPDDYNICONCB.
          _vncvSetDynamicIcon( somSelf, !_fDynamicIcon );
          break;
      }
      return (MRESULT)FALSE;

    case WM_NBSERVERPAGE_UPDDYNICONCB:
      WinCheckButton( hwnd, IDCB_DYNAMICICON, _fDynamicIcon );
      return (MRESULT)FALSE;
  } // switch( msg )

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}

/* ****************************************************************** */

#define _WMLB_CHECK_SELECTION    (WM_USER + 1)

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

static VOID _optdlgSetupSlider(HWND hwnd, ULONG ulId)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, ulId );
  ULONG      ulIdx;

  if ( hwndCtl == NULLHANDLE )
  {
    debug( "Slider #%u not exist", ulId );
    return;
  }

  // Shaft Height.
  WinSendMsg( hwndCtl, SLM_SETSLIDERINFO,
              MPFROM2SHORT( SMA_SHAFTDIMENSIONS, 0 ), MPFROMLONG( 12 ) );
  // Arm width and height.
  WinSendMsg( hwndCtl, SLM_SETSLIDERINFO,
              MPFROM2SHORT( SMA_SLIDERARMDIMENSIONS, 10 ),
              MPFROM2SHORT( 10, 20 ) );
  // Size of a tick mark.
  for( ulIdx = 0; ulIdx < 10; ulIdx += 1 )
    WinSendMsg( hwndCtl, SLM_SETTICKSIZE, MPFROM2SHORT( ulIdx, 5 ), 0 );
  // Text above a tick mark.
  WinSendMsg( hwndCtl, SLM_SETSCALETEXT, MPFROMSHORT( 1 ), MPFROMP( "Low" ) );
  WinSendMsg( hwndCtl, SLM_SETSCALETEXT, MPFROMSHORT( 8 ), MPFROMP( "High" ) );
}

// Fill "available encodings" list box (all not selected encodings).
static VOID _availEncFill(HWND hwnd, PSZ pszSelEncodings)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDLB_ALL_ENCODINGS );
  PCHAR      pcEncoding, pcEncodings = ALL_ENCODINGS;
  ULONG      cbEncoding, cbEncodings = strlen( ALL_ENCODINGS );
  CHAR       acEncoding[_ENC_NAME_MAX_LEN];

  WinSendMsg( hwndCtl, LM_DELETEALL, 0, 0 );

  while( utilStrCutWord( &cbEncodings, &pcEncodings, &cbEncoding, &pcEncoding ) )
  {
    if ( utilStrWordIndex( pszSelEncodings, cbEncoding, pcEncoding ) != -1 )
      // Encoding is selected.
      continue;

    memcpy( acEncoding, pcEncoding, cbEncoding );
    acEncoding[cbEncoding] = '\0';
    WinSendMsg( hwndCtl, LM_INSERTITEM, MPFROMLONG(LIT_END),
                MPFROMP(acEncoding) );
  }
}

// Read selected encodings into the buffer as ASCIIZ string.
static VOID _selEncQuery(HWND hwnd, ULONG cbBuf, PCHAR pcBuf)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS );
  SHORT      cbEncoding, sIdx = 0;

  while( cbBuf > 0 )  
  {
    if ( sIdx != 0 )
    {
      *pcBuf = ' ';
      pcBuf++;
      cbBuf--;
    }

    cbEncoding = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_QUERYITEMTEXT,
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
static VOID _selEncCheck(HWND hwnd)
{
  CHAR       acEncodings[_ENC_LIST_MAX_LEN];
  BOOL       fTight;

  _selEncQuery( hwnd, sizeof(acEncodings), acEncodings );
  fTight = utilStrWordIndex( acEncodings, 5, "Tight" ) != -1;

  WinEnableWindow( WinWindowFromID( hwnd, IDSLID_COMPRESS ),
                   fTight ||
                   ( utilStrWordIndex( acEncodings, 4, "Zlib" ) != -1 ) ||
                   ( utilStrWordIndex( acEncodings, 7, "ZlibHex" ) != -1 ) );
  WinEnableWindow( WinWindowFromID( hwnd, IDSLID_QUALITY ),
          fTight || ( utilStrWordIndex( acEncodings, 6, "ZYWRLE" ) != -1 ) );
}

static VOID _selEncSetup(HWND hwnd, PSZ pszEncodings)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS );
  PCHAR      pcEncoding, pcEncodings = pszEncodings;
  ULONG      cbEncoding, cbEncodings = strlen( pszEncodings );
  CHAR       acEncoding[_ENC_NAME_MAX_LEN];

  // Selected encodings.
  WinSendMsg( hwndCtl, LM_DELETEALL, 0, 0 );
  while( utilStrCutWord( &cbEncodings, &pcEncodings, &cbEncoding, &pcEncoding ) )
  {
    if ( utilStrWordIndex( ALL_ENCODINGS, cbEncoding, pcEncoding ) == -1 )
    {
      debug( "Invalid encoding: %s", debugBufPSZ( pcEncoding, cbEncoding ) );
      continue;
    }

    memcpy( acEncoding, pcEncoding, cbEncoding );
    acEncoding[cbEncoding] = '\0';
    WinSendMsg( hwndCtl, LM_INSERTITEM, MPFROMLONG(LIT_END),
                MPFROMP(acEncoding) );
  }

  // Available encodings (all not selected encodings).
  _availEncFill(/* somThis,*/ hwnd, pszEncodings );

  _selEncCheck( hwnd );
}


static BOOL _nbpEncodingsInitDlg(HWND hwnd, MPARAM mp2)
{
  vncv                 *somSelf = PVOIDFROMMP(mp2);

  // Store object to window pointer.
  WinSetWindowPtr( hwnd, QWL_USER, somSelf );

  _optdlgSetupSlider( hwnd, IDSLID_QUALITY );
  _optdlgSetupSlider( hwnd, IDSLID_COMPRESS );

  // Subclass function for listboxes to control selection changes.
  fnOldLBProc = WinSubclassWindow( WinWindowFromID( hwnd, IDLB_ALL_ENCODINGS ),
                                   _lbEncProc );
  WinSubclassWindow( WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS ),
                     _lbEncProc );

  // Set state of controls according to the options.
  WinSendMsg( hwnd, WM_COMMAND, MPFROMLONG(IDPB_UNDO), 0 );

  return TRUE;
}

static VOID _nbpEncodingsUndo(HWND hwnd, vncv *somSelf)
{
  vncvData   *somThis = vncvGetData( somSelf );

  _selEncSetup( hwnd, _pszEncodings );

  WinSendDlgItemMsg( hwnd, IDSLID_COMPRESS, SLM_SETSLIDERINFO,
                     MPFROM2SHORT( SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE ),
                     MPFROMSHORT( _ulCompressLevel ) );
  WinSendDlgItemMsg( hwnd, IDSLID_QUALITY, SLM_SETSLIDERINFO,
                     MPFROM2SHORT( SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE ),
                     MPFROMSHORT( _ulQualityLevel ) );
}

static VOID _nbpEncodingsDefault(HWND hwnd, vncv *somSelf)
{
  _selEncSetup( hwnd, DEF_ENCODINGS );

  WinSendDlgItemMsg( hwnd, IDSLID_COMPRESS, SLM_SETSLIDERINFO,
                     MPFROM2SHORT( SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE ),
                     MPFROMSHORT( DEF_COMPRESS_LEVEL ) );
  WinSendDlgItemMsg( hwnd, IDSLID_QUALITY, SLM_SETSLIDERINFO,
                     MPFROM2SHORT( SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE ),
                     MPFROMSHORT( DEF_QUALITY_LEVEL ) );
}

static VOID _nbpEncodingsDestroy(HWND hwnd, vncv *somSelf)
{
  vncvData   *somThis = vncvGetData( somSelf );
  CHAR       acEncodings[_ENC_LIST_MAX_LEN];

  _selEncQuery( hwnd, sizeof(acEncodings), acEncodings );
  if ( _pszEncodings != NULL )
    free( _pszEncodings );
  _pszEncodings = strdup( acEncodings );

  _ulCompressLevel =
    LONGFROMMR( WinSendDlgItemMsg( hwnd, IDSLID_COMPRESS, SLM_QUERYSLIDERINFO,
                                   MPFROM2SHORT( SMA_SLIDERARMPOSITION,
                                                 SMA_INCREMENTVALUE ), 0 ) );
  _ulQualityLevel =
    LONGFROMMR( WinSendDlgItemMsg( hwnd, IDSLID_QUALITY, SLM_QUERYSLIDERINFO,
                                   MPFROM2SHORT( SMA_SLIDERARMPOSITION,
                                                 SMA_INCREMENTVALUE ), 0 ) );

  _wpSaveDeferred( somSelf );
}

static VOID _cmdEncAdd(HWND hwnd)
{
  HWND       hwndLBAllEnc = WinWindowFromID( hwnd, IDLB_ALL_ENCODINGS );
  HWND       hwndLBSelEnc = WinWindowFromID( hwnd, IDLB_SEL_ENCODINGS );
  SHORT      sIdx, sInsIdx;
  CHAR       acEncoding[_ENC_NAME_MAX_LEN];

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
  CHAR       acEncodings[_ENC_LIST_MAX_LEN];

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
  CHAR       acEncoding[_ENC_NAME_MAX_LEN];

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

MRESULT EXPENTRY nbPageEncodings(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  vncv       *somSelf = WinQueryWindowPtr( hwnd, QWL_USER );
  vncvData   *somThis = somSelf == NULL ? NULL : vncvGetData( somSelf );

  switch( msg )
  {
    case WM_INITDLG:
      if ( _nbpEncodingsInitDlg( hwnd, mp2 ) )
        return (MRESULT)FALSE;
      break;

    case WM_DESTROY:
      if ( somThis != NULL )
        _nbpEncodingsDestroy( hwnd, somSelf );
      break;

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

        case IDPB_UNDO:
          _nbpEncodingsUndo( hwnd, somSelf );
          break;

        case IDPB_DEFAULT:
          _nbpEncodingsDefault( hwnd, somSelf );
          break;
      }
      return (MRESULT)FALSE;

    case WM_CONTROL:
/*      switch( SHORT1FROMMP(mp1) )
      {
        case IDD_CNT_SERIALDEVICES:
          break;
      }*/
      return (MRESULT)FALSE;

    case _WMLB_CHECK_SELECTION:
      _wmLBCheckSelection( hwnd, SHORT1FROMMP(mp1) );
      return (MRESULT)0;
  } // switch( msg )

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}
