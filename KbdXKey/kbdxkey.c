#include <string.h>
#define INCL_DOSNLS
#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_WIN
#include <os2.h>
#include "os2xkey.h"
#include "pmhelpers.h"
#include "namestbl.h"
#include "resource.h"
#include <utils.h>
#include <debug.h>

#define _KEYSYM_DIR              "keysym\\"
#define _FNAME_GENERALTABLE      _KEYSYM_DIR"general.xk"
#define _FNAME_LOCALTABLE        _KEYSYM_DIR"%0.xk"

// WM_MAININIT - initialization when window fully created.
#define WM_MAININIT    (WM_USER + 121)
// WM_SETCTLS - window size was changed, need to change controls size/position.
#define WM_SETCTLS     (WM_USER + 122)

#define MAINWIN_MINCX  100
#define MAINWIN_MINCY  100

#define MAX(a,b) ((a) > (b) ? (a) : (b))

typedef struct _KEYRECORD {
  MINIRECORDCORE       stRecCore;
  PSZ                  pszFlags;
  PSZ                  pszScan;
  PSZ                  pszChar;
  PSZ                  pszVK;

  CHAR                 acKeysym[64];
  CHAR                 acFlags[132];
  CHAR                 acScan[14];
  CHAR                 acChar[12];

  ULONG                ulKeysym;
  MPARAM               mp1;
  MPARAM               mp2;
  BOOL                 fPrimary;
} KEYRECORD, *PKEYRECORD;

typedef struct _DLGDATA {
  USHORT               usNamesTblMode; // IDMI_NAMESFORUNKNOWN /
                                       // IDMI_NAMESFORANY / IDMI_NONAMES
  USHORT               usRecordMode;   // IDMI_RECONEKEY / IDMI_RECMANYKEYS /
                                       // IDMI_RECSTOP
  HWND                 hwndCtxMenu;
  HWND                 hwndNamesTbl;
  PSZ                  pszFile;
  BOOL                 fChanged;
} DLGDATA, *PDLGDATA;


HAB                    hab;
HMQ                    hmq;
HWND                   hwndNamesTbl;

static HWND            hwndMain;
static SWP             swpDefault;
static SWP             swpMini;


static PSZ       apszVK[] =
{
  "unknown",
  "VK_BUTTON1",
  "VK_BUTTON2",
  "VK_BUTTON3",
  "VK_BREAK",
  "VK_BACKSPACE",
  "VK_TAB",
  "VK_BACKTAB",
  "VK_NEWLINE",
  "VK_SHIFT",
  "VK_CTRL",
  "VK_ALT",
  "VK_ALTGRAF",
  "VK_PAUSE",
  "VK_CAPSLOCK",
  "VK_ESC",
  "VK_SPACE",
  "VK_PAGEUP",
  "VK_PAGEDOWN",
  "VK_END",
  "VK_HOME",
  "VK_LEFT",
  "VK_UP",
  "VK_RIGHT",
  "VK_DOWN",
  "VK_PRINTSCRN",
  "VK_INSERT",
  "VK_DELETE",
  "VK_SCRLLOCK",
  "VK_NUMLOCK",
  "VK_ENTER",
  "VK_SYSRQ",
  "VK_F1",
  "VK_F2",
  "VK_F3",
  "VK_F4",
  "VK_F5",
  "VK_F6",
  "VK_F7",
  "VK_F8",
  "VK_F9",
  "VK_F10",
  "VK_F11",
  "VK_F12",
  "VK_F13",
  "VK_F14",
  "VK_F15",
  "VK_F16",
  "VK_F17",
  "VK_F18",
  "VK_F19",
  "VK_F20",
  "VK_F21",
  "VK_F22",
  "VK_F23",
  "VK_F24",
  "VK_ENDDRAG",
  "VK_CLEAR",
  "VK_EREOF",
  "VK_PA1",
  "VK_ATTN",
  "VK_CRSEL",
  "VK_EXSEL",
  "VK_COPY",
  "VK_BLK1",
  "VK_BLK2"
};


// tkdlgShow() from testkey.c
VOID tkdlgShow(HAB hab, HWND hwndOwner, HWND hwndNamesTbl, PXKBDMAP pMap);


static SHORT EXPENTRY __cnrComp(PRECORDCORE p1, PRECORDCORE p2, PVOID pStorage)
{
  PKEYRECORD pR1 = (PKEYRECORD)p1;
  PKEYRECORD pR2 = (PKEYRECORD)p2;

  if ( pR1->ulKeysym < pR2->ulKeysym )
    return -1;

  if ( pR1->ulKeysym > pR2->ulKeysym )
    return 1;

  if ( pR1->fPrimary && !pR2->fPrimary )
    return -1;

  if ( !pR1->fPrimary && pR2->fPrimary )
    return 1;

  return 0;
}

