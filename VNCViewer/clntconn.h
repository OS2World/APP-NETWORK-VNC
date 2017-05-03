#ifndef CLNTCONN_H
#define CLNTCONN_H

#include "filexfer.h"

#define RFBSTATE_NOTINIT         0
#define RFBSTATE_INITPROGRESS    1
#define RFBSTATE_WAITPASSWORD    2
#define RFBSTATE_WAITCREDENTIAL  3
#define RFBSTATE_READY           4
#define RFBSTATE_ERROR           5
#define RFBSTATE_FINISH          6

// CC_xxxxx : ccInit() result
#define CC_OK                    0
#define CC_INITIALIZED           1
#define CC_SEMERROR              2
#define CC_NOKBDMAP              3

// RFBBUTTON_xxxxx : ccSendMouseEvent(,,,ulButton)
#define RFBBUTTON_PRESSED        0x0100 /* Flag: mouse button is pressed. */
#define RFBBUTTON_LEFT           0x0001
#define RFBBUTTON_RIGHT          0x0002
#define RFBBUTTON_MIDDLE         0x0003
#define RFBBUTTON_WHEEL_UP       0x0004
#define RFBBUTTON_WHEEL_DOWN     0x0005

// CCSI_xxxxx : ccQuerySessionInfo(,ulItem,,)
#define CCSI_LAST_LOG_REC        0
#define CCSI_DESKTOP_NAME        1
#define CCSI_SERVER_HOST         2

// CCKEY_xxxxx : ccSendKeySequence(, paulKeySeq)
#define CCKEY_PRESS              0x80000000

// CCVO_xxxxx : ccSetViewOnly()
#define CCVO_OFF                 0
#define CCVO_ON                  1
#define CCVO_TOGGLE              2

#define ENC_NAME_MAX_LEN         12
#define ENC_LIST_MAX_LEN         70
#define CHARSET_NAME_MAX_LEN     32

typedef struct _CCPROPERTIES {
  LONG       lListenPort;       // Not -1: "listen mode" (wait for the server)

  CHAR       acHost[272];       /* Not "listen mode": address[:display],
                                                      display>=5900 ? is port
                                   "Listen mode": local interface IP-address.
                                */
  BOOL       fViewOnly;
  ULONG      ulBPP;             // 8/16/32
  CHAR       acCharset[CHARSET_NAME_MAX_LEN];
  CHAR       acDestHost[272];   // address[:port]
  CHAR       acEncodings[ENC_LIST_MAX_LEN];
  ULONG      ulCompressLevel;   // 0..9 for tight, zlib, zlibhex
  ULONG      ulQualityLevel;    // 0..9 for tight, zywrle
  ULONG      ulQoS_DSCP;        // 0 - off
  BOOL       fShareDesktop;
} CCPROPERTIES, *PCCPROPERTIES;

typedef struct _CCLOGONINFO {
  BOOL       fCredential;
  CHAR       acUserName[256];
  CHAR       acPassword[256];
} CCLOGONINFO, *PCCLOGONINFO;

typedef struct _CLNTCONN *PCLNTCONN;

// Moudule initialization.
ULONG ccInit();

// Module finalization.
VOID ccDone();

// Creates client connection object.
PCLNTCONN ccCreate(PCCPROPERTIES pProperties, HWND hwnd);

// Destroys client connection object.
VOID ccDestroy(PCLNTCONN pCC);

// Waits for module state (RFBSTATE_xxxxx, fEqual is TRUE) or end of state
// (fEqual is FALSE).
ULONG ccWaitState(PCLNTCONN pCC, ULONG ulState, BOOL fEqual);

// If hwnd is not NULLHANDLE it sets the window that will receives messages
// WM_VNC_xxxxx.
// Returns previous window handle (or current if hwnd is NULLHANDLE).
HWND ccSetWindow(PCLNTCONN pCC, HWND hwnd);

// On/off view-only mode.
BOOL ccSetViewOnly(PCLNTCONN pCC, ULONG ulViewOnly);

// Returns TRUE if RFB message type ulFRBMsg is supported.
BOOL ccIsRFBMsgSupported(PCLNTCONN pCC, BOOL fClient2Server, ULONG ulFRBMsg);

