#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#define INCL_WIN
#define INCL_DOSNLS
#define INCL_DOSERRORS
#define INCL_DOSDEVICES
#define INCL_DOSDEVIOCTL
#include <os2.h>
#define XK_3270
#include <keysym.h>
#include <geniconv.h>
#include <utils.h>
#include "os2xkey.h"
#include <debug.h>

/* Valid values for ddFlags field */
#define KDD_MONFLAG1    0x8000
#define KDD_MONFLAG2	0x4000
#define KDD_RESERVED	0x3C00
#define KDD_ACCENTED	0x0200
#define KDD_MULTIMAKE	0x0100
#define KDD_SECONDARY	0x0080
#define KDD_BREAK	0x0040
#define KDD_EXTENDEDKEY	0x0020
#define KDD_ACTIONFIELD	0x001F
#define KDD_KC_LONEKEY	0x8000
#define KDD_KC_PREVDOWN	0x4000
#define KDD_KC_KEYUP	0x2000
#define KDD_KC_ALT	0x1000
#define KDD_KC_CTRL	0x0800
#define KDD_KC_SHIFT	0x0400
#define KDD_KC_FLAGS	0x0FC00
/* Valid values for KDD_ACTIONFIELD portion of ddFlags field */
#define KDD_PUTINKIB	0x0000
#define KDD_ACK	        0x0001
#define KDD_PREFIXKEY	0x0002
#define KDD_OVERRUN	0x0003
#define KDD_RESEND	0x0004
#define KDD_REBOOTKEY	0x0005
#define KDD_DUMPKEY	0x0006
#define KDD_SHIFTKEY	0x0007
#define KDD_PAUSEKEY	0x0008
#define KDD_PSEUDOPAUSE	0x0009
#define KDD_WAKEUPKEY	0x000A
#define KDD_BADACCENT	0x000B
#define KDD_HOTKEY	0x000C
#define KDD_ACCENTKEY	0x0010
#define KDD_BREAKKEY	0x0011
#define KDD_PSEUDOBREAK	0x0012
#define KDD_PRTSCKEY	0x0013
#define KDD_PRTECHOKEY	0x0014
#define KDD_PSEUDOPRECH	0x0015
#define KDD_STATUSCHG	0x0016
#define KDD_WRITTENKEY	0x0017
#define KDD_UNDEFINED	0x001F

#define _KEYMAP_INIT_RECORDS     128
#define _KEYMAP_DELTA_RECORDS    32

static PSZ       apszVK[] =
{
  "",
  "VK_BUTTON1",
  "VK_BUTTON2",
  "VK_BUTTON3",
  "VK_BREAK",
  "VK_BACKSPACE",
  "VK_TAB",
  "VK_BACKTAB",
  "VK_NEWLINE",
  "VK_SHIFT",
  "VK_CTRL",
  "VK_ALT",
  "VK_ALTGRAF",
  "VK_PAUSE",
  "VK_CAPSLOCK",
  "VK_ESC",
  "VK_SPACE",
  "VK_PAGEUP",
  "VK_PAGEDOWN",
  "VK_END",
  "VK_HOME",
  "VK_LEFT",
  "VK_UP",
  "VK_RIGHT",
  "VK_DOWN",
  "VK_PRINTSCRN",
  "VK_INSERT",
  "VK_DELETE",
  "VK_SCRLLOCK",
  "VK_NUMLOCK",
  "VK_ENTER",
  "VK_SYSRQ",
  "VK_F1",
  "VK_F2",
  "VK_F3",
  "VK_F4",
  "VK_F5",
  "VK_F6",
  "VK_F7",
  "VK_F8",
  "VK_F9",
  "VK_F10",
  "VK_F11",
  "VK_F12",
  "VK_F13",
  "VK_F14",
  "VK_F15",
  "VK_F16",
  "VK_F17",
  "VK_F18",
  "VK_F19",
  "VK_F20",
  "VK_F21",
  "VK_F22",
  "VK_F23",
  "VK_F24",
  "VK_ENDDRAG",
  "VK_CLEAR",
  "VK_EREOF",
  "VK_PA1",
  "VK_ATTN",
  "VK_CRSEL",
  "VK_EXSEL",
  "VK_COPY",
  "VK_BLK1",
  "VK_BLK2"
};

static struct { ULONG ulScan; ULONG ulKeysym; } aScanKeysym[] =
{ { 0x0e, XK_BackSpace },
  { 0x0f, XK_Tab },
  { 0x1c, XK_Return },
  { 0x5f, XK_Pause },
  { 0x01, XK_Escape },
  { 0x39, XK_space },
  { 0x69, XK_Delete },
  { 0x52, XK_KP_Insert }, //XK_KP_0 },
  { 0x4f, XK_KP_End }, //XK_KP_1 },
  { 0x50, XK_KP_Down }, //XK_KP_2 },
  { 0x51, XK_KP_Page_Down }, //XK_KP_3 },
  { 0x4b, XK_KP_Left }, //XK_KP_4 },
  { 0x4c, XK_KP_Space }, // ??? Key '5' when NumLock is off. //XK_KP_5 },
  { 0x4d, XK_KP_Right }, //XK_KP_6 },
  { 0x47, XK_KP_Home }, //XK_KP_7 },
  { 0x48, XK_KP_Up }, //XK_KP_8 },
  { 0x49, XK_KP_Page_Up }, //XK_KP_9 },
  { 0x53, XK_KP_Delete }, //XK_KP_Decimal },
  { 0x5c, XK_KP_Divide },
  { 0x37, XK_KP_Multiply },
  { 0x4a, XK_KP_Subtract },
  { 0x4e, XK_KP_Add },
  { 0x5a, XK_KP_Enter },
  { 0x61, XK_Up },
  { 0x66, XK_Down },
  { 0x64, XK_Right },
  { 0x63, XK_Left },
  { 0x68, XK_Insert },
  { 0x60, XK_Home },
  { 0x65, XK_End },
  { 0x62, XK_Page_Up },
  { 0x67, XK_Page_Down },
  { 0x3b, XK_F1 },
  { 0x3c, XK_F2 },
  { 0x3d, XK_F3 },
  { 0x3e, XK_F4 },
  { 0x3f, XK_F5 },
  { 0x40, XK_F6 },
  { 0x41, XK_F7 },
  { 0x42, XK_F8 },
  { 0x43, XK_F9 },
  { 0x44, XK_F10 },
  { 0x57, XK_F11 },
  { 0x58, XK_F12 },
  { 0x45, XK_Num_Lock },
  { 0x3a, XK_Caps_Lock },
  { 0x46, XK_Scroll_Lock },
  { 0x36, XK_Shift_R },
  { 0x2a, XK_Shift_L },
  { 0x5b, XK_Control_R },
  { 0x1d, XK_Control_L },
  { 0x5e, XK_Alt_R },
  { 0x38, XK_Alt_L },
  { 0x7e, XK_Super_L },
  { 0x7f, XK_Super_R },
  { 0x5d, XK_Print } };

