#ifndef __DGIS_H
#define __DGIS_H

typedef USHORT PID16;
typedef USHORT TID16;
typedef USHORT HMODULE16;

/* Global Info Seg */

typedef struct _GINFOSEG
{
    ULONG   time;               /* time in seconds */
    ULONG   msecs;              /* milliseconds    */
    UCHAR   hour;               /* hours */
    UCHAR   minutes;            /* minutes */
    UCHAR   seconds;            /* seconds */
    UCHAR   hundredths;         /* hundredths */
    USHORT  timezone;           /* minutes from UTC */
    USHORT  cusecTimerInterval; /* timer interval (units = 0.0001 seconds) */
    UCHAR   day;                /* day */
    UCHAR   month;              /* month */
    USHORT  year;               /* year */
    UCHAR   weekday;            /* day of week */
    UCHAR   uchMajorVersion;    /* major version number */
    UCHAR   uchMinorVersion;    /* minor version number */
    UCHAR   chRevisionLetter;   /* revision letter */
    UCHAR   sgCurrent;          /* current foreground session */
    UCHAR   sgMax;              /* maximum number of sessions */
    UCHAR   cHugeShift;         /* shift count for huge elements */
    UCHAR   fProtectModeOnly;   /* protect mode only indicator */
    USHORT  pidForeground;      /* pid of last process in foreground session */
    UCHAR   fDynamicSched;      /* dynamic variation flag */
    UCHAR   csecMaxWait;        /* max wait in seconds */
    USHORT  cmsecMinSlice;      /* minimum timeslice (milliseconds) */
    USHORT  cmsecMaxSlice;      /* maximum timeslice (milliseconds) */
    USHORT  bootdrive;          /* drive from which the system was booted */
    UCHAR   amecRAS[ 32 ];      /* system trace major code flag bits */
    UCHAR   csgWindowableVioMax;/* maximum number of VIO windowable sessions */
    UCHAR   csgPMMax;           /* maximum number of pres. services sessions */
} GINFOSEG;

typedef GINFOSEG *PGINFOSEG;

/* Local Info Seg */

typedef struct _LINFOSEG
{
    PID16     pidCurrent;        /* current process id */
    PID16     pidParent;         /* process id of parent */
    USHORT    prtyCurrent;       /* priority of current thread */
    TID16     tidCurrent;        /* thread ID of current thread */
    USHORT    sgCurrent;         /* session */
    UCHAR     rfProcStatus;      /* process status */
    UCHAR     dummy1;
    BOOL16    fForeground;       /* current process has keyboard focus */
    UCHAR     typeProcess;       /* process type */
    UCHAR     dummy2;
    SEL       selEnvironment;    /* environment selector */
    USHORT    offCmdLine;        /* command line offset */
    USHORT    cbDataSegment;     /* length of data segment */
    USHORT    cbStack;           /* stack size */
    USHORT    cbHeap;            /* heap size */
    HMODULE16 hmod;              /* module handle of the application */
    SEL       selDS;             /* data segment handle of the application */
} LINFOSEG;

typedef LINFOSEG *PLINFOSEG;

/* Process Type codes (local info seg typeProcess field) */

#define PT_FULLSCREEN       0
#define PT_REALMODE         1
#define PT_WINDOWABLEVIO    2
#define PT_PM               3
#define PT_DETACHED         4

typedef SEL FAR16PTR;
typedef SEL PSEL16;

#define DosGetInfoSeg DOS16GETINFOSEG

extern APIRET16 APIENTRY16 DosGetInfoSeg(PSEL16 ginfSel, PSEL16 linfSel);

#define MAKEPGINFOSEG(sel)  ((PGINFOSEG)MAKEP(sel,0))
#define MAKEPLINFOSEG(sel)  ((PLINFOSEG)MAKEP(sel,0))

#endif  //__DGIS_H