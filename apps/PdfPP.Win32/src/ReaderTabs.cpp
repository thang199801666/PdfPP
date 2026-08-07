#include <PdfPP/Win32/ReaderState.hpp>

#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace PdfPP::Win32 {
namespace {

constexpr COLORREF kTabStripSurface = RGB(242, 243, 245);
constexpr COLORREF kActiveTabFill = RGB(255, 255, 255);
constexpr COLORREF kInactiveTabHover = RGB(231, 233, 237);
constexpr COLORREF kActiveTabBorder = RGB(214, 217, 223);
constexpr COLORREF kInactiveText = RGB(92, 98, 108);
constexpr COLORREF kCloseHoverFill = RGB(228, 230, 234);
constexpr COLORREF kClosePressedFill = RGB(214, 217, 222);
constexpr COLORREF kCloseGlyph = RGB(91, 96, 104);
constexpr COLORREF kAccent = RGB(20, 115, 230);

Gdiplus::Color gpColor(const COLORREF color, const BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void addRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect,
                    float radius) {
    radius = (std::max)(0.0F,
        (std::min)(radius, (std::min)(rect.Width, rect.Height) * 0.5F));
    if (radius <= 0.5F) {
        path.AddRectangle(rect);
        return;
    }
    const float diameter = radius * 2.0F;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.GetRight() - diameter, rect.Y,
        diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter,
        diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rect.X, rect.GetBottom() - diameter,
        diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

void fillRoundedRect(Gdiplus::Graphics& graphics, const RECT& rect,
                     const COLORREF color, const int radius, const BYTE alpha = 255) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::GraphicsPath path;
    addRoundedRect(path,
        Gdiplus::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
            static_cast<float>(rect.right - rect.left),
            static_cast<float>(rect.bottom - rect.top)),
        static_cast<float>(radius));
    Gdiplus::SolidBrush brush(gpColor(color, alpha));
    graphics.FillPath(&brush, &path);
}

void strokeRoundedRect(Gdiplus::Graphics& graphics, const RECT& rect,
                       const COLORREF color, const int radius, const float width = 1.0F) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    const float inset = width * 0.5F;
    Gdiplus::GraphicsPath path;
    addRoundedRect(path,
        Gdiplus::RectF(static_cast<float>(rect.left) + inset,
            static_cast<float>(rect.top) + inset,
            static_cast<float>(rect.right - rect.left) - width,
            static_cast<float>(rect.bottom - rect.top) - width),
        (std::max)(1.0F, static_cast<float>(radius) - inset));
    Gdiplus::Pen pen(gpColor(color), width);
    graphics.DrawPath(&pen, &path);
}

void paintCloseGlyph(Gdiplus::Graphics& graphics, const RECT& close,
                     const bool hovered, const bool pressed) {
    if (pressed) {
        fillRoundedRect(graphics, close, kClosePressedFill,
            (close.bottom - close.top) / 2);
    } else if (hovered) {
        fillRoundedRect(graphics, close, kCloseHoverFill,
            (close.bottom - close.top) / 2);
    }

    const float centerX = (static_cast<float>(close.left) + close.right) * 0.5F;
    const float centerY = (static_cast<float>(close.top) + close.bottom) * 0.5F;
    const float half = static_cast<float>(scaleDip(3));
    Gdiplus::Pen pen(gpColor(kCloseGlyph), hovered || pressed ? 1.7F : 1.45F);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawLine(&pen, centerX - half, centerY - half,
        centerX + half, centerY + half);
    graphics.DrawLine(&pen, centerX + half, centerY - half,
        centerX - half, centerY + half);
}

} // namespace

std::wstring tabLabelFor(const TabState& tab) {
    const std::wstring filename = std::filesystem::path(tab.path).filename().wstring();
    if (!filename.empty()) return filename;
    if (!tab.title.empty()) return tab.title;
    return L"Untitled";
}

