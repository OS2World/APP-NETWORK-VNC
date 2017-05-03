#include <string.h>
#define INCL_WIN
#include <os2.h>
#include <debug.h>
#include "pmhelpers.h"

// From XWP helpers: cnrhScrollToRecord()

/*
 *@@ cnrhScrollToRecord:
 *      scrolls a given container control to make a given
 *      record visible.
 *
 *      Returns:
 *      --  0:       OK, scrolled
 *      --  1:       record rectangle query failed (error)
 *      --  2:       cnr viewport query failed (error)
 *      --  3:       record is already visible (scrolling not necessary)
 *      --  4:       cnrinfo query failed (error)
 *
 *      Note: All messages are _sent_ to the container, not posted.
 *      Scrolling therefore occurs synchroneously before this
 *      function returns.
 *
 *      This function an improved version of the one (W)(C) Dan Libby, found at
 *      http://zebra.asta.fh-weingarten.de/os2/Snippets/Howt6364.HTML
 *      Improvements (C) 1998 Ulrich MÔller.
 *
 *@@changed V0.9.4 (2000-08-07) [umoeller]: now posting scroll messages to avoid sync errors
 *@@changed V0.9.9 (2001-03-12) [umoeller]: this never scrolled for root records in tree view if KeepParent == TRUE, fixed
 *@@changed V0.9.9 (2001-03-13) [umoeller]: largely rewritten; this now scrolls x properly too and is faster
 */

ULONG EXPENTRY cnrhScrollToRecord(HWND hwndCnr,       // in: container window
                         PRECORDCORE pRec,   // in: record to scroll to
                         ULONG fsExtent,
                                 // in: this determines what parts of pRec
                                 // should be made visible. OR the following
                                 // flags:
                                 // -- CMA_ICON: the icon rectangle
                                 // -- CMA_TEXT: the record text
                                 // -- CMA_TREEICON: the "+" sign in tree view
                         BOOL fKeepParent)
                                 // for tree views only: whether to keep
                                 // the parent record of pRec visible when scrolling.
                                 // If scrolling to pRec would make the parent
                                 // record invisible, we instead scroll so that
                                 // the parent record appears at the top of the
                                 // container workspace (Win95 style).