static PKEYRECORD _insertNewRecord(HWND hwndCtl, HWND hwndNamesTbl,
                                   ULONG ulKeysym, MPARAM mp1, MPARAM mp2,
                                   BOOL fInvalidate, PBOOL pfNameFound)
{
  PKEYRECORD           pRecord;
  PCHAR                pcPtr;
  RECORDINSERT         stRecIns;
  USHORT               usFlags = SHORT1FROMMP(mp1);
  UCHAR                ucScan  = SHORT2FROMMP(mp1) >> 8;
  USHORT               usChar  = SHORT1FROMMP(mp2);
  USHORT               usVK    = SHORT2FROMMP(mp2);

  // Allocate records for the container.
  pRecord = (PKEYRECORD)WinSendMsg( hwndCtl, CM_ALLOCRECORD,
                 MPFROMLONG( sizeof(KEYRECORD) - sizeof(MINIRECORDCORE) ),
                 MPFROMLONG( 1 ) );
  if ( pRecord == NULL )
    return NULL;

  pRecord->mp1 = mp1;
  pRecord->mp2 = mp2;

  pRecord->stRecCore.pszIcon = pRecord->acKeysym;
  pRecord->pszFlags          = pRecord->acFlags;
  pRecord->pszScan           = pRecord->acScan;
  pRecord->pszChar           = pRecord->acChar;

  pcPtr = pRecord->acFlags;
//  usFlags &= ~(KC_PREVDOWN | KC_LONEKEY | KC_KEYUP);
//  pcPtr += sprintf( pcPtr, "0x%X: ", usFlags );
  if ( (usFlags & KC_CHAR) != 0 )
  {
    pcPtr += sprintf( pcPtr, "KC_CHAR " );
    sprintf( pRecord->acChar, "%c 0x%X", usChar <= 32 ? ' ' : usChar, usChar );
  }
  if ( (usFlags & KC_SCANCODE) != 0 )
  {
    pcPtr += sprintf( pcPtr, "KC_SCANCODE " );
    sprintf( pRecord->acScan, "0x%X", ucScan );
  }
  if ( (usFlags & KC_VIRTUALKEY) != 0 )
  {
    pcPtr += sprintf( pcPtr, "KC_VIRTUALKEY " );
    if ( usVK > ARRAYSIZE(apszVK) )
      usVK = 0;
    pRecord->pszVK = apszVK[usVK];
  }
  if ( (usFlags & KC_DEADKEY) != 0 )
    pcPtr += sprintf( pcPtr, "KC_DEADKEY " );
  if ( (usFlags & KC_COMPOSITE) != 0 )
    pcPtr += sprintf( pcPtr, "KC_COMPOSITE " );
  if ( (usFlags & KC_INVALIDCOMP) != 0 )
    pcPtr += sprintf( pcPtr, "KC_INVALIDCOMP " );
  if ( (usFlags & KC_SHIFT) != 0 )
    pcPtr += sprintf( pcPtr, "KC_SHIFT " );
  if ( (usFlags & KC_ALT) != 0 )
    pcPtr += sprintf( pcPtr, "KC_ALT " );
  if ( (usFlags & KC_CTRL) != 0 )
    pcPtr += sprintf( pcPtr, "KC_CTRL " );
  if ( (usFlags & KC_TOGGLE) != 0 )
    pcPtr += sprintf( pcPtr, "KC_TOGGLE " );

  // keysym.

  *pfNameFound = FALSE;
  pRecord->ulKeysym = ulKeysym;
  if ( ulKeysym != 0 )
  {
    PSZ    pszName = (PSZ)WinSendMsg( hwndNamesTbl, WM_NTQUERYNAME,
                                      MPFROMLONG(pRecord->ulKeysym), 0 );
    if ( pszName == NULL )
      sprintf( pRecord->acKeysym, "0x%X", pRecord->ulKeysym );
    else
    {
      *pfNameFound = TRUE;
      strlcpy( pRecord->acKeysym, pszName, sizeof(pRecord->acKeysym) );
    }
  }

  // Insert record to the container.
  stRecIns.cb                 = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder       = (PRECORDCORE)CMA_END;
  stRecIns.pRecordParent      = NULL;
  stRecIns.zOrder             = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert     = 1;
  stRecIns.fInvalidateRecord  = fInvalidate;
  WinSendMsg( hwndCtl, CM_INSERTRECORD, (PRECORDCORE)pRecord, &stRecIns );

  return pRecord;
}


static BOOL _dlgSetNamesTblMode(HWND hwnd, USHORT usCmd)
{
  PDLGDATA   pData;
  HWND       hwndMenu;

  switch( usCmd )
  {
    case IDMI_NAMESFORUNKNOWN:
    case IDMI_NAMESFORANY:
    case IDMI_NONAMES:
      break;

    default:
      return FALSE;
  }

  hwndMenu = WinWindowFromID( hwnd, FID_MENU );
  pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( pData->usNamesTblMode != 0 )
    WinSendMsg( hwndMenu, MM_SETITEMATTR,
                MPFROM2SHORT( pData->usNamesTblMode, TRUE ),
                MPFROM2SHORT( MIA_CHECKED, 0 ) );

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT( usCmd, TRUE ),
              MPFROM2SHORT( MIA_CHECKED, MIA_CHECKED ) );
  pData->usNamesTblMode = usCmd;

  return TRUE;
}

static BOOL _dlgSetRecordMode(HWND hwnd, USHORT usCmd)
{
  PDLGDATA   pData;
  HWND       hwndMenu;
  BOOL       fStop = usCmd == IDMI_RECSTOP;

  switch( usCmd )
  {
    case IDMI_RECONEKEY:
    case IDMI_RECMANYKEYS:
    case IDMI_RECSTOP:
      break;

    default:
      return FALSE;
  }

  hwndMenu = WinWindowFromID( hwnd, FID_MENU );
  pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( pData->usRecordMode != 0 )
    WinSendMsg( hwndMenu, MM_SETITEMATTR,
                MPFROM2SHORT( pData->usRecordMode, TRUE ),
                MPFROM2SHORT( MIA_CHECKED, 0 ) );

  if ( !fStop )
  {
    WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT( usCmd, TRUE ),
                MPFROM2SHORT( MIA_CHECKED, MIA_CHECKED ) );
    WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT( IDMI_RECSTOP, TRUE ),
                MPFROM2SHORT( MIA_DISABLED, 0 ) );
    // Remove focus from the container to avoid loss of keybord events.
    WinSetFocus( HWND_DESKTOP, hwnd );
  }
  else
    WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT( IDMI_RECSTOP, TRUE ),
                MPFROM2SHORT( MIA_DISABLED, MIA_DISABLED ) );

  WinEnableControl( hwnd, IDCN_LIST, fStop );

  pData->usRecordMode = usCmd;

  return TRUE;
}

