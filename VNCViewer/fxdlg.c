#include <fcntl.h>
#include <ctype.h>
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#define INCL_GPI
#define INCL_WIN
#define INCL_DOSDEVICES
#define INCL_DOSDEVIOCTL
#include <os2.h>
#include <rfb/rfbclient.h>
#include <utils.h>
#include "clntconn.h"
#include "clntwnd.h"
#include "prbar.h"
#include "resource.h"
#include "linkseq.h"
#include <debug.h>

#define WM_ENDOFLOCALLIST        (WM_USER + 1)

typedef struct _DLGINITDATA {
  ULONG      ulSize;
  PSZ        pszServer;
  PCLNTCONN  pCC;
} DLGINITDATA, *PDLGINITDATA;


typedef struct _FINFO {
  FDATE      fdateCreation;      // Date of file creation.
  FTIME      ftimeCreation;      // Time of file creation.
  FDATE      fdateLastAccess;    // Date of last access.
  FTIME      ftimeLastAccess;    // Time of last access.
  FDATE      fdateLastWrite;     // Date of last write.
  FTIME      ftimeLastWrite;     // Time of last write.
  ULONG      ulAttr;
  ULLONG     ullSize;
} FINFO, *PFINFO;

typedef struct _SCANITEM {
  SEQOBJ     seqObj;
  FINFO      stFI;
  CHAR       acName[1];
} SCANITEM, *PSCANITEM;

// Current operation on directories/files.
// When need to change this defines: see array aulOpStrId[] in
// _createProgressDlg().
#define _SCAN_NONE     0
#define _SCAN_RLCOPY   1
#define _SCAN_RLMOVE   2
#define _SCAN_RDELETE  3
#define _SCAN_LRCOPY   4
#define _SCAN_LRMOVE   5
#define _SCAN_LDELETE  6

#define _SCANFL_OVERWRITE        0x0001
#define _SCANFL_NOOVERWRITE      0x0002
#define _SCANFL_RDIROVERWRITE    0x0004
#define _SCANFL_FORALL           0x0100

#define _SCANMB_OK               1001
#define _SCANMB_CANCEL           1002
#define _SCANMB_SKIP             1003
#define _SCANMB_ALL              1004
#define _SCANMB_NONE             1005
#define _SCANMB_NO               1006
#define _SCANMB_YES_RDELETE      1007
#define _SCANMB_YES_RLMOVE       1008
#define _SCANMB_YES_LRMOVE       1009
#define _SCANMB_OK_RDIR          1010
#define _SCANMB_YES_LDELETE      1011


// Containers records.
typedef struct _FILERECORD {
  MINIRECORDCORE       stRecCore;
  PSZ                  pszSize;
  CDATE                stDate;
  CTIME                stTime;
  PSZ                  pszAttr;

  CHAR                 szSize[16];
  CHAR                 szName[CCHMAXPATHCOMP];
  CHAR                 szAttr[8];
  BOOL                 fUpDir;

  FINFO                stFI;
} FILERECORD, *PFILERECORD;

typedef struct _DLGDATA {
  PCLNTCONN  pCC;
  HPOINTER   hptrFile;
  HPOINTER   hptrFolder;
  HPOINTER   hptrFileHidden;
  HPOINTER   hptrFolderHidden;
  HWND       hwndCtxMenu;
  BOOL       fFileListRequested;

  ULONG      ulScanOp;           // _SCAN_xxxxx
  LINKSEQ    lsScan;             // List of PSCANITEM objects.
  LINKSEQ    lsScanDel;          // List of PSCANITEM objects.
  ULLONG     ullScanSize;
  ULONG      ulScanFlag;         // _SCANFL_xxxxx
  HWND       hwndScanProgress;
  BOOL       fScanCancel;

  PFILERECORD  pEditRecord;      // Edited record for which a request sent.
  CHAR         acOldName[CCHMAXPATHCOMP];
} DLGDATA, *PDLGDATA;


// main.c
extern HAB             hab;

static VOID _ctlListEnter(HWND hwnd, PFILERECORD pRecord, BOOL fRemote);
static PFILERECORD _cnrSearch(HWND hwndCtl, PSZ pszName);
static VOID _scanError(HWND hwnd, ULONG ulMsgId);

#define _IS_FLD_VALID(pf) ( ( pf != (PFIELDINFO)(-1) ) && ( pf != NULL ) )


static VOID _moveToOwnerCenter(HWND hwnd)
{
  HWND       hwndOwner = WinQueryWindow( hwnd, QW_OWNER );
  HWND       hwndParent = WinQueryWindow( hwnd, QW_PARENT );
  RECTL      rectOwner, rectWin;

  if ( hwndOwner == NULLHANDLE )
    WinQueryWindowRect( hwndParent, &rectOwner );
  else
  {
    WinQueryWindowRect( hwndOwner, &rectOwner );
    WinMapWindowPoints( hwndOwner, hwndParent, (PPOINTL)&rectOwner, 2 );
  }
  WinQueryWindowRect( hwnd, &rectWin );

  WinSetWindowPos( hwnd, HWND_TOP,
    ( (rectOwner.xLeft + rectOwner.xRight) / 2 ) - ( rectWin.xRight / 2 ),
    ( (rectOwner.yBottom + rectOwner.yTop) / 2 ) - ( rectWin.yTop / 2 ),
    0, 0, SWP_MOVE | SWP_SHOW | SWP_ACTIVATE );
}



/* ********************************************************* */
/*                                                           */
/*           Progress dialog for the file manager.           */
/*                                                           */
/* ********************************************************* */

// WM_PDSETVALUE
//   mp1, SHORT 1: PDVT_xxxxx
//     PDVT_TOTALSIZE - mp2 is PULLONG
//     PDVT_FILENAME  - mp2 is PSZ
//     PDVT_FILECHUNK - mp2 is PULONG
//   mp2, PVOID: pointer to the value.
#define WM_PDSETVALUE            (WM_USER + 1)
#define PDVT_TOTALSIZE           0
#define PDVT_FILENAME            1
#define PDVT_FILECHUNK           2

typedef struct _PROGRESSDLGINITDATA {
  USHORT     usSize;
  CHAR       acOperation[256];
} PROGRESSDLGINITDATA, *PPROGRESSDLGINITDATA;

static BOOL _pdwmInitDlg(HWND hwnd, PPROGRESSDLGINITDATA pInitData)
{
  _moveToOwnerCenter( hwnd );
  WinSetDlgItemText( hwnd, IDST_OPERATION, pInitData->acOperation );

  return TRUE;
}

static VOID _pdwmDestory(HWND hwnd)
{
}

static VOID _pdwmPDSetValue(HWND hwnd, USHORT usType, PVOID pValue)
{
  CHAR       acBuf[32];
  HWND       hwndProgress = WinWindowFromID( hwnd, IDPBAR_OPERATION );
  PRBARINFO  stPrBarInfo;

  switch( usType )
  {
    case PDVT_TOTALSIZE:
      utilStrFromBytes( *((PULLONG)pValue), sizeof(acBuf), acBuf );
      WinSetDlgItemText( hwnd, IDST_TOTAL_SIZE, acBuf );

      stPrBarInfo.ullTotal = *((PULLONG)pValue);
      WinSendMsg( hwndProgress, PBM_SETPARAM, MPFROMLONG(PBARSF_TOTAL),
                  MPFROMP(&stPrBarInfo) );
      break;

    case PDVT_FILENAME:
      WinSetDlgItemText( hwnd, IDST_FILE, (PSZ)pValue );
      break;

    case PDVT_FILECHUNK:
      stPrBarInfo.ullValue = *((PULONG)pValue);
      stPrBarInfo.ulAnimation = PRBARA_STATIC;
      WinSendMsg( hwndProgress, PBM_SETPARAM,
                  MPFROMLONG(PBARSF_ANIMATION | PBARSF_VALUEINCR),
                  MPFROMP(&stPrBarInfo) );
      break;
  }
}

static MRESULT EXPENTRY _dlgProgressProc(HWND hwnd, ULONG msg, MPARAM mp1,
                                         MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_pdwmInitDlg( hwnd, (PPROGRESSDLGINITDATA)mp2 );

    case WM_DESTROY:
      _pdwmDestory( hwnd );
      break;

    case WM_PDSETVALUE:
      _pdwmPDSetValue( hwnd, SHORT1FROMMP(mp1), PVOIDFROMMP(mp2) );
      return (MRESULT)0;

    case WM_COMMAND:
      if ( SHORT1FROMMP(mp1) == MBID_CANCEL )
        WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_COMMAND,
                    MPFROMSHORT(MBID_CANCEL), MPFROM2SHORT(CMDSRC_OTHER,0) );
      return (MRESULT)0;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}

static HWND _createProgressDlg(HWND hwndOwner, ULONG ulOp)
{
  static ULONG         aulOpStrId[] =
  { /* 0 _SCAN_NONE   */             0,  /* 1 _SCAN_RLCOPY  */ IDS_OP_RLCOPY,
    /* 2 _SCAN_RLMOVE */ IDS_OP_RLMOVE,  /* 3 _SCAN_RDELETE */ IDS_OP_RDELETE,    // 3 _SCAN_RDELETE
    /* 4 _SCAN_LRCOPY */ IDS_OP_LRCOPY,  /* 5 _SCAN_LRMOVE  */ IDS_OP_LRMOVE,
    /* 6 _SCAN_LDELETE*/ IDS_OP_LDELETE };

  HWND                 hwndDlg;
  PROGRESSDLGINITDATA  stInitData;

  stInitData.usSize = sizeof(PROGRESSDLGINITDATA);
  if ( ( aulOpStrId[ulOp] == 0 ) || ( ulOp >= ARRAYSIZE(aulOpStrId) ) )
  {
    debug( "Invalid operation: %u", ulOp );
    stInitData.acOperation[0] = '\0';
  }
  else
    WinLoadString( hab, 0, aulOpStrId[ulOp], sizeof(stInitData.acOperation),
                   stInitData.acOperation );

  hwndDlg = WinLoadDlg( hwndOwner, hwndOwner, _dlgProgressProc, NULLHANDLE,
                        IDDLG_FXPROGRESS, &stInitData );
  if ( hwndDlg == NULLHANDLE )
    debug( "WinLoadDlg(,,,,IDDLG_FXPROGRESS,) failed" );

  return hwndDlg;
}


/* ********************************************************* */
/*                                                           */
/*           Files transfer (file manager) dialog.           */
/*                                                           */
/* ********************************************************* */

typedef struct _MBBTN {
  ULONG    ulStrId;
  ULONG    ulBtnId;
} MBBTN, *PMBBTN;

// Message box generic function.
static VOID _mbGeneric(HWND hwnd, ULONG ulStyle, ULONG cButtons,
                       PMBBTN paButtons, ULONG ulMsgId, PSZ pszName)
{
  PDLGDATA  pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  ULONG     cbMB2Info = sizeof(MB2INFO) + ( (cButtons - 1) * sizeof(MB2D) );
  PMB2INFO  pMB2Info = (PMB2INFO)malloc( cbMB2Info );
  ULONG     ulIdx;
  CHAR      acText[200];
  LONG      cbText, cbName;

  if ( pMB2Info == NULL )
  {
    debug( "Message box creation error, message id: %u", ulMsgId );
    WinPostMsg( hwnd, WM_MSGBOXDISMISS, 0, MPFROMLONG(_SCANMB_CANCEL) );
    return;
  }

  pMB2Info->cb         = cbMB2Info;
  pMB2Info->hIcon      = NULLHANDLE;
  pMB2Info->cButtons   = cButtons;
  pMB2Info->flStyle    = ulStyle;
  pMB2Info->hwndNotify = hwnd;

  for( ulIdx = 0; ulIdx < cButtons; ulIdx++ )
  {
    WinLoadString( hab, 0, paButtons[ulIdx].ulStrId, MAX_MBDTEXT + 1,
                   pMB2Info->mb2d[ulIdx].achText );
    pMB2Info->mb2d[ulIdx].idButton = paButtons[ulIdx].ulBtnId;
    pMB2Info->mb2d[ulIdx].flStyle = 0;
  }

  cbText = WinLoadMessage( hab, 0, ulMsgId, sizeof(acText) - 2, acText );
  if ( pszName != NULL )
  {
    acText[cbText] = '\n';
    cbText++;
    cbName = strlen( pszName );
    if ( cbName >= ( sizeof(acText) - 1 - cbText ) )
      // No buffer space for directory name. We will show trailing part.
      pszName = &pszName[cbName] - (sizeof(acText) - 1 - cbText);

    strcpy( &acText[cbText], pszName );
  }

  if ( pData->hwndScanProgress != NULLHANDLE )
    WinEnableWindow( pData->hwndScanProgress, FALSE );

  WinMessageBox2( HWND_DESKTOP, hwnd, acText, "", 0, pMB2Info );
  free( pMB2Info );
}

static VOID _mbOverwrite(HWND hwnd, PSZ pszLocalName)
{
  static MBBTN  aButtons[] = { { IDS_OK, _SCANMB_OK },
                   { IDS_CANCEL, _SCANMB_CANCEL }, { IDS_SKIP, _SCANMB_SKIP },
                   { IDS_ALL, _SCANMB_ALL }, { IDS_NONE, _SCANMB_NONE } };
              
  _mbGeneric( hwnd, MB_NONMODAL | MB_QUERY, ARRAYSIZE(aButtons), aButtons,
              IDM_OVERWRITE, pszLocalName );
}

static VOID _mbError(HWND hwnd, ULONG ulMsgId, PSZ pszName)
{
  static MBBTN  stButton = { IDS_CANCEL, _SCANMB_CANCEL };

  _mbGeneric( hwnd, MB_NONMODAL | MB_ERROR, 1, &stButton, ulMsgId, pszName );
}

