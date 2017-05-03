#ifndef LOG_H
#define LOG_H

#include <stdarg.h> 

typedef struct _LOGDATA {
  ULONG      ulLevel;
  ULONG      ulMaxSize;
  ULONG      ulFiles;
  CHAR       acFile[CCHMAXPATH];
} LOGDATA, *PLOGDATA;

VOID logClose();
BOOL logSet(HAB hab, PLOGDATA pLogData);
VOID logStr(ULONG ulLevel, PSZ pszStr);
VOID logVArg(ULONG ulLevel, PSZ pszFormat, va_list args);
VOID logFmt(ULONG ulLevel, PSZ pszFormat, ...);

#endif // LOG_H