static struct { ULONG ulKeysym; ULONG ulVK; } aKeysymVK[] =
{ { XK_Alt_L, VK_ALT },
  { XK_Alt_R, VK_ALTGRAF },
  { XK_Control_L, VK_CTRL },
  { XK_Control_R, VK_CTRL },
  { XK_Shift_L, VK_SHIFT },
  { XK_Shift_R, VK_SHIFT },
  { XK_space, VK_SPACE },
  { XK_BackSpace, VK_BACKSPACE },
  { XK_Tab, VK_TAB },
//    { , VK_BACKTAB }, // Shift + Tab 

  { XK_Break, VK_BREAK },
  { XK_Pause, VK_PAUSE },
  { XK_Linefeed, VK_NEWLINE },

  { XK_End, VK_END },
  { XK_Home, VK_HOME },
  { XK_Left, VK_LEFT },
  { XK_Up, VK_UP },
  { XK_Down, VK_DOWN },
  { XK_Right, VK_RIGHT },
  { XK_Page_Down, VK_PAGEDOWN },
  { XK_Page_Up, VK_PAGEUP },
  { XK_Delete, VK_DELETE },
  { XK_Insert, VK_INSERT },

  { XK_Caps_Lock, VK_CAPSLOCK },
  { XK_Num_Lock, VK_NUMLOCK },
  { XK_Scroll_Lock, VK_SCRLLOCK },
  { XK_Print, VK_PRINTSCRN },
  { XK_Return, VK_ENTER },
  { XK_Escape, VK_ESC },
  { XK_Sys_Req, VK_SYSRQ },

  { XK_Clear, VK_CLEAR },
  { XK_3270_EraseEOF, VK_EREOF },
  { XK_3270_PA1, VK_PA1 },
  { XK_3270_Attn, VK_ATTN },
  { XK_3270_CursorSelect, VK_CRSEL },
  { XK_3270_ExSelect, VK_EXSEL },
  { XK_3270_Copy, VK_COPY },
  { XK_Menu, VK_MENU },

  { XK_F1, VK_F1 },
  { XK_F2, VK_F2 },
  { XK_F3, VK_F3 },
  { XK_F4, VK_F4 },
  { XK_F5, VK_F5 },
  { XK_F6, VK_F6 },
  { XK_F7, VK_F7 },
  { XK_F8, VK_F8 },
  { XK_F9, VK_F9 },
  { XK_F10, VK_F10 },
  { XK_F11, VK_F11 },
  { XK_F12, VK_F12 },
  { XK_F13, VK_F13 },
  { XK_F14, VK_F14 },
  { XK_F15, VK_F15 },
  { XK_F16, VK_F16 },
  { XK_F17, VK_F17 },
  { XK_F18, VK_F18 },
  { XK_F19, VK_F19 },
  { XK_F20, VK_F20 },
  { XK_F21, VK_F21 },
  { XK_F22, VK_F22 },
  { XK_F23, VK_F23 },
  { XK_F24, VK_F24 } };

// Virtual key code - scan code for VIO.
static struct _VKSCAN2 {
  UCHAR      ucNoMod;
  UCHAR      ucCtrl;
  UCHAR      ucAlt;
  UCHAR      ucShift;
} aVKScan2[] =
{ { 0x00, 0x00, 0x00, 0x00 },   // (0x00) none
  { 0x00, 0x00, 0x00, 0x00 },   // (0x01) VK_BUTTON1
  { 0x00, 0x00, 0x00, 0x00 },   // (0x02) VK_BUTTON2
  { 0x00, 0x00, 0x00, 0x00 },   // (0x03) VK_BUTTON3
  { 0x00, 0x00, 0x00, 0x00 },   // (0x04) VK_BREAK
  { 0x0E, 0x0E, 0x0E, 0x0E },   // (0x05) VK_BACKSPACE
  { 0x0F, 0x94, 0x0F, 0x0F },   // (0x06) VK_TAB
  { 0x00, 0x00, 0x00, 0x00 },   // (0x07) VK_BACKTAB
  { 0x1C, 0x1C, 0x1C, 0x1C },   // (0x08) VK_NEWLINE
  { 0x00, 0x00, 0x00, 0x00 },   // (0x09) VK_SHIFT
  { 0x00, 0x00, 0x00, 0x00 },   // (0x0A) VK_CTRL
  { 0x00, 0x00, 0x00, 0x00 },   // (0x0B) VK_ALT
  { 0x00, 0x00, 0x00, 0x00 },   // (0x0C) VK_ALTGRAF
  { 0x00, 0x00, 0x00, 0x00 },   // (0x0D) VK_PAUSE
  { 0x00, 0x00, 0x00, 0x00 },   // (0x0E) VK_CAPSLOCK
  { 0x01, 0x01, 0x01, 0x01 },   // (0x0F) VK_ESC
  { 0x39, 0x39, 0x39, 0x39 },   // (0x10) VK_SPACE
  { 0x49, 0x84, 0x99, 0x49 },   // (0x11) VK_PAGEUP
  { 0x51, 0x76, 0xA1, 0x51 },   // (0x12) VK_PAGEDOWN
  { 0x4F, 0x75, 0x9F, 0x4F },   // (0x13) VK_END
  { 0x47, 0x77, 0x97, 0x47 },   // (0x14) VK_HOME
  { 0x4B, 0x73, 0x9B, 0x4B },   // (0x15) VK_LEFT
  { 0x48, 0x8D, 0x98, 0x48 },   // (0x16) VK_UP
  { 0x4D, 0x74, 0x9D, 0x4D },   // (0x17) VK_RIGHT
  { 0x50, 0x91, 0xA0, 0x50 },   // (0x18) VK_DOWN
  { 0x00, 0x00, 0x00, 0x00 },   // (0x19) VK_PRINTSCRN
  { 0x52, 0x92, 0xA2, 0x52 },   // (0x1A) VK_INSERT
  { 0x53, 0x93, 0xA3, 0x53 },   // (0x1B) VK_DELETE
  { 0x00, 0x00, 0x00, 0x00 },   // (0x1C) VK_SCRLLOCK
  { 0x00, 0x00, 0x00, 0x00 },   // (0x1D) VK_NUMLOCK
  { 0x1C, 0x1C, 0x1C, 0x1C },   // (0x1E) VK_ENTER
  { 0x00, 0x00, 0x00, 0x00 },   // (0x1F) VK_SYSRQ
  { 0x3B, 0x5E, 0x68, 0x54 },   // (0x20) VK_F1
  { 0x3C, 0x5F, 0x69, 0x55 },   // (0x21) VK_F2
  { 0x3D, 0x60, 0x6A, 0x56 },   // (0x22) VK_F3
  { 0x3E, 0x61, 0x6B, 0x57 },   // (0x23) VK_F4
  { 0x3F, 0x62, 0x6C, 0x58 },   // (0x24) VK_F5
  { 0x40, 0x63, 0x6D, 0x59 },   // (0x25) VK_F6
  { 0x41, 0x64, 0x6E, 0x5A },   // (0x26) VK_F7
  { 0x42, 0x65, 0x6F, 0x5B },   // (0x27) VK_F8
  { 0x43, 0x66, 0x70, 0x5C },   // (0x28) VK_F9
  { 0x44, 0x67, 0x71, 0x5D },   // (0x29) VK_F10
  { 0x85, 0x89, 0x8B, 0x87 },   // (0x2A) VK_F11
  { 0x86, 0x8A, 0x8C, 0x88 } }; // (0x2B) VK_F12


