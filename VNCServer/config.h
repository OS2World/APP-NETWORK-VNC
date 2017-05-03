#ifndef CONFIG_H
#define CONFIG_H

#include <netinet/in.h>
#include "log.h"

// Result for aclCheck()
// Bit 0 is set - allow: empty or allow-rule has been found.
#define ACL_NOT_FOUND            0
#define ACL_EMPTY                1
#define ACL_DENY                 2
#define ACL_ALLOW                3

typedef struct _ACLITEM {
  BOOL                 fEnable;
  struct in_addr       stInAddr1;
  struct in_addr       stInAddr2;
  BOOL                 fAllow;
  CHAR                 acComment[64];
} ACLITEM, *PACLITEM;

typedef struct _ACL {
  ULONG                ulCount;
  PACLITEM             paItems;
} ACL, *PACL;


typedef struct _CONFIG {
  ULONG      hIni;

  ULONG      ulPort;
  ULONG      ulHTTPPort;
  in_addr_t  inaddrListen;
  ULONG      ulDeferUpdateTime;
  ULONG      ulDeferPtrUpdateTime;
                               //    1   |   2   |   3   |   4   |   5  |
  BOOL       fNeverShared;     //  FALSE | FALSE | FALSE | TRUE  | TRUE |
  BOOL       fAlwaysShared;    //  FALSE | FALSE | TRUE  |  *    |   *  |
  BOOL       fDontDisconnect;  //  FALSE |  TRUE |   *   | FALSE | TRUE |
/*
     1. Disconnect existing clients on new non-shared connections.
          (Отключить существующих при новом не shred подключении)
     2. Block new non-shared connections if someone is already connected.
          (Не shred не разрешено если уже есть подключения)
     3. Always treat connections as shared, add new clients and keep old
        connections.
          (Всегда разрешено новое подключение)
     4. Never treat connections as shared, disconnect existing clients on new
        connection.
          (Отключить существующих при новом подключении)
     5. Never treat connections as shared, disable new clients if there is one
        already.
          (Не пускать если уже есть клиенты)
*/

  ULONG      ulProgressiveSliceHeight;
  BOOL       fHTTPProxyConnect;
  BOOL       fFileTransfer;
  BOOL       fUltraVNCSupport;

  BOOL       fPrimaryPassword;
  CHAR       acPrimaryPassword[9];
  BOOL       fViewOnlyPassword;
  CHAR       acViewOnlyPassword[9];

  ACL        stACL;

  BOOL       fGUIVisible;
  LONG       lGUIx;
  LONG       lGUIy;

  LOGDATA    stLogData;

  BOOL       fProgOnLogon;
  CHAR       acProgOnLogon[CCHMAXPATH+1];
  BOOL       fProgOnGone;
  CHAR       acProgOnGone[CCHMAXPATH+1];
  BOOL       fProgOnCAD;
  CHAR       acProgOnCAD[CCHMAXPATH+1];

  BOOL       fUseDriverVNCKBD;
  BOOL       fUseDriverKBD;
} CONFIG, *PCONFIG;


HINI iniOpen(HAB hab);
VOID iniClose(HINI hIni);

PCONFIG cfgGet();
PCONFIG cfgGetDefault();
VOID cfgStore(PCONFIG pConfig);
VOID cfgFree(PCONFIG pConfig);
VOID cfgSaveGUIData(HAB hab, LONG lX, LONG lY, BOOL fVisible);

#define aclInit(pacl) do { (pacl)->ulCount = 0; (pacl)->paItems = NULL; } while(0)
#define aclFree(pacl) do { if ((pacl)->paItems) free((pacl)->paItems); } while(0)
#define aclAt(pacl,idx) &(pacl)->paItems[idx]
#define aclCount(pacl) (pacl)->ulCount
BOOL aclInsert(PACL pACL, ULONG ulIndex, PACLITEM pItem);
BOOL aclRemove(PACL pACL, ULONG ulIndex);
BOOL aclMove(PACL pACL, ULONG ulIndex, BOOL fForward);
LONG aclItemToStr(PACL pACL, ULONG ulIndex, ULONG cbBuf, PCHAR pcBuf);
BOOL aclStrToItem(ULONG cbStr, PCHAR pcStr, PACLITEM pItem);
ULONG aclCheck(PACL pACL, struct in_addr *pInAddr);

#endif // CONFIG_H
