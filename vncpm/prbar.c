#include <string.h>
#define INCL_GPI
#define INCL_WIN
#define INCL_WINDIALOGS
#define INCL_DOSERRORS
#define INCL_DOSMODULEMGR
#include <os2.h>
#include "prbar.h"
#include <debug.h>

typedef struct _PRBARDATA {
  HPS                  hpsMem;
  PSZ                  pszText;
  unsigned long long   ullTotal;
  unsigned long long   ullValue;
  ULONG                ulAnimationStep;
  LONG                 lStepOffs;
  ULONG                ulFillSize;
  ULONG                ulTimer;
  ULONG                ulImageIdx;
  ULONG                cBmp;
  HBITMAP              ahBmp[16];
} PRBARDATA, *PPRBARDATA;


// "Copy" font from one presentation space to another.
static BOOL _pbSetFontFromPS(HPS hpsDst, HPS hpsSrc, LONG llcid)
{
  FATTRS		stFAttrs;
  SIZEF			sizeCharBox; 
  FONTMETRICS		stFontMetrics;
  BOOL			fError;

  if ( llcid <= 0 ||
       !GpiQueryFontMetrics( hpsSrc, sizeof(FONTMETRICS), &stFontMetrics ) )
  {
    debugPCP( "GpiQueryFontMetrics() failed" );
    return FALSE;
  }

  stFAttrs.usRecordLength = sizeof(FATTRS);
  stFAttrs.fsSelection = stFontMetrics.fsSelection;
  stFAttrs.lMatch = stFontMetrics.lMatch;
  strcpy( stFAttrs.szFacename, stFontMetrics.szFacename );
  stFAttrs.idRegistry = 0;//stFontMetrics.idRegistry;
  stFAttrs.usCodePage = 0;//stFontMetrics.usCodePage;
  stFAttrs.lMaxBaselineExt = 0;//stFontMetrics.lMaxBaselineExt;
  stFAttrs.lAveCharWidth = 0;//stFontMetrics.lAveCharWidth;
  stFAttrs.fsType = 0;//FATTR_TYPE_KERNING;// | FATTR_TYPE_ANTIALIASED;//stFontMetrics.fsType;
  //->   stFAttrs.fsFontUse = FATTR_FONTUSE_OUTLINE;
  //stFAttrs.fsType = stFontMetrics.fsType;
  stFAttrs.fsFontUse = 0;//FATTR_FONTUSE_TRANSFORMABLE | FATTR_FONTUSE_NOMIX;
// FATTR_FONTUSE_OUTLINE FATTR_FONTUSE_TRANSFORMABLE

  GpiSetCharSet( hpsDst, LCID_DEFAULT );
  fError = GpiCreateLogFont( hpsDst, NULL, llcid, &stFAttrs ) == GPI_ERROR;
  if ( fError )
    debug( "GpiCreateLogFont() failed" );
  GpiSetCharSet( hpsDst, llcid );

  GpiQueryCharBox( hpsSrc, &sizeCharBox );
  fError = !GpiSetCharBox( hpsDst, &sizeCharBox );
  if ( fError )
    debug( "GpiSetCharBox() failed" );

  return fError;
}

static VOID _pbStartAnimation(HWND hwnd, PPRBARDATA pData, ULONG ulAnimation)
{
  HAB        hab = WinQueryAnchorBlock( hwnd );

  switch( ulAnimation )
  {
    case PRBARA_LEFT:     pData->lStepOffs = 1; break;
    case PRBARA_LEFTDBL:  pData->lStepOffs = 2; break;
    case PRBARA_RIGHT:    pData->lStepOffs = -1; break;
    case PRBARA_RIGHTDBL: pData->lStepOffs = -2; break;
    default: // PRBARA_STATIC
      if ( pData->ulTimer != 0 )
      {
        WinStopTimer( hab, hwnd, pData->ulTimer );
        pData->ulTimer = 0;
      }
      pData->lStepOffs = 0;
      return;
  }

  if ( pData->ulTimer == 0 )
    pData->ulTimer = WinStartTimer( hab, hwnd, 1, 20 );
}

