#include <sys/socket.h>
#include <types.h>
#include <unistd.h>
#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_DOSMODULEMGR
#define INCL_DOSSEMAPHORES
#define INCL_GPI
#define INCL_WIN
#define INCL_DOSEXCEPTIONS
#define INCL_DOSDEVICES
#define INCL_DOSDEVIOCTL
#include <os2.h>
#include <../pmhook/vncshook.h>
#include "config.h"
#include "srvwin.h"
#include "gui.h"
#include "log.h"
#include "resource.h"
#include "rfbsrv.h"
#include <chatwin.h>
#include <utils.h>
#include <vnckbd.h>
#include <debug.h>

extern HAB             habSrv;             // from main.c
extern HMQ             hmqSrv;             // from main.c
extern HWND            hwndSrv;            // from main.c
extern PMHINIT         pmhInit;            // from main.c
extern PMHDONE         pmhDone;            // from main.c
extern PMHPOSTEVENT    pmhPostEvent;       // from main.c
extern HWND            hwndLastUnderPtr;   // from main.c
extern CHAR            acWinUnderPtrClass[128]; // from main.c
extern HWND            hwndGUI;            // from gui.c


static PSZ              pszDesktopName        = NULL;
static rfbScreenInfoPtr prfbScreen            = NULL;
static HPS              hpsScreen             = NULLHANDLE;
static HPS              hpsMem                = NULLHANDLE;
static rfbCursor        stCursor;
static HPOINTER         hptrLastPointer       = NULLHANDLE;
static PXKBDMAP         pKbdMap               = NULL;
static RECTL            rectlDesktop;
static ULONG            cClients              = 0;
static CHAR             acPassword[9]         = { 0 };
static CHAR             acViewOnlyPassword[9] = { 0 };
static PSZ              apszPasswords[3]      = { acPassword,
                                                  acViewOnlyPassword, NULL };
static ACL              stACL;
static PSZ              pszProgOnLogon        = NULL;
static PSZ              pszProgOnGone         = NULL;
static PSZ              pszProgOnCAD          = NULL;

static ULONG            cLogonClients         = 0;
static ULONG            ulMaxLogonClients     = 0;
static rfbClientPtr     *paLogonClients       = NULL;

static HFILE            hDriverVNCKBD         = NULLHANDLE;
static HFILE            hDriverKBD            = NULLHANDLE;

// Callback function for utilStrFormat(). It uses to build command to run
// external program.
static ULONG _cbSubstProg(CHAR chKey, ULONG cbBuf, PCHAR pcBuf, PVOID pData)
{
  rfbClientPtr         prfbClient = (rfbClientPtr)pData;
  PSZ                  pszResult = NULL;
  ULONG                cbResult;

  switch( chKey )
  {
    case 'h':          // %h: host name / ip-address.
      pszResult = prfbClient->host;
      break;

    case 'r':          // %r: 1 - reverse connection, 0 - otherwise.
      pszResult = prfbClient->reverseConnection ? "1" : "0";
      break;

    case 'v':          // %v: 1 - view-only client, 0 - otherwise.
      pszResult = prfbClient->viewOnly ? "1" : "0";
      break;

    case 'i':          // %i: answer from on-connect external program.
      if ( ( prfbClient->clientData != NULL ) &&
           ( ((PCLIENTDATA)prfbClient->clientData)->pszExtProgId != NULL ) )
        pszResult = ((PCLIENTDATA)prfbClient->clientData)->pszExtProgId;
      break;

    case 'o':          // %o: 1 - have other logged-in clients, 0 - otherwise.
      {
        rfbClientIteratorPtr prfbIter;
        rfbClientPtr         prfbClientScan;

        prfbIter = rfbGetClientIterator( prfbScreen );

        while( ( prfbClientScan = rfbClientIteratorNext( prfbIter ) ) )
        {
          if ( ( prfbClientScan != prfbClient ) &&
               ( ( prfbClientScan->state == RFB_INITIALISATION ) ||
                 ( prfbClientScan->state == RFB_NORMAL ) ) )
            // This is not current client and it authenticated.
            break;
        }

        rfbReleaseClientIterator( prfbIter );

        pszResult = prfbClientScan != NULL ? "1" : "0";
      }
      break;
  }

  if ( pszResult == NULL )
    return 0;

  cbResult = strlen( pszResult );
  if ( strlen( pszResult ) >= cbBuf )
    return 0;

  strcpy( pcBuf, pszResult );

  return cbResult;
}

/*
  VOID _progExecute(rfbClientPtr prfbClient, PSZ pszCommand,
                    ULONG cbOutputBuf, PCHAR pcOutputBuf)

  Runs external program (wait for) pszCommand, performs substitution for
  %-keys. Stores first line from stdout to pcOutputBuf if it's not a NULL.
*/

// Passes the string pszCommand to the command processor (CMD.EXE).
static LONG __execCmd(PSZ pszCommand)
{
  ULONG		ulRC;
  CHAR		acError[CCHMAXPATH];
  RESULTCODES	sResCodes;
  PCHAR		pcArg;
  PSZ		pszCmd;
  ULONG		cbCmd;
  ULONG		cbCommand;
  
  // Make arguments for CMD

  pszCmd = getenv( "COMSPEC" );
  cbCmd = strlen( pszCmd );
  cbCommand = strlen( pszCommand );
  pcArg = alloca( cbCmd + 5 /* ZERO " /c " */ + cbCommand + 2 /* ZERO ZERO */ );
  if ( pcArg == NULL )
  {
    debug( "Not enough stack size" );
    return -1;
  }
  memcpy( pcArg, pszCmd, cbCmd );
  pcArg[cbCmd++] = '\0';
  memcpy( &pcArg[cbCmd], " /c ", 4 );
  cbCmd += 4;
  memcpy( &pcArg[cbCmd], pszCommand, cbCommand );
  *((PUSHORT)&pcArg[cbCmd + cbCommand]) = 0;

  ulRC = DosExecPgm( acError, sizeof(acError), EXEC_SYNC, 
                     pcArg, NULL, &sResCodes, pcArg );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosExecPgm(,,,,,,%s), rc = %u", pcArg, ulRC );
    return -1;
  }

  return sResCodes.codeTerminate;
}

static VOID _progExecute(rfbClientPtr prfbClient, PSZ pszCommand,
                         ULONG cbOutputBuf, PCHAR pcOutputBuf)
{
  int        ahRead[2];
  int        hOldOut;
  int        iRC;
  CHAR       acCommand[CCHMAXPATH];

  if ( utilStrFormat( sizeof(acCommand), acCommand, -1, pszCommand,
                      _cbSubstProg, prfbClient ) <= 0 )
  {
    debug( "utilStrFormat() for \"%s\" failed", pszCommand );
    return;
  }

  if ( ( cbOutputBuf == 0 ) || ( pcOutputBuf == NULL ) )
  {
    debug( "#1 call system(\"%s\")", acCommand );
 //   system( acCommand );
    __execCmd( acCommand );
    return;
  }

  if ( pipe( ahRead ) != 0 )
  {
    debugCP( "Cannot create the pipe" );
    return;
  }

  hOldOut = dup( STDOUT_FILENO ); 
  if ( hOldOut == -1 )
  {
    debugCP( "dup() failed" );
    close( ahRead[0] );
    close( ahRead[1] );
    return;
  }

  dup2( ahRead[1], STDOUT_FILENO );
  close( ahRead[1] );

  debug( "#2 call system(\"%s\")", acCommand );
//  iRC = system( acCommand );
  __execCmd( acCommand );

  dup2( hOldOut, STDOUT_FILENO );
  close( hOldOut );

  iRC = read( ahRead[0], pcOutputBuf, cbOutputBuf - 1 );
  if ( iRC > 0 )
  {
    PCHAR  pcEOL = memchr( pcOutputBuf, '\n', iRC );

    if ( pcEOL == NULL )
      pcEOL = &pcOutputBuf[iRC + 1];

    *pcEOL =  '\0';
    utilStrTrim( pcOutputBuf );
    debug( "Program output: %s", pcOutputBuf );
  }
  else
  {
    debugCP( "No any data was received" );
    *pcOutputBuf = '\0';
  }

  if ( close( ahRead[0] ) == -1 )
    debug( "Cannot close ahRead[0]" );
}

/*
  LONG _queryFrgnProgType()

  Returns foreground program type (PROG_FULLSCREEN, PROG_WINDOWABLEVIO,
  PROG_WINDOWEDVDM, PROG_PM, PROG_DEFAULT) or -1 on error.
*/
static LONG _queryProgType(HWND hwnd)
{
  HSWITCH    hSw;
  ULONG      ulRC;
  SWCNTRL    stSwCntrl;

  hSw = WinQuerySwitchHandle( hwnd, 0 );
  if ( hSw == NULLHANDLE )
  {
    debug( "WinQuerySwitchHandle(%lu,0) returns NULLHANDLE", hwnd );
    return -1;
  }

  ulRC = WinQuerySwitchEntry( hSw, &stSwCntrl );
  if ( ulRC != NO_ERROR )
  {
    debug( "WinQuerySwitchEntry(), rc = %lu\n", ulRC );
    return -1;
  }

  return stSwCntrl.bProgType;
}

static LONG _queryFrgnProgType()
{
  HWND       hwndActive = WinQueryActiveWindow( HWND_DESKTOP );

  if ( hwndActive == NULLHANDLE )
  {
    debug( "WinQueryActiveWindow() returns NULLHANDLE" );
    return -1;
  }

  return _queryProgType( hwndActive );
}

static BOOL _isFullscreenMode()
{
  ULONG      ulRC, cbParam, cbData;
  UCHAR      ucScrnGrp;

  if ( hDriverVNCKBD != NULLHANDLE )
  {
    ulRC = DosDevIOCtl( hDriverVNCKBD, VNCKBDIOCTL_CATECORY,
                        VNCKBDIOCTL_FN_QUERYSCRGR,
                        NULL, 0, &cbParam, &ucScrnGrp,
                        sizeof(UCHAR), &cbData );
    if ( ulRC == NO_ERROR )
      return ucScrnGrp != 1;

    debug( "DosDevIOCtl( VNCKBD$, 0x92, 0x02, ... ), rc = %u", ulRC );
  }

  return _queryFrgnProgType() == PROG_FULLSCREEN;
}

