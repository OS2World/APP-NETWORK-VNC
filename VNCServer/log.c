#include <stdarg.h> 
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#include <ctype.h>
#define INCL_WINCOUNTRY
#include <os2.h>
#include "string.h"
#include "utils.h"
#include "log.h"
#include <debug.h>

static LOGDATA         stLogData = { 0 };
static int             iFile = -1;


static BOOL _logOpen()
{
  if ( ( stLogData.acFile[0] == '\0' ) || ( stLogData.ulFiles == 0 ) )
    return TRUE;

  iFile = open( stLogData.acFile, O_WRONLY | O_APPEND | O_CREAT | O_TEXT,
                S_IRWXU | S_IRWXG | S_IRWXO | S_IREAD );
  if ( iFile == -1 )
    debug( "Cannot open/create a logfile: %s", stLogData.acFile );

  return iFile != -1;
}

VOID logClose()
{
  if ( iFile != -1 )
  {
    close( iFile );
    iFile = -1;
  }
}

BOOL logSet(HAB hab, PLOGDATA pLogData)
{
  CHAR       acFile[CCHMAXPATH];
  PSZ        pszFile, pszDst;
  ULONG      cBytes;
  ULONG      ulRC;
  BOOL       fReopen;

  // Build fully qualified file name in acFile.

  // !!! We don't care about path like D:path assumed that the current path on
  // D: will not be changed.

  utilStrTrim( pLogData->acFile );
  pszFile = pLogData->acFile;

  if ( *pszFile == '\0' )
    acFile[0] = '\0';
  else
  {
    if ( ( *pszFile != '\\' ) && ( *pszFile != '/' ) && ( pszFile[1] != ':' ) )
    {
      // Not fully qualified name.

      if ( *pszFile == '.' && ( pszFile[1] == '\\' || pszFile[1] == '/' ) )
      {
        // Name like .\file. Skip to first character after '\'.
        do
          pszFile++;
        while( *pszFile == '\\' || *pszFile == '/' );
      }

      cBytes = utilQueryProgPath( sizeof(acFile), acFile );

      if ( (sizeof(acFile) - cBytes) <= strlen( pszFile ) )
        return FALSE;

      pszDst = &acFile[cBytes];
    }
    else
      pszDst = acFile;

    strcpy( pszDst, pszFile );
    utilPathOS2Slashes( strlen( acFile ), acFile );
  }

  fReopen = (stLogData.ulFiles == 0) != (pLogData->ulFiles == 0);
  if ( !fReopen )
  {
    ulRC = WinCompareStrings( hab, 0, 0, stLogData.acFile, acFile, 0 );
    fReopen = ( ( ulRC == WCS_GT ) || ( ulRC == WCS_LT ) );
  }

  stLogData.ulLevel   = pLogData->ulLevel;
  stLogData.ulMaxSize = pLogData->ulMaxSize;
  stLogData.ulFiles   = pLogData->ulFiles;
  strcpy( stLogData.acFile, acFile );

  if ( fReopen )
  {
    // Logfile name or log files number is changed - reopen.
    logClose();
    _logOpen();
  }
  else if ( ( stLogData.acFile[0] == '\0' ) || ( stLogData.ulFiles == 0 ) )
    logClose();

  return TRUE;
}

VOID logStr(ULONG ulLevel, PSZ pszStr)
{
  CHAR       acDst[CCHMAXPATH];
  time_t     timeLog;
  PCHAR      pcEnd;

  if ( ( ulLevel > stLogData.ulLevel ) || ( iFile == -1 ) )
    return;

  if( ( stLogData.ulMaxSize != 0 ) &&
      ( tell( iFile ) >= ( stLogData.ulMaxSize * 1024 ) ) )
  {
    // Logfile maximum size reached - rotate files.

    logClose();

    if ( stLogData.ulFiles == 1 )
      unlink( stLogData.acFile );
    else
    {
      CHAR   acSrc[CCHMAXPATH];
      CHAR   acExt[16];
      ULONG  ulIdx;

      ultoa( stLogData.ulFiles - 1, acExt, 10 );
      if ( utilSetExtension( sizeof(acDst), acDst, stLogData.acFile, acExt )
             != -1 )
      {
        unlink( acDst );         // Remove oldest file.

        for( ulIdx = stLogData.ulFiles - 2; ulIdx != 0; ulIdx-- )
        {
          ultoa( ulIdx, acExt, 10 );
          if ( utilSetExtension( sizeof(acSrc), acSrc, stLogData.acFile, acExt )
                 == -1 )
            break;

          rename( acSrc, acDst ); 
          strcpy( acDst, acSrc );
        }

        rename( stLogData.acFile, acDst ); 
      }
    }

    _logOpen();
  }

  time( &timeLog );
  write( iFile, acDst,
         strftime( acDst, sizeof(acDst), "%Y%m%d %H%M%S ",
                   localtime( &timeLog ) ) ); 

  pcEnd = strchr( pszStr, '\0' );
  while( isspace( *pszStr ) )
    pszStr++;
  while( ( pcEnd > pszStr ) && isspace( *(pcEnd - 1) ) )
    pcEnd--;
  write( iFile, pszStr, pcEnd - pszStr );

  write( iFile, "\n", 1 );
}

VOID logVArg(ULONG ulLevel, PSZ pszFormat, va_list args)
{
  CHAR       acBuf[1024];

  vsnprintf( acBuf, sizeof(acBuf), pszFormat, args );
  logStr( ulLevel, acBuf );
}

VOID logFmt(ULONG ulLevel, PSZ pszFormat, ...)
{
  va_list    args;

  va_start( args, pszFormat );
  logVArg( ulLevel, pszFormat, args );
  va_end( args );
}
