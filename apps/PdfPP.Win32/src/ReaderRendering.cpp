#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <cmath>

namespace PdfPP::Win32 {

// Recomputes per-page heights for the current zoom/dpi and the running
// offsets used to translate scroll positions into absolute document space.
// Results are cached; callers invoke this on zoom/dpi/document changes only.
void updatePageGeometry() {
    const bool sameDocument = document && geometryDocument == document &&
        std::abs(geometryZoom - zoom) < 1.0e-9 && geometryDpi == currentDpi;
    gapBetweenPages = scaleDip(24);
    if (sameDocument && !pagePixelOffsets.empty()) return;
    pagePixelHeights.clear();
    pagePixelOffsets.clear();
    documentPixelHeight = 0;
    if (!document) {
        geometryDocument.reset();
        return;
    }
    // The native page-size scale matches pdfpp_render: zoom * dpi / 96.
    // pdfpp_page_size then converts points -> pixels with 96/72, so the
    // result equals the actually rendered bitmap size.
    const double scale = zoom * static_cast<double>(currentDpi) / USER_DEFAULT_SCREEN_DPI;
    int offset = scaleDip(12);
    pagePixelOffsets.reserve(static_cast<std::size_t>(pageCount));
    for (int page = 0; page < pageCount; ++page) {
        int width = 0;
        int height = 0;
        const bool ok = document->PageSize(page, scale, width, height);
        if (!ok || height <= 0) height = pixelHeight > 0 ? pixelHeight : scaleDip(800);
        pagePixelHeights.push_back(height);
        pagePixelOffsets.push_back(offset);
        offset += height + gapBetweenPages;
    }
    documentPixelHeight = offset + scaleDip(12) - gapBetweenPages;
    geometryZoom = zoom;
    geometryDpi = currentDpi;
    geometryDocument = document;
}

void rememberCurrentPage() {
    if (pixelWidth <= 0 || pixelHeight <= 0 || pixelStride <= 0 || pixels.empty()) return;
    PageBitmap entry;
    entry.page = pageIndex;
    entry.zoom = zoom;
    entry.dpi = currentDpi;
    entry.width = pixelWidth;
    entry.height = pixelHeight;
    entry.stride = pixelStride;
    entry.pixels = pixels;
    pageCache.Store(std::move(entry));
}

void cacheCurrentPageAndRelease() {
    if (pixelWidth <= 0 || pixelHeight <= 0 || pixelStride <= 0 || pixels.empty()) return;
    PageBitmap entry;
    entry.page = pageIndex;
    entry.zoom = zoom;
    entry.dpi = currentDpi;
    entry.width = pixelWidth;
    entry.height = pixelHeight;
    entry.stride = pixelStride;
    entry.pixels = std::move(pixels);
    pageCache.Store(std::move(entry));
}

bool hasCachedPage(const int requestedPage, const double requestedZoom) {
    return pageCache.Contains(requestedPage, requestedZoom, currentDpi);
}

void rememberNativePage(RenderResult& result) {
    const int renderedPage = result.bitmap.page;
    const int renderedHeight = result.bitmap.height;
    const bool isNextPage = pageLayoutMode == PageLayoutMode::ContinuousNavigation &&
        renderedPage == pageIndex + 1 &&
        result.bitmap.zoom == zoom && result.bitmap.dpi == currentDpi;
    pageCache.Store(std::move(result.bitmap));
    // Adopt the true rendered height for this page so subsequent pages are
    // laid out below it without overlapping (handles rotated pages whose
    // pixel size differs from the unrotated crop box).
    if (renderedPage >= 0 && renderedPage < pageCount && renderedHeight > 0 &&
        renderedPage < static_cast<int>(pagePixelHeights.size())) {
        const std::size_t index = static_cast<std::size_t>(renderedPage);
        const int delta = renderedHeight - pagePixelHeights[index];
        if (delta != 0) {
            pagePixelHeights[index] = renderedHeight;
            for (std::size_t i = index + 1; i < pagePixelOffsets.size(); ++i) {
                pagePixelOffsets[i] += delta;
            }
            documentPixelHeight = std::max(0, documentPixelHeight + delta);
        }
    }
    if (isNextPage) {
        continuousNextPage = pageCache.Take(pageIndex + 1, zoom, currentDpi);
        updateCanvasScrollbars();
        InvalidateRect(canvas, nullptr, FALSE);
    }
}

void finishPageLayout() {
    RECT client{};
    GetClientRect(canvas, &client);
    const int viewportHeight = std::max(1L, client.bottom);
    if (pendingScrollY >= 0) {
        scrollY = pendingScrollY;
        pendingScrollY = -1;
    }
    else if (zoomAnchor.valid) {
        const int left = pageLeft(client, pixelWidth);
        const int top = pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
            ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] : 0;
        scrollX = static_cast<int>(std::lround(left + zoomAnchor.pageX * pixelWidth - zoomAnchor.clientX));
        scrollY = static_cast<int>(std::lround(top + zoomAnchor.pageY * pixelHeight - zoomAnchor.clientY));
        zoomAnchor.valid = false;
    }
    else {
        scrollX = 0;
        const bool arriveAtBottom = pageArrivalRequest &&
            pageArrivalRequest->page == pageIndex &&
            pageArrivalRequest->vertical == VerticalPageArrival::Bottom;
        const int top = pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
            ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] : 0;
        scrollY = arriveAtBottom
            ? std::max(0, top + pixelHeight - viewportHeight)
            : top;
    }
    pageArrivalRequest.reset();
    updateCanvasScrollbars();
    updatePageControls();
    InvalidateRect(canvas, nullptr, FALSE);
    setStatus(L"Ready");
}