static BOOL _dlgInit(HWND hwnd)
{
  HWND                 hwndMenu = WinWindowFromID( hwnd, FID_MENU );
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_LIST );
  PFIELDINFO           pFieldInfo;
  PFIELDINFO           pFldInf;
  CNRINFO              stCnrInf = { 0 };
  FIELDINFOINSERT      stFldInfIns = { 0 };
  CHAR                 acBuf[64];
  RECTL                rectCtl;
  PDLGDATA             pData = calloc( 1, sizeof(DLGDATA) );

  if ( pData == NULL )
  {
    debugCP( "Not enough memory" );
    return FALSE;
  }
  WinSetWindowULong( hwnd, QWL_USER, (ULONG)pData );

  pData->hwndNamesTbl = ntLoad( hwnd );
  if ( pData->hwndNamesTbl == NULLHANDLE )
  {
    debugCP( "ntLoad() failed" );
    WinMessageBox( HWND_DESKTOP, hwnd,
                   "Could not load keysymdef.h or keysym.h file.",
                   APP_NAME, 0, MB_ERROR | MB_OK );
    return FALSE;
  }

  // Tune main menu.

  if ( hwndMenu != NULLHANDLE )
  {
    CHAR     acItem[128];

    // Codepage number for item File->Load->Local table (NNN).
    if ( ( SHORT1FROMMR( WinSendMsg( hwndMenu, MM_QUERYITEMTEXT,
                                MPFROM2SHORT(IDMI_LOADLOCALTBL,sizeof(acBuf)),
                                (MPARAM)acBuf ) ) != 0 ) &&
         ( WinSubstituteStrings( hwnd, acBuf, sizeof(acItem), acItem ) != 0 )
       )
      WinSendMsg( hwndMenu, MM_SETITEMTEXT, MPFROM2SHORT(IDMI_LOADLOCALTBL,0),
                            (MPARAM)acItem );
  }

  // Container's fields.

  pFldInf = (PFIELDINFO)WinSendMsg( hwndCtl, CM_ALLOCDETAILFIELDINFO,
                                    MPFROMLONG( 5 ), NULL );
  if ( pFldInf == NULL )
  {
    debugCP( "WTF?!" );
    return FALSE;
  }

  pFieldInfo = pFldInf;

//    WinLoadString( NULLHANDLE, 0, IDS_NAME, sizeof(acBuf), acBuf );
  strcpy( acBuf, "keysym" );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( KEYRECORD, stRecCore.pszIcon );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

//    WinLoadString( NULLHANDLE, 0, IDS_SIZE, sizeof(acBuf), acBuf );
  strcpy( acBuf, "Flags" );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_FIREADONLY | CFA_HORZSEPARATOR | CFA_LEFT;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( KEYRECORD, pszFlags );
  stCnrInf.pFieldInfoLast = pFieldInfo;
  pFieldInfo = pFieldInfo->pNextFieldInfo;

//    WinLoadString( NULLHANDLE, 0, IDS_DATE, sizeof(acBuf), acBuf );
  strcpy( acBuf, "Scan" );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_RIGHT | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( KEYRECORD, pszScan );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

//    WinLoadString( NULLHANDLE, 0, IDS_TIME, sizeof(acBuf), acBuf );
  strcpy( acBuf, "Character" );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_RIGHT | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( KEYRECORD, pszChar );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

//    WinLoadString( NULLHANDLE, 0, IDS_ATTRIBUTES, sizeof(acBuf), acBuf );
  strcpy( acBuf, "Virtual key" );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_FIREADONLY | CFA_HORZSEPARATOR | CFA_LEFT | CFA_VCENTER;
  pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
  pFieldInfo->pTitleData = strdup( acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( KEYRECORD, pszVK );

  stFldInfIns.cb = sizeof(FIELDINFOINSERT);
  stFldInfIns.pFieldInfoOrder = (PFIELDINFO)CMA_FIRST;
  stFldInfIns.cFieldInfoInsert = 5;
  WinSendMsg( hwndCtl, CM_INSERTDETAILFIELDINFO, MPFROMP( pFldInf ),
              MPFROMP( &stFldInfIns ) );

  WinQueryWindowRect( hwndCtl, &rectCtl );
  stCnrInf.cb = sizeof(CNRINFO);
  stCnrInf.pSortRecord = __cnrComp;
  stCnrInf.flWindowAttr = CV_DETAIL | CA_DETAILSVIEWTITLES | CV_MINI |
                          CA_DRAWICON | CA_TITLEREADONLY | CFA_FITITLEREADONLY;
/*  stCnrInf.slBitmapOrIcon.cx = 16;
  stCnrInf.slBitmapOrIcon.cy = 16;*/
  stCnrInf.xVertSplitbar = ( rectCtl.xRight / 3 ) * 2;
  WinSendMsg( hwndCtl, CM_SETCNRINFO, MPFROMP( &stCnrInf ),
              MPFROMLONG( CMA_PSORTRECORD | CMA_PFIELDINFOLAST |
                          CMA_FLWINDOWATTR | /*CMA_SLBITMAPORICON |*/
                          CMA_XVERTSPLITBAR ) );

  pData->hwndCtxMenu = WinLoadMenu( HWND_DESKTOP, 0, IDM_RECORD );

  _dlgSetRecordMode( hwnd, IDMI_RECSTOP );
  _dlgSetNamesTblMode( hwnd, IDMI_NAMESFORUNKNOWN );

  return TRUE;
}

static VOID _dlgDestroy(HWND hwnd)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCN_LIST );
  PFIELDINFO pFieldInfo;
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  WinDestroyWindow( pData->hwndCtxMenu );

  // Free titles strings of the container.
  pFieldInfo = (PFIELDINFO)WinSendMsg( hwndCtl, CM_QUERYDETAILFIELDINFO, 0,
                                       MPFROMSHORT( CMA_FIRST ) );
  while( ( pFieldInfo != NULL ) && ( (LONG)pFieldInfo != -1 ) )
  {
    if ( pFieldInfo->pTitleData != NULL )
      free( pFieldInfo->pTitleData );

    pFieldInfo = (PFIELDINFO)WinSendMsg( hwndCtl, CM_QUERYDETAILFIELDINFO,
                    MPFROMP( pFieldInfo ), MPFROMSHORT( CMA_NEXT ) );
  }

  if ( pData->hwndNamesTbl != NULLHANDLE )
    WinDestroyWindow( pData->hwndNamesTbl );

  if ( pData->pszFile != NULL )
    free( pData->pszFile );

  if ( pData != NULL )
    free( pData );
}

