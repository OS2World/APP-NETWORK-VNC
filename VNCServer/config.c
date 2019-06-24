#include <string.h>
#include <stdlib.h>
#include <arpa\inet.h>
#include <ctype.h>
#define INCL_WINSHELLDATA
#include <os2.h>
#include <netinet/in.h>
#define UTIL_INET_ADDR
#include "utils.h"
#include "config.h"
#include <debug.h>

#define _INI_FILE      "vncserver.ini"
#define _INISEC_MAIN   "Server"
#define _INISEC_ACL    "ACL"
#define _INISEC_GUI    "GUI"
#define _INISEC_LOG    "Log"
#define _INISEC_PROG   "Programs"
#define _INISEC_KBD    "Keyboard"

HINI iniOpen(HAB hab)
{
  CHAR       acBuf[CCHMAXPATHCOMP];
  ULONG      ulRC = utilQueryProgPath( sizeof(acBuf), acBuf );
  HINI       hIni;

  strcpy( &acBuf[ulRC], _INI_FILE );

  hIni = PrfOpenProfile( hab, acBuf );
  if ( hIni == NULLHANDLE )
    debug( "PrfOpenProfile(,\"%s\") failed", acBuf );

  return hIni;
}

VOID iniClose(HINI hIni)
{
  PrfCloseProfile( hIni );
}

PCONFIG cfgGetDefault()
{
  PCONFIG    pConfig = malloc( sizeof(CONFIG) );

  if ( pConfig == NULL )
    return NULL;

  pConfig->hIni = NULLHANDLE;

  pConfig->ulPort = 5900;
  pConfig->ulHTTPPort = 5900 - 100;
  pConfig->inaddrListen = INADDR_ANY;
  pConfig->acSSLKeyFile[0] = '\0';
  pConfig->acSSLCertFile[0] ='\0';
  pConfig->ulDeferUpdateTime = 5;
  pConfig->ulDeferPtrUpdateTime = 0;
  pConfig->fAlwaysShared = FALSE;
  pConfig->fNeverShared = FALSE;
  pConfig->fDontDisconnect = FALSE;
  pConfig->fFileTransfer = TRUE;
  pConfig->fUltraVNCSupport = FALSE;
  pConfig->fHTTPProxyConnect = TRUE;
  pConfig->ulProgressiveSliceHeight = 0;
  pConfig->fPrimaryPassword = TRUE;
  strcpy( pConfig->acPrimaryPassword, "vncprim" );
  pConfig->fViewOnlyPassword = FALSE;
  strcpy( pConfig->acViewOnlyPassword, "vncview" );

  aclInit( &pConfig->stACL );

  pConfig->fGUIVisible = TRUE;
  pConfig->lGUIx = LONG_MIN;
  pConfig->lGUIy = LONG_MIN;

  pConfig->stLogData.ulLevel   = 1;
  pConfig->stLogData.ulMaxSize = 2 * 1024;
  pConfig->stLogData.ulFiles   = 0;
  strcpy( pConfig->stLogData.acFile, "vnc.log" );

  pConfig->fProgOnLogon        = FALSE;
  pConfig->acProgOnLogon[0]    = '\0';
  pConfig->fProgOnGone         = FALSE;
  pConfig->acProgOnGone[0]     = '\0';
  pConfig->fProgOnCAD          = FALSE;
  strcpy( pConfig->acProgOnCAD, "setboot.exe /b" );

  pConfig->fUseDriverVNCKBD    = TRUE;
  pConfig->fUseDriverKBD       = TRUE;

  return pConfig;
}