bool applyCachedPage(const int requestedPage, const double requestedZoom) {
    auto page = pageCache.Get(requestedPage, requestedZoom, currentDpi);
    if (!page) return false;
    pixels = std::move(page->pixels);
    pixelWidth = page->width;
    pixelHeight = page->height;
    pixelStride = page->stride;
    syncRenderedPageHeight(requestedPage);
    refreshTextGeometry();
    finishPageLayout();
    if (pageLayoutMode == PageLayoutMode::ContinuousNavigation) prefetchNextPage();
    setStatus(L"Ready (cached)");
    return true;
}

void startRender(const int requestedPage, const bool prefetch) {
    if (!document || requestedPage < 0 || requestedPage >= pageCount) return;
    if (!prefetch) setStatus(L"Rendering...");
    const auto requestedDocument = document;
    const double requestedZoom = zoom;
    const UINT requestedDpi = currentDpi;
    renderReady.store(false, std::memory_order_release);
    renderThread = std::thread([requestedDocument, requestedPage, requestedZoom, requestedDpi, prefetch] {
        RenderResult result;
        result.document = requestedDocument;
        result.prefetch = prefetch;
        result.bitmap = requestedDocument->Render(
            requestedPage, requestedZoom, requestedDpi, result.error);
        {
            std::lock_guard lock(renderMutex);
            renderResult = std::move(result);
        }
        renderReady.store(true, std::memory_order_release);
        PostMessageW(mainWindow, WM_RENDER_COMPLETE, 0, 0);
        });
}

void prefetchNextPage() {
    const int nextPage = pageIndex + 1;
    if (!document || nextPage >= pageCount) return;
    if (pageLayoutMode == PageLayoutMode::ContinuousNavigation &&
        hasCachedPage(nextPage, zoom)) {
        continuousNextPage = pageCache.Take(nextPage, zoom, currentDpi);
        updateCanvasScrollbars();
        InvalidateRect(canvas, nullptr, FALSE);
        return;
    }
    if (renderThread.joinable() || hasCachedPage(nextPage, zoom)) return;
    startRender(nextPage, true);
}

// Keep the scroll buffer two pages ahead so a fast wheel sweep never hits
// an un-rendered gap at a page boundary. The second page is warmed into the
// cache only, and becomes continuousNextPage as soon as it is reached.
void prefetchFurtherPage() {
    const int furtherPage = pageIndex + 2;
    if (!document || furtherPage >= pageCount) return;
    if (renderThread.joinable() || hasCachedPage(furtherPage, zoom)) return;
    startRender(furtherPage, true);
}

