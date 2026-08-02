#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

namespace PdfPP::Win32 {

std::wstring tabLabelFor(const TabState& tab) {
    // SumatraPDF labels tabs with the file name, never the PDF metadata
    // title, so documents with generic titles stay distinguishable.
    return std::filesystem::path(tab.path).filename().wstring();
}

void rebuildTabBar() {
    if (!tabBar) return;
    // The strip is fully custom-painted from the tabs vector; there are no
    // WC_TABCONTROL items to manage.
    hoverCloseTab = -1;
    InvalidateRect(tabBar, nullptr, TRUE);
}

void captureActiveTabState() {
    if (activeTab < 0 || activeTab >= static_cast<int>(tabs.size())) return;
    TabState& tab = tabs[static_cast<std::size_t>(activeTab)];
    tab.path = currentFilePath;
    tab.title = document ? utf8ToWide(document->Title().c_str()) : std::wstring{};
    tab.document = document;
    tab.pageCount = pageCount;
    tab.pageIndex = pageIndex;
    tab.zoom = zoom;
    tab.scrollX = scrollX;
    tab.scrollY = scrollY;
    tab.pixels = std::move(pixels);
    tab.pixelWidth = pixelWidth;
    tab.pixelHeight = pixelHeight;
    tab.pixelStride = pixelStride;
    tab.textChunks = std::move(textChunks);
    tab.searchHighlights = std::move(searchHighlights);
    tab.selectedChunks = std::move(selectedChunks);
    tab.lastSearchQuery = lastSearchQuery;
    tab.lastSearchPage = lastSearchPage;
    tab.layout = pageLayoutMode;
}

void applyTabState(TabState& tab) {
    document = tab.document;
    currentFilePath = tab.path;
    pageCount = tab.pageCount;
    pageIndex = tab.pageIndex;
    zoom = tab.zoom;
    scrollX = tab.scrollX;
    scrollY = tab.scrollY;
    pixels = std::move(tab.pixels);
    pixelWidth = tab.pixelWidth;
    pixelHeight = tab.pixelHeight;
    pixelStride = tab.pixelStride;
    textChunks = std::move(tab.textChunks);
    searchHighlights = std::move(tab.searchHighlights);
    selectedChunks = std::move(tab.selectedChunks);
    lastSearchQuery = tab.lastSearchQuery;
    lastSearchPage = tab.lastSearchPage;
    pageLayoutMode = tab.layout;
    tab.pixels.clear();
    tab.textChunks.clear();
    tab.searchHighlights.clear();
    tab.selectedChunks.clear();
}

void switchToTab(const int index) {
    if (index < 0 || index >= static_cast<int>(tabs.size()) || index == activeTab) return;
    switchingTab = true;
    captureActiveTabState();
    // Wait for any in-flight render of the outgoing tab so a stale
    // WM_RENDER_COMPLETE cannot repaint this tab with the wrong bitmap.
    if (renderThread.joinable()) renderThread.join();
    renderReady.store(false, std::memory_order_release);
    {
        std::lock_guard lock(renderMutex);
        renderResult = {};
    }
    activeTab = index;
    continuousNextPage.reset();
    applyTabState(tabs[static_cast<std::size_t>(index)]);
    pageCache.Clear();
    updatePageGeometry();
    rebuildTabBar();
    if (document) {
        const std::wstring title = tabs[static_cast<std::size_t>(index)].title.empty()
            ? tabLabelFor(tabs[static_cast<std::size_t>(index)])
            : tabs[static_cast<std::size_t>(index)].title;
        SetWindowTextW(mainWindow, (title + L" - Pdf++ Reader").c_str());
        populateBookmarkTree(tabLabelFor(tabs[static_cast<std::size_t>(index)]));
        updatePageControls();
        if (pixelWidth > 0 && !pixels.empty()) {
            updateCanvasScrollbars();
            InvalidateRect(canvas, nullptr, FALSE);
            if (pageLayoutMode == PageLayoutMode::ContinuousNavigation) prefetchNextPage();
        } else {
            renderPage();
        }
    } else {
        SetWindowTextW(mainWindow, L"Pdf++ Reader");
        TreeView_DeleteAllItems(pageList);
        tocItems.clear();
        updatePageControls();
        InvalidateRect(canvas, nullptr, TRUE);
    }
    switchingTab = false;
}

void closeTabAt(const int index) {
    if (tabs.empty() || index < 0 || index >= static_cast<int>(tabs.size())) return;
    if (openThread.joinable()) return;
    if (renderThread.joinable()) renderThread.join();
    renderReady.store(false, std::memory_order_release);
    {
        std::lock_guard lock(renderMutex);
        renderResult = {};
    }
    const int closed = index;
    tabs.erase(tabs.begin() + closed);
    if (tabs.empty()) {
        closeDocument();
        rebuildTabBar();
        return;
    }
    if (closed < activeTab) {
        --activeTab;
    } else if (closed == activeTab) {
        activeTab = std::min(closed, static_cast<int>(tabs.size()) - 1);
    }
    continuousNextPage.reset();
    applyTabState(tabs[static_cast<std::size_t>(activeTab)]);
    pageCache.Clear();
    rebuildTabBar();
    if (document) {
        const std::wstring title = tabs[static_cast<std::size_t>(activeTab)].title.empty()
            ? tabLabelFor(tabs[static_cast<std::size_t>(activeTab)])
            : tabs[static_cast<std::size_t>(activeTab)].title;
        SetWindowTextW(mainWindow, (title + L" - Pdf++ Reader").c_str());
        populateBookmarkTree(tabLabelFor(tabs[static_cast<std::size_t>(activeTab)]));
        updatePageControls();
        if (pixelWidth > 0 && !pixels.empty()) {
            updateCanvasScrollbars();
            InvalidateRect(canvas, nullptr, FALSE);
            if (pageLayoutMode == PageLayoutMode::ContinuousNavigation) prefetchNextPage();
        } else {
            renderPage();
        }
    } else {
        SetWindowTextW(mainWindow, L"Pdf++ Reader");
        TreeView_DeleteAllItems(pageList);
        tocItems.clear();
        updatePageControls();
        InvalidateRect(canvas, nullptr, TRUE);
    }
}

void closeActiveTab() {
    closeTabAt(activeTab);
}

// Close the tab identified by a close-button press, confirming with a
// warning dialog first (like SumatraPDF's close confirmation).
void requestCloseTab(const int index) {
    if (index < 0 || index >= static_cast<int>(tabs.size())) return;
    const std::wstring label = tabLabelFor(tabs[static_cast<std::size_t>(index)]);
    const std::wstring message = L"Close the \"" + label +
        L"\" tab? Any unsaved changes will be lost.";
    const int choice = MessageBoxW(mainWindow, message.c_str(),
        L"Close Tab", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_APPLMODAL);
    if (choice == IDYES) {
        closeTabAt(index);
    }
}

int tabStripWidth(const int clientWidth, const int count) {
    if (count <= 0) return clientWidth;
    // Tabs fill the strip; a single document gets the full width like
    // Chrome/Photoshop, many tabs clamp to a compact fixed size.
    return std::max(scaleDip(120), std::min(scaleDip(240), clientWidth / count));
}

RECT tabItemRectFor(const int index, const int count, const int clientWidth) {
    const int width = tabStripWidth(clientWidth, count);
    return RECT{ index * width, 0, (index + 1) * width, 0 };
}

RECT tabCloseRectFor(const RECT& item) {
    // Slightly larger than the drawn glyph so the hit target is easy to click.
    const int size = scaleDip(20);
    const int margin = scaleDip(2);
    return RECT{ item.right - margin - size,
                 (item.bottom - item.top - size) / 2,
                 item.right - margin,
                 (item.bottom - item.top + size) / 2 };
}

void paintTabItem(HWND, HDC dc, const int index, const RECT& item) {
    const bool selected = index == activeTab;
    const bool hover = index == hoverCloseTab;
    const COLORREF base = selected ? PdfPP::ModernWin32::Theme::canvas
        : PdfPP::ModernWin32::Theme::toolbar;
    const COLORREF fill = hover ? PdfPP::ModernWin32::Theme::controlHover : base;

    const int radius = scaleDip(5);
    fillRoundedTop(dc, item, fill, radius);

    // A subtle bottom border for inactive tabs keeps them sitting on the
    // strip line like Chrome; the active tab drops the border so it blends
    // into the page canvas below.
    if (!selected) {
        HPEN pen = CreatePen(PS_SOLID, 1, PdfPP::ModernWin32::Theme::border);
        const auto oldPen = SelectObject(dc, pen);
        MoveToEx(dc, item.left, item.bottom - 1, nullptr);
        LineTo(dc, item.right, item.bottom - 1);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? PdfPP::ModernWin32::Theme::text
                              : PdfPP::ModernWin32::Theme::mutedText);
    RECT label = item;
    label.left += scaleDip(10);
    label.right -= scaleDip(24);
    label.bottom -= 1;
    std::wstring text = index >= 0 && index < static_cast<int>(tabs.size())
        ? tabLabelFor(tabs[static_cast<std::size_t>(index)]) : L"";
    DrawTextW(dc, text.c_str(), -1, &label,
        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Close button.
    const RECT close = tabCloseRectFor(item);
    if (hover) {
        const HBRUSH closeBrush = CreateSolidBrush(RGB(232, 110, 90));
        const auto oldBrush = SelectObject(dc, closeBrush);
        Ellipse(dc, close.left, close.top, close.right, close.bottom);
        SelectObject(dc, oldBrush);
        DeleteObject(closeBrush);
        SetTextColor(dc, RGB(255, 255, 255));
    } else {
        SetTextColor(dc, PdfPP::ModernWin32::Theme::mutedText);
    }
    RECT closeText = close;
    DrawTextW(dc, L"×", -1, &closeText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// Maps a client-space point to a tab index, or -1.
int tabHitTest(const int x, const int clientWidth) {
    const int count = static_cast<int>(tabs.size());
    if (count <= 0 || clientWidth <= 0) return -1;
    const int width = tabStripWidth(clientWidth, count);
    const int index = x / width;
    if (index < 0 || index >= count) return -1;
    return index;
}

LRESULT CALLBACK tabBarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        const HBRUSH background = CreateSolidBrush(PdfPP::ModernWin32::Theme::toolbar);
        FillRect(dc, &client, background);
        DeleteObject(background);

        const HFONT oldFont = regularUiFont
            ? static_cast<HFONT>(SelectObject(dc, regularUiFont)) : nullptr;
        const int count = static_cast<int>(tabs.size());
        for (int index = 0; index < count; ++index) {
            RECT item = tabItemRectFor(index, count, client.right);
            item.bottom = client.bottom;
            paintTabItem(window, dc, index, item);
        }
        if (oldFont) SelectObject(dc, oldFont);

        // Bottom border of the whole strip.
        HPEN pen = CreatePen(PS_SOLID, 1, PdfPP::ModernWin32::Theme::border);
        const auto oldPen = SelectObject(dc, pen);
        MoveToEx(dc, 0, client.bottom - 1, nullptr);
        LineTo(dc, client.right, client.bottom - 1);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT client{}; GetClientRect(window, &client);
        const int index = tabHitTest(point.x, client.right);
        if (index < 0) break;
        RECT item = tabItemRectFor(index, static_cast<int>(tabs.size()), client.right);
        item.bottom = client.bottom;
        const RECT close = tabCloseRectFor(item);
        if (PtInRect(&close, point)) {
            requestCloseTab(index);
            return 0;
        }
        if (index != activeTab) {
            switchToTab(index);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT client{}; GetClientRect(window, &client);
        const int index = tabHitTest(point.x, client.right);
        int hover = -1;
        if (index >= 0) {
            RECT item = tabItemRectFor(index, static_cast<int>(tabs.size()), client.right);
            item.bottom = client.bottom;
            const RECT close = tabCloseRectFor(item);
            if (PtInRect(&close, point)) hover = index;
        }
        if (hover != hoverCloseTab) {
            hoverCloseTab = hover;
            InvalidateRect(window, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (hoverCloseTab != -1) {
            hoverCloseTab = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace PdfPP::Win32
