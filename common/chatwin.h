#ifndef CHATWIN_H
#define CHATWIN_H

// Message to owner ( registered with cwCreate() ).
// mp1 ULONG - CWM_xxxxx
// mp2 PCWMSGDATA
#define CWM_MESSAGE              0
#define CWM_CLOSE                1

// Message for the chat window.
#define WMCHAT_MESSAGE           (WM_USER+1)
// mp1 ULONG - type of message CWMT_xxxxx
// mp2 PWMCHATMSG if mp1 is CWMT_MSG_xxxxx,
//     PWMCHATSYS if mp1 is CWMT_SYSTEM
//     PSZ if mp1 is CWMT_LOCAL_NAME
#define CWMT_MSG_REMOTE          0
#define CWMT_MSG_LOCAL           1
#define CWMT_SYSTEM              2
#define CWMT_LOCAL_NAME          3

// Resource IDs
#define IDDLG_CHAT               36000
#define IDMLE_HISTORY            36001
#define IDEF_MESSAGE             36002


typedef struct _CWMSGDATA {
  PVOID      pUser;
  ULONG      cbText;
  PSZ        pszText;
  HWND       hwndChat;
} CWMSGDATA, *PCWMSGDATA;

typedef struct _WMCHATMSG {
  PSZ        pszUser;
  ULONG      cbText;
  PCHAR      pcText;
} WMCHATMSG, *PWMCHATMSG;

typedef struct _WMCHATSYS {
  BOOL       fAllow;
  ULONG      cbText;
  PCHAR      pcText;
} WMCHATSYS, *PWMCHATSYS;

// ulWinMsg - user defined message for owner window.
// pUser - user data (will be pointed with ulWinMsg message).
// Returns chat window handle or NULLHANDLE on error.
HWND EXPENTRY chatwinCreate(HWND hwndOwner, ULONG ulWinMsg, PVOID pUser);

#endif // CHATWIN_H
