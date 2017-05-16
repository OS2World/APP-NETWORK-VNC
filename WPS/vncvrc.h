#ifndef VNCV_H
#define VNCV_H

#define ID_ICON                     1

// Strings

#define IDS_SERVER                  0
#define IDS_ENCODINGS               1

// Dialogs

#define IDDLG_PAGE_SERVER        3100
#define IDEF_HOSTDISPLAY         3101
#define IDSB_ATTEMPTS            3102
#define IDSB_BPP                 3103
#define IDCB_REMEMBER_PSWD       3104
#define IDCB_VIEW_ONLY           3105
#define IDCB_SHARE_DESKTOP       3106
#define IDSB_DESTPORT            3107
#define IDEF_CHARSET             3108
#define IDCB_DYNAMICICON         3109

#define IDDLG_PAGE_ENCODINGS     3200
#define IDLB_ALL_ENCODINGS       3201
#define IDPB_ENC_ADD             3202
#define IDPB_ENC_DELETE          3203
#define IDLB_SEL_ENCODINGS       3204
#define IDPB_ENC_UP              3205
#define IDPB_ENC_DOWN            3206
#define IDSLID_QUALITY           3207
#define IDSLID_COMPRESS          3208

#define IDPB_UNDO                3501
#define IDPB_DEFAULT             3502

// Class popup menu.

#include <wpobject.h>  // WPMENUID_USER

#define IDMNU_VNCV               (WPMENUID_USER+1)
#define IDMI_DYNICON_ON          (WPMENUID_USER+2)
#define IDMI_DYNICON_OFF         (WPMENUID_USER+3)


#endif // VNCV_H

