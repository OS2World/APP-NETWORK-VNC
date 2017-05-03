#include <ctype.h>
#include <string.h>
#define INCL_WIN
#include <os2.h>
#include <vncpm.h>
#include "namestbl.h"
#include "resource.h"
#include <utils.h>
#include <debug.h>

#define WM_SETCTLS     (WM_USER + 122)

#define NTOBJ_NODE     0
#define NTOBJ_DEFINE   1

#if 0
#define _MIN_WIDTH     swpDefault.cx
#define _MIN_HEIGHT    swpDefault.cy
#else
#define _MIN_WIDTH     250
#define _MIN_HEIGHT    100
#endif

typedef struct _NTNODE  *PNTNODE;
typedef struct _NTOBJ   *PNTOBJ;

typedef struct _NTOBJ {
  PNTOBJ               pNext;
  ULONG                ulType;   // NTOBJ_xxxxx
  PNTNODE              pParent;
} NTOBJ;

typedef struct _NTNODE {
  NTOBJ                stObj;
  PNTOBJ               pObjects;
  ULONG                cObjects;
  PNTOBJ               pLast;
  CHAR                 acName[1];
} NTNODE;

typedef struct _NTDEFINE {
  NTOBJ                stObj;
  PSZ                  pszComment;
  ULONG                ulValue;
  CHAR                 acName[1];
} NTDEFINE, *PNTDEFINE;

typedef struct _DEFRECORD {
  MINIRECORDCORE       stRecCore;
  PNTOBJ               pObj;
} DEFRECORD, *PDEFRECORD;

typedef struct _DLGDATA {
  PNTNODE              pRootNode;
  PNTNODE              pSelNode;
} DLGDATA, *PDLGDATA;

extern HAB             hab;
extern HMQ             hmq;

static SWP             swpDefault;
static SWP             swpMini;


static PNTNODE __ntInsertNode(PNTNODE pParent, ULONG cbName, PCHAR pcName)
{
  PNTNODE    pNode = malloc( sizeof(NTNODE) + (cbName == 0 ? 0 : (cbName+4)) );

  if ( pNode == NULL )
    return NULL;

  pNode->stObj.ulType  = NTOBJ_NODE;
  pNode->stObj.pParent = pParent;
  if ( pcName != NULL )
  {
    *((PUSHORT)pNode->acName) = 0x205B; // "[ "
    memcpy( &pNode->acName[2], pcName, cbName );
    *((PUSHORT)&pNode->acName[cbName+2]) = 0x5D20; // " ]"
    cbName += 4;
  }
  pNode->acName[cbName] = '\0';
  pNode->pObjects = NULL;
  pNode->cObjects = 0;

  pNode->stObj.pNext = NULL;
  if ( pParent != NULL )
  {
    if ( pParent->pObjects == NULL )
    {
      pParent->pLast = (PNTOBJ)pNode;
      pParent->pObjects = (PNTOBJ)pNode;
    }
    else
    {
      pParent->pLast->pNext = (PNTOBJ)pNode;
      pParent->pLast = (PNTOBJ)pNode;
    }
    pParent->cObjects++;
  }

  return pNode;
}