static VOID _mbConfirm(HWND hwnd, ULONG ulMsgId, ULONG ulYesBtnId)
{
  MBBTN      aButtons[2];

  aButtons[0].ulStrId = IDS_YES;
  aButtons[0].ulBtnId = ulYesBtnId;
  aButtons[1].ulStrId = IDS_NO;
  aButtons[1].ulBtnId = _SCANMB_NO;

  _mbGeneric( hwnd, MB_NONMODAL | MB_QUERY, 2, aButtons, ulMsgId, NULL );
}

#define _isDir(attr) ( ((attr) & RFB_FILE_ATTRIBUTE_DIRECTORY) != 0 )

static BOOL _mkDir(PSZ pszName, PFINFO pFI)
{
  ULONG                ulRC;
  FILESTATUS3L         stInfo;

  // Do not check return code - a directory may exist.
  DosCreateDir( pszName, NULL );

  ulRC = DosQueryPathInfo( pszName, FIL_STANDARDL, &stInfo, sizeof(stInfo) );
  if ( ( ulRC != NO_ERROR ) || !_isDir( stInfo.attrFile ) )
    return FALSE;

  stInfo.attrFile        = pFI->ulAttr;
  stInfo.fdateCreation   = pFI->fdateCreation;
  stInfo.ftimeCreation   = pFI->ftimeCreation;
  stInfo.fdateLastAccess = pFI->fdateLastAccess;
  stInfo.ftimeLastAccess = pFI->ftimeLastAccess;
  stInfo.fdateLastWrite  = pFI->fdateLastWrite;
  stInfo.ftimeLastWrite  = pFI->ftimeLastWrite;

  ulRC = DosSetPathInfo( pszName, FIL_STANDARDL, &stInfo, sizeof(stInfo), 0 );
  if ( ulRC != NO_ERROR )
    debug( "DosSetPathInfo(), rc = %u", ulRC );

  return TRUE;
}

// Enable/disable all dialog controls.
static VOID _enableConrols(HWND hwnd, BOOL fEnable)
{
  static ULONG         aulControls[] =
    { IDCN_LLIST, IDCB_LDRIVES, IDPB_LUPDIR, IDPB_LROOT, IDEF_LPATH, IDCN_RLIST,
      IDCB_RDRIVES, IDPB_RUPDIR, IDPB_RROOT, IDEF_RPATH };
  ULONG      ulIdx;

  for( ulIdx = 0; ulIdx < ARRAYSIZE(aulControls); ulIdx++ )
    WinEnableControl( hwnd, aulControls[ulIdx], fEnable );
}

static VOID _os2FindBufToFInfo(PFILEFINDBUF3L pFind, PFINFO pFI)
{
  pFI->fdateCreation   = pFind->fdateCreation;
  pFI->ftimeCreation   = pFind->ftimeCreation;
  pFI->fdateLastAccess = pFind->fdateLastAccess;
  pFI->ftimeLastAccess = pFind->ftimeLastAccess;
  pFI->fdateLastWrite  = pFind->fdateLastWrite;
  pFI->ftimeLastWrite  = pFind->ftimeLastWrite;
  pFI->ulAttr          = pFind->attrFile;
  pFI->ullSize         = *((PULLONG)&pFind->cbFile);
}

// Convert windows file date/time to the unix timestamp.
static time_t _ftWinToUnix(ULLONG ullTime)
{
  // high:low - number of 100-nanosecond intervals since January 1, 1601.
  // The result represents the time in seconds since January 1, 1970.

  time_t     timeVal;
  ULL2UL     stTime;

  if ( ullTime == 0 )
    return 0;

  *((PULLONG)&stTime) = ullTime;
  ullTime = (((unsigned long long)stTime.ulHigh) << 32) + stTime.ulLow;
  ullTime /= 10000000;             // 100-nanoseconds -> seconds
  ullTime -= 11644473600LL;

  timeVal = (time_t)ullTime;
  return timeVal;
}

// Convert time to OS/2 file date/time.
static VOID _ftUnixToOS2(time_t timeFile, PFDATE pDate, PFTIME pTime)
{
  struct tm  *pTM;

  pTM = localtime( &timeFile );
  if ( pTM->tm_isdst == 1 )
  {
    // We got time with Daylight Savings Time flag. 
    pTM->tm_isdst = 0;
    timeFile -= ( mktime( pTM ) - timeFile );
    pTM = localtime( &timeFile );
  }

  pDate->day      = pTM->tm_mday;
  pDate->month    = pTM->tm_mon + 1;
  pDate->year     = ( pTM->tm_year + 1900 ) - 1980;
  pTime->twosecs  = pTM->tm_sec / 2;
  pTime->minutes  = pTM->tm_min;
  pTime->hours    = pTM->tm_hour;
}

// Convert windows file date/time to OS/2 file date/time.
static VOID _ftWinToOS2(ULLONG ullTime, PFDATE pDate, PFTIME pTime)
{
  _ftUnixToOS2( _ftWinToUnix( ullTime ), pDate, pTime );
}


static VOID _scanFilesClean(PDLGDATA pData)
{
  PSCANITEM  pScan;

  while( (pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan )) != NULL )
  {
    lnkseqRemove( &pData->lsScan, pScan );
    free( pScan );
  }

  while( (pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScanDel )) != NULL )
  {
    lnkseqRemove( &pData->lsScanDel, pScan );
    free( pScan );
  }

  if ( pData->hwndScanProgress != NULLHANDLE )
  {
    WinDestroyWindow( pData->hwndScanProgress );
    pData->hwndScanProgress = NULLHANDLE;
  }

  pData->ullScanSize   = 0;
  pData->ulScanOp      = _SCAN_NONE;
  pData->ulScanFlag    = 0;
  pData->fScanCancel  = FALSE;
}

static VOID _scanFilesAdd(PDLGDATA pData, LONG cbName, PCHAR pcName,
                          PFINFO pFI)
{
  PSCANITEM  pScan;
  BOOL       fDir = _isDir( pFI->ulAttr );

  if ( cbName < 0 )
    cbName = strlen( pcName );
  if ( cbName == 0 )
    return;

  if ( fDir )
  {
    if ( ( *((PUSHORT)&pcName[cbName-2]) == 0x2E5C     /* \.  */ ) ||
         ( *((PULONG)&pcName[cbName-3])  == 0x002E2E5C /* \.. */ ) )
      return;
  }

  // Make item for scan list.
  pScan = malloc( sizeof(SCANITEM) + cbName );
  if ( pScan == NULL )
    return;
  pScan->stFI = *pFI;
  memcpy( pScan->acName, pcName, cbName );
  pScan->acName[cbName] = '\0';

  // Insert new item to the scan-list. We keep next order of items:
  //   dir1                          - user selected dir., added at cmdCopy()
  //   dir1\dir11                    - second pos., dir. inserted during scanning
  //   dir1\dir12                    - directory, shifted from second position
  //   dir1\dir12\dirA               - directory, shifted from second position
  //   dir2                          - user selected dir., added at cmdCopy()
  //   dir1\file1.ext                - file, added at end during scanning dir1
  //   dir1\dir11\file11.ext         - file, added at end during scanning dir11
  //   dir1\dir12\dirA\fileInA.ext   - file, added at end during scanning dirA
  //
  // Directories will be inserted after the first directory if list is not
  // empty. First position is a current item which is in the scanning process.
  // Files will be inserted at the end of the list.
  // When all directory's items listed, direcory will be removed from list in
  // _filexferEndOfList(), local directory will be created in this time and
  // content of new first directory will be received. So, above in example, if
  // dir1 contains only dir11, dir12 and file1.ext the first record shoul be
  // remved in _filexferEndOfList() and content of dir1\dir11 will be requested.
  // If next item is a file (no directories left) we start receiving files.
  // When file downloaded, first record removes from list in _filexferEndOfFile
  // and next file requests.
  // Directories scanning -> Files receiving -> Empty list: "copy" is complete.
  // In dynamics:
  //
  // Step 1 - directories dir1, dir2 selected by user and listed in cmdCopy().
  // Content of dir1 requested. 
  //   dir1                          - user selected dir., added in cmdCopy()
  //   dir2                          - user selected dir., added in cmdCopy()
  //
  // Step 2 - content of dir1 obtained, end-of-list for dir1 received.
  // dir1 removed from the list, local directory dir1 created, content of
  // dir1\dir11 requested.
  //   dir1\dir11
  //   dir1\dir12
  //   dir2
  //   dir1\file1.ext
  //
  // Step 3 - content of dir11 obtained, end-of-list for dir11 received.
  //   dir1\dir12
  //   dir2
  //   dir1\file1.ext
  //   dir1\dir11\file11.ext
  //
  // and so on...

  if ( fDir )
  {
    PSCANITEM  pFirst = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );

    debug( "Add scan directory: %s", pScan->acName );

    if ( ( pFirst != NULL ) && _isDir( pFirst->stFI.ulAttr ) )
      lnkseqAddAfter( &pData->lsScan, pFirst, pScan );
    else
      lnkseqAddFirst( &pData->lsScan, pScan );
  }
  else
  {
    debug( "Add scan file: %s", pScan->acName );
    lnkseqAdd( &pData->lsScan, pScan );
    pData->ullScanSize += pFI->ullSize;
  }
}

// Add items selected in the container hwndList to the scan list plsScan.
static VOID _scanFilesAddSelected(HWND hwndList, PDLGDATA pData)
{
  PFILERECORD          pRecord = (PFILERECORD)CMA_FIRST;

  while( TRUE )
  {
    pRecord = (PFILERECORD)WinSendMsg( hwndList, CM_QUERYRECORDEMPHASIS,
                                 MPFROMP(pRecord), MPFROMSHORT(CRA_SELECTED) );
    if ( ( pRecord == NULL ) || ( pRecord == ((PFILERECORD)(-1)) ) )
      break;

    if ( !pRecord->fUpDir )
      _scanFilesAdd( pData, -1, pRecord->stRecCore.pszIcon,
                     &pRecord->stFI );
  }
}

// Sends request for the next item from scan list (or scan-to-delete list) which
// corresponds to the current operation (copy/move/delete).
static BOOL _scanSendRequest(HWND hwnd, PDLGDATA pData)
{
  PSCANITEM  pScan;
  HWND       hwndRPath = WinWindowFromID( hwnd, IDEF_RPATH );
  ULONG      cbScanBase = WinQueryWindowTextLength( hwndRPath );
  ULONG      cbScanName;
  PSZ        pszRemoteName;

l00:
  pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );
  if ( pScan != NULL )
  {
    cbScanName    = strlen( pScan->acName );
    pszRemoteName = alloca( cbScanBase + cbScanName + 1 );

    if ( pszRemoteName == NULL )
      debugCP( "Not enough stack space" );
    else
    {
      // Make full remote directory or file name.
      WinQueryWindowText( hwndRPath, cbScanBase + 1, pszRemoteName );
      strcpy( &pszRemoteName[cbScanBase], pScan->acName );

      if ( _isDir( pScan->stFI.ulAttr ) )
      {
        if ( ccFXRequestFileList( pData->pCC, pszRemoteName ) )
          return FALSE;
      }
      else
      {
        HWND     hwndCtl = WinWindowFromID( hwnd, IDEF_LPATH );
        ULONG    cbLocalPath = WinQueryWindowTextLength( hwndCtl );
        PSZ      pszLocalName = alloca( cbLocalPath + cbScanName + 1 );

        // Make local file full name.
        if ( pszLocalName == NULL )
          debugCP( "Not enough stack space" );
        else
        {
          BOOL       fSuccess;

          WinQueryWindowText( hwndCtl, cbLocalPath + 1, pszLocalName );
          strcpy( &pszLocalName[cbLocalPath], pScan->acName );

          if ( pData->ulScanOp == _SCAN_RDELETE )
          {
            debug( "ccFXDelete(,\"%s\") (scan list)", pszRemoteName );
            fSuccess = ccFXDelete( pData->pCC, pszRemoteName );
          }
          else // pData->ulScanOp is _SCAN_RLCOPY or _SCAN_RDELETE
          {
            // Start receiving a file.
            ULONG        ulScanFlag = pData->ulScanFlag;

            if ( (ulScanFlag & _SCANFL_FORALL) == 0 )
              pData->ulScanFlag = 0;

            if ( (ulScanFlag & _SCANFL_OVERWRITE) == 0 )
            {
              FILESTATUS3L   stInfo;
              ULONG          ulRC = DosQueryPathInfo( pszLocalName,
                                      FIL_STANDARDL, &stInfo, sizeof(stInfo) );

              if ( ( ulRC == NO_ERROR ) && !_isDir( stInfo.attrFile ) )
              {
                // File exists.

                if ( (ulScanFlag & _SCANFL_NOOVERWRITE) != 0 )
                {
                  // Skip this file.

                  // Decrease the total size by the size of the skipped file.
                  pData->ullScanSize -= pScan->stFI.ullSize;
                  if ( pData->hwndScanProgress != NULLHANDLE )
                    WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                                MPFROMSHORT(PDVT_TOTALSIZE),
                                MPFROMP(&pData->ullScanSize) );
              
                  // Remove current scan record and go to the next record.
                  lnkseqRemove( &pData->lsScan, pScan );
                  free( pScan );
                  goto l00;
                }

                // Show message box and exit. User selection will be specified
                // in the message WM_MSGBOXDISMISS.
                _mbOverwrite( hwnd, pszLocalName );
                return FALSE;
              } // if ( ( ulRC != NO_ERROR ) && !_isDir( stInfo.attrFile ) )
            } // if ( (ulScanFlag & _SCANFL_OVERWRITE) == 0 )

            debug( "ccFXRecvFile(,\"%s\",\"%s\")", pszRemoteName, pszLocalName );
            fSuccess = ccFXRecvFile( pData->pCC, pszRemoteName, pszLocalName );
          } // if ( pData->ulScanOp == _SCAN_RDELETE ) else

          if ( fSuccess )
          {
            if ( pData->hwndScanProgress != NULLHANDLE )
              WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                       MPFROMSHORT(PDVT_FILENAME), MPFROMP(&pScan->acName) );

            return FALSE;
          }

        } // if ( pszLocalName == NULL ) else
      } // if ( _isDir( pScan->stFI.ulAttr ) ) else
    } // if ( pszRemoteName == NULL ) else
  } // if ( pScan != NULL )
  else if ( lnkseqGetFirst( &pData->lsScanDel ) != NULL )
  {
    pScan          = (PSCANITEM)lnkseqGetFirst( &pData->lsScanDel );
    cbScanName     = strlen( pScan->acName );
    pszRemoteName  = alloca( cbScanBase + cbScanName + 1 );

    if ( pszRemoteName == NULL )
      debugCP( "Not enough stack space" );
    else
    {
      // Make full remote directory or file name.
      WinQueryWindowText( hwndRPath, cbScanBase + 1, pszRemoteName );
      strcpy( &pszRemoteName[cbScanBase], pScan->acName );

      debug( "ccFXDelete(,\"%s\") (scan-delete list)", pszRemoteName );
      if ( ccFXDelete( pData->pCC, pszRemoteName ) &&
           ( pData->hwndScanProgress != NULLHANDLE ) )
        WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                    MPFROMSHORT(PDVT_FILENAME), MPFROMP(&pScan->acName) );
    }

    return FALSE;
  }

  // Scan lists are empty or error occurred - end of operation.
  return TRUE;
}