static VOID _pbDrawBar(HWND hwnd, HPS hps, PPRBARDATA pData, BOOL fOnChanges)
{
  RECTL      rclBar, rclFill;
  ULONG      ulFillSize;
  HPS        hpsDraw, hpsWin;

  WinQueryWindowRect( hwnd, &rclBar );
  rclBar.xRight -= 4;            // Two points on each
  rclBar.yTop   -= 4;            // side for a frame.

  ulFillSize = (pData->ullValue >= pData->ullTotal) || (pData->ullTotal == 0)
                 ? rclBar.xRight
                 : ( (pData->ullValue * rclBar.xRight) / pData->ullTotal );
  if ( fOnChanges && ( pData->ulFillSize == ulFillSize ) )
    return;

  hpsWin = hps == NULLHANDLE ? WinGetPS( hwnd ) : hps;
  hpsDraw = pData->hpsMem != NULLHANDLE ? pData->hpsMem : hpsWin;
  rclFill = rclBar;

  // Filled left part of the bar.
  if ( pData->cBmp != 0 )
  {
    BITMAPINFOHEADER2 stBmpInf;
    HBITMAP  hBmp = pData->ahBmp[ pData->ulImageIdx % pData->cBmp ];
    HRGN     hrgn;
    POINTL   pt;

    rclFill.xRight = rclFill.xLeft + ulFillSize;

    hrgn = GpiCreateRegion( hpsDraw, 1, &rclFill );
    GpiSetClipRegion( hpsDraw, hrgn, NULL );

    stBmpInf.cbFix = sizeof(BITMAPINFOHEADER2);
    if ( GpiQueryBitmapInfoHeader( hBmp, &stBmpInf ) )
    {
      pt.x = -( (pData->ulAnimationStep) % stBmpInf.cx );
      pt.y = 0;
      do
      {
        WinDrawBitmap( hpsDraw, hBmp, NULL, &pt, 0, 0, DBM_NORMAL );
        pt.x += stBmpInf.cx;
      }
      while( pt.x < rclFill.xRight );
    }

    GpiSetClipRegion( hpsDraw, NULLHANDLE, NULL );
    GpiDestroyRegion( hpsDraw, hrgn );
  }

  // Not filled right part of the bar.
  if ( pData->pszText != NULL || !fOnChanges ||
       ( ulFillSize < pData->ulFillSize ) )
  {
    rclFill.xLeft = rclFill.xRight;
    rclFill.xRight = rclBar.xRight;
    WinFillRect( hpsDraw, &rclFill, SYSCLR_DIALOGBACKGROUND );
  }
  pData->ulFillSize = ulFillSize;

  // Window text.
  if ( pData->pszText != NULL )
  do
  {
    PSZ      pszText;
    ULONG    cbText;
    POINTL   aptlText[TXTBOX_COUNT];
    CHAR     acBuf[32];

    if ( *((PUSHORT)pData->pszText) == 0x0025 )
    {
      // Window text is '%' - make dynamic string like nnn%
      if ( ( pData->ullValue == ~0ULL ) ||      // Initial value.
           ( pData->ullTotal == 0 ) )
        break;

      cbText = sprintf( acBuf, "%u%%",
                        pData->ullValue >= pData->ullTotal ?
                          100 : ((pData->ullValue * 100) / pData->ullTotal) );
      pszText = acBuf;
    }
    else
    {
      // User defined window text.
      pszText = pData->pszText;
      cbText = strlen( pData->pszText );
    }

    GpiQueryTextBox( hpsDraw, cbText, pszText, 4, aptlText );
    aptlText[TXTBOX_BOTTOMLEFT].x = ( rclBar.xRight -
      ( aptlText[TXTBOX_BOTTOMLEFT].x + aptlText[TXTBOX_TOPRIGHT].x ) ) / 2 + 1;
    aptlText[TXTBOX_BOTTOMLEFT].y = ( rclBar.yTop -
      ( aptlText[TXTBOX_BOTTOMLEFT].y + aptlText[TXTBOX_TOPRIGHT].y ) ) / 2;

    // Draw 3D text.
    GpiSetColor( hpsDraw, CLR_WHITE );
    GpiCharStringAt( hpsDraw, &aptlText[TXTBOX_BOTTOMLEFT], cbText, pszText );

    aptlText[TXTBOX_BOTTOMLEFT].x -= 2;
    aptlText[TXTBOX_BOTTOMLEFT].y += 2;
    GpiSetColor( hpsDraw, CLR_DARKGRAY );
    GpiCharStringAt( hpsDraw, &aptlText[TXTBOX_BOTTOMLEFT], cbText, pszText );

    aptlText[TXTBOX_BOTTOMLEFT].x++;
    aptlText[TXTBOX_BOTTOMLEFT].y--;
    GpiSetColor( hpsDraw, CLR_BLACK );
    GpiCharStringAt( hpsDraw, &aptlText[TXTBOX_BOTTOMLEFT], cbText, pszText );
  }
  while( FALSE );

  // Copy prepeared image from memory PS to the window's PS.
  if ( pData->hpsMem != NULLHANDLE )
  {
    POINTL   aPoints[3];

    aPoints[0].x = 2;
    aPoints[0].y = 2;
    aPoints[1].x = 2 + rclBar.xRight;
    aPoints[1].y = 2 + rclBar.yTop;
    aPoints[2].x = 0;
    aPoints[2].y = 0;
    GpiBitBlt( hpsWin, hpsDraw, 3, aPoints, ROP_SRCCOPY, 0 );
  }

  if ( hps == NULLHANDLE )
    WinReleasePS( hpsWin );
}