static VOID __ntInsertDefine(PNTNODE pParent, ULONG cbName, PCHAR pcName,
                            ULONG cbValue, PCHAR pcValue,
                            ULONG cbComment, PCHAR pcComment)
{
  PNTDEFINE  pDefine;
  ULONG      ulValue;
  CHAR       acBuf[32];
  PCHAR      pcEnd;

  if ( cbValue > sizeof(acBuf) )
  {
    debug( "Value too long: \"%s\"", debugBufPSZ( pcValue, cbValue ) );
    return;
  }
  memcpy( acBuf, pcValue, cbValue );
  acBuf[cbValue] = '\0';

  ulValue = strtoul( acBuf, &pcEnd, 0 ); 
  if ( !utilStrToULong( cbValue, pcValue, 0, ~0, &ulValue ) )
  {
    debug( "Value is not ULONG: \"%s\"", debugBufPSZ( pcValue, cbValue ) );
    return;
  }

  pDefine = malloc( sizeof(NTDEFINE) + cbName );
  if ( pDefine == NULL )
    return;

  pDefine->stObj.ulType  = NTOBJ_DEFINE;
  pDefine->stObj.pParent = pParent;
  if ( cbComment != 0 )
  {
    pDefine->pszComment = malloc( cbComment + 1 );
    if ( pDefine->pszComment != NULL )
    {
      memcpy( pDefine->pszComment, pcComment, cbComment );
      pDefine->pszComment[cbComment] = '\0';
    }
  }
  else
    pDefine->pszComment = NULL;

  pDefine->ulValue = ulValue;

  memcpy( pDefine->acName, pcName, cbName );
  pDefine->acName[cbName] = '\0';

  pDefine->stObj.pNext = NULL;
  if ( pParent->pObjects == NULL )
  {
    pParent->pLast = (PNTOBJ)pDefine;
    pParent->pObjects = (PNTOBJ)pDefine;
  }
  else
  {
    pParent->pLast->pNext = (PNTOBJ)pDefine;
    pParent->pLast = (PNTOBJ)pDefine;
  }
  pParent->cObjects++;
}

static VOID __ntReadNode(FILE *fdDef, PNTNODE pNode)
{
  PCHAR      pcPtr;
  BOOL       fInComment = FALSE;
  PNTNODE    pSubNode;
  PCHAR      pcName, pcValue, pcComment;
  ULONG      cbName, cbValue, cbComment;
  CHAR       acBuf[1024];

  while( feof( fdDef ) == 0 )
  {
    if ( fgets( acBuf, sizeof(acBuf), fdDef ) == NULL )
      break;

    pcPtr = acBuf;
l00:
    STR_SKIP_SPACES( pcPtr );
    if ( *pcPtr == '\0' )
      continue;

    if ( !fInComment )
    {
      if ( *((PUSHORT)pcPtr) == 0x2F2F /* '//' */ )
        continue;

      if ( *((PUSHORT)pcPtr) == 0x2A2F /* slash and asterisc */ )
      {
        pcPtr += 2;
        STR_SKIP_SPACES( pcPtr );
        fInComment = TRUE;
      }
    }

    if ( fInComment )
    {
      pcPtr = strstr( pcPtr, "*/" );
      if ( pcPtr == NULL )
        continue;

      pcPtr += 2;
      fInComment = FALSE;
      goto l00;
    }

    if ( ( memicmp( pcPtr, "#endif", 6 ) == 0 ) &&
         ( isspace( pcPtr[6] ) || pcPtr[6] == '\0' ) )
      break;

    if ( ( memicmp( pcPtr, "#ifdef", 6 ) == 0 ) && isspace( pcPtr[6] ) )
    {
      pcPtr += 7;
      STR_SKIP_SPACES( pcPtr );
      pcName = pcPtr;
      STR_MOVE_TO_SPACE( pcPtr );
      pSubNode = __ntInsertNode( pNode, pcPtr - pcName, pcName );
      __ntReadNode( fdDef, pSubNode );
      continue;
    }

    if ( ( memicmp( pcPtr, "#define", 7 ) == 0 ) && isspace( pcPtr[7] ) )
    {
      pcPtr += 7;
      STR_SKIP_SPACES( pcPtr );
      pcName = pcPtr;
      STR_MOVE_TO_SPACE( pcPtr );
      cbName = pcPtr - pcName;

      STR_SKIP_SPACES( pcPtr );
      if ( *pcPtr == '\0' )
        continue;
      pcValue = pcPtr;
      STR_MOVE_TO_SPACE( pcPtr );
      cbValue = pcPtr - pcValue;

      STR_SKIP_SPACES( pcPtr );
      if ( ( *((PUSHORT)pcPtr) == 0x2F2F ) || ( *((PUSHORT)pcPtr) == 0x2A2F ) )
      {
        pcPtr += 2;
        STR_SKIP_SPACES( pcPtr );
        pcComment = pcPtr;

        pcPtr = strchr( pcPtr, '\0' );
        while( ( pcPtr > pcComment ) && isspace( *(pcPtr - 1) ) )
          pcPtr--;
        if ( ( (pcPtr - pcComment) >= 2 ) &&
             ( *((PUSHORT)(pcPtr - 2)) == 0x2F2A ) )
        {
          pcPtr -= 2;
          while( ( pcPtr > pcComment ) && isspace( *(pcPtr - 1) ) )
            pcPtr--;
        }
        cbComment = pcPtr - pcComment;
      }
      else
        cbComment = 0;

      __ntInsertDefine( pNode, cbName, pcName, cbValue, pcValue,
                        cbComment, pcComment );
      continue;
    } // if ( stricmp( pcPtr, "#define" ) == 0 )

  }
}

