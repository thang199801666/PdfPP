#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <PdfPP/ModernWin32.hpp>
#include <PdfPP/Win32/AppCommands.hpp>
#include <PdfPP/Win32/AppSettings.hpp>
#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace PdfPP::Win32 {

int RunReaderApplication(HINSTANCE instance, const int show) {
    PdfPP::ModernWin32::Initialize(instance);
    currentDpi = GetDpiForSystem();
    Gdiplus::GdiplusStartupInput gdiplusInput{}; ULONG_PTR gdiplusToken{};
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    appIcon = loadApplicationIcon();
    WNDCLASSW canvasClass{}; canvasClass.hInstance = instance; canvasClass.lpfnWndProc = canvasProc;
    canvasClass.lpszClassName = L"PdfPP.Win32.Canvas"; canvasClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&canvasClass);
    WNDCLASSW tabClass{}; tabClass.hInstance = instance; tabClass.lpfnWndProc = tabBarProc;
    tabClass.lpszClassName = L"PdfPP.Win32.TabStrip"; tabClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    tabClass.hbrBackground = nullptr;
    RegisterClassW(&tabClass);
    WNDCLASSW windowClass{}; windowClass.hInstance = instance; windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = L"PdfPP.Win32.Reader"; windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr; windowClass.hIcon = appIcon;
    RegisterClassW(&windowClass);
    mainWindow = CreateWindowW(windowClass.lpszClassName, L"Pdf++ Reader", WS_OVERLAPPEDWINDOW |
        WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, scaleDip(1200), scaleDip(850),
        nullptr, nullptr, instance, nullptr);
    DragAcceptFiles(mainWindow, TRUE);
    currentDpi = GetDpiForWindow(mainWindow);
    mainMenu = createMainMenu();
    SetMenu(mainWindow, mainMenu);
    LoadSettings(settings);
    zoom = settings.zoom;
    sidebarVisible = settings.sidebarVisible;
    handTool = settings.handTool;
    pageLayoutMode = settings.continuous
        ? PageLayoutMode::ContinuousNavigation : PageLayoutMode::SinglePage;
    rebuildRecentMenu();
    rebuildFavoritesMenu();
    // A dedicated class, not WC_TABCONTROL, so nothing intercepts mouse
    // clicks and the custom close button always receives them.
    tabBar = CreateWindowExW(0, L"PdfPP.Win32.TabStrip", nullptr,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TABBAR)), instance, nullptr);
    openButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Open", ID_OPEN,
        16, 47, 88, 30, true);
    printButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Print", ID_PRINT,
        110, 47, 88, 30);
    previousButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"▲", ID_PREVIOUS,
        112, 47, 34, 30);
    nextButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"▼", ID_NEXT,
        152, 47, 34, 30);
    pageEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER, 196, 49, 50, 26,
        mainWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PAGE)), nullptr, nullptr);
    pageCaption = CreateWindowW(L"STATIC", L"Page:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 34, 18, mainWindow, nullptr, nullptr, nullptr);
    pageLabel = CreateWindowW(L"STATIC", L"/ 0", WS_CHILD | WS_VISIBLE, 252, 52, 52, 20, mainWindow, nullptr, nullptr, nullptr);
    zoomOutButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"−", ID_ZOOM_OUT,
        310, 47, 34, 30);
    zoomLabel = CreateWindowW(L"STATIC", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTER,
        350, 52, 52, 20, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ZOOM_LABEL)), nullptr, nullptr);
    zoomInButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"+", ID_ZOOM_IN,
        408, 47, 34, 30);
    fitButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Fit", ID_FIT_WIDTH,
        448, 47, 76, 30);
    selectButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Select", ID_SELECT_TOOL,
        532, 47, 86, 30, true);
    handButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Hand", ID_HAND_TOOL,
        624, 47, 86, 30);
    findLabel = CreateWindowW(L"STATIC", L"Find:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 36, 18, mainWindow, nullptr, nullptr, nullptr);
    searchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 700, 49, 240, 26,
        mainWindow, nullptr, nullptr, nullptr);
    findButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Find", ID_FIND,
        948, 47, 64, 30);
    sidebarToggleButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"≡", ID_SIDEBAR_TOGGLE,
        1120, 47, 40, 30);
    statusLabel = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_LEFT,
        16, 824, 1100, 20, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
    sidebarTitle = CreateWindowW(L"STATIC", L"Table of Contents", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, PdfPP::ModernWin32::Layout::ribbonHeight,
        PdfPP::ModernWin32::Layout::sidebarWidth, 28, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SIDEBAR_TITLE)), nullptr, nullptr);
    bookmarkCloseButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"x", ID_BOOKMARK_CLOSE,
        0, 0, 24, 24);
    pageList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
        TVS_SHOWSELALWAYS | WS_VSCROLL,
        0, PdfPP::ModernWin32::Layout::ribbonHeight + 28,
        PdfPP::ModernWin32::Layout::sidebarWidth, 760, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PAGE_LIST)), instance, nullptr);
    TreeView_SetBkColor(pageList, PdfPP::ModernWin32::Theme::sidebar);
    TreeView_SetTextColor(pageList, PdfPP::ModernWin32::Theme::text);
    TreeView_SetLineColor(pageList, PdfPP::ModernWin32::Theme::border);
    canvas = CreateWindowW(canvasClass.lpszClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
        PdfPP::ModernWin32::Layout::sidebarWidth,
        PdfPP::ModernWin32::Layout::ribbonHeight, 990, 760, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CANVAS)), instance, nullptr);
    PdfPP::ModernWin32::ApplyDarkMode(mainWindow);
    for (const HWND control : {pageEdit, pageLabel, searchEdit, statusLabel, sidebarTitle, pageList, zoomLabel,
        pageCaption, findLabel, tabBar}) {
        PdfPP::ModernWin32::ApplyDarkMode(control);
    }
    refreshApplicationFonts(currentDpi);
    PdfPP::ModernWin32::ApplyDarkMode(canvas);
    updateCommandState();
    ShowWindow(mainWindow, show); UpdateWindow(mainWindow);
    RECT client{}; GetClientRect(mainWindow, &client); updateLayout(client.right, client.bottom);
    if (!startupOpenPath.empty()) {
        if (std::filesystem::exists(startupOpenPath)) {
            openPath(startupOpenPath);
        }
        startupOpenPath.clear();
    }
    const ACCEL accelerators[] = {
        {FCONTROL | FVIRTKEY, 'O', ID_OPEN},
        {FCONTROL | FVIRTKEY, 'P', ID_PRINT},
        {FCONTROL | FVIRTKEY, 'F', ID_FIND},
        {FCONTROL | FVIRTKEY, 'B', ID_BOOKMARKS},
        {FCONTROL | FSHIFT | FVIRTKEY, 'C', ID_VIEW_CONTINUOUS},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_VIEW_SINGLE_PAGE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'H', ID_SELECT_TOOL},
        {FVIRTKEY, VK_F3, ID_SEARCH_NEXT},
        {FVIRTKEY, VK_F7, ID_HAND_TOOL},
        {FVIRTKEY, VK_F11, ID_FULLSCREEN},
        {FCONTROL | FVIRTKEY, VK_HOME, ID_FIRST_PAGE},
        {FCONTROL | FVIRTKEY, VK_PRIOR, ID_PREVIOUS},
        {FCONTROL | FVIRTKEY, VK_NEXT, ID_NEXT},
        {FCONTROL | FVIRTKEY, VK_END, ID_LAST_PAGE},
        {FCONTROL | FVIRTKEY, '0', ID_VIEW_FIT_PAGE},
        {FCONTROL | FVIRTKEY, '1', ID_FIT_WIDTH},
        {FCONTROL | FVIRTKEY, '2', ID_VIEW_ACTUAL},
        {FCONTROL | FVIRTKEY, 'D', ID_ADD_FAVORITE},
        {FCONTROL | FVIRTKEY, 'W', ID_CLOSE_TAB},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_ZOOM_OUT},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_ZOOM_IN},
    };
    const HACCEL acceleratorTable = CreateAcceleratorTableW(
        const_cast<LPACCEL>(accelerators), static_cast<int>(std::size(accelerators)));
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_MOUSEWHEEL || message.message == WM_MOUSEHWHEEL) {
            POINT cursor{};
            GetCursorPos(&cursor);
            const HWND hovered = WindowFromPoint(cursor);
            if (hovered == canvas || IsChild(canvas, hovered)) {
                SendMessageW(canvas, message.message, message.wParam, message.lParam);
                continue;
            }
        }
        if (TranslateAcceleratorW(mainWindow, acceleratorTable, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (acceleratorTable) DestroyAcceleratorTable(acceleratorTable);
    if (regularUiFont) DeleteObject(regularUiFont);
    if (boldUiFont) DeleteObject(boldUiFont);
    if (appIcon) DestroyIcon(appIcon);
    Gdiplus::GdiplusShutdown(gdiplusToken); return static_cast<int>(message.wParam);
}

int RunReaderApplication(HINSTANCE instance, const int show, PWSTR commandLine) {
    // Accept an optional path argument ("PdfPP.Win32.exe file.pdf") and open
    // it once the window and message pump are up.
    std::wstring argument;
    if (commandLine && *commandLine) {
        std::wstring line(commandLine);
        while (!line.empty() && (line.front() == L' ' || line.front() == L'\t')) line.erase(line.begin());
        if (!line.empty() && line.front() == L'"') {
            const std::size_t close = line.find(L'"', 1);
            if (close != std::wstring::npos) argument = line.substr(1, close - 1);
        } else if (!line.empty()) {
            argument = line;
        }
    }
    startupOpenPath = argument;
    return RunReaderApplication(instance, show);
}

} // namespace PdfPP::Win32
