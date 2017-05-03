#include <ctype.h>
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#include <os2.h>
#include <rfb/rfbclient.h>
#include "clntconn.h"
#include "clntwnd.h"
#include "filexfer.h"
#include "csconv.h"
#include <utils.h>
#include <debug.h>

#define _TAG_LIST_OBTAINING           ((PVOID)0x00010000)
#define _TAG_FILE_DATA                ((PVOID)0x00010001)

// Client data to send/receive a file. Stored with _TAG_FILE_DATA.
typedef struct _FXCDFILE {
  PSZ        pszFName;  // fRecv is TRUE - local name, remote name otherwise.
  BOOL       fRecv;
  ULLONG     ullSize;
  HFILE      hFile;
  BOOL       fSendReady;
  BOOL       fSendAbort;         // Local file read error.

  CHAR       acBuf[sz_rfbBlockSize];
  ULONG      ulBufPos;
} FXCDFILE, *PFXCDFILE;

static rfbBool _cbRFBMessage(rfbClient* prfbClient,
                             rfbServerToClientMsg* message);

// stFileExtension should be registered with rfbClientRegisterExtension()
static rfbClientProtocolExtension stFileExtension =
{
  NULL,                // encodings
  NULL,                // handleEncoding
  _cbRFBMessage,       // handleMessage
  NULL,                // next
};


static BOOL _sendRFBFilexferMsg(rfbClient* client, uint8_t ui8ContentType,
                                uint8_t ui8ContentParam, uint32_t ui32Size,
                                uint32_t ui32Length, PCHAR pcBuf)
{
  rfbFileTransferMsg       stFTMsg;

  stFTMsg.type         = rfbFileTransfer;
  stFTMsg.contentType  = ui8ContentType;
  stFTMsg.contentParam = ui8ContentParam;
  stFTMsg.pad          = 0;
  stFTMsg.size         = rfbClientSwap32IfLE( ui32Size );
  stFTMsg.length       = rfbClientSwap32IfLE( ui32Length );

  if ( !WriteToRFBServer( client, (char *)&stFTMsg, sz_rfbFileTransferMsg ) )
    return FALSE;

  if ( ( pcBuf != NULL ) &&
       !WriteToRFBServer( client, pcBuf, ui32Length ) )
    return FALSE;

  return TRUE;
}

static VOID _cdfileFree(PFXCDFILE pCDFile)
{
  if ( pCDFile->pszFName != NULL )
    free( pCDFile->pszFName );

  free( pCDFile );
}