static PNTNODE _ntLoad()
{
  FILE       *fdDef;
  PNTNODE    pNode;

  fdDef = fopen( "keysymdef.h", "rt" );
  if ( fdDef == NULL )
    fdDef = fopen( "keysym.h", "rt" );

  if ( fdDef == NULL )
  {
    debug( "Can't open keysymdef.h / keysym.h file." );
    return NULL;
  }

  pNode = __ntInsertNode( NULL, 0, NULL ); // Root node.
  __ntReadNode( fdDef, pNode );

  fclose( fdDef );

  return pNode;
}

static VOID _ntFree(PNTNODE pNode)
{
  PNTOBJ     pScan = (PNTOBJ)pNode->pObjects;
  PNTOBJ     pNext;

  while( pScan != NULL )
  {
    pNext = pScan->pNext;

    switch( pScan->ulType )
    {
      case NTOBJ_NODE:
        _ntFree( (PNTNODE)pScan );
        break;

      case NTOBJ_DEFINE:
        if ( ((PNTDEFINE)pScan)->pszComment != NULL )
          free( ((PNTDEFINE)pScan)->pszComment );
        free( pScan );
        break;
    }

    pScan = pNext;
  }

  free( pNode );
}

static PNTDEFINE _ntFind(PNTNODE pNode, ULONG ulKeysym)
{
  PNTOBJ     pScan;

  for( pScan = pNode->pObjects; pScan != NULL; pScan = pScan->pNext )
  {
    switch( pScan->ulType )
    {
      case NTOBJ_NODE:
        {
          PNTDEFINE    pDefine = _ntFind( (PNTNODE)pScan, ulKeysym );

          if ( pDefine != NULL )
            return pDefine;
        }
        break;

      case NTOBJ_DEFINE:
        if ( ((PNTDEFINE)pScan)->ulValue == ulKeysym )
          return (PNTDEFINE)pScan;
        break;
    }
  }

  return NULL;
}


