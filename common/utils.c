#include <string.h>
#include <sys/stat.h>
#include <sys/types.h> 
#include <direct.h>
#include <ctype.h>
#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_WIN
#define INCL_GPIBITMAPS
#include <os2.h>

#ifdef __WATCOMC__
#include <types.h>
#endif
#include <sys\socket.h>
#include <sys\ioctl.h>
#include <arpa\inet.h>
#ifdef __WATCOMC__
#include <net\route.h>
#include <net\if.h>
#endif

#ifdef UTILS_WITH_OPENSSL
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#endif

#define UTIL_INET_ADDR
#include "utils.h"

#include <stdio.h>
#include <debug.h>

ULONG utilStrWordsCount(ULONG cbText, PCHAR pcText)
{
  ULONG      ulCount = 0;

  while( TRUE )
  {
    BUF_SKIP_SPACES( cbText, pcText );
    if ( cbText == 0 )
      break;
    BUF_MOVE_TO_SPACE( cbText, pcText );
    ulCount++;
  }

  return ulCount;
}

// utilStrCutWord(PULONG pcbText, PCHAR *ppcText,
//                PULONG pcbWord, PCHAR *ppcWord)
//
// Locates first word from the string *ppcText (length *pcbText) and places
// pointer to the finded word to ppcWord and length of this word in pcbWord.
// On return *pcbText contains pointer to the first character after founded
// word and *ppcText left length of the text.
// *pcbWord == 0 when no more words in the input text.

BOOL utilStrCutWord(PULONG pcbText, PCHAR *ppcText,
                    PULONG pcbWord, PCHAR *ppcWord)
{
  ULONG            cbText = *pcbText;
  PCHAR            pcText = *ppcText;
  PCHAR            pcWord;

  BUF_SKIP_SPACES( cbText, pcText );
  pcWord = pcText;
  BUF_MOVE_TO_SPACE( cbText, pcText );

  *pcbText = cbText;
  *ppcText = pcText;
  *pcbWord = pcText - pcWord;
  *ppcWord = pcWord;

  return pcText != pcWord;
}

// LONG utilStrWordIndex(PSZ pszList, PCHAR pcWord, ULONG cbWord)
//
// Returns index of word pointed by pcWord (and length equals cbWord) in the
// list pszList. The list must contain the words separated by a space.
// When cbWord < 0 the paramether pcWord is treated as a string.
// The function returns -1 if the word is not found.

LONG utilStrWordIndex(PSZ pszList, LONG cbWord, PCHAR pcWord)
{
  ULONG		   ulIdx;
  ULONG      cbList;
  ULONG      cbScanWord;
  PCHAR      pcScanWord;

  if ( pcWord == NULL || cbWord == 0 )
    return -1;

  if ( cbWord < 0 )
    cbWord = strlen( pcWord );

  BUF_SKIP_SPACES( cbWord, pcWord );
  BUF_RTRIM( cbWord, pcWord );
  if ( cbWord == 0 )
    return -1;

  cbList = strlen( pszList );
  for( ulIdx = 0; ; ulIdx++ )
  {
    utilStrCutWord( &cbList, &pszList, &cbScanWord, &pcScanWord );
    if ( cbScanWord == 0 )
      // All words in the list was checked.
      break;

    if ( ( cbScanWord == cbWord ) &&
         ( memicmp( pcScanWord, pcWord, cbScanWord ) == 0 ) )
      // Word found - return list index of word.
      return ulIdx;
  }

  return -1;
}

VOID utilStrTrim(PSZ pszText)
{
  PSZ        pszTrimText;

  pszTrimText = pszText;
  STR_SKIP_SPACES( pszTrimText );
  STR_RTRIM( pszTrimText );

  strcpy( pszText, pszTrimText );
}

// BOOL utilStrAddWords(PULONG pcbText, PCHAR *ppcText,
//                      ULONG cbWords, PCHAR pcWords,
//                      ULONG (*fnFilter)(ULONG cbWord, PCHAR pcWord))
//
// Adds words cbWords/pcWords to the end of the text *pcbText/*ppcText.
// For the new text *pcbText/*ppcText can be 0/NULL. Otherwise it should be
// _allocated_ memory.
// Output: pcbText/ppcText - new length and new pointer to the realocated
// memory for text.
// The memory block pointed by *ppcText must be deallocated by debugFree().
//
// If fnFilter is not a NULL it must return 0 - error, utilStrAddWords() will
// immediately return FALSE, 1 - add word, other - do not add given word.
//
// Returns NULL if there is insufficient memory available.

BOOL utilStrAddWords(PULONG pcbText, PCHAR *ppcText,
                     ULONG cbWords, PCHAR pcWords,
                     ULONG (*fnFilter)(ULONG cbWord, PCHAR pcWord) )
{
  ULONG      cbText;
  PCHAR      pcText;
  ULONG      cbWord;
  PCHAR      pcWord;
  ULONG      ulSize = 0;
  BOOL       fSpace;
  ULONG      ulRC;

  // Calculate additional space for the result text.

  cbText = cbWords;
  pcText = pcWords;
  while( TRUE )
  {
    utilStrCutWord( &cbText, &pcText, &cbWord, &pcWord );
    if ( cbWord == 0 )
      break;

    ulSize += cbWord + 1; // +1 - SPACE
  }
  if ( ulSize == 0 )
    return TRUE;
  fSpace = *pcbText != 0;
  if ( !fSpace )
    ulSize--;

  // Allocate memory for the result text.

  ulSize += *pcbText;
  pcText = debugReAlloc( *ppcText, ulSize );
  if ( pcText == NULL )
  {
    debug( "Not enough memory" );
    return FALSE;
  }
  *ppcText = pcText;
  pcText += *pcbText;
  *pcbText = ulSize;
  
  // Adding words to the end of text.

  while( TRUE )
  {
    utilStrCutWord( &cbWords, &pcWords, &cbWord, &pcWord );
    if ( cbWord == 0 )
      break;

    ulRC = ( fnFilter == NULL ) ? 1 : fnFilter( cbWord, pcWord );

    if ( ulRC == 1 )
    {
      if ( fSpace )
      {
        *pcText = ' ';
        pcText++;
      }
      else
        fSpace = TRUE;

      memcpy( pcText, pcWord, cbWord );
      pcText += cbWord;
    }
    else if ( ulRC != 0 )
    {
      if ( ulSize == cbWord )
      {
        fSpace = FALSE;
        ulSize = 0;
      }
      else
        ulSize -= ( cbWord + 1 );
    }
    else
    {
      debugFree( *ppcText );
      *ppcText = NULL;
      *pcbText = 0;
      return FALSE;
    }
  }

  if ( *pcbText != ulSize )
  {
    if ( ulSize == 0 )
    {
      debugFree( *ppcText );
      *ppcText = NULL;
    }
    else
      *ppcText = debugReAlloc( *ppcText, ulSize );
    *pcbText = ulSize;
  }

  return TRUE;
}

BOOL utilStrAppend(PULONG pcbText, PCHAR *ppcText, ULONG cbStr, PCHAR pcStr,
                   BOOL fFullStr)
{
  BOOL       fEnoughSpace = cbStr <= *pcbText;

  if ( !fEnoughSpace )
  {
    if ( fFullStr )
      return FALSE;
    cbStr = *pcbText;
  }

  memcpy( *ppcText, pcStr, cbStr );
  *pcbText -= cbStr;
  *ppcText += cbStr;

  return fEnoughSpace;
}

// PCHAR utilStrFindKey(ULONG cbText, PCHAR pcText, ULONG cbKey, PCHAR pcKey,
//                      PULONG pcbVal)
//
// Searches key pointed by pcKey and length cbKey bytes in the string pcText.
// Returns pointer to the value and length of value in pcbVal.
// Returns NULL if key not found.
// Input text format: key1=value1 key2 = "value of key2" key3  =  value3

PCHAR utilStrFindKey(ULONG cbText, PCHAR pcText, ULONG cbKey, PCHAR pcKey,
                     PULONG pcbVal)
{
  ULONG      cbScan;
  PCHAR      pcScan;

  while( TRUE )
  {
    // Skip spaces.
    BUF_SKIP_SPACES( cbText, pcText );  

    // Get pointer to key and length of the key to pcScan and cbScan.
    pcScan = pcText;

    while( (cbText > 0) && ( *pcText != '=' ) && !isspace( *pcText ) )
    {
      cbText--;
      pcText++;
    }
    cbScan = pcText - pcScan;
    if ( cbScan == 0 )
    {
      *pcbVal = 0;
      return NULL;
    }

    if ( (cbScan == cbKey) && ( memicmp( pcKey, pcScan, cbScan ) == 0 ) )
    {
      // Key found.

      BUF_SKIP_SPACES( cbText, pcText );
      if ( ( *pcText != '=' ) || ( *pcText == '\0' ) )
        // No '=' after the key - key without value.
        *pcbVal = 0;
      else
      {
        // Skip '=' and spaces.
        do { pcText++; cbText--; } while( isspace( *pcText ) );
        // End of input string after '=' - key without value.
        if ( *pcText == '\0' )
          *pcbVal = 0;
        else
        {
          // Read value.

          // Value starts with '"'? Yes - skip first '"'.
          BOOL fQuiotes = *pcText == '"';

          if ( fQuiotes )
          {
            pcText++;
            cbText--;
          }

          // Skip leading spaces in value.
          BUF_SKIP_SPACES( cbText, pcText );
          pcScan = pcText;

          // Skip characters up to '"' or space.
          while( ( cbText > 0 ) &&
                 ( fQuiotes ? (*pcText != '"') : !isspace( *pcText ) ) )
          {
            pcText++;
            cbText--;
          }
          *pcbVal = pcText - pcScan;
        }
      }
      return pcScan;
    } // if key found
  } // while( TRUE )
}