PCONFIG cfgGet(HAB hab)
{
  PCONFIG    pConfig = cfgGetDefault();
  CHAR       acBuf[CCHMAXPATHCOMP];
  ULONG      ulRC;
  HINI       hIni;

  if ( pConfig == NULL )
    return NULL;

  hIni = iniOpen( hab );
  if ( hIni == NULLHANDLE )
    return pConfig;
  pConfig->hIni = hIni;

  pConfig->ulPort =
    utilINIQueryULong( hIni, _INISEC_MAIN, "Port", pConfig->ulPort );

  pConfig->ulHTTPPort =
    utilINIQueryULong( hIni, _INISEC_MAIN, "HTTPPort", pConfig->ulHTTPPort );

  PrfQueryProfileString( hIni, _INISEC_MAIN, "Interface", "0.0.0.0",
                         acBuf, sizeof(acBuf) );
  pConfig->inaddrListen = inet_addr( acBuf );

  PrfQueryProfileString( hIni, _INISEC_MAIN, "Certificate", "",
                         pConfig->acSSLCertFile, sizeof(pConfig->acSSLCertFile) );
  PrfQueryProfileString( hIni, _INISEC_MAIN, "PrivateKey", "",
                         pConfig->acSSLKeyFile, sizeof(pConfig->acSSLKeyFile) );

  pConfig->ulDeferUpdateTime =
    utilINIQueryULong( hIni, _INISEC_MAIN, "DeferUpdateTime",
                       pConfig->ulDeferUpdateTime );

  pConfig->ulDeferPtrUpdateTime =
    utilINIQueryULong( hIni, _INISEC_MAIN, "DeferPtrUpdateTime",
                       pConfig->ulDeferPtrUpdateTime );

  pConfig->fAlwaysShared =
    utilINIQueryULong( hIni, _INISEC_MAIN, "AlwaysShared",
                       pConfig->fAlwaysShared ) != 0;

  pConfig->fNeverShared =
    utilINIQueryULong( hIni, _INISEC_MAIN, "NeverShared",
                       pConfig->fNeverShared ) != 0;

  pConfig->fDontDisconnect =
    utilINIQueryULong( hIni, _INISEC_MAIN, "DontDisconnect",
                       pConfig->fDontDisconnect ) != 0;

  pConfig->fFileTransfer =
    utilINIQueryULong( hIni, _INISEC_MAIN, "FileTransfer",
                       pConfig->fFileTransfer ) != 0;

  pConfig->fUltraVNCSupport =
    utilINIQueryULong( hIni, _INISEC_MAIN, "UltraVNCSupport",
                       pConfig->fUltraVNCSupport ) != 0;

  pConfig->fHTTPProxyConnect =
    utilINIQueryULong( hIni, _INISEC_MAIN, "HTTPProxyConnect",
                       pConfig->fHTTPProxyConnect );

  pConfig->ulProgressiveSliceHeight =
    utilINIQueryULong( hIni, _INISEC_MAIN, "ProgressiveSliceHeight",
                       pConfig->ulProgressiveSliceHeight );

  pConfig->fPrimaryPassword =
    utilINIQueryULong( hIni, _INISEC_MAIN, "UsePrimaryPassword",
                       pConfig->fPrimaryPassword );

  utilINIQueryPassword( hIni, _INISEC_MAIN, "PrimaryPassword",
                        sizeof(pConfig->acPrimaryPassword),
                        pConfig->acPrimaryPassword );

  pConfig->fViewOnlyPassword =
    utilINIQueryULong( hIni, _INISEC_MAIN, "UseViewOnlyPassword",
                       pConfig->fViewOnlyPassword ) != 0;

  utilINIQueryPassword( hIni, _INISEC_MAIN, "ViewOnlyPassword",
                        sizeof(pConfig->acViewOnlyPassword),
                        pConfig->acViewOnlyPassword );

  // Load ACL.

  // Clear current ACL.
  aclFree( &pConfig->stACL );
  aclInit( &pConfig->stACL );

       // Query length of all names list in ACL section of the INI-file.
  if ( PrfQueryProfileSize( hIni, _INISEC_ACL, NULL, &ulRC ) && ( ulRC != 0 ) )
  {
    PCHAR    pcList = malloc( ulRC );  // List of all names in _INISEC_ACL.

    if ( pcList != NULL )
    {
      // Query all names in ACL section of the INI-file. Each name end with \0,
      // last name ends with \0\0.
      if ( PrfQueryProfileData( hIni, _INISEC_ACL, NULL, pcList, &ulRC ) )
      {
        ACLITEM  stItem;
        PCHAR    pcName;

        // Read ACL records (by list of names) from the INI-file.
        for( pcName = pcList; *pcName != '\0'; pcName = strchr(pcName,'\0')+1 )
        {
          ulRC = PrfQueryProfileString( hIni, _INISEC_ACL, pcName, NULL,
                                        acBuf, sizeof(acBuf) );
          if ( ulRC != 0 )
          {
            if ( !aclStrToItem( ulRC - 1, acBuf, &stItem ) )
              debug( "Invalid ACL item %s: %s", pcName, acBuf );
            else
              aclInsert( &pConfig->stACL, ~0, &stItem );
          }
        }
      }

      free( pcList );
    } // if ( pcItemNames != NULL )
  }

  // Section "GUI"

  pConfig->fGUIVisible =
    utilINIQueryULong( hIni, _INISEC_GUI, "Visible", pConfig->fGUIVisible ) != 0;

  pConfig->lGUIx =
    utilINIQueryLong( hIni, _INISEC_GUI, "PositionX", pConfig->lGUIx );

  pConfig->lGUIy =
    utilINIQueryLong( hIni, _INISEC_GUI, "PositionY", pConfig->lGUIy );

  // Section "Log"

  pConfig->stLogData.ulLevel =
    utilINIQueryLong( hIni, _INISEC_LOG, "Level", pConfig->stLogData.ulLevel );
  pConfig->stLogData.ulMaxSize =
    utilINIQueryLong( hIni, _INISEC_LOG, "MaxSize",
                      pConfig->stLogData.ulMaxSize );
  pConfig->stLogData.ulFiles =
    utilINIQueryLong( hIni, _INISEC_LOG, "Files", pConfig->stLogData.ulFiles );
  PrfQueryProfileString( hIni, _INISEC_LOG, "File", pConfig->stLogData.acFile,
                         pConfig->stLogData.acFile,
                         sizeof(pConfig->stLogData.acFile) );

  // Section "Programs"

  pConfig->fProgOnLogon =
    utilINIQueryULong( hIni, _INISEC_PROG, "OnLogonEnable",
                       pConfig->fProgOnLogon ) != 0;
  PrfQueryProfileString( hIni, _INISEC_PROG, "OnLogon", NULL,
                         pConfig->acProgOnLogon,
                         sizeof(pConfig->acProgOnLogon) );

  pConfig->fProgOnGone =
    utilINIQueryULong( hIni, _INISEC_PROG, "OnGoneEnable",
                       pConfig->fProgOnGone ) != 0;
  PrfQueryProfileString( hIni, _INISEC_PROG, "OnGone", NULL,
                         pConfig->acProgOnGone, sizeof(pConfig->acProgOnGone) );

  pConfig->fProgOnCAD =
    utilINIQueryULong( hIni, _INISEC_PROG, "OnCADEnable",
                       pConfig->fProgOnCAD ) != 0;
  PrfQueryProfileString( hIni, _INISEC_PROG, "OnCAD", NULL,
                         pConfig->acProgOnCAD, sizeof(pConfig->acProgOnCAD) );

  // Section "Keyboard"

  pConfig->fUseDriverVNCKBD =
    utilINIQueryULong( hIni, _INISEC_KBD, "UseDriverVNCKBD",
                       pConfig->fUseDriverVNCKBD ) != 0;
  pConfig->fUseDriverKBD =
    utilINIQueryULong( hIni, _INISEC_KBD, "UseDriverKBD",
                       pConfig->fUseDriverKBD ) != 0;

  return pConfig;
}

