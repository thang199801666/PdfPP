#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <PdfPP/ModernWin32.hpp>
#include <PdfPP/Win32/AppCommands.hpp>
#include <PdfPP/Win32/AppSettings.hpp>
#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace PdfPP::Win32 {

namespace {

constexpr UINT_PTR kPageEditSubclassId = 0x50414745U; // "PAGE"
constexpr UINT_PTR kSidebarPanelSubclassId = 0x5342504EU; // "SBPN"
constexpr UINT_PTR kToolsPanelSubclassId = 0x54504E4CU; // "TPNL"

LRESULT CALLBACK sidebarPanelSubclassProc(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam,
                                          UINT_PTR subclassId, DWORD_PTR) {
    switch (message) {
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        // The TOC and tools controls are real children of their visual panel.
        // Forward their notifications and color requests to the reader window
        // so one place owns the UI behavior and theming.
        if (const HWND parent = GetParent(window)) {
            return SendMessageW(parent, message, wParam, lParam);
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, sidebarPanelSubclassProc, subclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK pageEditSubclassProc(HWND window, UINT message,
                                      WPARAM wParam, LPARAM lParam,
                                      UINT_PTR subclassId, DWORD_PTR) {
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            wchar_t value[32]{};
            GetWindowTextW(window, value, 32);
            const int requestedPage = _wtoi(value) - 1;
            if (requestedPage >= 0 && requestedPage < pageCount) {
                goToPage(requestedPage);
            } else {
                updatePageControls();
            }
            return 0;
        }
        break;
    case WM_CHAR:
        // The page edit uses ES_MULTILINE only to gain control over its text
        // formatting rectangle. Keep its user-facing behavior single-line.
        if (wParam == L'\r' || wParam == L'\n') return 0;
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, pageEditSubclassProc, subclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

int RunReaderApplication(HINSTANCE instance, const int show) {
    PdfPP::ModernWin32::Initialize(instance);
    currentDpi = GetDpiForSystem();
    Gdiplus::GdiplusStartupInput gdiplusInput{}; ULONG_PTR gdiplusToken{};
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    appIcon = loadApplicationIcon();
    WNDCLASSW canvasClass{}; canvasClass.style = CS_DBLCLKS; canvasClass.hInstance = instance; canvasClass.lpfnWndProc = canvasProc;
    canvasClass.lpszClassName = L"PdfPP.Win32.Canvas"; canvasClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&canvasClass);
    WNDCLASSW findPanelClass{}; findPanelClass.style = CS_HREDRAW | CS_VREDRAW;
    findPanelClass.hInstance = instance; findPanelClass.lpfnWndProc = findPanelProc;
    findPanelClass.lpszClassName = L"PdfPP.Win32.FindPanel";
    findPanelClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    findPanelClass.hbrBackground = nullptr;
    RegisterClassW(&findPanelClass);
    WNDCLASSW tabClass{}; tabClass.hInstance = instance; tabClass.lpfnWndProc = tabBarProc;
    tabClass.lpszClassName = L"PdfPP.Win32.TabStrip"; tabClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    tabClass.hbrBackground = nullptr;
    RegisterClassW(&tabClass);
    WNDCLASSW windowClass{}; windowClass.hInstance = instance; windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = L"PdfPP.Win32.Reader"; windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr; windowClass.hIcon = appIcon;
    RegisterClassW(&windowClass);
    mainWindow = CreateWindowW(windowClass.lpszClassName, L"Pdf++ Reader", WS_OVERLAPPEDWINDOW |
        WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, scaleDip(1200), scaleDip(850),
        nullptr, nullptr, instance, nullptr);
    DragAcceptFiles(mainWindow, TRUE);
    currentDpi = GetDpiForWindow(mainWindow);
    mainMenu = createMainMenu();
    SetMenu(mainWindow, mainMenu);
    LoadSettings(settings);
    zoom = settings.zoom;
    sidebarVisible = settings.sidebarVisible;
    sidebarWidthDip = std::clamp(settings.sidebarWidth,
        kSidebarMinWidthDip, kSidebarMaxWidthDip);
    toolsVisible = settings.toolsVisible;
    toolsWidthDip = std::clamp(settings.toolsWidth,
        kToolsMinWidthDip, kToolsMaxWidthDip);
    const int initialSidebarWidth = scaleDip(sidebarWidthDip);
    const int initialToolsWidth = toolsVisible ? scaleDip(toolsWidthDip) : 0;
    const int initialContentLeft = sidebarVisible
        ? initialSidebarWidth + scaleDip(kSidebarSplitterWidthDip)
        : 0;
    const int initialContentRight = (std::max)(initialContentLeft, scaleDip(1200) - initialToolsWidth);
    // Always start in Select mode. Tool choice is a transient interaction
    // state and should not persist across application launches.
    handTool = false;
    settings.handTool = false;
    pageLayoutMode = settings.continuous
        ? PageLayoutMode::ContinuousNavigation : PageLayoutMode::SinglePage;
    rebuildRecentMenu();
    rebuildFavoritesMenu();
    // A dedicated class, not WC_TABCONTROL, so nothing intercepts mouse
    // clicks and the custom close button always receives them.
    tabBar = CreateWindowExW(0, L"PdfPP.Win32.TabStrip", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
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
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER | ES_MULTILINE,
        196, 49, 30, 24,
        mainWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PAGE)), nullptr, nullptr);
    auto pageTextBoxStyle = PdfPP::ModernWin32::MacTextBoxTemplate(
        PdfPP::ModernWin32::ControlSize::Small);
    pageTextBoxStyle.horizontalPaddingDip = 3;
    pageTextBoxStyle.cornerRadiusDip = 6;
    PdfPP::ModernWin32::ApplyTextBoxTemplate(pageEdit, pageTextBoxStyle);
    SetWindowSubclass(pageEdit, pageEditSubclassProc, kPageEditSubclassId, 0);
    pageCaption = CreateWindowW(L"STATIC", L"Page:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 34, 18, mainWindow, nullptr, nullptr, nullptr);
    pageLabel = CreateWindowW(L"STATIC", L"/ 0", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        252, 52, 52, 20, mainWindow, nullptr, nullptr, nullptr);
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
    toolsToggleButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Tools", ID_TOOLS_TOGGLE,
        1068, 47, 54, 30);
    sidebarToggleButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"≡", ID_SIDEBAR_TOGGLE,
        1128, 47, 40, 30);
    statusLabel = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_LEFT,
        16, 824, 1100, 20, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
    sidebarPanel = PdfPP::ModernWin32::CreateMacPanel(mainWindow, instance, 0,
        0, PdfPP::ModernWin32::Layout::ribbonHeight,
        initialSidebarWidth, 760, false);
    // Make the panel the actual parent of the TOC controls. This removes the
    // overlapping-sibling paint race that made items flash once and then get
    // covered by the panel background. WS_CLIPCHILDREN guarantees the panel
    // never paints on top of its title/tree/button children.
    if (sidebarPanel) {
        const LONG_PTR style = GetWindowLongPtrW(sidebarPanel, GWL_STYLE);
        SetWindowLongPtrW(sidebarPanel, GWL_STYLE,
            style | static_cast<LONG_PTR>(WS_CLIPCHILDREN));
        SetWindowSubclass(sidebarPanel, sidebarPanelSubclassProc,
            kSidebarPanelSubclassId, 0);
    }
    auto sidebarPanelStyle = PdfPP::ModernWin32::MacPanelTemplate(false);
    sidebarPanelStyle.surface = PdfPP::ModernWin32::Theme::sidebar;
    sidebarPanelStyle.fill = RGB(255, 255, 255);
    sidebarPanelStyle.border = RGB(224, 227, 232);
    sidebarPanelStyle.cornerRadiusDip = 10;
    sidebarPanelStyle.drawShadow = false;
    PdfPP::ModernWin32::ApplyPanelTemplate(sidebarPanel, sidebarPanelStyle);