static BOOL _wmpbCreate(HWND hwnd, PPRBARCDATA pCData, PCREATESTRUCT pCreateSt)
{
  PPRBARDATA pData, pDataCut;
  PCHAR      pcEndVal, pcPos = pCreateSt->pszText;
  ULONG      ulId;
  PHBITMAP   phBmp;
  HPS        hps;
  HAB        hab = WinQueryAnchorBlock( hwnd );
  HDC        hdcMem;

  pData = calloc( 1, sizeof(PRBARDATA) );
  if ( pData == NULL )
    return FALSE;
  phBmp = pData->ahBmp;

  hps = WinGetPS( hwnd );

  // Create a memory presentation space.

  hdcMem = DevOpenDC( hab, OD_MEMORY, "*", 0, NULL, hps );
  if ( hdcMem != NULLHANDLE )
  {
    SIZEL    size = { 0, 0 };
    HPS      hpsMem = GpiCreatePS( hab, hdcMem, &size,
                            PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC );
    if ( hpsMem == NULLHANDLE )
      DevCloseDC( hdcMem );
    else
    {
      RECTL                      rectl;
      BITMAPINFOHEADER2          bmi = { 0 };
      HBITMAP                    hbmMem;
      LONG                       alFormats[4];

      // Set bitmap with current window size to the memory presentation space

      // Request most closely formats of bitmaps matches the device.
      GpiQueryDeviceBitmapFormats( hps, 4, (PLONG)&alFormats );
      // Create a bitmap and set for the memory presentation space.
      bmi.cbFix      = sizeof(BITMAPINFOHEADER2);
      bmi.cPlanes    = (USHORT)alFormats[0];
      bmi.cBitCount  = (USHORT)alFormats[1];
      WinQueryWindowRect( hwnd, &rectl );
      bmi.cx         = rectl.xRight - rectl.xLeft; // Bitmap size equals the size
      bmi.cy         = rectl.yTop - rectl.yBottom; // of the window.

      hbmMem = GpiCreateBitmap( hpsMem, &bmi, 0, NULL, NULL );
      if ( ( hbmMem == NULLHANDLE ) || ( hbmMem == GPI_ERROR ) )
        GpiDestroyPS( hpsMem );
      else
      {
        hbmMem = GpiSetBitmap( hpsMem, hbmMem );

        // Destroy old bitmap.
        if ( hbmMem != NULLHANDLE )
          GpiDeleteBitmap( hbmMem );

        // Set font for memory PS same as font in window's PS.
        _pbSetFontFromPS( hpsMem, hps, 1 );

        pData->hpsMem = hpsMem;
      }
    } // if ( hpsMem == NULLHANDLE ) else
  } // if ( hdcMem != NULLHANDLE )

  // Load bitmaps listed in window text.
  while( ( *pcPos == '#' ) &&
         ( ((PCHAR)phBmp - (PCHAR)pData) < sizeof(PRBARDATA) ) )
  {
    pcPos++;
    ulId = strtoul( pcPos, &pcEndVal, 0 ); 
    if ( pcEndVal == pcPos )
      break;

    *phBmp = GpiLoadBitmap( hps, 0, ulId, 0, 0 );
    if ( *phBmp == NULLHANDLE )
      debug( "Cannot load bitmap #%u", ulId );
    phBmp++;
    pData->cBmp++;
    pcPos = pcEndVal;
  }

  if ( pData->cBmp == 0 )
  {
    HMODULE              hMod;
    ULONG                ulRC;

    ulRC = DosQueryModuleHandle( VNCPMDLLNAME, &hMod );
    if ( ulRC != NO_ERROR )
      debug( "DosQueryModuleHandle(), rc = %u", ulRC );
    else
    {
      *phBmp = GpiLoadBitmap( hps, hMod, IDBMP_PRBAR1, 0, 0 );
      phBmp++;
      *phBmp = GpiLoadBitmap( hps, hMod, IDBMP_PRBAR2, 0, 0 );
      phBmp++;
      pData->cBmp = 2;
    }
  }

  WinReleasePS( hps );

  pData->ullValue = ~0ULL;

  pDataCut = realloc( pData, (PCHAR)phBmp - (PCHAR)pData );
  if ( pDataCut == NULL )
  {
    free( pData );
    return NULL;
  }
  WinSetWindowPtr( hwnd, 0, pDataCut );

  // Set window text without bitmaps IDs.
  if ( *pcPos == '\t' )
    pcPos++;
  WinSetWindowText( hwnd, pcPos );

  if ( ( pCData != NULL ) && ( pCData->usSize == sizeof(PRBARCDATA) ) )
    _pbStartAnimation( hwnd, pDataCut, pCData->usAnimation );

  return TRUE;
}