/*
  BOOL _jumpToDesktop(ULONG ulMethod)

  Switch to the desktop.
  ulMethod: (0) _JTDT_ALTESC, (1) _JTDT_CTRLESC, (2) _JTDT_SWITCH,
            (3) _JTDT_ANY.

  _JTDT_ALTESC, _JTDT_CTRLESC: Sends Alt+ESC or Ctrl+ESC via VNCKBD$.
  _JTDT_SWITCH: Uses Win*() API to switch on the desktop.
  _JTDT_ANY: Will try to switch on the desktop with first three methors only
  if current mode is fullscreen. Current mode will be checked before each
  attempt. Returns TRUE if current mode is not fullscreen.

  Returns TRUE on success.
*/

#define _JTDT_ALTESC   0
#define _JTDT_CTRLESC  1
#define _JTDT_SWITCH   2
#define _JTDT_ANY      3

static BOOL _jumpToDesktop(ULONG ulMethod)
{
  static USHORT ausSC[2][4] = { { 0x38, 0x01, 0x81, 0xB8 },    // Alt+ESC
                                { 0x1D, 0x01, 0x81, 0x9D } };  // Ctrl+ESC
  BOOL       fSuccess = FALSE;
  ULONG      ulIdx;

  switch( ulMethod )
  {
    case _JTDT_ALTESC:
    case _JTDT_CTRLESC:
      if ( hDriverVNCKBD != NULLHANDLE )
      {
        ULONG      cbParam, cbData;

        if ( DosDevIOCtl( hDriverVNCKBD, VNCKBDIOCTL_CATECORY,
                          VNCKBDIOCTL_FN_SENDSCAN, ausSC[ulMethod],
                          8, &cbParam, NULL, 0, &cbData ) == 0 )
          fSuccess = TRUE;
      }
      break;

    case _JTDT_SWITCH:
      {
        ULONG     cbItems  = WinQuerySwitchList( habSrv, NULL, 0 );
        ULONG     cbBuf    = ( cbItems * sizeof(SWENTRY) ) + sizeof(HSWITCH);
        PSWBLOCK  pSwBlock = malloc( cbBuf );
        HSWITCH   hSw      = NULLHANDLE;

        if ( pSwBlock == NULL )
          return FALSE;

        // Select target to switch: Desktop, "Ctrl-ESC" window or any PM prog.
        WinQuerySwitchList( habSrv, pSwBlock, cbBuf );
        for( ulIdx = 0; ulIdx < pSwBlock->cswentry; ulIdx++ )
        {
          if ( pSwBlock->aswentry[ulIdx].swctl.bProgType == PROG_DEFAULT &&
               strcmp( pSwBlock->aswentry[ulIdx].swctl.szSwtitle,
                       "Switch to" ) == 0 )
          {
            hSw = pSwBlock->aswentry[ulIdx].hswitch;
          }
          else if ( pSwBlock->aswentry[ulIdx].swctl.bProgType == PROG_PM )
          {
            if ( strcmp( pSwBlock->aswentry[ulIdx].swctl.szSwtitle,
                         "Desktop" ) == 0 )
            {
              hSw = pSwBlock->aswentry[ulIdx].hswitch;
              break;
            }

            if ( hSw == NULLHANDLE )
              hSw = pSwBlock->aswentry[ulIdx].hswitch;
          }
        }

        free( pSwBlock );

        if ( ( hSw != NULLHANDLE ) && ( WinSwitchToProgram( hSw ) == 0 ) )
          fSuccess = TRUE;
      }
      break;

    case _JTDT_ANY:
      for( ulIdx = hDriverVNCKBD != NULLHANDLE ? _JTDT_ALTESC : _JTDT_SWITCH;
           ; ulIdx++ )
      {
        if ( !_isFullscreenMode() )
        {
          fSuccess = TRUE;
          break;
        }

        if ( ulIdx == _JTDT_ANY )
          break;

        if ( _jumpToDesktop( ulIdx ) )
          DosSleep( 150 );
      }
      break;
  }

  return fSuccess;
}


// Libvncserver callback functions
// -------------------------------

static void _cbLog(const char *pcFormat, ...)
{
  va_list    args;

  va_start( args, pcFormat );
  logVArg( 3, (PSZ)pcFormat, args );
  va_end( args );
}

static void _cbLogErr(const char *pcFormat, ...)
{
  va_list    args;

  va_start( args, pcFormat );
  logVArg( 2, (PSZ)pcFormat, args );
  va_end( args );
}

static void _cbClientGone(rfbClientPtr prfbClient)
{
  ULONG      ulIdx;

  if ( ((PCLIENTDATA)prfbClient->clientData)->hwndChat != NULLHANDLE )
  {
    WMCHATSYS          stWMChatSys;
    CHAR               acBuf[64];

    stWMChatSys.cbText = WinLoadMessage( habSrv, NULLHANDLE,
                           IDMSG_USER_DISCONNECT, sizeof(acBuf), acBuf );
    stWMChatSys.fAllow = FALSE;
    stWMChatSys.pcText = acBuf;

    WinSendMsg( ((PCLIENTDATA)prfbClient->clientData)->hwndChat, WMCHAT_MESSAGE,
                MPFROMLONG(CWMT_SYSTEM), MPFROMP(&stWMChatSys) );
  }

  // Run external program.
  if ( ( pszProgOnGone != NULL ) &&
       ( ( prfbClient->state == RFB_INITIALISATION ) ||
         ( prfbClient->state == RFB_NORMAL ) ) )
    // This client was authenticated.
    _progExecute( prfbClient, pszProgOnGone, 0, NULL );

  if ( cClients == 0 )
    debug( "WTF?! clients counter already is 0" );
  else
  {
    cClients--;

    if ( cClients == 0 )
    {
      // Last client is gone - deactivate hooks.
      pmhDone( habSrv );
    }

    // Send message to the hidden server window.
    WinSendMsg( hwndSrv, WMSRV_CLNT_NUM_CHANGED, MPFROM2SHORT(cClients,FALSE),
                MPFROMP(prfbClient) );
  }

  logFmt( 0, "Client %s [%X] gone (was%s logged in%s), total clients: %u",
          prfbClient->host,
          prfbClient,
          prfbClient->state < RFB_NORMAL ? " not" : "",
          prfbClient->viewOnly ? ", view-only" : "",
          cClients );

  if ( ((PCLIENTDATA)prfbClient->clientData)->pszExtProgId != NULL)
    free( ((PCLIENTDATA)prfbClient->clientData)->pszExtProgId );

  free( prfbClient->clientData );
  prfbClient->clientData = NULL;

  // Remove client from "wait for authentication" list (if it was listed).

  for( ulIdx = 0; ulIdx < cLogonClients; ulIdx++ )
  {
    if ( paLogonClients[ulIdx] == prfbClient )
    {
      cLogonClients--;
      paLogonClients[ulIdx] = paLogonClients[cLogonClients];
      break;
    }
  }
}

static enum rfbNewClientAction _cbClientNew(rfbClientPtr prfbClient)
{
  struct sockaddr_in   stAddr;
  socklen_t            cbAddr = sizeof(stAddr);

  if ( !prfbClient->reverseConnection )
  {
    // Check incoming (only) client connections with access list.

    if ( getpeername( prfbClient->sock, (struct sockaddr *)&stAddr, &cbAddr ) ==
         -1 )
      debug( "getpeername() failed" );
    else if ( (aclCheck( &stACL, &stAddr.sin_addr ) & 0x01) == 0 )
    {
      logFmt( 1, "Client %s forbidden (ACL)", prfbClient->host );
      return RFB_CLIENT_REFUSE;
    }
  }

  prfbClient->clientGoneHook = _cbClientGone;
  prfbClient->clientData     = calloc( sizeof(CLIENTDATA), 1 );
  time( &((PCLIENTDATA)prfbClient->clientData)->timeConnect );
  xkMPFromKeysymStart( &((PCLIENTDATA)prfbClient->clientData)->stXKFromKeysym );

  cClients++;

  logFmt( 0, "Client %s [%X] connected%s, total clients: %u",
          prfbClient->host,
          prfbClient,
          prfbClient->reverseConnection ? " (reverse connection)" : "",
          cClients );

  if ( cClients == 1 )
  {
    // First client connected - activate hooks.
    if ( !pmhInit( habSrv, hmqSrv ) )
      debug( "pmhInit() failed" );

    rfbsUpdateScreen( rectlDesktop );
  }

  // Insert client into "wait for authentication" list.

  do
  {
    if ( cLogonClients == ulMaxLogonClients )
    {
      // Expand list.
      rfbClientPtr *paNewList = realloc( paLogonClients,
                                  (cLogonClients + 4) * sizeof(rfbClientPtr) );

      if ( paNewList == NULL )
        break;

      paLogonClients = paNewList;
      ulMaxLogonClients += 4;
    }
    paLogonClients[cLogonClients] = prfbClient;
    cLogonClients++;
  }
  while( FALSE );

  // Send message to the hidden server window.
  WinSendMsg( hwndSrv, WMSRV_CLNT_NUM_CHANGED, MPFROM2SHORT(cClients,TRUE),
              MPFROMP(prfbClient) );

  return RFB_CLIENT_ACCEPT;
}

