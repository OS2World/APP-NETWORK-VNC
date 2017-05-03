#ifndef CSCONV_H
#define CSCONV_H

#include <rfb/rfbclient.h>
#include <iconv.h>

#define CSC_LOCALTOREMOTE        0
#define CSC_REMOTETOLOCAL        1

#define cscFree(psz) free(psz)

// Wraper for iconv().
// Returns a new ASCIIZ string (should be destroyed by cscFree()) or NULL.
PSZ cscIConv(iconv_t ic, ULONG cbStrIn, PCHAR pcStrIn);

// Creates conversion object for the client.
// pszRemoteCS - charset name (like "WINDOWS-1251").
BOOL cscInit(rfbClient* client, PSZ pszRemoteCS);

// Destroys the conversion object.
VOID cscDone(rfbClient* client);

// Converts the string from/to remote charset.
// ulDirection - CSC_LOCALTOREMOTE or CSC_REMOTETOLOCAL.
// Returns a new ASCIIZ string (should be destroyed by cscFree()) or NULL on
// error.
PSZ cscConv(rfbClient* client, ULONG ulDirection, ULONG cbStr, PCHAR pcStr);
PSZ cscConvStr(rfbClient* client, ULONG ulDirection, PSZ pszStr);

#endif // CSCONV_H