// PSZ utilStrNewUnescapeQuotes(ULONG cbText, PCHAR pcText, BOOL fIfQuoted)
//
// Unescapes characters '\', '"' and returns pointer to the new string (you
// must use debugFree() later). Leading and trailing quotas will not be
// included in the result. When fIfQuoted is TRUE characters '\', '"' will be
// unescaped only if first character in text is quota.

PSZ utilStrNewUnescapeQuotes(ULONG cbText, PCHAR pcText, BOOL fIfQuoted)
{
  BOOL       fStartQuote;
  PCHAR      pcScan, pcEnd, pcResult;
  BOOL       fSlash = FALSE;
  ULONG      cbResult = 0;
  PSZ        pszResult;

  if ( ( cbText == 0 ) || ( pcText == NULL ) )
    return NULL;

  fStartQuote = *pcText == '"';
  if ( !fStartQuote && fIfQuoted )
    return utilStrNewSZ( cbText, pcText );

  if ( fStartQuote )
  {
    cbText--;
    pcText++;
    if ( ( cbText != 0 ) && ( pcText[ cbText - 1 ] == '"' ) )
      cbText--;

    if ( cbText == 0 )
      return NULL;
  }

  pcScan = pcText;
  pcEnd = &pcText[cbText];
  while( pcScan != pcEnd )
  {
    if ( !fSlash )
    {
      if ( *pcScan == '\\' )
        fSlash = TRUE;
      else
        cbResult++;
    }
    else
    {
      if ( ( *pcScan != '\\' ) && ( *pcScan != '"' ) )
        cbResult += 2;
      else
        cbResult++;

      fSlash = FALSE;
    }

    pcScan++;
  }

  if ( cbResult == 0 )
    return NULL;

  pszResult = debugMAlloc( cbResult + 1 );
  if ( pszResult == NULL )
    return NULL;

  pcResult = pszResult;
  pcScan = pcText;
  fSlash = FALSE;

  while( pcScan != pcEnd )
  {
    if ( !fSlash )
    {
      if ( *pcScan == '\\' )
        fSlash = TRUE;
      else
        *(pcResult++) = *pcScan;
    }
    else
    {
      if ( ( *pcScan != '\\' ) && ( *pcScan != '"' ) )
        *(pcResult++) = '\\';
      *(pcResult++) = *pcScan;

      fSlash = FALSE;
    }

    pcScan++;
  }

  *pcResult = '\0';
  return pszResult;
}

// PCHAR utilStrFindOption(ULONG cbText, PCHAR pcText,
//                         ULONG cbName, PCHAR pcName, PULONG pcbVal)
//
// Searches option pointed by pcName and length cbName bytes in the string
// pcText.
// Returns pointer to the value and length of value in pcbVal. Quotes will be
// included in result, quoted text can be escaped with '\' for '"' and '\'
// characters.
// Returns NULL if key not found.
// Input text format:  opt1=1; opt2 = "\\second\\ \"Value\"" ; opt3 = value3

PCHAR utilStrFindOption(ULONG cbText, PCHAR pcText,
                        ULONG cbName, PCHAR pcName, PULONG pcbVal)
{
  ULONG      cbScanName, cbScanVal;
  PCHAR      pcScanName, pcScanVal;

  while( cbText > 0 )
  {
    BUF_SKIP_SPACES( cbText, pcText );
    cbScanName = 0;
    pcScanName = pcText;

    while( ( cbText > 0 ) &&
           ( memchr( "=;\t\r\n\0", pcScanName[cbScanName], 6 ) == NULL ) )
    {
      cbScanName++;
      cbText--;
      pcText++;
    }
    BUF_RTRIM( cbScanName, pcScanName );

    if ( *pcText == '=' )
    {
      cbText--;
      pcText++;
      BUF_SKIP_SPACES( cbText, pcText );

      if ( *pcText == '"' )
      {
        BOOL           fSlash = FALSE;

        cbScanVal = 1;
        pcScanVal = pcText;
        pcText++;
        cbText--;

        while( cbText > 0 )
        {
          if ( !fSlash )
          {
            if ( *pcText == '"' )
            {
              pcText++;
              cbText--;
              cbScanVal++;
              break;
            }

            if ( *pcText == '\\' )
              fSlash = TRUE;
          }
          else
            fSlash = FALSE;

          pcText++;
          cbText--;
          cbScanVal++;
        }

        BUF_SKIP_SPACES( cbText, pcText );
      }
      else
      {
        cbScanVal = 0;
        pcScanVal = pcText;
        while( ( cbText > 0 ) && ( *pcText != ';' ) && ( *pcText != '\0' ) )
        {
          pcText++;
          cbText--;
          cbScanVal++;
        }

        BUF_RTRIM( cbScanVal, pcScanVal );
      }
    }
    else // *pcText == '='
    {
      cbScanVal = 0;
      pcScanVal = pcScanName;
    }

    if ( *pcText == ';' )
    {
      pcText++;
      cbText--;
    }

    if ( ( cbScanName == cbName ) &&
         ( memcmp( pcName, pcScanName, cbName ) == 0 ) )
    {
      *pcbVal = cbScanVal;
      return pcScanVal;
    }
  }

  *pcbVal = 0;
  return NULL;
}

PSZ utilStrNewGetOption(ULONG cbText, PCHAR pcText, PSZ pszName)
{
  ULONG      cbVal;
  PCHAR      pcVal = utilStrFindOption( cbText, pcText,
                                        strlen( pszName ), pszName, &cbVal );

  return utilStrNewUnescapeQuotes( cbVal, pcVal, TRUE );
}

BOOL utilStrToULong(LONG cbStr, PCHAR pcStr, ULONG ulMin, ULONG ulMax,
                    PULONG pulValue)
{
  CHAR       acVal[24];
  PCHAR      pcEnd;
  ULONG      ulValue;

  if ( cbStr < 0 )
    cbStr = strlen( pcStr );

  BUF_SKIP_SPACES( cbStr, pcStr );
  BUF_RTRIM( cbStr, pcStr );

  if ( ( cbStr == 0 ) || ( cbStr >= sizeof(acVal) ) || ( *pcStr == '-' ) )
    return FALSE;

  memcpy( acVal, pcStr, cbStr );
  acVal[cbStr] = '\0';

  ulValue = strtoul( acVal, &pcEnd, 0 );
  if ( ( pcEnd != &acVal[cbStr] ) || ( ulValue < ulMin ) ||
       ( ulValue > ulMax ) )
    return FALSE;

  if ( pulValue != NULL )
    *pulValue = ulValue;

  return TRUE;
}

BOOL utilStrToLong(LONG cbStr, PCHAR pcStr, LONG lMin, LONG lMax,
                   PLONG plValue)
{
  CHAR       acVal[24];
  PCHAR      pcEnd;
  LONG       lValue;

  if ( cbStr < 0 )
    cbStr = strlen( pcStr );

  BUF_SKIP_SPACES( cbStr, pcStr );
  BUF_RTRIM( cbStr, pcStr );

  if ( ( cbStr == 0 ) || ( cbStr >= sizeof(acVal) ) )
    return FALSE;

  memcpy( acVal, pcStr, cbStr );
  acVal[cbStr] = '\0';

  lValue = strtol( acVal, &pcEnd, 0 );
  if ( ( pcEnd != &acVal[cbStr] ) ||
       (
         ( lMin < lMax ) &&
         ( ( lValue < lMin ) || ( lValue > lMax ) )
       )
     )
    return FALSE;

  if ( plValue != NULL )
    *plValue = lValue;

  return TRUE;
}

BOOL utilStrToBool(LONG cbStr, PCHAR pcStr, PBOOL pfValue)
{
  if ( utilStrWordIndex( "1 Y YES ON", cbStr, pcStr ) != -1 )
  {
    *pfValue = TRUE;
    return TRUE;
  }

  if ( utilStrWordIndex( "0 N NO OFF", cbStr, pcStr ) != -1 )
  {
    *pfValue = FALSE;
    return TRUE;
  }

  return FALSE;
}