static VOID _wheelEvent(LONG lX, LONG lY, BOOL fWheelUp)
{
// PM123 commands (for WM_COMMAND)
#define IDM_M_VOL_RAISE     519
#define IDM_M_VOL_LOWER     520

  HWND       hwndUnderPtr;
  RECTL      rectl;
  CHAR       acWinUnderPtrClass[128];

  rectl.xLeft    = lX;
  rectl.yBottom  = lY;
  hwndUnderPtr = WinWindowFromPoint( HWND_DESKTOP, (PPOINTL)&rectl, TRUE );

  if ( WinQueryClassName( hwndUnderPtr, sizeof(acWinUnderPtrClass),
                          acWinUnderPtrClass ) == 0 )
    return;

  if ( strcmp( acWinUnderPtrClass, "PM123" ) == 0 )
    WinSendMsg( hwndUnderPtr, WM_COMMAND,
                MPFROMSHORT(fWheelUp ? IDM_M_VOL_RAISE : IDM_M_VOL_LOWER),
                MPFROM2SHORT(CMDSRC_OTHER,TRUE) );
  else
  {
    ULONG    ulMsg      = WM_CHAR;
    UCHAR    uchScan    = fWheelUp ? 0x48 : 0x50;
    USHORT   usChar     = fWheelUp ? 0x4800 : 0x5000;
    USHORT   usVK       = fWheelUp ? VK_UP : VK_DOWN;
    MPARAM   mp1up, mp2up, mp1down, mp2down;

    mp1down = MPFROMSH2CH( KC_SCANCODE | KC_VIRTUALKEY, 1, uchScan );
    mp2down = MPFROM2SHORT( usChar, usVK );
    mp1up   = MPFROMSH2CH( KC_SCANCODE | KC_VIRTUALKEY | KC_KEYUP | KC_PREVDOWN
                           , 1, uchScan );
    mp2up   = mp2down;

/*
    Future: разобраться почему в ВИО не шлёт.

    if ( strcmp( acWinUnderPtrClass, "Shield" ) == 0 )
    {
      //hwndUnderPtr = WinQueryWindow( hwndUnderPtr, QW_PARENT );

      // Target program in VIO mode - prepare mp2 for WM_VIOCHAR.
      ulMsg = WM_VIOCHAR;
      xkMakeMPForVIO( mp1down, &mp2down );
      xkMakeMPForVIO( mp1up, &mp2up );
    }
*/

    WinPostMsg( hwndUnderPtr, ulMsg, mp1down, mp2down );
    WinPostMsg( hwndUnderPtr, ulMsg, mp1up, mp2up );
  }

  WinQueryWindowRect( hwndUnderPtr, &rectl );
  WinMapWindowPoints( hwndUnderPtr, HWND_DESKTOP, (PPOINTL)&rectl, 2 );
  rfbsUpdateScreen( rectl );
}

static void _cbPtrEvent(int buttonMask, int x, int y, rfbClientPtr prfbClient)
{
  PCLIENTDATA          pClientData = (PCLIENTDATA)prfbClient->clientData;
  ULONG                ulVal;
  ULONG                ulMsg = 0;
  SHORT                sY = prfbScreen->height - y - 1;

  WinSetPointerPos( HWND_DESKTOP, x, sY );

  if ( pClientData->ulBtnFlags == buttonMask )
    return;

  ulVal = buttonMask & rfbButton1Mask;
  if ( ulVal != (pClientData->ulBtnFlags & rfbButton1Mask) )
    ulMsg = ulVal != 0 ? WM_BUTTON1DOWN : WM_BUTTON1UP;

  ulVal = buttonMask & rfbButton2Mask;
  if ( ulVal != (pClientData->ulBtnFlags & rfbButton2Mask) )
    ulMsg = ulVal != 0 ? WM_BUTTON3DOWN : WM_BUTTON3UP;

  ulVal = buttonMask & rfbButton3Mask;
  if ( ulVal != (pClientData->ulBtnFlags & rfbButton3Mask) )
    ulMsg = ulVal != 0 ? WM_BUTTON2DOWN : WM_BUTTON2UP;

  ulVal = buttonMask & rfbButton4Mask;
  if ( ulVal != (pClientData->ulBtnFlags & rfbButton4Mask) )
  {
    // Wheel: Up
    _wheelEvent( x, sY, TRUE );
    return;
  }

  ulVal = buttonMask & rfbButton5Mask;
  if ( ulVal != (pClientData->ulBtnFlags & rfbButton5Mask) )
  {
    // Wheel: Down
    _wheelEvent( x, sY, FALSE );
    return;
  }

  switch( ulMsg )
  {
    case 0: break;
    case WM_BUTTON1DOWN:
    case WM_BUTTON2DOWN:
    case WM_BUTTON3DOWN:
      _jumpToDesktop( _JTDT_ANY );
    default:
      sY = prfbScreen->height - y - 1;
      pmhPostEvent( habSrv, ulMsg, MPFROM2SHORT(x, sY), 0 );
  }

  pClientData->ulBtnFlags = buttonMask;
}

// Sends OS/2 PM event scancode to the system as "real" HW scancode.
static VOID _SendEvHWScan(ULONG ulScan, BOOL fDown)
{
  static struct _EXT {
    UCHAR    ucEvScan;
    USHORT   usHWScan;
  } aExtKeys[] = {
    { 0x68, 0x52E0 }, // Insert
    { 0x60, 0x47E0 }, // Home
    { 0x62, 0x49E0 }, // PgUp
    { 0x69, 0x53E0 }, // Delete
    { 0x65, 0x4FE0 }, // End
    { 0x67, 0x51E0 }, // PgDn
    { 0x61, 0x48E0 }, // Up
    { 0x63, 0x4BE0 }, // Left
    { 0x66, 0x50E0 }, // Down
    { 0x64, 0x4DE0 }, // Right
    { 0x5A, 0x1CE0 }, // Numpad Enter
    { 0x5B, 0x1DE0 }, // Right Ctrl
    { 0x7E, 0x5BE0 }, // Left Win
    { 0x7F, 0x5CE0 }, // Right Win
    { 0x7C, 0x5DE0 }, // Menu (Win kbd)
    { 0x5E, 0x38E0 }, // Right Alt
    { 0x5C, 0x35E0 }  // </> (Num pad)
  };
  static USHORT ausPause[] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };
  static USHORT ausBreak[] = { 0x46E0, 0xC6E0 };

  PUSHORT    pusScan;
  USHORT     cbScan;
  ULONG      cbParam, cbData, ulIdx;

  if ( ulScan == 0x5F ) // Pause (no break code).
  {
    if ( !fDown )
      // Skip event on key release.
      return;

    pusScan = ausPause;
    cbScan = sizeof(ausPause);
  }
  else if ( ulScan == 0x6E ) // Ctrl+Pause (Break)
  {
    if ( !fDown )
      // Skip event on key release.
      return;

    pusScan = ausBreak;
    cbScan = sizeof(ausBreak);
  }
  else
  {
    // Translate some OS/2 specified scan codes to HW scan codes.
    for( ulIdx = 0; ulIdx < ARRAYSIZE(aExtKeys); ulIdx++ )
    {
      if ( aExtKeys[ulIdx].ucEvScan == ulScan )
      {
        ulScan = aExtKeys[ulIdx].usHWScan;
        break;
      }
    }

    if ( !fDown )
    {
      // Set "release" bit.
      if ( (ulScan & 0xFF00) == 0 )
        ulScan |= 0x80;          // For 1-byte code.
      else
        ulScan |= 0x8000;        // For 2-byte code.
    }
    pusScan = (PUSHORT)&ulScan;
    cbScan = 2;
  }

  DosDevIOCtl( hDriverVNCKBD, VNCKBDIOCTL_CATECORY, VNCKBDIOCTL_FN_SENDSCAN,
               pusScan, cbScan, &cbParam, NULL, 0, &cbData );
}

static void _cbKeyEvent(rfbBool down, rfbKeySym key, rfbClientPtr prfbClient)
{
  PCLIENTDATA          pClientData = (PCLIENTDATA)prfbClient->clientData;
  PXKFROMKEYSYM        pXKFromKeysym = &pClientData->stXKFromKeysym;
  MPARAM               mp1, mp2;
  ULONG                ulMsg = WM_CHAR;
  ULONG                ulIdx;
  LONG                 lProgType;

  // Switch to the desktop (_JTDT_ANY - only when full-screen is current mode).
  _jumpToDesktop( _JTDT_ANY );

  if ( pKbdMap == NULL )
    return;

  if ( !xkMPFromKeysym( pKbdMap, key, down, pXKFromKeysym ) )
  {
    debug( "Keysym 0x%X cannot be converted to the system message", key );
    return;
  }

  // Detect VIO sessions.

  lProgType =  _queryFrgnProgType();
  // Hm... we can be still in full-screen? Perhaps this is an extra precaution.
  if ( ( lProgType == PROG_FULLSCREEN ) && _jumpToDesktop( _JTDT_SWITCH ) )
    lProgType =  _queryFrgnProgType();

  if ( lProgType != -1 )
  {
    if ( lProgType == PROG_WINDOWABLEVIO || lProgType == PROG_WINDOWEDVDM )
    {
      CHAR   acBuf[256];

      WinQueryClassName( WinQueryWindow( HWND_DESKTOP, QW_TOP ),
                         sizeof(acBuf), acBuf );

      if ( strcmp( acBuf, "#4" ) != 0 )
        // Not system menu for VIO/VDM window.
        ulMsg = WM_VIOCHAR;
    }
    else if ( ( lProgType != PROG_PM ) && ( lProgType != PROG_DEFAULT ) )
      ulMsg = WM_VIOCHAR;
  }

  // Send keyboard events to the system.

  for( ulIdx = 0; ulIdx < pXKFromKeysym->cOutput; ulIdx++ )
  {
    mp1 = pXKFromKeysym->aOutput[ulIdx].mp1;
    mp2 = pXKFromKeysym->aOutput[ulIdx].mp2;

    /*
     *  Special situations.
     */

    switch( SHORT2FROMMP(mp2) )
    {
      case VK_DELETE:
        if ( ( (SHORT1FROMMP(mp1) & KC_KEYUP) == 0 ) &&
             ( // 0x1D - Left Ctrl, 0x5B - Right Ctrl, Mask 0x01 - pressed.
               ( ((pXKFromKeysym->abState[0x1D] | pXKFromKeysym->abState[0x5B]) &
                 0x01) != 0 ) &&
               // 0x38 - Alt, 0x5E - AltGr, Mask 0x01 - pressed.
               ( ((pXKFromKeysym->abState[0x38] | pXKFromKeysym->abState[0x5E]) &
                 0x01) != 0 )
             ) &&
             ( pszProgOnCAD != NULL )
           )
        {
          // Run Ctrl-Alt-Del handler (external program).
          _progExecute( prfbClient, pszProgOnCAD, 0, NULL );
          continue;
        }
        break;
    }

    /*
     *  Use drivers VNCKBD$ and KBD$ for some events.
     */

    if ( (SHORT1FROMMP(mp1) & KC_SCANCODE) != 0 )
    {
      UCHAR            ucEvScan = SHORT2FROMMP(mp1) >> 8;
      BOOL             fDown = (SHORT1FROMMP(mp1) & KC_KEYUP) == 0;

      if ( hDriverVNCKBD != NULLHANDLE )
      {
        // Send some keys via VNCKBD$ driver.

        switch( ucEvScan )
        {
          default:
            // Send any key combinations with Ctrl or Alt to VNCKBD$.
            if ( ((pXKFromKeysym->abState[0x1D] | pXKFromKeysym->abState[0x5B] |
                   pXKFromKeysym->abState[0x38] | pXKFromKeysym->abState[0x5E])
                  & 0x01) == 0 )
              break;
          case 0x0F: // Tab
          case 0x2A: // Left Shift
          case 0x36: // Right Shift
          case 0x1D: // Left Ctrl
          case 0x5B: // Right Ctrl
          case 0x38: // Alt
          case 0x5E: // AltGr
          case 0x6E: // Ctrl+Break
          case 0x5F: // Pause/Break
          case 0x01: // ESC
          case 0x46: // Scroll Lock
            _SendEvHWScan( ucEvScan, fDown );
            continue;
        }
      }      // if ( hDriverVNCKBD != NULLHANDLE )

      if ( hDriverKBD != NULLHANDLE )
      {
        // Send some shift keys state over keyboard driver.

        USHORT         usShiftKey;
        ULONG          cbParam, cbData;

        switch( ucEvScan )
        {
          case 0x2A: usShiftKey = KBDST_SHIFT_LEFT_DOWN;  break;
          case 0x36: usShiftKey = KBDST_SHIFT_RIGHT_DOWN; break;
          case 0x1D: usShiftKey = KBDST_CTRL_LEFT_DOWN;   break;
          case 0x5B: usShiftKey = KBDST_CTRL_RIGHT_DOWN;  break;
          case 0x38: usShiftKey = KBDST_ALT_LEFT_DOWN;    break;
          case 0x5E: usShiftKey = KBDST_ALT_RIGHT_DOWN;   break;
          case 0x46: // Scroll Lock
            usShiftKey = KBDST_SCROLLLOCK_ON;
            if ( !fDown )
              continue;
            fDown = (pXKFromKeysym->abState[0x46] & 0x02) != 0;
            break;
          default:   usShiftKey = 0;
        }

        if ( usShiftKey != 0 )
        {
          SHIFTSTATE   stShiftState = { 0 };

          DosDevIOCtl( hDriverKBD, IOCTL_KEYBOARD, KBD_GETSHIFTSTATE,
                       NULL, 0, &cbParam,
                       &stShiftState, sizeof(stShiftState), &cbData );

          if ( fDown )
            stShiftState.fsState |= usShiftKey;
          else
            stShiftState.fsState &= ~usShiftKey;

          DosDevIOCtl( hDriverKBD, IOCTL_KEYBOARD, KBD_SETSHIFTSTATE,
                       &stShiftState, sizeof(stShiftState), &cbParam,
                       NULL, 0, &cbData );
          continue;
        }
      }
    }        // if ( hDriverKBD != NULLHANDLE )

    /*
     *  Post WM_CHAR / WM_VIOCHAR window event.
     */

    if ( ulMsg == WM_VIOCHAR )
      // Current program in VIO mode - prepare mp2 for WM_VIOCHAR.
      xkMakeMPForVIO( mp1, &mp2 );

/*    { XKEVENTSTR           stEvent;
      xkEventToStr( mp1, mp2, &stEvent );
      printf( "#%u: Flags: %s, Scan: %s, Char: 0x%X, VK: %s\n---\n", ulIdx,
              stEvent.acFlags, stEvent.acScan, SHORT1FROMMP(mp2), stEvent.acVK ); }*/

    // Post event over hook.
printf( "- %lu %lu %lu\n", ulMsg, mp1, mp2 );
    pmhPostEvent( habSrv, ulMsg, mp1, mp2 );
  } // for()
}