static ULONG EXPENTRY _xkKeysymFromScan(ULONG ulScan)
{
  ULONG      ulIdx;

  for( ulIdx = 0; ulIdx < ARRAYSIZE(aScanKeysym); ulIdx++ )
    if ( aScanKeysym[ulIdx].ulScan == ulScan )
      return aScanKeysym[ulIdx].ulKeysym;

  return 0;
}

static ULONG EXPENTRY _xkKeysymFromVK(ULONG ulVK, ULONG ulScan)
{
  ULONG      ulIdx;

  if ( ulVK == VK_CTRL )
    return ulScan == 0x5B ? XK_Control_R : XK_Control_L;

  for( ulIdx = 0; ulIdx < ARRAYSIZE(aKeysymVK); ulIdx++ )
    if ( aKeysymVK[ulIdx].ulVK == ulVK )
      return aKeysymVK[ulIdx].ulKeysym;

  return 0;
}


BOOL EXPENTRY xkCompMP(MPARAM m1p1, MPARAM m1p2, MPARAM m2p1, MPARAM m2p2)
{
  USHORT     usFl1  = SHORT1FROMMP(m1p1) & XKEYKC_MASK;
  USHORT     usFl2  = SHORT1FROMMP(m2p1) & XKEYKC_MASK;
  
  return (
           ( usFl1 == usFl2 )
         &&
           (
             ( (usFl1 & KC_SCANCODE) == 0 ? 0 : (SHORT2FROMMP(m1p1) >> 8) ) ==
             ( (usFl2 & KC_SCANCODE) == 0 ? 0 : (SHORT2FROMMP(m2p1) >> 8) )
           )
         &&
           (
             ( (usFl1 & KC_CHAR) == 0 ? 0 : SHORT1FROMMP(m1p2) ) ==
             ( (usFl2 & KC_CHAR) == 0 ? 0 : SHORT1FROMMP(m2p2) )
           )
         &&
           (
             ( (usFl1 & KC_VIRTUALKEY) == 0 ? 0 : SHORT2FROMMP(m1p2) ) ==
             ( (usFl2 & KC_VIRTUALKEY) == 0 ? 0 : SHORT2FROMMP(m2p2) )
           )
         );
}

ULONG EXPENTRY xkKeysymFromMPAuto(MPARAM mp1, MPARAM mp2)
{
  USHORT     usFlags  = SHORT1FROMMP(mp1);
  UCHAR      ucScan   = SHORT2FROMMP(mp1) >> 8;
  USHORT     usChar   = SHORT1FROMMP(mp2);
  USHORT     usVK     = SHORT2FROMMP(mp2);
  BOOL       fChar;
  ULONG      ulKeysym = 0;

  if ( (usFlags & KC_KEYUP) != 0 )
    // For key-pressed events only!
    return 0;

  if ( (usFlags & KC_SCANCODE) != 0 )
  {
    if ( (ucScan >= 0x47) && (ucScan <= 0x53) &&
         ( (usFlags & KC_CHAR) != 0 ) )
    {
      // Numeric keypad when NumLock is on.

      switch( usChar )
      {
        case '0': ulKeysym = XK_KP_0; break;
        case '1': ulKeysym = XK_KP_1; break;
        case '2': ulKeysym = XK_KP_2; break;
        case '3': ulKeysym = XK_KP_3; break;
        case '4': ulKeysym = XK_KP_4; break;
        case '5': ulKeysym = XK_KP_5; break;
        case '6': ulKeysym = XK_KP_6; break;
        case '7': ulKeysym = XK_KP_7; break;
        case '8': ulKeysym = XK_KP_8; break;
        case '9': ulKeysym = XK_KP_9; break;
        case ',':
        case '.': ulKeysym = XK_KP_Decimal; break;
      }
    }

    if ( ulKeysym == 0 )
      // keysym not found - try to get it from scan code.
      ulKeysym = _xkKeysymFromScan( ucScan );
  }

  if ( ( ulKeysym == 0 ) && ( (usFlags & KC_VIRTUALKEY) != 0 ) )
    // keysym not found - try to get it from virtual key code.
    ulKeysym = _xkKeysymFromVK( usVK, ucScan );

  fChar = ( (usFlags & KC_CHAR) != 0 ) ||
          ( (usFlags & (KC_CTRL | KC_ALT)) != 0 /*&&
            usChar != 0 && usChar <= 0xFF*/ );

  if ( ( ulKeysym == 0 ) && fChar )
  {
    if ( usChar <= 127 )
      // keysym equals character code < 0xFF
      ulKeysym = usChar;
    else
    {
      /* RFC 6143, 7.5.4.  KeyEvent
         Modern versions of the X Window System handle keysyms for Unicode
         characters, consisting of the Unicode character with the hex
         1000000 bit set.  For maximum compatibility, if a key has both a
         Unicode and a legacy encoding, clients should send the legacy
         encoding.
      */
      CHAR   acBuf[4];
      CHAR   acUTF8[4];

      *((PUSHORT)acBuf) = usChar;

      if ( StrUTF8( 1, acUTF8, sizeof(acUTF8), acBuf, acBuf[1] == 0 ? 1 : 2 )
           > 0 )
        ulKeysym = *((PULONG)acUTF8) | 0x1000000;
      else
        ulKeysym = usChar;
    }
  }

  return ulKeysym;
}

