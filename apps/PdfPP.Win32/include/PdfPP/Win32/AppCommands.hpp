#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace PdfPP::Win32::Command {

inline constexpr int ID_OPEN = 1001;
inline constexpr int ID_PREVIOUS = 1002;
inline constexpr int ID_NEXT = 1003;
inline constexpr int ID_PAGE = 1004;
inline constexpr int ID_FIND = 1005;
inline constexpr int ID_ZOOM_OUT = 1006;
inline constexpr int ID_ZOOM_IN = 1007;
inline constexpr int ID_STATUS = 1008;
inline constexpr int ID_CANVAS = 1009;
inline constexpr int ID_PAGE_LIST = 1010;
inline constexpr int ID_FIT_WIDTH = 1011;
inline constexpr int ID_SIDEBAR_TITLE = 1012;
inline constexpr int ID_HAND_TOOL = 1013;
inline constexpr int ID_ZOOM_LABEL = 1014;
inline constexpr int ID_VIEW_FIT_PAGE = 1015;
inline constexpr int ID_VIEW_ACTUAL = 1016;
inline constexpr int ID_ABOUT = 1017;
inline constexpr int ID_DOC_PROPERTIES = 1018;
inline constexpr int ID_CLOSE = 1019;
inline constexpr int ID_FIRST_PAGE = 1020;
inline constexpr int ID_LAST_PAGE = 1021;
inline constexpr int ID_SEARCH_NEXT = 1022;
inline constexpr int ID_HOME_TAB = 1023;
inline constexpr int ID_VIEW_TAB = 1024;
inline constexpr int ID_BOOKMARK_CLOSE = 1025;
inline constexpr int ID_BOOKMARKS = 1026;
inline constexpr int ID_FULLSCREEN = 1027;
inline constexpr int ID_VIEW_CONTINUOUS = 1028;
inline constexpr int ID_VIEW_SINGLE_PAGE = 1029;
inline constexpr int ID_SELECT_TOOL = 1030;
inline constexpr int ID_PRINT = 1031;
inline constexpr int ID_SIDEBAR_TOGGLE = 1032;
inline constexpr int ID_RECENT_BASE = 1033;
inline constexpr int ID_ADD_FAVORITE = 1034;
inline constexpr int ID_FAVORITE_BASE = 1035;
inline constexpr int ID_TABBAR = 1036;
inline constexpr int ID_CLOSE_TAB = 1037;
inline constexpr int ID_TOOLS_PANEL = 1038;
inline constexpr int ID_TOOLS_TOGGLE = 1039;
inline constexpr int ID_TOOLS_TITLE = 1040;
inline constexpr int ID_TOOLS_SEARCH = 1041;
inline constexpr int ID_TOOLS_TREE = 1042;

inline constexpr int ID_SHOW_PAGE_SHADOW = 1100;
inline constexpr int ID_MERGE_PDFS = 1101;
inline constexpr int ID_EXTRACT_PAGES = 1102;
inline constexpr int ID_SPLIT_PDF = 1103;
inline constexpr int ID_DELETE_PAGES = 1104;
inline constexpr int ID_MOVE_PAGE = 1105;
inline constexpr int ID_REVERSE_PAGES = 1106;
inline constexpr int ID_DUPLICATE_PAGES = 1107;
inline constexpr int ID_ADD_PASSWORD = 1108;
inline constexpr int ID_REMOVE_PASSWORD = 1109;
inline constexpr int ID_CHANGE_PASSWORD = 1110;
inline constexpr int ID_REORDER_PAGES = 1111;
inline constexpr int ID_FIND_CLOSE = 1112;
inline constexpr int ID_FIND_OPTIONS = 1113;
inline constexpr int ID_FIND_SCOPE_WHOLE_PAGE = 1114;
inline constexpr int ID_FIND_SCOPE_CURRENT_PAGE = 1115;
inline constexpr int ID_FIND_CASE_SENSITIVE = 1116;
inline constexpr int ID_FIND_CASE_INSENSITIVE = 1117;
inline constexpr int ID_FIND_MODE_NORMAL = 1118;
inline constexpr int ID_FIND_MODE_REGEX = 1119;
inline constexpr int ID_FIND_INCLUDE_COMMENTS = 1120;
inline constexpr int ID_FIND_INCLUDE_BOOKMARKS = 1121;
inline constexpr int ID_CRACK_PASSWORD = 1122;

inline constexpr int ID_TOOL_PDF_TEXT_EDITOR = 1201;
inline constexpr int ID_TOOL_COMPARE = 1202;
inline constexpr int ID_TOOL_COMPRESS = 1203;
inline constexpr int ID_TOOL_CONVERT = 1204;
inline constexpr int ID_TOOL_OCR = 1205;
inline constexpr int ID_TOOL_REDACT = 1206;
inline constexpr int ID_TOOL_MULTI_TOOL = 1207;
inline constexpr int ID_TOOL_SIGN_WITH_CERTIFICATE = 1208;
inline constexpr int ID_TOOL_TIMESTAMP_PDF = 1209;
inline constexpr int ID_TOOL_SIGN = 1210;
inline constexpr int ID_TOOL_SHARED_SIGNING = 1211;
inline constexpr int ID_TOOL_ADD_WATERMARK = 1212;
inline constexpr int ID_TOOL_ADD_STAMP = 1213;
inline constexpr int ID_TOOL_SANITIZE = 1214;
inline constexpr int ID_TOOL_FLATTEN = 1215;
inline constexpr int ID_TOOL_UNLOCK_FORMS = 1216;
inline constexpr int ID_TOOL_CHANGE_PERMISSIONS = 1217;
inline constexpr int ID_TOOL_GET_ALL_INFO = 1218;
inline constexpr int ID_TOOL_VALIDATE_SIGNATURE = 1219;
inline constexpr int ID_TOOL_CHANGE_METADATA = 1220;
inline constexpr int ID_TOOL_EDIT_TABLE_OF_CONTENTS = 1221;
inline constexpr int ID_TOOL_READ = 1222;

inline constexpr UINT WM_RENDER_COMPLETE = WM_APP + 41;
inline constexpr UINT WM_OPEN_COMPLETE = WM_APP + 42;
inline constexpr UINT_PTR RENDER_TIMER = 41;
inline constexpr UINT_PTR ZOOM_TIMER = 42;
inline constexpr UINT_PTR TEXT_GEOMETRY_TIMER = 43;

} // namespace PdfPP::Win32::Command