void rebuildTabBar() {
    if (!tabBar) return;
    hoverTab = -1;
    hoverCloseTab = -1;
    pressedCloseTab = -1;
    InvalidateRect(tabBar, nullptr, FALSE);
}

void captureActiveTabState() {
    if (activeTab < 0 || activeTab >= static_cast<int>(tabs.size())) return;
    TabState& tab = tabs[static_cast<std::size_t>(activeTab)];
    tab.path = currentFilePath;
    // The title was captured when the file was opened. Do not query the PDF
    // parser again here: captureActiveTabState() may run while the renderer is
    // still finishing a page for this document, and ReaderPdfDocument is not
    // intended to have its mutable object resolver used concurrently.
    if (tab.title.empty() && !currentFilePath.empty()) {
        tab.title = std::filesystem::path(currentFilePath).filename().wstring();
    }
    tab.document = document;
    tab.toc = currentToc;
    tab.pageCount = pageCount;
    tab.pageIndex = pageIndex;
    tab.zoom = zoom;
    tab.scrollX = scrollX;
    tab.scrollY = scrollY;
    tab.pixels = std::move(pixels);
    tab.pixelWidth = pixelWidth;
    tab.pixelHeight = pixelHeight;
    tab.pixelStride = pixelStride;
    tab.pixelPage = pixelPage;
    tab.pixelZoom = pixelZoom;
    tab.pixelDpi = pixelDpi;
    tab.textChunks = std::move(textChunks);
    tab.searchHighlights = std::move(searchHighlights);
    tab.selectedTextSpans = std::move(selectedTextSpans);
    tab.selectionAnchor = selectionAnchor;
    tab.selectionFocus = selectionFocus;
    tab.textGeometryPage = textGeometryPage;
    tab.lastSearchQuery = lastSearchQuery;
    tab.lastSearchPage = lastSearchPage;
    tab.layout = pageLayoutMode;
}

void applyTabState(TabState& tab) {
    document = tab.document;
    currentToc = tab.toc;
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
    pixelPage = tab.pixelPage;
    pixelZoom = tab.pixelZoom;
    pixelDpi = tab.pixelDpi;
    textChunks = std::move(tab.textChunks);
    searchHighlights = std::move(tab.searchHighlights);
    selectedTextSpans = std::move(tab.selectedTextSpans);
    selectionAnchor = tab.selectionAnchor;
    selectionFocus = tab.selectionFocus;
    textGeometryPage = tab.textGeometryPage;
    lastSearchQuery = tab.lastSearchQuery;
    lastSearchPage = tab.lastSearchPage;
    pageLayoutMode = tab.layout;
    rightPanelMode = RightPanelMode::Tools;
    currentPageComments.clear();
    commentItems.clear();
    commentPage = -1;
    activeCommentObjectNumber = 0;

    tab.pixels.clear();
    tab.textChunks.clear();
    tab.searchHighlights.clear();
    tab.selectedTextSpans.clear();
    tab.selectionAnchor = {};
    tab.selectionFocus = {};
}

