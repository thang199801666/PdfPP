#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>
#include <cwctype>
#include <regex>
#include <string>
#include <string_view>

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

int pageAtScrollOffset(const int offset) {
    if (pagePixelOffsets.empty()) return 0;
    const auto it = std::upper_bound(pagePixelOffsets.begin(), pagePixelOffsets.end(),
        std::max(0, offset));
    return std::clamp(static_cast<int>(std::distance(pagePixelOffsets.begin(), it)) - 1,
        0, std::max(0, pageCount - 1));
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
    EnableMenuItem(mainMenu, ID_EXTRACT_PAGES, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_SPLIT_PDF, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_DELETE_PAGES,
        MF_BYCOMMAND | (hasDocument && pageCount > 1 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_DUPLICATE_PAGES, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_MOVE_PAGE,
        MF_BYCOMMAND | (hasDocument && pageCount > 1 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_REORDER_PAGES,
        MF_BYCOMMAND | (hasDocument && pageCount > 1 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(mainMenu, ID_REVERSE_PAGES,
        MF_BYCOMMAND | (hasDocument && pageCount > 1 ? MF_ENABLED : MF_GRAYED));

    CheckMenuItem(mainMenu, ID_BOOKMARKS,
        MF_BYCOMMAND | (sidebarVisible ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(mainMenu, ID_TOOLS_TOGGLE,
        MF_BYCOMMAND | (toolsVisible ? MF_CHECKED : MF_UNCHECKED));
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
    CheckMenuItem(mainMenu, ID_SHOW_PAGE_SHADOW,
        MF_BYCOMMAND | (settings.showPageShadow ? MF_CHECKED : MF_UNCHECKED));
    if (selectButton) PdfPP::ModernWin32::SetActionButtonAccent(selectButton, !handTool);
    if (handButton) PdfPP::ModernWin32::SetActionButtonAccent(handButton, handTool);
    if (toolsToggleButton) PdfPP::ModernWin32::SetActionButtonAccent(toolsToggleButton, toolsVisible);
    if (sidebarToggleButton) PdfPP::ModernWin32::SetActionButtonAccent(sidebarToggleButton, sidebarVisible);
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
    const auto& toc = currentToc;
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
    // The TreeView is a child of the sidebar container. Repaint it once after
    // rebuilding so freshly inserted text is presented immediately.
    RedrawWindow(pageList, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_FRAME);
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
    showFindPanel(false);
    KillTimer(mainWindow, RENDER_TIMER);
    KillTimer(mainWindow, ZOOM_TIMER);
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
    currentToc.clear();
    rightPanelMode = RightPanelMode::Tools;
    currentPageComments.clear();
    commentItems.clear();
    commentPage = -1;
    activeCommentObjectNumber = 0;
    searchHighlights.clear(); clearTextSelection(); pageCache.Clear();
    KillTimer(mainWindow, TEXT_GEOMETRY_TIMER);
    textGeometryPage = -1;
    textGeometryRequestPage = -1;
    currentFilePath.clear();
    pixelWidth = pixelHeight = pixelStride = 0;
    pixelPage = -1;
    pixelZoom = zoom;
    pixelDpi = currentDpi;
    pagePixelHeights.clear();
    pagePixelOffsets.clear();
    pageGeometryBaseHeights.clear();
    documentPixelHeight = 0;
    pendingScrollY = -1;
    geometryZoom = -1.0;
    geometryDpi = 0;
    geometryDocument.reset();
    zoomAnchor = {};
    zoomRenderPending = false;
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

namespace {

std::wstring lowerFindText(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](const wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return result;
}

std::wstring normalizeFindTextForRegex(std::wstring_view text) {
    std::wstring normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t ch = text[index];
        if (ch == L'\r') {
            if (index + 1U < text.size() && text[index + 1U] == L'\n') {
                ++index;
            }
            normalized.push_back(L'\n');
        } else {
            normalized.push_back(ch);
        }
    }
    return normalized;
}

std::wstring makeSearchRegexPattern(std::wstring_view pattern) {
    // Acrobat-like behavior: allow '.' to span line breaks in extracted PDF text.
    // ECMAScript regex in the STL has no dotall flag, so expand unescaped dots
    // outside character classes to [\\s\\S].
    std::wstring translated;
    translated.reserve(pattern.size() * 2U);
    bool escaped = false;
    bool inClass = false;
    for (const wchar_t ch : pattern) {
        if (escaped) {
            translated.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == L'\\') {
            translated.push_back(ch);
            escaped = true;
            continue;
        }
        if (!inClass && ch == L'[') {
            inClass = true;
            translated.push_back(ch);
            continue;
        }
        if (inClass && ch == L']') {
            inClass = false;
            translated.push_back(ch);
            continue;
        }
        if (!inClass && ch == L'.') {
            translated += L"[\\s\\S]";
            continue;
        }
        translated.push_back(ch);
    }
    return translated;
}

bool validateFindExpression(const std::wstring& query, std::wstring& error) {
    error.clear();
    if (findPatternMode != FindPatternMode::RegularExpression) return true;
    try {
        auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
        if (findCaseMode == FindCaseMode::CaseInsensitive) {
            flags |= std::regex_constants::icase;
        }
        const std::wregex expression(makeSearchRegexPattern(query), flags);
        (void)expression;
        return true;
    } catch (const std::regex_error& exception) {
        error = L"Invalid regular expression (code " +
            std::to_wstring(static_cast<int>(exception.code())) + L")";
        return false;
    }
}

struct FindPatternCache final {
    std::wstring query;
    std::wstring foldedQuery;
    std::uint64_t revision{ (std::numeric_limits<std::uint64_t>::max)() };
    FindCaseMode caseMode{ FindCaseMode::CaseInsensitive };
    FindPatternMode patternMode{ FindPatternMode::Normal };
    std::optional<std::wregex> expression;
};

FindPatternCache& findPatternCache() {
    static FindPatternCache cache;
    return cache;
}

void refreshFindPatternCache() {
    auto& cache = findPatternCache();
    if (cache.query == lastSearchQuery &&
        cache.revision == findOptionsRevision &&
        cache.caseMode == findCaseMode &&
        cache.patternMode == findPatternMode) {
        return;
    }
    cache.query = lastSearchQuery;
    cache.revision = findOptionsRevision;
    cache.caseMode = findCaseMode;
    cache.patternMode = findPatternMode;
    cache.foldedQuery = findCaseMode == FindCaseMode::CaseInsensitive
        ? lowerFindText(lastSearchQuery) : std::wstring{};
    cache.expression.reset();
    if (findPatternMode == FindPatternMode::RegularExpression &&
        !lastSearchQuery.empty()) {
        try {
            auto flags = std::regex_constants::ECMAScript |
                std::regex_constants::optimize;
            if (findCaseMode == FindCaseMode::CaseInsensitive) {
                flags |= std::regex_constants::icase;
            }
            cache.expression.emplace(makeSearchRegexPattern(lastSearchQuery), flags);
        } catch (const std::regex_error&) {
            cache.expression.reset();
        }
    }
}

} // namespace

bool findPatternMatches(const std::wstring_view text) {
    if (lastSearchQuery.empty() || text.empty()) return false;
    refreshFindPatternCache();
    const auto& cache = findPatternCache();
    if (findPatternMode == FindPatternMode::RegularExpression) {
        if (!cache.expression) return false;
        const std::wstring normalized = normalizeFindTextForRegex(text);
        return std::regex_search(normalized.begin(), normalized.end(), *cache.expression);
    }
    if (findCaseMode == FindCaseMode::CaseSensitive) {
        return text.find(lastSearchQuery) != std::wstring_view::npos;
    }
    return lowerFindText(text).find(cache.foldedQuery) != std::wstring::npos;
}

void resetFindSearchState() {
    lastSearchQuery.clear();
    lastSearchPage = -1;
    lastSearchOptionsRevision = findOptionsRevision;
    searchHighlights.clear();
    if (canvas) InvalidateRect(canvas, nullptr, FALSE);
    if (findPanelVisible && searchEdit) SetFocus(searchEdit);
}

void findText() {
    if (!document) return;
    wchar_t query[512]{};
    GetWindowTextW(searchEdit, query, static_cast<int>(std::size(query)));
    if (!query[0]) {
        setStatus(L"Enter text to find");
        return;
    }

    const std::wstring requestedQuery(query);
    std::wstring validationError;
    if (!validateFindExpression(requestedQuery, validationError)) {
        setStatus(validationError);
        return;
    }

    const bool newQuery = requestedQuery != lastSearchQuery ||
        lastSearchOptionsRevision != findOptionsRevision;
    lastSearchQuery = requestedQuery;
    lastSearchOptionsRevision = findOptionsRevision;

    const int safePageCount = std::max(1, pageCount);
    const int current = std::clamp(pageIndex, 0, std::max(0, pageCount - 1));
    const int startPage = findScope == FindScope::CurrentPage
        ? current
        : (newQuery ? current : (lastSearchPage + 1) % safePageCount);
    const int pagesToSearch = findScope == FindScope::CurrentPage ? 1 : pageCount;
    const auto& bookmarks = currentToc;

    const auto activateResult = [&](const int page, const std::wstring& status) {
        lastSearchPage = page;
        goToPage(page);
        if (textGeometryPage == page) {
            updateSearchHighlights();
            InvalidateRect(canvas, nullptr, FALSE);
        }
        setStatus(status);
    };

    for (int offset = 0; offset < pagesToSearch; ++offset) {
        const int page = findScope == FindScope::CurrentPage
            ? current : (startPage + offset) % safePageCount;
        const std::wstring text = utf8ToWide(document->Text(page).c_str());
        if (findPatternMatches(text)) {
            activateResult(page, L"Found on page " + std::to_wstring(page + 1));
            return;
        }

        if (findIncludeComments) {
            for (const auto& comment : document->Comments(page)) {
                if (!findPatternMatches(utf8ToWide(comment.c_str()))) continue;
                activateResult(page,
                    L"Found in comment on page " + std::to_wstring(page + 1));
                return;
            }
        }

        if (findIncludeBookmarks) {
            for (const auto& bookmark : bookmarks) {
                if (bookmark.page != page || !findPatternMatches(bookmark.title)) continue;
                activateResult(page,
                    L"Found in bookmark on page " + std::to_wstring(page + 1));
                return;
            }
        }
    }
    lastSearchPage = -1;
    setStatus(findScope == FindScope::CurrentPage
        ? L"Text not found on current page" : L"Text not found");
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
        result.document = ReaderPdfDocument::Open(selectedPath, result.error);
        result.path = selectedPath;
        if (result.document) {
            result.pageCount = result.document->PageCount();
            result.toc = result.document->TableOfContents();
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
