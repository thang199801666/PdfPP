#include <PdfPP/Win32/ReaderState.hpp>

#include <gdiplus.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace PdfPP::Win32 {

void rebuildRecentMenu() {
    if (!recentMenu) return;
    while (GetMenuItemCount(recentMenu) > 0) {
        DeleteMenu(recentMenu, 0, MF_BYPOSITION);
    }
    if (settings.recentFiles.empty()) {
        AppendMenuW(recentMenu, MF_STRING | MF_GRAYED, ID_RECENT_BASE,
            L"No recent files");
        return;
    }
    for (std::size_t index = 0; index < settings.recentFiles.size(); ++index) {
        AppendMenuW(recentMenu, MF_STRING, ID_RECENT_BASE + static_cast<int>(index),
            settings.recentFiles[index].c_str());
    }
}

void persistSettings() {
    settings.zoom = zoom;
    settings.continuous = pageLayoutMode == PageLayoutMode::ContinuousNavigation;
    settings.sidebarVisible = sidebarVisible;
    settings.sidebarWidth = std::clamp(sidebarWidthDip,
        kSidebarMinWidthDip, kSidebarMaxWidthDip);
    // Select is always the startup tool; do not persist Hand mode.
    settings.handTool = false;
    settings.toolsVisible = toolsVisible;
    settings.toolsWidth = std::clamp(toolsWidthDip, kToolsMinWidthDip, kToolsMaxWidthDip);
    SaveSettings(settings);
    rebuildRecentMenu();
}

void rememberRecentFile(const std::wstring& path) {
    settings.AddRecent(path);
    persistSettings();
}

void rebuildFavoritesMenu() {
    if (!favoritesMenuHandle) return;
    // Keep the "Add Current Page" item and separator, clear the rest.
    while (GetMenuItemCount(favoritesMenuHandle) > 2) {
        DeleteMenu(favoritesMenuHandle, 2, MF_BYPOSITION);
    }
    if (settings.favorites.empty()) {
        AppendMenuW(favoritesMenuHandle, MF_STRING | MF_GRAYED, ID_FAVORITE_BASE,
            L"No favorites yet");
        return;
    }
    for (std::size_t index = 0; index < settings.favorites.size(); ++index) {
        const auto& favorite = settings.favorites[index];
        std::wstring label = favorite.title.empty()
            ? std::filesystem::path(favorite.path).filename().wstring()
            : favorite.title;
        label += L" (page " + std::to_wstring(favorite.page + 1) + L")";
        AppendMenuW(favoritesMenuHandle, MF_STRING,
            ID_FAVORITE_BASE + static_cast<int>(index), label.c_str());
    }
}

void toggleCurrentFavorite() {
    if (!document) return;
    const std::wstring title = utf8ToWide(document->Title().c_str());
    if (settings.HasFavorite(currentFilePath, pageIndex)) {
        settings.RemoveFavorite(currentFilePath, pageIndex);
        setStatus(L"Removed favorite");
    } else {
        settings.AddFavorite(Favorite{ currentFilePath, pageIndex, title });
        setStatus(L"Added page " + std::to_wstring(pageIndex + 1) + L" to favorites");
    }
    persistSettings();
    rebuildFavoritesMenu();
}

void showAboutDialog() {
    MessageBoxW(mainWindow,
        L"Pdf++ Reader\n\nA reusable native Win32 PDF workspace built on the Pdf++ kernel.",
        L"About Pdf++", MB_OK | MB_ICONINFORMATION);
}

void showDocumentProperties() {
    std::wstring message = L"Pages: " + std::to_wstring(pageCount) + L"\n";
    message += L"Current page: " + std::to_wstring(pageIndex + 1) + L"\n";
    message += L"Zoom: " + std::to_wstring(static_cast<int>(std::lround(zoom * 100.0))) + L"%";
    MessageBoxW(mainWindow, message.c_str(), L"Document Properties", MB_OK | MB_ICONINFORMATION);
}

