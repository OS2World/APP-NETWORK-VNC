#include <stdarg.h>
#include <sys\socket.h>
#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_DOSSEMAPHORES
#define INCL_DOSMISC
#define INCL_WIN
#define INCL_GPI
#define INCL_DOSNLS
#include <os2.h>
#include <rfb/rfbclient.h>
#include <ctype.h>
#include <os2xkey.h>
#include <utils.h>
#include "clntconn.h"
#include "clntwnd.h"
#include "resource.h"
#include "linkseq.h"
#include "filexfer.h"
#include "csconv.h"
#include <debug.h>

#define _WAIT_THREAD_TIME        4500

typedef struct _LOGREC {
  SEQOBJ               seqObj;
  time_t               timeRec;
  CHAR                 acText[1];
} LOGREC, *PLOGREC;

typedef struct _KEYSEQ {
  ULONG                ulCount;
  ULONG                ulIndex;
  ULONG                aulKeys[1];
} KEYSEQ, *PKEYSEQ;

typedef struct _CLNTCONN {
  SEQOBJ               seqObj;
  int                  argc;
  char                 *argv[16];
  CHAR                 acArg[256];
  int                  iRFBTid;
  HMTX                 hmtxRFB;
  HEV                  hevRFBState;
  ULONG                ulRFBState;
  rfbClient            *pRFBClient;
  PCCLOGONINFO         pLogonInfo;
  HWND                 hwnd;
  HPS                  hpsMem;
  HBITMAP              hbmMem;
  int                  iRFBButtons;
  HPOINTER             hptrPointer;
  PKEYSEQ              pRFBKeySeq;
  LINKSEQ              lsLog;
  XKFROMMP             stXKFromMP;
  ULONG                ulStopSigTime;
  HAB                  hab;
} CLNTCONN;

typedef struct _CHARTOKEYSYM {
  USHORT               usChar;
  ULONG                ulKeysym;
} CHARTOKEYSYM, *PCHARTOKEYSYM;

typedef unsigned long long       ULLONG;


static LINKSEQ         lsCCList;
static HMTX            hmtxCCList = NULLHANDLE;
static PXKBDMAP        pKbdMap = NULL;

static VOID _ccDestoryBitmap(PCLNTCONN pCC)
{
  if ( pCC->hpsMem != NULLHANDLE )
  {
    HDC      hdcMem = GpiQueryDevice( pCC->hpsMem );

    if ( pCC->hpsMem != NULLHANDLE )
    {
      if ( !GpiDestroyPS( pCC->hpsMem ) )
        debug( "GpiDestroyPS() failed" );
      pCC->hpsMem = NULLHANDLE;
    }

    if ( DevCloseDC( hdcMem ) == DEV_ERROR )
      debug( "DevCloseDC() failed" );
  }

  if ( pCC->hbmMem != NULLHANDLE )
  {
    if ( !GpiDeleteBitmap( pCC->hbmMem ) )
      debug( "GpiDeleteBitmap() failed" );
    pCC->hbmMem = NULLHANDLE;
  }
}

static VOID _conv8to16(PBYTE pbDst, PBYTE pbSrc, ULONG cPixels)
{
  register BYTE        bR, bG, bB;

  for( ; cPixels > 0; cPixels--, pbDst += 2, pbSrc++ )
  {
    bB = ((*pbSrc >> 6) & 0x03);
    bG = ((*pbSrc >> 3) & 0x07);
    bR = (*pbSrc & 0x07);

    *((PUSHORT)pbDst) = 
      ( ( bR << 13 ) | ( (bR & 6) << 10 ) |
      ( ( bB << 3 ) | ( bB << 1 ) | ( bB >> 1 ) ) |
      ( ( bG << 8 ) | ( bG << 5 ) ) );
  }
}

static VOID _conv8to32(PBYTE pbDst, PBYTE pbSrc, ULONG cPixels)
{
  register BYTE        bR, bG, bB;

  for( ; cPixels > 0; cPixels--, pbDst += 4, pbSrc++ )
  {
    bB = ((*pbSrc >> 6) & 0x03);
    bG = ((*pbSrc >> 3) & 0x07);
    bR = (*pbSrc & 0x07);

    *((PULONG)pbDst) = 
      ( ( bR << 21 ) | ( bR << 18 ) | ( (bR & 6) << 15 ) ) |
      ( ( bG << 13 ) | ( bG << 10 ) | ( (bG & 6) << 7 ) ) |
      ( ( bB << 6 ) | ( bB << 4 ) | ( bB << 2 ) | bB );
  }
}

static VOID _ccSetThreadState(PCLNTCONN pCC, ULONG ulState)
{
  debug( "New state: %u", ulState );
  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );
  pCC->ulRFBState = ulState;
  DosPostEventSem( pCC->hevRFBState );
  DosReleaseMutexSem( pCC->hmtxRFB );

  WinPostMsg( pCC->hwnd, WM_VNC_STATE, MPFROMP(pCC), MPFROMLONG(ulState) );
}


// libvncclient call back routines.
// --------------------------------

// Called from _cbRFBGetPassword() and _cbRFBGetCredential() to safety obtain
// logon information from CLNTCONN object.
static BOOL _logonInfoToThread(PCLNTCONN pCC, PCCLOGONINFO pLogonInfo,
                               BOOL fCredential)
{
  ULONG            ulRC;

  if ( pCC->pLogonInfo != NULL )
  {
    debugCP( "WTF?! We already have a logon information" );
    free( pCC->pLogonInfo );
    pCC->pLogonInfo = NULL;
  }

  _ccSetThreadState( pCC,
                       fCredential
                       ? RFBSTATE_WAITCREDENTIAL : RFBSTATE_WAITPASSWORD );
  DosResetEventSem( pCC->hevRFBState, &ulRC );
  debug( "Wait event semaphore, it will be posted when logon info. "
         "will be ready..." );
  ulRC = DosWaitEventSem( pCC->hevRFBState, SEM_INDEFINITE_WAIT );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosWaitEventSem(), rc = %u", ulRC );
    return FALSE;
  }
  debug( "Event semaphore posted, get logon information" );

  ulRC = pCC->pLogonInfo != NULL;
  if ( !ulRC )
    debug( "Logon information has not been set" );
  else if ( ( fCredential == TRUE ) != ( pLogonInfo->fCredential == TRUE ) )
  {
    debug( "Invalid logon informaton. Should be %s",
           pCC->ulRFBState == RFBSTATE_WAITCREDENTIAL ?
             "credential" : "VNC password" );
    ulRC = FALSE;
  }
  else
  {
    memcpy( pLogonInfo, pCC->pLogonInfo, sizeof(CCLOGONINFO) );
    memset( pCC->pLogonInfo, 0, sizeof(CCLOGONINFO) );
    free( pCC->pLogonInfo );
    pCC->pLogonInfo = NULL;
  }
  debug( "Back to state RFBSTATE_INITPROGRESS" );
  _ccSetThreadState( pCC, RFBSTATE_INITPROGRESS );

  return ulRC;
}

