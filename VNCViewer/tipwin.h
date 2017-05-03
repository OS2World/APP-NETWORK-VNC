#ifndef TIPWIN_H
#define TIPWIN_H

// WM_TIP - From tip window for controls owner.
// mp1:
//   SHORT - control ID,
//   SHORT - 0 - tip window not ready, 1 - tip is ready to accept a text.
// mp2: HWND - control window handle.
#define WM_TIP         (WM_USER + 11532)

// WM_TIP_TEXT - Message for tip window
// mp1: PCHAR - tip text or NULL.
// mp2:
//   SHORT - mp1 not a NULL - tip text length,
//           mp1 is NULL - resource message table ID (if mp1 is not a NULL).
//   SHORT - NOT ZERO - use resource string table instead message table.
// Special character: 0x01 (\1) - carriage return.
#define WM_TIP_TEXT    (WM_USER + 1)

// VOID twSet(HWND hwnd)
// Sets tip support for all controls in window (hwnd is parent for controls).
// Controls owner will receive messages WM_TIP and can set tip text with
// WM_TIP_TEXT message to the twQueryTipWinHandle() window to show the tip.
VOID twSet(HWND hwnd);

// HWND twQueryTipWinHandle()
// Returns handle of tip window to send WM_TIP_TEXT.
HWND twQueryTipWinHandle();

#endif
