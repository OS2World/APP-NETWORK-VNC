// [Digi] Debug stuff.

#include <time.h>
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#ifdef __WATCOMC__
#include <process.h>
#endif

#include <types.h>
#include <sys\socket.h>
#include <unistd.h>

#define INCL_DOSSEMAPHORES
#include <os2.h>
#define DEBUG_C
#include "debug.h"

static HMTX		hMtx;

typedef struct _DBGCOUNTER {
  struct _DBGCOUNTER *pNext;
  char               *pcName;
  int                iValue;
  unsigned int       cInc;
  unsigned int       cDec;
} DBGCOUNTER, *PDBGCOUNTER;

typedef struct _BUFPSZ {
  struct _BUFPSZ  *pNext;
  struct _BUFPSZ  **ppSelf;
  char            acString[1];
} BUFPSZ, *PBUFPSZ;

typedef struct _BUFPSZSEQ {
  PBUFPSZ		pList;
  PBUFPSZ		*ppLast;
  int			cBufPSZ;
} BUFPSZSEQ, *PBUFPSZSEQ;

#define MEMHDRFNLEN    32
typedef struct _MEMBLKHDR {
  char          acFile[MEMHDRFNLEN];
  int           iLine;
  size_t        cBytes;
} MEMBLKHDR, *PMEMBLKHDR;

static FILE		*fdDebug = NULL;
static PDBGCOUNTER	pCounters = NULL;
static unsigned int	cInit = 0;
static BUFPSZSEQ	lsBufPSZ;

#define DEBUG_BEGIN\
  if ( !fdDebug ) return;\
  DosRequestMutexSem( hMtx, SEM_INDEFINITE_WAIT );
#define DEBUG_BEGIN_0\
  if ( !fdDebug ) return 0;\
  DosRequestMutexSem( hMtx, SEM_INDEFINITE_WAIT );

#define DEBUG_END DosReleaseMutexSem( hMtx );


void debug_init(char *pcDebugFile)
{
  if ( cInit == 0 )
  {
    fdDebug = fopen( pcDebugFile, "a" );
    if ( fdDebug == NULL )
      printf( "Cannot open debug file %s\n", pcDebugFile );

    lsBufPSZ.ppLast = &lsBufPSZ.pList;
    DosCreateMutexSem( NULL, &hMtx, 0, FALSE );
  }

  cInit++;
}

void debug_done()
{
  if ( cInit == 0 )
    return;
  if ( cInit > 0 )
    cInit--;

  if ( cInit == 0 )
  {
    PBUFPSZ		pNextBufPSZ;
    PDBGCOUNTER		pNext, pScan = pCounters;

    while( lsBufPSZ.pList != NULL )
    {
      pNextBufPSZ = lsBufPSZ.pList->pNext;
      free( lsBufPSZ.pList );
      lsBufPSZ.pList = pNextBufPSZ;
    }

    debug_state();
    while( pScan != NULL )
    {
      pNext = pScan->pNext;
      free( pScan->pcName );
      free( pScan );
      pScan = pNext;
    }

    fclose( fdDebug );
    fdDebug = NULL;
    DosCloseMutexSem( hMtx );
  }
}

void debug_write(char *pcFormat, ...)
{
  va_list	arglist;
  time_t	t;
  char		acBuf[32];

  t = time( NULL );
  strftime( acBuf, sizeof(acBuf)-1, "%T", localtime( &t ) );
  DEBUG_BEGIN
  fprintf( fdDebug, "[%s] ", acBuf );
  va_start( arglist, pcFormat ); 
  vfprintf( fdDebug, pcFormat, arglist );
  va_end( arglist );
  DEBUG_END
  fflush(NULL);
}

void debug_text(char *pcFormat, ...)
{
  va_list	arglist;

  DEBUG_BEGIN
  va_start( arglist, pcFormat ); 
  vfprintf( fdDebug, pcFormat, arglist );
  va_end( arglist );
  DEBUG_END
  fflush(NULL);
}

char *debug_buf2psz(char *pcBuf, unsigned int cbBuf)
{
  PBUFPSZ	pBufPSZ = malloc( sizeof(BUFPSZ) + cbBuf );

  DEBUG_BEGIN_0;
  if ( lsBufPSZ.cBufPSZ > 32 )
  {
    // Remove first record
    PBUFPSZ	pFirst = lsBufPSZ.pList;

    lsBufPSZ.pList = pFirst->pNext;
    if ( pFirst->pNext != NULL )
      pFirst->pNext->ppSelf = &lsBufPSZ.pList;
    else
      lsBufPSZ.ppLast = &lsBufPSZ.pList;
    lsBufPSZ.cBufPSZ--;

    free( pFirst );
  }

  if ( pBufPSZ == NULL )
  {
    DEBUG_END;
    return "<debug_buf2psz() : not enough memory>";
  }

  memcpy( &pBufPSZ->acString, pcBuf, cbBuf );
  pBufPSZ->acString[cbBuf] = '\0';

  // Add new record to the end of list
  pBufPSZ->pNext = NULL;
  pBufPSZ->ppSelf = lsBufPSZ.ppLast;
  *lsBufPSZ.ppLast = pBufPSZ;
  lsBufPSZ.ppLast = &pBufPSZ->pNext;
  lsBufPSZ.cBufPSZ++;

  DEBUG_END;
  return pBufPSZ->acString;
}