VOID cfgStore(PCONFIG pConfig)
{
  HINI       hIni = pConfig->hIni;
  ULONG      ulIdx;
  CHAR       acBuf[128];
  CHAR       acKey[8];

  if ( hIni == NULLHANDLE )
  {
    debug( "INI file is not open" );
    return;
  }

  utilINIWriteULong( hIni, _INISEC_MAIN, "Port", pConfig->ulPort );
  utilINIWriteULong( hIni, _INISEC_MAIN, "HTTPPort", pConfig->ulHTTPPort );
  PrfWriteProfileString( hIni, _INISEC_MAIN, "Interface",
                     inet_ntoa( *((struct in_addr *)&pConfig->inaddrListen) ) );
  PrfWriteProfileString( hIni, _INISEC_MAIN, "Certificate",
                         pConfig->acSSLCertFile );
  PrfWriteProfileString( hIni, _INISEC_MAIN, "PrivateKey",
                         pConfig->acSSLKeyFile );
  utilINIWriteULong( hIni, _INISEC_MAIN, "DeferUpdateTime",
                     pConfig->ulDeferUpdateTime );
  utilINIWriteULong( hIni, _INISEC_MAIN, "DeferPtrUpdateTime",
                     pConfig->ulDeferPtrUpdateTime );
  utilINIWriteULong( hIni, _INISEC_MAIN, "AlwaysShared",
                     pConfig->fAlwaysShared ? 1 : 0 );
  utilINIWriteULong( hIni, _INISEC_MAIN, "NeverShared",
                     pConfig->fNeverShared ? 1 : 0 );
  utilINIWriteULong( hIni, _INISEC_MAIN, "DontDisconnect",
                     pConfig->fDontDisconnect ? 1 : 0 );
  utilINIWriteULong( hIni, _INISEC_MAIN, "FileTransfer",
                     pConfig->fFileTransfer ? 1 : 0 );
  utilINIWriteULong( hIni, _INISEC_MAIN, "UltraVNCSupport",
                     pConfig->fUltraVNCSupport ? 1 : 0 );
  utilINIWriteULong( hIni, _INISEC_MAIN, "HTTPProxyConnect",
                     pConfig->fHTTPProxyConnect ? 1 : 0 );
  utilINIWriteULong( hIni, _INISEC_MAIN, "ProgressiveSliceHeight",
                     pConfig->ulProgressiveSliceHeight );
  utilINIWriteULong( hIni, _INISEC_MAIN, "UsePrimaryPassword",
                     pConfig->fPrimaryPassword ? 1 : 0 );
  utilINIWritePassword( hIni, _INISEC_MAIN, "PrimaryPassword",
                     pConfig->acPrimaryPassword );
  utilINIWriteULong( hIni, _INISEC_MAIN, "UseViewOnlyPassword",
                     pConfig->fViewOnlyPassword ? 1 : 0 );
  utilINIWritePassword( hIni, _INISEC_MAIN, "ViewOnlyPassword",
                     pConfig->acViewOnlyPassword );

  // Remove all ARL records from the INI-file.
  PrfWriteProfileData( hIni, _INISEC_ACL, NULL, NULL, 0 );
  // Store new ACL.
  for( ulIdx = 0; ulIdx < aclCount( &pConfig->stACL ); ulIdx++ )
  {
    if ( aclItemToStr( &pConfig->stACL, ulIdx, sizeof(acBuf), acBuf ) == -1 )
      continue;

    sprintf( acKey, "R%.4X", ulIdx ); // Record name (key). It not realy uses.
    PrfWriteProfileString( hIni, _INISEC_ACL, acKey, acBuf );
  }

  // Section "GUI"

  utilINIWriteULong( hIni, _INISEC_GUI, "Visible", pConfig->fGUIVisible ? 1:0 );
  utilINIWriteLong( hIni, _INISEC_GUI, "PositionX", pConfig->lGUIx );
  utilINIWriteLong( hIni, _INISEC_GUI, "PositionY", pConfig->lGUIy );

  // Section "Log"

  utilINIWriteULong( hIni, _INISEC_LOG, "Level", pConfig->stLogData.ulLevel );
  utilINIWriteULong( hIni, _INISEC_LOG, "MaxSize",
                     pConfig->stLogData.ulMaxSize );
  utilINIWriteULong( hIni, _INISEC_LOG, "Files", pConfig->stLogData.ulFiles );
  PrfWriteProfileString( hIni, _INISEC_LOG, "File", pConfig->stLogData.acFile );

  // Section "Programs"

  utilINIWriteULong( hIni, _INISEC_PROG, "OnLogonEnable",
                     pConfig->fProgOnLogon ? 1:0 );
  PrfWriteProfileString( hIni, _INISEC_PROG, "OnLogon",
                         pConfig->acProgOnLogon );
  utilINIWriteULong( hIni, _INISEC_PROG, "OnGoneEnable",
                     pConfig->fProgOnGone ? 1:0 );
  PrfWriteProfileString( hIni, _INISEC_PROG, "OnGone", pConfig->acProgOnGone );
  utilINIWriteULong( hIni, _INISEC_PROG, "OnCADEnable",
                     pConfig->fProgOnCAD ? 1:0 );
  PrfWriteProfileString( hIni, _INISEC_PROG, "OnCAD", pConfig->acProgOnCAD );

  // Section "Keyboard"

  utilINIWriteULong( hIni, _INISEC_KBD, "UseDriverVNCKBD",
                     pConfig->fUseDriverVNCKBD ? 1:0 );
  utilINIWriteULong( hIni, _INISEC_KBD, "UseDriverKBD",
                     pConfig->fUseDriverKBD ? 1:0 );
}