static VOID _dlgSetControlsPos(HWND hwnd, LONG lDeltaX, LONG lDeltaY)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCN_LIST );
  SWP        swp;

  WinQueryWindowPos( hwndCtl, &swp );
  WinSetWindowPos( hwndCtl, 0, swp.x, swp.y, swp.cx + lDeltaX, swp.cy + lDeltaY,
                   SWP_SIZE );

  hwndCtl = WinWindowFromID( hwnd, IDST_STATUSLINE );
  WinQueryWindowPos( hwndCtl, &swp );
  WinSetWindowPos( hwndCtl, 0, 0, 0, swp.cx + lDeltaX, swp.cy, SWP_SIZE );
}

static PXKBDMAP _dlgNewXkbdMap(HWND hwnd)
{
  PKEYRECORD           pRecord = NULL;
  HWND                 hwndCN = WinWindowFromID( hwnd, IDCN_LIST );
  PXKBDMAP             pMap = xkMapNew();

  if ( pMap != NULL )
  {
    while( TRUE )
    {
      pRecord = (PKEYRECORD)WinSendMsg( hwndCN, CM_QUERYRECORD,
                                      MPFROMP(pRecord),
                                      pRecord == NULL
                                        ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                        : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER)
                                      );
      if ( ( pRecord == NULL ) || ( pRecord == ((PKEYRECORD)(-1)) ) ||
           !xkMapInsert( &pMap, pRecord->ulKeysym, pRecord->mp1, pRecord->mp2 ) )
        break;
    }
  }

  return pMap;
}

static VOID _dlgChanged(HWND hwnd, BOOL fChanged)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND       hwndMenu = WinWindowFromID( hwnd, FID_MENU );

  if ( ( hwndMenu == NULLHANDLE ) || ( pData->fChanged == fChanged ) )
    return;

  pData->fChanged = fChanged;

  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(IDMI_SAVE,TRUE),
              MPFROM2SHORT( MIA_DISABLED,
                            fChanged && (pData->pszFile != NULL)
                              ? 0 : MIA_DISABLED ) );

  // We don't check fChanged here: "Save as..." stay enabled after first
  // loading.
/*  WinSendMsg( hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(IDMI_SAVEAS,TRUE),
              MPFROM2SHORT( MIA_DISABLED, 0 ) );*/
}

static BOOL _dlgSave(HWND hwnd, BOOL fSaveAs)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSZ        pszFile;
  PXKBDMAP   pMap;
  FILEDLG    stFileDlg = { 0 };
  BOOL       fSaved = FALSE;

  if ( !fSaveAs )
  {
    pszFile = pData->pszFile;
    fSaveAs = pszFile == NULL;
  }
  else if ( pData->pszFile != NULL )
    strcpy( stFileDlg.szFullFile, pData->pszFile );

  if ( fSaveAs )
  {
    FILESTATUS3        stInfo;
    ULONG              ulRC;

    if ( pData->pszFile != NULL )
      strcpy( stFileDlg.szFullFile, pData->pszFile );
    else
    {
      ulRC = utilQueryProgPath( sizeof(stFileDlg.szFullFile),
                                stFileDlg.szFullFile );
      strcpy( &stFileDlg.szFullFile[ulRC], _KEYSYM_DIR );
    }

    stFileDlg.cbSize = sizeof(FILEDLG);
    stFileDlg.fl     = FDS_SAVEAS_DIALOG | FDS_CENTER | FDS_ENABLEFILELB;

    if ( ( WinFileDlg( HWND_DESKTOP, hwnd, &stFileDlg ) == NULLHANDLE ) ||
         ( stFileDlg.lReturn != DID_OK ) )
      return FALSE;

    pszFile = stFileDlg.szFullFile;
    ulRC = DosQueryPathInfo( pszFile, FIL_STANDARD, &stInfo, sizeof(stInfo) );
    if ( ( ulRC == NO_ERROR ) && ( (stInfo.attrFile & FILE_DIRECTORY) == 0 ) )
    {
      CHAR   acBuf[512];

      _snprintf( acBuf, sizeof(acBuf),
                 "File %s already exists. Do you whant to overwrite it?",
                 pszFile );
      ulRC = WinMessageBox( HWND_DESKTOP, hwnd, acBuf, APP_NAME, 0,
                            MB_ICONQUESTION | MB_YESNO );
      if ( ulRC != MBID_YES )
        return FALSE;
    }
  }

  pMap = _dlgNewXkbdMap( hwnd );
  if ( pMap != NULL )
  {
    if ( !xkMapSave( pMap, pszFile ) )
    {
      CHAR     acBuf[512];

      sprintf( acBuf, "Cannot store keyboard map to the file %s", pszFile );
      WinMessageBox( HWND_DESKTOP, hwnd, acBuf, APP_NAME, 0, MB_ERROR | MB_OK );
    }
    else
    {
      if ( fSaveAs )
      {
        CHAR acBuf[512];

        // Store a new file name.
        if ( pData->pszFile != NULL )
          free( pData->pszFile );
        pData->pszFile = strdup( pszFile );

        // Set file name to window title.
        _snprintf( acBuf, sizeof(acBuf), APP_NAME" - %s", pszFile );
        WinSetWindowText( hwnd, acBuf );
      }

      _dlgChanged( hwnd, FALSE );
      fSaved = TRUE;
    }

    xkMapFree( pMap );
  }

  return fSaved;
}

static BOOL _dlgSaveChangesAndClear(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  CHAR       acBuf[512];
  ULONG      ulAnswer;

  _dlgSetRecordMode( hwnd, IDMI_RECSTOP );

  if ( pData->fChanged )
  {
    _snprintf( acBuf, sizeof(acBuf), "Table was changed. Save changes to %s ?",
               pData->pszFile );
    ulAnswer = WinMessageBox( HWND_DESKTOP, hwnd, acBuf, APP_NAME, 0,
                              MB_ICONQUESTION | MB_YESNOCANCEL );
    if ( ( ulAnswer == MBID_CANCEL ) ||
         ( ( ulAnswer == MBID_YES ) && !_dlgSave( hwnd, FALSE ) ) )
      return FALSE;

    _dlgChanged( hwnd, FALSE );
  }

  WinSendMsg( WinWindowFromID( hwnd, IDCN_LIST ),
              CM_REMOVERECORD, 0, MPFROM2SHORT(0,CMA_FREE | CMA_INVALIDATE) );
  if ( pData->pszFile != NULL )
  {
    free( pData->pszFile );
    pData->pszFile = NULL;
  }
  WinSetWindowText( hwnd, APP_NAME );

  return TRUE;
}

