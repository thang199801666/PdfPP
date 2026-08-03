#pragma once

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

inline constexpr UINT WM_RENDER_COMPLETE = WM_APP + 41;
inline constexpr UINT WM_OPEN_COMPLETE = WM_APP + 42;
inline constexpr UINT_PTR RENDER_TIMER = 41;
inline constexpr UINT_PTR ZOOM_TIMER = 42;

} // namespace PdfPP::Win32::Command