LONG utilStrFromBytes(ULLONG ullVal, ULONG cbBuf, PCHAR pcBuf)
{
#define _IDS_TB        0
#define _IDS_GB        1
#define _IDS_MB        2
#define _IDS_KB        3
#define _IDS_BYTES     4
  static PSZ    apszEu[] = { "Tb", "Gb", "Mb", "Kb", "Bytes" };
  ULONG		ulVal = ullVal;
  ldiv_t	stDiv;
  ULONG		ulEuId;
  LONG		cbVal;

  if ( ulVal < 1024 )
    cbVal = _snprintf( pcBuf, cbBuf, "%u %s", (ULONG)ullVal, apszEu[_IDS_BYTES] );
  else
  {
    if ( ullVal >= (1024ULL * 1024ULL * 1024ULL * 1024ULL) )
    {
      ulVal = ullVal / (1024 * 1024 * 1024);
      ulEuId = _IDS_TB;
    }
    else if ( ullVal >= (1024 * 1024 * 1024) )
    {
      ulVal = ullVal / (1024 * 1024);
      ulEuId = _IDS_GB;
    }
    else if ( ullVal >= (1024 * 1024) )
    {
      ulVal = ullVal / 1024;
      ulEuId = _IDS_MB;
    }
    else
      ulEuId = _IDS_KB;

    stDiv = ldiv( ulVal, 1024 ); 

    if ( ulVal > 102400 || stDiv.rem == 0 )
      // NNN / NN / N
      cbVal = _snprintf( pcBuf, cbBuf, "%u %s", stDiv.quot, apszEu[ulEuId] );
    else
      // N.NN / NN.N
      cbVal = _snprintf( pcBuf, cbBuf,
                         ulVal < 10240 ? "%.2f %s" : "%.1f %s",
                         (float)ulVal / 1024, apszEu[ulEuId] );
  }

  return cbVal;
}

// BOOL utilStrSplitWords(ULONG cbStr, PCHAR pcStr, PSZ pszDelimiter,
//                        PULONG pulWords, PUTILSTRPART pWords)
//
// pulWords: In: maximum items in pWords, Out: total words in the string pcStr.
// Returns number of missed words (when not enough size of pParts) or 0 (all of
// words stored).

BOOL utilStrSplitWords(ULONG cbStr, PCHAR pcStr, PULONG pulWords,
                       PUTILSTRPART pWords)
{
  ULONG      cbWord;
  PCHAR      pcWord;
  ULONG      cFound = 0;
  ULONG      ulMissed;
  ULONG      ulWords = *pulWords;

  while( utilStrCutWord( &cbStr, &pcStr, &cbWord, &pcWord ) )
  {
    if ( cFound < ulWords )
    {
      pWords->pcPart = pcWord;
      pWords->cbPart = cbWord;
      pWords++;
    }
    cFound++;
  }

  ulMissed = cFound <= ulWords ? 0 : ( cFound - ulWords );
  *pulWords = cFound;

  for( ; cFound < ulWords; cFound++ )
  {
    pWords->pcPart = NULL;
    pWords->cbPart = 0;
    pWords++;
  }

  return ulMissed;
}

// BOOL utilStrFindParts(ULONG cbStr, PCHAR pcStr, PSZ pszDelimiter,
//                       PULONG pulParts, PUTILSTRPART pParts)
//
// pszDelimiter: ASCIIZ - delimeters.
// pulParts: In: maximum items in pParts, Out: total items in the string pcStr.
//
// Empty string - result is one zero-lenght part;
// String is ended witd delimiter - result have zero-lenght last part;
// String is one delimiter - result is two zero-lenght parts.
//
// Returns number of missed parts (when not enough size of pParts) or 0 (all of
// parts stored).

BOOL utilStrFindParts(ULONG cbStr, PCHAR pcStr, PSZ pszDelimiter,
                      PULONG pulParts, PUTILSTRPART pParts)
{
  ULONG      cFound = 0;
  PCHAR      pcPart = pcStr;
  ULONG      cbPart = 0;
  ULONG      ulMissed;

  while( cbStr != 0 )
  {
    if ( strchr( pszDelimiter, *pcStr ) != NULL )
    {
      if ( cFound < *pulParts )
      {
        pParts->pcPart = pcPart;
        pParts->cbPart = cbPart;
        pParts++;
      }
      cFound++;

      pcStr++;
      pcPart = pcStr;
      cbPart = 0;
    }
    else
    {
      pcStr++;
      cbPart++;
    }
    cbStr--;
  }

  if ( cFound < *pulParts )
  {
    pParts->pcPart = pcPart;
    pParts->cbPart = cbPart;
  }
  cFound++;
  ulMissed = cFound <= *pulParts ? 0 : ( cFound - *pulParts );
  *pulParts = cFound;

  return ulMissed;
}

// BOOL utilStrBuildParts(ULONG cbStr, PCHAR pcStr, PSZ pszDelimiter,
//                        ULONG ulParts, BOOL fRev, CHAR cNewDelim,
//                        PULONG pcbBuf, PCHAR pcBuf)
//
// Splits input string pcStr on parts by delimiter(s) pszDelimiter and
// buils a new string in pcBuf cntains up to ulParts parts with a new delimiter
// cNewDelim. If upParts is 0 - all parts will be used. If fRev is TRUE then
// parts will be in reverse order.
// pcbBuf: In: size of the output buffer pcBuf, Out: number of writen bytes.
// Returns FALSE when not enough space at pcBuf.

BOOL utilStrBuildParts(ULONG cbStr, PCHAR pcStr, PSZ pszDelimiter,
                       ULONG ulParts, BOOL fRev, CHAR cNewDelim,
                       PULONG pcbBuf, PCHAR pcBuf)
{
  ULONG                cbBuf = *pcbBuf;
  ULONG                cbRes = 0;
  BOOL                 fResult = TRUE;
  ULONG                ulFound = 0;
  ULONG                ulIdx;
  PUTILSTRPART         pPart, pParts;

  utilStrFindParts( cbStr, pcStr, pszDelimiter, &ulFound, NULL );
  if ( ulFound == 0 )
  {
    *pcbBuf = 0;
    return TRUE;
  }

  pParts = debugMAlloc( ulFound * sizeof(UTILSTRPART) );
  if ( pParts == NULL )
  {
    debug( "Not enough memory" );
    return 0;
  }

  utilStrFindParts( cbStr, pcStr, pszDelimiter, &ulFound, pParts );

  if ( ( ulParts == 0 ) || ( ulFound < ulParts ) )
    ulParts = ulFound;

  pPart = fRev ? &pParts[ulParts - 1] : &pParts[ulFound - ulParts];

  for( ulIdx = 0; ulIdx < ulParts; ulIdx++ )
  {
    if ( ulIdx != 0 )
    {
      if ( cbBuf <= pPart->cbPart )
      {
        fResult = FALSE;
        break;
      }

      *(pcBuf++) = cNewDelim;
      cbBuf--;
      cbRes++;
    }
    else if ( cbBuf < pPart->cbPart )
    {
      fResult = FALSE;
      break;
    }

    memcpy( pcBuf, pPart->pcPart, pPart->cbPart );
    pcBuf += pPart->cbPart;
    cbBuf -= pPart->cbPart;
    cbRes += pPart->cbPart;

    if ( fRev )
      pPart--;
    else
      pPart++;
  }

  debugFree( pParts );

  *pcbBuf = cbRes;
  return fResult;
}

ULONG utilStrFindURIHosts(ULONG cbText, PCHAR pcText,
                          BOOL (*fnFound)(ULONG cbAddr, PCHAR pcAddr, PVOID pData),
                          PVOID pData)
{
  PCHAR      pcColon;
  PCHAR      pcBegin, pcEnd;
  PCHAR      pcTextEnd;
  ULONG      cAddr = 0;

  pcTextEnd = &pcText[cbText];

  while( cbText > 5 )
  {
    pcColon = memchr( pcText, ':', cbText - 3 );
    if ( pcColon == NULL )
      break;

    if ( *((PUSHORT)&pcColon[1]) != (USHORT)0x2F2F || !isalnum( pcColon[3] ) )
      pcEnd = &pcColon[1];
    else
    {
      pcEnd = &pcColon[3];

      pcBegin = pcColon;
      while( ( pcBegin > pcText ) && isalpha( *(pcBegin-1) ) )
        pcBegin--;

      if ( ( pcColon != pcBegin ) && ( pcColon - pcBegin ) <= 64 )
      {
        while( ( pcEnd < pcTextEnd ) &&
               ( isalnum( *pcEnd ) || strchr( "-_.", *pcEnd ) != NULL ) )
          pcEnd++;

// BOOL utilVerifyDomainName(ULONG cbDomain, PCHAR pcDomain)

        if ( !fnFound( pcEnd - pcBegin, pcBegin, pData ) )
          break;

        cAddr++;
      }
    }

    cbText -= ( pcEnd - pcText );
    pcText = pcEnd;
  }

  return cAddr;
}

PSZ utilStrNewSZ(ULONG cbStr, PCHAR pcStr)
{
  PSZ        pszRes;

  if ( ( cbStr == 0 ) || ( pcStr == NULL ) )
    return NULL;

  pszRes = debugMAlloc( cbStr + 1 );
  if ( pszRes != NULL )
  {
    memcpy( pszRes, pcStr, cbStr );
    pszRes[cbStr] = '\0';
  }
  return pszRes;
}

PCHAR utilStrLastChar(ULONG cbText, PCHAR pcText, CHAR chSearch)
{
  PCHAR      pcScan;

  if ( ( pcText == NULL ) || ( cbText == 0 ) )
    return NULL;

  pcScan = &pcText[cbText-1];
  while( *pcScan != chSearch )
  {
    if ( pcScan == pcText )
      return NULL;
    pcScan--;
  }

  return pcScan;
}

BOOL utilStrToInAddr(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr)
{
  CHAR       szIP[16];
  u_long     ulAddr;

  BUF_SKIP_SPACES( cbStr, pcStr );
  BUF_RTRIM( cbStr, pcStr );

  if ( ( cbStr > ( sizeof(szIP) - 1 ) ) || ( cbStr == 0 ) )
    return FALSE;

  memcpy( &szIP, pcStr, cbStr );
  szIP[cbStr] = '\0';

  ulAddr = inet_addr( szIP );
  if ( pInAddr != NULL )
    pInAddr->s_addr = ulAddr;

  return ulAddr != ((u_long)(-1));
}