static rfbBool _cbRFBResize(rfbClient *pRFBClient)
{
  PCLNTCONN  pCC = (PCLNTCONN)rfbClientGetClientData( pRFBClient, NULL );
  ULLONG     ullAllocSize;
  ULONG      ulBmpBPP;

  switch( pRFBClient->format.bitsPerPixel )
  {
    case 8:
      {
        // libvncclient uses true-colors even for 8-bit bpp (indexed colors are
        // not supported). There are no 8-bit true-color bitmap formats. So, we
        // will use true-color bitmap, but with 16/32 bpp - what most closely
        // matches the local desktop (to speed up). 8-bit bpp frame buffer data
        // will be converted to 16/32 bpp bitmap at _cbRFBUpdate().
        LONG alFormats[2];
        HPS  hpsDesktop = WinGetScreenPS( HWND_DESKTOP );

        ulBmpBPP = 16; // 16 bpp for bitmap by default.
        if ( hpsDesktop != NULLHANDLE )
        {
          if ( GpiQueryDeviceBitmapFormats( hpsDesktop, 2, (PLONG)&alFormats )
               && ( alFormats[1] == 32 ) )
            // Ok, local destop is 32 bpp, use it for bitmap too.
            ulBmpBPP = 32;
          WinReleasePS( hpsDesktop );
        }

        pRFBClient->format.redShift     = 0;
        pRFBClient->format.greenShift   = 3;
        pRFBClient->format.blueShift    = 6;
        pRFBClient->format.redMax       = 0x07;
        pRFBClient->format.greenMax     = 0x07;
        pRFBClient->format.blueMax      = 0x03;
      }
      break;

    case 16:
      ulBmpBPP = 16;
      pRFBClient->format.redShift     = 11;
      pRFBClient->format.greenShift   = 5;
      pRFBClient->format.blueShift    = 0;
      pRFBClient->format.redMax       = 0x1F;
      pRFBClient->format.greenMax     = 0x3F;
      pRFBClient->format.blueMax      = 0x1F;
      break;

    default: // 32
      ulBmpBPP = 32;
      pRFBClient->format.bitsPerPixel = 32;
      pRFBClient->format.redShift     = 16;
      pRFBClient->format.greenShift   = 8;
      pRFBClient->format.blueShift    = 0;
      pRFBClient->format.redMax       = 0xFF;
      pRFBClient->format.greenMax     = 0xFF;
      pRFBClient->format.blueMax      = 0xFF;
      break;
  }
  
  if ( !SetFormatAndEncodings( pRFBClient ) )
  {
    debug( "SetFormatAndEncodings() failed" );
    return FALSE;
  }

  // Allocate frame buffer

  ullAllocSize = (ULLONG)pRFBClient->width * pRFBClient->height * (ulBmpBPP>>3);

  if ( ullAllocSize >= SIZE_MAX )
  {
    rfbClientErr( "CRITICAL: cannot allocate frameBuffer, "
                  "requested size is too large\n" );
    return FALSE;
  }

  // Avoid access to bitmap: ccGetHPS() will locks caller.
  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  if ( pRFBClient->frameBuffer != NULL )
    free( pRFBClient->frameBuffer );
  pRFBClient->frameBuffer = malloc( (size_t)ullAllocSize );

  if ( pRFBClient->frameBuffer == NULL )
  {
    DosReleaseMutexSem( pCC->hmtxRFB );
    rfbClientErr( "CRITICAL: frameBuffer allocation failed, "
                  "requested size too large or not enough memory?\n" );
    return FALSE;
  }

  // Create memory bitmap.

  _ccDestoryBitmap( pCC );
  do
  {
    SIZEL    size;
    HDC      hdcMem = DevOpenDC( pCC->hab, OD_MEMORY, "*", 0, NULL, NULLHANDLE );

    // Create a new memory presentation space.
    size.cx = pRFBClient->width;
    size.cy = pRFBClient->height;
    pCC->hpsMem = GpiCreatePS( pCC->hab, hdcMem, &size,
                               PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC );
    if ( pCC->hpsMem == NULLHANDLE )
      debug( "GpiCreatePS() failed. Memory PS was not created." );
    else
    {
      PBITMAPINFOHEADER2 pbmih = alloca( /*ulBmpBPP == 8 // - always 16 or 32
                                      ? sizeof(BITMAPINFO2) + (sizeof(RGB2) << 8)
                                      :*/ sizeof(BITMAPINFO2) );
      PBITMAPINFO2       pbmi = NULL;

      if ( pbmih == NULL )
        debug( "Not enough stack size" );
      else
      {
        // Create a system bitmap object
        memset( pbmih, 0, sizeof(BITMAPINFOHEADER2) );
        pbmih->cbFix           = sizeof(BITMAPINFOHEADER2);
        pbmih->cx              = pRFBClient->width;
        pbmih->cy              = pRFBClient->height;
        pbmih->cPlanes         = 1;
        pbmih->cBitCount       = ulBmpBPP;

        pCC->hbmMem = GpiCreateBitmap( pCC->hpsMem, pbmih, 0, NULL, pbmi );
        if ( ( pCC->hbmMem == GPI_ERROR ) || ( pCC->hbmMem == NULLHANDLE ) )
        {
          debug( "GpiCreateBitmap() failed: %u x %u, bpp: %u",
                 pbmih->cx, pbmih->cy, ulBmpBPP );
          pCC->hbmMem = NULLHANDLE;
        }
        // Set bitmap object for the memory presentation space.
        else if ( GpiSetBitmap( pCC->hpsMem, pCC->hbmMem ) == HBM_ERROR )
          debug( "GpiSetBitmap() failed" );
        else
          break; // Success.
      }
    }

    // Error.
    _ccDestoryBitmap( pCC );
    DosReleaseMutexSem( pCC->hmtxRFB );
    rfbClientErr( "CRITICAL: frame bitmap creation failed.\n" );
    return FALSE;
  }
  while( FALSE );

  DosReleaseMutexSem( pCC->hmtxRFB );

  // Set window size (the window may not be set at this time).

  if ( pCC->hwnd == NULLHANDLE )
    debug( "Window is not yet set" );
  else
    WinPostMsg( pCC->hwnd, WM_VNC_SETCLIENTSIZE,
                MPFROM2SHORT( pCC->pRFBClient->width,
                              pCC->pRFBClient->height ), 0 );

  return TRUE;
}

static void _cbRFBUpdate(rfbClient* pRFBClient, int x, int y, int w, int h)
{
  PCLNTCONN        pCC = (PCLNTCONN)rfbClientGetClientData( pRFBClient, NULL );
  struct {
    BITMAPINFOHEADER2  bmp2;
    RGB2               argb2Color[0x100];
  }                bm;
  ULONG            ulIdx;
  ULONG            cbLine = pRFBClient->width *
                            ( pRFBClient->format.bitsPerPixel >> 3 );
  PBYTE            pbData;
  LONG             lRC;
  HPS              hpsMem = ccGetHPS( pCC );
  LONG             lYTop;

  // Copy chaged lines from the frame buffer to the bitmap.

  memset( &bm, 0, sizeof(bm) );
  bm.bmp2.cbFix = sizeof(BITMAPINFOHEADER2);//16;
  if ( !GpiQueryBitmapInfoHeader( pCC->hbmMem, &bm.bmp2 ) )
  {
    debug( "GpiQueryBitmapInfoHeader() failed" );
    return;
  }

  pbData = (PBYTE)&pRFBClient->frameBuffer[y * cbLine];
  lYTop = bm.bmp2.cy - y - 1; // Y - top line relative bottom of bitmap.

  if ( pRFBClient->format.bitsPerPixel == 8 )
  {
    PBYTE    pbLine = malloc( ((bm.bmp2.cBitCount * bm.bmp2.cx) + 1) & ~0x01 );

    for( ulIdx = 0; ulIdx < h; ulIdx++, lYTop-- )
    {
      if ( bm.bmp2.cBitCount == 16 )
        _conv8to16( pbLine, pbData, bm.bmp2.cx );
      else // 32
        _conv8to32( pbLine, pbData, bm.bmp2.cx );

      lRC = GpiSetBitmapBits( pCC->hpsMem, lYTop, 1, pbLine,
                              (PBITMAPINFO2)&bm );
      if ( lRC == 0 || lRC == GPI_ALTERROR )
        debug( "GpiSetBitmapBits(,%u,,,) failed", lYTop );
      pbData += cbLine;
    }

    free( pbLine );
  }
  else
    for( ulIdx = 0; ulIdx < h; ulIdx++, lYTop-- )
    {
      lRC = GpiSetBitmapBits( pCC->hpsMem, lYTop, 1, pbData,
                              (PBITMAPINFO2)&bm );
      if ( lRC == 0 || lRC == GPI_ALTERROR )
        debug( "GpiSetBitmapBits(,%u,,,) failed", lYTop );
      pbData += cbLine;
    }

  ccReleaseHPS( pCC, hpsMem );

  if ( pCC->hwnd != NULLHANDLE )
  {
#if 0
    RECTL    rect;

    rect.xLeft = x;
    rect.yTop = bm.bmp2.cy - y;
    rect.xRight = rect.xLeft + w;
    rect.yBottom = rect.yTop - h;

    if ( !WinInvalidateRect( pCC->hwnd, &rect, FALSE ) )
      debug( "WinInvalidateRect() failed" );
#else
    SHORT sXLeft = x;
    SHORT sYTop = bm.bmp2.cy - y;
    SHORT sXRight = sXLeft + w;
    SHORT sYBottom = sYTop - h;

    WinPostMsg( pCC->hwnd, WM_VNC_UPDATE,
                MPFROM2SHORT(sXLeft, sYBottom), MPFROM2SHORT(sXRight, sYTop) );
#endif
  }
  else
    debugPCP( "No window to send WM_VNC_UPDATE." );
}