HICON loadApplicationIcon() {
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back(L"icons\\PDF++.png");
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const auto executableDirectory = std::filesystem::path(modulePath).parent_path();
        candidates.push_back(executableDirectory / L"icons" / L"PDF++.png");
        candidates.push_back(executableDirectory.parent_path().parent_path().parent_path() /
            L"icons" / L"PDF++.png");
    }
    for (const auto& path : candidates) {
        if (!std::filesystem::exists(path)) continue;
        Gdiplus::Bitmap bitmap(path.wstring().c_str());
        if (bitmap.GetLastStatus() != Gdiplus::Ok) continue;
        HICON icon{};
        if (bitmap.GetHICON(&icon) == Gdiplus::Ok && icon) return icon;
    }
    return LoadIconW(nullptr, IDI_APPLICATION);
}

HMENU createMainMenu() {
    const HMENU menu = CreateMenu();
    const HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_OPEN, L"Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_PRINT, L"Print...\tCtrl+P");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_CLOSE_TAB, L"Close Tab\tCtrl+W");
    AppendMenuW(file, MF_STRING, ID_CLOSE, L"Close");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    recentMenu = CreatePopupMenu();
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(recentMenu), L"Recent Files");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, SC_CLOSE, L"Exit\tAlt+F4");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File");

    const HMENU documentMenu = CreatePopupMenu();
    AppendMenuW(documentMenu, MF_STRING, ID_MERGE_PDFS, L"Merge PDFs...");
    AppendMenuW(documentMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(documentMenu, MF_STRING, ID_EXTRACT_PAGES, L"Extract Pages...");
    AppendMenuW(documentMenu, MF_STRING, ID_SPLIT_PDF, L"Split PDF...");
    AppendMenuW(documentMenu, MF_STRING, ID_DELETE_PAGES, L"Delete Pages...");
    AppendMenuW(documentMenu, MF_STRING, ID_DUPLICATE_PAGES, L"Duplicate Pages...");
    AppendMenuW(documentMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(documentMenu, MF_STRING, ID_MOVE_PAGE, L"Move Current Page...");
    AppendMenuW(documentMenu, MF_STRING, ID_REORDER_PAGES, L"Reorder Pages...");
    AppendMenuW(documentMenu, MF_STRING, ID_REVERSE_PAGES, L"Reverse Page Order...");
    AppendMenuW(documentMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(documentMenu, MF_STRING, ID_CRACK_PASSWORD, L"Crack Password...");
    AppendMenuW(documentMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(documentMenu, MF_STRING, ID_ADD_PASSWORD, L"Add Password...");
    AppendMenuW(documentMenu, MF_STRING, ID_REMOVE_PASSWORD, L"Remove Password...");
    AppendMenuW(documentMenu, MF_STRING, ID_CHANGE_PASSWORD, L"Change Password...");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(documentMenu), L"Document");

    const HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, ID_BOOKMARKS, L"Table of Contents\tCtrl+B");
    AppendMenuW(view, MF_STRING, ID_FULLSCREEN, L"Fullscreen\tF11");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_VIEW_CONTINUOUS, L"Continuous\tCtrl+Shift+C");
    AppendMenuW(view, MF_STRING, ID_VIEW_SINGLE_PAGE, L"Single Page\tCtrl+Shift+S");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_HAND_TOOL, L"Hand Tool\tF7");
    AppendMenuW(view, MF_STRING, ID_SELECT_TOOL, L"Select Text\tCtrl+Shift+H");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"View");

    const HMENU goTo = CreatePopupMenu();
    AppendMenuW(goTo, MF_STRING, ID_FIND, L"Find...\tCtrl+F");
    AppendMenuW(goTo, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(goTo, MF_STRING, ID_FIRST_PAGE, L"First Page\tCtrl+Home");
    AppendMenuW(goTo, MF_STRING, ID_PREVIOUS, L"Previous Page\tCtrl+Page Up");
    AppendMenuW(goTo, MF_STRING, ID_NEXT, L"Next Page\tCtrl+Page Down");
    AppendMenuW(goTo, MF_STRING, ID_LAST_PAGE, L"Last Page\tCtrl+End");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(goTo), L"Go To");

    const HMENU zoomMenu = CreatePopupMenu();
    AppendMenuW(zoomMenu, MF_STRING, ID_VIEW_FIT_PAGE, L"Fit Page\tCtrl+0");
    AppendMenuW(zoomMenu, MF_STRING, ID_FIT_WIDTH, L"Fit Width\tCtrl+1");
    AppendMenuW(zoomMenu, MF_STRING, ID_VIEW_ACTUAL, L"Actual Size (100%)\tCtrl+2");
    AppendMenuW(zoomMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(zoomMenu, MF_STRING, ID_ZOOM_IN, L"Zoom In\tCtrl++");
    AppendMenuW(zoomMenu, MF_STRING, ID_ZOOM_OUT, L"Zoom Out\tCtrl+-");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(zoomMenu), L"Zoom");

    const HMENU settingsMenu = CreatePopupMenu();
    AppendMenuW(settingsMenu, MF_STRING, ID_DOC_PROPERTIES, L"Document Properties");
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu, MF_STRING, ID_SHOW_PAGE_SHADOW, L"Show Page Shadow");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(settingsMenu), L"Settings");

    const HMENU favoritesMenu = CreatePopupMenu();
    favoritesMenuHandle = favoritesMenu;
    AppendMenuW(favoritesMenu, MF_STRING, ID_ADD_FAVORITE, L"Add Current Page to Favorites\tCtrl+D");
    AppendMenuW(favoritesMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(favoritesMenu), L"Favorites");

    const HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"About Pdf++");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Help");
    return menu;
}