BOOL utilStrToMask(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr)
{
  CHAR       szIP[16];
  ULONG      ulMask;
  ULONG      ulIdx;

  BUF_SKIP_SPACES( cbStr, pcStr );
  BUF_RTRIM( cbStr, pcStr );

  if ( ( cbStr == 15 ) && ( memcmp( pcStr, "255.255.255.255", 15 ) == 0 ) )
  {
    pInAddr->s_addr = 0xFFFFFFFF;
    return TRUE;
  }

  if ( ( cbStr > ( sizeof(szIP) - 1 ) ) || ( cbStr == 0 ) )
    return FALSE;

  memcpy( szIP, pcStr, cbStr );
  szIP[cbStr] = '\0';

  // nn , where 0 <= nn <= 32
  if ( strspn( szIP, "0123456789" ) == cbStr )
    return utilCIDRLenToInAddr( atol( szIP ), pInAddr);

  // nnn.nnn.nnn.nnn

  ulMask = inet_addr( szIP );

  if ( ulMask == ((u_long)(-1)) )
    return FALSE;
  pInAddr->s_addr = ulMask;

  // Test network mask.
  ulMask = ntohl( ulMask );
  for( ulIdx = 0; (ulIdx < 31) && ( (ulMask & 0x80000000) != 0 ); ulIdx++ )
    ulMask <<= 1;

  return ulMask == 0;
}

// BOOL utilStrToInAddrRange(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr1,
//                           struct in_addr *pInAddr2)
//
// String samples: 192.168.1.2 , 192.168.1.0/24 , 192.168.1.0/255.255.255.0 ,
//                 192.168.1.100-192.168.1.200 , all , Any

BOOL utilStrToInAddrRange(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr1,
                          struct in_addr *pInAddr2)
{
  PCHAR      pcDiv;

  if ( utilStrWordIndex( "ANY ALL", cbStr, pcStr ) != -1 )
  {
    pInAddr1->s_addr = 0;
    pInAddr2->s_addr = 0xFFFFFFFF;
    return TRUE;
  }

  pcDiv = memchr( pcStr, '/', cbStr );
  if ( pcDiv != NULL )
  {
    // Like: 192.168.1.0/24 or 192.168.1.0/255.255.255.0

    struct in_addr     stMask;

    if ( !utilStrToInAddr( pcDiv - pcStr, pcStr, pInAddr1 ) ||
         !utilStrToMask( cbStr - (pcDiv - pcStr) - 1, pcDiv + 1, &stMask ) )
      return FALSE;

    pInAddr1->s_addr &= stMask.s_addr;
    pInAddr2->s_addr = pInAddr1->s_addr | ~stMask.s_addr;
    return TRUE;
  }

  pcDiv = memchr( pcStr, '-', cbStr );
  if ( pcDiv != NULL )
  {
    // Like: 192.168.1.100-192.168.1.200

    return (
      utilStrToInAddr( pcDiv - pcStr, pcStr, pInAddr1 ) &&
      utilStrToInAddr( cbStr - (pcDiv - pcStr) - 1, pcDiv + 1, pInAddr2 ) &&
      ( ntohl( pInAddr1->s_addr ) <= ntohl( pInAddr2->s_addr ) ) );
  }

  if ( !utilStrToInAddr( cbStr, pcStr, pInAddr1 ) )
    // Like: 192.168.1.100
    return FALSE;

  *pInAddr2 = *pInAddr1;
  return TRUE;
}

BOOL utilInAddrRangeToStr(struct in_addr *pInAddr1, struct in_addr *pInAddr2,
                          ULONG cbBuf, PCHAR pcBuf)
{
  CHAR                 acAddr1[16], acAddr2[16];
  struct in_addr       stInAddr1, stInAddr2;
  ULONG                ulAddr1, ulAddr2, ulMask, ulIdx;

  // 0.0.0.0 - 255.255.255.255 -> "any"
  if ( ( pInAddr1->s_addr == 0 ) && ( pInAddr2->s_addr == ~0 ) )
    return _snprintf( pcBuf, cbBuf, "any" ) != -1;

  // First address equal second address -> n.n.n.n
  if ( pInAddr1->s_addr == pInAddr2->s_addr )
    return _snprintf( pcBuf, cbBuf, "%s", inet_ntoa( *pInAddr1 ) ) != -1;

  ulAddr1 = ntohl( pInAddr1->s_addr );
  ulAddr2 = ntohl( pInAddr2->s_addr );
  ulMask = ~ulAddr1 & ulAddr2;
  stInAddr1.s_addr = pInAddr1->s_addr;
  stInAddr2.s_addr = htonl( ~ulMask );
    // stInAddr1 - network address, stInAddr2 - network mask.

  // Check mask.
  for( ulIdx = 0; ulIdx < 31; ulIdx++ )
  {
    if ( (ulMask & 0x01) == 0 )
      break;
    ulMask >>= 1;
  }

  if ( ulMask == 0 )
  {
    // Mask is valid. Range can be represented as address/mask.
    strcpy( acAddr1, inet_ntoa( stInAddr1 ) );
    strcpy( acAddr2, inet_ntoa( stInAddr2 ) );
    return _snprintf( pcBuf, cbBuf, "%s/%s", acAddr1, acAddr2 ) != -1;
  }

  // Mask is invalid. Represent range as address 1 - address 2.
  strcpy( acAddr1, inet_ntoa( *pInAddr1 ) );
  strcpy( acAddr2, inet_ntoa( *pInAddr2 ) );
  return _snprintf( pcBuf, cbBuf, "%s - %s", acAddr1, acAddr2 ) != -1;
}

// BOOL utilStrToInAddrPort(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr,
//                          PUSHORT pusPort, BOOL fAnyIP, USHORT usDefaultPort)
//
// Parses the string pointed by pcStr and writes ip-address to pInAddr and
// port to pusPort. The input string may be in the format:
//   [n.n.n.n|*|any|all][:port]
// Values "0.0.0.0", "*", "any", "all" for ip-address allowed only when fAnyIP
// is TRUE and result ip will be 0.0.0.0. If port is not specified in string
// usDefaultPort will be used.

BOOL utilStrToInAddrPort(ULONG cbStr, PCHAR pcStr, struct in_addr *pInAddr,
                         PUSHORT pusPort, BOOL fAnyIP, USHORT usDefaultPort)
{
  PCHAR      pcIPEnd = memchr( pcStr, ':', cbStr );

  if ( pcIPEnd != NULL )
  {
    ULONG    ulPort;

    if ( !utilStrToULong( cbStr - (pcIPEnd - pcStr) - 1, pcIPEnd + 1,
                          1, 0xFFFF, &ulPort ) )
      return FALSE;

    *pusPort = ulPort;
    cbStr = pcIPEnd - pcStr;
  }
  else
    *pusPort = usDefaultPort;

  if ( utilStrWordIndex( "* ANY ALL", cbStr, pcStr ) != -1 )
  {
    pInAddr->s_addr = 0;
    return fAnyIP;
  }

  return utilStrToInAddr( cbStr, pcStr, pInAddr ) &&
         ( pInAddr->s_addr != 0 || fAnyIP );
}

// BOOL utilCIDRLenToInAddr(ULONG ulCIDRLen, struct in_addr *pInAddr)
//
// Makes network mask from number of bits ulCIDRLen. 0 <= ulCIDRLen <= 32

BOOL utilCIDRLenToInAddr(ULONG ulCIDRLen, struct in_addr *pInAddr)
{
  if ( ulCIDRLen > 32 )
    return FALSE;

  pInAddr->s_addr = ulCIDRLen == 32 ?
                      0xFFFFFFFF : htonl( ~(0xFFFFFFFF >> ulCIDRLen) );
  return TRUE;
}


// BOOL utilStrTimeToSec(ULONG cbStr, PCHAR pcStr, PULONG pulSec)
//
// Converts string to seconds value. Format of cbStr/pcStr:
//   N d[ay[s]][.] N h[our[s]][.] N m[in[ute[s]]][.] N s[ec[ond[s]]][.]
// if string is NNNN (integer >=0) - returns this value converted to ULONG.
// Returns TRUE if input string is valid and write seconds value to pulSec (if
// it's not a NULL).