static void _cbSetTextChat(rfbClientPtr prfbClient, int iLen, char *pcText)
{
  PCLIENTDATA          pClientData = (PCLIENTDATA)prfbClient->clientData;
  WMCHATSYS            stWMChatSys;
  WMCHATMSG            stWMChatMsg;
  CHAR                 acBuf[255];

  switch( (unsigned int)iLen )
  {
    case rfbTextChatOpen:
      if ( ( pClientData->hwndChat == NULLHANDLE ) && ( hwndGUI != NULLHANDLE ) )
        // Open a new chat dialog.
        pClientData->hwndChat = (HWND)
          WinSendMsg( hwndGUI, WMGUI_CHAT_OPEN, MPFROMP( prfbClient ), 0 );

      if ( pClientData->hwndChat != NULLHANDLE )
      {
        ULONG          cBytes;

        // Set local user name for chat.
        WinSendMsg( pClientData->hwndChat, WMCHAT_MESSAGE,
                    MPFROMLONG(CWMT_LOCAL_NAME), APP_NAME );

        // Insert "system" message.
        stWMChatSys.cbText = WinLoadMessage( habSrv, NULLHANDLE,
                               IDMSG_USER_CHAT_OPEN, sizeof(acBuf), acBuf );
        stWMChatSys.pcText = acBuf;
        stWMChatSys.fAllow = TRUE;         // Enable user input in chat window.
        WinSendMsg( pClientData->hwndChat, WMCHAT_MESSAGE,
                    MPFROMLONG(CWMT_SYSTEM), MPFROMP(&stWMChatSys) );

        // Set chat window title.
        strcpy( acBuf, APP_NAME " - " );                     // "VNC Server - "
        cBytes = APP_NAME_LENGTH + 3 +
           WinQueryWindowText( pClientData->hwndChat,        // "Chat"
                               sizeof(acBuf) - APP_NAME_LENGTH - 6,
                               &acBuf[APP_NAME_LENGTH + 3] );
        *((PULONG)&acBuf[cBytes]) = 0x00202D20;              // " - "
        cBytes += 3;
        strlcpy( &acBuf[cBytes], prfbClient->host,           // ip-address
                 sizeof(acBuf) - APP_NAME_LENGTH - 3 );
        WinSetWindowText( pClientData->hwndChat, acBuf );

        // Show chat window.
        WinShowWindow( pClientData->hwndChat, TRUE );
      }
      break;

    case rfbTextChatClose:
    case rfbTextChatFinished:
      if ( ( pClientData->hwndChat != NULLHANDLE ) &&
           ( pClientData->iLastChatMsgType != rfbTextChatFinished ) )
      {
        stWMChatSys.cbText = WinLoadMessage( habSrv, NULLHANDLE,
                               IDMSG_USER_CHAT_LEFT, sizeof(acBuf), acBuf );
        stWMChatSys.pcText = acBuf;
        stWMChatSys.fAllow = FALSE;        // Disable user input in chat window.

        WinSendMsg( ((PCLIENTDATA)prfbClient->clientData)->hwndChat,
                    WMCHAT_MESSAGE,
                    MPFROMLONG(CWMT_SYSTEM), MPFROMP(&stWMChatSys) );
      }
      iLen = rfbTextChatFinished;
      break;

    default:
      if ( ( (unsigned int)iLen <= rfbTextMaxSize ) &&
           ( pClientData->hwndChat != NULLHANDLE ) )
      {
        // Insert remote user's mssage.
        stWMChatMsg.pszUser = prfbClient->host;
        stWMChatMsg.pcText  = pcText;
        stWMChatMsg.cbText  = iLen;

        WinSendMsg( pClientData->hwndChat, WMCHAT_MESSAGE,
                    MPFROMLONG(CWMT_MSG_REMOTE), MPFROMP(&stWMChatMsg) );
      }
      break;
  }

  pClientData->iLastChatMsgType = iLen;
}


/*static void _cbReleaseAllKeys(rfbClientPtr prfbClient)
{
  PCLIENTDATA          pClientData = (PCLIENTDATA)prfbClient->clientData;

  if ( prfbClient->viewOnly )
    return;

  debugPCP();
}*/

// Copy string to the clipboard.
static void _cbSetCutText(char* pcText, int cbText, rfbClientPtr prfbClient)
{
  ULONG      ulRC;
  BOOL       fSuccess;
  PSZ        pszText;

  ulRC = DosAllocSharedMem( (PPVOID)&pszText, 0, cbText + 1,
                            PAG_COMMIT | PAG_READ | PAG_WRITE |
                            OBJ_GIVEABLE | OBJ_GETTABLE | OBJ_TILE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosAllocSharedMem() failed, rc = %u", ulRC );
    return;
  }
  memcpy( pszText, pcText, cbText );
  pszText[cbText] = '\0';

  if ( !WinOpenClipbrd( habSrv ) )
  {
    debug( "WinOpenClipbrd() failed" );
    fSuccess = FALSE;
  }
  else
  {    
    WinEmptyClipbrd( habSrv );

    fSuccess = WinSetClipbrdData( habSrv, (ULONG)pszText, CF_TEXT, CFI_POINTER );
    if ( !fSuccess )
      debug( "WinOpenClipbrd() failed" );

    WinCloseClipbrd( habSrv );
  }

  if ( !fSuccess )
    DosFreeMem( pszText );
}


// Utilites
// --------

static HPS _memPSCreate(ULONG ulCX, ULONG ulCY, ULONG ulBPP, PRGB2 paPalette)
{
  SIZEL                size;
  HDC                  hdcMem;
  HPS                  hpsMem;
  HBITMAP              hbmMem;
  PBITMAPINFOHEADER2   pbmih;
  ULONG                ulOptions;
  ULONG                cbPalette = ulBPP == 1 ? (sizeof(RGB2) << 1)
                                              : ulBPP == 8
                                                ? (sizeof(RGB2) << 8) : 0;

  hdcMem = DevOpenDC( habSrv, OD_MEMORY, "*", 0, NULL, NULLHANDLE );

  // Create a new memory presentation space.
  hpsMem = WinGetScreenPS( HWND_DESKTOP );
  ulOptions = GpiQueryPS( hpsMem, &size );
  WinReleasePS( hpsMem );
  size.cx = 0;
  size.cy = 0;
  hpsMem = GpiCreatePS( habSrv, hdcMem, &size, ulOptions | GPIA_ASSOC );
  if ( hpsMem == NULLHANDLE )
  {
    debug( "GpiCreatePS() failed. Memory PS was not created." );
    DevCloseDC( hdcMem );
    return NULLHANDLE;
  }

  pbmih = alloca( sizeof(BITMAPINFO2) + cbPalette );
  if ( pbmih == NULL )
  {
    debug( "Not enough stack size" );
    GpiDestroyPS( hpsMem );
    DevCloseDC( hdcMem );
    return NULLHANDLE;
  }

  // Create a system bitmap object
  memset( pbmih, 0, sizeof(BITMAPINFOHEADER2) );
  pbmih->cbFix           = sizeof(BITMAPINFOHEADER2);
  pbmih->cx              = ulCX;
  pbmih->cy              = ulCY;
  pbmih->cPlanes         = 1;
  pbmih->cBitCount       = ulBPP;
  if ( paPalette != NULL )
    memcpy( &((PBYTE)pbmih)[sizeof(BITMAPINFO2)], paPalette, cbPalette );

  hbmMem = GpiCreateBitmap( hpsMem, pbmih, 0, NULL, NULL );
  if ( ( hbmMem == GPI_ERROR ) || ( hbmMem == NULLHANDLE ) )
  {
    debug( "GpiCreateBitmap() failed" );
    GpiDestroyPS( hpsMem );
    DevCloseDC( hdcMem );
    return NULLHANDLE;
  }

  if ( !GpiSetBitmapId( hpsMem, hbmMem, 1 ) )
  {
    debug( "GpiSetBitmapId() failed" );
  }

  // Set bitmap object for the memory presentation space.
  if ( GpiSetBitmap( hpsMem, hbmMem ) == HBM_ERROR )
  {
    debug( "GpiSetBitmap() failed" );
    GpiDestroyPS( hpsMem );
    DevCloseDC( hdcMem );
    return NULLHANDLE;
  }

  return hpsMem;
}

