#ifndef FILEXFER_H
#define FILEXFER_H

#include <rfb/rfbclient.h>

// RFB_FILETIME and RFB_FIND_DATA defined in rfbserver.c

#define MAX_PATH 260

#define RFB_FILE_ATTRIBUTE_READONLY   0x1
#define RFB_FILE_ATTRIBUTE_HIDDEN     0x2
#define RFB_FILE_ATTRIBUTE_SYSTEM     0x4
#define RFB_FILE_ATTRIBUTE_DIRECTORY  0x10
#define RFB_FILE_ATTRIBUTE_ARCHIVE    0x20
#define RFB_FILE_ATTRIBUTE_NORMAL     0x80
#define RFB_FILE_ATTRIBUTE_TEMPORARY  0x100
#define RFB_FILE_ATTRIBUTE_COMPRESSED 0x800

#pragma pack(1)

typedef struct {
  uint32_t dwLowDateTime;
  uint32_t dwHighDateTime;
} RFB_FILETIME; 

typedef struct {
  uint32_t dwFileAttributes;
  RFB_FILETIME ftCreationTime;
  RFB_FILETIME ftLastAccessTime;
  RFB_FILETIME ftLastWriteTime;
  uint32_t nFileSizeHigh;
  uint32_t nFileSizeLow;
  uint32_t dwReserved0;
  uint32_t dwReserved1;
  uint8_t  cFileName[ MAX_PATH ];
  uint8_t  cAlternateFileName[ 14 ];
} RFB_FIND_DATA;

#pragma pack()

// Register the extension.
VOID fxRegister();
// Should be called before rfbClientCleanup().
VOID fxClean(rfbClient* client);

// Read remote drives/files list.
// Sends back to the window WM_VNC_FILEXFER, mp1 USHORT 1 (see clntwnd.h):
//   pszPath is NULL or empty:  CFX_PERMISSION_DENIED or CFX_DRIVES,
//   pszPath is remote path: CFX_PATH, CFX_FINDDATA and CFX_END_OF_LIST.          5
// Returns FALSE if request was not sent.
BOOL fxFileListRequest(rfbClient* client, PSZ pszPath);

// WM_VNC_FILEXFER, mp1 USHORT 1:
//   CFX_RECV_FAIL
//     The server reports an error.
//   CFX_CREATE_FAIL
//     DosOpen() failed, mp2 ULONG - error code.
//   CFX_WRITE_FAIL
//     DosWrite() failed, mp2 ULONG - error code.
//   CFX_END_OF_FILE
//     mp2 PFILESTATUS3L - file information, may be changed.
//     Inform the window that the file end is reached. If window procedure
//     returns TRUE - local file information pointed by mp2 will be updated.
BOOL fxRecvFile(rfbClient* client, PSZ pszRemoteName, PSZ pszLocalName);
BOOL fxAbortFileTransfer(rfbClient* client);

// Delete remote file or directory. Result will be returned by window message
// WM_VNC_FILEXFER: mp1 USHORT 1 - CFX_DELETE_RES, mp1 USHORT 2 - 0 on fail or
// 1 for success and mp2 PSZ - remote file name.
BOOL fxDelete(rfbClient* client, PSZ pszRemoteName);

// Create remote directory. Result will be returned by window message
// WM_VNC_FILEXFER: mp1 USHORT 1 - CFX_MKDIR_RES, mp2 USHORT 1 - 0 on fail or
// 1 for success and mp2 PSZ - remote file name.
BOOL fxMkDir(rfbClient* client, PSZ pszRemoteName);

// Rename remote file or directory. Result will be returned by window message
// WM_VNC_FILEXFER: mp1 USHORT 1 - CFX_RENAME_RES, mp2 USHORT 1 - 0 on fail or
// 1 for success. mp2 PSZ apszMsgArg[2] - an old and new names.
BOOL fxRename(rfbClient* client, PSZ pszRemoteName, PSZ pszNewName);

BOOL fxSendFile(rfbClient* client, PSZ pszRemoteName, PSZ pszLocalName);

// Replacement libvncclient's WaitForMessage() to support files sending.
int fxWaitForMessage(rfbClient* client, unsigned int uiUSecs);

#endif // FILEXFER_H