BOOL utilStrTimeToSec(ULONG cbStr, PCHAR pcStr, PULONG pulSec)
{
  ULONG      ulSec = 0;
  ULONG      ulVal;
  PCHAR      pcEnd = &pcStr[cbStr];
  ULONG      cbEU;
  PCHAR      pcEU;
  ULONG      ulMult;
  ULONG      ulLastMult = ~0;

  while( TRUE )
  {
    while( ( pcStr < pcEnd ) && isspace( *pcStr ) )
      pcStr++;

    if ( pcStr == pcEnd )
      break;

    if ( !isdigit( *pcStr ) )
      return FALSE;

    ulVal = 0;
    while( ( pcStr < pcEnd ) && isdigit( *pcStr ) )
    {
      ulVal = ( ulVal * 10 ) + ( *pcStr - '0' );
      pcStr++;
    }

    while( ( pcStr < pcEnd ) && isspace( *pcStr ) )
      pcStr++;

    pcEU = pcStr;
    while( ( pcStr < pcEnd ) && isalpha( *pcStr ) )
      pcStr++;
    cbEU = pcStr - pcEU;

    if ( ( cbEU == 0 ) && ( ulLastMult == ~0 ) )
    {
      ulSec = ulVal;
      break;
    }

    if ( memicmp( pcEU, "days", cbEU ) == 0 )
      ulMult = (60 * 60 * 24);
    else if ( memicmp( pcEU, "hours", cbEU ) == 0 )
      ulMult = (60 * 60);
    else if ( memicmp( pcEU, "minutes", cbEU ) == 0 )
      ulMult = 60;
    else if ( memicmp( pcEU, "seconds", cbEU ) == 0 )
      ulMult = 1;
    else
      return FALSE;

    if ( ulMult >= ulLastMult )
      return FALSE;
    ulLastMult = ulMult;

    ulVal *= ulMult;

    if ( ( pcStr < pcEnd ) && ( *pcStr == '.' ) )
      pcStr++;

    ulSec += ulVal;
  }

  if ( pulSec != NULL )
    *pulSec = ulSec;
  return TRUE;
}

// LONG utilSecToStrTime(ULONG ulSec, ULONG cbBuf, PCHAR pcBuf)
//
// Writes at the buffer seconds ulSec in ASCIZ string: Nd [Nh [Nm [Hs]]].
// Returns number of writen bytes or -1 if not enough buffer size.

LONG utilSecToStrTime(ULONG ulSec, ULONG cbBuf, PCHAR pcBuf)
{
  ULONG      ulDays, ulHours, ulMinutes;
  LONG       cBytes, lRes = 0;

  ulDays = ulSec / (60 * 60 * 24);
  ulSec %= (60 * 60 * 24);
  if ( ulDays != 0 )
  {
    lRes = _snprintf( pcBuf, cbBuf, "%ud", ulDays );
    if ( lRes < 0 )
      return -1;
    pcBuf += lRes;
    cbBuf -= lRes;
  }

  ulHours = ulSec / (60 * 60);
  ulSec %= (60 * 60);
  if ( ulHours != 0 )
  {
    cBytes = _snprintf( pcBuf, cbBuf, lRes == 0 ? "%uh" : " %uh", ulHours );
    if ( cBytes < 0 )
      return -1;
    lRes += cBytes;
    pcBuf += lRes;
    cbBuf -= lRes;
  }

  ulMinutes = ulSec / 60;
  ulSec %= 60;
  if ( ulMinutes != 0 )
  {
    cBytes = _snprintf( pcBuf, cbBuf, lRes == 0 ? "%um" : " %um", ulMinutes );
    if ( cBytes < 0 )
      return -1;
    lRes += cBytes;
    pcBuf += cBytes;
    cbBuf -= cBytes;
  }

  if ( ulSec != 0 )
  {
    cBytes = _snprintf( pcBuf, cbBuf, lRes == 0 ? "%us" : " %us", ulSec );
    if ( cBytes < 0 )
      return -1;
    lRes += cBytes;
  }

  return lRes;
}

// BOOL utilStrToBytes(ULONG cbStr, PCHAR pcStr, PULONG pulBytes)
//
// Converts string to bytes value. Format of cbStr/pcStr:
//   N [[k|m|g][b[ytes]]] , for ex.: 12, 12Bytes, 12 Kb, 12MBYTES, 1 kbyte
// if string is NNNN (integer >=0) - returns this value converted to ULONG.
// Returns TRUE if input string is valid and write seconds value to pulBytes
// (if it's not a NULL).

BOOL utilStrToBytes(ULONG cbStr, PCHAR pcStr, PULONG pulBytes)
{
  ULONG      ulVal = 0;
  ULONG      ulMult;
  PCHAR      pcEnd = &pcStr[cbStr];
  ULONG      cbEU;
  PCHAR      pcEU;

  while( ( pcStr < pcEnd ) && isspace( *pcStr ) )
    pcStr++;

  if ( ( pcStr == pcEnd ) || !isdigit( *pcStr ) )
    return FALSE;

  while( ( pcStr < pcEnd ) && isdigit( *pcStr ) )
  {
    ulVal = ( ulVal * 10 ) + ( *pcStr - '0' );
    pcStr++;
  }

  while( ( pcStr < pcEnd ) && isspace( *pcStr ) )
    pcStr++;

  pcEU = pcStr;
  while( ( pcStr < pcEnd ) && isalpha( *pcStr ) )
    pcStr++;
  cbEU = pcStr - pcEU;

  if ( cbEU != 0 )
  {
    switch( toupper( *pcEU ) )
    {
      case 'B':
        ulMult = 1;
        break;

      case 'K':
        ulMult = 1024;
        break;

      case 'M':
        ulMult = 1024 * 1024;
        break;

      case 'G':
        ulMult = 1024 * 1024 * 1024;
        break;

      default:
        return FALSE;
    }

    if ( ulMult != 1 )
    {
      pcEU++;
      cbEU--;
    }

    if ( memicmp( pcEU, "BYTES", cbEU ) != 0 )
      return FALSE;

    ulVal *= ulMult;
  }

  if ( pulBytes != NULL )
    *pulBytes = ulVal;

  return TRUE;
}

// LONG utilStrFormat(ULONG cbBuf, PCHAR pcBuf, LONG cbFormat, PCHAR pcFormat,
//                    ULONG (*fnValue)(CHAR chKey, ULONG cbBuf, PCHAR pcBuf,
//                                    PVOID pData),
//                    PVOID pData)
//
// Builds a string in pcBuf for given template pcFormat. Template may contains
// %-keys: %c, where 'c' is any character. The user function fnValue() will be
// called for each template key. Character % may be encoded at template as %%.
// The user function must fill buffer for given key chKey and return number of
// bytes written (must be less or equal cbBuf). pData is a user data pointer
// for function fnValue() last argument.
// Returns number of writed bytes (up to '\0') or -1 when too big value
// returned by fnValue().
//
// pcFormat may be ASCIIZ string when cbFormat < 0.

LONG utilStrFormat(ULONG cbBuf, PCHAR pcBuf, LONG cbFormat, PCHAR pcFormat,
                   ULONG (*fnValue)(CHAR chKey, ULONG cbBuf, PCHAR pcBuf,
                                    PVOID pData),
                   PVOID pData)
{
  BOOL       fKey = FALSE;
  ULONG      cbValue;
  PCHAR      pcStart = pcBuf;

  if ( pcFormat == NULL )
    return -1;

  if ( cbBuf == 0 )
    return 0;

  cbBuf--; // Left one byte for terminating ZERO.

  if ( cbFormat < 0 )
    cbFormat = strlen( pcFormat );

  while( ( cbFormat != 0 ) && ( cbBuf != 0 ) )
  {
    if ( fKey )
    {
      fKey = FALSE;

      if ( *pcFormat == '%' )
        goto utilStrFormat01;

      cbValue = fnValue( *pcFormat, cbBuf, pcBuf, pData );
      if ( cbBuf < cbValue )
      {
        debug( "Too big value returned by callback function" );
        return -1;
      }

      cbBuf -= cbValue;
      pcBuf += cbValue;
    }
    else if ( *pcFormat != '%' )
    {
utilStrFormat01:
      cbBuf--;
      *pcBuf = *pcFormat;
      pcBuf++;
    }
    else
      fKey = TRUE;

    pcFormat++;
    cbFormat--;
  }

  *pcBuf = '\0';
  return pcBuf - pcStart;
}

/*
// BOOL utilIPListAddStr(PUTILIPLIST pIPList, ULONG cbList, PCHAR pcList,
//                       ULONG ulUser)
//
// pIPList must be filled with zeros for first use. Addresses listed in the
// text cbList/pcList should be in format:
//   192.168.1.2 192.168.2.0/255.255.255.0 192.168.3.0/24
//   192.168.4.10-192.168.4.20 0/0 any all
// and separated by a space. The value ulUser will be associated with new
// addresses. Subsequent calls utilIPListAddStr() for pIPList will be add new
// addresses to the list.

BOOL utilIPListAddStr(PUTILIPLIST pIPList, ULONG cbList, PCHAR pcList,
                      ULONG ulUser)
{
  ULONG                cNewRec = utilStrWordsCount( cbList, pcList );
  ULONG                cbRec;
  PCHAR                pcRec;
  PUTILIPLISTREC       paNewList = debugReAlloc( pIPList->paList,
                         sizeof(UTILIPLISTREC) * ( cNewRec + pIPList->ulCount ) );
  PUTILIPLISTREC       pIPRec;
  struct in_addr       stInAddrFirst, stInAddrLast;

  if ( paNewList == NULL )
  {
    debug( "Not enough memory" );
    return FALSE;
  }
  pIPRec = &paNewList[pIPList->ulCount];
  pIPList->paList = paNewList;

  while( TRUE )
  {
    utilStrCutWord( &cbList, &pcList, &cbRec, &pcRec );
    if ( cbRec == 0 )
    {
      pIPList->ulCount += cNewRec;
      return TRUE;
    }

    if ( !utilStrToInAddrRange( cbRec, pcRec, &stInAddrFirst, &stInAddrLast ) )
    {
      debug( "Invalid address range: %s", debugBufPSZ( pcRec, cbRec ) );
      break;
    }

    pIPRec->ulFirstAddr = ntohl( stInAddrFirst.s_addr );
    pIPRec->ulLastAddr = ntohl( stInAddrLast.s_addr );
    pIPRec->ulUser = ulUser;
    pIPRec++;
  }

  // Rollback list.
  pIPList->paList = debugReAlloc( pIPList->paList,
                                  sizeof(UTILIPLISTREC) * pIPList->ulCount );
  return FALSE;
}

// BOOL utilIPListCheck(PUTILIPLIST pIPList, struct in_addr stInAddr,
//                      PULONG pulUser)
//
// Returns TRUE if the address stInAddr is in the list pIPList. Address
// associated value will be stored to pulUser if it is not a NULL.

BOOL utilIPListCheck(PUTILIPLIST pIPList, struct in_addr stInAddr,
                     PULONG pulUser)
{
  ULONG                ulIdx;
  ULONG                ulAddr = ntohl( stInAddr.s_addr );
  PUTILIPLISTREC       pIPRec = pIPList->paList;

  for( ulIdx = 0; ulIdx < pIPList->ulCount; ulIdx++, pIPRec++ )
  {
    if ( ulAddr >= pIPRec->ulFirstAddr && ulAddr <= pIPRec->ulLastAddr )
    {
      if ( pulUser != NULL )
        *pulUser = pIPRec->ulUser;
      return TRUE;
    }
  }

  return FALSE;
}
*/

