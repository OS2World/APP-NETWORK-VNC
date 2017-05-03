#ifndef RFBSRV_H
#define RFBSRV_H

#include <rfb/rfb.h>
#include <os2xkey.h>

#define CLNTDATA_MODKEY_SHIFT    0x01
#define CLNTDATA_MODKEY_CTRL     0x02
#define CLNTDATA_MODKEY_ALT      0x04
#define CLNTDATA_MODKEY_ALTGR    0x08
#define CLNTDATA_MODKEY_ALTS     (CLNTDATA_MODKEY_ALT | CLNTDATA_MODKEY_ALTGR)

typedef struct _CLIENTDATA {
  ULONG                ulBtnFlags;
  ULONG                ulModKey;           // CLNTDATA_MODKEY_xxxxx
  HWND                 hwndChat;
  time_t               timeConnect;
  int                  iLastChatMsgType;
  PSZ                  pszExtProgId;
  XKFROMKEYSYM         stXKFromKeysym;
} CLIENTDATA, *PCLIENTDATA;

typedef BOOL (*PFNCLIENT)(rfbClientPtr prfbClient, PVOID pUser);

BOOL rfbsInit(PCONFIG pConfig);
VOID rfbsDone();
BOOL rfbsSetServer(PCONFIG pConfig);
VOID rfbsSetMouse();
VOID rfbsUpdateScreen(RECTL rectlUpdate);
VOID rfbsSetPalette(ULONG cColors, PRGB2 pColors);
VOID rfbsProcessEvents(ULONG ulTimeout);
VOID rfbsSendClipboardText(PSZ pszText);
BOOL rbfsSendChatMsg(rfbClientPtr prfbClient, PSZ pszText);
BOOL rbfsSetChatWindow(rfbClientPtr prfbClient, HWND hwnd);
ULONG rbfsCheckPorts(in_addr_t inaddrListen, ULONG ulPort, ULONG ulHTTPPort);
BOOL rfbsAttach(int iSock, BOOL fDispatcher);
VOID rfbsForEachClient(PFNCLIENT fnClient, PVOID pUser);
BOOL rfbsDisconnect(rfbClientPtr prfbClient);

#endif // RFBSRV_H