ULONG EXPENTRY xkKeysymFromMP(PXKBDMAP pMap, MPARAM mp1, MPARAM mp2,
                              PXKFROMMP pXKFromMP)
{
  PXKEYREC   pRecord;
  ULONG      ulKPIdx, ulIdx;
  USHORT     usFlags = SHORT1FROMMP(mp1);
  UCHAR      ucScan  = SHORT2FROMMP(mp1) >> 8;
  USHORT     usChar  = SHORT1FROMMP(mp2);

  // Search scancode in previous-pressed array.
  for( ulKPIdx = 0; ulKPIdx < ARRAYSIZE(pXKFromMP->aKeysPressed); ulKPIdx++ )
  {
    if ( pXKFromMP->aKeysPressed[ulKPIdx].ucScan == ucScan )
      break;
  }

  pXKFromMP->cOutput = 0;

  if ( (usFlags & KC_KEYUP) == 0 )
  {
    // Key is pressed.

    ULONG    ulKeysym = 0;
    ULONG    ulMethod = XKEYMETHOD_NOTFOUND;

    usFlags = usFlags & XKEYKC_MASK;
    mp1 = MPFROMSH2CH( usFlags, 0, ucScan );

    // Search in key-map table.
    for( ulIdx = 0, pRecord = pMap->aList; ulIdx < pMap->ulCount;
         ulIdx++, pRecord++ )
    {
      if ( xkCompMP( pRecord->mp1, pRecord->mp2, mp1, mp2 ) )
      {
        ulKeysym = pRecord->ulKeysym;
        ulMethod = XKEYMETHOD_EXACT;
        break;
      }
    }

    if ( ulKeysym == 0 )
    {
      // Key was not found in key-table. Heuristic search.
      UCHAR      ucRecScan;
      USHORT     usRecFlags;

      if ( (usFlags & KC_CHAR) != 0 )
      {
        for( ulIdx = 0, pRecord = pMap->aList; ulIdx < pMap->ulCount;
             ulIdx++, pRecord++ )
        {
          ucRecScan = SHORT2FROMMP(pRecord->mp1) >> 8;
          usRecFlags = SHORT1FROMMP(pRecord->mp1);

          if ( ( ucRecScan == ucScan ) &&
               ( (usChar < 128) == (SHORT1FROMMP(pRecord->mp2) < 128) ) && // 7bit code.
               ( (usFlags & ~KC_SHIFT) == (usRecFlags & ~KC_SHIFT) ) )
          {
            ulKeysym = pRecord->ulKeysym;
            ulMethod = XKEYMETHOD_HEURISTIC;
            if ( (usFlags & KC_SHIFT) != (usRecFlags & KC_SHIFT) )
              // Different shift state for record and given event is a priority.
              // For cases when we have record with SHIFT and looks for key
              // when CapsLock is on.
              break;
          }
        }

        if ( ( ulKeysym == 0 ) && ( usChar > 127 ) )
        {
          for( ulIdx = 0, pRecord = pMap->aList; ulIdx < pMap->ulCount;
               ulIdx++, pRecord++ )
          {
            if ( usChar == SHORT1FROMMP(pRecord->mp2) )
            {
              ulKeysym = pRecord->ulKeysym;
              ulMethod = XKEYMETHOD_HEURISTIC;
            }
          }
        }
      } // if ( (usFlags & KC_CHAR) != 0 )

      if ( ulKeysym == 0 )
      {
        // Try to auto-detect key.

        ulKeysym = xkKeysymFromMPAuto( mp1, mp2 );
        if ( ulKeysym != 0 )
          ulMethod = XKEYMETHOD_AUTO;
        else
        {
          // Key still not found. Dirty way - search in table by scancode.

          for( ulIdx = 0, pRecord = pMap->aList; ulIdx < pMap->ulCount;
               ulIdx++, pRecord++ )
          {
            if ( ( SHORT2FROMMP(pRecord->mp1) >> 8 ) == ucScan )
            {
              ulKeysym = pRecord->ulKeysym;
              ulMethod = XKEYMETHOD_HEURISTIC;

              if ( SHORT1FROMMP(pRecord->mp2) == usChar )
                break;
            }
          }
        }
      } // if ( ulKeysym == 0 )
    } // if ( ulKeysym == 0 )

    if ( ulKeysym != 0 )
    {
      pXKFromMP->aOutput[pXKFromMP->cOutput].ulKeysym = ulKeysym;
      pXKFromMP->aOutput[pXKFromMP->cOutput].fPressed = TRUE;
      pXKFromMP->cOutput++;

      // Register pressed unregistered key.

      for( ulKPIdx = 0; ulKPIdx < ARRAYSIZE(pXKFromMP->aKeysPressed); ulKPIdx++ )
      {
        if ( ( pXKFromMP->aKeysPressed[ulKPIdx].ucScan == ucScan ) ||
             ( pXKFromMP->aKeysPressed[ulKPIdx].ucScan == 0 ) )
          break;
      }

      if ( ulKPIdx == ARRAYSIZE(pXKFromMP->aKeysPressed) )
      {
        pXKFromMP->aKeysPressed[0] = pXKFromMP->aKeysPressed[1];
        pXKFromMP->aKeysPressed[1] = pXKFromMP->aKeysPressed[2];
        pXKFromMP->aKeysPressed[2] = pXKFromMP->aKeysPressed[3];
        ulKPIdx = ARRAYSIZE(pXKFromMP->aKeysPressed) - 1;
      }

      pXKFromMP->aKeysPressed[ulKPIdx].ucScan = ucScan;
      pXKFromMP->aKeysPressed[ulKPIdx].ulKeysym = ulKeysym;
    }

    return ulMethod;
  }

  // Key is released.

  if ( ulKPIdx < ARRAYSIZE(pXKFromMP->aKeysPressed) )
  {
    pXKFromMP->aOutput[pXKFromMP->cOutput].ulKeysym =
      pXKFromMP->aKeysPressed[ulKPIdx].ulKeysym;
    pXKFromMP->aOutput[pXKFromMP->cOutput].fPressed = FALSE;
    pXKFromMP->cOutput++;

    // Remove the record.
    switch( ulKPIdx )
    {
      case 0: pXKFromMP->aKeysPressed[0] = pXKFromMP->aKeysPressed[1];
      case 1: pXKFromMP->aKeysPressed[1] = pXKFromMP->aKeysPressed[2];
      case 2: pXKFromMP->aKeysPressed[2] = pXKFromMP->aKeysPressed[3];
      case 3: { pXKFromMP->aKeysPressed[3].ucScan = 0; }
    }

    return XKEYMETHOD_WASPRESSED;
  }

  return XKEYMETHOD_NOTFOUND;
}