static void _cbRFBKeyboardLedState(rfbClient* cl, int value, int pad)
{
  /* note: pad is for future expansion 0=unused */
  debugPCP();
}

static void _cbRFBTextChat(rfbClient* pRFBClient, int value, char *text)
{
  PCLNTCONN        pCC = (PCLNTCONN)rfbClientGetClientData( pRFBClient, NULL );

  if ( pCC->hwnd == NULLHANDLE )
  {
    debug( "Window is not yet set" );
    return;
  }

  switch( value )
  {
    case rfbTextChatOpen:
      debug( "TextChat: We should open a textchat window" );
      WinSendMsg( pCC->hwnd, WM_VNC_CHAT, MPFROMLONG(CCCHAT_OPEN), 0 );
      TextChatOpen( pRFBClient );
      break;

    case rfbTextChatClose:
    case rfbTextChatFinished:
      debug( "TextChat: We should close our window" );
      WinSendMsg( pCC->hwnd, WM_VNC_CHAT, MPFROMLONG(CCCHAT_CLOSE), 0 );
      break;

    default:
      {
        PSZ      pszEncText = cscConvStr( pCC->pRFBClient, CSC_REMOTETOLOCAL,
                                          text );

        debug( "TextChat: Received \"%s\" (\"%s\")", text, pszEncText );

        WinSendMsg( pCC->hwnd, WM_VNC_CHAT, MPFROMLONG(CCCHAT_MESSAGE),
                    MPFROMP(pszEncText == NULL ? text : pszEncText) );

         if ( pszEncText != NULL )
           cscFree( pszEncText );
      }
      break;
  }
}

static char* _cbRFBGetPassword(rfbClient *pRFBClient)
{
  PCLNTCONN        pCC = (PCLNTCONN)rfbClientGetClientData( pRFBClient, NULL );
  CCLOGONINFO      stLogonInfo;
  PSZ              pszPassword;

  if ( !_logonInfoToThread( pCC, &stLogonInfo, FALSE ) )
    return NULL;

#ifdef DEBUG_CODE
// We should use "original" strdup() and malloc() functions. Memory allocated
// here for the password will be freed in libvncclient library (it's so ugly!).
// DEBUG_CODE was defined in debug.h.
#undef strdup
#undef malloc
#endif
  if ( stLogonInfo.acPassword[0] == '\0' )
  {
    // We can get a blank password. Function HandleVncAuth() in
    // libvncclient\rfbproto.c has appropriate changes.
    pszPassword = malloc( 1 );
    pszPassword[0] = '\0';
  }
  else
    pszPassword = strdup( stLogonInfo.acPassword );
#ifdef DEBUG_CODE
// Continue to use our debug-related routines...
#define strdup(src) debug_strdup(src, __FILE__, __LINE__)
#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#endif
  memset( &stLogonInfo, 0, sizeof(CCLOGONINFO) );

  return pszPassword;
}

static rfbCredential* _cbRFBGetCredential(rfbClient *pRFBClient,
                                         int credentialType)
{
  PCLNTCONN        pCC = (PCLNTCONN)rfbClientGetClientData( pRFBClient, NULL );
  CCLOGONINFO      stLogonInfo;
  rfbCredential    *pRFBCred;

  if ( credentialType != rfbCredentialTypeUser )
  {
    debug( "Unsupported logon information type requested" );
    return NULL;
  }

  if ( !_logonInfoToThread( pCC, &stLogonInfo, FALSE ) )
    return NULL;

#ifdef DEBUG_CODE
#undef malloc
#undef strdup
#endif
  pRFBCred = malloc( sizeof(rfbCredential) );
  pRFBCred->userCredential.username = strdup( stLogonInfo.acUserName );
  pRFBCred->userCredential.password = strdup( stLogonInfo.acPassword );
#ifdef DEBUG_CODE
#define malloc(size) debug_malloc(size, __FILE__, __LINE__)
#define strdup(src) debug_strdup(src, __FILE__, __LINE__)
#endif
  memset( &stLogonInfo, 0, sizeof(CCLOGONINFO) );

  return pRFBCred;
}

// We support cursors for 16 and 32 BPP. It seems, libvncclient don't give me
// color cursor image for 8 BPP.
static void cbGotCursorShape(rfbClient *pRFBClient, int xhot, int yhot,
                             int width, int height, int bytesPerPixel)
{
  PCLNTCONN            pCC = (PCLNTCONN)rfbClientGetClientData( pRFBClient, NULL );
  ULONG                ulBPP;
  PBITMAPINFOHEADER2   pbmih;
  PBITMAPINFO2         pbmi = NULL;
  PBYTE                pbImage;
  PBYTE                pbLine;
  ULONG                cbSrcLine, cbDstLine;
  PBYTE                pbInMask = (PBYTE)&pRFBClient->rcMask[ (width - 1) *
                                                              height ];
                                  // Last line in source mask.
  PBYTE                pbInImage, pbInMaskLine;
  HPS                  hps;
  register ULONG       ulPos;
  ULONG                ulLine;
  POINTERINFO          ptri = { 0 };

  if ( pCC->pRFBClient->appData.viewOnly )
    // Do not set cursor in view-only mode.
    return;

  pbmih = alloca( sizeof(BITMAPINFO2) + (2*sizeof(RGB2)) );
  if ( pbmih == NULL )
  {
    debug( "Not enough stack size" );
    return;
  }

  memset( pbmih, 0, sizeof(BITMAPINFOHEADER2) );
  pbmi           = (PBITMAPINFO2)pbmih;

  // Create cursor image bitmap.
  // -------------------------

/*  if ( bytesPerPixel == 1 )
    ulBPP = 16;
  else*/
    ulBPP = bytesPerPixel << 3;

  cbSrcLine = bytesPerPixel * width;
  cbDstLine = ( (ulBPP * width + 31) / 32 ) * 4;

  pbmih->cbImage = cbDstLine * height;

  // Buffer to store converted data for the system bitmap.
  pbImage = alloca( pbmih->cbImage );
  if ( pbImage == NULL )
  {
    debug( "Not enough stack size" );
    return;
  }

  pbInImage = (PBYTE)&pRFBClient->rcSource[pbmih->cbImage - cbSrcLine]; // Last line in source.
  pbInMaskLine = pbInMask; // Last line in source mask.

  for( ulLine = 0, pbLine = pbImage; ulLine < height;
       ulLine++, pbLine += cbDstLine,
       pbInImage -= cbSrcLine, pbInMaskLine -= width )
  {
/*    if ( bytesPerPixel == 1 )
      _conv8to16( pbLine, pbInMaskLine, width );
    else*/

    if ( bytesPerPixel == 2 )
    {
      for( ulPos = 0; ulPos < width; ulPos++ )
        ((PUSHORT)pbLine)[ulPos] = pbInMaskLine[ulPos] == 0
                                          ? 0 : ((PUSHORT)pbInImage)[ulPos];
    }
    else
    {
      for( ulPos = 0; ulPos < width; ulPos++ )
        ((PULONG)pbLine)[ulPos] = pbInMaskLine[ulPos] == 0
                                          ? 0 : ((PULONG)pbInImage)[ulPos];
    }

    // Padding.
    memset( &pbLine[cbSrcLine], 0, cbDstLine - cbSrcLine );
  }

  // Create a system bitmap object
  pbmih->cbFix     = sizeof(BITMAPINFOHEADER2);
  pbmih->cx        = width;
  pbmih->cy        = height;
  pbmih->cPlanes   = 1;
  pbmih->cBitCount = ulBPP;

  hps = WinGetPS( pCC->hwnd );
  ptri.hbmColor = GpiCreateBitmap( hps, pbmih, CBM_INIT, pbImage, pbmi );

  if ( ( ptri.hbmColor == GPI_ERROR ) || ( ptri.hbmColor == 0 ) )
  {
    debug( "GpiCreateBitmap() failed" );
    WinReleasePS( hps );
    return;
  }

  // Create cursor mask bitmap
  // -------------------------

  // AND mask.
  cbDstLine = ( (width + 31) / 32 ) * 4;
  memset( pbImage, 0x00, height * cbDstLine  );

  // XOR mask, convert from input.
  pbLine = &pbImage[height * cbDstLine];
  for( ulLine = 0, pbInMaskLine = pbInMask; ulLine < height;
       ulLine++, pbLine += cbDstLine, pbInMaskLine -= width )
  {
    memset( pbLine, 0, cbDstLine );
    for( ulPos = 0; ulPos < width; ulPos++ )
    {
      if ( pbInMaskLine[ulPos] == 0 )
        pbLine[ulPos >> 3] |= 0x80 >> (ulPos & 7);
    }
  }

  *((PULONG)&pbmi->argbColor[0]) = 0x00000000;
  *((PULONG)&pbmi->argbColor[1]) = 0x00FFFFFF;

  // Create a system bitmap object
  pbmih->cy        = 2 * height; // Doubhe height: invert mask and source mask.
  pbmih->cBitCount = 1;
  pbmih->cbImage   = cbDstLine * height;

  ptri.hbmPointer = GpiCreateBitmap( hps, pbmih, CBM_INIT, pbImage, pbmi );
  WinReleasePS( hps );

  if ( ( ptri.hbmPointer == GPI_ERROR ) || ( ptri.hbmPointer == 0 ) )
  {
    debug( "GpiCreateBitmap() failed" );
    return;
  }

  // Create system pointer
  // ---------------------

  ptri.fPointer = TRUE;
  ptri.xHotspot = xhot;
  ptri.yHotspot = height - yhot - 1;

  if ( pCC->hptrPointer != NULLHANDLE )
    WinDestroyPointer( pCC->hptrPointer );

  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  pCC->hptrPointer = WinCreatePointerIndirect( HWND_DESKTOP, &ptri );
  if ( pCC->hptrPointer == NULLHANDLE )
    debug( "WinCreatePointerIndirect() failed" );

  DosReleaseMutexSem( pCC->hmtxRFB );

  // Destroy bitmaps.
  GpiDeleteBitmap( ptri.hbmPointer );
  GpiDeleteBitmap( ptri.hbmColor );
}