static rfbBool _cbRFBMessage(rfbClient* client, rfbServerToClientMsg* pMsg)
{
  rfbFileTransferMsg   stFTMsg;
  PCHAR                pcBuf;
  PCLNTCONN            pCC;
  HWND                 hwnd;
  ULONG                ulRC, ulSizeHTmp;
  ULONG                fListObtaining = (ULONG)rfbClientGetClientData(
                                                client,  _TAG_LIST_OBTAINING );
  PSZ                  pszEnc;

  if ( pMsg->type != rfbFileTransfer )
    return FALSE;

  if ( !ReadFromRFBServer( client, ((char *)&stFTMsg) + 1,
                           sz_rfbFileTransferMsg - 1 ) )
  {
    debugCP( "ReadFromRFBServer() failed" );
    return TRUE;
  }

  if ( stFTMsg.contentType == rfbEndOfFile )
  {
    // UltraVNC returns garbage for size and length in this case. 8-( )
    stFTMsg.size   = 0;
    stFTMsg.length = 0;
  }
  else
  {
    stFTMsg.size   = rfbClientSwap32IfLE( stFTMsg.size );
    stFTMsg.length = rfbClientSwap32IfLE( stFTMsg.length );
  }
/*
  debug( "Msg. type: %d, contentType: %d, size: %d, length: %d",
         pMsg->type, stFTMsg.contentType, stFTMsg.size, stFTMsg.length );
*/

  if ( stFTMsg.length == 0 )
    pcBuf = NULL;
  else
  {
    pcBuf = malloc( stFTMsg.length + 1 );
    if ( !ReadFromRFBServer( client, pcBuf, stFTMsg.length ) )
    {
      debugCP( "ReadFromRFBServer() failed" );
      free( pcBuf );
      return TRUE;
    }
    // Make a buffered data ASCIIZ potential.
    pcBuf[stFTMsg.length] = '\0';
  }

  pCC = (PCLNTCONN)rfbClientGetClientData( client, NULL );
  hwnd = ccSetWindow( pCC, NULLHANDLE ); // NULLHANDLE - only query handle.

  switch( stFTMsg.contentType )
  {
    // Answers on fxFileListRequest().

    case rfbFileTransferAccess:
      // File transfer access was requested in fxSendListRequest() to obtain
      // the list of drives.

      if ( stFTMsg.size == -1 )
      {
        // File Transfer Permission denied.
        debugCP( "File Transfer Permission denied." );
        WinSendMsg( hwnd, WM_VNC_FILEXFER,
                    MPFROM2SHORT(CFX_PERMISSION_DENIED,0), 0 );
      }
      else
        // File transfer permission granted. Now we send drives list requset.
        if ( !_sendRFBFilexferMsg( client, rfbDirContentRequest,
                                   rfbRDrivesList, 0, 0, NULL ) )
        {
          if ( pcBuf != NULL )
            free( pcBuf );
          return FALSE;
        }

      break;

    case rfbDirPacket:
      switch( stFTMsg.contentParam )
      {
        case rfbADrivesList:
          // pcBuf contains drives list: C:?\0D:?\0\0 ,
          // where ? is 'l' (local) / 'f' (floppy) / 'c' (cdrom) / 'n' (network)
          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_DRIVES,stFTMsg.length), pcBuf );
          break;

        case rfbADirectory:
          // First rfbADirectory reply is a string - requested path.
          // Next replies is RFB_FIND_DATA records.
          // End-Of-List mark when stFTMsg.contentType is 0 or
          // stFTMsg.length is 0.
          // Second case: rfbserver.c/rfbSendDirContent(), opendir() result is
          // NULL.
          // Perhaps, one of End-Of-List variants is a bug in libvncserver.
          if ( stFTMsg.length != 0 )
          {
            PCHAR      pcName;
            ULONG      cbName;

            if ( fListObtaining == 0 )
            {
              // First rfbADirectory reply. pcBuf is a remote path.

              rfbClientSetClientData( client, _TAG_LIST_OBTAINING, (PVOID)1 );
              pszEnc = cscConv( client, CSC_REMOTETOLOCAL, stFTMsg.length,
                                pcBuf );
              if ( pszEnc != NULL )
              {
                cbName = strlen( pszEnc );
                pcName = pszEnc;
              }
              else
              {
                cbName = stFTMsg.length;
                pcName = pcBuf;
              }

              WinSendMsg( hwnd, WM_VNC_FILEXFER, MPFROM2SHORT(CFX_PATH,cbName),
                          MPFROMP(pcName) );
            }
            else
            {
              // Not first rfbADirectory reply. pcBuf is RFB_FIND_DATA pointer.

              RFB_FIND_DATA      *pData = (RFB_FIND_DATA *)pcBuf;
              ULONG              cbData = stFTMsg.length;
              RFB_FIND_DATA      stNewData;

              pszEnc = cscConvStr( client, CSC_REMOTETOLOCAL,
                                   (PCHAR)pData->cFileName );
              if ( pszEnc != NULL )
              {
                memcpy( &stNewData, pcBuf, sizeof(RFB_FIND_DATA) -
                        ( MAX_PATH + 14 ) );
                strlcpy( (PCHAR)stNewData.cFileName, pszEnc, MAX_PATH );
                pData = &stNewData;
                cbData = sizeof(RFB_FIND_DATA) - ( MAX_PATH + 14 ) +
                         strlen( pszEnc );
              }

              WinSendMsg( hwnd, WM_VNC_FILEXFER,
                          MPFROM2SHORT(CFX_FINDDATA,cbData), MPFROMP(pData) );
            }

            if ( pszEnc != NULL )
              cscFree( pszEnc );
            break;
          }
          // go to case 0

        case 0:
          // End of files list.
          rfbClientSetClientData( client, _TAG_LIST_OBTAINING, (PVOID)0 );
          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_END_OF_LIST,0), 0 );
          break;

        default:
          debug( "Unknown message contentParam=%d when contentType=rfbDirPacket",
                 stFTMsg.contentParam, stFTMsg.contentType );
      } // switch( stFTMsg.contentParam )
      break; // End case rfbDirPacket.

    // Answers on fxRecvFile() - sending from the server to the viewer.

    case rfbFileHeader:
      // Start receiving a file.
      {
        PFXCDFILE  pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                          _TAG_FILE_DATA );
        PCHAR      pcComma;
        ULONG      ulAction;
        LONGLONG   llSize = { 0, 0 };

        if ( pCDFile == NULL )
        {
          debugCP( "WTF? No FXCDFILE data" );
          break;
        }

        if ( stFTMsg.size == -1 )
        {
          // Server reports an error. Send window message
          // CFX_RFILE_READ_FAIL with remote file name.

          _sendRFBFilexferMsg( client, rfbFileHeader, 0, -1, 0, NULL );
          fxClean( client );

          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_RECV_READ_FAIL,stFTMsg.length),
                      MPFROMP(pcBuf) );
          break;
        }

        pcComma = strrchr( pcBuf, ',' );
        if ( pcComma != NULL)
          *pcComma = '\0'; // pcComma+1: file (change?) time (mm/dd/yyyy hh:mm)

        if ( !ReadFromRFBServer( client, ((char *)&ulSizeHTmp), 4 ) )
        {
          debugCP( "ReadFromRFBServer() failed" );
          break;
        }

        ulRC = DosOpenL( pCDFile->pszFName, &pCDFile->hFile, &ulAction,
                        llSize, FILE_NORMAL, 
                        OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS,
                        OPEN_FLAGS_FAIL_ON_ERROR | OPEN_FLAGS_SEQUENTIAL |
                        OPEN_SHARE_DENYWRITE | OPEN_ACCESS_WRITEONLY, NULL );
        if ( ulRC != NO_ERROR )
        {
          debug( "DosOpenL() for %s, rc = %u", pcBuf, ulRC );
          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_RECV_CREATE_FAIL,
                                   strlen( pCDFile->pszFName )),
                      MPFROMP(pCDFile->pszFName) );
          fxClean( client );
          break;
        }
        ((PULL2UL)&pCDFile->ullSize)->ulLow = stFTMsg.size;
        ((PULL2UL)&pCDFile->ullSize)->ulHigh = rfbClientSwap32IfLE( ulSizeHTmp );

        _sendRFBFilexferMsg( client, rfbFileHeader, 0, stFTMsg.size, 0, NULL );
      }
      break;

    case rfbFilePacket:
      // stFTMsg.length / pcBuf - a file's chunk.
      {
        PFXCDFILE  pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                          _TAG_FILE_DATA );
        ULONG      ulActual;

        if ( pCDFile == NULL )
        {
          debugCP( "WTF? No FXCDFILE data" );
          break;
        }

        if ( pCDFile->hFile == NULLHANDLE )
        {
          debugCP( "WTF? File was not opened" );
          break;
        }

        ulRC = DosWrite( pCDFile->hFile, pcBuf, stFTMsg.length, &ulActual );
        if ( ulRC != NO_ERROR )
        {
          debug( "DosWrite(), rc = %u", ulRC );
          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                 MPFROM2SHORT(CFX_RECV_WRITE_FAIL,
                              strlen( pCDFile->pszFName )),
                 MPFROMP(pCDFile->pszFName) );
          fxClean( client );
          break;
        }

        WinSendMsg( hwnd, WM_VNC_FILEXFER, MPFROM2SHORT(CFX_RECV_CHUNK,0),
                    MPFROMLONG(stFTMsg.length) );
      }
      break;

    case rfbEndOfFile:
      {
        PFXCDFILE      pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                          _TAG_FILE_DATA );
        FILESTATUS3L   stInfo;
        HFILE          hFile;

        if ( pCDFile == NULL )
        {
          debugCP( "WTF? No FXCDFILE data" );
          break;
        }

        hFile = pCDFile->hFile;
        if ( hFile == NULLHANDLE )
        {
          debugCP( "WTF? File was not opened" );
          break;
        }

        pCDFile->hFile = NULLHANDLE;
        fxClean( client );

        ulRC = DosQueryFileInfo( hFile, FIL_STANDARDL, &stInfo, sizeof(stInfo) );
        if ( ulRC != NO_ERROR )
          debug( "DosQueryFileInfo(), rc = %u", ulRC );

        // Inform the window that the file end is reached and let to change
        // file information.
        if ( (BOOL)WinSendMsg( hwnd, WM_VNC_FILEXFER,
                               MPFROM2SHORT(CFX_RECV_END_OF_FILE,sizeof(stInfo)),
                               &stInfo ) )
        {
          ulRC = DosSetFileInfo( hFile, FIL_STANDARDL, &stInfo, sizeof(stInfo) );
          if ( ulRC != NO_ERROR )
            debug( "DosSetFileInfo(), rc = %u", ulRC );
        }

        DosClose( hFile );
      }
      break;

    case rfbAbortFileTransfer:
      {
        PFXCDFILE  pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                          _TAG_FILE_DATA );

        if ( pCDFile == NULL )
        {
          debugCP( "WTF? No FXCDFILE data" );
          break;
        }

        if ( pCDFile->fRecv )
        {
          // Server reports an error. Send window message CFX_RFILE_READ_FAIL.
          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_RECV_READ_FAIL,0), 0 );
          fxClean( client );
          break;
        }
      }
      break;

    // Answers on fxSendFile() - sending from the viewer to the server.

    case rfbFileAcceptHeader:
      {
        PFXCDFILE  pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                              _TAG_FILE_DATA );

        if ( stFTMsg.size == -1 )
        {
          // Server reports an error. Send window message CFX_RFILE_READ_FAIL.
          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_SEND_WRITE_FAIL,0), 0 );
          fxClean( client );
          break;
        }

        pCDFile->fSendReady = TRUE;
      }
      break;


    case rfbCommandReturn:
      // Command answers.

      switch( stFTMsg.contentParam )
      {
        case rfbAFileDelete:
          // pcBuf contains directory/file name; stFTMsg.size is -1 on error.
          debug( "Delete - %s: %s", (int)stFTMsg.size == -1 ? "FAILED" : "Ok",
                 pcBuf );

          pszEnc = cscConv( client, CSC_REMOTETOLOCAL, stFTMsg.length, pcBuf );

          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_DELETE_RES,
                                   (int)stFTMsg.size != 0 ? 0 : 1),
                      MPFROMP(pszEnc == NULL ? pcBuf : pszEnc) );

          if ( pszEnc != NULL )
            cscFree( pszEnc );
          break;

        case rfbADirCreate:
          // pcBuf contains directory/file name; stFTMsg.size is -1 on error.
          debug( "Make directory - %s: %s", (int)stFTMsg.size == -1 ? "FAILED" : "Ok",
                 pcBuf );

          pszEnc = cscConv( client, CSC_REMOTETOLOCAL, stFTMsg.length, pcBuf );

          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_MKDIR_RES,
                                   (int)stFTMsg.size != 0 ? 0 : 1 ),
                      MPFROMP(pszEnc == NULL ? pcBuf : pszEnc) );

          if ( pszEnc != NULL )
            cscFree( pszEnc );
          break;

        case rfbAFileRename:
        {
          PCHAR        pcNewName = strrchr( pcBuf, '*' );
          PSZ          apszMsgArg[2], pszEnc2;

          if ( pcNewName == NULL )
          {
            debug( "No asterisk in answer rfbAFileRename: %s", pcBuf );
            break;
          }
          *pcNewName = '\0';
          pcNewName++;

          pszEnc = cscConvStr( client, CSC_REMOTETOLOCAL, pcBuf );
          pszEnc2 = cscConvStr( client, CSC_REMOTETOLOCAL, pcNewName );

          apszMsgArg[0] = pszEnc == NULL ? pcBuf : pszEnc;
          apszMsgArg[1] = pszEnc2 == NULL ? pcNewName : pszEnc2;
          WinSendMsg( hwnd, WM_VNC_FILEXFER,
                      MPFROM2SHORT(CFX_RENAME_RES,
                                   (int)stFTMsg.size != 0 ? 0 : 1 ),
                      apszMsgArg );

          if ( pszEnc != NULL )
            cscFree( pszEnc );
          if ( pszEnc2 != NULL )
            cscFree( pszEnc2 );
          break;
        }
      }
      break;

    default:
      debug( "Unknown message contentType: %d\n", stFTMsg.contentType );
  } // switch( stFTMsg.contentType )

  if ( pcBuf != NULL )
    free( pcBuf );

  return TRUE;
}