static VOID _cnrFillNames(HWND hwndCtl, PNTNODE pNode, PNTOBJ pSelObj)
{
  PDEFRECORD           pRecord, pRecords, pSelRecord = NULL;
  RECORDINSERT         stRecIns;
  PNTOBJ               pObj = (PNTOBJ)pNode->pObjects;
  ULONG                cRecords = pNode->stObj.pParent == NULL
                                    ? pNode->cObjects : pNode->cObjects + 1;

  WinSendMsg( hwndCtl, CM_REMOVERECORD, 0, MPFROM2SHORT(0,CMA_FREE) );
  pRecords = (PDEFRECORD)WinSendMsg( hwndCtl, CM_ALLOCRECORD,
                      MPFROMLONG( sizeof(DEFRECORD) - sizeof(MINIRECORDCORE) ),
                      MPFROMLONG( cRecords ) );
  if ( pRecords == NULL )
    return;

  pRecord = pRecords;

  if ( pNode->stObj.pParent != NULL )
  {
    pRecord->stRecCore.pszIcon = "< Return >";
    pRecord->pObj = (PNTOBJ)pNode->stObj.pParent;
    pSelRecord = pRecord;
    pRecord = (PDEFRECORD)pRecord->stRecCore.preccNextRecord;
  }

  while( pObj != NULL )
  {
    pRecord->stRecCore.pszIcon =
      pObj->ulType == NTOBJ_NODE ? ((PNTNODE)pObj)->acName
                                 : ((PNTDEFINE)pObj)->acName;
    pRecord->pObj = pObj;

    if ( pSelObj == pObj )
      pSelRecord = pRecord;

    pRecord = (PDEFRECORD)pRecord->stRecCore.preccNextRecord;
    pObj = pObj->pNext;
  }

  // Insert records to the container.
  stRecIns.cb                 = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder       = (PRECORDCORE)CMA_END;
  stRecIns.pRecordParent      = NULL;
  stRecIns.zOrder             = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert     = cRecords;
  stRecIns.fInvalidateRecord  = TRUE;
  WinSendMsg( hwndCtl, CM_INSERTRECORD, (PRECORDCORE)pRecords, &stRecIns );

  if ( pSelRecord != NULL )
  {
    WinSendMsg( hwndCtl, CM_SETRECORDEMPHASIS, MPFROMP(pSelRecord),
                MPFROM2SHORT(TRUE,CRA_SELECTED | CRA_CURSORED) );
    cnrhScrollToRecord( hwndCtl, (PRECORDCORE)pSelRecord, CMA_TEXT, FALSE );
  }
}


static BOOL _dlgInit(HWND hwnd)
{
  PDLGDATA   pData = malloc( sizeof(DLGDATA) );
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCN_NAMES );
  CNRINFO    stCnrInf = { 0 };

  if ( pData == NULL )
    return FALSE;

  pData->pRootNode = _ntLoad();
  if ( pData->pRootNode == NULL )
  {
    free( pData );
    return FALSE;
  }

  WinSetWindowULong( hwnd, QWL_USER, (ULONG)pData );

  stCnrInf.cb = sizeof(CNRINFO);
//  stCnrInf.pSortRecord = __cnrComp;
  stCnrInf.flWindowAttr = CV_TEXT | CV_FLOW;
  WinSendMsg( hwndCtl, CM_SETCNRINFO, MPFROMP( &stCnrInf ),
              MPFROMLONG( CMA_FLWINDOWATTR ) );

  _cnrFillNames( hwndCtl, pData->pRootNode, NULL );
  pData->pSelNode = pData->pRootNode;

  return TRUE;
}

static VOID _dlgDestroy(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( pData != NULL )
  {
    if ( pData->pRootNode != NULL )
      _ntFree( pData->pRootNode );
    free( pData );
  }
}

static VOID _dlgSetControlsPos(HWND hwnd, LONG lDeltaX, LONG lDeltaY)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCN_NAMES );
  SWP        swp;

  WinQueryWindowPos( hwndCtl, &swp );
  WinSetWindowPos( hwndCtl, 0, 0, 0, swp.cx + lDeltaX, swp.cy + lDeltaY,
                   SWP_SIZE );

  hwndCtl = WinWindowFromID( hwnd, IDST_STATUSLINE );
  WinQueryWindowPos( hwndCtl, &swp );
  WinSetWindowPos( hwndCtl, 0, 0, 0, swp.cx + lDeltaX, swp.cy,
                   SWP_SIZE );
}

static VOID _wmCommand(HWND hwnd, USHORT usCmd)
{
/*
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( pData == NULL )
    return;

  switch( usCmd )
  {
  }
*/
}