static VOID _memPSDestroy(HPS hpsMem)
{
  HBITMAP    hbmMem = GpiSetBitmap( hpsMem, NULLHANDLE );
  HDC        hdcMem = GpiQueryDevice( hpsMem );

  if ( !GpiDestroyPS( hpsMem ) )
    debug( "GpiDestroyPS() failed" );

  if ( DevCloseDC( hdcMem ) == DEV_ERROR )
    debug( "DevCloseDC() failed" );

  if ( ( hbmMem != NULLHANDLE ) && !GpiDeleteBitmap( hbmMem ) )
    debug( "GpiDeleteBitmap() failed" );
}

static VOID _cursorClear()
{
  if ( stCursor.source != NULL )
    free( stCursor.source );

  if ( stCursor.mask != NULL )
    free( stCursor.mask );

  if ( stCursor.richSource != NULL )
    free( stCursor.richSource );

  if ( stCursor.alphaSource != NULL )
    free( stCursor.alphaSource );

  memset( &stCursor, 0, sizeof(stCursor) );
}

static BOOL _cursorSet(HPOINTER hptrCur)
{
  ULONG                ulScreenBPP = prfbScreen->serverFormat.bitsPerPixel;
  POINTERINFO          stInfo;
  struct {
    BITMAPINFOHEADER2  stHdr;
    RGB2               argb2Color[0x100];
  }                    stBmInfo;
  HBITMAP              hbmCur;
  HPS                  hpsCur;
  POINTL               ptPos = { 0, 0 };
  PBYTE                pbImgLine, pbMaskLine;
  ULONG                ulPos, cbRFBLine;
  LONG                 lLine, lRC;
  register             ULONG ulBit, ulBytePos;
  PUCHAR               pucRFBSource;
  PUCHAR               pucRFBMask;

  if ( hptrCur == hptrLastPointer )
    // Cursor was not changed
    return TRUE;

  // Query current pointer data.
  if ( !WinQueryPointerInfo( hptrCur, &stInfo ) )
  {
    debugPCP( "WinQueryPointerInfo() failed" );
    return FALSE;
  }

  // Get a mask bitmap information. The bitmap consists of two parts: upper
  // half is XOR mask and bottom half is AND mask.

  hbmCur = stInfo.fPointer ? stInfo.hbmPointer : stInfo.hbmMiniPointer;
  if ( hbmCur == NULLHANDLE )
    debugPCP( "hbmCur is NULLHANDLE" );

  memset( &stBmInfo, 0, sizeof(stBmInfo) );
  stBmInfo.stHdr.cbFix = sizeof(BITMAPINFOHEADER2);
  if ( !GpiQueryBitmapInfoHeader( hbmCur, (PBITMAPINFOHEADER2)&stBmInfo.stHdr ) )
  {
    HPOINTER hptrArrow = WinQuerySysPointer( HWND_DESKTOP, SPTR_ARROW, FALSE );

    if ( hptrCur != hptrArrow )
      // Set system default (arrow) for the cursor.
      return _cursorSet( hptrArrow );

    debugPCP( "GpiQueryBitmapInfoHeader() failed" );
    return FALSE;
  }

  if ( ( stBmInfo.stHdr.cPlanes != 1 ) || ( stBmInfo.stHdr.cBitCount != 1 ) )
  {
    debug( "cPlanes = %u (should be 1), cBitCount = %u (should be 1)",
           stBmInfo.stHdr.cPlanes, stBmInfo.stHdr.cBitCount );
    return FALSE;
  }

  // Allocate memory to obtain one line of bitmap. Size is enough for color bmp.
  pbImgLine = alloca( ( (32*stBmInfo.stHdr.cx + 31) / 32 ) * 4 );
  pbMaskLine = alloca( ( (1*stBmInfo.stHdr.cx + 31) / 32 ) * 4 );
  if ( ( pbImgLine == NULL ) || ( pbMaskLine == NULL ) )
  {
    debugPCP( "Not enough stack space" );
    return FALSE;
  }

  // Create a presentation space and draw XOR and AND mask there.

  hpsCur = _memPSCreate( stBmInfo.stHdr.cx, stBmInfo.stHdr.cy, 1,
                         stBmInfo.argb2Color );
  if ( hpsCur == NULLHANDLE ) 
    return FALSE;

  if ( !WinDrawBitmap( hpsCur, hbmCur, NULL, &ptPos, 0, 0,
                       DBM_NORMAL | DBM_IMAGEATTRS ) )
  {
    debugPCP( "WinDrawBitmap() failed" );
    _memPSDestroy( hpsCur );
    return FALSE;
  }

  // Make a new rfbCursor object.

  _cursorClear();
  stCursor.width = stBmInfo.stHdr.cx;
  stCursor.height = stBmInfo.stHdr.cy >> 1;
  lRC = ( (stCursor.width + 7) / 8 ) * stCursor.height;
  stCursor.source = calloc( 1, lRC );
  stCursor.mask = calloc( 1, lRC );
  stCursor.richSource = calloc( stCursor.width * (ulScreenBPP >> 3),
                                stCursor.height );
  stCursor.xhot = stInfo.xHotspot;
  stCursor.yhot = stCursor.height - stInfo.yHotspot - 1;
  stCursor.foreRed = 0xFF; stCursor.foreGreen = 0xFF; stCursor.foreBlue = 0xFF;
  stCursor.backRed = 0; stCursor.backRed = 0; stCursor.backRed = 0;

  // Build cursor image and mask data.

  pucRFBSource = stCursor.source;
  pucRFBMask   = stCursor.mask;
  cbRFBLine    = (stBmInfo.stHdr.cx + 7) / 8;

  for( lLine = stCursor.height - 1; lLine >= 0;
       lLine--, pucRFBSource += cbRFBLine, pucRFBMask += cbRFBLine )
  {
    // Query line from the pointer XOR mask.
    lRC = GpiQueryBitmapBits( hpsCur, lLine, 1, pbImgLine,
                              (PBITMAPINFO2)&stBmInfo.stHdr );
    if ( ( lRC != 0 ) && ( lRC != GPI_ALTERROR ) )
      // Query line from the pointer AND mask.
      lRC = GpiQueryBitmapBits( hpsCur, lLine + stCursor.height, 1, pbMaskLine,
                                (PBITMAPINFO2)&stBmInfo.stHdr );

    if ( ( lRC == 0 ) || ( lRC == GPI_ALTERROR ) )
    {
      debug( "GpiQueryBitmapBits(,%u,,,) failed", lLine );
      break;
    }

    for( ulPos = 0; ulPos < stBmInfo.stHdr.cx; ulPos++ )
    {
      ulBit = 0x80 >> (ulPos & 0x7);
      ulBytePos = ulPos >> 3;

      {
        BOOL fAND = (ulBit & pbMaskLine[ulBytePos]) != 0;
        BOOL fXOR = (ulBit & pbImgLine[ulBytePos]) != 0;

        if ( !fAND || fXOR )
        {
          pucRFBMask[ulBytePos] |= ulBit;

          if ( !fAND )
          {
            if ( fXOR )
              pucRFBSource[ulBytePos] |= ulBit;
          }
          else if ( fXOR && ((lLine+ulPos) & 0x01) )
            pucRFBSource[ulBytePos] |= ulBit;
        }
      }
    } // for( ulPos ...
  } // for( lLine ...

  _memPSDestroy( hpsCur );

  // Build color cursor data.

  pucRFBSource = stCursor.richSource;
  hbmCur = stInfo.fPointer ? stInfo.hbmColor : stInfo.hbmMiniColor;
  if ( hbmCur == NULLHANDLE )
  {
    // No color bitmap for pointer - make rich cursor from 1-bit image.

    pucRFBMask = stCursor.source;
    for( lLine = 0; lLine < stCursor.height; lLine++, pucRFBMask += cbRFBLine )
    {
      for( ulPos = 0; ulPos < stBmInfo.stHdr.cx; ulPos++ )
      {
        if ( ( (0x80 >> (ulPos & 0x7)) & pucRFBMask[ulPos >> 3]) != 0 )
          switch( ulScreenBPP )
          {
            case  8: *((PBYTE)pucRFBSource)   = 0xFF;       break;
            case 32: *((PULONG)pucRFBSource)  = 0x00FFFFFF; break;
            default: *((PUSHORT)pucRFBSource) = 0xFFFF;     break;
          }

        pucRFBSource += ulScreenBPP >> 3;
      }
    }
  }
  else
  {
    // Have color bitmap for pointer - make rich cursor form bitmap.

    memset( &stBmInfo, 0, sizeof(stBmInfo) );
    stBmInfo.stHdr.cbFix = sizeof(BITMAPINFOHEADER2);
    if ( !GpiQueryBitmapInfoHeader( hbmCur, (PBITMAPINFOHEADER2)&stBmInfo.stHdr ) )
    {
      debugPCP( "GpiQueryBitmapInfoHeader() failed" );
      return FALSE;
    }

    hpsCur = _memPSCreate( stBmInfo.stHdr.cx, stBmInfo.stHdr.cy,
                           ulScreenBPP, NULL );
    if ( !WinDrawBitmap( hpsCur, hbmCur, NULL, &ptPos, 0, 0,
                         DBM_NORMAL | DBM_IMAGEATTRS ) )
      debugPCP( "WinDrawBitmap() failed" );
    else
    {
      stBmInfo.stHdr.cPlanes   = 1;            // Data will be converted to
      stBmInfo.stHdr.cBitCount = ulScreenBPP;  // descktop's format.

      cbRFBLine = stCursor.width * (ulScreenBPP >> 3);
      for( lLine = stBmInfo.stHdr.cy - 1; lLine >= 0;
           lLine--, pucRFBSource += cbRFBLine )
      {
        // Store line from bitmap to the rich cursor data.
        lRC = GpiQueryBitmapBits( hpsCur, lLine, 1, (PBYTE)pucRFBSource,
                                  (PBITMAPINFO2)&stBmInfo.stHdr );
        if ( ( lRC == 0 ) || ( lRC == GPI_ALTERROR ) )
        {
          debug( "GpiQueryBitmapBits(,%u,,,) failed", lLine );
          break;
        }
      }

    }
    _memPSDestroy( hpsCur );
  }

  // Set new cursor.
  rfbSetCursor( prfbScreen, &stCursor );
  hptrLastPointer = hptrCur;
  return TRUE;
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
      debug( "Inknown message 0x%X", ulCode );
      return;
  }

  logFmt( 0, "%s at line %u in file %s", pszMsg, ulLine, pszFile );
}