VOID fxRegister()
{
  rfbClientRegisterExtension( &stFileExtension );
}

VOID fxClean(rfbClient* client)
{
  PFXCDFILE  pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                          _TAG_FILE_DATA );
  if ( pCDFile != NULL )
  {
    if ( pCDFile->hFile != NULLHANDLE )
    {
      DosClose( pCDFile->hFile );
      if ( pCDFile->fRecv )
        DosDelete( pCDFile->pszFName );
    }

    _cdfileFree( pCDFile );
    rfbClientSetClientData( client, _TAG_FILE_DATA, NULL );
  }
}

BOOL fxFileListRequest(rfbClient* client, PSZ pszPath)
{
  ULONG                cbPath = STR_LEN( pszPath );
  PSZ                  pszBuf, pszEnc;
  uint8_t              ui8ContentType;
  uint8_t              ui8ContentParam;
  BOOL                 fSuccess;

  if ( cbPath == 0 )
  {
    // Empty list specified - we must query a drives list.
    // But first we check file transfer permission with rfbAbortFileTransfer
    // message. It did rfbFileTransferAccess answer with permission flag and
    // then we will send file-list request, see _cbRFBMessage().

    pszEnc = NULL;
    ui8ContentType  = rfbAbortFileTransfer;
    ui8ContentParam = 3;         // UltraVNC do it on my system (garbage?).
  }
  else
  {
    if ( pszPath[cbPath-1] != '\\' )
    {
      // Append trailing slash to the path.
      pszBuf = alloca( cbPath + 2 );
      if ( pszBuf == NULL )
      {
        debugCP( "Not enough stack space" );
        return FALSE;
      }

      memcpy( pszBuf, pszPath, cbPath );
      *((PUSHORT)&pszBuf[cbPath]) = 0x005C;
      cbPath++;
      pszPath = pszBuf;
    }

    pszEnc = cscConv( client, CSC_LOCALTOREMOTE, cbPath, pszPath );
    if ( pszEnc != NULL )
    {
      cbPath = strlen( pszEnc );
      pszPath = pszEnc;

      if ( ( cbPath == 0 ) || !isalpha( *pszPath ) ) // Conversion error?
      {
        free( pszEnc );
        return FALSE;
      }
    }

    ui8ContentType  = rfbDirContentRequest;
    ui8ContentParam = rfbRDirContent;
  }

  // Send request to the server.

  fSuccess = _sendRFBFilexferMsg( client, ui8ContentType, ui8ContentParam,
                                  0, cbPath, pszPath );
  if ( pszEnc != NULL )
    cscFree( pszEnc );

  return fSuccess;
}