// BOOL utilMakePathToFile(ULONG cbFName, PCHAR pcFName)
//
// Creates a path (all subdirectoies) for given file name.

BOOL utilMakePathToFile(ULONG cbFName, PCHAR pcFName)
{
  struct stat          stStat;
  CHAR                 acBuf[_MAX_PATH];
  PCHAR                pcSlash;
  ULONG                cbBuf;
  PCHAR                pcBuf;
  ULONG                cbDir;

  if ( ( pcFName == NULL ) || ( cbFName == 0 ) || ( cbFName >= sizeof(acBuf) ) )
    return FALSE;

  pcSlash = utilStrLastChar( cbFName, pcFName, '\\' );
  if ( pcSlash == NULL )
    return TRUE;

  cbBuf = pcSlash - pcFName;
  memcpy( &acBuf, pcFName, cbBuf );
  acBuf[cbBuf] = '\0';

  if ( stat( pcFName, &stStat ) != -1 )
    // Given file name contains a existent path.
    return S_ISDIR( stStat.st_mode );

  pcBuf = acBuf;

  while( TRUE )
  {
    pcSlash = memchr( pcFName, '\\', cbFName );
    if ( pcSlash == NULL )
      break;

    cbDir = pcSlash - pcFName;
    memcpy( pcBuf, pcFName, cbDir );
    pcBuf[cbDir] = '\0';

    // Create nonexistent directory if it isn't a first element and not one of:
    // [\], [.\], [D:\], [..\], [\\]
    if (
         (
           ( pcBuf != acBuf ) ||
           (
             ( acBuf[0] != '\0' ) &&
             ( *((PUSHORT)pcBuf) != (USHORT)0x002E     /* '\0.' */ ) &&
             ( *((PUSHORT)&pcBuf[1]) != (USHORT)0x003A /* '\0:' */ ) &&
             ( strcmp( pcBuf, ".." ) != 0 ) && ( strcmp( pcBuf, "\\\\" ) != 0 )
           )
         )
#ifdef __WATCOMC__
         && ( stat( acBuf, &stStat ) == -1 ) && ( mkdir( acBuf ) == -1 )
#else
         && ( stat( acBuf, &stStat ) == -1 ) && ( mkdir( acBuf, 0755 ) == -1 )
#endif
       )
      return FALSE;

    pcBuf[cbDir] = '\\';
    cbDir++;
    pcBuf = &pcBuf[cbDir];
    pcFName += cbDir;
    cbFName -= cbDir;
  }

  return TRUE;
}

BOOL utilPathExists(ULONG cbName, PCHAR pcName, BOOL fFile)
{
  struct stat          stStat;
  CHAR                 acBuf[_MAX_PATH];

  if ( ( pcName == NULL ) || ( cbName == 0 ) )
    return FALSE;

  strlcpy( acBuf, pcName, sizeof(acBuf) );

  return ( ( stat( acBuf, &stStat ) != -1 ) &&
           ( fFile != S_ISDIR(stStat.st_mode) ) );
}

// LONG utilSetExtension(ULONG cbBuf, PCHAR pcBuf, PSZ pszFile, PSZ pszExt)
//
// Adds or replaces an extension in the file name. Original file name should be
// given by parameter pszFile or should be placed at pcBuf when pszFile is NULL.
// The new extension should be specified by pszExt. The new name will have no
// extension if pszExt is NULL or empty string. The new name will be written
// into pcBuf.
// Returns the number of characters written into pcBuf, not counting the
// terminating null character, or -1 if more than cbBuf characters were
// requested to be generated. 

LONG utilSetExtension(ULONG cbBuf, PCHAR pcBuf, PSZ pszFile, PSZ pszExt)
{
  PCHAR      pcEnd;
  PCHAR      pcExt;
  BOOL       fFound = FALSE;
  ULONG      cbFName;
  ULONG      cbExt = pszExt == NULL ? 0 : strlen( pszExt );
  LONG       cbNewName;

  if ( pszFile == NULL )         // The original file name is not specified.
    pszFile = pcBuf;             // Get original file name from pcBuf.

  pcEnd = strchr( pszFile, '\0' );
  pcExt = pcEnd;
  while( pcExt > pszFile )
  {
    pcExt--;

    if ( *pcExt == '.' )
    {
      fFound = TRUE;
      break;
    }

    if ( *pcExt == '\\' )
      break;
  }

  // Length of the file name w/o extension.
  cbFName = fFound ? ( pcExt - pszFile ) : strlen( pszFile );

  // Length of the result.
  cbNewName = cbFName + cbExt;
  if ( cbExt != 0 )
    cbNewName++;                           // Point before extension.
  if ( cbBuf <= cbNewName )
    return -1;                             // Not enough buffer space.

  memcpy( pcBuf, pszFile, cbFName );
  pszFile = &pcBuf[cbFName];
  if ( cbExt == 0 )
    *pszFile = '\0';                       // New extension is not specified.
  else
  {
    *pszFile = '.';                        // Add a new extension with a
    strcpy( &pszFile[1], pszExt );         // leading point.
  }

  return cbNewName;
}

ULONG utilMessageBox(HWND hwnd, PSZ pszTitle, ULONG ulMsgResId, ULONG ulStyle)
{
  HAB      hab = WinQueryAnchorBlock( HWND_DESKTOP );
  CHAR     acText[256];
  CHAR     acSubstText[512];
  CHAR     acTitle[256];
  PSZ      pszText;
  PCHAR    pCh;

  if ( pszTitle == NULL )
  {
    WinQueryWindowText( hwnd, sizeof(acTitle) - 1, acTitle );
    pszTitle = acTitle;
  }
  WinLoadMessage( hab, 0, ulMsgResId, sizeof(acText), acText );

  pszText = ( hwnd != HWND_DESKTOP ) &&
            WinSubstituteStrings( hwnd, acText, sizeof(acSubstText),
                                  acSubstText ) != 0
              ? acSubstText : acText;

  pCh = pszText;
  while( (pCh = strchr( pCh, '\1' )) != NULL )
    *pCh = '\n';

  return WinMessageBox( HWND_DESKTOP, hwnd, pszText, pszTitle, 0, ulStyle );
}

ULONG utilQueryProgPath(ULONG cbBuf, PCHAR pcBuf)
{
  PTIB		pTIB;
  PPIB		pPIB;
  PCHAR		pCh;
  ULONG		ulRC;
  ULONG		cbPath;

  ulRC = DosGetInfoBlocks( &pTIB, &pPIB );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosGetInfoBlocks(), rc = %u", ulRC );
    return 0;
  }

  pCh = strrchr( pPIB->pib_pchcmd, '\\' );
  if ( pCh == NULL || pCh == pPIB->pib_pchcmd )
  {
    debug( "Cannot get path from %s, use .\\", pPIB->pib_pchcmd );
    if ( cbBuf < 3 )
      return 0;
    strcpy( pcBuf, ".\\" );
    return 2;
  }

  cbPath = pCh - pPIB->pib_pchcmd + 1;
  if ( cbBuf <= cbPath )
    return 0;

  memcpy( pcBuf, pPIB->pib_pchcmd, cbPath );
  pcBuf[cbPath] = '\0';
  return cbPath;
}

VOID utilPathOS2Slashes(ULONG cbBuf, PCHAR pcBuf)
{
  for( ; cbBuf != 0; cbBuf--, pcBuf++ )
    if ( *pcBuf == '/' )
      *pcBuf = '\\';
}


BOOL utilVerifyDomainName(ULONG cbDomain, PCHAR pcDomain)
{
  ULONG      cbPart;

  if ( ( pcDomain == NULL ) || ( cbDomain == 0 ) ||
       ( pcDomain[cbDomain - 1] == '.' ) )
    return FALSE;

  cbPart = 0;
  do
  {
    if ( ( !isalnum( *pcDomain ) && ( *pcDomain != '-' )
           && ( *pcDomain != '_' ) ) || // Sysadmins, READ RFC! Symb. '_' not
                                        // allowed! But mail.ru (for ex.) has
                                        // domain _spf.mail.ru.
         ( (++cbPart) > 63 ) )
      return FALSE;

    cbDomain--;
    pcDomain++;
    if ( *pcDomain == '.' ) 
    {
      if ( cbPart == 0 )
        return FALSE;

      cbDomain--;
      pcDomain++;
      cbPart = 0;
    }
  }
  while( cbDomain != 0 );

  return cbPart != 0;
}