void updateCanvasScrollbars() {
    if (!canvas) return;
    RECT client{}; GetClientRect(canvas, &client);
    const int viewportWidth = std::max(1L, client.right);
    const int viewportHeight = std::max(1L, client.bottom);
    const int nextWidth = continuousNextPage &&
        pageLayoutMode == PageLayoutMode::ContinuousNavigation
        ? continuousNextPage->width : 0;
    const int contentWidth = std::max(1, std::max(pixelWidth, nextWidth) + scaleDip(24));
    // The vertical scrollbar reflects the entire document, not just the
    // rendered window, so dragging the thumb sweeps through every page.
    const int contentHeight = documentPixelHeight > 0
        ? documentPixelHeight
        : std::max(1, pixelHeight + scaleDip(24));
    const int maxHorizontal = std::max(0, contentWidth - viewportWidth);
    const int maxVertical = std::max(0, contentHeight - viewportHeight);
    scrollX = std::clamp(scrollX, 0, maxHorizontal);
    scrollY = std::clamp(scrollY, 0, maxVertical);
    SCROLLINFO horizontal{ sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                          contentWidth - 1, static_cast<UINT>(viewportWidth), scrollX };
    SCROLLINFO vertical{ sizeof(vertical), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                        contentHeight - 1, static_cast<UINT>(viewportHeight), scrollY };
    SetScrollInfo(canvas, SB_HORZ, &horizontal, TRUE);
    SetScrollInfo(canvas, SB_VERT, &vertical, TRUE);
}

void setCanvasScroll(const int bar, int position) {
    if (!canvas) return;
    RECT client{}; GetClientRect(canvas, &client);
    const int viewport = bar == SB_VERT ? std::max(1L, client.bottom) : std::max(1L, client.right);
    const int pageGap = scaleDip(24);
    if (bar == SB_VERT && pageLayoutMode == PageLayoutMode::ContinuousNavigation &&
        documentPixelHeight > 0) {
        const int maximum = std::max(0, documentPixelHeight - viewport);
        position = std::clamp(position, 0, maximum);
        const int previous = scrollY;
        if (position == previous) return;
        scrollContinuousBy(position - previous);
        SetScrollPos(canvas, bar, scrollY, TRUE);
        return;
    }
    const int content = bar == SB_VERT
        ? std::max(1, pixelHeight + pageGap)
        : std::max(1, std::max(pixelWidth,
            (continuousNextPage && pageLayoutMode == PageLayoutMode::ContinuousNavigation)
            ? continuousNextPage->width : 0) + pageGap);
    const int maximum = std::max(0, content - viewport);
    position = std::clamp(position, 0, maximum);
    const int previous = bar == SB_VERT ? scrollY : scrollX;
    if (previous == position) {
        // Wheel/track messages continue to arrive at the edge of the document.
        // Avoid invalidating an unchanged canvas, otherwise the page and its
        // shadow visibly flicker at the scroll limit.
        return;
    }
    if (bar == SB_VERT) scrollY = position; else scrollX = position;
    SetScrollPos(canvas, bar, position, TRUE);
    // Repaint the whole canvas. The WM_PAINT path already double-buffers into
    // a scratch surface and WM_ERASEBKGND is suppressed, so a full invalidate
    // stays flicker-free.
    InvalidateRect(canvas, nullptr, FALSE);
}