// Sends file transfer request. Answer is a message with contentType
// rfbFileHeader.
// On rfbFileHeader window will be notices with CFX_RECV_xxxxx.
BOOL fxRecvFile(rfbClient* client, PSZ pszRemoteName, PSZ pszLocalName)
{
  ULONG      cbRemoteName = strlen( pszRemoteName );
  PFXCDFILE  pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                              _TAG_FILE_DATA );
  PSZ        pszEnc;
  BOOL       fSuccess;

  if ( pCDFile != NULL )
  {
    debug( "File transfering in progress (now requested %s)", pszRemoteName );
    return FALSE;
  }

  pCDFile = calloc( 1, sizeof(FXCDFILE) );
  if ( pCDFile == NULL )
  {
    debugCP( "Not enough memory" );
    return FALSE;
  }

  pCDFile->hFile      = -1;
  pCDFile->fRecv      = TRUE;
  pCDFile->pszFName   = strdup( pszLocalName );
  if ( pCDFile->pszFName == NULL )
  {
    debugCP( "abort" );
    _cdfileFree( pCDFile );
    return FALSE;
  }

  rfbClientSetClientData( client, _TAG_FILE_DATA, pCDFile );


  pszEnc = cscConv( client, CSC_LOCALTOREMOTE, cbRemoteName, pszRemoteName );
  if ( pszEnc == NULL )
    fSuccess = _sendRFBFilexferMsg( client, rfbFileTransferRequest, 0, 0,
                                    cbRemoteName, pszRemoteName );
  else
  {
    fSuccess = _sendRFBFilexferMsg( client, rfbFileTransferRequest, 0, 0,
                                    strlen( pszEnc ), pszEnc );
    cscFree( pszEnc );
  }

  if ( !fSuccess )
  {
    debugCP( "abort" );
    _cdfileFree( pCDFile );
    rfbClientSetClientData( client, _TAG_FILE_DATA, NULL );
  }

  return fSuccess;
}

