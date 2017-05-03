#ifndef PSWDLG_H
#define PSWDLG_H

#include "clntconn.h"

// WM_PSWD_ENTER - Message for owner.
// mp1 - PCCLOGONINFO, Not NULL: user has entered the logon information and
//       clicked OK; NULL - user clicked Cancel.
// mp2 - HWND, dialog window handle
#define WM_PSWD_ENTER  (WM_USER + 450)

typedef struct _PSWNDLGWMENTER {
  BOOL       fCredential;
  CHAR       acUserName[256];
  CHAR       acPassword[256];
} PSWNDLGWMENTER, *PPSWNDLGWMENTER;

HWND pswdlgOpen(PCLNTCONN pCC, HWND hwndOwner, BOOL fCredential);

#endif // PSWDLG_H