BOOL EXPENTRY xkKeysymFromMPCheck(PXKFROMMP pXKFromMP)
{
  ULONG      ulKPIdx;

  pXKFromMP->cOutput = 0;
  for( ulKPIdx = 0; ( ulKPIdx < ARRAYSIZE(pXKFromMP->aKeysPressed) ) &&
                    ( pXKFromMP->aKeysPressed[ulKPIdx].ucScan != 0 ); )
  {
    if ( ( WinGetPhysKeyState( HWND_DESKTOP,
             pXKFromMP->aKeysPressed[ulKPIdx].ucScan ) & 0x8000 ) == 0 )
    {
      pXKFromMP->aOutput[pXKFromMP->cOutput].ulKeysym =
        pXKFromMP->aKeysPressed[ulKPIdx].ulKeysym;
      pXKFromMP->aOutput[pXKFromMP->cOutput].fPressed = FALSE;
      pXKFromMP->cOutput++;

      switch( ulKPIdx )
      {
        case 0: pXKFromMP->aKeysPressed[0] = pXKFromMP->aKeysPressed[1];
        case 1: pXKFromMP->aKeysPressed[1] = pXKFromMP->aKeysPressed[2];
        case 2: pXKFromMP->aKeysPressed[2] = pXKFromMP->aKeysPressed[3];
        case 3: { pXKFromMP->aKeysPressed[3].ucScan = 0; }
      }
    }
    else
      ulKPIdx++;
  }

  return pXKFromMP->cOutput != 0;
}

VOID EXPENTRY xkMPFromKeysymStart(PXKFROMKEYSYM pXKFromKeysym)
{
  HFILE                hDriver;
  ULONG                ulRC, cbParam, cbData;
  SHIFTSTATE           stShiftState = { 0 };

  memset( pXKFromKeysym, 0, sizeof(XKFROMKEYSYM) );

  // Get "toggle" state for some keys.

  ulRC = DosOpen( "KBD$", &hDriver, &cbData, 0, 0, FILE_OPEN,
                  OPEN_SHARE_DENYREADWRITE | OPEN_ACCESS_READWRITE, NULL );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosOpen( \"KBD$\", ...), rc = %u", ulRC );
    return;
  }

  DosDevIOCtl( hDriver, IOCTL_KEYBOARD, KBD_GETSHIFTSTATE,
               NULL, 0, &cbParam,
               &stShiftState, sizeof(stShiftState), &cbData );
  DosClose( hDriver );

  if ( (stShiftState.fsState & KBDST_SCROLLLOCK_ON) != 0 )
    pXKFromKeysym->abState[0x46] |= 0x02;

  if ( (stShiftState.fsState & KBDST_NUMLOCK_ON) != 0 )
    pXKFromKeysym->abState[0x45] |= 0x02;

  if ( (stShiftState.fsState & KBDST_CAPSLOCK_ON) != 0 )
    pXKFromKeysym->abState[0x3A] |= 0x02;

  if ( (stShiftState.fsState & KBDST_INSERT_ON) != 0 )
  {
    pXKFromKeysym->abState[0x52] |= 0x02;
    pXKFromKeysym->abState[0x68] |= 0x02;
  }
}

BOOL EXPENTRY xkMPFromKeysym(PXKBDMAP pMap, ULONG ulKeysym, BOOL fPressed,
                             PXKFROMKEYSYM pXKFromKeysym)
{
  PXKEYREC   pRecord;
  ULONG      ulIdx;

  pXKFromKeysym->cOutput = 0;
  for( ulIdx = 0, pRecord = pMap->aList; ulIdx < pMap->ulCount;
       ulIdx++, pRecord++ )
  {
    if ( pRecord->ulKeysym == ulKeysym )
    {
      USHORT           usFlags = SHORT1FROMMP(pRecord->mp1) & XKEYKC_MASK;
      UCHAR            ucScan  = SHORT2FROMMP(pRecord->mp1) >> 8;
/*
      ULONG            ulXKFlag;

      // Store modificator flags.
#if 0
      switch( ulKeysym )
      {
        case XK_Shift_L:   ulXKFlag = XKFKSFL_SHIFT_L; break;
        case XK_Shift_R:   ulXKFlag = XKFKSFL_SHIFT_R; break;
        case XK_Control_L: ulXKFlag = XKFKSFL_CTRL_L;  break;
        case XK_Control_R: ulXKFlag = XKFKSFL_CTRL_R;  break;
        case XK_Alt_L:     ulXKFlag = XKFKSFL_ALT;     break;
        case XK_Alt_R:     ulXKFlag = XKFKSFL_ALTGR;   break;
#else
      switch( ucScan )
      {
        case 0x2A: ulXKFlag = XKFKSFL_SHIFT_L; break;
        case 0x36: ulXKFlag = XKFKSFL_SHIFT_R; break;
        case 0x1D: ulXKFlag = XKFKSFL_CTRL_L;  break;
        case 0x5B: ulXKFlag = XKFKSFL_CTRL_R;  break;
        case 0x38: ulXKFlag = XKFKSFL_ALT;     break;
        case 0x5E: ulXKFlag = XKFKSFL_ALTGR;   break;
#endif
        default:   ulXKFlag = 0;
      }

      if ( fPressed )
        pXKFromKeysym->ulFlags |= ulXKFlag;
      else
        pXKFromKeysym->ulFlags &= ~ulXKFlag;
*/
      // Make system event data (mp1, mp2).

      if ( !fPressed )
      {
        usFlags |= KC_KEYUP;
        usFlags &= ~KC_CHAR;         // Is it really necessary?
        pXKFromKeysym->abState[ucScan] &= ~0x01;
      }
      else
      {
        if ( (pXKFromKeysym->abState[ucScan] & 0x01) != 0 )
          usFlags |= KC_PREVDOWN;
        else
        {
          pXKFromKeysym->abState[ucScan] |= 0x01;

          if ( (pXKFromKeysym->abState[ucScan] & 0x02) != 0 )
            pXKFromKeysym->abState[ucScan] &= ~0x02;
          else
            pXKFromKeysym->abState[ucScan] |= 0x02;
        }
      }

      if ( (pXKFromKeysym->abState[ucScan] & 0x02) != 0 )
        usFlags |= KC_TOGGLE;

      // And add modificators to the system event data.

#if 1
      // Scancodes: 0x2A - Left Shift, 0x36 - Right Shift.
      if ( ((pXKFromKeysym->abState[0x2A] | pXKFromKeysym->abState[0x36]) &
           0x01) != 0 )
        usFlags |= KC_SHIFT;
      // Scancodes: 0x1D - Left Ctrl, 0x5B - Right Ctrl.
      if ( ((pXKFromKeysym->abState[0x1D] | pXKFromKeysym->abState[0x5B]) &
           0x01) != 0 )
        usFlags |= KC_CTRL;
      // Scancode: 0x38 - Alt (not for 0x5E - AltGr)
      if ( (pXKFromKeysym->abState[0x38] & 0x01) != 0 )
        usFlags |= KC_ALT;
#else
      if ( (pXKFromKeysym->ulFlags & XKFKSFL_SHIFT_MASK) != 0 )
        usFlags |= KC_SHIFT;
      if ( (pXKFromKeysym->ulFlags & XKFKSFL_CTRL_MASK) != 0 )
        usFlags |= KC_CTRL;
      if ( (pXKFromKeysym->ulFlags & XKFKSFL_ALT) != 0 )
        usFlags |= KC_ALT;
#endif

      pXKFromKeysym->aOutput[pXKFromKeysym->cOutput].mp1 =
        MPFROMSH2CH( usFlags, 1, ucScan );
      pXKFromKeysym->aOutput[pXKFromKeysym->cOutput].mp2 = pRecord->mp2;
      pXKFromKeysym->cOutput++;

      break;
    }
  }

  if ( pXKFromKeysym->cOutput == 0 )
  {
    USHORT   usFlags = fPressed ? KC_CHAR : (KC_CHAR | KC_KEYUP);
    USHORT   usChar = 0;

    if ( (ulKeysym & 0xFFFF0000) == 0x1000000 )
    {
      /* RFC 6143, 7.5.4.  KeyEvent
         Modern versions of the X Window System handle keysyms for Unicode
         characters, consisting of the Unicode character with the hex
         1000000 bit set.
      */
      CHAR   acUTF8[4];
      CHAR   acBuf[4];

      *((PULONG)acUTF8) = ulKeysym & 0x0000FFFF;

      if ( StrUTF8( 0, acBuf, sizeof(acBuf), acUTF8, acUTF8[1] == 0 ? 1 : 2 )
           > 0 )
        usChar = *((PUSHORT)acBuf);
    }
    else if ( ulKeysym <= 256 )
      usChar = ulKeysym;

    if ( usChar != 0 )
    {
      // Scancodes: 0x2A - Left Shift, 0x36 - Right Shift.
      if ( ((pXKFromKeysym->abState[0x2A] | pXKFromKeysym->abState[0x36]) &
           0x01) != 0 )
        usFlags |= KC_SHIFT;
      // Scancodes: 0x1D - Left Ctrl, 0x5B - Right Ctrl.
      if ( ((pXKFromKeysym->abState[0x1D] | pXKFromKeysym->abState[0x5B]) &
           0x01) != 0 )
        usFlags |= KC_CTRL;
      // Scancode: 0x38 - Alt (not for 0x5E - AltGr)
      if ( (pXKFromKeysym->abState[0x38] & 0x01) != 0 )
        usFlags |= KC_ALT;
/*
      if ( (pXKFromKeysym->ulFlags & XKFKSFL_SHIFT_MASK) != 0 )
        usFlags |= KC_SHIFT;
      if ( (pXKFromKeysym->ulFlags & XKFKSFL_CTRL_MASK) != 0 )
        usFlags |= KC_CTRL;
      if ( (pXKFromKeysym->ulFlags & XKFKSFL_ALT) != 0 )
        usFlags |= KC_ALT;
*/
      pXKFromKeysym->aOutput[pXKFromKeysym->cOutput].mp1 =
        MPFROMSH2CH( usFlags, 1, 0 );
      pXKFromKeysym->aOutput[pXKFromKeysym->cOutput].mp2 =
        MPFROM2SHORT( usChar, 0 );
      pXKFromKeysym->cOutput++;
    }
  }

  return pXKFromKeysym->cOutput != 0;
}

