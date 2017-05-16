#ifndef CLNTWND_H
#define CLNTWND_H

// WM_VNC_STATE
// mp1 PCLNTCONN
// mp2 ULONG - RFBSTATE_xxxxx
#define WM_VNC_STATE             (WM_USER + 351)

// WM_VNC_SETCLIENTSIZE
// mp1 USHORT - width, USHORT - height.
#define WM_VNC_SETCLIENTSIZE     (WM_USER + 352)

// WM_VNC_UPDATE
// mp1 SHORT 1 - X left
//     SHORT 2 - Y bottom
// mp2 SHORT 1 - X right
//     SHORT 2 - Y top
#define WM_VNC_UPDATE            (WM_USER + 353)

// WM_VNC_CHAT
// mp1: ULONG - CCCHAT_OPEN, CCCHAT_CLOSE,  CCCHAT_MESSAGE.
// mp2: PSZ - message if mp1 is VNC_CHAT_MESSAGE.
#define WM_VNC_CHAT              (WM_USER + 354)

// CCCHAT_xxxxx : WM_VNC_CHAT, mp1
#define CCCHAT_OPEN              0
#define CCCHAT_CLOSE             1
#define CCCHAT_MESSAGE           2

// Messages from chatwnd module.
#define WM_CHATWIN               (WM_USER + 355)

// WM_CLIPBOARD
// mp1 ULONG - text length
// mp2 PCHAR - text
#define WM_VNC_CLIPBOARD         (WM_USER + 356)

// WM_VNC_FXDRIVES - remote drives list, message from filexfer module.
// This message will be redirected to fxdlg module.
// mp1 USHORT 1 - command CFX_xxxxx
//     USHORT 2 - data length (except CFX_xxxxx_RES)
// mp2 data:
//   CFX_DRIVES: PCHAR - C:?\0D:?\0\0 , where ? is 'l' (local) / 'f' (floppy) /
//                       'c' (cdrom) / 'n' (network),
//   CFX_PATH: PCHAR - length in mp2 USHORT 2; Remote path, begin of files list,
//   CFX_FINDDATA: RFB_FIND_DATA* - files list item,
//   CFX_xxxxx_FAIL: file/directory name, may be NULL,
//   CFX_EOF: NULL,
//   CFX_DELETE_RES, CFX_MKDIR_RES: mp1 USHORT 2 - 0: failed, 1: ok,
//                                  mp2 PSZ - remote file name.
//   CFX_RENAME_RES: mp1 USHORT 2 - 0: failed, 1: renamed,
//                   mp2 *PSZ - two PSZ, 0: old full name, 1: new full name.

#define WM_VNC_FILEXFER          (WM_USER + 357)
#define CFX_DRIVES                1
#define CFX_PATH                  2
#define CFX_FINDDATA              3
#define CFX_PERMISSION_DENIED     4
#define CFX_END_OF_LIST           5
#define CFX_DELETE_RES            6
#define CFX_MKDIR_RES             7
#define CFX_RENAME_RES            8
#define CFX_RECV_CREATE_FAIL      9
#define CFX_RECV_WRITE_FAIL      10
#define CFX_RECV_READ_FAIL       11
#define CFX_RECV_CHUNK           12
#define CFX_RECV_END_OF_FILE     13
#define CFX_SEND_WRITE_FAIL      14
#define CFX_SEND_READ_FAIL       15
#define CFX_SEND_CHUNK           16
#define CFX_SEND_END_OF_FILE     17

// Internal commands for WM_COMMAND.
#define CWC_NEW_CONNECTION       5100
#define CWC_VIEW_ONLY            5101
#define CWC_SEND_CAD             5102
#define CWC_SEND_CTRL_ESC        5103
#define CWC_SEND_ALT_ESC         5104
#define CWC_SEND_ALT_TAB         5105
#define CWC_CHAT                 5106
#define CWC_FILE_TRANSFER        5107
#define CWC_FILE_TRANSFER_CLOSE  5108
#define CWC_SCREENSHOT           5109

VOID cwInit();
VOID cwDone();

// Creates window for client connection object.
// pszWinTitle can be NULL or an empty string - title will be generated on
// server's information.
BOOL cwCreate(PCLNTCONN pCC, PSZ pszHost, PSZ pszWinTitle, HWND hwndNotify);

#endif // CLNTWND_H