void refreshApplicationFonts(const UINT dpi) {
    HFONT newRegular = PdfPP::ModernWin32::UiFontForDpi(dpi, 7, false);
    HFONT newBold = PdfPP::ModernWin32::UiFontForDpi(dpi, 8, true);
    HFONT newPageRegular = PdfPP::ModernWin32::UiFontForDpi(dpi, 9, false);
    HFONT newPageBold = PdfPP::ModernWin32::UiFontForDpi(dpi, 9, true);
    if (!newRegular || !newBold || !newPageRegular || !newPageBold) {
        if (newRegular) DeleteObject(newRegular);
        if (newBold) DeleteObject(newBold);
        if (newPageRegular) DeleteObject(newPageRegular);
        if (newPageBold) DeleteObject(newPageBold);
        return;
    }

    for (const HWND control : {openButton, zoomOutButton, zoomLabel, zoomInButton, fitButton,
        selectButton, handButton, toolsToggleButton, findPanel, searchEdit, findButton, findCloseButton, statusLabel,
        pageList, bookmarkCloseButton, toolsSearchEdit, toolsTree, tabBar}) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newRegular), TRUE);
    }
    for (const HWND control : {previousButton, nextButton, pageEdit, pageCaption, pageLabel}) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newPageRegular), TRUE);
    }
    if (pageCaption)
        SendMessageW(pageCaption, WM_SETFONT, reinterpret_cast<WPARAM>(newPageBold), TRUE);
    if (pageList) {
        SendMessageW(pageList, WM_SETFONT, reinterpret_cast<WPARAM>(newPageRegular), TRUE);
        TreeView_SetIndent(pageList, MulDiv(18, static_cast<int>(dpi), 96));
        TreeView_SetItemHeight(pageList, MulDiv(26, static_cast<int>(dpi), 96));
    }
    if (sidebarTitle)
        SendMessageW(sidebarTitle, WM_SETFONT, reinterpret_cast<WPARAM>(newPageBold), TRUE);
    if (toolsTitle)
        SendMessageW(toolsTitle, WM_SETFONT, reinterpret_cast<WPARAM>(newPageBold), TRUE);
    if (toolsTree) {
        SendMessageW(toolsTree, WM_SETFONT, reinterpret_cast<WPARAM>(newRegular), TRUE);
        TreeView_SetIndent(toolsTree, MulDiv(16, static_cast<int>(dpi), 96));
        TreeView_SetItemHeight(toolsTree, MulDiv(24, static_cast<int>(dpi), 96));
    }

    const HFONT oldRegular = regularUiFont;
    const HFONT oldBold = boldUiFont;
    const HFONT oldPageRegular = pageUiFont;
    const HFONT oldPageBold = pageBoldUiFont;
    regularUiFont = newRegular;
    boldUiFont = newBold;
    pageUiFont = newPageRegular;
    pageBoldUiFont = newPageBold;
    if (oldRegular) DeleteObject(oldRegular);
    if (oldBold) DeleteObject(oldBold);
    if (oldPageRegular) DeleteObject(oldPageRegular);
    if (oldPageBold) DeleteObject(oldPageBold);
}

} // namespace PdfPP::Win32