// VOID EXPENTRY xkMakeMPForVIO(MPARAM mp1, PMPARAM pmp2)
//
// Converts WM_CHAR second paramether to WM_VIOCHAR paramether.

static BOOL _isASCIIControlChar(UCHAR ucChar)
{
  switch( ucChar )
  {
    /* Skip the ones with virtual keys. */
    case 0x08 + 0x40: case 0x08 + 0x60: // backspace
    case 0x09 + 0x40: case 0x09 + 0x60: // horizontal tab
    case 0x1b + 0x40: case 0x1b + 0x60: // escape
    case 0x0d + 0x40: case 0x0d + 0x60: // return
      return FALSE;
  }

  return (ucChar >= 'A' && ucChar <= '_') || (ucChar >= 'a' && ucChar <= 'z');
}

VOID EXPENTRY xkMakeMPForVIO(MPARAM mp1, PMPARAM pmp2)
{
  USHORT     usFlags  = SHORT1FROMMP(mp1);
  UCHAR      ucScan   = CHAR4FROMMP(mp1);
  UCHAR      ucChar   = SHORT1FROMMP(*pmp2);
  USHORT     usVK     = SHORT2FROMMP(*pmp2);
  USHORT     usKDD    = (usFlags & KC_KEYUP) != 0 ? KDD_BREAK : 0;

  if ( ( (usFlags & KC_CTRL) != 0 ) &&
     //(ucChar >= 0x40) && (ucChar < 0x7F) )
       _isASCIIControlChar( ucChar ) )
    ucChar -= ucChar >= 0x60 ? 0x60 : 0x40; // Upcase, ctrl character

  if ( (usFlags & KC_VIRTUALKEY) != 0 )
  {
    if ( (usFlags & KC_SHIFT) != 0 )
      ucScan = aVKScan2[usVK].ucShift;
    else if ( (usFlags & KC_ALT) != 0 )
      ucScan = aVKScan2[usVK].ucAlt;
    else if ( (usFlags & KC_CTRL) != 0 )
      ucScan = aVKScan2[usVK].ucCtrl;
    else if ( ( ( ucScan < 0x47 ) || ( ucScan > 0x53 ) ||
              ( ( usFlags & KC_CHAR) == 0 ) ) )
      ucScan = aVKScan2[usVK].ucNoMod;

    if ( ucChar == 0xE0 )
      usKDD |= ( KDD_SECONDARY | KDD_EXTENDEDKEY );
    else
      switch( usVK )
      {
        case VK_SHIFT:
        case VK_ALT:
        case VK_CTRL:
          usKDD |= KDD_SHIFTKEY;
      }
  }

  // WM_VIOCHAR:
  //      mp1.s1: KC_ flags.
  //      mp1.c3: repeat
  //      mp1.c4: scancode
  //      mp2.c1: translated char
  //      mp2.c2: translated scancode
  //      mp2.s2: KDD_ flags.

//  *pmp2 = MPFROM2SHORT( ( ucScan << 8 ) | ucChar, usV );
  *pmp2 = MPFROM2SHORT(MAKESHORT(ucChar, ucScan), usKDD);
}

#ifdef DEBUG_CODE
static VOID _loadMapDebugErr(PSZ pszFile, ULONG ulLine, ULONG ulCode)
{
  PSZ        pszCode;
  CHAR       acCode[16];

  switch( ulCode )
  {
    case XKEYERR_KEYSYM:  pszCode = "XKEYERR_KEYSYM"; break;
    case XKEYERR_CHAR:    pszCode = "XKEYERR_CHAR"; break;
    case XKEYERR_FLAG:    pszCode = "XKEYERR_FLAG"; break;
    case XKEYERR_SCAN:    pszCode = "XKEYERR_SCAN"; break;
    case XKEYERR_VK:      pszCode = "XKEYERR_VK"; break;
    default:
      sprintf( acCode, "#%u", ulCode );
      pszCode = acCode;
  }

  printf( "Error in %s at line %u: %s\n", pszFile, ulLine, pszCode );
}
#endif

