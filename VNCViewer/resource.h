#ifndef RESOURCE_H
#define RESOURCE_H

#define APP_NAME                    "VNC Viewer"
#define APP_NAME_LENGTH             10

#define IDICON_VIEWWIN            1
#define IDICON_FILE               2
#define IDICON_FOLDER             3
#define IDICON_FILE_HIDDEN        4
#define IDICON_FOLDER_HIDDEN      5
#define IDPTR_DEFPTR              6

// Messages
#define IDM_INVALID_DEST_HOST     0
#define IDM_INVALID_HOST          1
#define IDM_FORGET_QUESTION       2
#define IDM_INI_OPEN_ERROR        3
#define IDM_TIP_HOST              4
#define IDM_TIP_DESTHOST          5
#define IDM_TIP_LISTEN_DESTHOST   6
#define IDM_TIP_CHARSET           7
#define IDM_TIP_ALL_ENCODINGS     8
#define IDM_TIP_SEL_ENCODINGS     9
#define IDM_TIP_COMPRESS_LEVEL   10
#define IDM_TIP_QUALITY_LEVEL    11
#define IDM_TIP_IFADDR           12
#define IDM_TIP_PORT             13
#define IDM_SW_INVALID           14
#define IDM_SW_NO_VALUE          15
#define IDM_SW_NO_HOST           16
#define IDM_SW_INVALID_VALUE     17
#define IDM_SW_INVALID_DEPTH     18
#define IDM_SW_INVALID_ENC       19
#define IDM_SW_UNKNOWN           20
#define IDM_ALREADY_BINDED       21
#define IDM_CHAT_CLOSED          22
#define IDM_CHAT_NOT_DETECTED    23
#define IDM_OVERWRITE            24
#define IDM_LMKDIR_FAIL          25
#define IDM_RMKDIR_FAIL          26
#define IDM_RFILE_DELETE_FAIL    27
#define IDM_QUERY_RDELETE        28
#define IDM_QUERY_LRMOVE         29
#define IDM_RENAME_FAIL          30
#define IDM_RECV_READ_FAIL       31
#define IDM_RECV_WRITE_FAIL      32
#define IDM_SEND_WRITE_FAIL      33
#define IDM_SEND_READ_FAIL       34
#define IDM_RDIRFILEREPLACE      35
#define IDM_OVERWRITERDIR        36
#define IDM_QUERY_LDELETE        37
#define IDM_LFILE_DELETE_FAIL    38
#define IDM_QUERY_RLMOVE         39
#define IDM_LRENAME_FAIL         40
#define IDM_PERMISSION_DENIED    41

// Strings
#define IDS_NEW_CONNECTION        0
#define IDS_VIEW_ONLY             1
#define IDS_SEND_CAD              2
#define IDS_SEND_CTRL_ESC         3
#define IDS_SEND_ALT_ESC          4
#define IDS_SEND_ALT_TAB          5
#define IDS_CHAT                  6
#define IDS_FILE_TRANSFER         7
#define IDS_CONNECT               8
#define IDS_LISTEN                9
#define IDS_LISTENING            10
#define IDS_WAIT_SERVER          11
#define IDS_NAME                 12
#define IDS_SIZE                 13
#define IDS_DATE                 14
#define IDS_TIME                 15
#define IDS_ATTRIBUTES           16
#define IDS_OK                   17
#define IDS_CANCEL               18
#define IDS_SKIP                 19
#define IDS_ALL                  20
#define IDS_NONE                 21
#define IDS_YES                  22
#define IDS_NO                   23
#define IDS_OP_RLCOPY            24
#define IDS_OP_RLMOVE            25
#define IDS_OP_RDELETE           26
#define IDS_OP_LRCOPY            27
#define IDS_OP_LRMOVE            28
#define IDS_OP_LDELETE           29


// Dialogs

#define IDDLG_PROGRESS           1000
#define IDPBAR_PROGRESS          1001

#define IDDLG_PASSWORD           2100
#define IDDDLG_CREDENTIAL        2200
#define IDEF_USERNAME            2001
#define IDEF_PASSWORD            2002

#define IDDLG_OPTIONS            3000
#define IDSB_ATTEMPTS            3001
#define IDSB_BPP                 3002
#define IDCB_REMEMBER_PSWD       3003
#define IDCB_VIEW_ONLY           3004
#define IDCB_SHARE_DESKTOP       3005
#define IDSB_DESTPORT            3006
#define IDEF_CHARSET             3007
#define IDLB_ALL_ENCODINGS       3008
#define IDPB_ENC_ADD             3009
#define IDPB_ENC_DELETE          3010
#define IDLB_SEL_ENCODINGS       3011
#define IDPB_ENC_UP              3012
#define IDPB_ENC_DOWN            3013
#define IDSLID_QUALITY           3014
#define IDSLID_COMPRESS          3015
#define IDCB_USE_AS_DEFAULT      3016
#define IDPB_UNDO                3017
#define IDPB_DEFAULT             3018

#define IDDLG_LAUNCHPAD          4000
#define IDD_NOTEBOOK             4001

#define IDDLG_PAGE_CONNECT       5000
#define IDST_HOST                5001
#define IDCB_HOST                5002
#define IDPB_OPTIONS             5003
#define IDPB_FORGET              5004

#define IDDLG_PAGE_LISTEN        6000
#define IDCB_IFADDR              6001
#define IDSB_PORT                6002

#define IDDLG_FILEXFER           7000
#define IDCB_LDRIVES             7001
#define IDPB_LUPDIR              7002
#define IDPB_LROOT               7003
#define IDEF_LPATH               7004
#define IDCN_LLIST               7005
#define IDCB_RDRIVES             7006
#define IDPB_RUPDIR              7007
#define IDPB_RROOT               7008
#define IDEF_RPATH               7009
#define IDCN_RLIST               7010
#define IDMI_COPY                7011
#define IDMI_MOVE                7012
#define IDMI_MKDIR               7013
#define IDMI_DELETE              7014
#define IDMI_LDRIVE              7015
#define IDMI_RDRIVE              7016
#define IDMI_RENAME              7017
#define IDMI_REFRESH             7018
#define IDMI_OPENFOLDER          7019

#define IDDLG_FXPROGRESS         8000
#define IDST_OPERATION           8001
#define IDST_FILE                8002
#define IDST_TOTAL_SIZE          8003
#define IDPBAR_OPERATION         8004

#endif // RESOURCE_H
