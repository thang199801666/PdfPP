#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace PdfPP::Win32 {

void toggleFullscreen(HWND window) {
    if (!window) return;
    if (!fullscreen) {
        fullscreenStyle = GetWindowLongW(window, GWL_STYLE);
        fullscreenExStyle = GetWindowLongW(window, GWL_EXSTYLE);
        fullscreenPlacement.length = sizeof(fullscreenPlacement);
        if (!GetWindowPlacement(window, &fullscreenPlacement)) return;

        MONITORINFO monitor{ sizeof(monitor) };
        const HMONITOR display = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(display, &monitor)) return;
        fullscreen = true;
        SetMenu(window, nullptr);
        SetWindowLongW(window, GWL_STYLE, fullscreenStyle & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowLongW(window, GWL_EXSTYLE,
            fullscreenExStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
        SetWindowPos(window, HWND_TOP,
            monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else {
        fullscreen = false;
        SetWindowLongW(window, GWL_STYLE, fullscreenStyle);
        SetWindowLongW(window, GWL_EXSTYLE, fullscreenExStyle);
        SetMenu(window, mainMenu);
        SetWindowPlacement(window, &fullscreenPlacement);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    RECT client{};
    GetClientRect(window, &client);
    updateLayout(client.right, client.bottom);
    InvalidateRect(window, nullptr, TRUE);
    updateCommandState();
}

void updateLayout(const int width, const int height) {
    const int tabBarHeight = tabBar ? scaleDip(26) : 0;
    const int ribbon = scaleDip(PdfPP::ModernWin32::Layout::ribbonHeight);
    const int status = scaleDip(PdfPP::ModernWin32::Layout::statusHeight);
    const int bodyHeight = std::max(0, height - ribbon - tabBarHeight - status);
    const int sidebar = sidebarVisible ? scaleDip(PdfPP::ModernWin32::Layout::sidebarWidth) : 0;
    const int controlY = scaleDip((PdfPP::ModernWin32::Layout::ribbonHeight - PdfPP::ModernWin32::Layout::controlHeight) / 2);
    const int controlHeight = scaleDip(PdfPP::ModernWin32::Layout::controlHeight);
    // The tab strip sits above the page canvas and starts at the splitter
    // (right edge of the Table of Contents sidebar), so tabs never draw
    // over the sidebar.
    if (tabBar) {
        MoveWindow(tabBar, sidebar, ribbon, std::max(0, width - sidebar), tabBarHeight, TRUE);
    }
    // ---- SumatraPDF-style toolbar, ordered left to right: ----
    // Open | Print || Find | Prev Page Next || Zoom out % Zoom in Fit || Select Hand
    // The search field absorbs available width so the toolbar never
    // overflows on narrow windows.
    const int dOpen = scaleDip(50) + scaleDip(2);
    const int dPrint = scaleDip(48) + scaleDip(8);
    const int dFindLabel = scaleDip(34);
    const int dFindButton = scaleDip(42) + scaleDip(8);
    const int dPageNav = scaleDip(30) + scaleDip(1) + scaleDip(38) + scaleDip(40) +
        scaleDip(1) + scaleDip(42) + scaleDip(1) + scaleDip(30) + scaleDip(8);
    const int dZoom = scaleDip(30) + scaleDip(1) + scaleDip(42) + scaleDip(1) +
        scaleDip(30) + scaleDip(1) + scaleDip(46) + scaleDip(8);
    const int dSelect = scaleDip(50) + scaleDip(2) + scaleDip(44) + scaleDip(4);
    const int fixedBeforeSearch = scaleDip(8) + dOpen + dPrint;
    const int fixedAfterSearch = dFindLabel + dFindButton + dPageNav + dZoom + dSelect + scaleDip(34);
    const int availableForSearch = std::max(scaleDip(48),
        width - fixedBeforeSearch - fixedAfterSearch);
    const int searchWidth = std::min(scaleDip(190), availableForSearch);

    int left = scaleDip(8);
    MoveWindow(openButton, left, controlY, scaleDip(50), controlHeight, TRUE);
    left += dOpen;
    MoveWindow(printButton, left, controlY, scaleDip(48), controlHeight, TRUE);
    left += dPrint;
    MoveWindow(findLabel, left, controlY + scaleDip(4), scaleDip(34), scaleDip(18), TRUE);
    left += dFindLabel;
    MoveWindow(searchEdit, left, controlY + scaleDip(1), searchWidth, scaleDip(22), TRUE);
    left += searchWidth + scaleDip(3);
    MoveWindow(findButton, left, controlY, scaleDip(42), controlHeight, TRUE);
    left += scaleDip(42) + scaleDip(8);
    ribbonSep1 = left - scaleDip(6);
    MoveWindow(previousButton, left, controlY, scaleDip(32), controlHeight, TRUE);
    left += scaleDip(32) + scaleDip(2);
    MoveWindow(pageCaption, left, controlY + scaleDip(4), scaleDip(42), scaleDip(18), TRUE);
    left += scaleDip(42);
    MoveWindow(pageEdit, left, controlY + scaleDip(1), scaleDip(44), scaleDip(23), TRUE);
    left += scaleDip(44) + scaleDip(2);
    MoveWindow(pageLabel, left, controlY + scaleDip(4), scaleDip(46), scaleDip(18), TRUE);
    left += scaleDip(46) + scaleDip(2);
    MoveWindow(nextButton, left, controlY, scaleDip(32), controlHeight, TRUE);
    left += scaleDip(32) + scaleDip(12);
    ribbonSep2 = left - scaleDip(6);
    MoveWindow(zoomOutButton, left, controlY, scaleDip(34), controlHeight, TRUE);
    left += scaleDip(34) + scaleDip(2);
    MoveWindow(zoomLabel, left, controlY + scaleDip(4), scaleDip(46), scaleDip(18), TRUE);
    left += scaleDip(46) + scaleDip(2);
    MoveWindow(zoomInButton, left, controlY, scaleDip(34), controlHeight, TRUE);
    left += scaleDip(34) + scaleDip(2);
    MoveWindow(fitButton, left, controlY, scaleDip(50), controlHeight, TRUE);
    left += scaleDip(50) + scaleDip(12);
    ribbonSep3 = left - scaleDip(6);
    MoveWindow(selectButton, left, controlY, scaleDip(56), controlHeight, TRUE);
    left += scaleDip(56) + scaleDip(4);
    MoveWindow(handButton, left, controlY, scaleDip(50), controlHeight, TRUE);
    // Sidebar toggle stays anchored to the right edge like SumatraPDF.
    MoveWindow(sidebarToggleButton, std::max(left + scaleDip(8), width - scaleDip(50)),
        controlY, scaleDip(38), controlHeight, TRUE);
    const int sidebarHeader = scaleDip(28);
    // The sidebar starts directly under the toolbar so it fills the space
    // to the left of the tab strip (the tab strip only spans the canvas).
    MoveWindow(sidebarTitle, 0, ribbon, sidebar, sidebarHeader, TRUE);
    MoveWindow(bookmarkCloseButton, std::max(0, sidebar - scaleDip(28)), ribbon + scaleDip(2),
        scaleDip(24), scaleDip(24), TRUE);
    MoveWindow(pageList, 0, ribbon + sidebarHeader, sidebar,
        std::max(0, bodyHeight + tabBarHeight - sidebarHeader), TRUE);
    ShowWindow(sidebarTitle, sidebarVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(bookmarkCloseButton, sidebarVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(pageList, sidebarVisible ? SW_SHOW : SW_HIDE);
    MoveWindow(canvas, sidebar, ribbon + tabBarHeight, std::max(0, width - sidebar), bodyHeight, TRUE);
    MoveWindow(statusLabel, scaleDip(16), height - status + scaleDip(4),
        std::max(0, width - scaleDip(32)), scaleDip(20), TRUE);
    updateCanvasScrollbars();
    // The canvas can move without receiving a size change (for example when
    // the sidebar is toggled). Repaint it after every parent relayout so the
    // page is positioned against the new client rectangle immediately.
    if (canvas) InvalidateRect(canvas, nullptr, FALSE);
}

LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        // Double-buffer the page into a scratch surface sized to the invalid
        // region, then blit it once. Without this, every repaint erases the
        // region to the canvas background and redraws it directly on the
        // window, which flashes continuously while continuous-scrolling.
        const int bufferWidth = static_cast<int>(std::max<LONG>(1, paint.rcPaint.right - paint.rcPaint.left));
        const int bufferHeight = static_cast<int>(std::max<LONG>(1, paint.rcPaint.bottom - paint.rcPaint.top));
        const HDC mem = CreateCompatibleDC(dc);
        const HBITMAP backing = CreateCompatibleBitmap(dc, bufferWidth, bufferHeight);
        const HGDIOBJ previousBitmap = SelectObject(mem, backing);
        SetViewportOrgEx(mem, -paint.rcPaint.left, -paint.rcPaint.top, nullptr);

        const HBRUSH background = CreateSolidBrush(PdfPP::ModernWin32::Theme::canvas);
        FillRect(mem, &paint.rcPaint, background);
        DeleteObject(background);
        if (pageLayoutMode == PageLayoutMode::ContinuousNavigation && document) {
            // Continuous mode is a document viewport, not a two-page carousel.
            // Draw every cached page whose absolute document rectangle touches
            // the viewport. This keeps all visible pages fixed in one coordinate
            // system while scrollY moves the complete document together.
            const int firstPage = pageAtScrollOffset(scrollY);
            const int lastPage = pageAtScrollOffset(scrollY + client.bottom);
            for (int page = firstPage; page <= lastPage && page < pageCount; ++page) {
                auto bitmap = pageCache.Get(page, zoom, currentDpi);
                if (!bitmap || !bitmap->IsValid()) {
                    if (page == pageIndex && pixelWidth > 0 && !pixels.empty()) {
                        PageBitmap current;
                        current.page = pageIndex;
                        current.zoom = zoom;
                        current.dpi = currentDpi;
                        current.width = pixelWidth;
                        current.height = pixelHeight;
                        current.stride = pixelStride;
                        current.pixels = pixels;
                        bitmap = std::move(current);
                    }
                }
                if (!bitmap || !bitmap->IsValid()) continue;

                const int top = pagePixelOffsets[static_cast<std::size_t>(page)] - scrollY;
                const int left = pageLeft(client, bitmap->width) - scrollX;
                const RECT pageRect{ left, top, left + bitmap->width, top + bitmap->height };
                const RECT shadowRect{ pageRect.left + 5, pageRect.top + 5,
                    pageRect.right + 5, pageRect.bottom + 5 };
                const HBRUSH shadow = CreateSolidBrush(RGB(193, 198, 205));
                FillRect(mem, &shadowRect, shadow);
                DeleteObject(shadow);

                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = bitmap->width;
                info.bmiHeader.biHeight = -bitmap->height;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;
                RECT clipped{};
                if (IntersectRect(&clipped, &paint.rcPaint, &pageRect)) {
                    StretchDIBits(mem, clipped.left, clipped.top,
                        clipped.right - clipped.left, clipped.bottom - clipped.top,
                        clipped.left - left, clipped.top - top,
                        clipped.right - clipped.left, clipped.bottom - clipped.top,
                        bitmap->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
                }

                if (page == pageIndex) {
                    const RECT selectionRect{
                        std::min(selectionStart.x, selectionEnd.x),
                        std::min(selectionStart.y, selectionEnd.y),
                        std::max(selectionStart.x, selectionEnd.x),
                        std::max(selectionStart.y, selectionEnd.y) };
                    for (const std::size_t index : searchHighlights) {
                        if (index < textChunks.size()) {
                            fillOverlay(mem, chunkClientRect(textChunks[index], client, top),
                                96, RGB(255, 220, 40));
                        }
                    }
                    for (const std::size_t index : selectedChunks) {
                        if (index < textChunks.size()) {
                            fillOverlay(mem, chunkClientRect(textChunks[index], client, top),
                                112, RGB(45, 120, 235));
                        }
                    }
                    if (selectingText && selectionStart.x != selectionEnd.x &&
                        selectionStart.y != selectionEnd.y) {
                        fillOverlay(mem, selectionRect, 64, RGB(45, 120, 235));
                    }
                }
            }
        }
        BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, bufferWidth, bufferHeight,
            mem, 0, 0, SRCCOPY);
        SelectObject(mem, previousBitmap);
        DeleteObject(backing);
        DeleteDC(mem);
        EndPaint(window, &paint); return 0;
    }
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, handTool ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    if (message == WM_SIZE) {
        updateCanvasScrollbars();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_KEYDOWN) {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (control && (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT)) {
            setZoom(zoom - 0.10); return 0;
        }
        if (control && (wParam == VK_OEM_PLUS || wParam == VK_ADD)) {
            setZoom(zoom + 0.10); return 0;
        }
        if (control && wParam == '0') { setZoom(1.0); return 0; }
        if (control && wParam == 'F') {
            SetFocus(searchEdit); SendMessageW(searchEdit, EM_SETSEL, 0, -1); return 0;
        }
        if (control && wParam == 'C') { copySelectedText(); return 0; }
        RECT client{};
        GetClientRect(window, &client);
        const int viewportHeight = std::max(1L, client.bottom);
        const int line = scaleDip(24);
        switch (wParam) {
        case VK_PRIOR:
            if (pageLayoutMode == PageLayoutMode::ContinuousNavigation)
                scrollContinuousBy(-viewportHeight);
            else
                goToPage(pageIndex - 1);
            return 0;
        case VK_NEXT:
            if (pageLayoutMode == PageLayoutMode::ContinuousNavigation)
                scrollContinuousBy(viewportHeight);
            else
                goToPage(pageIndex + 1);
            return 0;
        case VK_HOME: goToPage(0); return 0;
        case VK_END: goToPage(pageCount - 1); return 0;
        case VK_UP:
            if (pageLayoutMode == PageLayoutMode::ContinuousNavigation)
                scrollContinuousBy(-line);
            else
                setCanvasScroll(SB_VERT, scrollY - line);
            return 0;
        case VK_DOWN:
            if (pageLayoutMode == PageLayoutMode::ContinuousNavigation)
                scrollContinuousBy(line);
            else
                setCanvasScroll(SB_VERT, scrollY + line);
            return 0;
        case VK_LEFT: setCanvasScroll(SB_HORZ, scrollX - line); return 0;
        case VK_RIGHT: setCanvasScroll(SB_HORZ, scrollX + line); return 0;
        case VK_F3: findText(); return 0;
        }
    }
    if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN) {
        if (message == WM_LBUTTONDOWN && !handTool) {
            selectingText = true;
            selectionStart = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            selectionEnd = selectionStart;
            selectedChunks.clear();
            SetCapture(window);
            SetFocus(window);
            return 0;
        }
        panning = true;
        panStart = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        panStartX = scrollX;
        panStartY = scrollY;
        SetCapture(window);
        SetFocus(window);
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return 0;
    }
    if (message == WM_MOUSEMOVE && panning) {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        setCanvasScroll(SB_HORZ, panStartX - (x - panStart.x));
        const int desiredY = panStartY - (y - panStart.y);
        if (pageLayoutMode == PageLayoutMode::ContinuousNavigation) {
            scrollContinuousBy(desiredY - scrollY);
        } else {
            setCanvasScroll(SB_VERT, desiredY);
        }
        return 0;
    }
    if (message == WM_MOUSEMOVE && selectingText) {
        const POINT next{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (next.x != selectionEnd.x || next.y != selectionEnd.y) {
            selectionEnd = next;
            updateLiveSelection();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    if (message == WM_LBUTTONUP || message == WM_MBUTTONUP) {
        if (GetCapture() == window) ReleaseCapture();
        if (message == WM_LBUTTONUP && selectingText) {
            updateLiveSelection();
            selectingText = false;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        panning = false;
        return 0;
    }
    if (message == WM_VSCROLL || message == WM_HSCROLL) {
        const int bar = message == WM_VSCROLL ? SB_VERT : SB_HORZ;
        const int old = bar == SB_VERT ? scrollY : scrollX;
        SCROLLINFO info{ sizeof(info), SIF_ALL }; GetScrollInfo(window, bar, &info);
        int next = old;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: next -= scaleDip(24); break;
        case SB_LINEDOWN: next += scaleDip(24); break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: next = info.nTrackPos; break;
        }
        next = std::clamp(next, 0, std::max(0, info.nMax - static_cast<int>(info.nPage) + 1));
        if (bar == SB_VERT && pageLayoutMode == PageLayoutMode::ContinuousNavigation) {
            scrollContinuousBy(next - old);
        } else {
            setCanvasScroll(bar, next);
        }
        return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(window, &point);
        if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
            setZoomAtPoint(zoom + (delta > 0 ? 0.10 : -0.10), point);
            return 0;
        }
        if ((GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0) {
            static double horizontalRemainder{};
            RECT client{};
            GetClientRect(window, &client);
            horizontalRemainder += static_cast<double>(delta) *
                wheelScrollDistance(std::max(1L, client.right)) / WHEEL_DELTA;
            const int step = static_cast<int>(horizontalRemainder);
            horizontalRemainder -= step;
            if (step != 0) setCanvasScroll(SB_HORZ, scrollX + step);
            return 0;
        }
        static double verticalRemainder{};
        RECT client{};
        GetClientRect(window, &client);
        addWheelDelta(verticalRemainder, delta, std::max(1L, client.bottom));
        return 0;
    }
    if (message == WM_MOUSEHWHEEL) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        static double horizontalRemainder{};
        RECT client{};
        GetClientRect(window, &client);
        horizontalRemainder += static_cast<double>(delta) *
            wheelScrollDistance(std::max(1L, client.right)) / WHEEL_DELTA;
        const int step = static_cast<int>(horizontalRemainder);
        horizontalRemainder -= step;
        if (step != 0) setCanvasScroll(SB_HORZ, scrollX + step);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        POINT screenPoint{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const HWND hovered = WindowFromPoint(screenPoint);
        if (GetFocus() == canvas || hovered == canvas) {
            return SendMessageW(canvas, message, wParam, lParam);
        }
    }
    if (message == WM_OPEN_COMPLETE) {
        if (!openReady.exchange(false, std::memory_order_acq_rel)) return 0;
        if (openThread.joinable()) openThread.join();
        OpenResult loaded;
        {
            std::lock_guard lock(openMutex);
            loaded = std::move(openResult);
            openResult = {};
        }
        EnableWindow(openButton, TRUE);
        if (!loaded.error.empty() || !loaded.document) {
            setStatus(L"Open failed: " + utf8ToWide(loaded.error.c_str()));
            return 0;
        }
        if (pendingOpenCreatesTab && !tabs.empty()) {
            captureActiveTabState();
            TabState tab;
            tab.path = loaded.path;
            tab.title = loaded.title;
            tab.document = loaded.document;
            tab.pageCount = loaded.pageCount;
            tab.pageIndex = 0;
            if (openPageAfterLoad >= 0 && openPageAfterLoad < tab.pageCount) {
                tab.pageIndex = openPageAfterLoad;
            }
            tab.zoom = zoom;
            tab.layout = pageLayoutMode;
            tabs.push_back(std::move(tab));
            pendingOpenCreatesTab = false;
            openPageAfterLoad = -1;
            activeTab = static_cast<int>(tabs.size()) - 1;
            continuousNextPage.reset();
            pageCache.Clear();
            applyTabState(tabs[static_cast<std::size_t>(activeTab)]);
            rebuildTabBar();
            SetWindowTextW(mainWindow, (loaded.title + L" - Pdf++ Reader").c_str());
            populateBookmarkTree(loaded.title);
            updatePageControls();
            renderPage();
            return 0;
        }
        pendingOpenCreatesTab = false;
        closeDocument();
        document = loaded.document;
        currentFilePath = loaded.path;
        pageCount = loaded.pageCount;
        pageIndex = 0;
        if (openPageAfterLoad >= 0 && openPageAfterLoad < pageCount) {
            pageIndex = openPageAfterLoad;
        }
        openPageAfterLoad = -1;
        populateBookmarkTree(loaded.title);
        SetWindowTextW(mainWindow, (loaded.title + L" - Pdf++ Reader").c_str());
        renderPage();
        if (tabs.empty()) {
            TabState tab;
            tab.path = loaded.path;
            tab.title = loaded.title;
            tab.document = document;
            tab.pageCount = pageCount;
            tab.pageIndex = pageIndex;
            tab.zoom = zoom;
            tab.layout = pageLayoutMode;
            tabs.push_back(std::move(tab));
            activeTab = 0;
            rebuildTabBar();
        }
        return 0;
    }
    if (message == WM_TIMER && wParam == ZOOM_TIMER) {
        const ULONGLONG elapsed = GetTickCount64() - zoomRequestTick;
        if (elapsed < kZoomDebounceMs) return 0;
        zoomRenderPending = false;
        KillTimer(window, ZOOM_TIMER);
        pageCache.Clear();
        updatePageGeometry();
        renderPage();
        return 0;
    }
    if (message == WM_TIMER && wParam == RENDER_TIMER) {
        if (renderReady.load(std::memory_order_acquire)) {
            KillTimer(window, RENDER_TIMER);
            SendMessageW(window, WM_RENDER_COMPLETE, 0, 0);
        }
        return 0;
    }
    if (message == WM_RENDER_COMPLETE) {
        if (!renderReady.exchange(false, std::memory_order_acq_rel)) return 0;
        KillTimer(window, RENDER_TIMER);
        if (renderThread.joinable()) renderThread.join();
        RenderResult result;
        {
            std::lock_guard lock(renderMutex);
            result = std::move(renderResult);
            renderResult = {};
        }
        const bool current = result.document == document && result.bitmap.page == pageIndex &&
            result.bitmap.dpi == currentDpi &&
            std::abs(result.bitmap.zoom - zoom) < 1.0e-9;
        if (!current && result.document == document && zoomRenderPending) {
            // The user changed zoom while the previous bitmap was rendering.
            // Never install the stale geometry; schedule only the newest zoom.
            SetTimer(window, ZOOM_TIMER, kZoomDebounceMs, nullptr);
        }
        if (result.prefetch) {
            const bool success = result.error.empty();
            if (success) rememberNativePage(result);
            if (success && pageLayoutMode == PageLayoutMode::ContinuousNavigation) {
                prefetchFurtherPage();
            }
            if (current) {
                if (success) applyCachedPage(pageIndex, zoom);
                else renderPage();
            }
            else if (result.document == document && !hasCachedPage(pageIndex, zoom)) renderPage();
        }
        else if (!current) {
            if (zoomRenderPending) {
                SetTimer(window, ZOOM_TIMER, kZoomDebounceMs, nullptr);
            } else {
                renderPage();
            }
        }
        else if (!result.error.empty()) {
            setStatus(L"Render failed: " + utf8ToWide(result.error.c_str()));
        }
        else {
            applyRenderedPage(std::move(result.bitmap));
            prefetchNextPage();
        }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        const HBRUSH background = CreateSolidBrush(PdfPP::ModernWin32::Theme::window);
        FillRect(dc, &client, background);
        DeleteObject(background);
        const int ribbonHeight = scaleDip(PdfPP::ModernWin32::Layout::ribbonHeight);
        const int statusHeight = scaleDip(PdfPP::ModernWin32::Layout::statusHeight);
        const int tabBarHeight = tabBar ? scaleDip(26) : 0;
        const int contentTop = ribbonHeight + tabBarHeight;
        RECT ribbon{ 0, 0, client.right, ribbonHeight };
        // Soft two-stop gradient gives the toolbar a slightly raised look.
        const HBRUSH ribbonBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::toolbar);
        FillRect(dc, &ribbon, ribbonBrush);
        DeleteObject(ribbonBrush);
        const int sidebarWidth = sidebarVisible ? scaleDip(PdfPP::ModernWin32::Layout::sidebarWidth) : 0;
        RECT sidebar{ 0, contentTop,
                     sidebarWidth,
                     client.bottom - statusHeight };
        const HBRUSH sidebarBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::sidebar);
        FillRect(dc, &sidebar, sidebarBrush);
        DeleteObject(sidebarBrush);
        RECT status{ 0, client.bottom - statusHeight,
                    client.right, client.bottom };
        const HBRUSH statusBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::status);
        FillRect(dc, &status, statusBrush);
        DeleteObject(statusBrush);

        SetBkMode(dc, TRANSPARENT);
        HPEN separator = CreatePen(PS_SOLID, 1, PdfPP::ModernWin32::Theme::border);
        const auto oldPen = SelectObject(dc, separator);
        const int sepTop = scaleDip(7);
        const int sepBottom = scaleDip(35);
        const int separators[] = { ribbonSep1, ribbonSep2, ribbonSep3 };
        for (const int x : separators) {
            if (x <= 0) continue;
            MoveToEx(dc, x, sepTop, nullptr);
            LineTo(dc, x, sepBottom);
            SelectObject(dc, separator);
        }
        SelectObject(dc, separator);
        MoveToEx(dc, 0, ribbonHeight - 1, nullptr);
        LineTo(dc, client.right, ribbonHeight - 1);
        if (sidebarVisible) {
            SelectObject(dc, separator);
            MoveToEx(dc, sidebarWidth, contentTop, nullptr);
            LineTo(dc, sidebarWidth, client.bottom - 1);
        }
        SelectObject(dc, oldPen);
        DeleteObject(separator);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const bool sidebar = control == pageList || control == sidebarTitle;
        const COLORREF background = control == statusLabel ? PdfPP::ModernWin32::Theme::status
            : sidebar ? PdfPP::ModernWin32::Theme::sidebar
            : PdfPP::ModernWin32::Theme::toolbar;
        PdfPP::ModernWin32::SetControlColors(dc, background, PdfPP::ModernWin32::Theme::text);
        static HBRUSH sidebarBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::sidebar);
        static HBRUSH toolbarBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::toolbar);
        return reinterpret_cast<LRESULT>(sidebar ? sidebarBrush : toolbarBrush);
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_DPICHANGED) {
        const UINT dpi = HIWORD(wParam);
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        applyDpiChange(window, dpi);
        if (suggested) {
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
        RECT client{};
        GetClientRect(window, &client);
        updateLayout(client.right, client.bottom);
        return 0;
    }
    if (message == WM_DISPLAYCHANGE) {
        applyDpiChange(window, GetDpiForWindow(window));
        RECT client{};
        GetClientRect(window, &client);
        updateLayout(client.right, client.bottom);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    if (message == WM_NOTIFY) {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->hwndFrom == pageList && header->code == TVN_SELCHANGEDW &&
            !updatingBookmarkSelection) {
            const auto* change = reinterpret_cast<const NMTREEVIEWW*>(lParam);
            const int selected = static_cast<int>(change->itemNew.lParam);
            if (selected >= 0 && selected < pageCount && selected != pageIndex) {
                goToPage(selected);
            }
            return 0;
        }
    }
    if (message == WM_COMMAND &&
        (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0)) {
        switch (LOWORD(wParam)) {
        case ID_OPEN:
            // Opening when a document is already loaded opens a new tab.
            openDocument();
            return 0;
        case ID_PRINT: printCurrentPage(); return 0;
        case ID_CLOSE: closeActiveTab(); return 0;
        case ID_CLOSE_TAB:
            if (pendingCloseTabIndex >= 0 && pendingCloseTabIndex < static_cast<int>(tabs.size())) {
                const int toClose = pendingCloseTabIndex;
                pendingCloseTabIndex = -1;
                requestCloseTab(toClose);
            } else {
                requestCloseTab(activeTab);
            }
            return 0;
        case SC_CLOSE: PostMessageW(window, WM_CLOSE, 0, 0); return 0;
        case ID_FIRST_PAGE: goToPage(0); return 0;
        case ID_PREVIOUS: goToPage(pageIndex - 1); return 0;
        case ID_NEXT: goToPage(pageIndex + 1); return 0;
        case ID_LAST_PAGE: goToPage(pageCount - 1); return 0;
        case ID_FIND:
            if (HIWORD(wParam) == BN_CLICKED) findText();
            else { SetFocus(searchEdit); SendMessageW(searchEdit, EM_SETSEL, 0, -1); }
            return 0;
        case ID_SEARCH_NEXT: findText(); return 0;
        case ID_ZOOM_OUT: setZoom(zoom - 0.10); return 0;
        case ID_ZOOM_IN: setZoom(zoom + 0.10); return 0;
        case ID_FIT_WIDTH: fitToWidth(); return 0;
        case ID_VIEW_FIT_PAGE: fitToPage(); return 0;
        case ID_VIEW_ACTUAL: setZoom(1.0); return 0;
        case ID_VIEW_CONTINUOUS:
            pageLayoutMode = PageLayoutMode::ContinuousNavigation;
            prefetchNextPage();
            updateCanvasScrollbars();
            InvalidateRect(canvas, nullptr, FALSE);
            updateCommandState();
            setStatus(L"Continuous navigation");
            return 0;
        case ID_VIEW_SINGLE_PAGE:
            pageLayoutMode = PageLayoutMode::SinglePage;
            continuousNextPage.reset();
            updateCanvasScrollbars();
            InvalidateRect(canvas, nullptr, FALSE);
            updateCommandState();
            setStatus(L"Single-page navigation");
            return 0;
        case ID_FULLSCREEN: toggleFullscreen(window); return 0;
        case ID_HAND_TOOL:
            handTool = true;
            updateCommandState();
            return 0;
        case ID_SELECT_TOOL:
            handTool = false;
            updateCommandState();
            return 0;
        case ID_DOC_PROPERTIES: showDocumentProperties(); return 0;
        case ID_ABOUT: showAboutDialog(); return 0;
        case ID_BOOKMARKS:
            sidebarVisible = !sidebarVisible;
            { RECT client{}; GetClientRect(window, &client); updateLayout(client.right, client.bottom); }
            updateCommandState();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case ID_SIDEBAR_TOGGLE:
            sidebarVisible = !sidebarVisible;
            { RECT client{}; GetClientRect(window, &client); updateLayout(client.right, client.bottom); }
            updateCommandState();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case ID_BOOKMARK_CLOSE:
            sidebarVisible = false;
            { RECT client{}; GetClientRect(window, &client); updateLayout(client.right, client.bottom); }
            updateCommandState();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case ID_ADD_FAVORITE: toggleCurrentFavorite(); return 0;
        default:
            if (LOWORD(wParam) >= ID_FAVORITE_BASE &&
                LOWORD(wParam) < ID_FAVORITE_BASE + static_cast<int>(AppSettings::kMaxFavorites)) {
                const std::size_t index = static_cast<std::size_t>(LOWORD(wParam) - ID_FAVORITE_BASE);
                if (index < settings.favorites.size()) {
                    const auto& favorite = settings.favorites[index];
                    if (std::filesystem::exists(favorite.path)) {
                        openPath(favorite.path);
                        openPageAfterLoad = favorite.page;
                    } else {
                        settings.favorites.erase(
                            settings.favorites.begin() + static_cast<std::ptrdiff_t>(index));
                        persistSettings();
                        rebuildFavoritesMenu();
                        setStatus(L"Favorite file no longer exists");
                    }
                }
                return 0;
            }
            if (LOWORD(wParam) >= ID_RECENT_BASE &&
                LOWORD(wParam) < ID_RECENT_BASE + static_cast<int>(AppSettings::kMaxRecentFiles)) {
                const std::size_t index = static_cast<std::size_t>(LOWORD(wParam) - ID_RECENT_BASE);
                if (index < settings.recentFiles.size()) {
                    const std::wstring path = settings.recentFiles[index];
                    if (std::filesystem::exists(path)) {
                        openPath(path);
                    } else {
                        settings.recentFiles.erase(
                            settings.recentFiles.begin() + static_cast<std::ptrdiff_t>(index));
                        persistSettings();
                        setStatus(L"File no longer exists: " + path);
                    }
                }
                return 0;
            }
            break;
        }
    }
    if (message == WM_SIZE) {
        updateLayout(LOWORD(lParam), HIWORD(lParam));
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_RETURN && GetFocus() == pageEdit) {
        wchar_t text[32]{}; GetWindowTextW(pageEdit, text, 32); const int page = _wtoi(text) - 1;
        if (page >= 0 && page < pageCount) { goToPage(page); } return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_RETURN && GetFocus() == searchEdit) {
        findText(); return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_F11) {
        toggleFullscreen(window); return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000) && tabs.size() > 1) {
        const int next = (activeTab + 1) % static_cast<int>(tabs.size());
        switchToTab(next);
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE && fullscreen) {
        toggleFullscreen(window); return 0;
    }
    if (message == WM_DROPFILES) {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0) {
            wchar_t path[MAX_PATH]{};
            if (DragQueryFileW(drop, 0, path, MAX_PATH) > 0) {
                openPath(path);
            }
        }
        DragFinish(drop);
        return 0;
    }
    if (message == WM_DESTROY) { closeDocument(); saveSettingsOnExit(); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace PdfPP::Win32
