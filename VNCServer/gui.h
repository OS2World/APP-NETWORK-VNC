#ifndef GUI_H
#define GUI_H

// WMGUI_CLNT_NUM_CHANGED - Clients counter was changed. The message will be
// sent send from server hidden window to the gui main window.
// mp1 SHORT 1 - current clients number.
//     SHORT 2 - TRUE: client connected.
//               FALSE: client disconnected.
// mp2 rfbClientPtr
#define WMGUI_CLNT_NUM_CHANGED   (WM_USER + 1)

// WMGUI_CHAT_OPEN - Open chat dialog.
// mp1 rfbClientPtr
// Returns chat window handle.
#define WMGUI_CHAT_OPEN          (WM_USER + 2)

// Message from extended system tray widget for XCenter/eCenter (for module
// internal use).
#define WMGUI_XSYSTRAY           (WM_USER + 3)

#define WMGUI_CREATE             (WM_USER + 4)

// Messages from chatwin module.
#define WMGUI_CHATWIN            (WM_USER + 5)

BOOL guiInit(PCONFIG pConfig, ULONG ulGUIShowTimeout);
VOID guiDone();

#endif // GUI_H
