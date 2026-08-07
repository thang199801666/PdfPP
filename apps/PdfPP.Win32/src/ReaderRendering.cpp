#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>

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
        pageGeometryBaseHeights.clear();
        return;
    }
    // ReaderPdfDocument uses the same scale for both direct Pdf++.Core page
    // geometry and the app-local Windows renderer: zoom * dpi / 96. The
    // point-to-pixel conversion below therefore matches the final bitmap.
    const bool needsBaseGeometry = geometryDocument != document ||
        pageGeometryBaseHeights.size() != static_cast<std::size_t>(pageCount);
    if (needsBaseGeometry) {
        pageGeometryBaseHeights.clear();
        pageGeometryBaseHeights.reserve(static_cast<std::size_t>(pageCount));
        for (int page = 0; page < pageCount; ++page) {
            int width = 0;
            int height = 0;
            if (!document->PageSize(page, kPageGeometryBaseScale, width, height) || height <= 0) {
                height = 0;
            }
            pageGeometryBaseHeights.push_back(height);
        }
    }
    const double scale = zoom * static_cast<double>(currentDpi) / USER_DEFAULT_SCREEN_DPI;
    int offset = scaleDip(12);
    pagePixelOffsets.reserve(static_cast<std::size_t>(pageCount));
    for (int page = 0; page < pageCount; ++page) {
        const int baseHeight = pageGeometryBaseHeights[static_cast<std::size_t>(page)];
        int height = baseHeight > 0
            ? static_cast<int>(std::lround(baseHeight * scale / kPageGeometryBaseScale))
            : 0;
        if (height <= 0) height = pixelHeight > 0 ? pixelHeight : scaleDip(800);
        pagePixelHeights.push_back(height);
        pagePixelOffsets.push_back(offset);
        offset += height + gapBetweenPages;
    }
    documentPixelHeight = offset + scaleDip(12) - gapBetweenPages;
    geometryZoom = zoom;
    geometryDpi = currentDpi;
    geometryDocument = document;
}

void cacheCurrentPageAndRelease() {
    if (pixelPage < 0 || pixelWidth <= 0 || pixelHeight <= 0 ||
        pixelStride <= 0 || pixels.empty()) return;
    PageBitmap entry;
    entry.page = pixelPage;
    entry.zoom = pixelZoom;
    entry.dpi = pixelDpi;
    entry.width = pixelWidth;
    entry.height = pixelHeight;
    entry.stride = pixelStride;
    entry.pixels = std::move(pixels);
    pageCache.Store(std::move(entry));
    pixelPage = -1;
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
    else if (pageLayoutMode != PageLayoutMode::ContinuousNavigation) {
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
    if (pageLayoutMode == PageLayoutMode::ContinuousNavigation &&
        documentPixelHeight > 0) {
        scrollY = std::clamp(scrollY, 0,
            std::max(0, documentPixelHeight - viewportHeight));
    }
    pageArrivalRequest.reset();
    updateCanvasScrollbars();
    updatePageControls();
    InvalidateRect(canvas, nullptr, FALSE);
    setStatus(L"Ready");
}

bool applyCachedPage(const int requestedPage, const double requestedZoom) {
    // Transfer the cached bitmap instead of copying its multi-megabyte pixel
    // vector. The previous current page is returned to the cache before page
    // changes, so this keeps navigation effectively zero-copy.
    auto page = pageCache.Take(requestedPage, requestedZoom, currentDpi);
    if (!page) return false;
    pixels = std::move(page->pixels);
    pixelWidth = page->width;
    pixelHeight = page->height;
    pixelStride = page->stride;
    pixelPage = requestedPage;
    pixelZoom = page->zoom;
    pixelDpi = page->dpi;
    syncRenderedPageHeight(requestedPage);
    requestTextGeometryRefresh();
    // A cached page is adopted while the user is already at an absolute
    // document offset. Do not treat it like a fresh page navigation.
    if (pendingScrollY >= 0) {
        scrollY = pendingScrollY;
        pendingScrollY = -1;
    }
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
        updateCanvasScrollbars();
        InvalidateRect(canvas, nullptr, FALSE);
        return;
    }
    if (renderThread.joinable() || hasCachedPage(nextPage, zoom)) return;
    startRender(nextPage, true);
}

