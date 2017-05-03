#ifndef LNCHPAD_H
#define LNCHPAD_H

BOOL lpOpenDlg();
BOOL lpCommitArg(int argc, char** argv);
BOOL lpStoreLogonInfo(PSZ pszHost, PCCLOGONINFO pLogonInfo);
BOOL lpQueryLogonInfo(PSZ pszHost, PCCLOGONINFO pLogonInfo,
                          BOOL fCredential);
BOOL lpStoreWinRect(PSZ pszHost, PRECTL pRect);
BOOL lpQueryWinRect(PSZ pszHost, PRECTL pRect);
VOID lpStoreWinPresParam(HWND hwnd);
VOID lpQueryWinPresParam(HWND hwnd);

#endif // LNCHPAD_H