    sidebarTitle = CreateWindowW(L"STATIC", L"Table of Contents",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, initialSidebarWidth, 36, sidebarPanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SIDEBAR_TITLE)), nullptr, nullptr);
    bookmarkCloseButton = PdfPP::ModernWin32::CreateActionButton(sidebarPanel, instance, L"×", ID_BOOKMARK_CLOSE,
        0, 0, 22, 22);
    pageList = CreateWindowExW(0, WC_TREEVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_LINESATROOT |
        TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | WS_VSCROLL,
        0, 36, initialSidebarWidth, 760, sidebarPanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PAGE_LIST)), instance, nullptr);
    TreeView_SetBkColor(pageList, RGB(255, 255, 255));
    TreeView_SetTextColor(pageList, PdfPP::ModernWin32::Theme::text);
    TreeView_SetLineColor(pageList, RGB(226, 229, 234));
    TreeView_SetIndent(pageList, scaleDip(18));
    TreeView_SetItemHeight(pageList, scaleDip(26));
#if defined(TVS_EX_DOUBLEBUFFER) && defined(TreeView_SetExtendedStyle)
    TreeView_SetExtendedStyle(pageList, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
#endif
    toolsPanel = PdfPP::ModernWin32::CreateMacPanel(mainWindow, instance, ID_TOOLS_PANEL,
        0, PdfPP::ModernWin32::Layout::ribbonHeight,
        initialToolsWidth, 760, false);
    if (toolsPanel) {
        const LONG_PTR style = GetWindowLongPtrW(toolsPanel, GWL_STYLE);
        SetWindowLongPtrW(toolsPanel, GWL_STYLE, style | static_cast<LONG_PTR>(WS_CLIPCHILDREN));
        SetWindowSubclass(toolsPanel, sidebarPanelSubclassProc,
            kToolsPanelSubclassId, 0);
    }
    auto toolsPanelStyle = PdfPP::ModernWin32::MacPanelTemplate(false);
    toolsPanelStyle.surface = RGB(248, 249, 251);
    toolsPanelStyle.fill = RGB(248, 249, 251);
    toolsPanelStyle.border = RGB(224, 227, 232);
    toolsPanelStyle.cornerRadiusDip = 10;
    toolsPanelStyle.drawShadow = false;
    PdfPP::ModernWin32::ApplyPanelTemplate(toolsPanel, toolsPanelStyle);
    toolsTitle = CreateWindowW(L"STATIC", L"Tools",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, initialToolsWidth, 34, toolsPanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOLS_TITLE)), nullptr, nullptr);
    toolsSearchEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, toolsPanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOLS_SEARCH)), instance, nullptr);
    PdfPP::ModernWin32::ApplyMacTextBoxStyle(toolsSearchEdit, PdfPP::ModernWin32::ControlSize::Regular);
    SendMessageW(toolsSearchEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search tools..."));
    toolsTree = CreateWindowExW(0, WC_TREEVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_LINESATROOT |
        TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | WS_VSCROLL,
        0, 0, initialToolsWidth, 700, toolsPanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOLS_TREE)), instance, nullptr);
    TreeView_SetBkColor(toolsTree, RGB(248, 249, 251));
    TreeView_SetTextColor(toolsTree, PdfPP::ModernWin32::Theme::text);
    TreeView_SetLineColor(toolsTree, RGB(226, 229, 234));
    TreeView_SetIndent(toolsTree, scaleDip(16));
    TreeView_SetItemHeight(toolsTree, scaleDip(24));