void switchToTab(const int index) {
    if (index < 0 || index >= static_cast<int>(tabs.size()) || index == activeTab) return;
    switchingTab = true;
    KillTimer(mainWindow, TEXT_GEOMETRY_TIMER);
    textGeometryRequestPage = -1;
    captureActiveTabState();
    if (renderThread.joinable()) renderThread.join();
    renderReady.store(false, std::memory_order_release);
    {
        std::lock_guard lock(renderMutex);
        renderResult = {};
    }
    activeTab = index;
    applyTabState(tabs[static_cast<std::size_t>(index)]);
    pageCache.Clear();
    updatePageGeometry();
    requestTextGeometryRefresh();
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
    KillTimer(mainWindow, TEXT_GEOMETRY_TIMER);
    textGeometryRequestPage = -1;
    // An inactive-tab close still rebuilds the active document from its TabState.
    // Capture the live active state first so its bitmap/selection/scroll state is
    // not replaced by the moved-from snapshot stored in the vector.
    if (index != activeTab) captureActiveTabState();
    if (renderThread.joinable()) renderThread.join();
    renderReady.store(false, std::memory_order_release);
    {
        std::lock_guard lock(renderMutex);
        renderResult = {};
    }

    const int closed = index;
    tabs.erase(tabs.begin() + closed);
    hoverTab = -1;
    hoverCloseTab = -1;
    pressedCloseTab = -1;
    if (tabs.empty()) {
        closeDocument();
        rebuildTabBar();
        return;
    }
    if (closed < activeTab) {
        --activeTab;
    } else if (closed == activeTab) {
        activeTab = (std::min)(closed, static_cast<int>(tabs.size()) - 1);
    }

    applyTabState(tabs[static_cast<std::size_t>(activeTab)]);
    pageCache.Clear();
    updatePageGeometry();
    requestTextGeometryRefresh();
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

void requestCloseTab(const int index) {
    if (index < 0 || index >= static_cast<int>(tabs.size())) return;
    const std::wstring label = tabLabelFor(tabs[static_cast<std::size_t>(index)]);
    const std::wstring message = L"Close the \"" + label +
        L"\" tab? Any unsaved changes will be lost.";
    const int choice = MessageBoxW(mainWindow, message.c_str(),
        L"Close Tab", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_APPLMODAL);
    if (choice == IDYES) closeTabAt(index);
}

int tabStripWidth(const int clientWidth, const int count) {
    if (count <= 0 || clientWidth <= 0) return 0;
    const int outerPadding = scaleDip(8);
    const int gap = scaleDip(4);
    const int usable = (std::max)(1, clientWidth - outerPadding * 2 - gap * (count - 1));
    const int natural = usable / count;
    return std::clamp(natural, scaleDip(116), scaleDip(220));
}

RECT tabItemRectFor(const int index, const int count, const int clientWidth) {
    if (index < 0 || count <= 0 || clientWidth <= 0) return RECT{};
    const int outerPadding = scaleDip(8);
    const int gap = scaleDip(4);
    const int width = tabStripWidth(clientWidth, count);
    const int left = outerPadding + index * (width + gap);
    return RECT{ left, scaleDip(3), (std::min)(clientWidth - outerPadding, left + width), 0 };
}

RECT tabCloseRectFor(const RECT& item) {
    const int size = scaleDip(18);
    const int rightInset = scaleDip(7);
    const int centerY = (item.top + item.bottom) / 2;
    return RECT{
        item.right - rightInset - size,
        centerY - size / 2,
        item.right - rightInset,
        centerY + size / 2
    };
}

void paintTabItem(HWND, HDC dc, const int index, const RECT& item) {
    if (index < 0 || index >= static_cast<int>(tabs.size())) return;
    const bool active = index == activeTab;
    const bool hovered = index == hoverTab;
    const bool closeHovered = index == hoverCloseTab;
    const bool closePressed = index == pressedCloseTab;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

    RECT body = item;
    body.bottom -= scaleDip(2);
    if (active) {
        // Very light shadow under the selected tab gives it a raised card feel
        // without creating a heavy browser-like border.
        RECT shadow = body;
        OffsetRect(&shadow, 0, scaleDip(1));
        fillRoundedRect(graphics, shadow, RGB(170, 176, 186), scaleDip(8), 36);
        fillRoundedRect(graphics, body, kActiveTabFill, scaleDip(8));
        strokeRoundedRect(graphics, body, kActiveTabBorder, scaleDip(8), 1.0F);

        RECT accent{ body.left + scaleDip(10), body.top,
            body.right - scaleDip(10), body.top + scaleDip(2) };
        fillRoundedRect(graphics, accent, kAccent, scaleDip(1));
    } else if (hovered) {
        fillRoundedRect(graphics, body, kInactiveTabHover, scaleDip(8));
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, active ? PdfPP::ModernWin32::Theme::text : kInactiveText);
    HFONT tabFont = active && pageBoldUiFont ? pageBoldUiFont
        : (pageUiFont ? pageUiFont : regularUiFont);
    HGDIOBJ oldFont = tabFont ? SelectObject(dc, tabFont) : nullptr;
    RECT label = body;
    label.left += scaleDip(12);
    label.right = tabCloseRectFor(body).left - scaleDip(5);
    const std::wstring text = tabLabelFor(tabs[static_cast<std::size_t>(index)]);
    DrawTextW(dc, text.c_str(), -1, &label,
        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (oldFont) SelectObject(dc, oldFont);

    const RECT close = tabCloseRectFor(body);
    paintCloseGlyph(graphics, close, closeHovered, closePressed);
}

int tabHitTest(const int x, const int clientWidth) {
    const int count = static_cast<int>(tabs.size());
    if (count <= 0 || clientWidth <= 0) return -1;
    for (int index = 0; index < count; ++index) {
        RECT item = tabItemRectFor(index, count, clientWidth);
        item.bottom = scaleDip(kTabBarHeightDip);
        POINT point{ x, (item.top + item.bottom) / 2 };
        if (PtInRect(&item, point)) return index;
    }
    return -1;
}

LRESULT CALLBACK tabBarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);

        const HBRUSH background = CreateSolidBrush(kTabStripSurface);
        FillRect(dc, &client, background);
        DeleteObject(background);

        // Subtle line separating the tab surface from the canvas.
        HPEN line = CreatePen(PS_SOLID, 1, RGB(220, 223, 228));
        HGDIOBJ oldPen = SelectObject(dc, line);
        MoveToEx(dc, 0, client.bottom - 1, nullptr);
        LineTo(dc, client.right, client.bottom - 1);
        SelectObject(dc, oldPen);
        DeleteObject(line);

        const int count = static_cast<int>(tabs.size());
        for (int index = 0; index < count; ++index) {
            RECT item = tabItemRectFor(index, count, client.right);
            item.bottom = client.bottom;
            paintTabItem(window, dc, index, item);
        }

        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        SetFocus(window);
        const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT client{};
        GetClientRect(window, &client);
        const int index = tabHitTest(point.x, client.right);
        if (index < 0) return 0;

        RECT item = tabItemRectFor(index, static_cast<int>(tabs.size()), client.right);
        item.bottom = client.bottom;
        const RECT close = tabCloseRectFor(item);
        if (PtInRect(&close, point)) {
            pressedCloseTab = index;
            SetCapture(window);
            InvalidateRect(window, &close, FALSE);
            return 0;
        }
        if (index != activeTab) switchToTab(index);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (pressedCloseTab < 0) break;
        const int index = pressedCloseTab;
        pressedCloseTab = -1;
        if (GetCapture() == window) ReleaseCapture();

        const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT client{};
        GetClientRect(window, &client);
        if (index >= 0 && index < static_cast<int>(tabs.size())) {
            RECT item = tabItemRectFor(index, static_cast<int>(tabs.size()), client.right);
            item.bottom = client.bottom;
            const RECT close = tabCloseRectFor(item);
            InvalidateRect(window, &close, FALSE);
            if (PtInRect(&close, point)) requestCloseTab(index);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (pressedCloseTab >= 0) {
            pressedCloseTab = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE: {
        const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT client{};
        GetClientRect(window, &client);
        const int index = tabHitTest(point.x, client.right);
        int closeHover = -1;
        if (index >= 0) {
            RECT item = tabItemRectFor(index, static_cast<int>(tabs.size()), client.right);
            item.bottom = client.bottom;
            const RECT close = tabCloseRectFor(item);
            if (PtInRect(&close, point)) closeHover = index;
        }
        if (hoverTab != index || hoverCloseTab != closeHover) {
            hoverTab = index;
            hoverCloseTab = closeHover;
            InvalidateRect(window, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
        TrackMouseEvent(&track);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (hoverTab != -1 || hoverCloseTab != -1) {
            hoverTab = -1;
            hoverCloseTab = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace PdfPP::Win32
