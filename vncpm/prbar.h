/* ********************************************************* */
/*                                                           */
/*                   ProgressBar control.                    */
/*                                                           */
/* ********************************************************* */

/*
  Resource examples:

    CONTROL "ProgressBar Title",
            IDPBAR_OPERATION, 4, 25, 232, 7, WC_PROGRESSBAR, WS_VISIBLE
      CTLDATA 6, 0, 2,

    CONTROL "#101#102\t%",
            IDPBAR_OPERATION, 4, 25, 232, 7, WC_PROGRESSBAR,
            WS_VISIBLE

    CONTROL "#101#102#103\tMy ProgressBar and my bitmaps",
            IDPBAR_OPERATION, 4, 25, 232, 7, WC_PROGRESSBAR, WS_VISIBLE

  Where: 101, 102, 103 - bitmap resource IDs.
         \t - separator: <id list> \t <Title>
         Use '%' (or <id list>\t% ) as window text to show %-value on bar.
  To understand CTLDATA see structure PRBARCDATA defination.
*/

#ifndef PRBAR_H
#define PRBAR_H

// ProgressBar class name
#define WC_PROGRESSBAR           "VNCProgressBar"

// Resource IDs
#define IDBMP_PRBAR1             1
#define IDBMP_PRBAR2             2

// PBM_SETPARAM - window message for the ProgressBar control.
// mp1 - ULONG, ORed flags PBARSF_xxxxx
// mp2 - PPRBARINFO
#define PBM_SETPARAM             (WM_USER + 1)
#define PBARSF_ANIMATION         0x0001
#define PBARSF_IMAGE             0x0002
#define PBARSF_TOTAL             0x0004
#define PBARSF_VALUE             0x0008
#define PBARSF_VALUEINCR         0x0010

#define PRBARA_STATIC            0
#define PRBARA_LEFT              1
#define PRBARA_LEFTDBL           2
#define PRBARA_RIGHT             3
#define PRBARA_RIGHTDBL          4

// ProgressBar-control data structure. 
#pragma pack(1)
typedef struct _PRBARCDATA {
  USHORT     usSize;
  USHORT     usImageIdx;
  USHORT     usAnimation;        // PRBARA_xxxxx
} PRBARCDATA, *PPRBARCDATA;
#pragma pack()

// PBM_SETPARAM, mp2
typedef struct _PRBARINFO {
  ULONG                ulAnimation;        // PRBARA_xxxxx
  ULONG                ulImageIdx;
  unsigned long long   ullTotal;
  unsigned long long   ullValue;
} PRBARINFO, *PPRBARINFO;

// Window class WC_PROGRESSBAR registration.
VOID EXPENTRY prbarRegisterClass(HAB hab);

#endif // PRBAR_H