PXKBDMAP EXPENTRY xkMapNew()
{
  PXKBDMAP   pMap;

  pMap = calloc( 1, sizeof(XKBDMAP) +
                    ( (_KEYMAP_INIT_RECORDS - 1) * sizeof(XKEYREC) ) );
  if ( pMap == NULL )
    return NULL;

  pMap->ulAllocated = _KEYMAP_INIT_RECORDS;
  return pMap;
}

VOID EXPENTRY xkMapFree(PXKBDMAP pMap)
{
  free( pMap );
}

BOOL EXPENTRY xkMapInsert(PXKBDMAP *ppMap, ULONG ulKeysym, MPARAM mp1,
                          MPARAM mp2)
{
  PXKBDMAP   pMap = *ppMap;
  PXKEYREC   pRecord;
  USHORT     usFlags = SHORT1FROMMP(mp1);
  UCHAR      ucScan  = SHORT2FROMMP(mp1) >> 8;

  if ( (usFlags & KC_KEYUP) != 0 )
    return TRUE;

  mp1 = MPFROMSH2CH( usFlags & XKEYKC_MASK, 0, ucScan );

  if ( pMap->ulCount == pMap->ulAllocated )
  {
    // Expand list.
    pMap = realloc( pMap, sizeof(XKBDMAP) +
                          ( (pMap->ulAllocated + _KEYMAP_DELTA_RECORDS - 1) *
                            sizeof(XKEYREC) ) );
    if ( pMap == NULL )
      return FALSE;

    *ppMap = pMap;
    pMap->ulAllocated += _KEYMAP_DELTA_RECORDS;
  }

  pRecord = &pMap->aList[pMap->ulCount];
  pMap->ulCount++;
  pRecord->ulKeysym = ulKeysym;
  pRecord->mp1 = mp1;
  pRecord->mp2 = mp2;
  return TRUE;
}

static PXKBDMAP _xkMapLoad(PXKBDMAP pMap, PSZ pszFile,
                           PFNLOADMAPERROR pfnError)
{
  FILE       *fdInput = fopen( pszFile, "rt" );
  PCHAR      pcBegin, pcEnd;
  ULONG      ulLine = 0;
  ULONG      ulKeysym;
  USHORT     usFlags, usVK;
  ULONG      ulChar;
  ULONG      ulScan;
  CHAR       acBuf[1024];

  if ( fdInput == NULL )
    return NULL;

#ifdef DEBUG_CODE
  if ( pfnError == NULL )
    pfnError = _loadMapDebugErr;
#endif

  if ( pMap == NULL )
  {
    pMap = xkMapNew();
    if ( pMap == NULL )
      return NULL;
  }

  while( fgets( acBuf, sizeof(acBuf), fdInput ) != NULL ) 
  {
    ulLine++;
    pcBegin = acBuf;
    STR_SKIP_SPACES( pcBegin );
    if ( *pcBegin == '#' || *pcBegin == ';' || *pcBegin == '\0' )
      // Comment or empty line.
      continue;

    pcEnd = strchr( pcBegin, ':' );
    if ( pcEnd == NULL )
    {
      if ( pfnError != NULL )
        pfnError( pszFile, ulLine, XKEYERR_KEYSYM );
      continue;
    }

    // keysym.

    if ( !utilStrToULong( pcEnd - pcBegin, pcBegin, 0, ~0, &ulKeysym ) )
    {
      if ( pfnError != NULL )
        pfnError( pszFile, ulLine, XKEYERR_KEYSYM );
      continue;
    }

    pcEnd++; // skip ':'

    // Flags.

    usFlags = 0;
    while( TRUE )
    {
      STR_SKIP_SPACES( pcEnd );
      pcBegin = pcEnd;
      while( ( *pcEnd != '\0' ) && ( *pcEnd != ',' ) && !isspace( *pcEnd ) )
        pcEnd++;

      if ( pcEnd == pcBegin )
        break;

      switch( utilStrWordIndex( "KC_CHAR KC_SCANCODE KC_VIRTUALKEY KC_DEADKEY "
                                "KC_COMPOSITE KC_INVALIDCOMP KC_SHIFT KC_ALT "
                                "KC_CTRL KC_LONEKEY", pcEnd - pcBegin, pcBegin ) )
      {
        case -1:
          pfnError( pszFile, ulLine, XKEYERR_FLAG );
          break;

        case 0:
          usFlags |= KC_CHAR;
          break;

        case 1:
          usFlags |= KC_SCANCODE;
          break;

        case 2:
          usFlags |= KC_VIRTUALKEY;
          break;

        case 3:
          usFlags |= KC_DEADKEY;
          break;

        case 4:
          usFlags |= KC_COMPOSITE;
          break;

        case 5:
          usFlags |= KC_INVALIDCOMP;
          break;

        case 6:
          usFlags |= KC_SHIFT;
          break;

        case 7:
          usFlags |= KC_ALT;
          break;

        case 8:
          usFlags |= KC_CTRL;
          break;

        case 9:
          usFlags |= KC_LONEKEY;
          break;
      }
    }

    // Scan code.

    STR_SKIP_SPACES( pcEnd );
    ulScan = 0;
    if ( *pcEnd == ',' )
    {
      pcEnd++; // skip ','
      STR_SKIP_SPACES( pcEnd );
      if ( *pcEnd != ',' && *pcEnd != '\0' )
      {
        pcBegin = pcEnd;
        while( ( *pcEnd != '\0' ) && ( *pcEnd != ',' ) && !isspace( *pcEnd ) )
          pcEnd++;

        if ( !utilStrToULong( pcEnd - pcBegin, pcBegin, 0, 0xFF, &ulScan ) )
        {
          if ( pfnError != NULL )
            pfnError( pszFile, ulLine, XKEYERR_SCAN );
          continue;
        }
      }
    }

    // Character code.

    STR_SKIP_SPACES( pcEnd );
    ulChar = 0;
    if ( *pcEnd == ',' )
    {
      pcEnd++; // skip ','
      STR_SKIP_SPACES( pcEnd );
      if ( *pcEnd != ',' && *pcEnd != '\0' )
      {
        pcBegin = pcEnd;
        while( ( *pcEnd != '\0' ) && ( *pcEnd != ',' ) && !isspace( *pcEnd ) )
          pcEnd++;

        if ( !utilStrToULong( pcEnd - pcBegin, pcBegin, 0, 0xFFFF, &ulChar ) )
        {
          if ( pfnError != NULL )
            pfnError( pszFile, ulLine, XKEYERR_CHAR );
          continue;
        }
      }
    }

    // Virtual key.

    STR_SKIP_SPACES( pcEnd );
    usVK = 0;
    if ( *pcEnd == ',' )
    {
      ULONG  ulIdx;

      pcEnd++;
      STR_SKIP_SPACES( pcEnd );
      if ( *pcEnd != '\0' )
      {
        pcBegin = pcEnd;
        STR_MOVE_TO_SPACE( pcEnd );
        *pcEnd = '\0';

        for( ulIdx = 1; ulIdx < ARRAYSIZE(apszVK); ulIdx++ )
        {
          if ( stricmp( apszVK[ulIdx], pcBegin ) == 0 )
          {
            usVK = ulIdx;
            break;
          }
        }

        if ( usVK == 0 )
        {
          if ( pfnError != NULL )
            pfnError( pszFile, ulLine, XKEYERR_VK );
          continue;
        }
      }
    }

    // Store a new record.
    if ( !xkMapInsert( &pMap, ulKeysym, MPFROMSH2CH( usFlags, 0, ulScan ),
                       MPFROM2SHORT( ulChar, usVK ) ) )
    {
      free( pMap );
      pMap = NULL;
      break;
    }
  }

  fclose( fdInput ); 

  return pMap;
}