static BOOL _scanLocalRequest(HWND hwnd, PDLGDATA pData)
{
  HWND           hwndLPath = WinWindowFromID( hwnd, IDEF_LPATH );
  CHAR           acLocalName[CCHMAXPATH];
  ULONG          cbLPath = WinQueryWindowText( hwndLPath, CCHMAXPATH - 4,
                                               acLocalName );
  HDIR           hDir = HDIR_CREATE;
  ULONG          ulRC, cFind, ulMaxFindName;
  FILEFINDBUF3L  stFind;
  FINFO          stFI;
  PSCANITEM      pScan;
  PSZ            pszLocalName;
  PCHAR          pcPos;

l01:
  pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );
  if ( pScan != NULL )
  {
    ulRC = strlen( pScan->acName );
    if ( ulRC > ( sizeof(acLocalName) - cbLPath - 4 ) )
    {
      // Remove current scan record and go to the next record.
      debug( "WTF? Too long name" );
      lnkseqRemove( &pData->lsScan, pScan );
      free( pScan );
      goto l01;
    }

    if ( ( pData->ulScanOp != _SCAN_LDELETE ) &&
         ( strchr( pScan->acName, '\\' ) == NULL ) )
    {
      // Copy/move operation and top-level scan item (relative curent path).

      PFILERECORD  pRecord = _cnrSearch( WinWindowFromID( hwnd, IDCN_RLIST ),
                                         pScan->acName );
      ULONG        ulScanFlag = pData->ulScanFlag;

      if ( (ulScanFlag & _SCANFL_FORALL) == 0 )
        pData->ulScanFlag = 0;

      if ( pRecord != NULL )
      {
        // We have some name on remote files list panel.

        if ( _isDir( pScan->stFI.ulAttr ) != _isDir( pRecord->stFI.ulAttr ) )
        {
          // Local item is directory and remote item is not or vice versa.
          static MBBTN  aButtons[] = { { IDS_SKIP, _SCANMB_SKIP },
                                       { IDS_CANCEL, _SCANMB_CANCEL } };
          _mbGeneric( hwnd, MB_NONMODAL | MB_ICONHAND, 2, aButtons,
                      IDM_RDIRFILEREPLACE, pScan->acName );
          return FALSE;
        }
        else if ( !_isDir( pScan->stFI.ulAttr ) )
        {
          // Current scan item is a file.
          if ( (ulScanFlag & _SCANFL_NOOVERWRITE) != 0 )
          {
            // Skip this file.

            // Decrease the total size by the size of the skipped file.
            pData->ullScanSize -= pScan->stFI.ullSize;
            if ( pData->hwndScanProgress != NULLHANDLE )
              WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                          MPFROMSHORT(PDVT_TOTALSIZE),
                          MPFROMP(&pData->ullScanSize) );
              
            // Remove current scan record and go to the next record.
            lnkseqRemove( &pData->lsScan, pScan );
            free( pScan );
            goto l01;
          }

          if ( (ulScanFlag & _SCANFL_OVERWRITE) == 0 )
          {
            _mbOverwrite( hwnd, pScan->acName );
            return FALSE;
          }
        }
        else
        {
          // Current scan item is a direcory.
          if ( (ulScanFlag & _SCANFL_RDIROVERWRITE) == 0 )
          {
            static MBBTN  aButtons[] = { { IDS_OK, _SCANMB_OK_RDIR },
                                         { IDS_SKIP, _SCANMB_SKIP },
                                         { IDS_CANCEL, _SCANMB_CANCEL } };
            _mbGeneric( hwnd, MB_NONMODAL | MB_QUERY, 3, aButtons,
                        IDM_OVERWRITERDIR, pScan->acName );
            return FALSE;
          }
        }
      }
    } // if ( strchr( pScan->acName, '\\' ) == NULL )

    pszLocalName = &acLocalName[cbLPath]; // Name relative current local directory.
    strcpy( pszLocalName, pScan->acName );

    if ( !_isDir( pScan->stFI.ulAttr ) )
    {
      HWND           hwndRPath;
      ULONG          cbRPath;
      PSZ            pszRemoteName;

      if ( pData->hwndScanProgress != NULLHANDLE )
        WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                 MPFROMSHORT(PDVT_FILENAME), MPFROMP(&pScan->acName) );

      if ( pData->ulScanOp == _SCAN_LDELETE )
      {
        ulRC = DosDelete( acLocalName );
        if ( ulRC != NO_ERROR )
        {
          debug( "#1 DosDelete(\"%s\"), rc = %u", acLocalName, ulRC );
          _scanError( hwnd, IDM_LFILE_DELETE_FAIL );
          return FALSE;
        }
        lnkseqRemove( &pData->lsScan, pScan );
        free( pScan );
        goto l01;
      }

      // pData->ulScanOp is _SCAN_LRCOPY or _SCAN_LRMOVE
      hwndRPath = WinWindowFromID( hwnd, IDEF_RPATH );
      cbRPath = WinQueryWindowTextLength( hwndRPath );
      pszRemoteName = alloca( cbRPath + ulRC + 1 );

      if ( pszRemoteName == NULL )
        debugCP( "Not enough stack size." );
      else
      {
        WinQueryWindowText( hwndRPath, cbRPath + 1, pszRemoteName );
        strcpy( &pszRemoteName[cbRPath], pScan->acName );
        debug( "Start copy. Local %s to remote %s", acLocalName, pszRemoteName );

        if ( !ccFXSendFile( pData->pCC, pszRemoteName, acLocalName ) )
          _scanError( hwnd, IDM_SEND_READ_FAIL );
        return FALSE;
      }
    } // if ( !_isDir( pScan->stFI.ulAttr ) )
    else
    {
      cbLPath += ulRC + 1;
      *((PULONG)&pszLocalName[ulRC]) = 0x00002A5C; // Append '\\*\0';
      pcPos = &pszLocalName[ulRC + 1];
      ulMaxFindName = sizeof(acLocalName) - ( pcPos - acLocalName );

      cFind = 1;
      ulRC = DosFindFirst( acLocalName, &hDir,
                           FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM |
                           FILE_DIRECTORY | FILE_ARCHIVED,
                           &stFind, sizeof(stFind), &cFind, FIL_STANDARDL );
      if ( ulRC != NO_ERROR )
      {
        debug( "DosFindFirst() for \"%s\", rc = %u", acLocalName, ulRC );
        return FALSE;
      }

      if ( stFind.cchName < ulMaxFindName )
      {
        strcpy( pcPos, stFind.achName );
        _os2FindBufToFInfo( &stFind, &stFI );
        _scanFilesAdd( pData, strlen( pszLocalName ), pszLocalName, &stFI );
      }

      while( ulRC != ERROR_NO_MORE_FILES )
      {
        cFind = 1;

        ulRC = DosFindNext( hDir, &stFind, sizeof(stFind), &cFind );
        if ( ulRC != NO_ERROR )
          break;

        if ( stFind.cchName < ulMaxFindName )
        {
          strcpy( pcPos, stFind.achName );
          _os2FindBufToFInfo( &stFind, &stFI );
          _scanFilesAdd( pData, strlen( pszLocalName ), pszLocalName, &stFI );
        }
      }

      DosFindClose( hDir );
      WinPostMsg( hwnd, WM_ENDOFLOCALLIST, 0, 0 );
      return FALSE;
    } // if ( !_isDir( pScan->stFI.ulAttr ) ) else
  } // if ( pScan != NULL )
  else
    while( lnkseqGetFirst( &pData->lsScanDel ) != NULL )
    {
      pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScanDel );
      strcpy( &acLocalName[cbLPath], pScan->acName );

      if ( pData->hwndScanProgress != NULLHANDLE )
        WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                    MPFROMSHORT(PDVT_FILENAME), MPFROMP(&pScan->acName) );

      debug( "Delete %s: %s",
             _isDir( pScan->stFI.ulAttr ) ? "directory" : "file", acLocalName );

      if ( pData->hwndScanProgress != NULLHANDLE )
        WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                 MPFROMSHORT(PDVT_FILENAME), MPFROMP(&pScan->acName) );

      if ( _isDir( pScan->stFI.ulAttr ) )
        DosDeleteDir( acLocalName );
      else
      {
        ulRC = DosDelete( acLocalName );
        if ( ulRC != NO_ERROR )
        {
          debug( "#2 DosDelete(\"%s\"), rc = %u", acLocalName, ulRC );
          _scanError( hwnd, IDM_LFILE_DELETE_FAIL );
          return FALSE;
        }
      }

      lnkseqRemove( &pData->lsScanDel, pScan );
      free( pScan );
    }

  // Scan lists are empty or error occurred - end of operation.
  debugCP( "Scan lists are empty or error occurred - end of operation." );
  return TRUE;
}

static VOID _scanNextStep(HWND hwnd, PDLGDATA pData)
{
  BOOL       fOpEnd;

  if ( pData->fScanCancel )
    debugCP( "Scan is canceled." );
  else
  {
    if ( pData->ulScanOp == _SCAN_LRCOPY || pData->ulScanOp == _SCAN_LRMOVE ||
         pData->ulScanOp == _SCAN_LDELETE )
    {
      // Starting obtain/delete/send the contents of a local directories.
      debugCP( "_scanLocalRequest()" );
      fOpEnd = _scanLocalRequest( hwnd, pData );
    }
    else
    {
      // Send request to the server for the first item in scan list.
      debugCP( "_scanLocalRequest()" );
      fOpEnd = _scanSendRequest( hwnd, pData );
    }

    if ( !fOpEnd )
      return;
  }

  // Scan lists are empty, error occurred or operation is canceled.
  debug( "Scan lists are empty, error occurred or operation is canceled. "
         "Operation: %u", pData->ulScanOp );

  _enableConrols( hwnd, TRUE );

  // Refresh visible files list.
  if ( ( pData->ulScanOp == _SCAN_LRMOVE ) ||
       ( pData->ulScanOp == _SCAN_LRCOPY ) ||
       ( pData->ulScanOp == _SCAN_RLMOVE ) ||
       ( pData->ulScanOp == _SCAN_RDELETE ) )
  {
    debugCP( "Refresh list of remote directory" );
    _ctlListEnter( hwnd, NULL, TRUE );     // List of remote directory.
  }

  if ( ( pData->ulScanOp == _SCAN_LRMOVE ) ||
       ( pData->ulScanOp == _SCAN_LDELETE ) ||
       ( pData->ulScanOp == _SCAN_RLMOVE ) ||
       ( pData->ulScanOp == _SCAN_RLCOPY ) )
  {
    debugCP( "Refresh list of local directory" );
    _ctlListEnter( hwnd, NULL, FALSE );    // List of local directory.
  }

  _scanFilesClean( pData );      // End any current scan-operation.
}

static BOOL _scanSkipNext(PDLGDATA pData)
{
  PSCANITEM      pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );

  if ( pScan != NULL )
    lnkseqRemove( &pData->lsScan, pScan );
  else
  {
    pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScanDel );
    if ( pScan != NULL )
      lnkseqRemove( &pData->lsScanDel, pScan );
  }

  if ( pScan == NULL )
    return FALSE;

  pData->ullScanSize -= pScan->stFI.ullSize;
  free( pScan );

  return TRUE;
}

static VOID _scanError(HWND hwnd, ULONG ulMsgId)
{
  static MBBTN  stButtons[] = { { IDS_SKIP, _SCANMB_SKIP },
                                { IDS_CANCEL, _SCANMB_CANCEL } };
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSCANITEM  pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );

  if ( pScan == NULL )
    pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScanDel );

  if ( pScan == NULL )
    debugCP( "WTF? No records in the scan list" );
  else
    _mbGeneric( hwnd, MB_NONMODAL | MB_ERROR, 2, stButtons, ulMsgId,
                pScan->acName );
}