{
    QUERYRECORDRECT qRect;
    RECTL           rclRecord,
                    rclCnr;
    LONG            lXOfs = 0,
                    lYOfs = 0;

    qRect.cb = sizeof(qRect);
    qRect.pRecord = (PRECORDCORE)pRec;
    qRect.fsExtent = fsExtent;

    if (fKeepParent)
    {
        CNRINFO         CnrInfo;
        // this is only valid in tree view, so check
        if (!WinSendMsg(hwndCnr,
                        CM_QUERYCNRINFO,
                        (MPARAM)&CnrInfo,
                        (MPARAM)sizeof(CnrInfo)))
            return 4;
        else
            // disable if not tree view
            fKeepParent = ((CnrInfo.flWindowAttr & CV_TREE) != 0);
    }

    // query record location and size of container
    if (!WinSendMsg(hwndCnr,
                    CM_QUERYRECORDRECT,
                    &rclRecord,
                    &qRect))
        return 1;

    if (!WinSendMsg(hwndCnr,
                    CM_QUERYVIEWPORTRECT,
                    &rclCnr,
                    MPFROM2SHORT(CMA_WINDOW,
                                    // returns the client area rectangle
                                    // in container window coordinates
                                 FALSE)) )
        return 2;

    // check if left bottom point of pRec is currently visible in container

    #define IS_BETWEEN(a, b, c) (((a) >= (b)) && ((a) <= (c)))

    // 1) set lXOfs if we need to scroll horizontally
    if (!IS_BETWEEN(rclRecord.xLeft, rclCnr.xLeft, rclCnr.xRight))
        // record xLeft is outside viewport:
        // scroll horizontally so that xLeft is exactly on left of viewport
        lXOfs = (rclRecord.xLeft - rclCnr.xLeft);
    else if (!IS_BETWEEN(rclRecord.xRight, rclCnr.xLeft, rclCnr.xRight))
        // record xRight is outside viewport:
        // scroll horizontally so that xRight is exactly on right of viewport
        lXOfs = (rclRecord.xRight - rclCnr.xRight);

    // 2) set lYOfs if we need to scroll vertically
    if (!IS_BETWEEN(rclRecord.yBottom, rclCnr.yBottom, rclCnr.yTop))
        // record yBottom is outside viewport:
        // scroll horizontally so that yBottom is exactly on bottom of viewport
        lYOfs =   (rclCnr.yBottom - rclRecord.yBottom)    // this would suffice
                + (rclRecord.yTop - rclRecord.yBottom);
                            // but we make the next rcl visible too
    else if (!IS_BETWEEN(rclRecord.yTop, rclCnr.yBottom, rclCnr.yTop))
        // record yTop is outside viewport:
        // scroll horizontally so that yTop is exactly on top of viewport
        lYOfs = (rclRecord.yTop - rclCnr.yTop);

    if (fKeepParent && (lXOfs || lYOfs))
    {
        // keep parent enabled, and we're scrolling:
        // find the parent record then
        qRect.cb = sizeof(qRect);
        qRect.pRecord = (PRECORDCORE)WinSendMsg(hwndCnr,
                                                CM_QUERYRECORD,
                                                (MPARAM)pRec,
                                                MPFROM2SHORT(CMA_PARENT,
                                                             CMA_ITEMORDER));
        if (qRect.pRecord)     // V0.9.9 (2001-03-12) [umoeller]
        {
            // parent exists:
            // get PARENT record rectangle then
            RECTL rclParentRecord;
            qRect.fsExtent = fsExtent;
            if (WinSendMsg(hwndCnr,
                           CM_QUERYRECORDRECT,
                           &rclParentRecord,
                           &qRect))
            {
                // check if parent record WOULD still be visible
                // if we scrolled what we calculated above
                RECTL rclCnr2;
                memcpy(&rclCnr2, &rclCnr, sizeof(rclCnr2));
//              [ Digi ] Replace winhOffsetRect(&rclCnr2, lXOfs, -lYOfs)
//                       with WinOffsetRect().
                WinOffsetRect( WinQueryAnchorBlock( hwndCnr ), &rclCnr2,
                               lXOfs, -lYOfs );

                if (    lXOfs
                     && (!IS_BETWEEN(rclParentRecord.xLeft, rclCnr2.xLeft, rclCnr2.xRight))
                   )
                    // record xLeft is outside viewport:
                    // scroll horizontally so that xLeft is exactly on left of viewport
                    lXOfs = (rclParentRecord.xLeft - rclCnr.xLeft);

                if (    lYOfs
                     && (!IS_BETWEEN(rclParentRecord.yBottom, rclCnr2.yBottom, rclCnr2.yTop))
                   )
                    // record yBottom is outside viewport:
                    // recalculate y ofs so that we scroll so
                    // that parent record is on top of cnr viewport
                    lYOfs =   (rclCnr.yTop - rclParentRecord.yTop) // this would suffice
                            - (rclRecord.yTop - rclRecord.yBottom);  // but we make the previous rcl visible too
            }
        }
    }

    // V0.9.14 (2001-07-28) [umoeller]
    // tried WinSendMsg (instead of post) because
    // otherwise we can't get auto-edit on wpshCreateFromTemplate
    // to work... but this causes two problems:
    // --  for some reason, the WPS selects the recc under
    //     the mouse then, which is very wrong in most situations;
    // --  since the WPS expands the root record in tree views
    //     after the root has been populated, this can cause
    //     totally garbled display for complex trees.
    // So I'm back to WinPostMsg... // @@todo

    if (lXOfs)
        // scroll horizontally
        WinPostMsg(hwndCnr,
                   CM_SCROLLWINDOW,
                   (MPARAM)CMA_HORIZONTAL,
                   (MPARAM)lXOfs);

    // scroll vertically
    if (lYOfs)
        WinPostMsg(hwndCnr,
                   CM_SCROLLWINDOW,
                   (MPARAM)CMA_VERTICAL,
                   (MPARAM)lYOfs);

    return 0;
}