static void cbGotXCutText(rfbClient *pRFBClient, const char *pszText, int cbText)
{
  PCLNTCONN            pCC = (PCLNTCONN)rfbClientGetClientData( pRFBClient, NULL );
/*  PSZ                  pszClipboard;
  ULONG                ulRC;
  BOOL                 fSuccess;*/

  if ( ( pszText == NULL ) || ( pCC->hwnd == NULLHANDLE ) )
    return;

  WinSendMsg( pCC->hwnd, WM_VNC_CLIPBOARD, MPFROMLONG(cbText), MPFROMP(pszText) );
/*
  ulRC = DosAllocSharedMem( (PPVOID)&pszClipboard, 0, cbText + 1,
                            PAG_COMMIT | PAG_READ | PAG_WRITE |
                            OBJ_GIVEABLE | OBJ_GETTABLE | OBJ_TILE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosAllocSharedMem() failed, rc = %u", ulRC );
    return;
  }

  strcpy( pszClipboard, pszText );

  if ( !WinOpenClipbrd( hab ) )
  {
    debug( "WinOpenClipbrd() failed" );
    fSuccess = FALSE;
  }
  else
  {    
    WinEmptyClipbrd( hab );

printf( "-1- %s\n", pszClipboard );
    fSuccess = WinSetClipbrdData( hab, (ULONG)pszClipboard, CF_TEXT,
                                  CFI_POINTER );
    if ( !fSuccess )
      debug( "WinOpenClipbrd() failed" );

debugPCP();
    WinCloseClipbrd( hab );
debugPCP();
  }

  if ( !fSuccess )
    DosFreeMem( pszClipboard );

  WinSetClipbrdOwner( hab, pCC->hwnd );
*/
}

static void cbBell(rfbClient *pRFBClient)
{
  WinAlarm( HWND_DESKTOP, WA_NOTE );
}

// Thread.

static void threadClient(void *pArg)
{
  PCLNTCONN            pCC = (PCLNTCONN)pArg;
  int                  iRC;
  BOOL                 fInit;
  HMQ                  hmq;

  _ccSetThreadState( pCC, RFBSTATE_INITPROGRESS );

  // PM stuff initialization.
  pCC->hab = WinInitialize( 0 );
  hmq = WinCreateMsgQueue( pCC->hab, 0 );
  if ( hmq == NULLHANDLE )
  {
    debug( "WinCreateMsgQueue() failed" );
    _ccSetThreadState( pCC, RFBSTATE_FINISH );
    _endthread();
  }

  fInit = rfbInitClient( pCC->pRFBClient, &pCC->argc, pCC->argv );

  if ( !fInit )
  {
    debug( "rfbInitClient() failed" );
    // RFB-client object pCC->pRFBClient destroyed by rfbInitClient().
    pCC->pRFBClient = NULL;
  }
  else if ( pCC->ulStopSigTime == 0 )
  {
    rfbClientLog( "VNC client format: %u bits per pixel.\n",
                  pCC->pRFBClient->format.bitsPerPixel );

    _ccSetThreadState( pCC, RFBSTATE_READY );

    while( pCC->ulStopSigTime == 0 )
    {
      iRC = fxWaitForMessage( pCC->pRFBClient, 50000 );
      if ( iRC < 0 )
      {
        debug( "WaitForMessage(), rc = %d", iRC );
        break;
      }

//      DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

#if 1
      // WaitForMessage() can block thread when data is buffered, see
      // libvncclient\sockets.c ReadFromRFBServer(). We use cycle with
      // checking the availability of buffered data to process all messages.
      if ( iRC != 0 )
      {
        while( HandleRFBServerMessage( pCC->pRFBClient ) &&
               pCC->pRFBClient->buffered && ( pCC->ulStopSigTime == 0 ) )
          { }

        if ( recv( pCC->pRFBClient->sock, &iRC, 1, MSG_PEEK ) == 0 )
        {
          debugPCP( "Connection closed" );
          break;
        }
      }
#else
      if ( ( iRC != 0 ) && !HandleRFBServerMessage( pCC->pRFBClient ) )
      {
        debug( "HandleRFBServerMessage() returns FALSE" );
        DosReleaseMutexSem( pCC->hmtxRFB );
        _ccSetThreadState( pCC, RFBSTATE_ERROR );
        break;
      }
#endif

      if ( pCC->pRFBKeySeq != NULL )
      {
        ULONG          ulIndex = pCC->pRFBKeySeq->ulIndex;

        if ( ulIndex == pCC->pRFBKeySeq->ulCount )
        {
          free( pCC->pRFBKeySeq );
          pCC->pRFBKeySeq = NULL;
        }
        else
        {
          ULONG        ulKey = pCC->pRFBKeySeq->aulKeys[ulIndex];

          ccSendKeyEvent( pCC, ulKey & ~CCKEY_PRESS, (ulKey & CCKEY_PRESS)!=0 );
          pCC->pRFBKeySeq->ulIndex++;
        }
      }
//      DosReleaseMutexSem( pCC->hmtxRFB );
    }

    debug( "End thread cycle..." );
  }

  _ccSetThreadState( pCC, RFBSTATE_FINISH );
  WinDestroyMsgQueue( hmq );
  debugCP( "End thread" );

  _endthread();
}

// Global log handle.

static void _cbLibLog(const char *format, ...)
{
  PTIB       tib;
  PPIB       pib;
  PCLNTCONN  pCC;
  va_list    args;
  int        iRC;
  CHAR       acBuf[256];

  va_start(args, format);
  iRC = _vsnprintf( acBuf, sizeof(acBuf), format, args );
  va_end(args);

  if ( iRC == -1 )
    iRC = sizeof(acBuf);
  printf( acBuf );

  DosGetInfoBlocks( &tib, &pib );
  DosRequestMutexSem( hmtxCCList, SEM_INDEFINITE_WAIT );

  // Search CLNTCONN.

  for( pCC = (PCLNTCONN)lnkseqGetFirst( &lsCCList ); pCC != NULL;
       pCC = (PCLNTCONN)lnkseqGetNext( pCC ) )
  {
    if ( pCC->iRFBTid == tib->tib_ptib2->tib2_ultid )
      break;
  }

  if ( pCC == NULL )
    debug( "Thread for the new log record is not found. Text: %s", acBuf );
  else
  {
    // Create a log record.

    PLOGREC  pLogRec = malloc( sizeof(LOGREC) + iRC );

    if ( pLogRec != NULL )
    {
      time( &pLogRec->timeRec );
      memcpy( pLogRec->acText, acBuf, iRC );
      pLogRec->acText[iRC] = '\0';

      DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );
      lnkseqAddFirst( &pCC->lsLog, pLogRec );
      DosReleaseMutexSem( pCC->hmtxRFB );

      debug( "Log record stored: %s", acBuf );
    }
  }

  DosReleaseMutexSem( hmtxCCList );
}