// Compares two container records (files/directories).
static SHORT EXPENTRY __cnrComp(PRECORDCORE p1, PRECORDCORE p2, PVOID pStorage)
{
  PFILERECORD   pR1 = (PFILERECORD)p1;
  PFILERECORD   pR2 = (PFILERECORD)p2;

  // Directory ".." on top.
  if ( pR1->fUpDir && !pR2->fUpDir )
    return -1;

  // Directories before files.
  if ( _isDir( pR1->stFI.ulAttr ) && !_isDir( pR2->stFI.ulAttr ) )
    return -1;
  if ( !_isDir( pR1->stFI.ulAttr ) && _isDir( pR2->stFI.ulAttr ) )
    return 1;

  // Compare names.
  switch( WinCompareStrings( hab, 0, 0, pR1->stRecCore.pszIcon,
                             pR2->stRecCore.pszIcon, 0 ) )
  {
    case WCS_GT: return 1;
    case WCS_LT: return -1;
  }

  return 0; // Both directories or both files with same name (must not occur).
}

// Tune container hwndCtl.
static VOID _cnrSetup(HWND hwndCtl)
{
  PFIELDINFO           pFieldInfo;
  PFIELDINFO           pFldInf;
  CNRINFO              stCnrInf = { 0 };
  FIELDINFOINSERT      stFldInfIns = { 0 };
  CHAR                 acBuf[64];
  RECTL                rectCtl;

  // Fields.

  pFldInf = (PFIELDINFO)WinSendMsg( hwndCtl, CM_ALLOCDETAILFIELDINFO,
                                    MPFROMLONG( 6 ), NULL );
  if ( pFldInf == NULL )
    debugPCP( "WTF?!" );
  else
  {
    pFieldInfo = pFldInf;

    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_BITMAPORICON | CFA_LEFT | CFA_CENTER | CFA_VCENTER;
    pFieldInfo->flTitle = CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = NULL;
    pFieldInfo->offStruct = FIELDOFFSET( FILERECORD, stRecCore.hptrIcon );
    stCnrInf.pFieldInfoObject = pFieldInfo;
    pFieldInfo = pFieldInfo->pNextFieldInfo;
    stCnrInf.pFieldInfoLast = pFieldInfo;

    WinLoadString( NULLHANDLE, 0, IDS_NAME, sizeof(acBuf), acBuf );
    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT | CFA_VCENTER;
    pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = strdup( acBuf );
    pFieldInfo->offStruct = FIELDOFFSET( FILERECORD, stRecCore.pszIcon );
    pFieldInfo = pFieldInfo->pNextFieldInfo;

    WinLoadString( NULLHANDLE, 0, IDS_SIZE, sizeof(acBuf), acBuf );
    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_STRING | CFA_FIREADONLY | CFA_HORZSEPARATOR | CFA_RIGHT | CFA_VCENTER | CFA_SEPARATOR;
    pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = strdup( acBuf );
    pFieldInfo->offStruct = FIELDOFFSET( FILERECORD, pszSize );
    pFieldInfo = pFieldInfo->pNextFieldInfo;

    WinLoadString( NULLHANDLE, 0, IDS_DATE, sizeof(acBuf), acBuf );
    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_DATE | CFA_HORZSEPARATOR | CFA_RIGHT | CFA_VCENTER | CFA_SEPARATOR;
    pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = strdup( acBuf );
    pFieldInfo->offStruct = FIELDOFFSET( FILERECORD, stDate );
    pFieldInfo = pFieldInfo->pNextFieldInfo;

    WinLoadString( NULLHANDLE, 0, IDS_TIME, sizeof(acBuf), acBuf );
    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_TIME | CFA_HORZSEPARATOR | CFA_RIGHT | CFA_VCENTER | CFA_SEPARATOR;
    pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = strdup( acBuf );
    pFieldInfo->offStruct = FIELDOFFSET( FILERECORD, stTime );
    pFieldInfo = pFieldInfo->pNextFieldInfo;

    WinLoadString( NULLHANDLE, 0, IDS_ATTRIBUTES, sizeof(acBuf), acBuf );
    pFieldInfo->cb = sizeof(FIELDINFO);
    pFieldInfo->flData = CFA_STRING | CFA_FIREADONLY | CFA_HORZSEPARATOR | CFA_CENTER | CFA_VCENTER;
    pFieldInfo->flTitle = CFA_CENTER | CFA_FITITLEREADONLY;
    pFieldInfo->pTitleData = strdup( acBuf );
    pFieldInfo->offStruct = FIELDOFFSET( FILERECORD, pszAttr );

    stFldInfIns.cb = sizeof(FIELDINFOINSERT);
    stFldInfIns.pFieldInfoOrder = (PFIELDINFO)CMA_FIRST;
    stFldInfIns.cFieldInfoInsert = 6;
    WinSendMsg( hwndCtl, CM_INSERTDETAILFIELDINFO, MPFROMP( pFldInf ),
                MPFROMP( &stFldInfIns ) );
  }

  WinQueryWindowRect( hwndCtl, &rectCtl );

  stCnrInf.cb = sizeof(CNRINFO);
  stCnrInf.pSortRecord = __cnrComp;
  stCnrInf.flWindowAttr = CV_DETAIL | CA_DETAILSVIEWTITLES | CV_MINI |
                          CA_DRAWICON | CA_TITLEREADONLY/* | CFA_FITITLEREADONLY*/;
  stCnrInf.slBitmapOrIcon.cx = 16;
  stCnrInf.slBitmapOrIcon.cy = 16;
  stCnrInf.xVertSplitbar = rectCtl.xRight / 2;
  WinSendMsg( hwndCtl, CM_SETCNRINFO, MPFROMP( &stCnrInf ),
              MPFROMLONG( CMA_PSORTRECORD | CMA_PFIELDINFOLAST |
                          CMA_FLWINDOWATTR | CMA_SLBITMAPORICON |
                          CMA_XVERTSPLITBAR ) );
}

// Free memory allocated for the container in _cnrSetup().
static VOID _cnrDone(HWND hwndCtl)
{
  PFIELDINFO pFieldInfo;

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
}

// Inserts file record in the contained.
static PFILERECORD _cnrInsert(HWND hwndCtl, PDLGDATA pData, PSZ pszName,
                              PFINFO pFI)
{
  PFILERECORD   pRecord;
  RECORDINSERT  stRecIns;
  BOOL          fDir    = _isDir( pFI->ulAttr );
  BOOL          fHidden = (pFI->ulAttr & RFB_FILE_ATTRIBUTE_HIDDEN) != 0;
  BOOL          fUpDir  = fDir && strcmp( pszName, ".." ) == 0;
  PFDATE        pDate;
  PFTIME        pTime;

  // Allocate records for the container.
  pRecord = (PFILERECORD)WinSendMsg( hwndCtl, CM_ALLOCRECORD,
                 MPFROMLONG( sizeof(FILERECORD) - sizeof(MINIRECORDCORE) ),
                 MPFROMLONG( 1 ) );
  if ( pRecord == NULL )
    return NULL;

  pRecord->stRecCore.hptrIcon = fUpDir ? NULLHANDLE
                                  : fHidden ? ( fDir ? pData->hptrFolderHidden
                                                     : pData->hptrFileHidden )
                                            : ( fDir ? pData->hptrFolder
                                                     : pData->hptrFile );
  pRecord->stRecCore.pszIcon  = pRecord->szName;
  pRecord->pszSize            = pRecord->szSize;
  pRecord->pszAttr            = pRecord->szAttr;

  if ( fDir )
  {
    pDate = &pFI->fdateLastAccess;
    pTime = &pFI->ftimeLastAccess;
  }
  else
  {
    pDate = &pFI->fdateLastWrite;
    pTime = &pFI->ftimeLastWrite;
  }

  pRecord->stDate.day         = pDate->day;
  pRecord->stDate.month       = pDate->month;
  pRecord->stDate.year        = pDate->year + 1980;
  pRecord->stTime.hours       = pTime->hours;
  pRecord->stTime.minutes     = pTime->minutes;
  pRecord->stTime.seconds     = pTime->twosecs * 2;
  pRecord->stTime.ucReserved  = 0;

  if ( fDir )
    strcpy( pRecord->szSize, "<DIR>" );
  else
    utilStrFromBytes( pFI->ullSize, sizeof(pRecord->szSize), pRecord->szSize );

  strlcpy( pRecord->szName, pszName, CCHMAXPATHCOMP );
  pRecord->fUpDir  = fUpDir;
  pRecord->stFI    = *pFI;

  if ( fUpDir )
    pRecord->szAttr[0] = '\0';
  else
  {
    pRecord->szAttr[0] = (pFI->ulAttr & RFB_FILE_ATTRIBUTE_ARCHIVE)  == 0 ? '-' : 'A';
    pRecord->szAttr[1] = !fDir ? '-' : 'D';
    pRecord->szAttr[2] = (pFI->ulAttr & RFB_FILE_ATTRIBUTE_SYSTEM)   == 0 ? '-' : 'S';
    pRecord->szAttr[3] = !fHidden ? '-' : 'H';
    pRecord->szAttr[4] = (pFI->ulAttr & RFB_FILE_ATTRIBUTE_READONLY) == 0 ? '-' : 'R';
    pRecord->szAttr[5] = '\0';
  }

  // Insert record to the container.
  stRecIns.cb                 = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder       = (PRECORDCORE)CMA_END;
  stRecIns.pRecordParent      = NULL;
  stRecIns.zOrder             = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert     = 1;
  stRecIns.fInvalidateRecord  = TRUE;
  WinSendMsg( hwndCtl, CM_INSERTRECORD, (PRECORDCORE)pRecord, &stRecIns );

  return pRecord;
}

static VOID _cnrInsertLocal(HWND hwndCtl, PDLGDATA pData, PFILEFINDBUF3L pFind)
{
  FINFO      stFI;

  if ( pFind->cchName == 1 && pFind->achName[0] == '.' &&
       (pFind->attrFile & FILE_DIRECTORY) != 0 )
    return;

  _os2FindBufToFInfo( pFind, &stFI );
  _cnrInsert( hwndCtl, pData, pFind->achName, &stFI );
}

// Clean files list container.
static VOID _cnrClear(HWND hwndCtl)
{
  WinSendMsg( hwndCtl, CM_CLOSEEDIT, 0, 0 );
  WinSendMsg( hwndCtl, CM_REMOVERECORD, MPFROMP(NULL),
              MPFROM2SHORT(0,CMA_FREE | CMA_INVALIDATE) );
}

static PFILERECORD _cnrSearch(HWND hwndCtl, PSZ pszName)
{
  PFILERECORD          pRecord = NULL;

  do
  {
    pRecord = (PFILERECORD)WinSendMsg( hwndCtl, CM_QUERYRECORD,
                                       MPFROMP(pRecord),
                                       pRecord == NULL
                                         ? MPFROM2SHORT(CMA_FIRST,CMA_ITEMORDER)
                                         : MPFROM2SHORT(CMA_NEXT,CMA_ITEMORDER)
                                     );
    if ( ( pRecord == NULL ) || ( pRecord == ((PFILERECORD)(-1)) ) )
      break;
  }
  while( WinCompareStrings( hab, 0, 0, pszName, pRecord->stRecCore.pszIcon, 0 )
           != WCS_EQ );

  return pRecord;
}


// Send files-list request to the server or read local files list.
static VOID _queryFileList(HWND hwnd, PSZ pszPath, BOOL fRemote)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSZ                  pszFind;
  ULONG                cbPath;
  FILEFINDBUF3L        stFind;
  HDIR                 hDir = HDIR_CREATE;
  ULONG                ulRC, cFind;
  HWND                 hwndCtl;

  if ( fRemote )
  {
    // Query remote files list.

    if ( pData->fFileListRequested ||
         !ccFXRequestFileList( pData->pCC, pszPath ) )
      return;
    _enableConrols( hwnd, FALSE );
    pData->fFileListRequested = TRUE;
    return;
  }

  // Get local files list.

  cbPath = STR_LEN( pszPath );
  if ( ( cbPath > 0 ) && ( pszPath[cbPath - 1] == '\\' ) )
    cbPath--;

  pszFind = alloca( cbPath + 4 );
  if ( pszFind == NULL )
  {
    debugCP( "Not enough stack space" );
    return;
  }
  memcpy( pszFind, pszPath, cbPath );
  *((PULONG)&pszFind[cbPath]) = 0x00002A5C; // Append '\\*\0';

  cFind = 1;
  ulRC = DosFindFirst( pszFind, &hDir,
                       FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM |
                       FILE_DIRECTORY | FILE_ARCHIVED,
                       &stFind, sizeof(stFind), &cFind, FIL_STANDARDL );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosFindFirst() for \"%s\", rc = %u", pszFind, ulRC );
    return;
  }

  *((PULONG)&pszFind[cbPath]) = 0x0000005C; // Append '\\';
  WinSetDlgItemText( hwnd, IDEF_LPATH, pszFind );
  hwndCtl = WinWindowFromID( hwnd, IDCN_LLIST );
  _cnrClear( hwndCtl );

  // Don't insert directory ".." in list of root directory.
  if ( *((PULONG)&pszFind[1]) != 0x0005C3A /* :\\\0\0 */ ||
       (stFind.attrFile & FILE_DIRECTORY) == 0 ||
       stFind.cchName != 2 || *((PUSHORT)stFind.achName) != 0x2E2E /* .. */ )
    _cnrInsertLocal( hwndCtl, pData, &stFind );

  while( ulRC != ERROR_NO_MORE_FILES )
  {
    cFind = 1;

    ulRC = DosFindNext( hDir, &stFind, sizeof(stFind), &cFind );
    if ( ( ulRC != NO_ERROR ) /*&& ( ulRC != ERROR_NO_MORE_FILES )*/ )
      break;

    // Don't insert directory ".." in list of root directory.
    if ( *((PULONG)&pszFind[1]) != 0x0005C3A /* :\\\0\0 */ ||
       (stFind.attrFile & FILE_DIRECTORY) == 0 ||
         stFind.cchName != 2 || *((PUSHORT)stFind.achName) != 0x2E2E /* .. */ )
      _cnrInsertLocal( hwndCtl, pData, &stFind );
  }

  DosFindClose( hDir );
}