void debug_textbuf(char *pcBuf, unsigned int cbBuf, int fCRLF)
{
  DEBUG_BEGIN
  fwrite( pcBuf, cbBuf, 1, fdDebug );
  if ( fCRLF )
    fputs( "\n", fdDebug );
  DEBUG_END
  fflush(NULL);
}

int debug_counter(char *pcName, int iDelta)
{
  PDBGCOUNTER   pScan;
  int           iRes = 0;

  DEBUG_BEGIN_0

  for( pScan = pCounters;
       ( pScan != NULL ) && ( strcmp( pcName, pScan->pcName ) != 0 );
       pScan = pScan->pNext )
  { }

  if ( pScan == NULL )
  {
    pScan = calloc( 1, sizeof(DBGCOUNTER) );
    if ( pScan == NULL )
      fprintf( fdDebug, "Not enough memory for new counter: %s\n", pcName );
    else
    {
      pScan->pcName = strdup( pcName );
      if ( pScan->pcName == NULL )
      {
        free( pScan );
        pScan = NULL;
        fprintf( fdDebug, "Not enough memory for new counter name: %s\n", pcName );
      }
      else
      {
        pScan->pNext = pCounters;
        pCounters = pScan;
      }
    }
  }

  if ( pScan != NULL )
  {
    if ( iDelta > 0 )
      pScan->cInc++;
    else if ( iDelta < 0 )
    {
      pScan->cDec++;
      iRes = pScan->iValue == 0;
    }
    pScan->iValue += iDelta;
  }

  DEBUG_END
  return iRes;
}


void *debug_malloc(size_t size, char *pcFile, int iLine)
{
  void		*pBlock = malloc( sizeof(MEMBLKHDR) + size );
  int       cbFNPart, cbFile = strlen( pcFile );
  char      acCounter[MEMHDRFNLEN + 8];

  if ( pBlock == NULL )
  {
    debug_write( "%s#%u : Not enough memory\n", pcFile, iLine );
    return NULL;
  }

  cbFNPart = min( MEMHDRFNLEN - 1, cbFile );

  strcpy( ((PMEMBLKHDR)pBlock)->acFile, &pcFile[cbFile - cbFNPart] );

  ((PMEMBLKHDR)pBlock)->cBytes = size;
  ((PMEMBLKHDR)pBlock)->iLine = iLine;
  debug_counter( "$mem_alloc", size );

  if ( _snprintf( acCounter, sizeof(acCounter), "%s#%u",
                  ((PMEMBLKHDR)pBlock)->acFile, iLine ) == -1 )
    debug( "Can't build a counter name" );
  else
    debug_counter( acCounter, size );

  return ((char *)pBlock) + sizeof(MEMBLKHDR);
}

void *debug_calloc(size_t n, size_t size, char *pcFile, int iLine)
{
  void		*pBlock;

  size *= n;
  pBlock = debug_malloc( size, pcFile, iLine );
  if ( pBlock != NULL )
    bzero( pBlock, size );

  return pBlock;
}

void debug_free(void *ptr, char *pcFile, int iLine)
{
  char       acCounter[MEMHDRFNLEN + 8];

  if ( ptr == NULL )
  {
    debug_write( "%s#%u : debug_free(): Pointer is NULL\n", pcFile, iLine );
    return;
  }

  ptr = ((char *)ptr) - sizeof(MEMBLKHDR);
  debug_counter( "$mem_alloc", -((PMEMBLKHDR)ptr)->cBytes );

  if ( _snprintf( acCounter, sizeof(acCounter), "%s#%u",
                  ((PMEMBLKHDR)ptr)->acFile, ((PMEMBLKHDR)ptr)->iLine ) == -1 )
    debug( "Can't build a counter name" );
  else
    debug_counter( acCounter, -((PMEMBLKHDR)ptr)->cBytes );

  free( ptr );
}

void *debug_realloc(void *pMemOld, size_t size, char *pcFile, int iLine)
{
  void		   *pNewMem;

  pNewMem = debug_malloc( size, pcFile, iLine );

  if ( pMemOld != NULL )
  {
    size_t sizeOld = ((PMEMBLKHDR)((char *)pMemOld - sizeof(MEMBLKHDR)))->cBytes;

    if ( pNewMem != NULL )
      memcpy( pNewMem, pMemOld, min( sizeOld, size ) );

    if ( ( size == 0 || pNewMem != NULL ) && ( pMemOld != NULL ) )
      debug_free( pMemOld, pcFile, iLine );
  }

  return pNewMem;
}