VOID cfgFree(PCONFIG pConfig)
{
  if ( pConfig == NULL )
    debug( "Argument is NULL" );
  else
  {
    if ( pConfig->hIni != NULLHANDLE )
      PrfCloseProfile( pConfig->hIni );
    
    aclFree( &pConfig->stACL );
    free( pConfig );
  }
}

VOID cfgSaveGUIData(HAB hab, LONG lX, LONG lY, BOOL fVisible)
{
  HINI       hIni = iniOpen( hab );

  if ( hIni == NULLHANDLE )
    return;

  utilINIWriteULong( hIni, _INISEC_GUI, "Visible", fVisible );
  utilINIWriteLong( hIni, _INISEC_GUI, "PositionX", lX );
  utilINIWriteLong( hIni, _INISEC_GUI, "PositionY", lY );
  PrfCloseProfile( hIni );
}


// ACL
// ---

BOOL aclInsert(PACL pACL, ULONG ulIndex, PACLITEM pItem)
{
  if ( ulIndex > pACL->ulCount )
    ulIndex = pACL->ulCount;

  if ( (pACL->ulCount & 0x0F) == 0 )
  {
    PACLITEM  paItems = realloc( pACL->paItems,
                                 (pACL->ulCount + 0x10) * sizeof(ACLITEM) );

    if ( paItems == NULL )
    {
      debug( "Not enough memory" );
      return FALSE;
    }

    pACL->paItems = paItems;
  }

  memmove( &pACL->paItems[ulIndex + 1], &pACL->paItems[ulIndex],
           (pACL->ulCount - ulIndex) * sizeof(ACLITEM) );
  pACL->paItems[ulIndex] = *pItem;
  pACL->ulCount++;

  return TRUE;
}

