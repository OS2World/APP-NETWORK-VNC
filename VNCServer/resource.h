#ifndef RESOURCE_H
#define RESOURCE_H

#define APP_NAME                    "VNC Server"
#define APP_NAME_LENGTH             10

#define ID_ICON                      1
#define ID_ICON_ONLINE               2
#define IDICON_NULL                  3
#define IDICON_ACL_DISABLE           4
#define IDICON_ACL_ENABLE            5

#define IDS_UNDO                     0
#define IDS_DEFAULT                  1
#define IDS_GENERAL                  2
#define IDS_ACCESS                   3
#define IDS_OPTIONS                  4
#define IDS_KEYBOARD                 5
#define IDS_LOGGING                  6
#define IDS_PROGRAMS                 7
#define IDS_SOURCE                   8
#define IDS_COMMENT                  9
#define IDS_ALLOW                   10
#define IDS_DENY                    11
#define IDS_CLIENTS                 12
#define IDS_HOST                    13
#define IDS_TIME                    14
#define IDS_VERSION                 15
#define IDS_STATE                   16

#define IDMSG_USER_CHAT_OPEN        0
#define IDMSG_USER_CHAT_LEFT        1
#define IDMSG_USER_DISCONNECT       2
#define IDMSG_CHAT_NOT_DETECTED     3

#define IDACCTBL_ACL                90

#define IDMNU_MAIN                  100
#define CMD_CONFIG_OPEN             101
#define CMD_CONFIG_CLOSE            102
#define CMD_ATTACH_VIEWER_OPEN      103
#define CMD_ATTACH_VIEWER_CLOSE     104
#define CMD_QUIT                    105
#define CMD_GUI_VISIBLE             106
#define CMD_GUI_HIDDEN              107
#define CMD_CLIENTS_OPEN            108
#define CMD_CLIENTS_CLOSE           109

#define IDMNU_ACL                   200
#define IDM_RULE                    201
#define IDM_ENABLE                  202
#define IDM_DISABLE                 203
#define IDM_ALLOW                   204
#define IDM_DENY                    205
#define IDM_COMMENT                 206

#define IDMNU_CLIENTS               300
#define IDM_DISCONNECT              301
#define IDM_CHAT                    302

#define IDDLG_CONFIG                1000
#define IDNB_CONFIG                 1001
#define IDPB_UNDO                   1002
#define IDPB_DEFAULT                1003

#define IDDLG_PAGE_GENERAL          1100
#define IDCB_PRIM_PSWD              1101
#define IDEF_PRIM_PSWD              1102
#define IDCB_VO_PSWD                1103
#define IDEF_VO_PSWD                1104
#define IDLB_INTERFACES             1105
#define IDSB_PORT                   1106
#define IDSB_HTTP_PORT              1107

#define IDDLG_PAGE_ACCESS           1200
#define IDCN_ACL                    1201
#define IDPB_ACL_NEW                1202
#define IDPB_ACL_REMOVE             1203
#define IDPB_ACL_UP                 1204
#define IDPB_ACL_DOWN               1205
#define IDRB_SHARED_ASIS            1206
#define IDRB_SHARED_ASIS_DONTDISCON 1207
#define IDRB_ALWAYSSHARED           1208
#define IDRB_NEVERSHARED            1209
#define IDRB_NEVERSHARED_DONTDISCON 1210

#define IDDLG_PAGE_OPTIONS          1300
#define IDSB_DEFUPDTIME             1301
#define IDSB_DEFPTRUPDTIME          1302
#define IDSB_SLICEHEIGHT            1303
#define IDCB_FILETRANSFER           1304
#define IDCB_ULTRAVNC               1305
#define IDCB_HTTPPROXY              1306
#define IDCB_GUIVISIBLE             1307

#define IDDLG_PAGE_KBD              1400
#define IDCB_DRIVER_VNCKBD          1401
#define IDCB_DRIVER_KBD             1402

#define IDDLG_PAGE_LOG              1500
#define IDEF_LOG_FILE               1501
#define IDPN_LOG_FIND               1502
#define IDPB_LOG_FOLDER             1503
#define IDSB_LOG_LEVEL              1504
#define IDSB_LOG_SIZE               1505
#define IDSB_LOG_FILES              1506

#define IDDLG_PAGE_PROGRAMS         1600
#define IDMLE_PROGRAMS              1601
#define IDCB_ONLOGON                1602
#define IDEF_ONLOGON                1603
#define IDCB_ONGONE                 1604
#define IDEF_ONGONE                 1605
#define IDCB_ONCAD                  1606
#define IDEF_ONCAD                  1607
#define IDDATA_PROGRAMS_PAGE_HELP   1650

#define IDDLG_ATTACH_VIEWER         1800
#define IDCB_ATTACH_HOST            1801

#define IDDLG_CLIENTS               1900
#define IDCN_CLIENTS                1901

#endif // RESOURCE_H
