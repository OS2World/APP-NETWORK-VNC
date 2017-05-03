#ifndef SRVWIN_H
#define SRVWIN_H

// WMSRV_RECONFIG - RFB server reconfiguration.
// mp1 PCONFIG
#define WMSRV_RECONFIG           (WM_USER + 1)

// WMSRV_CHAT_MESSAGE - Send message from the chat window to the client.
// mp1 rfbClientPtr
// mp2 PSZ          - message text or NULL to send chat-open request..
#define WMSRV_CHAT_MESSAGE       (WM_USER + 2)

// WMSRV_CHAT_WINDOW - Set chat window for client (only "close chat" releazed).
// mp1 rfbClientPtr
// mp2 HWND         - NULLHANDLE: chat window closure.
#define WMSRV_CHAT_WINDOW        (WM_USER + 3)

// WMSRV_CLNT_NUM_CHANGED - Clients counter was changed.
// mp1 SHORT 1 - current clients number.
//     SHORT 2 - TRUE: client connected.
//               FALSE: client disconnected.
// mp2 rfbClientPtr
#define WMSRV_CLNT_NUM_CHANGED   (WM_USER + 4)

// WMSRV_ATTACH - Attach listening client or dispatcher.
// mp1 ULONG - connected socket.
// mp2 ULONG - FALSE: attach listening client.
//             TRUE: attach dispatcher (not releazed).
#define WMSRV_ATTACH             (WM_USER + 5)

// WMSRV_LIST_CLIENTS - List all clients
// mp1 HWND - window to send back message.
// mp2 ULONG - window message.
// Will send back message (ULONG)mp2 to (HWND)mp1:
//   mp1 rfbClientPtr
//   mp2 - reserved
#define WMSRV_LIST_CLIENTS       (WM_USER + 6)

// WMSRV_DISCONNECT - Close client connection.
// mp1 rfbClientPtr
#define WMSRV_DISCONNECT         (WM_USER + 7)

#endif // SRVWIN_H