BOOL aclRemove(PACL pACL, ULONG ulIndex)
{
  if ( ulIndex > pACL->ulCount )
    return FALSE;

  if ( (pACL->ulCount & 0x01) == 0 )
  {
    PACLITEM  paItems = realloc( pACL->paItems,
                                 (pACL->ulCount - 0x01) * sizeof(ACLITEM) );

    if ( paItems == NULL )
    {
      debug( "Not enough memory" ); // ?!  -=8-( )
      return FALSE;
    }

    pACL->paItems = paItems;
  }

  pACL->ulCount--;
  memcpy( &pACL->paItems[ulIndex], &pACL->paItems[ulIndex + 1],
          (pACL->ulCount - ulIndex) * sizeof(ACLITEM) );

  return TRUE;
}

BOOL aclMove(PACL pACL, ULONG ulIndex, BOOL fForward)
{
  ACLITEM    stItem;
  ULONG      ulNewIndex;

  stItem = pACL->paItems[ulIndex];
  ulNewIndex = ulIndex + ( fForward ? 1 : -1 );
  if ( ulNewIndex >= pACL->ulCount )
    return FALSE;

  pACL->paItems[ulIndex] = pACL->paItems[ulNewIndex];
  pACL->paItems[ulNewIndex] = stItem;
  return TRUE;
}