#if defined(TVS_EX_DOUBLEBUFFER) && defined(TreeView_SetExtendedStyle)
    TreeView_SetExtendedStyle(toolsTree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
#endif
    canvas = CreateWindowW(canvasClass.lpszClassName, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN,
        initialContentLeft,
        PdfPP::ModernWin32::Layout::ribbonHeight, (std::max)(0, initialContentRight - initialContentLeft), 760, mainWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CANVAS)), instance, nullptr);
    // Acrobat-style transient find bar. The panel is a real child of the
    // canvas, so it floats above page pixels without reserving ribbon space.
    // Its edit/buttons are children of the panel and move as one z-ordered unit.
    findPanel = CreateWindowExW(WS_EX_CONTROLPARENT, findPanelClass.lpszClassName, nullptr,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, canvas, nullptr, instance, nullptr);
    PdfPP::ModernWin32::ApplyMacPanelStyle(findPanel, true);
    searchEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_CENTER,
        0, 0, 0, 0, findPanel, nullptr, instance, nullptr);
    PdfPP::ModernWin32::ApplyMacTextBoxStyle(
        searchEdit, PdfPP::ModernWin32::ControlSize::Regular);
    SendMessageW(searchEdit, EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(L"Find text"));
    findButton = CreateWindowW(L"BUTTON", L"...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, findPanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_OPTIONS)), instance, nullptr);
    findCloseButton = CreateWindowW(L"BUTTON", L"×",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, findPanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIND_CLOSE)), instance, nullptr);
    PdfPP::ModernWin32::ApplyMacButtonStyle(
        findButton, PdfPP::ModernWin32::ButtonStyle::Ghost);
    PdfPP::ModernWin32::ApplyMacButtonStyle(
        findCloseButton, PdfPP::ModernWin32::ButtonStyle::Ghost);
    PdfPP::ModernWin32::SetActionButtonStyle(
        bookmarkCloseButton, PdfPP::ModernWin32::ButtonStyle::Ghost);
    ShowWindow(findPanel, SW_HIDE);
    PdfPP::ModernWin32::ApplyDarkMode(mainWindow);
    for (const HWND control : {pageEdit, pageLabel, statusLabel, sidebarPanel, sidebarTitle, pageList, toolsPanel, toolsTitle, toolsSearchEdit, toolsTree, zoomLabel,
        pageCaption, tabBar}) {
        PdfPP::ModernWin32::ApplyDarkMode(control);
    }
    refreshApplicationFonts(currentDpi);
    populateToolsTree();
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
        // Handle Ctrl+F before accelerator/dialog processing so it works no
        // matter which child control currently owns keyboard focus.
        if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) &&
            message.wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            showFindPanel(true);
            continue;
        }
        if (message.hwnd == searchEdit && message.message == WM_KEYDOWN) {
            if (message.wParam == VK_RETURN) {
                findText();
                continue;
            }
            if (message.wParam == VK_ESCAPE) {
                showFindPanel(false);
                continue;
            }
        }
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