static VOID _cbKbdMapLoadErr(PSZ pszFile, ULONG ulLine, ULONG ulCode)
{
  PSZ        pszMsg;

  switch( ulCode )
  {
    case XKEYERR_KEYSYM:
      pszMsg = "Invalid keysym code";
      break;

    case XKEYERR_CHAR:
      pszMsg = "Invalid characted code";
      break;

    case XKEYERR_FLAG:
      pszMsg = "Unknown flag(s)";
      break;

    case XKEYERR_SCAN:
      pszMsg = "Invalid scan code";
      break;

    case XKEYERR_VK:
      pszMsg = "Unknown virtual key name";
      break;

    default:
      printf( "Unknown error code 0x%X\n", ulCode );
      return;
  }

  printf( "%s at line %u in file %s\n", pszMsg, ulLine, pszFile );
}

// Public routines.

ULONG ccInit()
{
  ULONG      ulRC;

  if ( hmtxCCList != NULLHANDLE )
  {
    debug( "Already initialized" );
    return CC_INITIALIZED;
  }

  ulRC = DosCreateMutexSem( NULL, &hmtxCCList, 0, FALSE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosCreateMutexSem(), rc = %u", ulRC );
    return CC_SEMERROR;
  }

  pKbdMap = xkMapLoad( "keysym\\user.xk & keysym\\%L.xk & keysym\\general.xk",
                       _cbKbdMapLoadErr );
  if ( pKbdMap == NULL )
  {
    DosCloseMutexSem( hmtxCCList );
    hmtxCCList = NULLHANDLE;
    return CC_NOKBDMAP;
  }

  lnkseqInit( &lsCCList );

  rfbClientLog = _cbLibLog;
  rfbClientErr = _cbLibLog;

  fxRegister();

  return CC_OK;
}

// Destroy all connection objects and windows.
VOID ccDone()
{
  HWND       hwnd;

  debugCP();

  if ( hmtxCCList == NULLHANDLE )
  {
    debug( "Not initialized" );
    return;
  }

  if ( lnkseqGetCount( &lsCCList ) != 0 )
  {
    debug( "%u objects was not destroyed, do it now...",
           lnkseqGetCount( &lsCCList ) );
  }

  while( lnkseqGetFirst( &lsCCList ) != NULL )
  {
    hwnd = ((PCLNTCONN)lnkseqGetFirst( &lsCCList ))->hwnd;

    ccDestroy( (PCLNTCONN)lnkseqGetFirst( &lsCCList ) );
    if ( hwnd != NULLHANDLE )
      WinDestroyWindow( hwnd );
  }

  DosCloseMutexSem( hmtxCCList );
  hmtxCCList = NULLHANDLE;

  xkMapFree( pKbdMap );
}

PCLNTCONN ccCreate(PCCPROPERTIES pProperties, HWND hwnd)
{
  ULONG                ulRC;
  PCLNTCONN            pCC = calloc( 1, sizeof(CLNTCONN) );
  PCHAR                pcArg;

  if ( pCC == NULL )
    return NULL;

  if ( !pProperties->fViewOnly )
  {
    pCC->hptrPointer = WinLoadPointer( HWND_DESKTOP, NULLHANDLE, IDPTR_DEFPTR );
    if ( pCC->hptrPointer == NULLHANDLE )
      debug( "WinLoadPointer() failed" );
  }

  pCC->hwnd = hwnd;
  lnkseqInit( &pCC->lsLog );
  xkKeysymFromMPStart( &pCC->stXKFromMP );
  pCC->iRFBTid = -1;

  do
  {
    ulRC = DosCreateMutexSem( NULL, &pCC->hmtxRFB, 0, FALSE );
    if ( ulRC != NO_ERROR )
    {
      debug( "DosCreateMutexSem(), rc = %u", ulRC );
      break;
    }

    ulRC = DosCreateEventSem( NULL, &pCC->hevRFBState, DCE_AUTORESET, FALSE );
    if ( ulRC != NO_ERROR )
    {
      debug( "DosCreateEventSem(), rc = %u", ulRC );
      break;
    }

    switch( pProperties->ulBPP )
    {
      case 8:
        pCC->pRFBClient = rfbGetClient( 8, 1, 1 ); // 8 BPP
        break;

      case 16:
        pCC->pRFBClient = rfbGetClient( 8, 2, 2 ); // 16 BPP
        break;

      default:
        pCC->pRFBClient = rfbGetClient( 8, 3, 4 ); // 32 BPP
    }

    if ( pCC->pRFBClient == NULL )
    {
      debug( "rfbGetClient() - result is NULL" );
      break;
    }

    pCC->pRFBClient->MallocFrameBuffer       = _cbRFBResize;
    pCC->pRFBClient->canHandleNewFBSize      = TRUE;
    pCC->pRFBClient->GotFrameBufferUpdate    = _cbRFBUpdate;
    pCC->pRFBClient->HandleKeyboardLedState  = _cbRFBKeyboardLedState;
    pCC->pRFBClient->HandleTextChat          = _cbRFBTextChat;
    pCC->pRFBClient->listenPort              = pProperties->lListenPort;
    pCC->pRFBClient->listen6Port             = pProperties->lListenPort;
    pCC->pRFBClient->GetPassword             = _cbRFBGetPassword;
    pCC->pRFBClient->GetCredential           = _cbRFBGetCredential;
    pCC->pRFBClient->GotCursorShape          = cbGotCursorShape;
    pCC->pRFBClient->GotXCutText             = cbGotXCutText;
    pCC->pRFBClient->Bell                    = cbBell;
    pCC->pRFBClient->appData.useBGR233       = TRUE;
    // We don't support cursor for 8 BPP.
    pCC->pRFBClient->appData.useRemoteCursor = pProperties->ulBPP != 8;
    pCC->pRFBClient->appData.viewOnly        = pProperties->fViewOnly;
    pCC->pRFBClient->appData.shareDesktop    = pProperties->fShareDesktop;

    if ( pProperties->lListenPort >= 0 )
    {
      // "Listen mode", set local interface.
      pCC->pRFBClient->listenAddress = pProperties->acHost[0] == '\0' ?
                                         NULL : strdup( pProperties->acHost );
    }

    rfbClientSetClientData( pCC->pRFBClient, NULL, pCC );

    if ( !cscInit( pCC->pRFBClient, pProperties->acCharset ) )
      rfbClientLog( "Can not open character encoding: %s\n",
                    pProperties->acCharset );

    // Arguments for the thread.

    pCC->argc = 1;
    pCC->argv[0] = pCC->acArg;
    pcArg = pCC->acArg + sprintf( pCC->acArg, "VNCViewer" ) + 1;

    if ( pProperties->acEncodings[0] != '\0' )
    {
      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "-encodings" ) + 1;

      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      strcpy( pcArg, pProperties->acEncodings );
      pcArg += strlen( pProperties->acEncodings ) + 1;
    }

    if ( pProperties->ulCompressLevel <= 9 )
    {
      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "-compress" ) + 1;

      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "%u", pProperties->ulCompressLevel ) + 1;
    }

    if ( pProperties->ulQualityLevel <= 9 )
    {
      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "-quality" ) + 1;

      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "%u", pProperties->ulQualityLevel ) + 1;
    }

    if ( pProperties->ulQoS_DSCP != 0 )
    {
      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "-qosdscp" ) + 1;

      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "%u", pProperties->ulQoS_DSCP ) + 1;
    }

    if ( pProperties->acDestHost[0] != '\0' )
    {
      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "-repeaterdest" ) + 1;

      pCC->argv[pCC->argc] = pcArg;
      pCC->argc++;
      pcArg += sprintf( pcArg, "%s", pProperties->acDestHost ) + 1;
    }

    pCC->argv[pCC->argc] = pcArg;
    pCC->argc++;

    if ( pProperties->lListenPort < 0 )
      pcArg += sprintf( pcArg, "%s", pProperties->acHost ) + 1;
    else
      pcArg += sprintf( pcArg, "-listennofork" ) + 1;