BOOL fxAbortFileTransfer(rfbClient* client)
{
  PFXCDFILE  pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                              _TAG_FILE_DATA );

  if ( pCDFile == NULL )
  {
    debugCP( "No FXCDFILE data. Is file receiving in progress?" );
    return FALSE;
  }

  if ( pCDFile->fSendReady )
  {
    pCDFile->fSendAbort = TRUE;
    return TRUE;
  }

  if ( !_sendRFBFilexferMsg( client, rfbAbortFileTransfer, 0, 0, 0, NULL ) )
    return FALSE;

  fxClean( client );
  return TRUE;
}

BOOL fxDelete(rfbClient* client, PSZ pszRemoteName)
{
  PSZ   pszEnc   = cscConvStr( client, CSC_LOCALTOREMOTE, pszRemoteName );
  PSZ   pszName  = pszEnc == NULL ? pszRemoteName : pszEnc;
  BOOL  fSuccess = _sendRFBFilexferMsg( client, rfbCommand, rfbCFileDelete, 0,
                                        strlen( pszName ), pszName );

  if ( pszEnc != NULL )
    cscFree( pszEnc );

  return fSuccess;
}

BOOL fxMkDir(rfbClient* client, PSZ pszRemoteName)
{
  PSZ   pszEnc   = cscConvStr( client, CSC_LOCALTOREMOTE, pszRemoteName );
  PSZ   pszName  = pszEnc == NULL ? pszRemoteName : pszEnc;
  BOOL  fSuccess = _sendRFBFilexferMsg( client, rfbCommand, rfbCDirCreate, 0,
                                        strlen( pszName ), pszName );

  if ( pszEnc != NULL )
    cscFree( pszEnc );

  return fSuccess;
}

