#ifndef OS2KEY_H
#define OS2KEY_H

// KBDST_xxxxx for KBD$ ioctl KBD_GETSHIFTSTATE / KBD_SETSHIFTSTATE.

/*
  Bit 15 SysReq key down
  Bit 14 Caps Lock key down
  Bit 13 NumLock key down
  Bit 12 ScrollLock key down
  Bit 11 Right Alt key down
  Bit 10 Right Ctrl key down
  Bit 9 Left Alt key down
  Bit 8 Left Ctrl key down
  Bit 7 Insert on
  Bit 6 Caps Lock on
  Bit 5 NumLock on
  Bit 4 ScrollLock on
  Bit 3 Either Alt key down
  Bit 2 Either Ctrl key down
  Bit 1 Left Shift key down
  Bit 0 Right Shift key down
*/
#define KBDST_SHIFT_RIGHT_DOWN   0x0001
#define KBDST_SHIFT_LEFT_DOWN    0x0002
#define KBDST_CTRL_EITHER_DOWN   0x0004
#define KBDST_ALT_EITHER_DOWN    0x0008
#define KBDST_SCROLLLOCK_ON      0x0010
#define KBDST_NUMLOCK_ON         0x0020
#define KBDST_CAPSLOCK_ON        0x0040
#define KBDST_INSERT_ON          0x0080
#define KBDST_CTRL_LEFT_DOWN     0x0100
#define KBDST_ALT_LEFT_DOWN      0x0200
#define KBDST_CTRL_RIGHT_DOWN    0x0400
#define KBDST_ALT_RIGHT_DOWN     0x0800
#define KBDST_SCROLLLOCK_DOWN    0x1000
#define KBDST_NUMLOCK_DOWN       0x2000
#define KBDST_CAPSLOCK_DOWN      0x4000
#define KBDST_SYSREQ_DOWN        0x8000


typedef struct _XKEYREC {
  ULONG      ulKeysym;
  MPARAM     mp1;
  MPARAM     mp2;
} XKEYREC, *PXKEYREC;

typedef struct _XKBDMAP {
  ULONG      ulAllocated;
  ULONG      ulCount;
  XKEYREC    aList[1];
} XKBDMAP, *PXKBDMAP;

typedef struct _XKEVENTSTR {
  CHAR       acFlags[158];
  CHAR       acScan[8];
  CHAR       acChar[8];
  CHAR       acVK[16];
} XKEVENTSTR, *PXKEVENTSTR;

typedef struct _XKFROMMP {
  // Last pressed keys. Internal using.
  struct {
    ULONG    ucScan;
    ULONG    ulKeysym;
  }          aKeysPressed[4];

  // Output.
  struct {
    ULONG    ulKeysym;
    BOOL     fPressed;
  }          aOutput[4];
  ULONG      cOutput;

} XKFROMMP, *PXKFROMMP;


#define XKFKSFL_SHIFT_L          0x01
#define XKFKSFL_SHIFT_R          0x01
#define XKFKSFL_CTRL_L           0x02
#define XKFKSFL_CTRL_R           0x04
#define XKFKSFL_ALT              0x08
#define XKFKSFL_ALTGR            0x10
#define XKFKSFL_SHIFT_MASK       (XKFKSFL_SHIFT_L | XKFKSFL_SHIFT_R)
#define XKFKSFL_CTRL_MASK        (XKFKSFL_CTRL_L | XKFKSFL_CTRL_R)
#define XKFKSFL_ALT_MASK         (XKFKSFL_ALT | XKFKSFL_ALTGR)

typedef struct _XKFROMKEYSYM {
//  ULONG      ulFlags;            // XKFKSFL_xxxxx
  BYTE       abState[256];       // Index: system scancode (as for WM_CHAR),
                                 // Value: 0x01 - key pressed, 0x02 - toggle.

  // Output.
  struct {
    PMPARAM  mp1;
    PMPARAM  mp2;
  }          aOutput[4];
  ULONG      cOutput;

} XKFROMKEYSYM, *PXKFROMKEYSYM;


// xkKeysymFromMP() result, XKEYMETHOD_xxxxx
#define XKEYMETHOD_NOTFOUND      0
#define XKEYMETHOD_AUTO          1
#define XKEYMETHOD_EXACT         2
#define XKEYMETHOD_HEURISTIC     3
#define XKEYMETHOD_WASPRESSED    4

// Used WM_CHAR flags.
#define XKEYKC_MASK    ~(KC_PREVDOWN | KC_KEYUP | KC_TOGGLE | KC_INVALIDCHAR | \
                         KC_DBCSRSRVD1 | KC_DBCSRSRVD2)
#define XKEYERR_KEYSYM           1
#define XKEYERR_CHAR             2
#define XKEYERR_FLAG             3
#define XKEYERR_SCAN             4
#define XKEYERR_VK               5
typedef VOID (*PFNLOADMAPERROR)(PSZ pszFile, ULONG ulLine, ULONG ulCode);

BOOL EXPENTRY xkCompMP(MPARAM m1p1, MPARAM m1p2, MPARAM m2p1, MPARAM m2p2);

// Determines keysym for WM_CHAR paramethers mp1 and mp2.
// For key-pressed events only!
// Returns 0 if keysym can not be found.
ULONG EXPENTRY xkKeysymFromMPAuto(MPARAM mp1, MPARAM mp2);

// Searches keysym in keymap for WM_CHAR paramethers mp1 and mp2.
// Result: used method (XKEYMETHOD_xxxxx).
#define xkKeysymFromMPStart(_pXKFromMP) memset(_pXKFromMP,0,sizeof(XKFROMMP))
ULONG EXPENTRY xkKeysymFromMP(PXKBDMAP pMap, MPARAM mp1, MPARAM mp2,
                              PXKFROMMP pXKFromMP);
BOOL EXPENTRY xkKeysymFromMPCheck(PXKFROMMP pXKFromMP);

VOID EXPENTRY xkMPFromKeysymStart(PXKFROMKEYSYM pXKFromKeysym);
BOOL EXPENTRY xkMPFromKeysym(PXKBDMAP pMap, ULONG ulKeysym, BOOL fPressed,
                             PXKFROMKEYSYM pXKFromKeysym);

VOID EXPENTRY xkMakeMPForVIO(MPARAM mp1, PMPARAM pmp2);

PXKBDMAP EXPENTRY xkMapNew();
VOID EXPENTRY xkMapFree(PXKBDMAP pMap);
BOOL EXPENTRY xkMapInsert(PXKBDMAP *ppMap, ULONG ulKeysym,
                          MPARAM mp1, MPARAM mp2);

// pszFile may be a list of filenames separated with '&'. Key %L will be
// replaced with system codepage number.
PXKBDMAP EXPENTRY xkMapLoad(PSZ pszFile, PFNLOADMAPERROR pfnError);

VOID EXPENTRY xkEventToStr(MPARAM mp1, MPARAM mp2, PXKEVENTSTR pEvent);
BOOL EXPENTRY xkMapSave(PXKBDMAP pMap, PSZ pszFile);

#endif // OS2KEY_H
