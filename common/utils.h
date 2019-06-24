#ifndef UTIL_H
#define UTIL_H

#define BUF_SKIP_SPACES(cb, pc) \
  while( (cb > 0) && isspace( *pc ) ) { cb--; pc++; }
#define BUF_MOVE_TO_SPACE(cb, pc) \
  while( (cb > 0) && !isspace( *pc ) ) { cb--; pc++; }
#define BUF_SKIP_DELIM(cb,pc,d) \
  while( (cb > 0) && ( (*pc == d ) || isspace(*pc) ) ) { cb--; pc++; }
#define BUF_RTRIM(cb, pc) \
  while( (cb > 0) && ( isspace( pc[cb - 1] ) ) ) cb--
#define BUF_ENDS_WITH(cb, pc, cbend, pcend) \
  ( ( (cb) >= (cbend) ) && ( memcmp( (pc)-(cbend), pcend, cbend ) == 0 ) )
#define BUF_I_ENDS_WITH(cb, pc, cbend, pcend) \
  ( ( (cb) >= (cbend) ) && ( memicmp( (&((pc)[cb]))-(cbend), pcend, cbend ) == 0 ) )

#define STR_SAFE(p) ( (p) == NULL ? "" : (p) )
#define STR_LEN(p) ( (p) == NULL ? 0 : strlen( p ) )
#define STR_ICMP(s1,s2) stricmp( STR_SAFE(s1), STR_SAFE(s2) )
#define STR_COPY(d,s) strcpy( d, STR_SAFE(s) )

#define STR_SKIP_SPACES(p) do { while( isspace( *p ) ) p++; } while( 0 )
#define STR_RTRIM(p) do { PSZ __p = strchr( p, '\0' ); \
  while( (__p > p) && isspace( *(__p - 1) ) ) __p--; \
  *__p = '\0'; \
} while( 0 )
#define STR_MOVE_TO_SPACE(p) \
  do { while( (*p != '\0') && !isspace( *p ) ) p++; } while( 0 )
#define STR_SKIP_DELIM(p,d) while( ( *p == d ) || isspace(*p) ) p++
// BUF_STR_IEQ() and BUF_STR_IEQ() returns TRUE or FALSE
#define BUF_STR_EQ(cb, pc, s) ((cb == strlen(s)) && (memcmp(pc,s,cb) == 0))
#define BUF_STR_IEQ(cb, pc, s) ((cb == strlen(s)) && (memicmp(pc,s,cb) == 0))

#define ARRAYSIZE(a) ( sizeof(a) / sizeof(a[0]) )

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define IS_HIGH_PTR(p)   ((unsigned long int)(p) >= (512*1024*1024))

#ifdef __WATCOMC__
#define	LONG_MAX       0x7fffffffL         /* max value for a long */
#define	LONG_MIN       (-0x7fffffffL - 1)  /* min value for a long */
#define	ULONG_MAX      (~0)                /* max value for a unsigned long */
#define	ULONG_MIN      0                   /* min value for a unsigned long */
#endif

typedef unsigned long long       ULLONG, *PULLONG;
typedef struct _ULL2UL {
  ULONG      ulLow;
  ULONG      ulHigh;
} ULL2UL, *PULL2UL;

// utilStrFindParts(,,,,PUTILSTRPART)
typedef struct _UTILSTRPART {
  ULONG      cbPart;
  PCHAR      pcPart;
} UTILSTRPART, *PUTILSTRPART;

// utilIPList*()
/*
typedef struct _UTILIPLISTREC {
  ULONG                ulFirstAddr;        // IP-address.
  ULONG                ulLastAddr;         // IP-address.
  ULONG                ulUser;             // User value.
} UTILIPLISTREC, *PUTILIPLISTREC;

typedef struct _UTILIPLIST {
  ULONG                ulCount;
  PUTILIPLISTREC       paList;
} UTILIPLIST, *PUTILIPLIST;
*/

ULONG utilStrWordsCount(ULONG cbText, PCHAR pcText);
BOOL utilStrCutWord(PULONG pcbText, PCHAR *ppcText,
                    PULONG pcbWord, PCHAR *ppcWord);
LONG utilStrWordIndex(PSZ pszList, LONG cbWord, PCHAR pcWord);
VOID utilStrTrim(PSZ pszText);
BOOL utilStrAddWords(PULONG pcbText, PCHAR *ppcText,
                     ULONG cbWords, PCHAR pcWords,
                     ULONG (*fnFilter)(ULONG cbWord, PCHAR pcWord) );
BOOL utilStrAppend(PULONG pcbText, PCHAR *ppcText, ULONG cbStr, PCHAR pcStr,
                   BOOL fFullStr);
PCHAR utilStrFindKey(ULONG cbText, PCHAR pcText, ULONG cbKey, PCHAR pcKey,
                     PULONG pcbVal);
PSZ utilStrNewUnescapeQuotes(ULONG cbText, PCHAR pcText, BOOL fIfQuoted);
PCHAR utilStrFindOption(ULONG cbText, PCHAR pcText,
                        ULONG cbName, PCHAR pcName, PULONG pcbVal);
PSZ utilStrNewGetOption(ULONG cbText, PCHAR pcText, PSZ pszName);
BOOL utilStrToULong(LONG cbStr, PCHAR pcStr, ULONG ulMin, ULONG ulMax,
                    PULONG pulValue);
BOOL utilStrToLong(LONG cbStr, PCHAR pcStr, LONG lMin, LONG lMax,
                   PLONG plValue);
