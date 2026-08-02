#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace PdfPP::Win32 {

bool tryCrossPageBoundary() {
    const ULONGLONG now = GetTickCount64();
    if (now - lastPageCrossTick < kPageCrossDebounceMs) return false;
    lastPageCrossTick = now;
    return true;
}

int pageLeft(const RECT& client, const int width) {
    return std::max(scaleDip(12), static_cast<int>((client.right - width) / 2));
}

void updateCommandState() {
    if (!mainMenu) return;
    const bool hasDocument = document && pageCount > 0;
    const bool hasPrevious = hasDocument && pageIndex > 0;
    const bool hasNext = hasDocument && pageIndex + 1 < pageCount;

    EnableMenuItem(mainMenu, ID_CLOSE, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_CLOSE_TAB, MF_BYCOMMAND | (!tabs.empty() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_PRINT, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    if (printButton) EnableWindow(printButton, hasDocument);
    EnableMenuItem(mainMenu, ID_FIRST_PAGE, MF_BYCOMMAND | (hasPrevious ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_PREVIOUS, MF_BYCOMMAND | (hasPrevious ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_NEXT, MF_BYCOMMAND | (hasNext ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_LAST_PAGE, MF_BYCOMMAND | (hasNext ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_FIND, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_SEARCH_NEXT, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_DOC_PROPERTIES, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_ZOOM_OUT, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_ZOOM_IN, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_FIT_WIDTH, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_VIEW_FIT_PAGE, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_VIEW_ACTUAL, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));

    CheckMenuItem(mainMenu, ID_BOOKMARKS,
        MF_BYCOMMAND | (sidebarVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(mainMenu, ID_HAND_TOOL,
        MF_BYCOMMAND | (handTool ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(mainMenu, ID_FULLSCREEN,
        MF_BYCOMMAND | (fullscreen ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(mainMenu, ID_VIEW_CONTINUOUS,
        MF_BYCOMMAND | (pageLayoutMode == PageLayoutMode::ContinuousNavigation
            ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(mainMenu, ID_VIEW_SINGLE_PAGE,
        MF_BYCOMMAND | (pageLayoutMode == PageLayoutMode::SinglePage
            ? MF_CHECKED : MF_UNCHECKED));
    if (selectButton) PdfPP::ModernWin32::SetActionButtonAccent(selectButton, !handTool);
    if (handButton) PdfPP::ModernWin32::SetActionButtonAccent(handButton, handTool);
}

void selectBookmarkPage() {
    if (!pageList) return;
    for (const HTREEITEM item : tocItems) {
        TVITEMW value{};
        value.mask = TVIF_PARAM;
        value.hItem = item;
        if (!TreeView_GetItem(pageList, &value) || value.lParam != pageIndex) continue;
        if (TreeView_GetSelection(pageList) == item) return;
        updatingBookmarkSelection = true;
        TreeView_SelectItem(pageList, item);
        updatingBookmarkSelection = false;
        return;
    }
}

void populateBookmarkTree(const std::wstring&) {
    if (!pageList) return;
    TreeView_DeleteAllItems(pageList);
    tocItems.clear();
    bookmarkRoot = TVI_ROOT;
    const std::vector<TocItem> toc = document ? document->TableOfContents() : std::vector<TocItem>{};
    if (toc.empty()) {
        std::wstring emptyText = L"No table of contents available";
        TVINSERTSTRUCTW item{};
        item.hParent = bookmarkRoot;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = emptyText.data();
        item.item.lParam = -1;
        TreeView_InsertItem(pageList, &item);
    } else {
        std::vector<HTREEITEM> parents;
        for (std::size_t index = 0; index < toc.size(); ++index) {
            const TocItem& entry = toc[index];
            const int level = std::max(0, entry.level);
            while (parents.size() > static_cast<std::size_t>(level)) parents.pop_back();
            const HTREEITEM parent = parents.empty() ? bookmarkRoot : parents.back();
            std::wstring label = entry.title.empty() ? L"(untitled)" : entry.title;
            const bool hasChild = index + 1U < toc.size() && toc[index + 1U].level > level;
            TVINSERTSTRUCTW item{};
            item.hParent = parent;
            item.hInsertAfter = TVI_LAST;
            item.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
            item.item.pszText = label.data();
            item.item.lParam = entry.page;
            item.item.cChildren = hasChild ? 1 : 0;
            const HTREEITEM inserted = TreeView_InsertItem(pageList, &item);
            if (inserted) tocItems.push_back(inserted);
            if (parents.size() == static_cast<std::size_t>(level)) parents.push_back(inserted);
            else if (level < static_cast<int>(parents.size())) parents[static_cast<std::size_t>(level)] = inserted;
        }
    }
    selectBookmarkPage();
}

void updatePageControls() {
    wchar_t value[32]{};
    swprintf_s(value, L"%d", pageIndex + 1);
    SetWindowTextW(pageEdit, value);
    wchar_t label[64]{};
    swprintf_s(label, L"/ %d", pageCount);
    SetWindowTextW(pageLabel, label);
    EnableWindow(previousButton, document && pageIndex > 0);
    EnableWindow(nextButton, document && pageIndex + 1 < pageCount);
    selectBookmarkPage();
    updateZoomLabel();
    updateCommandState();
}

void closeDocument() {
    KillTimer(mainWindow, RENDER_TIMER);
    if (openThread.joinable()) openThread.join();
    openReady.store(false, std::memory_order_release);
    {
        std::lock_guard lock(openMutex);
        openResult = {};
    }
    if (renderThread.joinable()) renderThread.join();
    renderReady.store(false, std::memory_order_release);
    {
        std::lock_guard lock(renderMutex);
        renderResult = {};
    }
    document.reset(); pageCount = 0; pageIndex = 0; pixels.clear(); textChunks.clear();
    searchHighlights.clear(); selectedChunks.clear(); pageCache.Clear();
    continuousNextPage.reset();
    currentFilePath.clear();
    pixelWidth = pixelHeight = pixelStride = 0;
    pagePixelHeights.clear();
    pagePixelOffsets.clear();
    documentPixelHeight = 0;
    pendingScrollY = -1;
    geometryZoom = -1.0;
    geometryDpi = 0;
    geometryDocument.reset();
    zoomAnchor = {};
    pageArrivalRequest.reset();
    lastSearchQuery.clear();
    lastSearchPage = -1;
    TreeView_DeleteAllItems(pageList);
    tocItems.clear();
    bookmarkRoot = {};
    tabs.clear();
    activeTab = -1;
    if (tabBar) InvalidateRect(tabBar, nullptr, TRUE);
    updatePageControls();
    InvalidateRect(canvas, nullptr, TRUE);
}

void saveSettingsOnExit() {
    persistSettings();
}

void findText() {
    if (!document) return;
    wchar_t query[256]{};
    GetWindowTextW(searchEdit, query, 256);
    if (!query[0]) return;
    const std::wstring requestedQuery(query);
    const bool newQuery = requestedQuery != lastSearchQuery;
    const int startPage = newQuery
        ? std::clamp(pageIndex, 0, std::max(0, pageCount - 1))
        : (lastSearchPage + 1) % std::max(1, pageCount);
    lastSearchQuery = requestedQuery;
    for (int offset = 0; offset < pageCount; ++offset) {
        const int page = (startPage + offset) % pageCount;
        const std::wstring text = utf8ToWide(document->Text(page).c_str());
        if (text.find(requestedQuery) != std::wstring::npos) {
            lastSearchPage = page;
            goToPage(page);
            setStatus(L"Found on page " + std::to_wstring(page + 1));
            return;
        }
    }
    lastSearchPage = -1;
    setStatus(L"Text not found");
}

void openPath(const std::wstring& selectedPath) {
    if (openThread.joinable()) return;
    setStatus(L"Opening PDF...");
    EnableWindow(openButton, FALSE);
    rememberRecentFile(selectedPath);
    pendingOpenCreatesTab = document != nullptr;
    openReady.store(false, std::memory_order_release);
    openThread = std::thread([selectedPath] {
        OpenResult result;
        result.document = NativePdfDocument::Open(selectedPath, result.error);
        result.path = selectedPath;
        if (result.document) {
            result.pageCount = result.document->PageCount();
            result.title = utf8ToWide(result.document->Title().c_str());
            if (result.title.empty()) result.title = selectedPath;
        }
        {
            std::lock_guard lock(openMutex);
            openResult = std::move(result);
        }
        openReady.store(true, std::memory_order_release);
        PostMessageW(mainWindow, WM_OPEN_COMPLETE, 0, 0);
        });
}

void openDocument() {
    if (openThread.joinable()) return;
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{ sizeof(dialog) };
    dialog.hwndOwner = mainWindow;
    dialog.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return;
    openPath(path);
}

void printCurrentPage() {
    if (!document || pixelWidth <= 0 || pixelHeight <= 0 || pixels.empty()) return;
    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = mainWindow;
    dialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS | PD_HIDEPRINTTOFILE;
    dialog.nMinPage = 1;
    dialog.nMaxPage = 1;
    dialog.nFromPage = 1;
    dialog.nToPage = 1;
    if (!PrintDlgW(&dialog)) return;
    HDC printer = dialog.hDC;
    if (!printer) {
        if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
        if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
        return;
    }
    DOCINFOW doc{};
    doc.cbSize = sizeof(doc);
    doc.lpszDocName = L"Pdf++ Reader";
    if (StartDocW(printer, &doc) > 0) {
        const int paperWidth = GetDeviceCaps(printer, HORZRES);
        const int paperHeight = GetDeviceCaps(printer, VERTRES);
        const double fit = std::min(
            static_cast<double>(paperWidth) / pixelWidth,
            static_cast<double>(paperHeight) / pixelHeight);
        const int outWidth = std::max(1, static_cast<int>(pixelWidth * fit));
        const int outHeight = std::max(1, static_cast<int>(pixelHeight * fit));
        const int outX = (paperWidth - outWidth) / 2;
        const int outY = (paperHeight - outHeight) / 2;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = pixelWidth;
        info.bmiHeader.biHeight = -pixelHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        if (StartPage(printer) > 0) {
            SetMapMode(printer, MM_TEXT);
            SetStretchBltMode(printer, HALFTONE);
            StretchDIBits(printer, outX, outY, outWidth, outHeight,
                0, 0, pixelWidth, pixelHeight,
                pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            EndPage(printer);
        }
        EndDoc(printer);
    }
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    DeleteDC(printer);
    setStatus(L"Page " + std::to_wstring(pageIndex + 1) + L" sent to printer");
}

} // namespace PdfPP::Win32
