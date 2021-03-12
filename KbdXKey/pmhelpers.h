#ifndef PMHELPERS_H
#define PMHELPERS_H

ULONG EXPENTRY cnrhScrollToRecord(HWND hwndCnr,       // in: container window
                         PRECORDCORE pRec,   // in: record to scroll to
                         ULONG fsExtent,
                                 // in: this determines what parts of pRec
                                 // should be made visible. OR the following
                                 // flags:
                                 // -- CMA_ICON: the icon rectangle
                                 // -- CMA_TEXT: the record text
                                 // -- CMA_TREEICON: the "+" sign in tree view
                         BOOL fKeepParent);
                                 // for tree views only: whether to keep
                                 // the parent record of pRec visible when scrolling.
                                 // If scrolling to pRec would make the parent
                                 // record invisible, we instead scroll so that
                                 // the parent record appears at the top of the
                                 // container workspace (Win95 style).


#endif // PMHELPERS_H