static BOOL _isCtlFocused(HWND hwnd)
{
  HENUM      henum;
  HWND       hwndEnum;
  HWND       hwndFocus = WinQueryFocus( HWND_DESKTOP );

  if ( hwnd == hwndFocus )
    return TRUE;

  henum = WinBeginEnumWindows( hwnd );
  while( ( hwndEnum = WinGetNextWindow( henum ) ) != NULLHANDLE )
  {
    if ( hwndEnum == hwndFocus )
      break;
  }
  WinEndEnumWindows( henum );

  return hwndEnum != NULLHANDLE;
}

static BOOL _isRemotePanelActived(HWND hwnd)
{
  static ULONG         aRCtlIDs[] = { IDCN_RLIST, IDCB_RDRIVES, IDPB_RUPDIR,
                                      IDPB_RROOT, IDEF_RPATH };
  ULONG                ulIdx;

  for( ulIdx = 0; ulIdx < ARRAYSIZE(aRCtlIDs); ulIdx++ )
  {
    if ( _isCtlFocused( WinWindowFromID( hwnd, aRCtlIDs[ulIdx] ) ) )
      return TRUE;
  }

  return FALSE;
}

// ULONG _queryActiveSelection(HWND hwnd)
//
// Returns: _ACTIVESEL_NONE   - no any items selected.
//          _ACTIVESEL_REMOTE - remote panel actived and item(s) is selected,
//          _ACTIVESEL_LOCAL  - local panel actived and item(s) is selected,

#define _ACTIVESEL_NONE          0
#define _ACTIVESEL_REMOTE        1
#define _ACTIVESEL_LOCAL         2

static ULONG _queryActiveSelection(HWND hwnd)
{
  PFILERECORD          pRecord = (PFILERECORD)CMA_FIRST;
  BOOL     fRemote = _isRemotePanelActived( hwnd );
  HWND     hwndCN = WinWindowFromID( hwnd, fRemote ? IDCN_RLIST : IDCN_LLIST );

  do
  {
    pRecord = (PFILERECORD)WinSendMsg( hwndCN, CM_QUERYRECORDEMPHASIS,
                                 MPFROMP(pRecord), MPFROMSHORT(CRA_SELECTED) );
    if ( ( pRecord == NULL ) || ( pRecord == ((PFILERECORD)(-1)) ) )
      return _ACTIVESEL_NONE;
  }
  while( pRecord->fUpDir );

  return fRemote ? _ACTIVESEL_REMOTE : _ACTIVESEL_LOCAL;
}


static PSZ _queryDiskType(ULONG ulDrv)
{
  ULONG			ulRC;
  #pragma pack(1)
  struct {
    UCHAR	ucCmd;
    UCHAR	ucDrv;
  }			stParms;
  #pragma pack()
  ULONG			cbParms, cbData;
  BIOSPARAMETERBLOCK	stBPB;
  BYTE                  bFixed;
  PSZ                   pszType;

  // Is fixed disk (bFixed) ?
  stParms.ucDrv = ulDrv;
  ulRC = DosDevIOCtl( (HFILE)(-1), IOCTL_DISK, DSK_BLOCKREMOVABLE, &stParms,
                      sizeof(stParms), &cbParms, &bFixed, 1, &cbData );
  if ( ulRC == NO_ERROR )
  {
    pszType = bFixed == 0 ? "removable" : "local";

    stParms.ucCmd = 0; 
    ulRC = DosDevIOCtl( (HFILE)(-1), IOCTL_DISK, DSK_GETDEVICEPARAMS,
                        &stParms, sizeof(stParms), &cbParms,
                        &stBPB, sizeof(stBPB), &cbData );
    if ( ulRC == NO_ERROR )
    {
      if ( (stBPB.fsDeviceAttr & 0x08) != 0 ) // Removable flag (flash drive?)
        pszType = "removable";
      else if ( ( stBPB.bDeviceType == 7 ) &&
                ( stBPB.usBytesPerSector == 2048 ) &&
                ( stBPB.usSectorsPerTrack == (USHORT)(-1) ) )
        pszType = "CD/DVD";
      else
      {
        switch ( stBPB.bDeviceType )
        {
          case DEVTYPE_TAPE:      // 6
            pszType = "tape"; // Tape
            break;

          case DEVTYPE_UNKNOWN:
            if ( ( stBPB.usBytesPerSector == 2048 ) &&
                 ( stBPB.usSectorsPerTrack == (USHORT)(-1) ) )
            {
              pszType = "CD"; // CD
              break;
            }
            // 7, 1.44/1.84M  3.5" floppy ?
          case DEVTYPE_48TPI:     // 0, 360k  5.25" floppy
          case DEVTYPE_96TPI:     // 1, 1.2M  5.25" floppy
          case DEVTYPE_35:        // 2, 720k  3.5" floppy
          case 9:                 // 9, 288MB 3.5" floppy?
            pszType = ulDrv <= 1 ? "floppy" : "video disk";
            break;

          case 8:                 // CD/DVD?
            if ( ( stBPB.usBytesPerSector == 2048 ) &&
                 ( stBPB.usSectorsPerTrack == (USHORT)(-1) ) )
              pszType = "CD/DVD";
            break;
        }
      }
    }
  }
  else
    pszType = ulRC == ERROR_NOT_SUPPORTED ? "remote" : "";

  return pszType;
}

static BOOL _wmInitDlg(HWND hwnd, PDLGINITDATA pInitData)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCB_LDRIVES );
  CHAR       acBuf[256];
  ULONG      ulDiskCur, ulDiskMap, ulIdx;
  PDLGDATA   pData = calloc( 1, sizeof(DLGDATA) );
  HACCEL     hAccel;

  if ( pData == NULL )
    return FALSE;

  pData->pCC        = pInitData->pCC;
  pData->hptrFile   = WinLoadPointer( HWND_DESKTOP, 0, IDICON_FILE );
  pData->hptrFolder = WinLoadPointer( HWND_DESKTOP, 0, IDICON_FOLDER );
  pData->hptrFileHidden   = WinLoadPointer( HWND_DESKTOP, 0, IDICON_FILE_HIDDEN );
  pData->hptrFolderHidden = WinLoadPointer( HWND_DESKTOP, 0, IDICON_FOLDER_HIDDEN );
  lnkseqInit( &pData->lsScan );
  lnkseqInit( &pData->lsScanDel );

  WinSetWindowULong( hwnd, QWL_USER, (ULONG)pData );

  _cnrSetup( WinWindowFromID( hwnd, IDCN_LLIST ) );
  _cnrSetup( WinWindowFromID( hwnd, IDCN_RLIST ) );

  // Set window title.
  ulIdx = WinQueryWindowText( hwnd, sizeof(acBuf), acBuf );
  *((PULONG)&acBuf[ulIdx]) = 0x00202D20; // " - "
  strlcpy( &acBuf[ulIdx+3], pInitData->pszServer, sizeof(acBuf) - ulIdx - 3 );
  WinSetWindowText( hwnd, acBuf );

  // Set accelerator table.
  hAccel = WinLoadAccelTable( hab, NULLHANDLE, IDDLG_FILEXFER );
  WinSetAccelTable( hab, hAccel, hwnd );

  // Load context menu.
  pData->hwndCtxMenu = WinLoadMenu( HWND_DESKTOP, 0, IDDLG_FILEXFER );

  // Fill local drives list.
  if ( DosQueryCurrentDisk( &ulDiskCur, &ulDiskMap ) == NO_ERROR )
  {
    SHORT  sItemIdx;

    for( ulIdx = 2; ulIdx < 26; ulIdx++ )
      if ( (( ulDiskMap << ( 31 - ulIdx ) ) >> 31) != 0 )
      {
        // Make item text. Drive name like "D: ".
        *((PULONG)acBuf) = 0x00203A41L + ulIdx; // 'A: \0' + ulIdx
        // Append drive type (local/removable/remote/CD/e.t.c.).
        strcpy( &acBuf[3], _queryDiskType( ulIdx ) );

        sItemIdx = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_INSERTITEM,
                                      MPFROMSHORT(LIT_END), acBuf ) );

        // Item's handle is a drive name string 'D:'.
        if ( ( sItemIdx != LIT_MEMERROR ) && ( sItemIdx != LIT_ERROR ) )
        {
          acBuf[2] = '\0';
          WinSendMsg( hwndCtl, LM_SETITEMHANDLE,
                      MPFROMSHORT(sItemIdx), MPFROMLONG( *((PULONG)acBuf) ) );
        }
      }
    WinSendMsg( hwndCtl, LM_SELECTITEM, MPFROMSHORT(0), MPFROMSHORT(0xFFFF) );
  }

  WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ACTIVATE | SWP_SHOW );

  return TRUE;
}

static VOID _wmDestory(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  WinDestroyAccelTable( WinQueryAccelTable( hab, hwnd ) );
  WinDestroyWindow( pData->hwndCtxMenu );
  _cnrDone( WinWindowFromID( hwnd, IDCN_LLIST ) );
  _cnrDone( WinWindowFromID( hwnd, IDCN_RLIST ) );

  if ( pData == NULL )
    return;

  _scanFilesClean( pData );

  if ( pData->hptrFile != NULLHANDLE )
    WinDestroyPointer( pData->hptrFile );

  if ( pData->hptrFolder != NULLHANDLE )
    WinDestroyPointer( pData->hptrFolder );

  free( pData );
}

static VOID _ctlDriveSelected(HWND hwnd, HWND hwndCtl, BOOL fRemote)
{
  USHORT     usIdx;
  CHAR       acBuf[CCHMAXPATHCOMP];
  CHAR       acCurPath[CCHMAXPATH];

  usIdx = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_QUERYSELECTION,
                                    MPFROMSHORT(LIT_FIRST), 0 ) );

  // Query current path from read-only entry field.
  WinQueryDlgItemText( hwnd, fRemote ? IDEF_RPATH : IDEF_LPATH,
                       sizeof(acCurPath), acCurPath );
  // Query selected drive from combobox. Item's handle is string 'D:' or 'D:\'.
  *((PULONG)acBuf) = LONGFROMMR( WinSendMsg( hwndCtl, LM_QUERYITEMHANDLE,
                                             MPFROM2SHORT(usIdx,0), 0 ) );

  if ( *((PUSHORT)acBuf) != *((PUSHORT)acCurPath) )
    // Drive changed.
    _queryFileList( hwnd, acBuf, fRemote );
}

// Read and show remote or local directory pointed by pRecord.
// If pRecord is NULL - refresh current files list in the container.
static VOID _ctlListEnter(HWND hwnd, PFILERECORD pRecord, BOOL fRemote)
{
  HWND    hwndCtl = WinWindowFromID( hwnd, fRemote ? IDEF_RPATH : IDEF_LPATH );
  PSZ     pszPath;
  PCHAR   pcPos;
  ULONG   cbName, cbPath;

  if ( pRecord != NULL )
  {
    if ( pRecord->fUpDir )
    {
      WinSendMsg( hwnd, WM_COMMAND,
                  fRemote ? MPFROMSHORT(IDPB_RUPDIR) : MPFROMSHORT(IDPB_LUPDIR),
                  MPFROM2SHORT(CMDSRC_OTHER,0) );
      return;
    }

    if ( !_isDir( pRecord->stFI.ulAttr ) )
      return;

    cbName = strlen( pRecord->szName );
  }
  else
    cbName = 0;

  cbPath = WinQueryWindowTextLength( hwndCtl ) + cbName + 2;
  pszPath = alloca( cbPath );
  if ( pszPath == NULL )
  {
    debugCP( "Not enough stack space" );
    return;
  }

  pcPos = pszPath + WinQueryWindowText( hwndCtl, cbPath, pszPath );
  if ( cbName != 0 )
  {
    memcpy( pcPos, pRecord->szName, cbName );
    *((PUSHORT)&pcPos[cbName]) = 0x005C; // Append '\\\0'.
  }

  debug( "_queryFileList(,\"%s\",%u)", pszPath, fRemote );
  _queryFileList( hwnd, pszPath, fRemote );
}

// Reads and show parent or root local or remote directory.
static VOID _cmdUpDir(HWND hwnd, BOOL fJumpToRoot, BOOL fRemote)
{
  HWND       hwndCtl = WinWindowFromID( hwnd,
                                        fRemote ? IDEF_RPATH : IDEF_LPATH );
  ULONG      cbPath  = WinQueryWindowTextLength( hwndCtl );
  PSZ        pszPath;
  PCHAR      pcPos;

  if ( cbPath == 0 )
    return;

  pszPath = alloca( cbPath + 1 );
  if ( pszPath == NULL )
  {
    debugCP( "Not enough stack space" );
    return;
  }

  WinQueryWindowText( hwndCtl, cbPath + 1, pszPath );
  pcPos = strchr( pszPath, '\\' );
  if ( ( pcPos == NULL ) || ( pcPos[1] == '\0' ) )
    // Current directory is root.
    return;

  if ( !fJumpToRoot )
  {
    pcPos = &pszPath[cbPath - 2];
    while( ( pcPos > pszPath ) && ( *(pcPos - 1) != '\\' ) )
      pcPos--;
  }
  else
    pcPos++;

  *pcPos = '\0';
  _queryFileList( hwnd, pszPath, fRemote );
}