char *debug_strdup(const char *src, char *pcFile, int iLine)
{
  char		   *dst;

  if ( src == NULL )
    return NULL;

  dst = debug_malloc( strlen( src ) + 1, pcFile, iLine );
  if ( dst != NULL )
    strcpy( dst, src );

  return dst;
}


int debug_socket(int domain, int type, int protocol)
{
  int        rc = socket( domain, type, protocol );

  if ( rc != -1 )
    debug_counter( "$sockets", DBGCNT_INC );

  return rc;
}

int debug_accept(int s, void *name, int *namelen)
{
  int        rc = accept( s, (struct sockaddr *)name, namelen );

  if ( rc != -1 )
    debug_counter( "$sockets", DBGCNT_INC );

  return rc;
}

int debug_soclose(int s)
{
  int        rc = soclose( s );

  if ( rc != -1 )
    debug_counter( "$sockets", DBGCNT_DEC );

  return rc;
}


typedef struct _DEBUGTHREAD {
  void (*fnThread)(void *);
  void *pArg;
} DEBUGTHREAD, *PDEBUGTHREAD;

static void threadDebug(void *pArg)
{
  void (*fnRealThread)(void *) = ((PDEBUGTHREAD)pArg)->fnThread;
  void *pRealArg               = ((PDEBUGTHREAD)pArg)->pArg;

  free( pArg );
  debug_counter( "$threads", DBGCNT_INC );
  fnRealThread( (void *)pRealArg );
  debug_counter( "$threads", DBGCNT_DEC );
}

int debug_beginthread(void (*start_address)(void *), void *stack_bottom,
                      unsigned stack_size, void *arglist)
{
  PDEBUGTHREAD   pDbgThreadArg = malloc( sizeof(DEBUGTHREAD) );
  int rc;

  if ( pDbgThreadArg == NULL )
    return -1;

  pDbgThreadArg->fnThread = start_address;
  pDbgThreadArg->pArg = arglist;
  rc = _beginthread( threadDebug, stack_bottom, stack_size, pDbgThreadArg );
  if ( rc == -1 )
    free( pDbgThreadArg );

  return rc;
}

void debug_endthread()
{
  debug_counter( "$threads", DBGCNT_DEC );
  _endthread();
}



// debug_state()
// Writes list of counters and values to the logfile.

static int _counterComp(const void *pCnt1, const void *pCnt2)
{
  return strcmp( (*(PDBGCOUNTER *)pCnt1)->pcName,
                 (*(PDBGCOUNTER *)pCnt2)->pcName );
}

static void _debugCounter(PDBGCOUNTER pCounter)
{
  debug_text( "%-25s %5u          %5u(%d)     %5d\n",
              pCounter->pcName, pCounter->cInc, pCounter->cDec,
              pCounter->cInc - pCounter->cDec, pCounter->iValue );
}

void DBGLIBENTRY debug_state()
{
  ULONG             ulCount = 0, ulIdx;
  PDBGCOUNTER       pScan;
  PDBGCOUNTER       *ppList;

  debug_write( "Debug counters:\n"
               "Counter name            Incr. times     Decr. times     Value\n" );

  DEBUG_BEGIN

  // Allocalte stack for the sorted counters list.
  for( pScan = pCounters; pScan != NULL; pScan = pScan->pNext )
    ulCount++;
  ppList = alloca( sizeof(PDBGCOUNTER) * ulCount );

  if ( ppList == NULL )
  {
    // Output unsorted list.
    for( pScan = pCounters; pScan != NULL; pScan = pScan->pNext )
      _debugCounter( pScan );
    return;
  }

  // Fill list of counters.
  for( pScan = pCounters, ulIdx = 0; ulIdx < ulCount;
       pScan = pScan->pNext, ulIdx++ )
    ppList[ulIdx] = pScan;
  // Sort it...
  qsort( ppList, ulCount, sizeof(PDBGCOUNTER), _counterComp );

  // Output sorted list.
  for( ulIdx = 0; ulIdx < ulCount; ulIdx++ )
    _debugCounter( ppList[ulIdx] );

  DEBUG_END
}

int debug_memused()
{
  PDBGCOUNTER		pScan;

  DEBUG_BEGIN_0

  for( pScan = pCounters;
       ( pScan != NULL ) && ( strcmp( "$mem_alloc", pScan->pcName ) != 0 );
       pScan = pScan->pNext )
  { }

  DEBUG_END

  return pScan == NULL ? -1 : pScan->iValue;
}
