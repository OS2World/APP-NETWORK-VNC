#ifndef OPTIONSDLG_H
#define OPTIONSDLG_H

#include "linkseq.h"

// WM_OPTDLG_ENTER - inform the owner that the options dialog is closed by
// system command or "Connet" button is pressed.
// mp1 - POPTDLGDATA, New options,
// mp2 - ULONG, Not zero - use as default options.
#define WM_OPTDLG_ENTER          (WM_USER + 250)

#define OPT_ENC_COUNT            12

typedef struct _OPTDLGDATA {
  ULONG      ulAttempts;
  ULONG      ulBPP;              // 8/16/32
  BOOL       fRememberPswd;
  BOOL       fViewOnly;
  BOOL       fShareDesktop;
  CHAR       acCharset[CHARSET_NAME_MAX_LEN];
  CHAR       acEncodings[ENC_LIST_MAX_LEN];
  ULONG      ulCompressLevel;    // 0..9 for tight, zlib, zlibhex
  ULONG      ulQualityLevel;     // 0..9
} OPTDLGDATA, *POPTDLGDATA;

HWND optdlgOpen(HWND hwndOwner, PSZ pszHost, POPTDLGDATA pData);

#endif // OPTIONSDLG_H