// Move through the document using absolute scroll offsets. scrollY is the
// distance from the top of the first page, so dragging the scrollbar thumb
// lands on any page of the document, not just the currently rendered ones.
bool scrollContinuousBy(int delta) {
    if (pageLayoutMode != PageLayoutMode::ContinuousNavigation || delta == 0 || pixelHeight <= 0) {
        return false;
    }
    RECT client{};
    GetClientRect(canvas, &client);
    const int viewportHeight = std::max(1, static_cast<int>(client.bottom));
    const int maximumScroll = std::max(0, documentPixelHeight - viewportHeight);
    const int oldScroll = scrollY;
    const int target = std::clamp(scrollY + delta, 0, maximumScroll);

    // Absolute offset where the next page really begins, derived from the
    // current page's actual rendered height. This must match how WM_PAINT
    // places the next page (top + pixelHeight + gap), otherwise scrolling
    // promotes too early and the next page appears to "eat" the previous one.
    const int currentPageTop = pageIndex < static_cast<int>(pagePixelOffsets.size())
        ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] : 0;
    const int nextPageTop = currentPageTop + pixelHeight + gapBetweenPages;

    if (target < nextPageTop) {
        // Still inside the current page or the gap below it: the current
        // bitmap stays; both pages are painted naturally.
        if (target < pagePixelOffsets[static_cast<std::size_t>(pageIndex)]) {
            // Scrolled back above the top of the current page.
            if (pageIndex > 0) {
                auto previous = pageCache.Take(pageIndex - 1, zoom, currentDpi);
                if (previous) {
                    if (!tryCrossPageBoundary()) {
                        scrollY = target;
                        InvalidateRect(canvas, nullptr, FALSE);
                        return scrollY != oldScroll;
                    }
                    cacheCurrentPageAndRelease();
                    --pageIndex;
                    pixels = std::move(previous->pixels);
                    pixelWidth = previous->width;
                    pixelHeight = previous->height;
                    pixelStride = previous->stride;
                    scrollY = target;
                    refreshTextGeometry();
                    updatePageControls();
                    updateCanvasScrollbars();
                    continuousNextPage = pageCache.Take(pageIndex + 1, zoom, currentDpi);
                    InvalidateRect(canvas, nullptr, FALSE);
                    prefetchNextPage();
                    return true;
                }
                // Fall through: previous page not cached, render it.
                pendingScrollY = target;
                scrollY = target;
                --pageIndex;
                renderPage();
                return true;
            }
        }
        scrollY = target;
        if (scrollY != oldScroll) InvalidateRect(canvas, nullptr, FALSE);
        return scrollY != oldScroll;
    }

    // target is at or past the top of the next page: the current page has
    // fully scrolled away, so it is safe to promote the next bitmap.
    if (pageIndex + 1 < pageCount && continuousNextPage) {
        if (!tryCrossPageBoundary()) {
            scrollY = target;
            InvalidateRect(canvas, nullptr, FALSE);
            return scrollY != oldScroll;
        }
        cacheCurrentPageAndRelease();
        PageBitmap next = std::move(*continuousNextPage);
        continuousNextPage.reset();
        ++pageIndex;
        pixels = std::move(next.pixels);
        pixelWidth = next.width;
        pixelHeight = next.height;
        pixelStride = next.stride;
        scrollY = target;
        refreshTextGeometry();
        updatePageControls();
        updateCanvasScrollbars();
        InvalidateRect(canvas, nullptr, FALSE);
        prefetchNextPage();
        return true;
    }

    // The next (or a farther) page is not rendered yet: schedule it and
    // remember the absolute offset so finishPageLayout jumps straight there.
    int farPage = pageIndex;
    for (int page = 0; page < pageCount; ++page) {
        if (page >= static_cast<int>(pagePixelOffsets.size())) break;
        const int offset = pagePixelOffsets[static_cast<std::size_t>(page)];
        const int height = pagePixelHeights.empty()
            ? pixelHeight : pagePixelHeights[static_cast<std::size_t>(page)];
        if (target < offset + height) { farPage = page; break; }
        farPage = page;
    }
    farPage = std::clamp(farPage, 0, std::max(0, pageCount - 1));
    if (farPage != pageIndex) {
        pendingScrollY = target;
        scrollY = target;
        pageIndex = farPage;
        renderPage();
        return true;
    }
    scrollY = target;
    return scrollY != oldScroll;
}