LONG aclItemToStr(PACL pACL, ULONG ulIndex, ULONG cbBuf, PCHAR pcBuf)
{
  PACLITEM   pItem;
  CHAR       acAddr[32];
  LONG       cBytes;

  if ( ulIndex >= pACL->ulCount )
    return -1;

  pItem = &pACL->paItems[ulIndex];
  
  if ( !utilInAddrRangeToStr( &pItem->stInAddr1, &pItem->stInAddr2,
                              sizeof(acAddr), acAddr ) )
    return -1;

  cBytes = _snprintf( pcBuf, cbBuf, "%s %s %s %s",
                      pItem->fEnable ? "enable" : "disable", acAddr,
                      pItem->fAllow ? "allow" : "deny", pItem->acComment );
  if ( cBytes > 0 )
  {
    while( isspace( pcBuf[cBytes-1] ) )
      cBytes--;
    pcBuf[cBytes] = '\0';
  }

  return cBytes;
}

BOOL aclStrToItem(ULONG cbStr, PCHAR pcStr, PACLITEM pItem)
{
  ULONG      cbWord;
  PCHAR      pcWord;
  LONG       lIdx;

  if ( !utilStrCutWord( &cbStr, &pcStr, &cbWord, &pcWord ) )
    return FALSE;

  lIdx = utilStrWordIndex( "enable disable", cbWord, pcWord );
  if ( lIdx == -1 )
    return FALSE;

  pItem->fEnable = lIdx == 0;

  if ( !utilStrCutWord( &cbStr, &pcStr, &cbWord, &pcWord ) ||
       !utilStrToInAddrRange( cbWord, pcWord,
                              &pItem->stInAddr1, &pItem->stInAddr2 ) )
    return FALSE;

  if ( !utilStrCutWord( &cbStr, &pcStr, &cbWord, &pcWord ) )
    return FALSE;

  lIdx = utilStrWordIndex( "allow deny", cbWord, pcWord );
  if ( lIdx == -1 )
    return FALSE;

  pItem->fAllow = lIdx == 0;

  if ( cbStr >= sizeof(pItem->acComment) )
    cbStr = sizeof(pItem->acComment) - 1;

  memcpy( pItem->acComment, pcStr, cbStr );
  pItem->acComment[cbStr] = '\0';

  return TRUE;
}

ULONG aclCheck(PACL pACL, struct in_addr *pInAddr)
{
  ULONG      ulIdx;
  PACLITEM   pItem;
  ULONG      ulAddr;

  if ( pACL->ulCount == 0 )
    return ACL_EMPTY;

  ulAddr = ntohl( pInAddr->s_addr );
  for( ulIdx = 0, pItem = pACL->paItems; ulIdx < pACL->ulCount;
       ulIdx++, pItem++ )
  {
    if ( pItem->fEnable && ( ulAddr >= ntohl( pItem->stInAddr1.s_addr ) ) &&
         ( ulAddr <= ntohl( pItem->stInAddr2.s_addr ) ) )
      return pItem->fAllow ? ACL_ALLOW : ACL_DENY;
  }

  return ACL_NOT_FOUND;
}
