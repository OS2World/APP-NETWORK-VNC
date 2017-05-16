/*
  Inter-process communication for VNC Viewer.
*/

#ifndef VNCVIPC_H
#define VNCVIPC_H

// Window message from VNC Viewer to application window specified with -N.
#define VNCVI_ATOM_WMVNCVNOTIFY            "WM_VNCVNOTIFY"

// mp1, SHORT 1 - VNCVNOTIFY_xxxxx.
// VNCVNOTIFY_CLIENTWINDOW, m2: HWND - client window handle.
#define VNCVNOTIFY_CLIENTWINDOW            1
// VNCVNOTIFY_SETCLIENTSIZE, mp2: SHORT 1 - cx, SHORT 1 - cy
#define VNCVNOTIFY_SETCLIENTSIZE           2

// Window messages from application to VNC Viewer's client window.

// WM_APP2VNCV_SCREENSHOT
//   mp1: ULONG - size of a shared memory object,
//   mp2: PVOID - address of the allocated memory.
// Memory shoul be allocated by requester with
// DosAllocSharedMem(,,,OBJ_GETTABLE | PAG_READ | PAG_WRITE).
#define WM_APP2VNCV_SCREENSHOT             (WM_USER + 500)

#endif // VNCVIPC_H