// After a page is actually rendered, adopt its true pixel height into the
// geometry so following pages sit exactly below it (no overlap).
void syncRenderedPageHeight(const int page) {
    if (page < 0 || page >= pageCount ||
        page >= static_cast<int>(pagePixelHeights.size()) ||
        pixelHeight <= 0) return;
    const std::size_t index = static_cast<std::size_t>(page);
    if (pagePixelHeights[index] == pixelHeight) return;
    const int delta = pixelHeight - pagePixelHeights[index];
    pagePixelHeights[index] = pixelHeight;
    for (std::size_t i = index + 1; i < pagePixelOffsets.size(); ++i) {
        pagePixelOffsets[i] += delta;
    }
    documentPixelHeight = std::max(0, documentPixelHeight + delta);
}

void applyRenderedPage(PageBitmap bitmap) {
    if (!bitmap.IsValid()) return;
    pixelWidth = bitmap.width;
    pixelHeight = bitmap.height;
    pixelStride = bitmap.stride;
    pixels = std::move(bitmap.pixels);
    syncRenderedPageHeight(pageIndex);
    refreshTextGeometry();
    continuousNextPage.reset();
    rememberCurrentPage();
    finishPageLayout();
}

void renderPage() {
    if (!document || pageIndex < 0 || pageIndex >= pageCount) return;
    updatePageGeometry();
    continuousNextPage.reset();
    // A render already in flight owns the current document safely. Let it
    // finish in the background; WM_RENDER_COMPLETE will discard stale results.
    if (renderThread.joinable()) {
        SetTimer(mainWindow, RENDER_TIMER, 15, nullptr);
        return;
    }
    if (applyCachedPage(pageIndex, zoom)) return;
    startRender(pageIndex, false);
}

bool navigatePage(const int targetPage, const VerticalPageArrival arrival) {
    if (!document || targetPage < 0 || targetPage >= pageCount || targetPage == pageIndex) {
        return false;
    }
    zoomAnchor = {};
    pageArrivalRequest = PageArrivalRequest{ targetPage, arrival };
    if (arrival == VerticalPageArrival::Bottom) {
        pendingScrollY = -1;
    } else {
        pendingScrollY = targetPage < static_cast<int>(pagePixelOffsets.size())
            ? pagePixelOffsets[static_cast<std::size_t>(targetPage)] : 0;
    }
    pageIndex = targetPage;
    renderPage();
    return true;
}

// Navigate to an absolute page index, jumping the scrollbar to the top of
// that page. Used by menus, shortcuts, TOC and page-number entry.
void goToPage(const int targetPage) {
    if (!document || targetPage < 0 || targetPage >= pageCount) return;
    zoomAnchor = {};
    pageArrivalRequest.reset();
    pendingScrollY = targetPage < static_cast<int>(pagePixelOffsets.size())
        ? pagePixelOffsets[static_cast<std::size_t>(targetPage)] : 0;
    pageIndex = targetPage;
    renderPage();
}

void setZoom(const double value) {
    const double next = std::clamp(value, 0.25, 3.0);
    if (std::abs(next - zoom) < 1.0e-9) return;
    zoomAnchor = {};
    zoom = next;
    renderPage();
}

void setZoomAtPoint(const double value, POINT point) {
    const double next = std::clamp(value, 0.25, 3.0);
    if (std::abs(next - zoom) < 1.0e-9) return;
    if (pixelWidth > 0 && pixelHeight > 0) {
        RECT client{};
        GetClientRect(canvas, &client);
        const int left = pageLeft(client, pixelWidth);
        zoomAnchor.valid = true;
        zoomAnchor.clientX = point.x;
        zoomAnchor.clientY = point.y;
        zoomAnchor.pageX = std::clamp(
            static_cast<double>(point.x + scrollX - left) / pixelWidth, 0.0, 1.0);
        const int pageTop = pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
            ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] : scaleDip(12);
        zoomAnchor.pageY = std::clamp(
            static_cast<double>(point.y + scrollY - pageTop) / pixelHeight, 0.0, 1.0);
    }
    zoom = next;
    renderPage();
}

void fitToWidth() {
    if (pixelWidth <= 0) return;
    RECT client{}; GetClientRect(canvas, &client);
    const double available = std::max(1L, client.right - scaleDip(PdfPP::ModernWin32::Layout::gutter) * 2);
    setZoom(zoom * available / pixelWidth);
}