#ifdef DEBUG_FILE
    {
      ULONG  ulIdx;

      for( ulIdx = 0; ulIdx < pCC->argc; ulIdx++ )
        debug( "Arg %u: %s", ulIdx, pCC->argv[ulIdx] );
    }
#endif

    // Run thread.
    pCC->ulRFBState = RFBSTATE_NOTINIT;
    pCC->iRFBTid = _beginthread( threadClient, NULL, 65535, pCC );
    if ( pCC->iRFBTid == -1 )
    {
      debug( "_beginthread() failed" );
      break;
    }

    // Store pointer to the new object at the list of objests.
    debugCP( "Store pointer to the new object at the list of objests" );
    ulRC = DosRequestMutexSem( hmtxCCList, SEM_INDEFINITE_WAIT );
    if ( ulRC != NO_ERROR )
      debug( "DosRequestMutexSem(), rc = %u", ulRC );
    lnkseqAdd( &lsCCList, pCC );
    debug( "Client connection objects: %u", lnkseqGetCount( &lsCCList ) );
    DosReleaseMutexSem( hmtxCCList );

    // Let thread to read data from stThreadData...
    ccWaitState( pCC, RFBSTATE_NOTINIT, FALSE );

    // Success.
    return pCC;
  }
  while( FALSE );

  // Client connection object creation failed,

  if ( pCC != NULL )
  {
    if ( pCC->hmtxRFB != NULLHANDLE )
      DosCloseMutexSem( pCC->hmtxRFB );

    if ( pCC->hevRFBState != NULLHANDLE )
      DosCloseEventSem( pCC->hevRFBState );

    if ( pCC->pRFBClient != NULL )
      rfbClientCleanup( pCC->pRFBClient );

    free( pCC );
  }

  return NULL;
}

VOID ccDestroy(PCLNTCONN pCC)
{
  ULONG      ulRC;

  debugCP( "Enter" );

  // Remove pointer to the object from the list of objests.
  ulRC = DosRequestMutexSem( hmtxCCList, SEM_INDEFINITE_WAIT );
  if ( ulRC != NO_ERROR )
    debug( "DosRequestMutexSem(), rc = %u", ulRC );
  lnkseqRemove( &lsCCList, pCC );
  DosReleaseMutexSem( hmtxCCList );

  if ( pCC->iRFBTid == -1 )
    debug( "No thread for the object" );
  else if ( ccDisconnectSignal( pCC ) )
  {
    debugCP( "Wait for thread" );
    ulRC = DosWaitEventSem( pCC->hevRFBState, 150 );
    if ( ulRC != NO_ERROR )
      debug( "DosWaitEventSem(), rc = %u", ulRC );

    if ( pCC->ulRFBState != RFBSTATE_FINISH )
    {
      debug( "Thread is not finished, kill it..." );
      ulRC = DosKillThread( pCC->iRFBTid );
      if ( ulRC != NO_ERROR )
        debug( "DosKillThread(), rc = %u", ulRC );
    }

    debugCP( "Thread ended, continue destruction..." );
  }

  if ( pCC->pRFBClient != NULL )
  {
    cscDone( pCC->pRFBClient );

    if ( pCC->pRFBClient->listenAddress != NULL )
      free( pCC->pRFBClient->listenAddress );

    if ( pCC->pRFBClient->frameBuffer != NULL )
      free( pCC->pRFBClient->frameBuffer );

    fxClean( pCC->pRFBClient );
    rfbClientCleanup( pCC->pRFBClient );
  }

  lnkseqFree( &pCC->lsLog, PLOGREC, free );

  _ccDestoryBitmap( pCC );

  if ( pCC->pLogonInfo != NULL )
    free( pCC->pLogonInfo );

  if ( pCC->hptrPointer != NULLHANDLE )
    WinDestroyPointer( pCC->hptrPointer );

  if ( pCC->hmtxRFB != NULLHANDLE )
    DosCloseMutexSem( pCC->hmtxRFB );

  if ( pCC->hevRFBState != NULLHANDLE )
    DosCloseEventSem( pCC->hevRFBState );

  if ( pCC->pRFBKeySeq != NULL )
    free( pCC->pRFBKeySeq );

  free( pCC );
}

ULONG ccWaitState(PCLNTCONN pCC, ULONG ulState, BOOL fEqual)
{
  ULONG      ulRC = DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  if ( ulRC != NO_ERROR )
  {
    debug( "DosRequestMutexSem(), rc = %u", ulRC );
    return pCC->ulRFBState;
  }

  debug( "Wait for state %s %u", fEqual ? "==" : "!=", ulState );

  while( ( fEqual == TRUE ) != ( pCC->ulRFBState == ulState ) )
  {
    DosReleaseMutexSem( pCC->hmtxRFB );
    ulRC = DosWaitEventSem( pCC->hevRFBState, 50 );
    DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );
  }
  ulState = pCC->ulRFBState;

  DosReleaseMutexSem( pCC->hmtxRFB );
  debug( "State is %u", ulState );
  return ulState;
}

HWND ccSetWindow(PCLNTCONN pCC, HWND hwnd)
{
  HWND       hwndOld = pCC->hwnd;
/*  ULONG      ulRC = DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  if ( ulRC != NO_ERROR )
    debug( "DosRequestMutexSem(), rc = %u", ulRC );
*/
  if ( hwnd != NULLHANDLE )
    pCC->hwnd = hwnd;
/*  DosReleaseMutexSem( pCC->hmtxRFB );*/

  return hwndOld;
}

BOOL ccSetViewOnly(PCLNTCONN pCC, ULONG ulViewOnly)
{
  BOOL       fViewOnly;

  switch( ulViewOnly )
  {
    case CCVO_OFF:
      fViewOnly = FALSE;
      break;

    case CCVO_ON:
      fViewOnly = TRUE;
      break;

    default:  // CCVO_TOGGLE:
      pCC->pRFBClient->appData.viewOnly = !pCC->pRFBClient->appData.viewOnly;
      return TRUE;
  }

  if ( !pCC->pRFBClient->appData.viewOnly == !fViewOnly )
    return FALSE;

  pCC->pRFBClient->appData.viewOnly = fViewOnly;

  return TRUE;
}

/*
  BOOL ccIsRFBMsgSupported(PCLNTCONN pCC, BOOL fClient2Server, ULONG ulFRBMsg)

  Returns TRUE if RFB message type ulFRBMsg is supported. Defines for ulFRBMsg
  (see ..\libvncserver\rfb\rfbproto.h):

  fClient2Server is TRUE:
    rfbSetPixelFormat           rfbFixColourMapEntries
    rfbSetEncodings             rfbFramebufferUpdateRequest
    rfbKeyEvent                 rfbPointerEvent
    rfbClientCutText            rfbFileTransfer
    rfbSetScale                 rfbSetServerInput
    rfbSetSW                    rfbTextChat
    rfbPalmVNCSetScaleFactor    rfbXvp

  fClient2Server is FALSE:
    rfbFramebufferUpdate        rfbSetColourMapEntries
    rfbBell                     rfbServerCutText
    rfbResizeFrameBuffer        rfbPalmVNCReSizeFrameBuffer
*/
BOOL ccIsRFBMsgSupported(PCLNTCONN pCC, BOOL fClient2Server, ULONG ulFRBMsg)
{
  return fClient2Server ?
           SupportsClient2Server( pCC->pRFBClient, ulFRBMsg ) :
           SupportsServer2Client( pCC->pRFBClient, ulFRBMsg );
}

// Returns TRUE if current state is RFBSTATE_READY. Ensures that state will
// not change until ccUnlockReadyState().
BOOL ccLockReadyState(PCLNTCONN pCC)
{
  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );
  if ( pCC->ulRFBState != RFBSTATE_READY )
  {
    DosReleaseMutexSem( pCC->hmtxRFB );
    return FALSE;
  }

  return TRUE;
}

// Unlocks state change from RFBSTATE_READY to another.
VOID ccUnlockReadyState(PCLNTCONN pCC)
{
  DosReleaseMutexSem( pCC->hmtxRFB );
}