static VOID _getScreen(PRECTL prectl)
{
  ULONG      ulIdx;
  ULONG      cbLine = prfbScreen->width * ( prfbScreen->bitsPerPixel >> 3 );
  PBYTE      pbData;
  LONG       lRC;
  struct {
    BITMAPINFOHEADER2  stHdr;
    RGB2               argb2Color[0x100];
  }          stBmInfo;
  POINTL     aPoints[3];

  // Y - in frame buffer is lower line relative top of bitmap.
  pbData = (PBYTE)&prfbScreen->frameBuffer[ (prfbScreen->height - prectl->yBottom
                                            - 1) * cbLine ];

  memcpy( aPoints, prectl, sizeof(RECTL) );
  aPoints[2] = aPoints[0];
  lRC = GpiBitBlt( hpsMem, hpsScreen, 3, aPoints, ROP_SRCCOPY, BBO_IGNORE );

  if ( lRC == GPI_ERROR )
    debug( "GpiBitBlt() failed" );

  memset( &stBmInfo, 0, sizeof(BITMAPINFOHEADER2) );
  stBmInfo.stHdr.cbFix     = sizeof(BITMAPINFOHEADER2);
  stBmInfo.stHdr.cPlanes   = 1;
  stBmInfo.stHdr.cBitCount = prfbScreen->bitsPerPixel;
  for( ulIdx = aPoints[0].y; ulIdx < aPoints[1].y; ulIdx++ )
  {
    lRC = GpiQueryBitmapBits( hpsMem, ulIdx, 1, pbData,
                              (PBITMAPINFO2)&stBmInfo );
    if ( ( lRC == 0 ) || ( lRC == GPI_ALTERROR ) )
      debug( "GpiQueryBitmapBits(,%u,,,) failed", ulIdx );
    pbData -= cbLine;
  }
}

BOOL _isClientExists(rfbClientPtr prfbClient)
{
  rfbClientIteratorPtr prfbIter;
  rfbClientPtr         prfbClientScan;

  prfbIter = rfbGetClientIterator( prfbScreen );

  while( ( prfbClientScan = rfbClientIteratorNext( prfbIter ) ) )
  {
    if ( prfbClientScan == prfbClient )
      break;
  }

  rfbReleaseClientIterator( prfbIter );

  return prfbClientScan != NULL;
}

static VOID _progsFree()
{
  if ( pszProgOnLogon != NULL )
  {
    free( pszProgOnLogon );
    pszProgOnLogon = NULL;
  }

  if ( pszProgOnGone != NULL )
  {
    free( pszProgOnGone );
    pszProgOnGone = NULL;
  }

  if ( pszProgOnCAD != NULL )
  {
    free( pszProgOnCAD );
    pszProgOnCAD = NULL;
  }
}

// Public routines
// ---------------

BOOL rfbsInit(PCONFIG pConfig)
{
  LONG       alFormats[2];
  BOOL       fRC;
  PSZ        pszHostName;

  rfbLog = _cbLog;
  rfbErr = _cbLogErr;

  memset( &stCursor, 0, sizeof(stCursor) );
  aclInit( &stACL );

  hpsScreen = WinGetScreenPS( HWND_DESKTOP );
  if ( hpsScreen == NULLHANDLE )
  {
    debug( "WinGetScreenPS() failed" );
    return FALSE;
  }

  // Query desktop BPP.
  fRC = GpiQueryDeviceBitmapFormats( hpsScreen, 2, (PLONG)&alFormats );
  if ( !fRC )
  {
    debug( "GpiQueryDeviceBitmapFormats() failed" );
    WinReleasePS( hpsScreen );
    return FALSE;
  }

  switch( alFormats[1] )
  {
    case 8:
    case 32:
      break;
    default:
      alFormats[1] = 16;
  }

  // Create memory bitmap.
  WinQueryWindowRect( HWND_DESKTOP, &rectlDesktop );
  hpsMem = _memPSCreate( rectlDesktop.xRight - rectlDesktop.xLeft,
                         rectlDesktop.yTop - rectlDesktop.yBottom,
                         alFormats[1], NULL );
  if ( hpsMem == NULLHANDLE )
  {
    WinReleasePS( hpsScreen );
    return FALSE;
  }

  // Create and initialize the server. Setup logfile.
  if ( !rfbsSetServer( pConfig ) )
  {
    _memPSDestroy( hpsMem );
    WinReleasePS( hpsScreen );
    return FALSE;
  }

  // Load keysym codes table.
  pKbdMap = xkMapLoad( "keysym\\user.xk & keysym\\%L.xk & keysym\\general.xk",
                       _cbKbdMapLoadErr );
  if ( pKbdMap == NULL )
    logFmt( 0, "Could not load keyboard map" );

  _getScreen( &rectlDesktop );
  _cursorSet( WinQueryPointer( HWND_DESKTOP ) );

  pszHostName = getenv( "HOSTNAME" );
  if ( pszHostName != NULL )
  {
    pszDesktopName = malloc( strlen( APP_NAME ) + 4 + strlen( pszHostName ) );
    if ( pszDesktopName != NULL )
      sprintf( pszDesktopName, APP_NAME " (%s)", pszHostName );
  }
  
  if ( pszDesktopName == NULL )
    pszDesktopName = strdup( APP_NAME );

  return TRUE;
}

VOID rfbsDone()
{
  if ( prfbScreen != NULL )
  {
    if ( prfbScreen->colourMap.data.bytes != NULL )
    {
      free( prfbScreen->colourMap.data.bytes );
      prfbScreen->colourMap.data.bytes = NULL;
      // Avoid _libvncserver's_ free() for prfbScreen->colourMap.data.bytes.
      // This memory allocated by _our application_ in _WMPalette().
      // Unixoids have a very strange idea about using the memory. :-/
    }

    if ( prfbScreen->frameBuffer != NULL )
    {
      free( prfbScreen->frameBuffer );
      prfbScreen->frameBuffer = NULL;
    }

    if ( prfbScreen->sslkeyfile != NULL )
      free( prfbScreen->sslkeyfile );
    if ( prfbScreen->sslcertfile != NULL )
      free( prfbScreen->sslcertfile );

    rfbShutdownServer( prfbScreen, TRUE );
    rfbScreenCleanup( prfbScreen );
    prfbScreen = NULL;
  }
  _cursorClear();
  aclFree( &stACL );
  _progsFree();

  if ( paLogonClients != NULL )
  {
    free( paLogonClients );
    paLogonClients    = NULL;
    cLogonClients     = 0;
    ulMaxLogonClients = 0;
  }

  if ( hpsMem != NULLHANDLE )
  {
    _memPSDestroy( hpsMem );
    hpsMem = NULLHANDLE;
  }

  WinReleasePS( hpsScreen );

  if ( pszDesktopName != NULL )
  {
    free( pszDesktopName );
    pszDesktopName = NULL;
  }

  if ( pKbdMap != NULL )
  {
    xkMapFree( pKbdMap );
    pKbdMap = NULL;
  }

  if ( hDriverVNCKBD != NULLHANDLE )
  {
    DosClose( hDriverVNCKBD );
    hDriverVNCKBD = NULLHANDLE;
  }

  if ( hDriverKBD != NULLHANDLE )
  {
    DosClose( hDriverKBD );
    hDriverKBD = NULLHANDLE;
  }

  logClose();
}

VOID rfbsSetMouse()
{
  POINTL               pointl;
  rfbClientIteratorPtr prfbIterator;
  rfbClientPtr         prfbClient;

  if ( prfbScreen == NULL )
    return;

  _cursorSet( WinQueryPointer( HWND_DESKTOP ) );

  WinQueryPointerPos( HWND_DESKTOP, &pointl );
  pointl.y = prfbScreen->height - pointl.y - 1;

  if ( ( prfbScreen->cursorX != pointl.x ) ||
       ( prfbScreen->cursorY != pointl.y ) )
  {
    prfbScreen->cursorX = pointl.x;
    prfbScreen->cursorY = pointl.y;

    prfbIterator = rfbGetClientIterator( prfbScreen );
    while( (prfbClient = rfbClientIteratorNext( prfbIterator )) )
      prfbClient->cursorWasMoved = TRUE;
    rfbReleaseClientIterator( prfbIterator );
  }
}

