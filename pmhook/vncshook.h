#ifndef VNCSHOOK_H
#define VNCSHOOK_H

#define PMHEXPORT      __declspec(dllexport)

#define HM_SCREEN      (WM_USER + 101)
#define HM_MOUSE       (WM_USER + 102)
#define HM_VIOCHAR     (WM_USER + 103)

#ifdef VNCSHOOK_STATIC
PMHEXPORT BOOL APIENTRY pmhInit(HAB hab, HMQ hmq);
PMHEXPORT VOID APIENTRY pmhDone(HAB hab);

// Posts keybord or mouse message to the system queue.
PMHEXPORT BOOL APIENTRY pmhPostEvent(HAB hab, ULONG ulMsg,
                                     MPARAM mp1, MPARAM mp2);
#endif

typedef BOOL (APIENTRY *PMHINIT)(HAB hab, HMQ hmq);
typedef VOID (APIENTRY *PMHDONE)(HAB hab);
typedef BOOL (APIENTRY *PMHPOSTEVENT)(HAB hab, ULONG ulMsg,
                                      MPARAM mp1, MPARAM mp2);

#endif // VNCSHOOK_H