// Verify string like: host.name.dom[:port]
BOOL utilVerifyHostPort(ULONG cbStr, PCHAR pcStr)
{
  PCHAR      pcColumn = memchr( pcStr, ':', cbStr );
  ULONG      ulNameLen;

  if ( pcColumn != NULL )
  {
    ulNameLen = pcColumn - pcStr;
    pcColumn++;
    if ( isspace( pcColumn[1] ) ||
         !utilStrToULong( cbStr - ulNameLen - 1, pcColumn, 0, 0xFFFF, NULL ) )
      return FALSE;
  }
  else
    ulNameLen = cbStr;

  return utilVerifyDomainName( ulNameLen, pcStr );
}

// PCHAR utilEMailDomain(ULONG cbAddr, PCHAR pcAddr, PULONG pcbDomain)
//
// Returns pointer to the domain part of the e-mail address. Optionaly, writes
// to pcbDomain lenght of the domain part (if pcbDomain is not a NULL).
// Returns NULL if malformed address given, it can be used to verify e-mail
// address.

PCHAR utilEMailDomain(ULONG cbAddr, PCHAR pcAddr, PULONG pcbDomain)
{
  PCHAR      pcAt;
  ULONG      cbDomain;

  if ( ( pcAddr == NULL ) || ( cbAddr < 5 ) )
    return NULL;

  pcAt = memchr( pcAddr, '@', cbAddr );
  if ( ( pcAt == NULL ) || ( pcAt == (pcAddr + 1) ) || ( pcAt[1] == '.' ) )
    return NULL;

  pcAt++;
  cbDomain = cbAddr - ( pcAt - pcAddr );
  if ( cbDomain < 3 )
    return NULL;

  if ( *pcAt == '[' )
  {
    CHAR             szIP[16];
    ULONG            cbIP = cbDomain - 2;

    if ( ( pcAt[cbDomain-1] != ']' ) || ( cbIP > 15 ) )
      return NULL;

    memcpy( &szIP, &pcAt[1], cbIP );
    szIP[cbIP] = '\0';
    if ( inet_addr( szIP ) == ((u_long)(-1)) )
      return NULL;
  }
  else if ( !utilVerifyDomainName( cbDomain, pcAt ) )
    return NULL;

  if ( pcbDomain != NULL )
    *pcbDomain = cbDomain;

  return pcAt;
}

// BOOL utilIsMatch(ULONG cbStr, PCHAR pcStr, ULONG cbPtrn, PCHAR pcPtrn)
//
// _isMatch() Compares the string with a pattern which may contain symbols: *?

BOOL utilIsMatch(ULONG cbStr, PCHAR pcStr, ULONG cbPtrn, PCHAR pcPtrn)
{
  PCHAR		pcStarStr = NULL;
  ULONG		cbStarStr = 0;
  PCHAR		pcStartPtrn = NULL;
  ULONG		cbStartPtrn = 0;
  CHAR		chStr, chPtrn;

  if ( cbStr == 0 && cbPtrn == 0 )
    return TRUE;

  while( cbStr != 0 )
  {
    chStr = toupper( *pcStr );

    if ( cbPtrn != 0 )
    {
      chPtrn = toupper( *pcPtrn );

      if ( chPtrn == '?' || chPtrn == chStr )
      {
        pcPtrn++;
        cbPtrn--;
        pcStr++;
        cbStr--;
        continue;
      }

      if ( chPtrn == '*' )
      {
        //skip all continuous '*'
        do
        {
          cbPtrn--;
          if ( cbPtrn == 0 )		// if end with '*', its match.
            return TRUE;
          pcPtrn++;
        }
        while( *pcPtrn == '*' );

        pcStartPtrn = pcPtrn;		// store '*' pos for string and pattern
        cbStartPtrn = cbPtrn;
        pcStarStr = pcStr;
        cbStarStr = cbStr;
        continue;
      }
    }

    if ( ( cbPtrn == 0 || chPtrn != chStr ) && ( pcStartPtrn != NULL ) )
    {
      pcStarStr++;		// Skip non-match char of string, regard it
      cbStarStr--;		// matched in '*'.
      pcStr = pcStarStr;
      cbStr = cbStarStr;

      pcPtrn = pcStartPtrn;	// Pattern backtrace to later char of '*'.
      cbPtrn = cbStartPtrn;
    }
    else
      return FALSE;
  }

  // Check if later part of ptrn are all '*'
  while( cbPtrn != 0 )
  {
    if ( *pcPtrn != '*' )
      return FALSE;

    pcPtrn++;
    cbPtrn--;
  }

  return TRUE;
}

/* BOOL utilBSearch(PVOID pKey, PVOID pBase, ULONG ulNum, ULONG cbWidth,
                 int (*fnComp)( const void *pkey, const void *pbase),
                 PULONG pulIndex)

   Alternative function for OpenWatcom C Library bsearch().
   Returns TRUE and index of the object at pulIndex if a matching object found.
   Returns FALSE and index where object pKey must be placed at pulIndex if a
   matching object could not be found. 
*/

BOOL utilBSearch(const void *pKey, PVOID pBase, ULONG ulNum, ULONG cbWidth,
                 int (*fnComp)(const void *pkey, const void *pbase),
                 PULONG pulIndex)
{
  PCHAR		pcList = (PCHAR)pBase;
  LONG		lHi, lLo, lMed;
  LONG		lComp;

  if ( ulNum == 0 )
  {
    *pulIndex = 0;
    return FALSE;
  }

  lComp = fnComp( pKey, &pcList[ ( ulNum - 1 ) * cbWidth ] );

  if ( lComp > 0 )
  {
    *pulIndex = ulNum;
    return FALSE;
  }

  if ( lComp == 0 )
  {
    *pulIndex = ulNum - 1;
    return TRUE;
  }

  lComp = fnComp( pKey, pcList );

  if ( lComp <= 0 )
  {
    *pulIndex = 0;
    return lComp == 0;
  }

  lHi = ulNum - 1;
  lLo = 0;

  while( ( lHi - lLo ) > 1 )
  {
    lMed = ( lLo + lHi ) / 2;
    lComp = fnComp( pKey, &pcList[ lMed * cbWidth ] );

    if ( lComp > 0 )
    {
      lLo = lMed;
    }
    else if ( lComp < 0 )
    {
      lHi = lMed;
    }
    else
    {
      *pulIndex = lMed;
      return TRUE;
    }
  }

  *pulIndex = lHi;
  return FALSE;
}


// System INI API utilites.
// ------------------------

BOOL utilINIWriteULong(HINI hIni, PSZ pszApp, PSZ pszKey, ULONG ulData)
{
  CHAR       acData[32];

  ultoa( ulData, acData, 10 );
  if ( !PrfWriteProfileString( hIni, pszApp, pszKey, acData ) )
  {
    debug( "Value not stored: app: %s, key: %s, value: %u",
           pszApp, pszKey, ulData );
    return FALSE;
  }

  return TRUE;
}

ULONG utilINIQueryULong(HINI hIni, PSZ pszApp, PSZ pszKey, ULONG ulDefault)
{
  CHAR       acData[32];
  ULONG      cbData;

  cbData = PrfQueryProfileString( hIni, pszApp, pszKey, NULL, acData,
                                  sizeof(acData) );
  utilStrToULong( cbData - 1, acData, 0, ~0, &ulDefault );

  return ulDefault;
}

BOOL utilINIWriteLong(HINI hIni, PSZ pszApp, PSZ pszKey, LONG lData)
{
  CHAR       acData[32];

  ltoa( lData, acData, 10 );
  if ( !PrfWriteProfileString( hIni, pszApp, pszKey, acData ) )
  {
    debug( "Value not stored: app: %s, key: %s, value: %d",
           pszApp, pszKey, lData );
    return FALSE;
  }

  return TRUE;
}

LONG utilINIQueryLong(HINI hIni, PSZ pszApp, PSZ pszKey, LONG lDefault)
{
  CHAR       acData[32];
  ULONG      cbData;

  cbData = PrfQueryProfileString( hIni, pszApp, pszKey, NULL, acData,
                                  sizeof(acData) );
  utilStrToLong( cbData - 1, acData, LONG_MIN, LONG_MAX, &lDefault );

  return lDefault;
}

// utilINIWriteWinPresParam(HWND hwnd, HINI hIni, PSZ pszApp)
// utilINIQueryWinPresParam(HWND hwnd, HINI hIni, PSZ pszApp)
//   Write/query window presentation paramethers.

typedef struct _INIWPP {
  HWND       hwnd;
  HINI       hIni;
  PSZ        pszApp;
  CHAR       acKey[1024];
  CHAR       acBuf[256];
} INIWPP, *PINIWPP;

#define INIPP_TYPE_STRING        0
#define INIPP_TYPE_RGB           1