static VOID _wmpbDestroy(HWND hwnd)
{
  PPRBARDATA pData = (PPRBARDATA)WinQueryWindowPtr( hwnd, 0 );
  ULONG      ulIdx;
  HDC        hdcMem;

  if ( pData != NULL )
  {
    if ( pData->pszText != NULL )
      free( pData->pszText );

    for( ulIdx = 0; ulIdx < pData->cBmp; ulIdx++ )
    {
      if ( pData->ahBmp[ulIdx] != NULLHANDLE )
        GpiDeleteBitmap( pData->ahBmp[ulIdx] ); 
    }

    if ( pData->hpsMem != NULLHANDLE )
    {
      // Destroy memory presentation space with bitmap.

      if ( !GpiDeleteBitmap( GpiSetBitmap( pData->hpsMem, NULLHANDLE ) ) )
        debugCP( "GpiDeleteBitmap() failed" );
      hdcMem = GpiQueryDevice( pData->hpsMem );
      if ( !GpiDestroyPS( pData->hpsMem ) )
        debugCP( "GpiDestroyPS() failed" );
      if ( DevCloseDC( hdcMem ) == DEV_ERROR )
        debugCP( "DevCloseDC() failed" );
    }

    free( pData );
  }
}

static BOOL _wmpbSetWindowParams(HWND hwnd, PWNDPARAMS pParams)
{
  PPRBARDATA pData = (PPRBARDATA)WinQueryWindowPtr( hwnd, 0 );
  BOOL       fResult = FALSE;

  if ( (pParams->fsStatus & WPM_TEXT) != 0 )
  {
    PSZ      pszText;

    if ( ( pParams->pszText != NULL ) && ( *pParams->pszText != '\0' ) )
    {
      pszText = strdup( pParams->pszText );

      if ( pszText != NULL )
        fResult = TRUE;
    }
    else
    {
      pszText = NULL;
      fResult = TRUE;
    }

    if ( fResult )
    {
      if ( pData->pszText != NULL )
        free( pData->pszText );
      pData->pszText = pszText;

      _pbDrawBar( hwnd, NULLHANDLE, pData, FALSE );
    }
  }

  return fResult;
}

static BOOL _wmpbQueryWindowParams(HWND hwnd, PWNDPARAMS pParams)
{
  PPRBARDATA pData = (PPRBARDATA)WinQueryWindowPtr( hwnd, 0 );
  BOOL       fResult = FALSE;

  if ( (pParams->fsStatus & (WPM_TEXT | WPM_CCHTEXT)) != 0 )
  {
    ULONG    cbText = pData->pszText == NULL ? 0 : strlen( pData->pszText );

    if ( (pParams->fsStatus & WPM_TEXT) == 0 )
      fResult = TRUE;
    else if ( cbText < pParams->cchText )
    {
      strcpy( pParams->pszText, pData->pszText );
      fResult = TRUE;
    }
    else
      fResult = FALSE;

    pParams->cchText = cbText;
  }

  return fResult;
}

