#ifndef START_H
#define START_H

#define WM_WA_UPDATEICON         ( WM_USER + 2 )

BOOL startViewer(vncv *somSelf, PSZ pszMClass);
BOOL startSwithTo(vncv *somSelf);
VOID startDestroy(vncv *somSelf);

#endif // START_H