// Start operation on selected files/directories (copy/move/delete).
// ulOp is a operation _SCAN_xxxxx.
static VOID _cmdScanOp(HWND hwnd, ULONG ulOp)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( ( WinQueryDlgItemTextLength( hwnd, IDEF_LPATH ) == 0 ) ||
       ( WinQueryDlgItemTextLength( hwnd, IDEF_RPATH ) == 0 ) )
    return;

  _scanFilesClean( pData );

  _scanFilesAddSelected(
       WinWindowFromID( hwnd,
         ulOp == _SCAN_LRCOPY || ulOp == _SCAN_LRMOVE || ulOp == _SCAN_LDELETE
           ? IDCN_LLIST : IDCN_RLIST ),
       pData );

  if ( lnkseqGetCount( &pData->lsScan ) == 0 )
    return;

  pData->ulScanOp = ulOp;
  _enableConrols( hwnd, FALSE );

  // Show progress window.
  pData->hwndScanProgress = _createProgressDlg( hwnd, ulOp );
  WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
              MPFROMSHORT(PDVT_TOTALSIZE), MPFROMP(&pData->ullScanSize) );

  _scanNextStep( hwnd, pData );
}

static VOID _cmdMkDir(HWND hwnd, BOOL fRemote)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND                 hwndCtl = WinWindowFromID( hwnd,
                                           fRemote ? IDCN_RLIST : IDCN_LLIST );
  FINFO                stFI;
  time_t               timeFile = time( NULL );
  CNREDITDATA          stCnrEditData;

  _ftUnixToOS2( timeFile, &stFI.fdateCreation, &stFI.ftimeCreation );
  stFI.fdateLastAccess = stFI.fdateCreation;
  stFI.ftimeLastAccess = stFI.ftimeCreation;
  stFI.fdateLastWrite = stFI.fdateLastWrite;
  stFI.ftimeLastWrite = stFI.ftimeLastWrite;
  stFI.ulAttr = RFB_FILE_ATTRIBUTE_DIRECTORY;
  stFI.ullSize = 0;

  memset( &stCnrEditData, '\0', sizeof(CNREDITDATA) );
  stCnrEditData.cb = sizeof(CNREDITDATA);
  stCnrEditData.hwndCnr = hwndCtl;
  stCnrEditData.pRecord =
    (PRECORDCORE)_cnrInsert( hwndCtl, pData, "New Folder", &stFI );
  stCnrEditData.id = CID_LEFTDVWND;
  // Set stCnrEditData.pFieldInfo to the second field "Name".
  stCnrEditData.pFieldInfo =
    (PFIELDINFO)WinSendMsg( hwndCtl, CM_QUERYDETAILFIELDINFO, 0,
                            MPFROMSHORT(CMA_FIRST) );
  stCnrEditData.pFieldInfo =
    (PFIELDINFO)WinSendMsg( hwndCtl, CM_QUERYDETAILFIELDINFO,
                  MPFROMP(stCnrEditData.pFieldInfo), MPFROMSHORT( CMA_NEXT ) );

  if ( (BOOL)WinSendMsg( hwndCtl, CM_OPENEDIT, MPFROMP(&stCnrEditData), 0 ) )
    pData->pEditRecord = (PFILERECORD)stCnrEditData.pRecord;
}

static VOID _cmdRename(HWND hwnd)
{
  HWND          hwndCtl = WinWindowFromID( hwnd, _isRemotePanelActived( hwnd )
                                                   ? IDCN_RLIST : IDCN_LLIST );
  PFILERECORD   pRecord;
  CNREDITDATA   stCnrEditData;

  pRecord = (PFILERECORD)WinSendMsg( hwndCtl, CM_QUERYRECORDEMPHASIS,
                                     MPFROMLONG(CMA_FIRST),
                                     MPFROMSHORT(CRA_CURSORED) );
  if ( ( pRecord != NULL ) && ( !pRecord->fUpDir ) )
  {
    memset( &stCnrEditData, '\0', sizeof(CNREDITDATA) );
    stCnrEditData.cb          = sizeof(CNREDITDATA);
    stCnrEditData.hwndCnr     = hwndCtl;
    stCnrEditData.pRecord     = (PRECORDCORE)pRecord;
    stCnrEditData.id          = CID_LEFTDVWND;
    stCnrEditData.pFieldInfo  = NULL;

    do
      stCnrEditData.pFieldInfo = (PFIELDINFO)
        WinSendMsg( hwndCtl, CM_QUERYDETAILFIELDINFO,
                    MPFROMP(stCnrEditData.pFieldInfo),
                    stCnrEditData.pFieldInfo == NULL
                      ? MPFROMSHORT(CMA_FIRST) : MPFROMSHORT(CMA_NEXT) );
    while( _IS_FLD_VALID( stCnrEditData.pFieldInfo ) &&
           ( stCnrEditData.pFieldInfo->offStruct !=
               FIELDOFFSET( FILERECORD, stRecCore.pszIcon ) ) );

    if ( _IS_FLD_VALID( stCnrEditData.pFieldInfo ) )
      WinSendMsg( hwndCtl, CM_OPENEDIT, MPFROMP(&stCnrEditData), 0 );
  }
}

static VOID _cmdOpenFolder(HWND hwnd)
{
#ifndef OPEN_DEFAULT
#define OPEN_DEFAULT   0
#endif
  HWND       hwndCtl = WinWindowFromID( hwnd, IDEF_LPATH );
  CHAR       acPath[CCHMAXPATH];
  ULONG      cbPath = WinQueryWindowText( hwndCtl, sizeof(acPath), acPath );
  HOBJECT    hObject;
  PSZ        pszObject;

  if ( cbPath == 0 )
    return;

  if ( cbPath <= 3 )
  {
    sprintf( &acPath[16], "<WP_DRIVE_%c>", acPath[0] );
    pszObject = &acPath[16];
  }
  else
  {
    acPath[cbPath-1] = '\0';     // Remove trailing slash.
    pszObject = acPath;
  }

  hObject = WinQueryObject( pszObject );
  // We use 1 for 3th argument of WinOpenObject(), TRUE redefined in rfbproto.h
  if ( ( hObject != NULLHANDLE ) &&
       WinOpenObject( hObject, OPEN_DEFAULT, 1 ) )
    WinOpenObject( hObject, OPEN_DEFAULT, 1 ); // Move to the fore.
}

static VOID _cmdCnrEndEdit(HWND hwnd, PCNREDITDATA pCnrEditData)
{
  PDLGDATA       pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  BOOL           fRemote = WinQueryWindowUShort( pCnrEditData->hwndCnr,
                                                 QWS_ID ) == IDCN_RLIST;
  PFILERECORD    pRecord = (PFILERECORD)pCnrEditData->pRecord;
  PSZ            pszNewName = *pCnrEditData->ppszText;
  PCHAR          pcPos, pcEnd = strchr( pszNewName, '\0' );
  HWND           hwndCtl = WinWindowFromID( hwnd,
                                            fRemote ? IDEF_RPATH : IDEF_LPATH );
  ULONG          cbPath = WinQueryWindowTextLength( hwndCtl );
  PSZ            pszFullNewName;
  ULONG          ulRC;

  // Make full new name for the file/directory.

  while( ( pcEnd > pszNewName ) && isspace( *(pcEnd-1) ) ) pcEnd--;
  while( ( pcEnd > pszNewName ) && isspace( *pszNewName ) ) pszNewName++;

  pszFullNewName = alloca( cbPath + ( pcEnd - pszNewName ) + 1 );
  if ( pszFullNewName == NULL )
  {
    debugCP( "Not enough stack space" );
    return;
  }
  WinQueryWindowText( hwndCtl, cbPath + 1, pszFullNewName );
  for( pcPos = &pszFullNewName[cbPath]; pszNewName < pcEnd; pszNewName++ )
  {
    if ( *pszNewName == '\r' )
      continue;
    *pcPos = *pszNewName == '\n' ? ' ' : *pszNewName;
    pcPos++;
  }
  *pcPos = '\0';


  // For the new directory pData->pEditRecord has been set in _cmdMkDir().
  if ( pData->pEditRecord == pRecord )
  {
    // The name of the new directory is entered (creating directory).

    if ( !fRemote )
    {
      ulRC = DosCreateDir( pszFullNewName, NULL );
      debug( "DosCreateDir(\"%s\",NULL), rc = %u", pszFullNewName, ulRC );
      if ( ulRC == NO_ERROR )
        _ctlListEnter( hwnd, NULL, FALSE );
      else
      {
        _enableConrols( hwnd, FALSE );
        _mbError( hwnd, IDM_LMKDIR_FAIL, pszFullNewName );
      }

      return;
    }

    // Send request to the server.
    // Result will be returned with WM_VNC_FILEXFER, mp1: CFX_MKDIR_RES.
    if ( !ccFXMkDir( pData->pCC, pszFullNewName ) )
      return;
  }
  else
  {
    // The new name for file/directory is entered (renaming).

    PSZ      pszFullOldName;

    if ( strcmp( pData->acOldName, &pszFullNewName[cbPath] ) == 0 )
      // Text was not changed.
      return;

    // Make full old name for the remote file/directory.
    pszFullOldName = alloca( cbPath + 1 + strlen( pData->acOldName ) );
    if ( pszFullOldName == NULL )
    {
      debugCP( "Not enough stack space" );
      return;
    }
    memcpy( pszFullOldName, pszFullNewName, cbPath );
    strcpy( &pszFullOldName[cbPath], pData->acOldName );

    if ( !fRemote )
    {
      ulRC = DosMove( pszFullOldName, pszFullNewName );
      debug( "DosMove(\"%s\",\"%s\"), rc = %u",
             pszFullOldName, pszFullNewName, ulRC );
      if ( ulRC == NO_ERROR )
        _ctlListEnter( hwnd, NULL, FALSE );
      else
      {
        _enableConrols( hwnd, FALSE );
        _mbError( hwnd, IDM_LRENAME_FAIL, pszFullNewName );
      }

      return;
    }

    // Send request to the server.
    // Result will be returned with WM_VNC_FILEXFER, mp1: CFX_RENAME_RES.
    if ( !ccFXRename( pData->pCC, pszFullOldName, pszFullNewName ) )
      return;

    pData->pEditRecord = (PFILERECORD)pRecord;
  }

  _enableConrols( hwnd, FALSE );
}

static VOID _cmdContextMenu(HWND hwnd, USHORT usCNId, PFILERECORD pMenuRecord)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND                 hwndCN = WinWindowFromID( hwnd, usCNId );
  POINTL               pt;
  BOOL                 fSelected;
  PFILERECORD          pRecord = (PFILERECORD)CMA_FIRST;

  if ( ( pMenuRecord != NULL ) && pMenuRecord->fUpDir )
    pMenuRecord = NULL;

  if ( pMenuRecord != NULL )
  {
    BOOL     fOnSelectedItem = FALSE;

    // Check: is menu's item one of selected items?
    while( TRUE )
    {
      pRecord = (PFILERECORD)WinSendMsg( hwndCN, CM_QUERYRECORDEMPHASIS,
                                   MPFROMP(pRecord), MPFROMSHORT(CRA_SELECTED) );
      if ( ( pRecord == NULL ) || ( pRecord == ((PFILERECORD)(-1)) ) )
      {
        pRecord = NULL;
        break;
      }

      if ( !pRecord->fUpDir && ( pRecord == pMenuRecord ) )
      {
        fOnSelectedItem = TRUE;
        break;
      }
    }

    if ( !fOnSelectedItem )
    {
      // Menu's item is not selected - reset selection.
      pRecord = (PFILERECORD)CMA_FIRST;
      while( TRUE )
      {
        pRecord = (PFILERECORD)WinSendMsg( hwndCN, CM_QUERYRECORDEMPHASIS,
                                     MPFROMP(pRecord), MPFROMSHORT(CRA_SELECTED) );
        if ( ( pRecord == NULL ) || ( pRecord == ((PFILERECORD)(-1)) ) )
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

  pRecord = (PFILERECORD)WinSendMsg( hwndCN, CM_QUERYRECORDEMPHASIS,
                            MPFROMLONG(CMA_FIRST), MPFROMSHORT(CRA_SELECTED) );
  fSelected = ( pRecord != NULL ) && ( pRecord != ((PFILERECORD)(-1)) ) &&
              !pRecord->fUpDir;

  WinSendMsg( pData->hwndCtxMenu, MM_SETITEMATTR,
              MPFROM2SHORT( IDMI_COPY, TRUE ),
              MPFROM2SHORT( MIA_DISABLED, fSelected ? 0 : MIA_DISABLED ) );
  WinSendMsg( pData->hwndCtxMenu, MM_SETITEMATTR,
              MPFROM2SHORT( IDMI_MOVE, TRUE ),
              MPFROM2SHORT( MIA_DISABLED, fSelected ? 0 : MIA_DISABLED ) );
  WinSendMsg( pData->hwndCtxMenu, MM_SETITEMATTR,
              MPFROM2SHORT( IDMI_DELETE, TRUE ),
              MPFROM2SHORT( MIA_DISABLED, fSelected ? 0 : MIA_DISABLED ) );

  WinSendMsg( pData->hwndCtxMenu, MM_SETITEMATTR,
              MPFROM2SHORT( IDMI_RENAME, TRUE ),
              MPFROM2SHORT( MIA_DISABLED,
                            pMenuRecord != NULL ? 0 : MIA_DISABLED ) );

  WinSendMsg( pData->hwndCtxMenu, MM_SETITEMATTR,
              MPFROM2SHORT( IDMI_OPENFOLDER, TRUE ),
              MPFROM2SHORT( MIA_DISABLED,
                            usCNId == IDCN_LLIST ? 0 : MIA_DISABLED ) );

  WinSendMsg( hwndCN, CM_CLOSEEDIT, 0, 0 );          // Close editor.
  WinQueryMsgPos( hab, &pt );                        // Detect menu position.
  WinPopupMenu( HWND_DESKTOP, hwnd, pData->hwndCtxMenu, pt.x, pt.y,
                IDMI_COPY, PU_HCONSTRAIN | PU_VCONSTRAIN | PU_MOUSEBUTTON1 |
                PU_MOUSEBUTTON2 | PU_KEYBOARD );
}

// Pressing the button in the progress dialog or message box.
static VOID _wmMsgBoxDisMiss(HWND hwnd, HWND hwndScanMsgBox, ULONG ulButtonId)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  // hwndScanMsgBox may be NULLHANDLE when an error occurred during the
  // creation of message box at _mbGeneric().
  if ( hwndScanMsgBox != NULLHANDLE )
    WinDestroyWindow( hwndScanMsgBox );

  switch( ulButtonId )
  {
    case _SCANMB_OK:
      pData->ulScanFlag = _SCANFL_OVERWRITE;
      break;

    case _SCANMB_OK_RDIR:
      pData->ulScanFlag = _SCANFL_RDIROVERWRITE;
      break;

    case _SCANMB_ALL:
      pData->ulScanFlag = _SCANFL_OVERWRITE | _SCANFL_FORALL;
      break;

    case _SCANMB_NONE:
      pData->ulScanFlag = _SCANFL_NOOVERWRITE | _SCANFL_FORALL;
      break;

    case _SCANMB_SKIP:
      if ( _scanSkipNext( pData ) )
      {
        if ( pData->hwndScanProgress != NULLHANDLE )
          WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                   MPFROMSHORT(PDVT_TOTALSIZE), MPFROMP(&pData->ullScanSize) );
        break;
      }
      debugCP( "_scanSkipNext() - FAILED" );
      // Empty list? Go to _SCANMB_CANCEL.

    case _SCANMB_CANCEL:
      pData->fScanCancel = TRUE;
      break;

    // Confirmation message box ( _mbConfirm() ) buttons.

    case _SCANMB_YES_RDELETE:
      _cmdScanOp( hwnd, _SCAN_RDELETE );
      return;

    case _SCANMB_YES_LDELETE:
      _cmdScanOp( hwnd, _SCAN_LDELETE );
      return;

    case _SCANMB_YES_RLMOVE:
      _cmdScanOp( hwnd, _SCAN_RLMOVE );
      return;

    case _SCANMB_YES_LRMOVE:
      _cmdScanOp( hwnd, _SCAN_LRMOVE );
      return;

    case _SCANMB_NO:
      return;
  }

  if ( pData->hwndScanProgress != NULLHANDLE )
    WinEnableWindow( pData->hwndScanProgress, TRUE );

  // Resume or cancel curent operation.
  _scanNextStep( hwnd, pData );
}

