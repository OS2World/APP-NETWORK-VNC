#ifndef PROGRESS_H
#define PROGRESS_H

#include "clntconn.h"

typedef struct _HOSTDATA {
  CCPROPERTIES       stProperties;
  ULONG              ulAttempts;
  BOOL               fRememberPswd;

  CHAR               acWinTitle[128];
  HWND               hwndNotify;
} HOSTDATA, *PHOSTDATA;

BOOL prStart(PHOSTDATA pHostData);

#endif // PROGRESS_H