BOOL rfbsSetServer(PCONFIG pConfig)
{
  rfbScreenInfoPtr     prfbNewScreen;
  ULONG                ulRC;
  ULONG                ulAction;

  if ( hpsMem == NULLHANDLE )
  {
    debug( "hpsMem is NULLHANDLE" );
    return FALSE;
  }

  logSet( habSrv, &pConfig->stLogData );

  if ( prfbScreen != NULL )
    // Modify existen RFB server.
    prfbNewScreen = prfbScreen;
  else
  {
    // First initialization - create RFB server.

    BITMAPINFOHEADER2  stBmpInfo;

    memset( &stBmpInfo, 0, sizeof(stBmpInfo) );
    stBmpInfo.cbFix = sizeof(BITMAPINFOHEADER2);
    if ( !GpiQueryBitmapInfoHeader( GpiQueryBitmapHandle( hpsMem, 1 ),
                                    &stBmpInfo ) )
    {
      debug( "GpiQueryBitmapInfoHeader() failed" );
      return FALSE;
    }

    prfbNewScreen = rfbGetScreen( NULL, NULL, stBmpInfo.cx, stBmpInfo.cy, 8, 3,
                                  stBmpInfo.cBitCount >> 3 );
    if ( prfbNewScreen == NULL )
    {
      debug( "rfbGetScreen() failed" );
      return FALSE;
    }

    prfbNewScreen->versionString             = APP_NAME " " VERSION;
    prfbNewScreen->passwordCheck             = rfbCheckPasswordByList;
    prfbNewScreen->authPasswdData            = (void*)apszPasswords;
    prfbNewScreen->ptrAddEvent               = _cbPtrEvent;
    prfbNewScreen->kbdAddEvent               = _cbKeyEvent;
    prfbNewScreen->newClientHook             = _cbClientNew;
    prfbNewScreen->setXCutText               = _cbSetCutText;
    prfbNewScreen->setTextChat               = _cbSetTextChat;
    prfbNewScreen->httpDir                   = "./webclients";
    prfbNewScreen->ignoreSIGPIPE             = TRUE;

    switch( stBmpInfo.cBitCount )
    {
      case 8:
        prfbNewScreen->serverFormat.trueColour = FALSE;
        break;

      case 32:
        prfbNewScreen->serverFormat.redShift   = 16;
        prfbNewScreen->serverFormat.greenShift = 8;
        prfbNewScreen->serverFormat.blueShift  = 0;
        break;

      default: // 16
        prfbNewScreen->serverFormat.redMax     = 31;
        prfbNewScreen->serverFormat.greenMax   = 63;
        prfbNewScreen->serverFormat.blueMax    = 31;
        prfbNewScreen->serverFormat.redShift   = 11;
        prfbNewScreen->serverFormat.greenShift = 5;
        prfbNewScreen->serverFormat.blueShift  = 0;
    }

    if ( prfbScreen == NULL )
    {
      prfbNewScreen->frameBuffer = malloc( stBmpInfo.cx * stBmpInfo.cy *
                                           (stBmpInfo.cBitCount >> 3) );
      if ( prfbNewScreen->frameBuffer == NULL )
      {
        debug( "Not enough memory" );
        rfbScreenCleanup( prfbNewScreen );
        return FALSE;
      }
    }
  }

  // Variable properties of the RFB server.

  prfbNewScreen->desktopName             = pszDesktopName == NULL
                                             ? APP_NAME : pszDesktopName;
  prfbNewScreen->alwaysShared            = pConfig->fAlwaysShared;
  prfbNewScreen->port                    = pConfig->ulPort;
  prfbNewScreen->httpPort                = pConfig->ulHTTPPort;
  prfbNewScreen->deferUpdateTime         = pConfig->ulDeferUpdateTime;
  prfbNewScreen->deferPtrUpdateTime      = pConfig->ulDeferPtrUpdateTime;
  prfbNewScreen->alwaysShared            = pConfig->fAlwaysShared;
  prfbNewScreen->neverShared             = pConfig->fNeverShared;
  prfbNewScreen->dontDisconnect          = pConfig->fDontDisconnect;
  prfbNewScreen->httpEnableProxyConnect  = pConfig->fHTTPProxyConnect;
  prfbNewScreen->progressiveSliceHeight  = pConfig->ulProgressiveSliceHeight;
  prfbNewScreen->listenInterface         = pConfig->inaddrListen;
  prfbNewScreen->permitFileTransfer      = pConfig->fFileTransfer ? -1 : 0;

  prfbNewScreen->protocolMajorVersion    = rfbProtocolMajorVersion; // 3
  // Minor version for UltraVNC features is 6 (UltraVNC viewer will check it).
  prfbNewScreen->protocolMinorVersion    = pConfig->fUltraVNCSupport
                                          ? 6 : rfbProtocolMinorVersion;

  // SSL key and cert. files for websockets.
  if ( prfbNewScreen->sslkeyfile != NULL )
    free( prfbNewScreen->sslkeyfile );
  prfbNewScreen->sslkeyfile              = strdup( pConfig->acSSLKeyFile );

  if ( prfbNewScreen->sslcertfile != NULL )
    free( prfbNewScreen->sslcertfile );
  prfbNewScreen->sslcertfile             = strdup( pConfig->acSSLCertFile );

  debug( "SSL key: %s , cert.: %s\n",
         prfbNewScreen->sslkeyfile, prfbNewScreen->sslcertfile );

  // Set passwords.

  if ( pConfig->fPrimaryPassword )
  {
    strcpy( acPassword, pConfig->acPrimaryPassword );
    apszPasswords[0] = acPassword;
    apszPasswords[1] = NULL;
    prfbNewScreen->authPasswdFirstViewOnly = 1;
  }
  else
    prfbNewScreen->authPasswdFirstViewOnly = 0;

  if ( pConfig->fViewOnlyPassword )
  {
    strcpy( acViewOnlyPassword, pConfig->acViewOnlyPassword );

    if ( pConfig->fPrimaryPassword )
    {
      apszPasswords[1] = acViewOnlyPassword;
      apszPasswords[2] = NULL;
    }
    else
    {
      apszPasswords[0] = acViewOnlyPassword;
      apszPasswords[1] = NULL;
    }
  }
  else if ( !pConfig->fPrimaryPassword )
    apszPasswords[0] = NULL;

  // Set external programs.

  _progsFree();

  if ( pConfig->fProgOnLogon && pConfig->acProgOnLogon[0] != '\0' )
    pszProgOnLogon = strdup( pConfig->acProgOnLogon );

  if ( pConfig->fProgOnGone && pConfig->acProgOnGone[0] != '\0' )
    pszProgOnGone = strdup( pConfig->acProgOnGone );

  if ( pConfig->fProgOnCAD && pConfig->acProgOnCAD[0] != '\0' )
    pszProgOnCAD = strdup( pConfig->acProgOnCAD );

  // RFB server initialization.

  if ( prfbScreen != NULL )
  {
    // The new binding for an existing RFB server.

    rfbClientPtr         prfbClient;
    rfbClientIteratorPtr prfbIter;

    // Close server sockets.
    rfbShutdownSockets( prfbScreen );
    rfbHttpShutdownSockets( prfbScreen );

    // Create new server sockets.
    prfbScreen->httpInitDone = FALSE;
    rfbInitSockets( prfbScreen );     // FD_ZERO(&(rfbScreen->allFds)) here!
    rfbHttpInitSockets( prfbScreen );

    // Insert client's sockets at the sockets descriptors list.
    prfbIter = rfbGetClientIterator( prfbScreen );
    while( ( prfbClient = rfbClientIteratorNext( prfbIter ) ) )
    {
      if ( prfbClient->sock == -1 )
        continue;

      FD_SET( prfbClient->sock, &prfbScreen->allFds );
      prfbScreen->maxFd = rfbMax( prfbClient->sock, prfbScreen->maxFd );
    }
    rfbReleaseClientIterator( prfbIter );
  }
  else
  {
    // Run RFB server (it's a first call rfbsSetServer()).
    rfbInitServer( prfbNewScreen );
    prfbScreen = prfbNewScreen;
  }

  // Replace current ACL with a configured ACL.
  aclFree( &stACL );
  stACL = pConfig->stACL;
  aclInit( &pConfig->stACL ); // Avoid ACL free during pConfig destruction.

  // Open VNCKBD$ and KBD$ drivers.

  if ( pConfig->fUseDriverVNCKBD )
  {
    if ( hDriverVNCKBD == NULLHANDLE )
    {
      ulRC = DosOpen( "VNCKBD$", &hDriverVNCKBD, &ulAction, 0, 0, FILE_OPEN,
                      OPEN_SHARE_DENYREADWRITE | OPEN_ACCESS_READWRITE, NULL );
      if ( ulRC != NO_ERROR )
      {
        logFmt( 0, "Could not open VNCKBD$, rc=%u", ulRC );
        hDriverVNCKBD = NULLHANDLE;
      }
    }
  }
  else if ( hDriverVNCKBD != NULLHANDLE )
  {
    DosClose( hDriverVNCKBD );
    hDriverVNCKBD = NULLHANDLE;
  }

  if ( pConfig->fUseDriverKBD )
  {
    if ( hDriverKBD == NULLHANDLE )
    {
      ulRC = DosOpen( "KBD$", &hDriverKBD, &ulAction, 0, 0, FILE_OPEN,
                      OPEN_SHARE_DENYREADWRITE | OPEN_ACCESS_READWRITE, NULL );
      if ( ulRC != NO_ERROR )
      {
        logFmt( 0, "Could not open KBD$, rc=%u", ulRC );
        hDriverKBD = NULLHANDLE;
      }
    }
  }
  else if ( hDriverKBD != NULLHANDLE )
  {
    DosClose( hDriverKBD );
    hDriverKBD = NULLHANDLE;
  }

  return TRUE;
}

VOID rfbsUpdateScreen(RECTL rectlUpdate)
{
  RECTL      rectlNew;

  if ( prfbScreen == NULL )
    return;

  WinIntersectRect( habSrv, &rectlNew, &rectlUpdate, &rectlDesktop );
  if ( !WinIsRectEmpty( habSrv, &rectlNew ) )
  {
    _getScreen( &rectlNew );

    rectlNew.yBottom = prfbScreen->height - rectlNew.yBottom;
    rectlNew.yTop = prfbScreen->height - rectlNew.yTop - 1;
    rfbMarkRectAsModified( prfbScreen,
                           rectlNew.xLeft, rectlNew.yTop,
                           rectlNew.xRight, rectlNew.yBottom );
  }
}