static ULONG _cbFileNameSubst(CHAR chKey, ULONG cbBuf, PCHAR pcBuf,
                              PVOID pData)
{
  if ( chKey == 'L' )
  {
    // Key %L - local codepage number.

    COUNTRYCODE          stCountryCode = { 0 };
    COUNTRYINFO          stCountryInfo; 
    ULONG                ulRC;
    LONG                 cbActual;

    ulRC = DosQueryCtryInfo( sizeof(COUNTRYINFO), &stCountryCode,
                             &stCountryInfo, (PULONG)&cbActual );
    if ( ulRC != NO_ERROR )
      return 0;

    cbActual = _snprintf( pcBuf, cbBuf, "%u", stCountryInfo.codepage );
    return cbActual < 0 ? 0 : cbActual;
  }

  return 0;
}

PXKBDMAP EXPENTRY xkMapLoad(PSZ pszFile, PFNLOADMAPERROR pfnError)
{
  PCHAR      pcEnd, pcPos;
  PXKBDMAP   pMapNew, pMap = NULL;
  CHAR       acFile[CCHMAXPATH];
  ULONG      cbFile;

  while( TRUE )
  {
    STR_SKIP_SPACES( pszFile );
    if ( *pszFile == '\0' )
      break;

    pcPos = strchr( pszFile, '&' );
    if ( pcPos == NULL )
      pcPos = strchr( pszFile, '\0' );
    pcEnd = pcPos;
    while( ( pcEnd > pszFile ) && isspace( *(pcEnd-1) ) )
      pcEnd--;

    cbFile = pcEnd - pszFile;
    if ( cbFile != 0 )
    {
      utilStrFormat( sizeof(acFile), acFile, cbFile, pszFile, _cbFileNameSubst,
                     NULL );

      pMapNew = _xkMapLoad( pMap, acFile, pfnError );
      if ( pMapNew != NULL )
        pMap = pMapNew;
    }

    if ( *pcPos == '\0' )
      break;
    pszFile = pcPos + 1;
  }

  return pMap;
}


VOID EXPENTRY xkEventToStr(MPARAM mp1, MPARAM mp2, PXKEVENTSTR pEvent)
{
  PCHAR      pcBuf     = pEvent->acFlags;
  USHORT     usFlags   = SHORT1FROMMP(mp1);
  USHORT     usVK      = SHORT2FROMMP(mp2);

  if ( (usFlags & KC_CHAR) != 0 )
    pcBuf += sprintf( pcBuf, "KC_CHAR " );
  if ( (usFlags & KC_VIRTUALKEY) != 0 )
    pcBuf += sprintf( pcBuf, "KC_VIRTUALKEY " );
  if ( (usFlags & KC_SCANCODE) != 0 )
    pcBuf += sprintf( pcBuf, "KC_SCANCODE " );
  if ( (usFlags & KC_SHIFT) != 0 )
    pcBuf += sprintf( pcBuf, "KC_SHIFT " );
  if ( (usFlags & KC_CTRL) != 0 )
    pcBuf += sprintf( pcBuf, "KC_CTRL " );
  if ( (usFlags & KC_ALT) != 0 )
    pcBuf += sprintf( pcBuf, "KC_ALT " );
  if ( (usFlags & KC_KEYUP) != 0 )
    pcBuf += sprintf( pcBuf, "KC_KEYUP " );
  if ( (usFlags & KC_PREVDOWN) != 0 )
    pcBuf += sprintf( pcBuf, "KC_PREVDOWN " );
  if ( (usFlags & KC_LONEKEY) != 0 )
    pcBuf += sprintf( pcBuf, "KC_LONEKEY " );
  if ( (usFlags & KC_DEADKEY) != 0 )
    pcBuf += sprintf( pcBuf, "KC_DEADKEY " );
  if ( (usFlags & KC_COMPOSITE) != 0 )
    pcBuf += sprintf( pcBuf, "KC_COMPOSITE " );
  if ( (usFlags & KC_INVALIDCOMP) != 0 )
    pcBuf += sprintf( pcBuf, "KC_INVALIDCOMP " );
  if ( (usFlags & KC_TOGGLE) != 0 )
    pcBuf += sprintf( pcBuf, "KC_TOGGLE " );
  if ( (usFlags & KC_INVALIDCHAR) != 0 )
    pcBuf += sprintf( pcBuf, "KC_INVALIDCHAR " );
  // Max. string length for flags: 153 + 1*ZERO characters at pEvent->acFlags.

  if ( ( pcBuf > pEvent->acFlags ) && ( *(pcBuf - 1) == ' ' ) )
    *(pcBuf - 1) = '\0';

  sprintf( pEvent->acChar, "0x%X", SHORT1FROMMP(mp2) );
  sprintf( pEvent->acScan, "0x%X", SHORT2FROMMP(mp1) >> 8 );
  strcpy( pEvent->acVK, apszVK[usVK <= ARRAYSIZE(apszVK) ? usVK : 0] );
}

BOOL EXPENTRY xkMapSave(PXKBDMAP pMap, PSZ pszFile)
{
  PXKEYREC   pRecord;
  XKEVENTSTR stEvent;
  FILE       *fdOutput;
  ULONG      ulIdx;

  fdOutput = fopen( pszFile, "w+t" );
  if ( fdOutput == NULL )
    return FALSE;

  for( ulIdx = 0, pRecord = pMap->aList; ulIdx < pMap->ulCount;
       ulIdx++, pRecord++ )
  {
    xkEventToStr( pRecord->mp1, pRecord->mp2, &stEvent );

    fprintf( fdOutput, "0x%X: %s, %s, %s, %s\n",
             pRecord->ulKeysym, stEvent.acFlags, stEvent.acScan,
             stEvent.acChar, stEvent.acVK );
  }

  fclose( fdOutput );
  return TRUE;
}