static VOID _dlgLoad(HWND hwnd, PSZ pszFile, BOOL fShowError)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PXKBDMAP             pMap;
  HWND                 hwndCN = WinWindowFromID( hwnd, IDCN_LIST );
  BOOL                 fNameFound;
  ULONG                ulIdx;
  PXKEYREC             pXKeyRec;
  PKEYRECORD           pRecord;
  BOOL                 fExist;
  CHAR                 acBuf[512];

  if ( !_dlgSaveChangesAndClear( hwnd ) )
    return;

  pMap = xkMapLoad( pszFile, NULL );
  if ( ( pMap == NULL ) && fShowError )
  {
    _snprintf( acBuf, sizeof(acBuf), "Cannot load file %s", pszFile );
    WinMessageBox( HWND_DESKTOP, hwnd, acBuf, APP_NAME, 0, MB_ERROR | MB_OK );
    return;
  }

  // Set file name to window title.
  _snprintf( acBuf, sizeof(acBuf), APP_NAME" - %s", pszFile );
  WinSetWindowText( hwnd, acBuf );
  // Store opened file name.
  pData->pszFile = strdup( pszFile );

  if ( pMap != NULL )
  {
    for( pXKeyRec = &pMap->aList[0], ulIdx = 0; ulIdx < pMap->ulCount;
         pXKeyRec++, ulIdx++ )
    {
      // Search for an existing record for keysym in the container.

      pRecord = NULL;
      fExist = FALSE;
      while( TRUE )
      {
        pRecord = (PKEYRECORD)WinSendMsg( hwndCN, CM_QUERYRECORD, MPFROMP(pRecord),
                                          pRecord == NULL
                                            ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                            : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER)
                                        );
        if ( ( pRecord == NULL ) || ( pRecord == ((PKEYRECORD)(-1)) ) )
          break;

        if ( pRecord->ulKeysym == pXKeyRec->ulKeysym )
        {
          fExist = TRUE;
          break;
        }
      }

      // Insert a new record to the container.
      pRecord = _insertNewRecord( hwndCN, pData->hwndNamesTbl, pXKeyRec->ulKeysym,
                                  pXKeyRec->mp1, pXKeyRec->mp2, FALSE,
                                  &fNameFound );
      if ( pRecord != NULL )
        pRecord->fPrimary = !fExist;
    }
    WinSendMsg( hwndCN, CM_INVALIDATERECORD, 0, MPFROM2SHORT(0,CMA_REPOSITION) );

    xkMapFree( pMap );
  } // if ( pMap != NULL )
}

static VOID _cmdContextMenu(HWND hwnd, USHORT usCNId, PKEYRECORD pMenuRecord)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND                 hwndCN = WinWindowFromID( hwnd, usCNId );
  POINTL               pt;
  BOOL                 fSelected, fPrimary = FALSE;
  PKEYRECORD           pRecord = (PKEYRECORD)CMA_FIRST;

  if ( pMenuRecord == NULL )
    return;

//  if ( pMenuRecord != NULL )
  {
    BOOL     fOnSelectedItem = FALSE;

    // Check: is menu's item one of selected items?
    while( TRUE )
    {
      pRecord = (PKEYRECORD)WinSendMsg( hwndCN, CM_QUERYRECORDEMPHASIS,
                                   MPFROMP(pRecord), MPFROMSHORT(CRA_SELECTED) );
      if ( ( pRecord == NULL ) || ( pRecord == ((PKEYRECORD)(-1)) ) )
      {
        pRecord = NULL;
        break;
      }

      if ( pRecord == pMenuRecord )
      {
        fOnSelectedItem = TRUE;
        break;
      }
    }

    if ( !fOnSelectedItem )
    {
      // Menu's item is not selected - reset selection.
      pRecord = (PKEYRECORD)CMA_FIRST;
      while( TRUE )
      {
        pRecord = (PKEYRECORD)WinSendMsg( hwndCN, CM_QUERYRECORDEMPHASIS,
                                     MPFROMP(pRecord), MPFROMSHORT(CRA_SELECTED) );
        if ( ( pRecord == NULL ) || ( pRecord == ((PKEYRECORD)(-1)) ) )
        {
          pRecord = NULL;
          break;
        }

        WinSendMsg( hwndCN, CM_SETRECORDEMPHASIS, MPFROMP(pRecord),
                    MPFROM2SHORT(FALSE,CRA_SELECTED) );
      }

      // Select only menu's item
      WinSendMsg( hwndCN, CM_SETRECORDEMPHASIS, MPFROMP(pMenuRecord),
                  MPFROM2SHORT(TRUE,CRA_SELECTED) );
    }

    // Menu's item always cursored.
    WinSendMsg( hwndCN, CM_SETRECORDEMPHASIS, MPFROMP(pMenuRecord),
                MPFROM2SHORT(TRUE,CRA_CURSORED) );
  }

  pRecord = (PKEYRECORD)WinSendMsg( hwndCN, CM_QUERYRECORDEMPHASIS,
                            MPFROMLONG(CMA_FIRST), MPFROMSHORT(CRA_SELECTED) );
  fSelected = ( pRecord != NULL ) && ( pRecord != ((PKEYRECORD)(-1)) );

  if ( fSelected )
  {
    // Detect primary record.

    PKEYRECORD         pScanRec = NULL;

    while( TRUE )
    {
      pScanRec = (PKEYRECORD)WinSendMsg( hwndCN, CM_QUERYRECORD,
                                      MPFROMP(pScanRec),
                                      pScanRec == NULL
                                        ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                        : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER)
                                      );
      if ( ( pScanRec == NULL ) || ( pScanRec == ((PKEYRECORD)(-1)) ) )
        break;

      if ( pRecord->ulKeysym == pScanRec->ulKeysym )
      {
        fPrimary = pRecord == pScanRec;
        break;
      }
    }
  }

  WinSendMsg( pData->hwndCtxMenu, MM_SETITEMATTR,
              MPFROM2SHORT( IDMI_DELETE, TRUE ),
              MPFROM2SHORT( MIA_DISABLED, fSelected ? 0 : MIA_DISABLED ) );
  WinSendMsg( pData->hwndCtxMenu, MM_SETITEMATTR,
              MPFROM2SHORT( IDMI_PRIMARY, TRUE ),
              MPFROM2SHORT( MIA_DISABLED, fPrimary ? MIA_DISABLED : 0 ) );