static MRESULT _wmControl(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCN_NAMES );

  if ( SHORT1FROMMP(mp1) != IDCN_NAMES )
    return (MRESULT)0;

  switch( SHORT2FROMMP(mp1) )
  {
    case CN_ENTER:
      {
        PNOTIFYRECORDENTER       pNotify = (PNOTIFYRECORDENTER)mp2;
        PDEFRECORD               pRecord = (PDEFRECORD)pNotify->pRecord;
        PNTOBJ                   pObj = pRecord->pObj;

        if ( pObj->ulType == NTOBJ_NODE )
        {
          PDLGDATA     pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

          _cnrFillNames( hwndCtl, (PNTNODE)pObj, (PNTOBJ)pData->pSelNode );
          pData->pSelNode = (PNTNODE)pObj;
        }
        else
        {
          NOTIFYNTENTER          stNotify;

          WinSendMsg( hwnd, WM_CLOSE, 0, 0 );

          stNotify.ulValue = ((PNTDEFINE)pObj)->ulValue;
          stNotify.pszName = ((PNTDEFINE)pObj)->acName;
          WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_CONTROL,
                      MPFROM2SHORT( WinQueryWindowUShort( hwnd, QWS_ID ),
                                    NC_ENTER ),
                      MPFROMP( &stNotify ) );
        }
      }
      return (MRESULT)0;

    case CN_EMPHASIS:
      {
        PNOTIFYRECORDEMPHASIS    pNotify = (PNOTIFYRECORDEMPHASIS)mp2;

        if ( (pNotify->fEmphasisMask & CRA_CURSORED) != 0 )
        {
          PDEFRECORD   pRecord = (PDEFRECORD)pNotify->pRecord;
          PNTOBJ       pObj = pRecord->pObj;
          CHAR         acBuf[256];
          HWND         hwndSL = WinWindowFromID( hwnd, IDST_STATUSLINE );

          if ( ( pObj->ulType == NTOBJ_NODE
                   ? snprintf( acBuf, sizeof(acBuf), "%s Items: %u",
                               ((PNTNODE)pObj)->acName,
                               ((PNTNODE)pObj)->cObjects )
                   : snprintf( acBuf, sizeof(acBuf), "%s | Value: 0x%X%s%s",
                               ((PNTDEFINE)pObj)->acName,
                               ((PNTDEFINE)pObj)->ulValue,
                               ((PNTDEFINE)pObj)->pszComment == NULL
                                 ? "" : " | Comment: ",
                               ((PNTDEFINE)pObj)->pszComment == NULL
                                 ? "" : ((PNTDEFINE)pObj)->pszComment ) )
                 == -1 )
            acBuf[0] = '\0';

          WinSetWindowText( hwndSL, acBuf );
        }
      }
      return (MRESULT)0;
  }

  return (MRESULT)0;
}