// Sends command to the thread to end session.
// Returns FALSE when CLNTCONN object already disconnected.
BOOL ccDisconnectSignal(PCLNTCONN pCC)
{
  BOOL       fDisconnectInProgress;

  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );
  fDisconnectInProgress = pCC->ulRFBState != RFBSTATE_FINISH;
  if ( fDisconnectInProgress )
  {
    DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &pCC->ulStopSigTime,
                     sizeof(ULONG) );
    if ( pCC->ulStopSigTime == 0 )  // Avoid zero value.
      pCC->ulStopSigTime++;
    debug( "Set disconnect signal at %u msec.", pCC->ulStopSigTime );
  }
  DosReleaseMutexSem( pCC->hmtxRFB );

  return fDisconnectInProgress;
}

HPS ccGetHPS(PCLNTCONN pCC)
{
  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );
  return pCC->hpsMem;
}

VOID ccReleaseHPS(PCLNTCONN pCC, HPS hps)
{
  if ( hps != pCC->hpsMem )
  {
    debug( "Invalid HPS" );
    return;
  }

  DosReleaseMutexSem( pCC->hmtxRFB );
}

BOOL ccQueryFrameSize(PCLNTCONN pCC, PSIZEL psizeFrame)
{
  if ( !ccLockReadyState( pCC ) )
    return FALSE;

  psizeFrame->cx = pCC->pRFBClient->width;
  psizeFrame->cy = pCC->pRFBClient->height;

  ccUnlockReadyState( pCC );
  return TRUE;
}

BOOL ccQueryViewOnly(PCLNTCONN pCC)
{
  return pCC->pRFBClient->appData.viewOnly;
}

HPOINTER ccQueryPointer(PCLNTCONN pCC)
{
  return pCC->pRFBClient->appData.viewOnly || ( pCC->hptrPointer == NULLHANDLE )
           ? WinQuerySysPointer( HWND_DESKTOP, SPTR_ARROW, FALSE )
           : pCC->hptrPointer;
}

// Writes in pcBuf information specified by ulItem from session log.
// Returns TRUE on success (information found).
BOOL ccQuerySessionInfo(PCLNTCONN pCC, ULONG ulItem, ULONG cbBuf, PCHAR pcBuf)
{
  PLOGREC    pLogRec;
  BOOL       fSuccess = FALSE;

  if ( ulItem > CCSI_SERVER_HOST )
    return FALSE;

  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  pLogRec = (PLOGREC)lnkseqGetFirst( &pCC->lsLog );

  switch( ulItem )
  {
    case CCSI_LAST_LOG_REC:
      if ( pLogRec != NULL )
      {
        strlcpy( pcBuf, pLogRec->acText, cbBuf );
        fSuccess = TRUE;
      }
      break;

    case CCSI_SERVER_HOST:
      strlcpy( pcBuf, pCC->pRFBClient->serverHost, cbBuf );
      fSuccess = TRUE;
      break;

    default:
    {
      PSZ      pszTarget;
      ULONG    cbTarget, cbLogText;

      switch( ulItem )
      {
        case CCSI_DESKTOP_NAME:
          pszTarget = "Desktop name \"";
          break;
      }
      cbTarget = strlen( pszTarget );

      // Look for log record starts with pszTarget.
      for( ; pLogRec != NULL; pLogRec = (PLOGREC)lnkseqGetNext( pLogRec ) )
      {
        cbLogText = strlen( pLogRec->acText );
        if ( ( cbLogText >= cbTarget ) &&
             ( memcmp( pLogRec->acText, pszTarget, cbTarget ) == 0 ) )
          break;
      }

      if ( cbBuf != 0 )
      {
        if ( pLogRec != NULL )
        {
          // Log record found.
          switch( ulItem )
          {
            case CCSI_DESKTOP_NAME:
              // Get desktop name from log record <Desktop name "..."\n>
              pszTarget = &pLogRec->acText[cbTarget];
              cbTarget = strlen( pszTarget );
              if ( cbTarget > 0 ) // Remove last <\n>.
                cbTarget--;
              if ( cbTarget > 0 ) // Remove last <">.
                cbTarget--;
              if ( cbTarget >= cbBuf )
                cbTarget = cbBuf - 1;
              memcpy( pcBuf, pszTarget, cbTarget );
              pcBuf[cbTarget] = '\0';
              fSuccess = TRUE;
              break;
          }
        } // if ( pLogRec != NULL )
        else
        {
          switch( ulItem )
          {
            case CCSI_DESKTOP_NAME:
              {
                // Desktop name is not found in log. Build host:display string.
                ULONG      ulDisplay = pCC->pRFBClient->serverPort;

                // Get display from port number.
                // See display to port conv. in vncviewer.c/rfbInitClient().
                if ( ulDisplay >= 5900 && ulDisplay < (5900+5900) )
                  ulDisplay -= 5900;

                if ( _snprintf( pcBuf, cbBuf, "%s:%u",
                                pCC->pRFBClient->serverHost, ulDisplay ) == -1 )
                  pcBuf[cbBuf - 1] = '\0';
                fSuccess = TRUE;
                break;
              }
          }
        } // if ( pLogRec != NULL ) else
      } // if ( cbBuf != 0 )
    }
  } // switch

  DosReleaseMutexSem( pCC->hmtxRFB );

  if ( !fSuccess && ( cbBuf != 0 ) )
    *pcBuf = '\0';

  return fSuccess;
}

// The password or credential shoul be requested before calling this function,
// i.e. we must have state RFBSTATE_WAITPASSWORD (pszUserName nor uses) or
// RFBSTATE_WAITCREDENTIAL.
// Returns FALSE if state is not RFBSTATE_WAITPASSWORD/RFBSTATE_WAITCREDENTIAL
// or invalid type (pLogonInfo->fCredential) of logon information.
BOOL ccSendLogonInfo(PCLNTCONN pCC, PCCLOGONINFO pLogonInfo)
{
  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );
  if ( ( pCC->ulRFBState != RFBSTATE_WAITPASSWORD ) &&
       ( pCC->ulRFBState != RFBSTATE_WAITCREDENTIAL ) )
  {
    debugCP( "Password or credential has not been requested" );
    DosReleaseMutexSem( pCC->hmtxRFB );
    return FALSE;
  }

  if ( ( pCC->ulRFBState == RFBSTATE_WAITCREDENTIAL ) !=
       ( pLogonInfo->fCredential == TRUE ) )
  {
    debug( "Invalid logon informaton. Should be %s",
           pCC->ulRFBState == RFBSTATE_WAITCREDENTIAL ?
             "credential" : "VNC password" );
    DosReleaseMutexSem( pCC->hmtxRFB );
    return FALSE;
  }

  if ( pCC->pLogonInfo != NULL )
  {
    debugCP( "WTF?! Logon information already been set" );
    free( pCC->pLogonInfo );
  }
  pCC->pLogonInfo = malloc( sizeof(CCLOGONINFO) );

  if ( pCC->pLogonInfo != NULL )
    memcpy( pCC->pLogonInfo, pLogonInfo, sizeof(CCLOGONINFO) );

  debug( "Post event semaphore to inform thread that name and password can be readed..." );
  DosPostEventSem( pCC->hevRFBState );

  DosReleaseMutexSem( pCC->hmtxRFB );

  return TRUE;
}

VOID ccSendMouseEvent(PCLNTCONN pCC, LONG lX, LONG lY, ULONG ulButton)
{
  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  if ( !pCC->pRFBClient->appData.viewOnly &&
       ( pCC->ulRFBState == RFBSTATE_READY ) )
  {
    if ( ulButton != 0 )
    {
      int   iRFBButton;

      switch( ulButton & ~RFBBUTTON_PRESSED )
      {
        case RFBBUTTON_LEFT:       iRFBButton = rfbButton1Mask; break;
        case RFBBUTTON_RIGHT:      iRFBButton = rfbButton3Mask; break;
        case RFBBUTTON_MIDDLE:     iRFBButton = rfbButton2Mask; break;
        case RFBBUTTON_WHEEL_DOWN: iRFBButton = rfbButton5Mask; break;
//        case RFBBUTTON_WHEEL_UP:
        default:                   iRFBButton = rfbButton4Mask; break;
      }

      if ( (ulButton & RFBBUTTON_PRESSED) != 0 )
        pCC->iRFBButtons |= iRFBButton;
      else
        pCC->iRFBButtons &= ~iRFBButton;
    }

    lY = pCC->pRFBClient->height - lY - 1;
    SendPointerEvent( pCC->pRFBClient, lX, lY, pCC->iRFBButtons );

    // Only "pressed" events for Wheel.
    if ( (pCC->iRFBButtons & (rfbButton4Mask | rfbButton5Mask)) != 0 )
    {
      pCC->iRFBButtons &= ~(rfbButton4Mask | rfbButton5Mask);
      SendPointerEvent( pCC->pRFBClient, lX, lY, pCC->iRFBButtons );
    }
  }

  DosReleaseMutexSem( pCC->hmtxRFB );
}