//  WinSendMsg( hwndCN, CM_CLOSEEDIT, 0, 0 );          // Close editor.
  WinQueryMsgPos( hab, &pt );                        // Detect menu position.
  WinPopupMenu( HWND_DESKTOP, hwnd, pData->hwndCtxMenu, pt.x, pt.y,
                IDMI_DELETE, PU_HCONSTRAIN | PU_VCONSTRAIN | PU_MOUSEBUTTON1 |
                PU_MOUSEBUTTON2 | PU_KEYBOARD );
}

static BOOL _wmTranslateAccel(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  // No keyboard accelerator in record mode.
  return ( pData == NULL ) || ( pData->usRecordMode == IDMI_RECSTOP );
}

static VOID _wmCommand(HWND hwnd, USHORT usCmd)
{
  if ( _dlgSetNamesTblMode( hwnd, usCmd ) || _dlgSetRecordMode( hwnd, usCmd ) )
    return;

  switch( usCmd )
  {
    case IDMI_PRIMARY:
      {
        // Set Primary flag for the cursored record and clear flag for other
        // records with some keysym.

        HWND           hwndCN = WinWindowFromID( hwnd, IDCN_LIST );
        PKEYRECORD     pRecord = (PKEYRECORD)WinSendMsg( hwndCN,
                                                   CM_QUERYRECORDEMPHASIS,
                                                   MPFROMLONG(CMA_FIRST),
                                                   MPFROMSHORT(CRA_CURSORED) );
        PKEYRECORD     pScanRec = NULL;

        if ( ( pRecord != NULL ) && ( pRecord != ((PKEYRECORD)(-1)) ) )
        {
          while( TRUE )
          {
            pScanRec = (PKEYRECORD)WinSendMsg( hwndCN, CM_QUERYRECORD,
                                            MPFROMP(pScanRec),
                                            pScanRec == NULL
                                              ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                              : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER)
                                            );
            if ( ( pScanRec == NULL ) || ( pScanRec == ((PKEYRECORD)(-1)) ) )
              break;

            if ( pRecord->ulKeysym == pScanRec->ulKeysym )
              pScanRec->fPrimary = pRecord == pScanRec;
          }

          WinSendMsg( hwndCN, CM_SORTRECORD, MPFROMP(__cnrComp), 0 );
          _dlgChanged( hwnd, TRUE );
        }
      }
      break;

    case IDMI_NEW:
      _dlgSaveChangesAndClear( hwnd );
      break;

    case IDMI_LOADGENTBL:
      {
        CHAR           acFName[CCHMAXPATH];
        ULONG          cbPath = utilQueryProgPath( sizeof(acFName), acFName );

        strcpy( &acFName[cbPath], _FNAME_GENERALTABLE );
        _dlgLoad( hwnd, acFName, FALSE );
      }
      break;

    case IDMI_LOADLOCALTBL:
      {
        CHAR           acBuf[CCHMAXPATH];
        CHAR           acFName[CCHMAXPATH];
        ULONG          cbPath = utilQueryProgPath( sizeof(acBuf) - 11, acBuf );

        strcpy( &acBuf[cbPath], _FNAME_LOCALTABLE );
        if ( WinSubstituteStrings( hwnd, acBuf, sizeof(acFName),
                                   acFName ) != 0 )
          _dlgLoad( hwnd, acFName, FALSE );
      }
      break;

    case IDMI_LOADFILE:
      {
        FILEDLG        stFileDlg = { 0 };
        ULONG          cbPath = utilQueryProgPath( sizeof(stFileDlg.szFullFile),
                                                   stFileDlg.szFullFile );

        stFileDlg.cbSize = sizeof(FILEDLG);
        stFileDlg.fl     = FDS_OPEN_DIALOG | FDS_CENTER | FDS_ENABLEFILELB;
        strcpy( &stFileDlg.szFullFile[cbPath], _KEYSYM_DIR"*.xk" );

        if ( ( WinFileDlg( HWND_DESKTOP, hwnd, &stFileDlg ) != NULLHANDLE ) &&
             ( stFileDlg.lReturn == DID_OK ) )
          _dlgLoad( hwnd, stFileDlg.szFullFile, TRUE );
      }
      break;

    case IDMI_SAVE:
      _dlgSave( hwnd, FALSE );
      break;

    case IDMI_SAVEAS:
      _dlgSave( hwnd, TRUE );
      break;

    case IDMI_TESTKEY:
      {
        PXKBDMAP             pMap = _dlgNewXkbdMap( hwnd );

        if ( pMap != NULL )
        {
          PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

          tkdlgShow( hab, hwnd, pData->hwndNamesTbl, pMap );
          xkMapFree( pMap );
        }
      }
      break;

    case IDMI_EXIT:
      WinSendMsg( hwnd, WM_CLOSE, 0, 0 );
      break;

    case IDMI_DELETE:
      {
        HWND           hwndCN = WinWindowFromID( hwnd, IDCN_LIST );
        PKEYRECORD     pRecord;

        while( TRUE )
        {
          pRecord = (PKEYRECORD)WinSendMsg( hwndCN, CM_QUERYRECORDEMPHASIS,
                            MPFROMLONG(CMA_FIRST), MPFROMSHORT(CRA_SELECTED) );

          if ( ( pRecord == NULL ) || ( pRecord == ((PKEYRECORD)(-1)) ) )
            break;

          WinSendMsg( hwndCN, CM_REMOVERECORD, MPFROMP(&pRecord),
                      MPFROM2SHORT(1,CMA_FREE | CMA_INVALIDATE) );
          _dlgChanged( hwnd, TRUE );
        }
      }
      break;
  }
}