static struct {
  ULONG      ulPresParam;
  PSZ        pszName;
  ULONG      ulType;
} aINIPresParam[] =
{
  { PP_FONTNAMESIZE,             "FONT",        INIPP_TYPE_STRING },
  { PP_FOREGROUNDCOLOR,          "FORECOL",     INIPP_TYPE_RGB    },
  { PP_BACKGROUNDCOLOR,          "BACKCOL",     INIPP_TYPE_RGB    },
  { PP_HILITEFOREGROUNDCOLOR,    "HILFORECOL",  INIPP_TYPE_RGB    },
  { PP_HILITEBACKGROUNDCOLOR,    "HILBACKCOL",  INIPP_TYPE_RGB    },
  { PP_DISABLEDFOREGROUNDCOLOR,  "DISFORECOL",  INIPP_TYPE_RGB    },
  { PP_DISABLEDBACKGROUNDCOLOR,  "DISBACKCOL",  INIPP_TYPE_RGB    },
  { PP_BORDERCOLOR,              "BORDERCOL",   INIPP_TYPE_RGB    },
  { PP_ACTIVECOLOR,              "ACTIVECOL",   INIPP_TYPE_RGB    },
  { PP_INACTIVECOLOR,            "INACTIVECOL", INIPP_TYPE_RGB    }
};

static VOID _iniWriteWinPresParam(PINIWPP pINIWPP, PSZ pszKeyBase)
{
  HENUM                henum;
  ULONG                ulWinId;
  PCHAR                pszKey;
  ULONG                ulIdx;

  ulWinId = WinQueryWindowUShort( pINIWPP->hwnd, QWS_ID );

  pszKey = pszKeyBase + sprintf( pszKeyBase, "#%u", ulWinId );

  for( ulIdx = 0; ulIdx < ARRAYSIZE(aINIPresParam); ulIdx++ )
  {
    if ( WinQueryPresParam( pINIWPP->hwnd, aINIPresParam[ulIdx].ulPresParam,
                            0, NULL, sizeof(pINIWPP->acBuf), &pINIWPP->acBuf,
                            QPF_NOINHERIT ) != 0 )
    {
      sprintf( pszKey, "_%s", aINIPresParam[ulIdx].pszName );

      switch( aINIPresParam[ulIdx].ulType )
      {
        case INIPP_TYPE_STRING:
          PrfWriteProfileString( pINIWPP->hIni, pINIWPP->pszApp,
                                 pINIWPP->acKey, pINIWPP->acBuf );
          break;

        case INIPP_TYPE_RGB:
          utilINIWriteULong( pINIWPP->hIni, pINIWPP->pszApp,
                             pINIWPP->acKey, *((PULONG)&pINIWPP->acBuf[0]) );
          break;
      }
    }
  }

  henum = WinBeginEnumWindows( pINIWPP->hwnd );
  while( ( pINIWPP->hwnd = WinGetNextWindow( henum ) ) != NULLHANDLE )
    _iniWriteWinPresParam( pINIWPP, pszKey );
  WinEndEnumWindows( henum );
}

static VOID _iniQueryWinPresParam(PINIWPP pINIWPP, PSZ pszKeyBase)
{
  HENUM                henum;
  ULONG                ulWinId;
  PCHAR                pszKey;
  ULONG                ulIdx;

  ulWinId = WinQueryWindowUShort( pINIWPP->hwnd, QWS_ID );

  pszKey = pszKeyBase + sprintf( pszKeyBase, "#%u", ulWinId );

  for( ulIdx = 0; ulIdx < ARRAYSIZE(aINIPresParam); ulIdx++ )
  {
    sprintf( pszKey, "_%s", aINIPresParam[ulIdx].pszName );

    switch( aINIPresParam[ulIdx].ulType )
    {
      case INIPP_TYPE_STRING:
        if ( PrfQueryProfileString( pINIWPP->hIni, pINIWPP->pszApp,
                                    pINIWPP->acKey, NULL, pINIWPP->acBuf,
                                    sizeof(pINIWPP->acBuf) ) != 0 )
          WinSetPresParam( pINIWPP->hwnd, aINIPresParam[ulIdx].ulPresParam,
                           strlen( pINIWPP->acBuf ) + 1, pINIWPP->acBuf );
        break;

      case INIPP_TYPE_RGB:
        *((PULONG)&pINIWPP->acBuf[0]) = utilINIQueryULong( pINIWPP->hIni,
                                                           pINIWPP->pszApp,
                                                           pINIWPP->acKey, ~0 );
        if ( *((PULONG)&pINIWPP->acBuf[0]) != ~0 ) 
          WinSetPresParam( pINIWPP->hwnd, aINIPresParam[ulIdx].ulPresParam,
                           sizeof(RGB2), (PULONG)&pINIWPP->acBuf[0] );
        break;
    }
  }

  henum = WinBeginEnumWindows( pINIWPP->hwnd );
  while( ( pINIWPP->hwnd = WinGetNextWindow( henum ) ) != NULLHANDLE )
    _iniQueryWinPresParam( pINIWPP, pszKey );
  WinEndEnumWindows( henum );
}

VOID utilINIWriteWinPresParam(HWND hwnd, HINI hIni, PSZ pszApp)
{
  INIWPP    stINIWPP;

  stINIWPP.hwnd = hwnd;
  stINIWPP.hIni = hIni;
  stINIWPP.pszApp = pszApp;
  stINIWPP.acKey[0] = '\0';
  _iniWriteWinPresParam( &stINIWPP, stINIWPP.acKey );
}

VOID utilINIQueryWinPresParam(HWND hwnd, HINI hIni, PSZ pszApp)
{
  INIWPP    stINIWPP;

  stINIWPP.hwnd = hwnd;
  stINIWPP.hIni = hIni;
  stINIWPP.pszApp = pszApp;
  stINIWPP.acKey[0] = '\0';
  _iniQueryWinPresParam( &stINIWPP, stINIWPP.acKey );
}

#ifdef UTILS_WITH_OPENSSL

BOOL utilINIWritePassword(HINI hIni, PSZ pszApp, PSZ pszKey, PSZ pszPassword)
{
  PCHAR      pcEncBuf;
  ULONG      cbEncBuf;
  BOOL       fSuccess;

  utilB64Enc( pszPassword, strlen( pszPassword ), &pcEncBuf, &cbEncBuf );

  fSuccess = PrfWriteProfileData( hIni, pszApp, pszKey, pcEncBuf, cbEncBuf );
  free( pcEncBuf );

  return fSuccess;
}

ULONG utilINIQueryPassword(HINI hIni, PSZ pszApp, PSZ pszKey,
                           ULONG cbBuf, PCHAR pcBuf)
{
  PCHAR      pcEncBuf, pcDecBuf;
  ULONG      cbEncBuf, cbDecBuf;

  if ( !PrfQueryProfileSize( hIni, pszApp, pszKey, &cbEncBuf ) )
    return 0;

  pcEncBuf = malloc( cbEncBuf );
  if ( pcEncBuf == NULL )
    return 0;

  if ( !PrfQueryProfileData( hIni, pszApp, pszKey, pcEncBuf, &cbEncBuf ) )
  {
    free( pcEncBuf );
    return 0;
  }

  utilB64Dec( pcEncBuf, cbEncBuf, &pcDecBuf, &cbDecBuf );
  free( pcEncBuf );

  if ( pcDecBuf == NULL )
    return 0;

  strlcpy( pcBuf, pcDecBuf, cbBuf );
  free( pcDecBuf );

  return cbDecBuf;
}


// Base64
// ------

VOID utilB64Enc(PCHAR pcData, ULONG cbData, PCHAR *ppcBuf, PULONG pcbBuf)
{
  BIO        *bioBuf, *bioB64f;
  BUF_MEM    *pMem;

  bioB64f = BIO_new( BIO_f_base64() );
  bioBuf  = BIO_new( BIO_s_mem() );
  bioBuf  = BIO_push( bioB64f, bioBuf );

  BIO_set_flags( bioBuf, BIO_FLAGS_BASE64_NO_NL );
  BIO_set_close( bioBuf, BIO_CLOSE );
  BIO_write( bioBuf, pcData, cbData );
  BIO_flush( bioBuf );

  BIO_get_mem_ptr( bioBuf, &pMem );
  *pcbBuf = pMem->length;
  *ppcBuf = malloc( *pcbBuf + 1 );
  memcpy( *ppcBuf, pMem->data, *pcbBuf );
  (*ppcBuf)[ *pcbBuf ] = '\0';

  BIO_free_all( bioBuf );
}

VOID utilB64Dec(PCHAR pcData, ULONG cbData, PCHAR *ppcBuf, PULONG pcbBuf)
{
  BIO        *bioBuf, *bioB64f;

  bioB64f = BIO_new( BIO_f_base64() );
  bioBuf = BIO_new_mem_buf( (void *)pcData, cbData );
  bioBuf = BIO_push( bioB64f, bioBuf );
  *ppcBuf = malloc( cbData );

  BIO_set_flags( bioBuf, BIO_FLAGS_BASE64_NO_NL );
  BIO_set_close( bioBuf, BIO_CLOSE );
  *pcbBuf = BIO_read( bioBuf, *ppcBuf, cbData );
  *ppcBuf = realloc( *ppcBuf, (*pcbBuf) + 1 );
  (*ppcBuf)[ *pcbBuf ] = '\0';

  BIO_free_all( bioBuf );
}

#endif // UTILS_WITH_OPENSSL