VOID ccSendKeyEvent(PCLNTCONN pCC, ULONG ulKey, BOOL fDown)
{
  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  if ( !pCC->pRFBClient->appData.viewOnly &&
       ( pCC->ulRFBState == RFBSTATE_READY ) )
    SendKeyEvent( pCC->pRFBClient, ulKey, fDown );

  DosReleaseMutexSem( pCC->hmtxRFB );
}

VOID ccSendWMCharEvent(PCLNTCONN pCC, MPARAM mp1, MPARAM mp2)
{
  ULONG      ulIdx;

  if ( mp1 == 0
         ? xkKeysymFromMPCheck( &pCC->stXKFromMP )
         : ( xkKeysymFromMP( pKbdMap, mp1, mp2, &pCC->stXKFromMP ) !=
              XKEYMETHOD_NOTFOUND )
     )
  {
    for( ulIdx = 0; ulIdx < pCC->stXKFromMP.cOutput; ulIdx++ )
      ccSendKeyEvent( pCC, pCC->stXKFromMP.aOutput[ulIdx].ulKeysym,
                      pCC->stXKFromMP.aOutput[ulIdx].fPressed );
  }
}

// Sends XK-key codes. For pressed state code must be ORed with CCKEY_PRESS.
VOID ccSendKeySequence(PCLNTCONN pCC, ULONG cKeySeq, PULONG paulKeySeq)
{
  PKEYSEQ    pRFBKeySeq;

  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  if ( !pCC->pRFBClient->appData.viewOnly )
  {
    if ( pCC->pRFBKeySeq == NULL )
      pRFBKeySeq = calloc( 1, sizeof(KEYSEQ) - sizeof(ULONG) +
                           ( cKeySeq * sizeof(ULONG) ) );
    else
    {
      debugPCP( "Previous key sequence was not sent fully" );

      pRFBKeySeq = realloc( pCC->pRFBKeySeq,
                            sizeof(KEYSEQ) - sizeof(ULONG) +
                            ( (pCC->pRFBKeySeq->ulCount + cKeySeq) * sizeof(ULONG) ) );
    }

    if ( pRFBKeySeq != NULL )
    {
      memcpy( &pRFBKeySeq->aulKeys[pRFBKeySeq->ulCount], paulKeySeq,
              cKeySeq * sizeof(ULONG) );
      pRFBKeySeq->ulCount += cKeySeq;
      pCC->pRFBKeySeq = pRFBKeySeq;
    }
  }

  DosReleaseMutexSem( pCC->hmtxRFB );
}

VOID ccSendClipboardText(PCLNTCONN pCC, PSZ pszText)
{
  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  if ( !pCC->pRFBClient->appData.viewOnly &&
       ( pCC->ulRFBState == RFBSTATE_READY ) && ( pszText != NULL ) )
    SendClientCutText( pCC->pRFBClient, pszText, strlen( pszText ) );

  DosReleaseMutexSem( pCC->hmtxRFB );
}

BOOL ccSendChat(PCLNTCONN pCC, ULONG ulCmd, PSZ pszText)
{
  BOOL       fSuccess = FALSE;

  DosRequestMutexSem( pCC->hmtxRFB, SEM_INDEFINITE_WAIT );

  if ( pCC->ulRFBState == RFBSTATE_READY )
  {
    switch( ulCmd )
    {
      case CCCHAT_OPEN:
        pCC->pRFBClient->supportedMessages.client2server[((rfbTextChat & 0xFF)/8)]
          |= (1<<(rfbTextChat % 8));
        fSuccess = TextChatOpen( pCC->pRFBClient );
        break;

      case CCCHAT_CLOSE:
        fSuccess = TextChatClose( pCC->pRFBClient ) &&
                   TextChatFinish( pCC->pRFBClient );
        break;

      case CCCHAT_MESSAGE:
        {
          PSZ      pszEncText = cscConvStr( pCC->pRFBClient, CSC_LOCALTOREMOTE,
                                            pszText );

          fSuccess = TextChatSend( pCC->pRFBClient,
                                   pszEncText == NULL ? pszText : pszEncText );
          if ( pszEncText != NULL )
            cscFree( pszEncText );
        }
        break;
    }
  }

  DosReleaseMutexSem( pCC->hmtxRFB );

  return fSuccess;
}

BOOL ccFXRequestFileList(PCLNTCONN pCC, PSZ pszPath)
{
  return fxFileListRequest( pCC->pRFBClient, pszPath );
}

// Start file receiving.
BOOL ccFXRecvFile(PCLNTCONN pCC, PSZ pszRemoteName, PSZ pszLocalName)
{
  return fxRecvFile( pCC->pRFBClient, pszRemoteName, pszLocalName );
}

BOOL ccFXAbortFileTransfer(PCLNTCONN pCC)
{
  return fxAbortFileTransfer( pCC->pRFBClient );
}

BOOL ccFXDelete(PCLNTCONN pCC, PSZ pszRemoteName)
{
  return fxDelete( pCC->pRFBClient, pszRemoteName );
}

BOOL ccFXMkDir(PCLNTCONN pCC, PSZ pszRemoteName)
{
  return fxMkDir( pCC->pRFBClient, pszRemoteName );
}

BOOL ccFXRename(PCLNTCONN pCC, PSZ pszRemoteName, PSZ pszNewName)
{
  return fxRename( pCC->pRFBClient, pszRemoteName, pszNewName );
}

BOOL ccFXSendFile(PCLNTCONN pCC, PSZ pszRemoteName, PSZ pszLocalName)
{
  return fxSendFile( pCC->pRFBClient, pszRemoteName, pszLocalName );
}

VOID ccThreadWatchdog()
{
  ULONG      ulTime;
  PCLNTCONN  pCC;
  ULONG      ulRC;

  DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &ulTime, sizeof(ULONG) );

  DosRequestMutexSem( hmtxCCList, SEM_INDEFINITE_WAIT );

  for( pCC = (PCLNTCONN)lnkseqGetFirst( &lsCCList ); pCC != NULL;
       pCC = (PCLNTCONN)lnkseqGetNext( pCC ) )
  {
    if ( ( pCC->ulStopSigTime != 0 ) &&
         ( (ulTime - pCC->ulStopSigTime) > _WAIT_THREAD_TIME ) )
    {
      debug( "The thread does not end for a long time (%u msec.), "
             "kill it...", ulTime - pCC->ulStopSigTime );
      _ccSetThreadState( pCC, RFBSTATE_FINISH );
      ulRC = DosKillThread( pCC->iRFBTid );
      if ( ulRC != NO_ERROR )
        debug( "DosKillThread(), rc = %u", ulRC );

      pCC->iRFBTid = -1;
    }
  }

  DosReleaseMutexSem( hmtxCCList );
}

BOOL ccSearchListener(PSZ pszAddress, USHORT usPort)
{
  PCLNTCONN  pCC;

  DosRequestMutexSem( hmtxCCList, SEM_INDEFINITE_WAIT );

  for( pCC = (PCLNTCONN)lnkseqGetFirst( &lsCCList ); pCC != NULL;
       pCC = (PCLNTCONN)lnkseqGetNext( pCC ) )
  {
    if ( ( pCC->pRFBClient->listenPort == usPort ) &&
         ( STR_ICMP( pCC->pRFBClient->listenAddress, pszAddress ) == 0 ) )
    {
      if ( pCC->hwnd != NULLHANDLE )
      {
        WinSetWindowPos( pCC->hwnd, HWND_TOP, 0, 0, 0, 0,
                         SWP_ZORDER | SWP_ACTIVATE );
        break;
      }
    }
  }

  DosReleaseMutexSem( hmtxCCList );

  return pCC != NULL;
}