BOOL fxRename(rfbClient* client, PSZ pszRemoteName, PSZ pszNewName)
{
  PSZ   pszEncName = cscConvStr( client, CSC_LOCALTOREMOTE, pszRemoteName );
  PSZ   pszName    = pszEncName == NULL ? pszRemoteName : pszEncName;
  PSZ   pszEncNew  = cscConvStr( client, CSC_LOCALTOREMOTE, pszNewName );
  PSZ   pszNew     = pszEncNew == NULL ? pszNewName : pszEncNew;
  PSZ   pszNames   = alloca( strlen( pszName ) + strlen( pszNew ) + 2 );
  LONG  cbNames;
  BOOL  fSuccess;

  if ( pszNames == NULL )
  {
    debugCP( "Not enough stack space" );
    fSuccess = FALSE;
  }
  else
  {
    cbNames = sprintf( pszNames, "%s*%s", pszEncName, pszEncNew );
    fSuccess = _sendRFBFilexferMsg( client, rfbCommand, rfbCFileRename, 0,
                                    cbNames, pszNames );
  }

  if ( pszEncName != NULL )
    cscFree( pszEncName );

  if ( pszEncNew != NULL )
    cscFree( pszEncNew );

  return fSuccess;
}

// Sends file transfer-offer request
BOOL fxSendFile(rfbClient* client, PSZ pszRemoteName, PSZ pszLocalName)
{
  PFXCDFILE     pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                             _TAG_FILE_DATA );
  ULONG         hFile, ulAction, ulRC, ulSizeHTmp;
  FILESTATUS3L  stInfo;
  LONG          cbBuf = strlen( pszRemoteName ) + 24;
  PCHAR         pcBuf = alloca( cbBuf );
  struct tm     sTM;
  time_t        timeFile;
  CHAR          acTime[64];
  LONGLONG      llSize = { 0, 0 };
  BOOL          fSuccess;
  PSZ           pszEnc;

  if ( pcBuf == NULL )
  {
    debugCP( "Not enough stack space" );
    return FALSE;
  }

  if ( pCDFile != NULL )
  {
    debugCP( "File transfering in progress" );
    return FALSE;
  }

  ulRC = DosOpenL( pszLocalName, &hFile, &ulAction, llSize, FILE_NORMAL,
                   OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                   OPEN_FLAGS_FAIL_ON_ERROR | OPEN_FLAGS_SEQUENTIAL |
                   OPEN_SHARE_DENYWRITE | OPEN_ACCESS_READONLY, NULL );
  if ( ulRC != NO_ERROR )
  {
    debug( "Can't open file for reading: %s", pszLocalName );
    return FALSE;
  }  

  // Make GMT time string for the local file.
  DosQueryFileInfo( hFile, FIL_STANDARDL, &stInfo, sizeof(stInfo) );
  sTM.tm_year  = stInfo.fdateLastWrite.year + 1980 - 1900;
  sTM.tm_mon   = stInfo.fdateLastWrite.month - 1;
  sTM.tm_mday  = stInfo.fdateLastWrite.day;
  sTM.tm_hour  = stInfo.ftimeLastWrite.hours;
  sTM.tm_min   = stInfo.ftimeLastWrite.minutes;
  sTM.tm_sec   = stInfo.ftimeLastWrite.twosecs * 2;
  sTM.tm_wday  = -1;
  sTM.tm_yday  = -1;
  sTM.tm_isdst = 0;
  timeFile = mktime( &sTM );
  strftime( acTime, sizeof(acTime), "%m/%d/%Y %H:%M", gmtime( &timeFile ) );
  // Build string for the request: filename,mm/dd/yyyy hh:mm
  cbBuf = sprintf( pcBuf, "%s,%s", pszRemoteName, acTime );