// Returns TRUE if state is RFBSTATE_READY and locks the object in this state
// until function ccUnlockReadyState() is called.
BOOL ccLockReadyState(PCLNTCONN pCC);
VOID ccUnlockReadyState(PCLNTCONN pCC);

// Ask to pCC's thread to disconnect and exit.
BOOL ccDisconnectSignal(PCLNTCONN pCC);

// Locks the object and returns memory presentation space handle containing the
// image of remote desktop.
HPS ccGetHPS(PCLNTCONN pCC);
VOID ccReleaseHPS(PCLNTCONN pCC, HPS hps);

// Writes in psizeFrame remote desktop size. Returns FALSE if current state of
// pCC is not RFBSTATE_READY.
BOOL ccQueryFrameSize(PCLNTCONN pCC, PSIZEL psizeFrame);

// Returns current view-only mode on/off.
BOOL ccQueryViewOnly(PCLNTCONN pCC);

// Returns current mouse pointer.
HPOINTER ccQueryPointer(PCLNTCONN pCC);

// Writes in pcBuf information specified by ulItem (CCSI_xxxxx) from session
// log. Returns TRUE on success (information found).
BOOL ccQuerySessionInfo(PCLNTCONN pCC, ULONG ulItem, ULONG cbBuf, PCHAR pcBuf);

// The password or credential shoul be requested before calling this function,
// i.e. we must have state RFBSTATE_WAITPASSWORD (pszUserName nor uses) or
// RFBSTATE_WAITCREDENTIAL.
// Returns FALSE if state is not RFBSTATE_WAITPASSWORD/RFBSTATE_WAITCREDENTIAL
// or invalid type (pLogonInfo->fCredential) of logon information.
BOOL ccSendLogonInfo(PCLNTCONN pCC, PCCLOGONINFO pLogonInfo);

VOID ccSendMouseEvent(PCLNTCONN pCC, LONG lX, LONG lY, ULONG ulButton);

VOID ccSendKeyEvent(PCLNTCONN pCC, ULONG ulKey, BOOL fDown);

// When mp1 is 0 it sends "release" events for registered pressed keys that not
// realy pressed now.
VOID ccSendWMCharEvent(PCLNTCONN pCC, MPARAM mp1, MPARAM mp2);

// Sends XK-key codes. For pressed state code must be ORed with CCKEY_PRESS.
VOID ccSendKeySequence(PCLNTCONN pCC, ULONG cKeySeq, PULONG paulKeySeq);

VOID ccSendClipboardText(PCLNTCONN pCC, PSZ pszText);

// Sends chat command: ulCmd - CCCHAT_xxxxx,
//                     pszText - message text if ulCmd is VNC_CHAT_MESSAGE.
BOOL ccSendChat(PCLNTCONN pCC, ULONG ulCmd, PSZ pszText);

BOOL ccFXSupportDetected(PCLNTCONN pCC);

// Queries list of files on remote path pszPath. If pszPath is NULL drives list
// will be requested.
// See WM_VNC_FILEXFER messages (on replies).
BOOL ccFXRequestFileList(PCLNTCONN pCC, PSZ pszPath);

// Reseives a file.
BOOL ccFXRecvFile(PCLNTCONN pCC, PSZ pszRemoteName, PSZ pszLocalName);

BOOL ccFXAbortFileTransfer(PCLNTCONN pCC);

// Delete remote file or directory. Result will be returned by window message
// WM_VNC_FILEXFER, mp1 USHORT 1: CFX_DELETE_RES.
BOOL ccFXDelete(PCLNTCONN pCC, PSZ pszRemoteName);

// Create remote directory. Result will be returned by window message
// WM_VNC_FILEXFER, mp1 USHORT 1: CFX_MKDIR_RES.
BOOL ccFXMkDir(PCLNTCONN pCC, PSZ pszRemoteName);

// Rename remote file or directory. Result will be returned by window message
// WM_VNC_FILEXFER: mp1 USHORT 1 - CFX_RENAME_RES.
BOOL ccFXRename(PCLNTCONN pCC, PSZ pszRemoteName, PSZ pszNewName);

BOOL ccFXSendFile(PCLNTCONN pCC, PSZ pszRemoteName, PSZ pszLocalName);

VOID ccThreadWatchdog();

BOOL ccSearchListener(PSZ pszAddress, USHORT usPort);

#endif // CLNTCONN_H
