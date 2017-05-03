#define INCL_DOSSEMAPHORES
#define INCL_DOSERRORS
#include <os2.h>
#include <rfb/rfbclient.h>
#include <errno.h>
#include "csconv.h"
#include <debug.h>

#define _TAG_CSCONV    ((PVOID)0x00010002)

typedef struct _CONVDIR {
  iconv_t    ic;
  HMTX       hmtxDir;
} CONVDIR, *PCONVDIR;

typedef struct _CLIENTCONV {
  CONVDIR    stLR;
  CONVDIR    stRL;
  CHAR       acRemoteCP[1];
} CLIENTCONV, *PCLIENTCONV;


static iconv_t _dirIConvOpen(ULONG ulDirection, PSZ pszCS)
{
  PSZ        pszToCode = ulDirection == CSC_LOCALTOREMOTE ? pszCS : "";
  PSZ        pszFromCode = ulDirection == CSC_LOCALTOREMOTE ? "" : pszCS;
  iconv_t    ic;

  ic = iconv_open( (const char *)pszToCode, (const char *)pszFromCode );
  if ( ic == ((iconv_t)(-1)) )
    debug( "iconv_open(\"%s\",\"%s\") failed", pszToCode, pszFromCode );

  return ic;
}

static BOOL _dirInit(PCONVDIR pDir, ULONG ulDirection, PSZ pszCS)
{
  ULONG      ulRC;

  pDir->ic = _dirIConvOpen( ulDirection, pszCS );
  if ( pDir->ic == ((iconv_t)(-1)) )
    return FALSE;

  ulRC = DosCreateMutexSem( NULL, &pDir->hmtxDir, 0, FALSE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosCreateMutexSem(), rc = %u", ulRC );
    pDir->hmtxDir = NULLHANDLE;
    iconv_close( pDir->ic );
    return FALSE;
  }

  return TRUE;
}

static VOID _dirDone(PCONVDIR pDir)
{
  if ( pDir->ic != ((iconv_t)(-1)) )
    iconv_close( pDir->ic );

  if ( pDir->hmtxDir != NULLHANDLE )
  {
    DosCloseMutexSem( pDir->hmtxDir );
    pDir->hmtxDir = NULLHANDLE;
  }
}


PSZ cscIConv(iconv_t ic, ULONG cbStrIn, PCHAR pcStrIn)
{
  ULONG      cbStrOut;
  PCHAR      pcStrOut, pcDst;
  size_t     rc;
  BOOL       fError = FALSE;

  if ( ic == ((iconv_t)(-1)) )
    return NULL;

  cbStrOut = ( ( cbStrIn > 4 ? cbStrIn : 4 ) + 2 ) * 2;
  pcStrOut = malloc( cbStrOut );
  if ( pcStrOut == NULL )
  {
    debugCP( "Not enough memory" );
    return NULL;
  }

  pcDst = pcStrOut;
  while( cbStrIn > 0 )
  {
    rc = iconv( ic, (const char **)&pcStrIn, (size_t *)&cbStrIn,
                &pcDst, (size_t *)&cbStrOut );
    if ( rc == (size_t)-1 )
    {
      if ( errno == EILSEQ )
      {
        // Try to skip invalid character.
        pcStrIn++;
        cbStrIn--;
        continue;
      }

//      debugCP( "iconv() failed" );
      fError = TRUE;
      break;
    }
  }

  if ( !fError )
    iconv( ic, NULL, 0, &pcDst, (size_t *)&cbStrOut );

  if ( fError )
  {
    free( pcStrOut );
    return NULL;
  }

  // Write trailing ZERO (2 bytes).
  if ( cbStrOut >= 2 )
  {
    *((PUSHORT)pcDst) = 0;
    pcDst += 2;
  }
  else
  {
//    fError = TRUE;               // The destination buffer overflow.
    if ( cbStrOut == 1 )
    {
      *pcDst = 0;
      pcDst++;
    }
  }

  pcDst = realloc( pcStrOut, (pcDst - pcStrOut) );
  if ( pcDst == NULL )
  {
    debugCP( "realloc() failed" );
    return pcStrOut;
  }

  return pcDst;
}

BOOL cscInit(rfbClient* client, PSZ pszRemoteCS)
{
  PCLIENTCONV          pClientConv;

  if ( ( pszRemoteCS == NULL ) || ( *pszRemoteCS == '\0' ) )
    return TRUE;

  pClientConv = malloc( sizeof(CLIENTCONV) + strlen( pszRemoteCS ) );
  if ( pClientConv == NULL )
    return FALSE;

  if ( !_dirInit( &pClientConv->stLR, CSC_LOCALTOREMOTE, pszRemoteCS ) )
  {
    free( pClientConv );
    return FALSE;
  }

  if ( !_dirInit( &pClientConv->stRL, CSC_REMOTETOLOCAL, pszRemoteCS ) )
  {
    _dirDone( &pClientConv->stLR );
    free( pClientConv );
    return FALSE;
  }

  strcpy( pClientConv->acRemoteCP, pszRemoteCS );
  cscDone( client );
  rfbClientSetClientData( client, _TAG_CSCONV, pClientConv );

  return TRUE;
}

VOID cscDone(rfbClient* client)
{
  PCLIENTCONV  pClientConv = (PCLIENTCONV)rfbClientGetClientData( client,
                                                                  _TAG_CSCONV );

  if ( pClientConv == NULL )
    return;

  _dirDone( &pClientConv->stLR );
  _dirDone( &pClientConv->stRL );
  free( pClientConv );
  rfbClientSetClientData( client, _TAG_CSCONV, NULL );
}

PSZ cscConv(rfbClient* client, ULONG ulDirection, ULONG cbStr, PCHAR pcStr)
{
  PCLIENTCONV          pClientConv;
  PCONVDIR             pDir;
  PSZ                  pszStrOut;
  ULONG                ulRC;

  if ( cbStr == 0 )
    return NULL;

  pClientConv = (PCLIENTCONV)rfbClientGetClientData( client, _TAG_CSCONV );
  if ( pClientConv == NULL )
    return NULL;
/*  {
    pszStrOut = malloc( cbStr + 1 );
    if ( pszStrOut != NULL )
      strcpy( pszStrOut, pcStr );
    return pszStrOut;
  }*/

  pDir = ulDirection == CSC_LOCALTOREMOTE
           ? &pClientConv->stLR : &pClientConv->stRL;

  ulRC = DosRequestMutexSem( pDir->hmtxDir, SEM_INDEFINITE_WAIT );
  if ( ulRC != NO_ERROR )
    debug( "DosRequestMutexSem(), rc = %u", ulRC );

//  pszStrOut = _dirConv( pDir, cbStr, pcStr );
  pszStrOut = cscIConv( pDir->ic, cbStr, pcStr );

  if ( pszStrOut == NULL )
  {
    // Recreate iconv object on error.
    if ( pDir->ic != ((iconv_t)(-1)) )
      iconv_close( pDir->ic );
    pDir->ic = _dirIConvOpen( ulDirection, pClientConv->acRemoteCP );
  }

  DosReleaseMutexSem( pDir->hmtxDir );

  if ( ( pszStrOut != NULL ) && ( *((PUSHORT)pszStrOut) == 0 ) )
  {
    // No any characters decoded.
    free( pszStrOut );
    return NULL;
  }

  return pszStrOut;
}

PSZ cscConvStr(rfbClient* client, ULONG ulDirection, PSZ pszStr)
{
  if ( pszStr == NULL )
    return NULL;

  return cscConv( client, ulDirection, strlen( pszStr ), pszStr );
}