//  debug( "Remote file name: %s", pcBuf );

  pCDFile = calloc( 1, sizeof(FXCDFILE) );
  if ( pCDFile == NULL )
  {
    debugCP( "Not enough memory" );
    return FALSE;
  }

  pCDFile->hFile      = hFile;
  pCDFile->fRecv      = FALSE;
  pCDFile->pszFName   = strdup( pszRemoteName );
  if ( pCDFile->pszFName == NULL )
  {
    debugCP( "abort" );
    _cdfileFree( pCDFile );
    return FALSE;
  }

  rfbClientSetClientData( client, _TAG_FILE_DATA, pCDFile );
  ulSizeHTmp = rfbClientSwap32IfLE( ((PULL2UL)&stInfo.cbFile)->ulHigh );

  pszEnc = cscConvStr( client, CSC_LOCALTOREMOTE, pszRemoteName );
  if ( pszEnc != NULL )
  {
    pcBuf = pszEnc;
    cbBuf = strlen( pszEnc );
  }

  fSuccess = _sendRFBFilexferMsg( client, rfbFileTransferOffer, 0,
                                  ((PULL2UL)&stInfo.cbFile)->ulLow,
                                  cbBuf, pcBuf ) &&
             WriteToRFBServer( client, (char *)&ulSizeHTmp, sizeof(ULONG) );

  if ( pszEnc != NULL )
    cscFree( pszEnc );
  
  if ( !fSuccess )
  {
    debugCP( "abort" );
    rfbClientSetClientData( client, _TAG_FILE_DATA, NULL );
    _cdfileFree( pCDFile );
  }

  return fSuccess;
}