// Keep the scroll buffer two pages ahead so a fast wheel sweep never hits
// an un-rendered gap at a page boundary. The second page is warmed into the
// cache only and is immediately available when it enters the viewport.
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
    const PageBitmap* nextPage = pageLayoutMode == PageLayoutMode::ContinuousNavigation
        ? pageCache.Peek(pageIndex + 1, zoom, currentDpi) : nullptr;
    const int nextWidth = nextPage && nextPage->IsValid() ? nextPage->width : 0;
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
    const PageBitmap* nextPage = pageLayoutMode == PageLayoutMode::ContinuousNavigation
        ? pageCache.Peek(pageIndex + 1, zoom, currentDpi) : nullptr;
    const int nextWidth = nextPage && nextPage->IsValid() ? nextPage->width : 0;
    const int content = bar == SB_VERT
        ? std::max(1, pixelHeight + pageGap)
        : std::max(1, std::max(pixelWidth, nextWidth) + pageGap);
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
    const int movement = previous - position;
    if (std::abs(movement) < viewport) {
        ScrollWindowEx(canvas, bar == SB_VERT ? 0 : movement,
            bar == SB_VERT ? movement : 0,
            nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
    } else {
        InvalidateRect(canvas, nullptr, FALSE);
    }
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
    if (target == oldScroll) return false;

    // Commit the complete logical scroll state before asking Windows to paint.
    // The previous implementation first copied the old window pixels with
    // ScrollWindowEx and changed page ownership afterwards. At a page boundary
    // that briefly mixed two different layouts, allowing the following page to
    // appear on top of the preceding page.
    scrollY = target;
    SetScrollPos(canvas, SB_VERT, scrollY, TRUE);

    const int targetPage = pageAtScrollOffset(target);
    if (targetPage != pageIndex) {
        cacheCurrentPageAndRelease();
        pendingScrollY = target;
        pageIndex = targetPage;
        if (applyCachedPage(targetPage, zoom)) {
            prefetchNextPage();
        } else {
            renderPage();
        }
    }

    // Paint this wheel/touchpad step immediately so scrolling retains the feel
    // of the fast version. We deliberately do not reuse on-screen pixels here:
    // canvasProc renders a coherent full viewport into a persistent back buffer
    // and presents it in one blit, preventing both horizontal tearing and page
    // overlap while still avoiding deferred/coalesced WM_PAINT updates.
    RedrawWindow(canvas, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    return true;
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
    pixelPage = bitmap.page;
    pixelZoom = bitmap.zoom;
    pixelDpi = bitmap.dpi;
    pixels = std::move(bitmap.pixels);
    syncRenderedPageHeight(pageIndex);
    requestTextGeometryRefresh();
    finishPageLayout();
}

void renderPage() {
    if (!document || pageIndex < 0 || pageIndex >= pageCount) return;
    updatePageGeometry();
    if (pixelPage == pageIndex && pixelWidth > 0 && pixelHeight > 0 &&
        pixelStride > 0 && !pixels.empty() && pixelDpi == currentDpi &&
        std::abs(pixelZoom - zoom) < 1.0e-9) {
        if (zoomRenderPending) {
            zoomRenderPending = false;
            zoomAnchor = {};
            KillTimer(mainWindow, ZOOM_TIMER);
        }
        requestTextGeometryRefresh();
        updateCanvasScrollbars();
        updatePageControls();
        InvalidateRect(canvas, nullptr, FALSE);
        return;
    }
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
    cacheCurrentPageAndRelease();
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
    if (targetPage != pageIndex) cacheCurrentPageAndRelease();
    pageArrivalRequest.reset();
    pendingScrollY = targetPage < static_cast<int>(pagePixelOffsets.size())
        ? pagePixelOffsets[static_cast<std::size_t>(targetPage)] : 0;
    pageIndex = targetPage;
    renderPage();
}

void setZoom(const double value) {
    const double next = std::clamp(value, AppSettings::kMinimumZoom, AppSettings::kMaximumZoom);
    if (std::abs(next - zoom) < 1.0e-9) return;
    zoomAnchor = {};
    zoom = next;
    requestZoomRender();
}

void setZoomAtPoint(const double value, POINT point) {
    const double next = std::clamp(value, AppSettings::kMinimumZoom, AppSettings::kMaximumZoom);
    if (std::abs(next - zoom) < 1.0e-9) return;
    if (!zoomRenderPending && pixelWidth > 0 && pixelHeight > 0) {
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
    requestZoomRender();
}

void requestZoomRender() {
    if (!document) return;
    zoomRenderPending = true;
    zoomRequestTick = GetTickCount64();
    SetTimer(mainWindow, ZOOM_TIMER, kZoomDebounceMs, nullptr);
    updateZoomLabel();
    InvalidateRect(canvas, nullptr, FALSE);
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
        if (findPatternMatches(utf8ToWide(textChunks[index].text.c_str()))) {
            searchHighlights.push_back(index);
        }
    }
}

void clearTextSelection() {
    selectedTextSpans.clear();
    selectionAnchor = {};
    selectionFocus = {};
}

void requestTextGeometryRefresh() {
    if (!document || pageIndex < 0 || pageIndex >= pageCount) {
        textChunks.clear();
        searchHighlights.clear();
        clearTextSelection();
        textGeometryPage = -1;
        textGeometryRequestPage = -1;
        return;
    }
    if (textGeometryPage == pageIndex) return;

    // Text extraction can be considerably more expensive than adopting an
    // already cached bitmap. Defer it until wheel/page navigation settles so
    // crossing a page boundary never blocks the UI thread.
    textChunks.clear();
    searchHighlights.clear();
    clearTextSelection();
    textGeometryRequestPage = pageIndex;
    SetTimer(mainWindow, TEXT_GEOMETRY_TIMER, kTextGeometryDebounceMs, nullptr);
}

void refreshTextGeometry() {
    textChunks.clear();
    searchHighlights.clear();
    clearTextSelection();
    if (document && pageIndex >= 0 && pageIndex < pageCount) {
        textChunks = document->TextChunks(pageIndex);
        textGeometryPage = pageIndex;
        updateSearchHighlights();
    } else {
        textGeometryPage = -1;
    }
}

namespace {

std::size_t chunkCharacterCount(const TextChunk& chunk) {
    return utf8ToWide(chunk.text.c_str()).size();
}

bool positionBefore(const TextPosition& first, const TextPosition& second) {
    return first.chunk < second.chunk ||
        (first.chunk == second.chunk && first.offset < second.offset);
}

void rebuildSelectionSpans() {
    selectedTextSpans.clear();
    if (!selectionAnchor.valid || !selectionFocus.valid || textChunks.empty()) return;

    TextPosition first = selectionAnchor;
    TextPosition last = selectionFocus;
    if (positionBefore(last, first)) std::swap(first, last);
    if (first.chunk >= textChunks.size() || last.chunk >= textChunks.size()) return;

    for (std::size_t index = first.chunk; index <= last.chunk; ++index) {
        const std::size_t length = chunkCharacterCount(textChunks[index]);
        const std::size_t begin = index == first.chunk
            ? std::min(first.offset, length) : 0U;
        const std::size_t end = index == last.chunk
            ? std::min(last.offset, length) : length;
        if (begin < end) selectedTextSpans.push_back({ index, begin, end });
    }
}

bool isWordCharacter(const wchar_t character) {
    return std::iswalnum(character) != 0 || character == L'_';
}

} // namespace

TextPosition hitTestTextPosition(const POINT point, const RECT& client,
                                 const int pageTop, const bool nearest) {
    TextPosition best{};
    long long bestDistance = (std::numeric_limits<long long>::max)();

    for (std::size_t index = 0; index < textChunks.size(); ++index) {
        const std::wstring text = utf8ToWide(textChunks[index].text.c_str());
        if (text.empty()) continue;
        RECT rect = chunkClientRect(textChunks[index], client, pageTop);
        const int padding = scaleDip(2);
        RECT hitRect{ rect.left - padding, rect.top - padding,
                      rect.right + padding, rect.bottom + padding };
        const bool inside = PtInRect(&hitRect, point) != FALSE;
        if (!nearest && !inside) continue;

        const int dx = point.x < rect.left ? rect.left - point.x
            : (point.x > rect.right ? point.x - rect.right : 0);
        const int dy = point.y < rect.top ? rect.top - point.y
            : (point.y > rect.bottom ? point.y - rect.bottom : 0);
        // Prefer the closest text line before horizontal proximity. This makes
        // a drag through the whitespace at the end of a line behave like
        // Acrobat/Chrome rather than jumping to a word on another line.
        const long long distance = static_cast<long long>(dy) * dy * 16LL +
            static_cast<long long>(dx) * dx;
        if (distance > bestDistance) continue;

        const int width = std::max(1, static_cast<int>(rect.right - rect.left));
        const double fraction = std::clamp(
            static_cast<double>(point.x - rect.left) / width, 0.0, 1.0);
        const std::size_t offset = std::min(text.size(),
            static_cast<std::size_t>(std::lround(fraction * text.size())));
        best = { true, index, offset };
        bestDistance = distance;
        if (inside && !nearest) break;
    }
    return best;
}

RECT selectionSpanClientRect(const TextSelectionSpan& span, const RECT& client,
                             const int pageTop) {
    if (span.chunk >= textChunks.size()) return {};
    const std::size_t length = chunkCharacterCount(textChunks[span.chunk]);
    if (length == 0) return {};

    RECT rect = chunkClientRect(textChunks[span.chunk], client, pageTop);
    const int width = std::max(1, static_cast<int>(rect.right - rect.left));
    const std::size_t begin = std::min(span.begin, length);
    const std::size_t end = std::min(std::max(span.end, begin), length);
    const int selectedLeft = rect.left + static_cast<int>(std::lround(
        static_cast<double>(width) * begin / length));
    const int selectedRight = rect.left + static_cast<int>(std::lround(
        static_cast<double>(width) * end / length));
    // A one-pixel vertical expansion hides tiny gaps between adjacent glyph
    // runs while still keeping the blue selection aligned to the text line.
    return RECT{ selectedLeft, rect.top - 1,
                 std::max(selectedLeft + 1, selectedRight), rect.bottom + 1 };
}

void beginTextSelection(const POINT point, const bool extendExisting) {
    RECT client{};
    GetClientRect(canvas, &client);
    const int pageTop = pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
        ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] - scrollY
        : scaleDip(12) - scrollY;
    const TextPosition hit = hitTestTextPosition(point, client, pageTop, false);
    if (!hit.valid) {
        clearTextSelection();
        selectingText = false;
        return;
    }

    if (!extendExisting || !selectionAnchor.valid) selectionAnchor = hit;
    selectionFocus = hit;
    selectionStart = point;
    selectionEnd = point;
    selectingText = true;
    rebuildSelectionSpans();
}