static VOID _wmNTShow(HWND hwnd, ULONG ulKeysym, BOOL fActivate)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( ulKeysym != 0 )
  {
    PNTDEFINE          pDefine = _ntFind( pData->pRootNode, ulKeysym );
    HWND               hwndCtl = WinWindowFromID( hwnd, IDCN_NAMES );

    if ( pDefine != NULL )
    {
      _cnrFillNames( hwndCtl, pDefine->stObj.pParent, (PNTOBJ)pDefine );
      pData->pSelNode = pDefine->stObj.pParent;
    }
  }

  WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0,
                   fActivate ? (SWP_ACTIVATE | SWP_SHOW) : SWP_SHOW );
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      {
        WinQueryWindowPos( hwnd, &swpDefault );
        swpMini = swpDefault;

        if ( !_dlgInit( hwnd ) )
          WinDestroyWindow( hwnd );
        else
        {
          SWP        swp;

          WinQueryWindowPos( hwnd, &swp );

          WinSetWindowPos( hwnd, HWND_TOP,
            ( WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN ) / 2 ) - (swp.cx / 2),
            ( WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN ) / 2 ) - (swp.cy / 2),
            0, 0, SWP_MOVE | SWP_NOADJUST );

          WinPostMsg( hwnd, WM_SETCTLS, MPFROMLONG(swpDefault.cx),
                      MPFROMLONG(swpDefault.cy) );
        }
      }
      return (MRESULT)TRUE;

    case WM_CLOSE:
      WinShowWindow( hwnd, FALSE );
      WinSetWindowPos( WinQueryWindow( hwnd, QW_OWNER ), HWND_TOP, 0, 0, 0, 0,
                       SWP_ACTIVATE );
      return (MRESULT)0;

    case WM_DESTROY:
      _dlgDestroy( hwnd );
      break;

    case WM_ADJUSTWINDOWPOS:
      {
        SWP  swp;

        WinQueryWindowPos( hwnd, (PSWP)&swp );

        if ( (((PSWP)mp1)->fl & SWP_MINIMIZE) != 0 )
          WinQueryWindowPos( hwnd, (PSWP)&swpMini );
        else if ( (((PSWP)mp1)->fl & (SWP_RESTORE | SWP_MAXIMIZE)) != 0 )
        {
          if ( (swp.fl & SWP_MINIMIZE) != 0 )
          {
            ((PSWP)mp1)->fl = ((PSWP)mp1)->fl & ~SWP_ACTIVATE;

            WinPostMsg( hwnd, WM_SETCTLS,
                        MPFROMLONG(swpMini.cx), MPFROMLONG(swpMini.cy) );
          }
          else
          {
            if ( (((PSWP)mp1)->cx<swpDefault.cx) ||
                 (((PSWP)mp1)->cy<swpDefault.cy) )
            {
               ((PSWP)mp1)->cx = MAX( swpDefault.cx, ((PSWP)mp1)->cx );
               ((PSWP)mp1)->cy = MAX( swpDefault.cy, ((PSWP)mp1)->cy );
            }

            WinPostMsg( hwnd, WM_SETCTLS,
                        MPFROMLONG(swp.cx), MPFROMLONG(swp.cy) );
          }
        }
        else if ( (((PSWP)mp1)->fl & SWP_SIZE) != 0 )
        {
           if ( ( ((PSWP)mp1)->cx < _MIN_WIDTH ) ||
                ( ((PSWP)mp1)->cy < _MIN_HEIGHT ) )
           {
             ((PSWP)mp1)->cx = MAX( _MIN_WIDTH, ((PSWP)mp1)->cx );
             ((PSWP)mp1)->cy = MAX( _MIN_HEIGHT, ((PSWP)mp1)->cy );
           }

           WinPostMsg( hwnd, WM_SETCTLS,
                       MPFROMLONG(swp.cx), MPFROMLONG(swp.cy) );
        } 
      }
      break;

    case WM_SETCTLS:
      {
         SWP           swp;
         LONG          lDeltaX, lDeltaY;

         WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ACTIVATE );
         WinQueryWindowPos( hwnd, (PSWP)&swp );

         if ( !(swp.fl & SWP_MINIMIZE) )
         {
            lDeltaX = swp.cx - LONGFROMMP(mp1);
            lDeltaY = swp.cy - LONGFROMMP(mp2);
            if ( ( lDeltaX != 0 ) || ( lDeltaY != 0 ) )
              _dlgSetControlsPos( hwnd, lDeltaX, lDeltaY );

//            WinShowWindow( hwnd, TRUE );
         }
      } 
      return (MRESULT)0;

    case WM_COMMAND:
      _wmCommand( hwnd, SHORT1FROMMP(mp1) );
      return (MRESULT)0;

    case WM_CONTROL:
      return _wmControl( hwnd, mp1, mp2 );

    case WM_NTSHOW:
      _wmNTShow( hwnd, LONGFROMMP(mp1), LONGFROMMP(mp2) );
      return (MRESULT)0;

    case WM_NTQUERYNAME:
    {
      PDLGDATA         pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
      PNTDEFINE        pDefine = _ntFind( pData->pRootNode, LONGFROMMP(mp1) );

      return (MRESULT)( pDefine == NULL ? NULL : pDefine->acName );
    }
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


HWND ntLoad(HWND hwndOwner)
{
  return WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, NULLHANDLE,
                     IDDLG_NAMESTBL, NULL );
}
