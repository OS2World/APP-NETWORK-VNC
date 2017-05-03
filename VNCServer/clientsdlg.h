#ifndef CLIENTS_H
#define CLIENTS_H

// WMCLNT_CLNTNUMCHANGED - message from GUI window.
#define WMCLNT_CLNTNUMCHANGED    (WM_USER+1)

// WMCLNT_LISTCLIENT - internal dialog message.
#define WMCLNT_LISTCLIENT        (WM_USER+2)

HWND clientsCreate(HAB hab, HWND hwndOwner);

#endif // CLIENTS_H