void selectWordAt(const POINT point) {
    RECT client{};
    GetClientRect(canvas, &client);
    const int pageTop = pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
        ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] - scrollY
        : scaleDip(12) - scrollY;
    TextPosition hit = hitTestTextPosition(point, client, pageTop, false);
    if (!hit.valid || hit.chunk >= textChunks.size()) {
        clearTextSelection();
        return;
    }

    const std::wstring text = utf8ToWide(textChunks[hit.chunk].text.c_str());
    if (text.empty()) {
        clearTextSelection();
        return;
    }
    std::size_t character = std::min(hit.offset, text.size() - 1U);
    if (character > 0 && hit.offset == text.size()) --character;
    const bool word = isWordCharacter(text[character]);
    std::size_t begin = character;
    std::size_t end = character + 1U;
    while (begin > 0 && isWordCharacter(text[begin - 1U]) == word) --begin;
    while (end < text.size() && isWordCharacter(text[end]) == word) ++end;

    selectionAnchor = { true, hit.chunk, begin };
    selectionFocus = { true, hit.chunk, end };
    selectedTextSpans = { TextSelectionSpan{ hit.chunk, begin, end } };
    selectionStart = point;
    selectionEnd = point;
}

void selectAllText() {
    clearTextSelection();
    if (textChunks.empty()) return;
    std::size_t first = 0;
    while (first < textChunks.size() && chunkCharacterCount(textChunks[first]) == 0) ++first;
    if (first == textChunks.size()) return;
    std::size_t last = textChunks.size() - 1U;
    while (last > first && chunkCharacterCount(textChunks[last]) == 0) --last;
    selectionAnchor = { true, first, 0 };
    selectionFocus = { true, last, chunkCharacterCount(textChunks[last]) };
    rebuildSelectionSpans();
}

// Recompute character-accurate spans from an anchor and moving focus. Unlike
// a rubber-band rectangle this follows document reading order across lines,
// matching selection behaviour in Acrobat and browser PDF viewers.
void updateLiveSelection() {
    if (!selectingText || !selectionAnchor.valid) return;
    RECT client{};
    GetClientRect(canvas, &client);
    const int pageTop = pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
        ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] - scrollY
        : scaleDip(12) - scrollY;
    const TextPosition hit = hitTestTextPosition(selectionEnd, client, pageTop, true);
    if (!hit.valid) return;
    selectionFocus = hit;
    rebuildSelectionSpans();
}

} // namespace PdfPP::Win32