// Fill remote drives combobox.
static VOID _filexferDrives(HWND hwnd, PSZ pszList)
{
  HWND       hwndCtl = WinWindowFromID( hwnd, IDCB_RDRIVES );
  PCHAR      pcPos;
  CHAR       acBuf[16];
  ULONG      sItemIdx;

  if ( pszList == NULL )
    return;

  WinSendMsg( hwndCtl, LM_DELETEALL, 0, 0 );

  for( pcPos = pszList; *pcPos != '\0'; pcPos += strlen( pcPos ) + 1 )
  {
    *((PUSHORT)acBuf) = *((PUSHORT)pcPos);
    strcpy( &acBuf[2],
            pcPos[2] == 'l' ? " local" :
            pcPos[2] == 'f' ? " removable" :
            pcPos[2] == 'c' ? " CD/DVD" :
            pcPos[2] == 'n' ? " remote" : "" );

    sItemIdx = SHORT1FROMMR( WinSendMsg( hwndCtl, LM_INSERTITEM,
                                         MPFROMSHORT(LIT_END), acBuf ) );

    // Set drive name (D:) to the item's handle.
    if ( ( sItemIdx != LIT_MEMERROR ) && ( sItemIdx != LIT_ERROR ) )
    {
      acBuf[2] = '\0';
      WinSendMsg( hwndCtl, LM_SETITEMHANDLE,
                  MPFROMSHORT(sItemIdx), MPFROMLONG( *((PULONG)acBuf) ) );
    }
  }
  WinSendMsg( hwndCtl, LM_SELECTITEM, MPFROMSHORT(0), MPFROMSHORT(0xFFFF) );
}

// The content of a remote directory - start.
static VOID _filexferPath(HWND hwnd, USHORT cbPath, PCHAR pcPath)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSZ        pszPath;
  PSCANITEM  pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );

  if ( pScan != NULL )
  {
    // Scan remote directories.

    if ( !_isDir( pScan->stFI.ulAttr ) )   
      debug( "First scan item is not a directory, received: \"%s\"",
             debugBufPSZ( pcPath, cbPath ) );
    return;
  }

  // Remote directory is read for display.
  pszPath = alloca( cbPath + 1 );
  if ( pszPath == NULL )
  {
    debugCP( "Not enough stack space" );
    return;
  }

  _enableConrols( hwnd, TRUE );

  // Set current remote path to the read-only entry field control.
  memcpy( pszPath, pcPath, cbPath );
  pszPath[cbPath] = '\0';
  WinSetDlgItemText( hwnd, IDEF_RPATH, pszPath );

  _cnrClear( WinWindowFromID( hwnd, IDCN_RLIST ) );
}

// The content of a remote directory - new item.
static VOID _filexferFindData(HWND hwnd, RFB_FIND_DATA *pFindData)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSCANITEM  pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );
  FINFO      stFI;

  if ( *((PUSHORT)pFindData->cFileName) == 0x002E ) // "."
    return;

  // Fill stFI structure. It will be part of SCANITEM or FILERECORD.
  _ftWinToOS2( *((PULLONG)&pFindData->ftCreationTime),
               &stFI.fdateCreation, &stFI.ftimeCreation );
  _ftWinToOS2( *((PULLONG)&pFindData->ftLastAccessTime),
               &stFI.fdateLastAccess, &stFI.ftimeLastAccess );
  _ftWinToOS2( *((PULLONG)&pFindData->ftLastWriteTime),
               &stFI.fdateLastWrite, &stFI.ftimeLastWrite );
  stFI.ulAttr = pFindData->dwFileAttributes;
  ((PULL2UL)&stFI.ullSize)->ulLow = pFindData->nFileSizeLow;
  ((PULL2UL)&stFI.ullSize)->ulHigh = pFindData->nFileSizeHigh;

  if ( pScan != NULL )
  {
    // Scan remote directories.

    ULONG    cbPath = strlen( pScan->acName );
    ULONG    cbName = strlen( (char *)pFindData->cFileName );
    PSZ      pszFullName;

    if ( !_isDir( pScan->stFI.ulAttr ) )
    {
      debug( "First scan item is not a directory, received: \"%s\"",
             pFindData->cFileName );
      return;
    }

    if ( /*_isDir( pFindData->dwFileAttributes ) &&*/
         ( strcmp( (char *)pFindData->cFileName, ".." ) == 0 ) )
      // Up-directory - skip.
      return;

    pszFullName = alloca( cbPath + 1 + cbName + 1 );
    if ( pszFullName == NULL )
    {
      debugCP( "Not enough stack space" );
      return;
    }

    cbName = sprintf( pszFullName, "%s\\%s",
                      pScan->acName, pFindData->cFileName );
    _scanFilesAdd( pData, cbName, pszFullName, &stFI );
    return;
  }

  // Remote directory is read for display. Add item to the container.
  _cnrInsert( WinWindowFromID( hwnd, IDCN_RLIST ), pData,
              (PSZ)pFindData->cFileName, &stFI );
}

// The content of a remote directory - full list is received.
static VOID _filexferEndOfList(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSCANITEM  pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );

  if ( pScan != NULL )
  {
    // Scan remote directories in progres.

    if ( !_isDir( pScan->stFI.ulAttr ) )
    {
      debugCP( "First scan item is not a directory" );
      return;
    }

    if ( pData->ulScanOp != _SCAN_RDELETE )
    {
      // Create a local directory.

      HWND     hwndCtl = WinWindowFromID( hwnd, IDEF_LPATH );
      ULONG    cbLocalName = WinQueryWindowTextLength( hwndCtl );
      PSZ      pszLocalName = alloca( cbLocalName + strlen( pScan->acName ) + 1 );

      if ( pszLocalName == NULL )
        debugCP( "Not enough stack space" );
      else
      {
        WinQueryWindowText( hwndCtl, cbLocalName + 1, pszLocalName );
        strcpy( &pszLocalName[cbLocalName], pScan->acName );
        if ( !_mkDir( pszLocalName, &pScan->stFI ) )
        {
          _mbError( hwnd, IDM_LMKDIR_FAIL, pszLocalName );
          return;
        }
      }
    }

    lnkseqRemove( &pData->lsScan, pScan );
    if ( pData->ulScanOp == _SCAN_RLCOPY )
    {
      debug( "Remove scaned dir. from scan list: %s", pScan->acName );
      free( pScan );
    }
    else // _SCAN_RLMOVE, _SCAN_RDELETE
    {
      debug( "Move scaned dir. from scan list to delete list: %s",
             pScan->acName );
      lnkseqAddFirst( &pData->lsScanDel, pScan );
    }

    // Start next directory scan or first file receiving.
    _scanNextStep( hwnd, pData );

    if ( pData->hwndScanProgress != NULLHANDLE )
      WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                  MPFROMSHORT(PDVT_TOTALSIZE), MPFROMP(&pData->ullScanSize) );

    return;
  }

  pData->fFileListRequested = FALSE;
  _enableConrols( hwnd, TRUE );
}

static VOID _filexferFileChunk(HWND hwnd, ULONG ulBytes)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

  if ( pData->hwndScanProgress != NULLHANDLE )
    WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                MPFROMSHORT(PDVT_FILECHUNK), MPFROMP(&ulBytes) );

  if ( pData->fScanCancel )
  {
    ccFXAbortFileTransfer( pData->pCC );
    _scanNextStep( hwnd, pData );
  }
}

static VOID _filexferEndOfFile(HWND hwnd, PFILESTATUS3L pInfo)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSCANITEM  pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );

  if ( pScan == NULL )
  {
    debugCP( "WTF? No records in the scan list" );
    return;
  }

  if ( pInfo != NULL )
  {
    pInfo->attrFile        = pScan->stFI.ulAttr;
    pInfo->fdateCreation   = pScan->stFI.fdateCreation;
    pInfo->ftimeCreation   = pScan->stFI.ftimeCreation;
    pInfo->fdateLastAccess = pScan->stFI.fdateLastAccess;
    pInfo->ftimeLastAccess = pScan->stFI.ftimeLastAccess;
    pInfo->fdateLastWrite  = pScan->stFI.fdateLastWrite;
    pInfo->ftimeLastWrite  = pScan->stFI.ftimeLastWrite;
  }

  lnkseqRemove( &pData->lsScan, pScan );
  if ( ( pData->ulScanOp == _SCAN_RLMOVE || pData->ulScanOp == _SCAN_LRMOVE ) )
  {
    debug( "Move scaned file from scan list to delete list: %s",
           pScan->acName );
    lnkseqAddFirst( &pData->lsScanDel, pScan );
  }
  else // _SCAN_RLCOPY, _SCAN_LRCOPY
  {
    debug( "Remove scaned file from scan list: %s", pScan->acName );
    free( pScan );
  }

  _scanNextStep( hwnd, pData );
}

static VOID _filexferDeleteResult(HWND hwnd, BOOL fSuccess, PSZ pszName)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSCANITEM  pScan;

  if ( ( pData->ulScanOp == _SCAN_RDELETE ) &&
       ( lnkseqGetFirst( &pData->lsScan ) != NULL ) )
  {
    pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );
    if ( _isDir( pScan->stFI.ulAttr ) )
      debug( "WTF? First item in scan list is not a file" );
    lnkseqRemove( &pData->lsScan, pScan );
  }
  else
  {
    pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScanDel );
    if ( pScan != NULL )
    {
      if ( !_isDir( pScan->stFI.ulAttr ) )
        debug( "WTF? First item in scan-to-delete list is not a directory" );
      if ( stricmp( pszName, pScan->acName ) == 0 )
        debug( "WTF? Name in aswer %s, should be %", pszName, pScan->acName );
      lnkseqRemove( &pData->lsScanDel, pScan );
    }
    else if ( pData->ulScanOp == _SCAN_RLMOVE )
    {
      debugCP( "WTF? No records in the scan-to-delete list" );
      return;
    }
  }

  if ( pScan != NULL )
    free( pScan );

  if ( fSuccess )
    // Send delete request for the next file/directory.
    _scanNextStep( hwnd, pData );
  else
    _mbError( hwnd, IDM_RFILE_DELETE_FAIL, pszName );
}

