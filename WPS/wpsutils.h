#ifndef WPSUTILS_H
#define WPSUTILS_H

HMODULE wpsutilGetModuleHandle();

BOOL wpsutilLoadStrNew(PVOID somSelf, PSZ pszClass, ULONG ulKey,
                       PSZ *ppszValue);
BOOL wpsutilSetupStrNew(PVOID somSelf, PSZ pszSetupString, PSZ pszKey,
                        PSZ *ppszValue);
BOOL wpsutilSetupReadBool(PVOID somSelf, PSZ pszSetupString, PSZ pszKey,
                          PBOOL pfValue);
BOOL wpsutilSetupReadULong(PVOID somSelf, PSZ pszSetupString, PSZ pszKey,
                           PULONG pulValue);

/*
   PVOID wpsutilIconFromBitmap(HBITMAP hBmp, PULONG pulSize)

   Creates an icon from the bitmap. Result is a pointer to icon data - like
   ICO-file content in memory. Pointer should be destroyed with free().
   Returns NULL on error.
*/
PVOID wpsutilIconFromBitmap(HBITMAP hBmp, PULONG pulSize);

#endif // WPSUTILS_H
