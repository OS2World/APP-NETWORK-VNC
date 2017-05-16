#ifndef NBPAGE_H
#define NBPAGE_H

#define WM_NBSERVERPAGE_UPDDYNICONCB       (WM_USER + 1)

MRESULT EXPENTRY nbPageServer(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
MRESULT EXPENTRY nbPageEncodings(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);

#endif // NBPAGE_H
