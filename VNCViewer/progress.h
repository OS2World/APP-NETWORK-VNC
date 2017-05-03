#ifndef PROGRESS_H
#define PROGRESS_H

#include "clntconn.h"

BOOL prStart(PCCPROPERTIES pProperties, BOOL fRememberPswd,
             ULONG ulMaxAttempts, PSZ pszWinTitle);

#endif // PROGRESS_H