BOOL utilStrToBool(LONG cbStr, PCHAR pcStr, PBOOL pfValue);
LONG utilStrFromBytes(ULLONG ullVal, ULONG cbBuf, PCHAR pcBuf);
BOOL utilStrSplitWords(ULONG cbStr, PCHAR pcStr, PULONG pulWords,
                      PUTILSTRPART pWords);
BOOL utilStrFindParts(ULONG cbStr, PCHAR pcStr, PSZ pszDelimiter,
                      PULONG pulParts, PUTILSTRPART pParts);
BOOL utilStrBuildParts(ULONG cbStr, PCHAR pcStr, PSZ pszDelimiter,
                       ULONG ulParts, BOOL fRev, CHAR cNewDelim,
                       PULONG pcbBuf, PCHAR pcBuf);
ULONG utilStrFindURIHosts(ULONG cbText, PCHAR pcText,
                          BOOL (*fnFound)(ULONG cbAddr, PCHAR pcAddr, PVOID pData),
                          PVOID pData);
PSZ utilStrNewSZ(ULONG cbStr, PCHAR pcStr);
PCHAR utilStrLastChar(ULONG cbText, PCHAR pcText, CHAR chSearch);
ULONG utilLoadInsertStr(HMODULE hMod,              // module handle
                        BOOL fStrMsg,              // string (1) / message (0)
                        ULONG ulId,                // string/message id
                        ULONG cVal, PSZ *ppszVal,  // pointers to values
                        ULONG cbBuf, PCHAR pcBuf); // result buffer


#ifdef UTIL_INET_ADDR
BOOL utilStrToInAddr(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr);
BOOL utilStrToMask(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr);
BOOL utilStrToInAddrRange(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr1,
                          struct in_addr *pInAddr2);
BOOL utilInAddrRangeToStr(struct in_addr *pInAddr1, struct in_addr *pInAddr2,
                          ULONG cbBuf, PCHAR pcBuf);
BOOL utilStrToInAddrPort(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr,
                         PUSHORT pusPort, BOOL fAnyIP, USHORT usDefaultPort);
BOOL utilCIDRLenToInAddr(ULONG ulCIDRLen, struct in_addr *pInAddr);
#endif

BOOL utilStrTimeToSec(ULONG cbStr, PCHAR pcStr, PULONG pulSec);
LONG utilSecToStrTime(ULONG ulSec, ULONG cbStr, PCHAR pcStr);
BOOL utilStrToBytes(ULONG cbStr, PCHAR pcStr, PULONG pulSec);
LONG utilStrFormat(ULONG cbBuf, PCHAR pcBuf, LONG cbFormat, PCHAR pcFormat,
                   ULONG (*fnValue)(CHAR chKey, ULONG cbBuf, PCHAR pcBuf,
                                    PVOID pData),
                   PVOID pData);

/*
#define utilIPListFree(pIPList) \
   if ( (pIPList)->paList != NULL ) debugFree( (pIPList)->paList )
BOOL utilIPListAddStr(PUTILIPLIST pIPList, ULONG cbList,
                      PCHAR pcList, ULONG ulUser);
BOOL utilIPListCheck(PUTILIPLIST pIPList, struct in_addr stInAddr,
                     PULONG pulUser);
*/

// Creates all subdirectories in path.
BOOL utilMakePathToFile(ULONG cbFName, PCHAR pcFName);
BOOL utilPathExists(ULONG cbName, PCHAR pcName, BOOL fFile);
LONG utilSetExtension(ULONG cbBuf, PCHAR pcBuf, PSZ pszFile, PSZ pszExt);
ULONG utilMessageBox(HWND hwnd, PSZ pszTitle, ULONG ulMsgResId, ULONG ulStyle);
ULONG utilQueryProgPath(ULONG cbBuf, PCHAR pcBuf);
VOID utilPathOS2Slashes(ULONG cbBuf, PCHAR pcBuf);

BOOL utilVerifyDomainName(ULONG cbDomain, PCHAR pcDomain);
BOOL utilVerifyHostPort(ULONG cbStr, PCHAR pcStr);
PCHAR utilEMailDomain(ULONG cbAddr, PCHAR pcAddr, PULONG pcbDomain);
BOOL utilIsMatch(ULONG cbStr, PCHAR pcStr, ULONG cbPtrn, PCHAR pcPtrn);
BOOL utilBSearch(const void *pKey, PVOID pBase, ULONG ulNum, ULONG cbWidth,
                 int (*fnComp)(const void *pkey, const void *pbase),
                 PULONG pulIndex);

BOOL utilINIWriteULong(HINI hIni, PSZ pszApp, PSZ pszKey, ULONG ulData);
ULONG utilINIQueryULong(HINI hIni, PSZ pszApp, PSZ pszKey, ULONG ulDefault);
BOOL utilINIWriteLong(HINI hIni, PSZ pszApp, PSZ pszKey, LONG lData);
LONG utilINIQueryLong(HINI hIni, PSZ pszApp, PSZ pszKey, LONG lDefault);
VOID utilINIWriteWinPresParam(HWND hwnd, HINI hIni, PSZ pszApp);
VOID utilINIQueryWinPresParam(HWND hwnd, HINI hIni, PSZ pszApp);
BOOL utilINIWritePassword(HINI hIni, PSZ pszApp, PSZ pszKey, PSZ pszPassword);
ULONG utilINIQueryPassword(HINI hIni, PSZ pszApp, PSZ pszKey,
                           ULONG cbBuf, PCHAR pcBuf);

VOID utilB64Enc(PCHAR pcData, ULONG cbData, PCHAR *ppcBuf, PULONG pcbBuf);
VOID utilB64Dec(PCHAR pcData, ULONG cbData, PCHAR *ppcBuf, PULONG pcbBuf);

#endif // UTIL_H