// Send a new color map to the clients.
VOID rfbsSetPalette(ULONG cColors, PRGB2 pColors)
{
  HPAL       hPal, hPalOld;
  ULONG      ulIdx;
  PBYTE      pbColor;

  if ( ( prfbScreen == NULL ) || prfbScreen->serverFormat.trueColour )
    return;

  // Set the palette for memory PS.

  hPal = GpiCreatePalette( habSrv, 0, LCOLF_CONSECRGB, cColors, (PULONG)pColors );
  if ( hPal == NULLHANDLE )
    debug( "GpiCreatePalette() failed" );
  else
  {
    hPalOld = GpiSelectPalette( hpsMem, hPal );
    if ( hPalOld == PAL_ERROR )
    {
      debug( "GpiSelectPalette() failed" );
      GpiDeletePalette( hPal );
    }
    else if ( hPalOld != NULLHANDLE )
      GpiDeletePalette( hPalOld );
  }

  // Convert and send palette to the clients.

  pbColor = malloc( 3 * cColors );
  if ( pbColor != NULL )
  {
    prfbScreen->colourMap.count = cColors;
    prfbScreen->colourMap.is16  = 0;
    if ( prfbScreen->colourMap.data.bytes != NULL )
      free( prfbScreen->colourMap.data.bytes );
    prfbScreen->colourMap.data.bytes = (uint8_t *)pbColor;

    for( ulIdx = 0; ulIdx < cColors; ulIdx++ )
    {
      *(pbColor++) = pColors[ulIdx].bRed;
      *(pbColor++) = pColors[ulIdx].bGreen;
      *(pbColor++) = pColors[ulIdx].bBlue;
    }

    rfbSetClientColourMaps( prfbScreen, 0, cColors );
  }
}

VOID rfbsProcessEvents(ULONG ulTimeout)
{
  ULONG                ulIdx;
  rfbClientIteratorPtr prfbIter;
  rfbClientPtr         prfbClient;
  CHAR                 acBuf[CCHMAXPATH];

  if ( prfbScreen->listenSock == -1 )
  {
    DosSleep( ulTimeout );
    return;
  }

  // rfbProcessEvents() sends the file data after waiting for incoming data.
  // We will set timeout (for incoming data) to zero if we have data to send.
  prfbIter = rfbGetClientIterator( prfbScreen );
  while( ( prfbClient = rfbClientIteratorNext( prfbIter ) ) )
  {
    if ( ( prfbClient->fileTransfer.fd != -1 ) &&
         ( prfbClient->fileTransfer.sending != 0 ) )
    {
      // Ok, we have a client that receives the file.
      ulTimeout = 0;
      break;
    }
  }
  rfbReleaseClientIterator( prfbIter );

  rfbProcessEvents( prfbScreen, ulTimeout * 1000 );

  if ( cLogonClients == 0 )
    return;

  // We have clients registered at _cbClientNew() and not logged-in yet.
  // Let's see is there a clients authenticated now. If we find logged-in
  // client: remove from list, run external program.

  prfbIter = rfbGetClientIterator( prfbScreen );
  while( ( prfbClient = rfbClientIteratorNext( prfbIter ) ) )
  {
    if ( ( prfbClient->state != RFB_INITIALISATION ) &&
         ( prfbClient->state != RFB_NORMAL ) )
      // This client is not authenticated.
      continue;

    for( ulIdx = 0; ulIdx < cLogonClients; ulIdx++ )
    {
      if ( paLogonClients[ulIdx] == prfbClient )
      {
        // We found a client who is _now_ authenticated.

        // Remove client from "wait for authentication" list.
        cLogonClients--;
        paLogonClients[ulIdx] = paLogonClients[cLogonClients];

        // Run external program.

        if ( pszProgOnLogon != NULL )
        {
          _progExecute( prfbClient, pszProgOnLogon, sizeof(acBuf), acBuf );
          if ( acBuf[0] == '\0' )
          {
            // Empty string returned - client forbidden.
            logFmt( 1, "Client %s forbidden (external program)", prfbClient->host );
            rfbCloseClient( prfbClient );
            prfbClient = NULL;
          }
          else
            // Store external program answer to client's data.
            ((PCLIENTDATA)prfbClient->clientData)->pszExtProgId =
               strdup( acBuf );
        }

        if ( prfbClient != NULL )
          _jumpToDesktop( _JTDT_ANY );

        break;
      } // if ( paLogonClients[ulIdx] == prfbClient )
    } // for( ulIdx = 0; ulIdx < cLogonClients; ulIdx++ )
  } // while( ( prfbClient = rfbClientIteratorNext( prfbIter ) ) )

  rfbReleaseClientIterator( prfbIter );
}

VOID rfbsSendClipboardText(PSZ pszText)
{
  ULONG      cbText;

  cbText = strlen( pszText );
  if ( cbText != 0 )
    rfbSendServerCutText( prfbScreen, pszText, cbText );
}

// Sends chat message if pszText is not NULL. Otherwise it sends open-chat
// request to the client.
BOOL rbfsSendChatMsg(rfbClientPtr prfbClient, PSZ pszText)
{
  if ( _isClientExists( prfbClient ) )
  {
    ULONG cbText = pszText == NULL ? rfbTextChatOpen : strlen( pszText );

    return rfbSendTextChatMessage( prfbClient, cbText, pszText );
  }

  return FALSE;
}

BOOL rbfsSetChatWindow(rfbClientPtr prfbClient, HWND hwnd)
{
  if ( !_isClientExists( prfbClient ) )
    return FALSE;

  if ( ( hwnd == NULLHANDLE ) &&
       ( ((PCLIENTDATA)prfbClient->clientData)->hwndChat != NULLHANDLE ) )
  {
    rfbSendTextChatMessage( prfbClient, rfbTextChatClose, "" );
    rfbSendTextChatMessage( prfbClient, rfbTextChatFinished, "" );
  }
  else if ( ( hwnd != NULLHANDLE ) &&
            ( ((PCLIENTDATA)prfbClient->clientData)->hwndChat == NULLHANDLE ) )
  {
    rfbSendTextChatMessage( prfbClient, rfbTextChatOpen, "" );
  }

  ((PCLIENTDATA)prfbClient->clientData)->hwndChat = hwnd;
  return TRUE;
}

// ULONG rbfsCheckPorts(in_addr_t inaddrListen, ULONG ulPort, ULONG ulHTTPPort)
//
// Checks availability of new listen ports for server.
// Returns bit mask value: bit 1 is set - listen port is Ok.
//                         bit 2 is set - listen HTTP port is Ok.

ULONG rbfsCheckPorts(in_addr_t inaddrListen, ULONG ulPort, ULONG ulHTTPPort)
{
  ULONG      ulResult = 0;
  int        iSock;

  if ( ( ulPort > 0 ) && ( ulPort <= 0xFFFF ) )
  {
    if ( ( prfbScreen != NULL ) &&
         ( prfbScreen->listenInterface == inaddrListen ) &&
           ( ( prfbScreen->listenSock != -1 && ulPort == prfbScreen->port ) ||
             ( prfbScreen->httpListenSock != -1 && ulPort == prfbScreen->httpPort ) ) )
      ulResult = 0x01;
    else
    {
      iSock = rfbListenOnTCPPort( ulPort, inaddrListen );
      if ( iSock != -1 )
      {
        ulResult = 0x01;
        soclose( iSock );
      }
    }
  }

  if ( ( ulHTTPPort > 0 ) && ( ulHTTPPort <= 0xFFFF ) &&
       ( ulHTTPPort != ulPort ) )
  {
    if ( ( prfbScreen != NULL ) &&
         ( prfbScreen->listenInterface == inaddrListen ) &&
           ( ( prfbScreen->listenSock != -1 && ulHTTPPort == prfbScreen->port ) ||
             ( prfbScreen->httpListenSock != -1 && ulHTTPPort == prfbScreen->httpPort ) ) )
      ulResult |= 0x02;
    else
    {
      iSock = rfbListenOnTCPPort( ulHTTPPort, inaddrListen );
      if ( iSock != -1 )
      {
        ulResult |= 0x02;
        soclose( iSock );
      }
    }
  }

  return ulResult;
}

BOOL rfbsAttach(int iSock, BOOL fDispatcher)
{
  rfbClientPtr         prfbClient;

/*
Can't connect to TightVNC client. Libvncserver log:

24/12/2016 02:19:12 Making connection to client on host 192.168.1.50 port 5500
24/12/2016 02:19:12   other clients:
24/12/2016 02:19:13 Normal socket connection
24/12/2016 02:19:14 rfbProcessClientProtocolVersion: read: Connection reset by peer

TightVNC client log:

[ 7144/ 6552] 2016-12-24 02:45:34:583 - Server sent protocol version: RFB 003.008
[ 7144/ 6552] 2016-12-24 02:45:34:583 - Send to server protocol version: RFB 003.008
[ 7144/ 6552] 2016-12-24 02:45:34:583 - Protocol stage is "Authentication".
[ 7144/ 6552] 2016-12-24 02:45:34:583 : Negotiating about security type...
[ 7144/ 6552] 2016-12-24 02:45:34:583 : Reading list of security types...
[ 7144/ 6552] 2016-12-24 02:45:34:583 - onDisconnect: Failed to recv data from socket.
*/
  if ( fDispatcher )
  {
    debugPCP( "Dispatcher not supported" );
    return FALSE;
  }

  if ( !rfbSetNonBlocking( iSock ) )
  {
    soclose( iSock );
    return FALSE;
  }

  prfbClient = rfbNewClient( prfbScreen, iSock );
  if ( prfbClient == NULL )
    return FALSE;

  prfbClient->reverseConnection = TRUE;

/*
  [OS2TK], setsockopt()

  TCP_NODELAY (Stream sockets only.) Setting on disables the buffering
              algorithm so that the client's TCP sends small packets as soon as
              possible. This often has no performance effects on LANs, but can
              degrade performance on WANs. 
  Hm... libvncserver use it...

  ULONG                ulVal = 1;
  setsockopt( iSock, IPPROTO_TCP, TCP_NODELAY, (char *)&ulVal, sizeof(ulVal) );
*/

  FD_SET( iSock, &prfbScreen->allFds );
  prfbScreen->maxFd = rfbMax( iSock, prfbScreen->maxFd );

  return TRUE;
}

VOID rfbsForEachClient(PFNCLIENT fnClient, PVOID pUser)
{
  rfbClientIteratorPtr prfbIterator;
  rfbClientPtr         prfbClient;

  prfbIterator = rfbGetClientIterator( prfbScreen );

  while( (prfbClient = rfbClientIteratorNext( prfbIterator )) )
    if ( !fnClient( prfbClient, pUser ) )
      break;

  rfbReleaseClientIterator( prfbIterator );
}

BOOL rfbsDisconnect(rfbClientPtr prfbClient)
{
  if ( _isClientExists( prfbClient ) )
  {
    rfbCloseClient( prfbClient );
    return TRUE;
  }

  return FALSE;
}