int fxWaitForMessage(rfbClient* client, unsigned int uiUSecs)
{
  PFXCDFILE  pCDFile = (PFXCDFILE)rfbClientGetClientData( client,
                                                              _TAG_FILE_DATA );
  PCLNTCONN  pCC = (PCLNTCONN)rfbClientGetClientData( client, NULL );
  HWND       hwnd = ccSetWindow( pCC, NULLHANDLE ); // NULLHANDLE - only query handle.
  fd_set     fdsRead;
  struct     timeval stTV;
  int        iRC;

  if ( client->serverPort==-1 )
    /* playing back vncrec file */
    return 1;

  if ( ( pCDFile != NULL ) && pCDFile->fSendReady )
  {
    ULONG    cbActual;
    ULONG    ulRC;
    CHAR     acBuf[sz_rfbBlockSize];

    while( !pCDFile->fSendAbort )
    {
      ulRC = DosRead( pCDFile->hFile, &acBuf, sizeof(acBuf), &cbActual );
      if ( ulRC != NO_ERROR )
      {
        debug( "DosRead(), rc = %u", ulRC );
        WinSendMsg( hwnd, WM_VNC_FILEXFER,
                    MPFROM2SHORT(CFX_SEND_READ_FAIL,
                                 strlen( pCDFile->pszFName )),
                    MPFROMP(pCDFile->pszFName) );
        fxClean( client );
        pCDFile = NULL;
        break;
      }
      else if ( cbActual == 0 )
      {
        debugCP( "End of file" );
        _sendRFBFilexferMsg( client, rfbEndOfFile, 0, 0, 0, NULL );
        fxClean( client );
        pCDFile = NULL;
        WinSendMsg( hwnd, WM_VNC_FILEXFER, MPFROM2SHORT(CFX_SEND_END_OF_FILE,0),
                    0 );
        break;
      }
      else if ( _sendRFBFilexferMsg( client, rfbFilePacket, 0, 0, cbActual,
                                     acBuf ) )
      {
        WinSendMsg( hwnd, WM_VNC_FILEXFER, MPFROM2SHORT(CFX_SEND_CHUNK,0),
                    MPFROMLONG(cbActual) );

        // Have incoming data?
        FD_ZERO( &fdsRead );
        FD_SET( client->sock, &fdsRead );
        stTV.tv_sec  = 0;
        stTV.tv_usec = 0;
        iRC = select( client->sock + 1, &fdsRead, NULL, NULL, &stTV );
        if ( iRC != 0 )
          return iRC;
      }
      else
        pCDFile->fSendAbort = TRUE;
    } // while( !pCDFile->fSendAbort )

    if ( ( pCDFile != NULL ) && pCDFile->fSendAbort )
    {
      _sendRFBFilexferMsg( client, rfbAbortFileTransfer, 0, 0, 0, NULL );
      fxClean( client );
    }
  }

  FD_ZERO( &fdsRead );
  FD_SET( client->sock, &fdsRead );
  stTV.tv_sec  = uiUSecs / 1000000;
  stTV.tv_usec = uiUSecs % 1000000;
  iRC = select( client->sock + 1, &fdsRead, NULL, NULL, &stTV );

  return iRC;
}