void fitToPage() {
    if (pixelWidth <= 0 || pixelHeight <= 0) return;
    RECT client{}; GetClientRect(canvas, &client);
    const double availableWidth = std::max(1L, client.right - scaleDip(PdfPP::ModernWin32::Layout::gutter) * 2);
    const double availableHeight = std::max(1L, client.bottom - scaleDip(PdfPP::ModernWin32::Layout::gutter) * 2);
    setZoom(zoom * std::min(availableWidth / pixelWidth, availableHeight / pixelHeight));
}

void captureCanvasCenterAnchor() {
    if (!canvas || pixelWidth <= 0 || pixelHeight <= 0) return;
    RECT client{};
    GetClientRect(canvas, &client);
    const POINT point{ client.right / 2, client.bottom / 2 };
    const int left = pageLeft(client, pixelWidth);
    zoomAnchor.valid = true;
    zoomAnchor.clientX = point.x;
    zoomAnchor.clientY = point.y;
    zoomAnchor.pageX = std::clamp(
        static_cast<double>(point.x + scrollX - left) / pixelWidth, 0.0, 1.0);
    const int pageTop = pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
        ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] : scaleDip(12);
    zoomAnchor.pageY = std::clamp(
        static_cast<double>(point.y + scrollY - pageTop) / pixelHeight, 0.0, 1.0);
}

void applyDpiChange(HWND window, const UINT dpi) {
    if (!dpi || dpi == currentDpi) return;
    captureCanvasCenterAnchor();
    currentDpi = dpi;
    refreshApplicationFonts(dpi);
    pageCache.Clear();
    RECT client{};
    GetClientRect(window, &client);
    updateLayout(client.right, client.bottom);
    InvalidateRect(window, nullptr, TRUE);
    InvalidateRect(canvas, nullptr, TRUE);
    if (document) renderPage();
}

void updateSearchHighlights() {
    searchHighlights.clear();
    if (lastSearchQuery.empty()) return;
    for (std::size_t index = 0; index < textChunks.size(); ++index) {
        if (utf8ToWide(textChunks[index].text.c_str()).find(lastSearchQuery) != std::wstring::npos) {
            searchHighlights.push_back(index);
        }
    }
}

void refreshTextGeometry() {
    textChunks.clear();
    searchHighlights.clear();
    selectedChunks.clear();
    if (document && pageIndex >= 0 && pageIndex < pageCount) {
        textChunks = document->TextChunks(pageIndex);
        updateSearchHighlights();
    }
}

void selectTextChunks(const RECT& selection, const RECT& client, const int pageTop) {
    selectedChunks.clear();
    for (std::size_t index = 0; index < textChunks.size(); ++index) {
        RECT chunk = chunkClientRect(textChunks[index], client, pageTop);
        const RECT normalized{ std::min(selection.left, selection.right),
                              std::min(selection.top, selection.bottom),
                              std::max(selection.left, selection.right),
                              std::max(selection.top, selection.bottom) };
        RECT intersection{};
        if (IntersectRect(&intersection, &chunk, &normalized)) selectedChunks.push_back(index);
    }
}

// Recompute the highlighted text from the current drag selection so the
// highlights follow the pointer live (like regular PDF readers).
void updateLiveSelection() {
    if (!selectingText) { selectedChunks.clear(); return; }
    RECT client{}; GetClientRect(canvas, &client);
    const int pageTop = pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
        ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] - scrollY
        : scaleDip(12) - scrollY;
    const RECT selection =
        selectionEnd.x == selectionStart.x && selectionEnd.y == selectionStart.y
        ? RECT{ selectionStart.x - 1, selectionStart.y - 1,
                selectionStart.x + 1, selectionStart.y + 1 }
        : RECT{ selectionStart.x, selectionStart.y, selectionEnd.x, selectionEnd.y };
    selectTextChunks(selection, client, pageTop);
}

} // namespace PdfPP::Win32