static MRESULT _wmControl(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  switch( SHORT1FROMMP(mp1) )
  {
    case IDCN_LIST:
      switch( SHORT2FROMMP(mp1) )
      {
        case CN_ENTER:
          {
            PNOTIFYRECORDENTER       pNotify = (PNOTIFYRECORDENTER)mp2;
            PKEYRECORD               pRecord = (PKEYRECORD)pNotify->pRecord;

            if ( pRecord != NULL )
              WinSendMsg( pData->hwndNamesTbl, WM_NTSHOW,
                          MPFROMLONG(pRecord->ulKeysym), MPFROMLONG(TRUE) );
          }
          return (MRESULT)0;

        case CN_EMPHASIS:
          if ( WinIsWindowVisible( pData->hwndNamesTbl ) )
          {
            PNOTIFYRECORDEMPHASIS    pNotify = (PNOTIFYRECORDEMPHASIS)mp2;
            PKEYRECORD               pRecord = (PKEYRECORD)pNotify->pRecord;

            WinSendMsg( pData->hwndNamesTbl, WM_NTSHOW,
                        MPFROMLONG(pRecord->ulKeysym), MPFROMLONG(FALSE) );
          }
          return (MRESULT)0;

        case CN_CONTEXTMENU:
          _cmdContextMenu( hwnd, SHORT1FROMMP(mp1), (PKEYRECORD)mp2 );
          return (MRESULT)0;
      }
      break;

    case IDDLG_NAMESTBL:
      // Keysym has been selected by user.
      if ( SHORT2FROMMP(mp1) == NC_ENTER )
      {
        PNOTIFYNTENTER pNotify = (PNOTIFYNTENTER)mp2;
        HWND           hwndCtl = WinWindowFromID( hwnd, IDCN_LIST );
        PKEYRECORD     pRecord = (PKEYRECORD)
                WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                            MPFROMLONG(CMA_FIRST), MPFROMSHORT(CRA_CURSORED) );
 
        if ( ( pRecord != NULL ) && ( pRecord != ((PKEYRECORD)(-1)) ) &&
             ( pRecord->ulKeysym != pNotify->ulValue ) )
        {
          pRecord->ulKeysym = pNotify->ulValue;
          strlcpy( pRecord->acKeysym, pNotify->pszName,
                   sizeof(pRecord->acKeysym) );
          WinSendMsg( hwndCtl, CM_INVALIDATERECORD, MPFROMP(&pRecord),
                      MPFROM2SHORT(1,CMA_TEXTCHANGED) );
          WinSetFocus( HWND_DESKTOP, hwndCtl );
          _dlgChanged( hwnd, TRUE );
        }
      }
      break;
  }

  return (MRESULT)0;
}

static VOID _wmChar(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  PKEYRECORD           pRecord;
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_LIST );
  USHORT               usFlags = SHORT1FROMMP(mp1);
  UCHAR                ucScan  = (usFlags & KC_SCANCODE) == 0 ?
                                  0 : SHORT2FROMMP(mp1) >> 8;
  BOOL                 fNameFound;

/*  if ( WinQueryFocus( HWND_DESKTOP ) != hwnd )
    return;*/

  if ( ( pData == NULL ) || ( pData->usRecordMode == IDMI_RECSTOP ) ||
       ( (usFlags & KC_KEYUP) != 0 ) || ( (usFlags & KC_SCANCODE) == 0 ) )
    return;

  // Remove some flags and cleaning parameters.
  mp1 = MPFROMSH2CH( usFlags & XKEYKC_MASK, 1, ucScan );

  // Unselect all records in the container.
  pRecord = (PKEYRECORD)CMA_FIRST;
  while( TRUE )
  {
    pRecord = (PKEYRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                                 MPFROMP(pRecord), MPFROMSHORT(CRA_SELECTED) );
    if ( ( pRecord == NULL ) || ( pRecord == ((PKEYRECORD)(-1)) ) )
      break;

    WinSendMsg( hwndCtl, CM_SETRECORDEMPHASIS, MPFROMP(pRecord),
                MPFROM2SHORT(FALSE,CRA_SELECTED) );
  }

  // Search record for the key.
  pRecord = NULL;
  do
  {
    pRecord = (PKEYRECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD,
                                      MPFROMP(pRecord),
                                      pRecord == NULL
                                        ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                        : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER)
                                     );
    if ( ( pRecord == NULL ) || ( pRecord == ((PKEYRECORD)(-1)) ) )
    {
      pRecord = NULL;
      break;
    }
  }
  while( !xkCompMP( pRecord->mp1, pRecord->mp2, mp1, mp2 ) );

  if ( pRecord == NULL )
  {
    // Record not found - create a new record.
    pRecord = _insertNewRecord( hwndCtl, pData->hwndNamesTbl,
                                xkKeysymFromMPAuto( mp1, mp2 ),
                                mp1, mp2, TRUE, &fNameFound );
    _dlgChanged( hwnd, TRUE );
  }
  else
    fNameFound = TRUE;

  // Make record visible.
  cnrhScrollToRecord( hwndCtl, (PRECORDCORE)pRecord, CMA_TEXT, FALSE );
  // Select record.
  WinSendMsg( hwndCtl, CM_SETRECORDEMPHASIS, MPFROMP(pRecord),
              MPFROM2SHORT(TRUE,CRA_SELECTED | CRA_CURSORED) );

  if ( ( ( pData->usNamesTblMode == IDMI_NAMESFORUNKNOWN ) && !fNameFound ) ||
       ( pData->usNamesTblMode == IDMI_NAMESFORANY ) )
    WinSendMsg( pData->hwndNamesTbl, WM_NTSHOW,
                MPFROMLONG(pRecord->ulKeysym), MPFROMLONG(TRUE) );

  if ( pData->usRecordMode == IDMI_RECONEKEY )
    _dlgSetRecordMode( hwnd, IDMI_RECSTOP );
}