static VOID _wmpbPaint(HWND hwnd)
{
  PPRBARDATA pData = (PPRBARDATA)WinQueryWindowPtr( hwnd, 0 );
  RECTL      rectl;
  HPS        hps;
  POINTL     pt;

  if ( pData == NULL )
    return;

  WinQueryWindowRect( hwnd, &rectl );

  hps = WinBeginPaint( hwnd, NULLHANDLE, NULL );

  // Draw frame.
  rectl.xRight--;
  rectl.yTop--;
  GpiSetColor( hps, SYSCLR_BUTTONDARK );
  pt.x = rectl.xRight;
  pt.y = rectl.yTop;
  GpiMove( hps, &pt );
  pt.y = 0;
  GpiLine( hps, &pt );
  pt.x = 0;
  GpiLine( hps, &pt );
  GpiSetColor( hps, SYSCLR_BUTTONLIGHT );
  pt.y = rectl.yTop;
  GpiLine( hps, &pt );
  pt.x = rectl.xRight - 1;
  GpiLine( hps, &pt );
  pt.y = 1;
  GpiLine( hps, &pt );
  pt.x = 1;
  GpiLine( hps, &pt );
  GpiSetColor( hps, SYSCLR_BUTTONDARK );
  pt.y = rectl.yTop - 1;
  GpiLine( hps, &pt );
  pt.x = rectl.xRight - 2;
  GpiLine( hps, &pt );

  _pbDrawBar( hwnd, hps, pData, FALSE );
  WinEndPaint( hps );
}

MRESULT EXPENTRY _wndProgressProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_CREATE:
      if ( !_wmpbCreate( hwnd, (PPRBARCDATA)mp1, (PCREATESTRUCT)mp2 ) )
        return (MRESULT)FALSE;
      break;

    case WM_DESTROY:
      _wmpbDestroy( hwnd );
      break;

    case WM_SETWINDOWPARAMS:
      return (MRESULT)_wmpbSetWindowParams( hwnd, (PWNDPARAMS)mp1 );

    case WM_QUERYWINDOWPARAMS:
      return (MRESULT)_wmpbQueryWindowParams( hwnd, (PWNDPARAMS)mp1 );

    case WM_PAINT:
      _wmpbPaint( hwnd );
      return (MRESULT)0;

    case WM_TIMER:
    {
      PPRBARDATA       pData = (PPRBARDATA)WinQueryWindowPtr( hwnd, 0 );

      if ( ( pData != NULL ) && ( SHORT1FROMMP(mp1) == pData->ulTimer ) )
      {
        pData->ulAnimationStep += pData->lStepOffs;
        WinInvalidateRect( hwnd, NULL, FALSE );
      }
      break;
    }

    case WM_PRESPARAMCHANGED:
      if ( LONGFROMMP(mp1) == PP_FONTNAMESIZE )
      {
        PPRBARDATA     pData = (PPRBARDATA)WinQueryWindowPtr( hwnd, 0 );

        if ( ( pData != NULL ) && ( pData->hpsMem != NULLHANDLE ) )
        {
          HPS  hps = WinGetPS( hwnd );

          _pbSetFontFromPS( pData->hpsMem, hps, 1 );
          WinReleasePS( hps );
        }
      }
      break;

    case PBM_SETPARAM:
    {
      PPRBARDATA       pData   = (PPRBARDATA)WinQueryWindowPtr( hwnd, 0 );
      ULONG            ulFlags = LONGFROMMP(mp1);
      PPRBARINFO       pInfo   = (PPRBARINFO)mp2;
      BOOL             fDrawOnChanges = ( (ulFlags & PBARSF_IMAGE) == 0 ) ||
                                    ( pData->ulImageIdx == pInfo->ulImageIdx );

      if ( !fDrawOnChanges )
        pData->ulImageIdx = pInfo->ulImageIdx;

      if ( (ulFlags & PBARSF_ANIMATION) != 0 )
        _pbStartAnimation( hwnd, pData, pInfo->ulAnimation );

      if ( (ulFlags & PBARSF_TOTAL) != 0 )
        pData->ullTotal = pInfo->ullTotal;

      if ( (ulFlags & PBARSF_VALUEINCR) != 0 )
      {
        if ( pData->ullValue == ~0ULL )              // Initial value.
          pData->ullValue = pInfo->ullValue;
        else
          pData->ullValue += pInfo->ullValue;
      }
      else if ( (ulFlags & PBARSF_VALUE) != 0 )
        pData->ullValue = pInfo->ullValue;

      _pbDrawBar( hwnd, NULLHANDLE, pData, fDrawOnChanges );
      return (MRESULT)0;
    }
  }

  return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}


VOID EXPENTRY prbarRegisterClass(HAB hab)
{
  WinRegisterClass( hab, WC_PROGRESSBAR, _wndProgressProc,
                    CS_SIZEREDRAW | CS_SYNCPAINT, sizeof(PVOID) );
}