static VOID _filexferMkDirResult(HWND hwnd, BOOL fSuccess, PSZ pszName)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_RLIST );

  if ( pData->ulScanOp == _SCAN_LRCOPY || pData->ulScanOp == _SCAN_LRMOVE )
  {
    if ( !fSuccess )
      debug( "Can't create a remote directory %s", pszName );

    // Scan next local directory.
    _scanNextStep( hwnd, pData );
    return;
  }

  _enableConrols( hwnd, TRUE );
  if ( fSuccess )
  {
    PCHAR    pcEnd = strchr( pszName, '\0' );

    // User may enter leading and trailing spaces, newline characters. We
    // removed these characters when sending request in _cmdCnrEndEdit(). Now
    // we put file name from the server's answer to the container record.
    while( ( pcEnd > pszName ) && ( *(pcEnd-1) != '\\' ) ) pcEnd--;
    strlcpy( pData->pEditRecord->szName, pcEnd, CCHMAXPATHCOMP );

    WinSendMsg( hwndCtl, CM_SORTRECORD, MPFROMP(__cnrComp), 0 );
  }
  else
  {
    _mbError( hwnd, IDM_RMKDIR_FAIL, pszName );
    WinSendMsg( hwndCtl, CM_REMOVERECORD, MPFROMP(&pData->pEditRecord),
                MPFROM2SHORT(1,CMA_FREE | CMA_INVALIDATE) );
  }

  pData->pEditRecord = NULL;
}

static VOID _filexferRenameResult(HWND hwnd, BOOL fSuccess, PSZ *apszNames)
{
  PDLGDATA             pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  HWND                 hwndCtl = WinWindowFromID( hwnd, IDCN_RLIST );
  PSZ                  pszName = apszNames[1];

  _enableConrols( hwnd, TRUE );

  if ( fSuccess )
  {
    PCHAR    pcEnd = strchr( pszName, '\0' );

    // User may enter leading and trailing spaces, newline characters. We
    // removed these characters when sending request in _cmdCnrEndEdit(). Now
    // we put file name from the server's answer to the container record.
    while( ( pcEnd > pszName ) && ( *(pcEnd-1) != '\\' ) ) pcEnd--;
    strlcpy( pData->pEditRecord->szName, pcEnd, CCHMAXPATHCOMP );

    WinSendMsg( hwndCtl, CM_SORTRECORD, MPFROMP(__cnrComp), 0 );
  }
  else
  {
    // Set old text (name) for the record.
    strcpy( pData->pEditRecord->szName, pData->acOldName );
    WinSendMsg( hwndCtl, CM_INVALIDATERECORD, MPFROMP(&pData->pEditRecord),
                MPFROM2SHORT(1,CMA_TEXTCHANGED) );

    _mbError( hwnd, IDM_RENAME_FAIL, pszName );
  }

  pData->pEditRecord = NULL;
}

// The content of local directory received.
static VOID _wmEndOfLocalList(HWND hwnd)
{
  PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );
  PSCANITEM  pScan = (PSCANITEM)lnkseqGetFirst( &pData->lsScan );
  HWND       hwndRPath = WinWindowFromID( hwnd, IDEF_RPATH );
  ULONG      cbPath = WinQueryWindowTextLength( hwndRPath );
  PSZ        pszRemoteName;

  if ( pScan == NULL )
    debugCP( "Scan list is empty" );
  else
  do
  {
    // Scan local directories in progres.

    if ( !_isDir( pScan->stFI.ulAttr ) )
    {
      debugCP( "First scan item is not a directory" );
      break;
    }

    if ( pData->ulScanOp != _SCAN_LDELETE )
    {
      // _SCAN_LRCOPY, _SCAN_LRMOVE.
      // Send request to create remote directory.
      pszRemoteName = alloca( cbPath + strlen( pScan->acName ) + 1 );
      if ( pszRemoteName == NULL )
        break;
      WinQueryWindowText( hwndRPath, cbPath + 1, pszRemoteName );
      strcpy( &pszRemoteName[cbPath], pScan->acName );
      debug( "Send request to create remote directory: %s", pszRemoteName );
      if ( !ccFXMkDir( pData->pCC, pszRemoteName ) )
        break;
    }

    lnkseqRemove( &pData->lsScan, pScan );
    if ( pData->ulScanOp == _SCAN_LRCOPY )
    {
      debug( "Remove scaned dir. from scan list: %s", pScan->acName );
      free( pScan );
    }
    else // _SCAN_LRMOVE, _SCAN_LDELETE
    {
      debug( "Move scaned dir. from scan list to delete list: %s",
             pScan->acName );
      lnkseqAddFirst( &pData->lsScanDel, pScan );
    }

    if ( pData->ulScanOp == _SCAN_LDELETE )
    {
      debugCP( "_scanNextStep()..." );
      _scanNextStep( hwnd, pData );
    }
    else if ( pData->hwndScanProgress != NULLHANDLE )
      WinSendMsg( pData->hwndScanProgress, WM_PDSETVALUE,
                  MPFROMSHORT(PDVT_TOTALSIZE), MPFROMP(&pData->ullScanSize) );

    return;
  }
  while( FALSE );

  _scanFilesClean( pData );
  _enableConrols( hwnd, TRUE );
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmInitDlg( hwnd, (PDLGINITDATA)mp2 );

    case WM_DESTROY:
      _wmDestory( hwnd );
      break;

    case WM_CLOSE:
      {
        PDLGDATA pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

        ccFXAbortFileTransfer( pData->pCC );
        WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_COMMAND,
                    MPFROMSHORT(CWC_FILE_TRANSFER_CLOSE),
                    MPFROM2SHORT(CMDSRC_OTHER,FALSE) );
      }
      return (MRESULT)0;

    case WM_CONTROL:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDCB_RDRIVES:
          _ctlDriveSelected( hwnd, HWNDFROMMP(mp2), TRUE );
          break;

        case IDCB_LDRIVES:
          _ctlDriveSelected( hwnd, HWNDFROMMP(mp2), FALSE );
          break;

        case IDCN_LLIST:
        case IDCN_RLIST:
          switch( SHORT2FROMMP(mp1) )
          {
            case CN_ENTER:
              _ctlListEnter( hwnd,
                             (PFILERECORD)((PNOTIFYRECORDENTER)mp2)->pRecord,
                             SHORT1FROMMP(mp1) == IDCN_RLIST );
              break;

            case CN_BEGINEDIT:
              {
                PDLGDATA pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

                // Store old name to avoid renaming when text is not changed.
                strlcpy( pData->acOldName,
                         ((PFILERECORD)((PCNREDITDATA)mp2)->pRecord)->szName,
                         CCHMAXPATHCOMP );
              }
              break;

            case CN_REALLOCPSZ:
              if ( ((PCNREDITDATA)mp2)->cbText == 0 )
                return (MRESULT)FALSE;
              if ( ((PCNREDITDATA)mp2)->cbText > CCHMAXPATHCOMP )
                ((PCNREDITDATA)mp2)->cbText = CCHMAXPATHCOMP;
              return (MRESULT)TRUE;

            case CN_ENDEDIT:
              _cmdCnrEndEdit( hwnd, (PCNREDITDATA)mp2 );
              return (MRESULT)0;

            case CN_CONTEXTMENU:
              _cmdContextMenu( hwnd, SHORT1FROMMP(mp1), (PFILERECORD)mp2 );
              return (MRESULT)0;
          }
          break;
      }
      return (MRESULT)0;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDPB_RUPDIR:
          _cmdUpDir( hwnd, FALSE, TRUE );
          break;

        case IDPB_LUPDIR:
          _cmdUpDir( hwnd, FALSE, FALSE );
          break;

        case IDPB_RROOT:
          _cmdUpDir( hwnd, TRUE, TRUE );
          break;

        case IDPB_LROOT:
          _cmdUpDir( hwnd, TRUE, FALSE );
          break;

        case MBID_CANCEL:
          // Command from the progress dialog.
          {
            PDLGDATA   pData = (PDLGDATA)WinQueryWindowULong( hwnd, QWL_USER );

            // Operation will be canceled on next request in _scanNextStep().
            pData->fScanCancel = TRUE;
            break;
          }

        default:
          // Context menu / accelerator-table commands.

          if ( !WinIsWindowEnabled( WinWindowFromID( hwnd, IDEF_LPATH ) ) )
            // Controls are disabled and container context menu can not be
            // displayed. But commands still accessible via accelerator-table.
            // We don't check commands here if controls are disabled.
            break;

          switch( SHORT1FROMMP(mp1) )
          {
            case IDMI_LDRIVE:        // ALT-F1
            case IDMI_RDRIVE:        // ALT-F2
              WinSetFocus( HWND_DESKTOP,
                           WinWindowFromID( hwnd,
                                            SHORT1FROMMP(mp1) == IDMI_LDRIVE
                                              ? IDCB_LDRIVES : IDCB_RDRIVES ) );
              break;

            case IDMI_COPY:
              _cmdScanOp( hwnd, _isRemotePanelActived( hwnd )
                                  ? _SCAN_RLCOPY : _SCAN_LRCOPY );
              break;

            case IDMI_MOVE:
              // We will start moving files in _wmMsgBoxDisMiss() after user
              // approval.
              switch( _queryActiveSelection( hwnd ) )
              {
                case _ACTIVESEL_REMOTE:
                  _mbConfirm( hwnd, IDM_QUERY_RLMOVE, _SCANMB_YES_RLMOVE );
                  break;

                case _ACTIVESEL_LOCAL:
                  _mbConfirm( hwnd, IDM_QUERY_LRMOVE, _SCANMB_YES_LRMOVE );
                  break;
              }
              break;

            case IDMI_MKDIR:
              _cmdMkDir( hwnd, _isRemotePanelActived( hwnd ) );
              break;

            case IDMI_DELETE:
              // We will start deleting files in _wmMsgBoxDisMiss() after user
              // approval.
              switch( _queryActiveSelection( hwnd ) )
              {
                case _ACTIVESEL_REMOTE:
                  _mbConfirm( hwnd, IDM_QUERY_RDELETE, _SCANMB_YES_RDELETE );
                  break;

                case _ACTIVESEL_LOCAL:
                  _mbConfirm( hwnd, IDM_QUERY_LDELETE, _SCANMB_YES_LDELETE );
                  break;
              }
              break;

            case IDMI_RENAME:
              _cmdRename( hwnd );
              break;

            case IDMI_REFRESH:
              _ctlListEnter( hwnd, NULL, _isRemotePanelActived( hwnd ) );
              break;

            case IDMI_OPENFOLDER:
              _cmdOpenFolder( hwnd );
              break;
          } // switch( SHORT1FROMMP(mp1) )
        // End of default case

      } // switch( SHORT1FROMMP(mp1) )
      return (MRESULT)0;

    case WM_MSGBOXINIT:
      _moveToOwnerCenter( HWNDFROMMP(mp1) );
      return (MRESULT)0;

    case WM_MSGBOXDISMISS:
      _wmMsgBoxDisMiss( hwnd, HWNDFROMMP(mp1), LONGFROMMP(mp2) );
      return (MRESULT)0;

    case WM_VNC_FILEXFER:
      // Message from filexfer module redirecet via client's window.
      switch( SHORT1FROMMP(mp1) )
      {
        case CFX_PERMISSION_DENIED:
debugPCP( "CFX_PERMISSION_DENIED" );
          break;

        case CFX_RECV_CHUNK:
        case CFX_SEND_CHUNK:
          _filexferFileChunk( hwnd, LONGFROMMP(mp2) );
          break;

        case CFX_DRIVES:
          // List of remote drives.
          _filexferDrives( hwnd, (PSZ)mp2 );
          break;

        case CFX_PATH:
          // Files list - start.
          _filexferPath( hwnd, SHORT2FROMMP(mp1), (PCHAR)mp2 );
          break;

        case CFX_FINDDATA:
          // Files list - new item.
          _filexferFindData( hwnd, (RFB_FIND_DATA *)mp2 );
          break;

        case CFX_END_OF_LIST:
          // End of drives/files list.
          _filexferEndOfList( hwnd );
          break;

        case CFX_DELETE_RES:
          // Result after ccFXDelete().
          _filexferDeleteResult( hwnd, SHORT2FROMMP(mp1) != 0, (PSZ)mp2 );
          break;

        case CFX_MKDIR_RES:
          // Result after ccFXMkDir().
          _filexferMkDirResult( hwnd, SHORT2FROMMP(mp1) != 0, (PSZ)mp2 );
          break;

        case CFX_RENAME_RES:
          // Result after ccFXRename().
          _filexferRenameResult( hwnd, SHORT2FROMMP(mp1) != 0, (PSZ *)mp2 );
          break;

        case CFX_RECV_CREATE_FAIL:
        case CFX_RECV_WRITE_FAIL:
          _scanError( hwnd, IDM_RECV_WRITE_FAIL );
          break;

        case CFX_RECV_READ_FAIL:
          _scanError( hwnd, IDM_RECV_READ_FAIL );
          break;

        case CFX_RECV_END_OF_FILE:
          _filexferEndOfFile( hwnd, (PFILESTATUS3L)mp2 );
          break;

        case CFX_SEND_WRITE_FAIL:
          _scanError( hwnd, IDM_SEND_WRITE_FAIL );
          break;

        case CFX_SEND_READ_FAIL:
          _scanError( hwnd, IDM_SEND_READ_FAIL );
          break;

        case CFX_SEND_END_OF_FILE:
          _filexferEndOfFile( hwnd, NULL );
          break;
      }
      return (MRESULT)0;

    case WM_ENDOFLOCALLIST:
      _wmEndOfLocalList( hwnd );
      return (MRESULT)0;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


HWND fxdlgOpen(HWND hwndOwner, PSZ pszServer, PCLNTCONN pCC)
{
  HWND                 hwndDlg;
  DLGINITDATA          stInitData;

  stInitData.ulSize         = sizeof(DLGINITDATA);
  stInitData.pszServer      = pszServer;
  stInitData.pCC            = pCC;

  hwndDlg = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, NULLHANDLE,
                        IDDLG_FILEXFER, &stInitData );
  if ( hwndDlg == NULLHANDLE )
    debug( "WinLoadDlg(,,,,IDDLG_FILEXFER,) failed" );

  return hwndDlg;
}
