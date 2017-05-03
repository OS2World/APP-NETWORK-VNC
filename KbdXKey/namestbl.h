#ifndef NAMESTBL_H
#define NAMESTBL_H

// WM_CONTROL - Message for the owner
// mp2 USHORT 2 = NC_ENTER - user selected value (mp2 is PNOTIFYNTENTER).
#define NC_ENTER       1000

// WM_NTSHOW - Show names table,
//   mp1 ULONG - keysym code.
//   mp2 BOOL  - make window actived.
#define WM_NTSHOW      (WM_USER+1)

// WM_NTQUERYNAME - Query name for the keysym code, mp1 ULONG - keysym code.
// Returns: PSZ, name for the keysym code or NULL.
#define WM_NTQUERYNAME (WM_USER+2)

typedef struct _NOTIFYNTENTER {
  ULONG      ulValue;
  PSZ        pszName;
} NOTIFYNTENTER, *PNOTIFYNTENTER;

HWND ntLoad(HWND hwndOwner);

#endif // NAMESTBL_H