static PSZ _wmSubstituteString(HWND hwnd, USHORT usIndex)
{
  static CHAR         acBuf[64];

  if ( usIndex == 0 )
  {
    // %0 - codepage number.

    COUNTRYCODE          stCountryCode = { 0 };
    COUNTRYINFO          stCountryInfo; 
    ULONG                ulRC;
    LONG                 cbActual;

    ulRC = DosQueryCtryInfo( sizeof(COUNTRYINFO), &stCountryCode,
                             &stCountryInfo, (PULONG)&cbActual );
    if ( ulRC != NO_ERROR )
    {
      debug( "DosQueryCtryInfo(), rc = %u", ulRC );
      return NULL;
    }

    ultoa( stCountryInfo.codepage, acBuf, 10 );
  }
  else
    return NULL;

  return acBuf;
}

static MRESULT EXPENTRY _dlgMainProc(HWND hwnd, ULONG msg, MPARAM mp1,
                                     MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      WinPostMsg( hwnd, WM_MAININIT, 0, 0 );
      return (MRESULT)TRUE;

    case WM_DESTROY:
      _dlgDestroy( hwnd );
      break;

    case WM_MAININIT:
      {
        HACCEL     hAccel;
        SWP        swp;
        USHORT     usWinId = WinQueryWindowUShort( hwnd, QWS_ID );

      /*
        {
          SWCNTRL    swctl; 
          swctl.hwnd          = hwnd;
          swctl.hwndIcon      = hwnd;
          swctl.hprog         = NULLHANDLE;
          swctl.idProcess     = 0;
          swctl.idSession     = 0;
          swctl.uchVisibility = SWL_VISIBLE;
          swctl.fbJump        = SWL_JUMPABLE;
          WinQueryWindowText( hwnd, sizeof(swctl.szSwtitle), swctl.szSwtitle );
          WinAddSwitchEntry( &swctl );
        }

        {
          FRAMECDATA stFrame;

          stFrame.cb = sizeof(FRAMECDATA);
          stFrame.flCreateFlags = FCF_MENU;
          stFrame.hmodResources = 0L;
          stFrame.idResources = usWinId;
          WinCreateFrameControls( hwnd, &stFrame, NULL );
        }
      */

        hAccel = WinLoadAccelTable( hab, NULLHANDLE, usWinId );
        if ( hAccel != NULLHANDLE )
          WinSetAccelTable( hab, hAccel, hwnd );

        WinQueryWindowPos( hwnd, &swp );
        swpDefault = swp;
        swpMini = swp;

        if ( WinLoadMenu( hwnd, 0, usWinId ) != NULLHANDLE )
          swp.cy += WinQuerySysValue( HWND_DESKTOP, SV_CYMENU );

        WinSetWindowPos( hwnd, HWND_TOP,
          ( WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN ) / 2 ) - (swp.cx / 2),
          ( WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN ) / 2 ) - (swp.cy / 2),
          swp.cx, swp.cy, SWP_MOVE | SWP_SIZE | SWP_NOADJUST );

        if ( !_dlgInit( hwnd ) )
          WinPostQueueMsg( hmq, WM_QUIT, 0, 0 );
        else
          WinPostMsg( hwnd, WM_SETCTLS, MPFROMLONG(swpDefault.cx),
                      MPFROMLONG(swpDefault.cy) ); 
      }
      return (MRESULT)0;

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
           if ( ( ((PSWP)mp1)->cx < swpDefault.cx ) ||
                ( ((PSWP)mp1)->cy < swpDefault.cy ) )
           {
             ((PSWP)mp1)->cx = MAX( swpDefault.cx, ((PSWP)mp1)->cx );
             ((PSWP)mp1)->cy = MAX( swpDefault.cy, ((PSWP)mp1)->cy );
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

            WinShowWindow( hwnd, TRUE );
         }
      } 
      return (MRESULT)0;

    case WM_CLOSE:
      if ( _dlgSaveChangesAndClear( hwnd ) )
        WinPostQueueMsg( hmq, WM_QUIT, 0, 0 );
      return (MRESULT)0;

    case WM_COMMAND:
      _wmCommand( hwnd, SHORT1FROMMP(mp1) );
      return (MRESULT)0;

    case WM_CONTROL:
      return _wmControl( hwnd, mp1, mp2 );

    case WM_TRANSLATEACCEL:
      if ( !_wmTranslateAccel( hwnd ) )
        return (MRESULT)FALSE;
      break;

    case WM_CHAR:
      _wmChar( hwnd, mp1, mp2 );
      break;

    case WM_SUBSTITUTESTRING:
      return (MRESULT)_wmSubstituteString( hwnd, SHORT1FROMMP(mp1) );
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


int main()
{
  PTIB       tib;
  PPIB       pib;
  QMSG       qmsg;

  debugInit();

  // Change process type code for use Win* API from VIO session.
  DosGetInfoBlocks( &tib, &pib );
  if ( pib->pib_ultype == 2 || pib->pib_ultype == 0 )
  {
    // VIO windowable or fullscreen protect-mode session.
    pib->pib_ultype = 3; // Presentation Manager protect-mode session.
  }

  // Initialize PM
  hab = WinInitialize( 0 );
  if ( !hab )
  {
    debug( "WinInitialize() failed" );
    return 1;
  }

  // Create default size message queue 
  hmq = WinCreateMsgQueue( hab, 0 );
  if ( hmq == NULLHANDLE )
  {
    debug( "WinCreateMsgQueue() failed" );
    WinTerminate( hab );
    return 1;
  }

  hwndMain = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, _dlgMainProc, NULLHANDLE,
                         IDDLG_KEYTBL, NULL );

  if ( hwndMain == NULLHANDLE )
    debug( "WinLoadDlg() failed" );
  else
  {
    // Process messages
    while( WinGetMsg( hab, &qmsg, 0, 0, 0 ) )
      WinDispatchMsg( hab, &qmsg );

    WinDestroyWindow( hwndMain );
  }

  WinDestroyMsgQueue( hmq );
  WinTerminate( hab );

  debugDone();
  return 0;
}
