#include <PdfPP/Win32/ReaderState.hpp>

#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <climits>
#include <iterator>
#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace PdfPP::Win32 {

namespace {

void centerPageEditFormattingRect() {
    if (!pageEdit) return;
    RECT client{};
    GetClientRect(pageEdit, &client);
    const int width = static_cast<int>(client.right - client.left);
    const int height = static_cast<int>(client.bottom - client.top);
    if (width <= 0 || height <= 0) return;

    HDC dc = GetDC(pageEdit);
    TEXTMETRICW metrics{};
    if (dc) {
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(pageEdit, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
        GetTextMetricsW(dc, &metrics);
        if (oldFont) SelectObject(dc, oldFont);
        ReleaseDC(pageEdit, dc);
    }

    const int textHeight = metrics.tmHeight > 0
        ? static_cast<int>(metrics.tmHeight)
        : scaleDip(14);
    const int horizontalInset = scaleDip(3);
    const int verticalInset = (std::max)(0, (height - textHeight) / 2);
    RECT format{
        horizontalInset,
        verticalInset,
        (std::max)(horizontalInset + 1, width - horizontalInset),
        (std::min)(height, verticalInset + textHeight + scaleDip(1))
    };
    SendMessageW(pageEdit, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&format));
    InvalidateRect(pageEdit, nullptr, TRUE);
}

Gdiplus::Color canvasGpColor(const COLORREF color, const BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void addCanvasRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect,
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

void fillCanvasRoundedRect(Gdiplus::Graphics& graphics, const RECT& rect,
                           const COLORREF color, const BYTE alpha,
                           const int radiusPx) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::GraphicsPath path;
    addCanvasRoundedRect(path,
        Gdiplus::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
            static_cast<float>(rect.right - rect.left),
            static_cast<float>(rect.bottom - rect.top)),
        static_cast<float>(radiusPx));
    Gdiplus::SolidBrush brush(canvasGpColor(color, alpha));
    graphics.FillPath(&brush, &path);
}

void paintModernCanvasSurface(HDC dc, const RECT& client) {
    if (!dc || client.right <= client.left || client.bottom <= client.top) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

    const Gdiplus::Rect bounds(client.left, client.top,
        client.right - client.left, client.bottom - client.top);
    Gdiplus::LinearGradientBrush background(bounds,
        canvasGpColor(RGB(244, 245, 247)),
        canvasGpColor(RGB(235, 238, 242)),
        Gdiplus::LinearGradientModeVertical);
    graphics.FillRectangle(&background, bounds);

    // The one-pixel highlight under the tabs separates the chrome from the
    // work surface while keeping the canvas light and visually open.
    Gdiplus::Pen topLine(canvasGpColor(RGB(255, 255, 255), 190), 1.0F);
    graphics.DrawLine(&topLine, static_cast<float>(client.left),
        static_cast<float>(client.top), static_cast<float>(client.right),
        static_cast<float>(client.top));
}

void paintModernPageShadow(HDC dc, const RECT& pageRect, const RECT& client) {
    if (!dc || pageRect.right <= pageRect.left || pageRect.bottom <= pageRect.top) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetClip(Gdiplus::Rect(client.left, client.top,
        client.right - client.left, client.bottom - client.top));

    const int radius = scaleDip(4);
    RECT outer = pageRect;
    InflateRect(&outer, scaleDip(3), scaleDip(3));
    OffsetRect(&outer, 0, scaleDip(5));
    fillCanvasRoundedRect(graphics, outer, RGB(80, 88, 100), 20, radius + scaleDip(2));

    RECT middle = pageRect;
    InflateRect(&middle, scaleDip(1), scaleDip(1));
    OffsetRect(&middle, 0, scaleDip(3));
    fillCanvasRoundedRect(graphics, middle, RGB(72, 80, 92), 28, radius + scaleDip(1));

    RECT nearShadow = pageRect;
    OffsetRect(&nearShadow, 0, scaleDip(1));
    fillCanvasRoundedRect(graphics, nearShadow, RGB(68, 75, 86), 34, radius);
}

void paintModernPageBorder(HDC dc, const RECT& pageRect, const RECT& client) {
    if (!dc || pageRect.right <= pageRect.left || pageRect.bottom <= pageRect.top) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetClip(Gdiplus::Rect(client.left, client.top,
        client.right - client.left, client.bottom - client.top));
    Gdiplus::Pen border(canvasGpColor(RGB(205, 209, 216), 230), 1.0F);
    const int pageWidth = (std::max)(
        1, static_cast<int>(pageRect.right - pageRect.left) - 1);
    const int pageHeight = (std::max)(
        1, static_cast<int>(pageRect.bottom - pageRect.top) - 1);
    const Gdiplus::RectF borderRect(
        static_cast<Gdiplus::REAL>(pageRect.left) + 0.5F,
        static_cast<Gdiplus::REAL>(pageRect.top) + 0.5F,
        static_cast<Gdiplus::REAL>(pageWidth),
        static_cast<Gdiplus::REAL>(pageHeight));
    graphics.DrawRectangle(&border, borderRect);
}

class CanvasBackBuffer final {
public:
    ~CanvasBackBuffer() { Reset(); }

    bool Ensure(HDC reference, const int width, const int height) {
        if (dc_ && bitmap_ && width_ == width && height_ == height) return true;
        Reset();
        if (!reference || width <= 0 || height <= 0) return false;

        dc_ = CreateCompatibleDC(reference);
        if (!dc_) return false;
        bitmap_ = CreateCompatibleBitmap(reference, width, height);
        if (!bitmap_) {
            DeleteDC(dc_);
            dc_ = nullptr;
            return false;
        }
        previousBitmap_ = SelectObject(dc_, bitmap_);
        if (!previousBitmap_ || previousBitmap_ == HGDI_ERROR) {
            DeleteObject(bitmap_);
            DeleteDC(dc_);
            bitmap_ = nullptr;
            dc_ = nullptr;
            previousBitmap_ = nullptr;
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

    // During a live splitter/window resize, presenting the already composed
    // viewport is dramatically cheaper than running StretchDIBits for the PDF
    // page on every mouse move.  Resize the cached frame itself and replace it
    // with a fully composed frame once the resize operation ends.
    bool PresentResizedSnapshot(HDC target, const int width, const int height) {
        if (!target || !dc_ || !bitmap_ || width_ <= 0 || height_ <= 0 ||
            width <= 0 || height <= 0) {
            return false;
        }
        if (width == width_ && height == height_) {
            return BitBlt(target, 0, 0, width, height, dc_, 0, 0, SRCCOPY) != FALSE;
        }

        HDC nextDc = CreateCompatibleDC(target);
        if (!nextDc) return false;
        HBITMAP nextBitmap = CreateCompatibleBitmap(target, width, height);
        if (!nextBitmap) {
            DeleteDC(nextDc);
            return false;
        }
        HGDIOBJ nextPrevious = SelectObject(nextDc, nextBitmap);
        if (!nextPrevious || nextPrevious == HGDI_ERROR) {
            DeleteObject(nextBitmap);
            DeleteDC(nextDc);
            return false;
        }

        SetStretchBltMode(nextDc, COLORONCOLOR);
        const BOOL copied = StretchBlt(nextDc, 0, 0, width, height,
            dc_, 0, 0, width_, height_, SRCCOPY);
        if (!copied) {
            SelectObject(nextDc, nextPrevious);
            DeleteObject(nextBitmap);
            DeleteDC(nextDc);
            return false;
        }

        if (dc_ && previousBitmap_) SelectObject(dc_, previousBitmap_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
        dc_ = nextDc;
        bitmap_ = nextBitmap;
        previousBitmap_ = nextPrevious;
        width_ = width;
        height_ = height;
        return BitBlt(target, 0, 0, width, height, dc_, 0, 0, SRCCOPY) != FALSE;
    }

    void Reset() noexcept {
        if (dc_ && previousBitmap_) SelectObject(dc_, previousBitmap_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
        dc_ = nullptr;
        bitmap_ = nullptr;
        previousBitmap_ = nullptr;
        width_ = 0;
        height_ = 0;
    }

    [[nodiscard]] HDC Dc() const noexcept { return dc_; }

private:
    HDC dc_{};
    HBITMAP bitmap_{};
    HGDIOBJ previousBitmap_{};
    int width_{};
    int height_{};
};

CanvasBackBuffer canvasBackBuffer;

int sidebarSplitterWidthPixels() {
    return sidebarVisible ? scaleDip(kSidebarSplitterWidthDip) : 0;
}

int effectiveSidebarWidthPixels(const int clientWidth) {
    if (!sidebarVisible || clientWidth <= 0) return 0;

    const int splitter = sidebarSplitterWidthPixels();
    const int availableForSidebar = (std::max)(
        0, clientWidth - splitter - scaleDip(kCanvasMinWidthDip));
    const int maximum = (std::min)(scaleDip(kSidebarMaxWidthDip), availableForSidebar);
    const int minimum = (std::min)(scaleDip(kSidebarMinWidthDip), maximum);
    return std::clamp(scaleDip(sidebarWidthDip), minimum, maximum);
}

int effectiveToolsWidthPixels(const int clientWidth) {
    if (!toolsVisible || clientWidth <= 0) return 0;
    const int sidebar = effectiveSidebarWidthPixels(clientWidth);
    const int splitter = sidebarSplitterWidthPixels();
    const int minimumCanvas = scaleDip(kCanvasMinWidthDip);
    const int available = (std::max)(0,
        clientWidth - sidebar - splitter - minimumCanvas);
    const int maximum = (std::min)(scaleDip(kToolsMaxWidthDip), available);
    const int minimum = (std::min)(scaleDip(kToolsMinWidthDip), maximum);
    return std::clamp(scaleDip(toolsWidthDip), minimum, maximum);
}


struct CommentHit final {
    int page{-1};
    std::uint32_t objectNumber{};
};

bool pageDisplayBounds(const int page, const RECT& client,
                       int& left, int& top, int& width, int& height) {
    left = top = width = height = 0;
    if (!document || page < 0 || page >= pageCount) return false;

    if (page == pixelPage && pixelWidth > 0 && pixelHeight > 0 &&
        std::abs(pixelZoom - zoom) < 1.0e-9 && pixelDpi == currentDpi) {
        width = pixelWidth;
        height = pixelHeight;
    } else if (const PageBitmap* cached = pageCache.Peek(page, zoom, currentDpi);
               cached && cached->IsValid()) {
        width = cached->width;
        height = cached->height;
    } else {
        const double renderScale = zoom * static_cast<double>(currentDpi) /
            static_cast<double>(USER_DEFAULT_SCREEN_DPI);
        if (!document->PageSize(page, renderScale, width, height)) return false;
    }
    if (width <= 0 || height <= 0) return false;

    if (pageLayoutMode == PageLayoutMode::ContinuousNavigation) {
        if (page >= static_cast<int>(pagePixelOffsets.size())) return false;
        top = pagePixelOffsets[static_cast<std::size_t>(page)] - scrollY;
    } else {
        if (page != pageIndex) return false;
        top = scaleDip(12) - scrollY;
    }
    left = pageLeft(client, width) - scrollX;
    return true;
}

bool annotationClientRect(const CommentItem& comment, const int page,
                          const RECT& client, RECT& result) {
    result = {};
    if (!document) return false;

    int pageLeftPixels = 0;
    int pageTopPixels = 0;
    int renderWidth = 0;
    int renderHeight = 0;
    if (!pageDisplayBounds(page, client, pageLeftPixels, pageTopPixels,
                           renderWidth, renderHeight)) {
        return false;
    }

    PageCoordinateInfo geometry{};
    if (!document->PageCoordinates(page, geometry)) return false;
    const double boxWidth = geometry.right - geometry.left;
    const double boxHeight = geometry.top - geometry.bottom;
    if (boxWidth <= 0.0 || boxHeight <= 0.0) return false;

    const int rotation = ((geometry.rotation % 360) + 360) % 360;
    const double displayWidthPoints = (rotation == 90 || rotation == 270)
        ? boxHeight : boxWidth;
    const double displayHeightPoints = (rotation == 90 || rotation == 270)
        ? boxWidth : boxHeight;
    if (displayWidthPoints <= 0.0 || displayHeightPoints <= 0.0) return false;

    auto mapPoint = [&](const double x, const double y) -> POINT {
        const double u = x - geometry.left;
        const double v = y - geometry.bottom;
        double displayX = 0.0;
        double displayY = 0.0;
        switch (rotation) {
        case 90:
            displayX = v;
            displayY = u;
            break;
        case 180:
            displayX = boxWidth - u;
            displayY = v;
            break;
        case 270:
            displayX = boxHeight - v;
            displayY = boxWidth - u;
            break;
        default:
            displayX = u;
            displayY = boxHeight - v;
            break;
        }
        return POINT{
            static_cast<LONG>(std::lround(pageLeftPixels +
                (displayX / displayWidthPoints) * renderWidth)),
            static_cast<LONG>(std::lround(pageTopPixels +
                (displayY / displayHeightPoints) * renderHeight))};
    };

    const POINT corners[] = {
        mapPoint(comment.left, comment.bottom),
        mapPoint(comment.left, comment.top),
        mapPoint(comment.right, comment.bottom),
        mapPoint(comment.right, comment.top)
    };
    LONG minX = corners[0].x;
    LONG maxX = corners[0].x;
    LONG minY = corners[0].y;
    LONG maxY = corners[0].y;
    for (const auto& corner : corners) {
        minX = (std::min)(minX, corner.x);
        maxX = (std::max)(maxX, corner.x);
        minY = (std::min)(minY, corner.y);
        maxY = (std::max)(maxY, corner.y);
    }
    result = RECT{minX, minY, maxX, maxY};

    // AutoCAD SHX annotations can have a very tight or even almost point-like
    // /Rect. Give them an Acrobat-like click target without changing visuals.
    const int padding = scaleDip(8);
    if (result.right - result.left < scaleDip(12)) {
        const LONG center = (result.left + result.right) / 2;
        result.left = center - scaleDip(6);
        result.right = center + scaleDip(6);
    }
    if (result.bottom - result.top < scaleDip(12)) {
        const LONG center = (result.top + result.bottom) / 2;
        result.top = center - scaleDip(6);
        result.bottom = center + scaleDip(6);
    }
    InflateRect(&result, padding, padding);
    return true;
}

std::optional<CommentHit> hitTestCommentAtPoint(const POINT point, const RECT& client) {
    if (!document || pageCount <= 0) return std::nullopt;

    int candidatePage = pageIndex;
    if (pageLayoutMode == PageLayoutMode::ContinuousNavigation && !pagePixelOffsets.empty()) {
        const int documentY = point.y + scrollY;
        candidatePage = pageAtScrollOffset(documentY);
        if (candidatePage < 0 || candidatePage >= pageCount ||
            candidatePage >= static_cast<int>(pagePixelOffsets.size()) ||
            candidatePage >= static_cast<int>(pagePixelHeights.size())) {
            return std::nullopt;
        }
        const int pageTopDocument = pagePixelOffsets[static_cast<std::size_t>(candidatePage)];
        const int pageBottomDocument = pageTopDocument +
            pagePixelHeights[static_cast<std::size_t>(candidatePage)];
        if (documentY < pageTopDocument || documentY > pageBottomDocument) {
            return std::nullopt; // click was in the inter-page gap
        }
    }

    const auto comments = document->CommentsDetailed(candidatePage);
    std::optional<CommentHit> nearest;
    long long nearestDistanceSquared = LLONG_MAX;
    const long long snapDistance = static_cast<long long>(scaleDip(18));
    const long long snapDistanceSquared = snapDistance * snapDistance;
    for (const auto& comment : comments) {
        RECT rect{};
        if (!annotationClientRect(comment, candidatePage, client, rect)) continue;
        if (PtInRect(&rect, point)) {
            return CommentHit{candidatePage, comment.objectNumber};
        }

        const LONG nearestX = std::clamp(point.x, rect.left, rect.right);
        const LONG nearestY = std::clamp(point.y, rect.top, rect.bottom);
        const long long dx = static_cast<long long>(point.x) - nearestX;
        const long long dy = static_cast<long long>(point.y) - nearestY;
        const long long distanceSquared = dx * dx + dy * dy;
        if (distanceSquared < nearestDistanceSquared) {
            nearestDistanceSquared = distanceSquared;
            nearest = CommentHit{candidatePage, comment.objectNumber};
        }
    }
    if (nearest && nearestDistanceSquared <= snapDistanceSquared) return nearest;
    return std::nullopt;
}

bool pointHitsSidebarSplitter(const HWND window, const POINT point) {
    if (!sidebarVisible || !window) return false;
    RECT client{};
    GetClientRect(window, &client);
    const int sidebar = effectiveSidebarWidthPixels(client.right);
    const int splitter = sidebarSplitterWidthPixels();
    if (splitter <= 0) return false;

    const int ribbon = scaleDip(PdfPP::ModernWin32::Layout::ribbonHeight);
    const int status = scaleDip(PdfPP::ModernWin32::Layout::statusHeight);
    const int clientBottom = static_cast<int>(client.bottom);
    const int bottom = (std::max)(ribbon, clientBottom - status);
    return point.x >= sidebar && point.x < sidebar + splitter &&
        point.y >= ribbon && point.y < bottom;
}

void setWindowBounds(const HWND child, const int x, const int y,
                     const int width, const int height) {
    if (!child) return;
    SetWindowPos(child, nullptr, x, y, (std::max)(0, width), (std::max)(0, height),
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

HDWP deferWindowBounds(HDWP batch, const HWND child, const int x, const int y,
                       const int width, const int height) {
    if (!child) return batch;
    if (!batch) {
        setWindowBounds(child, x, y, width, height);
        return nullptr;
    }
    return DeferWindowPos(batch, child, nullptr,
        x, y, (std::max)(0, width), (std::max)(0, height),
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

RECT readerContentBand(const int width, const int height) {
    const int ribbon = scaleDip(PdfPP::ModernWin32::Layout::ribbonHeight);
    const int status = scaleDip(PdfPP::ModernWin32::Layout::statusHeight);
    return RECT{ 0, ribbon, (std::max)(0, width),
        (std::max)(ribbon, height - status) };
}

// Resize the TOC/canvas as one transaction.  The previous fast path used
// SWP_NOREDRAW and then repainted only the new child rectangles.  Pixels from
// the old rounded sidebar bounds therefore remained in the parent client area,
// producing the stacked "saved image" trails seen while resizing.  Defer the
// child moves, invalidate the exposed parent band before the move, and present
// one complete frame afterwards instead.
void updateSidebarLayoutFast(const HWND window, const int width, const int height) {
    if (!window) return;

    const int tabBarHeight = tabBar ? scaleDip(kTabBarHeightDip) : 0;
    const int ribbon = scaleDip(PdfPP::ModernWin32::Layout::ribbonHeight);
    const int status = scaleDip(PdfPP::ModernWin32::Layout::statusHeight);
    const int bodyHeight = (std::max)(0, height - ribbon - tabBarHeight - status);
    const int sidebar = effectiveSidebarWidthPixels(width);
    const int splitterWidth = sidebarSplitterWidthPixels();
    const int tools = effectiveToolsWidthPixels(width);
    const int contentLeft = sidebar + splitterWidth;
    const int contentRight = (std::max)(contentLeft, width - tools);
    const int canvasWidth = (std::max)(0, contentRight - contentLeft);

    const int sidebarOuter = scaleDip(6);
    const int sidebarInner = scaleDip(8);
    const int sidebarHeader = scaleDip(36);
    const int sidebarPanelX = sidebarOuter;
    const int sidebarPanelY = ribbon + sidebarOuter;
    const int sidebarPanelWidth = (std::max)(0, sidebar - sidebarOuter * 2);
    const int sidebarPanelHeight = (std::max)(0,
        bodyHeight + tabBarHeight - sidebarOuter * 2);
    const int titleLeft = sidebarInner + scaleDip(4);
    const int closeSize = scaleDip(22);
    const int closeRight = sidebarPanelWidth - sidebarInner;
    const int listLeft = sidebarInner;
    const int listTop = sidebarHeader;
    const int listBottom = sidebarPanelHeight - sidebarInner;

    const RECT dirty = readerContentBand(width, height);
    InvalidateRect(window, &dirty, TRUE);

    // Windows in the first batch share mainWindow as their parent.  The TOC
    // title/list/close button are children of sidebarPanel and therefore use
    // a second batch with panel-relative coordinates.  This removes all
    // overlapping-sibling painting from the TOC.
    const int toolsOuter = scaleDip(6);
    const int toolsInner = scaleDip(10);
    const int toolsHeader = scaleDip(32);
    const int toolsSearchHeight = scaleDip(28);
    const int toolsPanelX = contentRight + toolsOuter;
    const int toolsPanelY = ribbon + toolsOuter;
    const int toolsPanelWidth = (std::max)(0, tools - toolsOuter * 2);
    const int toolsPanelHeight = (std::max)(0, bodyHeight + tabBarHeight - toolsOuter * 2);

    HDWP batch = BeginDeferWindowPos(4);
    batch = deferWindowBounds(batch, tabBar, contentLeft, ribbon,
        (std::max)(0, contentRight - contentLeft), tabBarHeight);
    batch = deferWindowBounds(batch, sidebarPanel, sidebarPanelX, sidebarPanelY,
        sidebarPanelWidth, sidebarPanelHeight);
    batch = deferWindowBounds(batch, canvas, contentLeft, ribbon + tabBarHeight,
        canvasWidth, bodyHeight);
    batch = deferWindowBounds(batch, toolsPanel, toolsPanelX, toolsPanelY,
        toolsPanelWidth, toolsPanelHeight);
    if (batch) EndDeferWindowPos(batch);

    HDWP sidebarBatch = BeginDeferWindowPos(3);
    sidebarBatch = deferWindowBounds(sidebarBatch, sidebarTitle, titleLeft, 0,
        (std::max)(0, closeRight - closeSize - scaleDip(8) - titleLeft), sidebarHeader);
    sidebarBatch = deferWindowBounds(sidebarBatch, bookmarkCloseButton,
        (std::max)(titleLeft, closeRight - closeSize),
        (sidebarHeader - closeSize) / 2, closeSize, closeSize);
    sidebarBatch = deferWindowBounds(sidebarBatch, pageList, listLeft, listTop,
        (std::max)(0, sidebarPanelWidth - sidebarInner * 2),
        (std::max)(0, listBottom - listTop));
    if (sidebarBatch) EndDeferWindowPos(sidebarBatch);

    HDWP toolsBatch = BeginDeferWindowPos(3);
    toolsBatch = deferWindowBounds(toolsBatch, toolsTitle, toolsInner, 0,
        (std::max)(0, toolsPanelWidth - toolsInner * 2), toolsHeader);
    toolsBatch = deferWindowBounds(toolsBatch, toolsSearchEdit, toolsInner, toolsHeader,
        (std::max)(0, toolsPanelWidth - toolsInner * 2), toolsSearchHeight);
    toolsBatch = deferWindowBounds(toolsBatch, toolsTree, toolsInner,
        toolsHeader + toolsSearchHeight + scaleDip(8),
        (std::max)(0, toolsPanelWidth - toolsInner * 2),
        (std::max)(0, toolsPanelHeight - toolsHeader - toolsSearchHeight - toolsInner - scaleDip(8)));
    if (toolsBatch) EndDeferWindowPos(toolsBatch);

    ShowWindow(toolsPanel, toolsVisible ? SW_SHOW : SW_HIDE);

    // Keep the floating find bar attached to the right edge of the canvas.
    if (findPanel) {
        const int availablePanelWidth = (std::max)(scaleDip(120), canvasWidth - scaleDip(24));
        const int panelWidth = (std::min)(scaleDip(348), availablePanelWidth);
        const int panelHeight = scaleDip(50);
        const int panelLeft = (std::max)(scaleDip(8),
            canvasWidth - panelWidth - scaleDip(14));
        setWindowBounds(findPanel, panelLeft, scaleDip(8), panelWidth, panelHeight);
    }

    // The panel owns the TOC controls, so paint it as one hierarchy.  Its
    // WS_CLIPCHILDREN style guarantees that the rounded background cannot
    // overwrite TreeView text after the TreeView has painted.
    if (sidebarPanel && sidebarVisible) {
        RedrawWindow(sidebarPanel, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    if (toolsPanel && toolsVisible) {
        RedrawWindow(toolsPanel, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }

    // One synchronous present keeps dragging visually attached to the cursor
    // and clears regions exposed by the old child bounds.
    RedrawWindow(window, &dirty, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

void resizeSidebarFromClientX(const HWND window, const int clientX,
                              const bool finalLayout = false) {
    if (!window || currentDpi == 0) return;
    RECT client{};
    GetClientRect(window, &client);

    const int splitter = scaleDip(kSidebarSplitterWidthDip);
    const int clientRight = static_cast<int>(client.right);
    const int maximumPixels = (std::max)(
        0, clientRight - splitter - scaleDip(kCanvasMinWidthDip));
    const int minimumPixels = (std::min)(scaleDip(kSidebarMinWidthDip), maximumPixels);
    const int clampedPixels = std::clamp(clientX, minimumPixels, maximumPixels);
    const int nextSidebarWidthDip = std::clamp(
        MulDiv(clampedPixels, USER_DEFAULT_SCREEN_DPI, static_cast<int>(currentDpi)),
        0, kSidebarMaxWidthDip);

    if (nextSidebarWidthDip == sidebarWidthDip && !finalLayout) return;
    sidebarWidthDip = nextSidebarWidthDip;

    if (finalLayout) {
        // One complete layout at drag end updates rounded regions and any
        // dependent metrics not needed by the live-resize fast path.
        updateLayout(static_cast<int>(client.right), static_cast<int>(client.bottom));
        RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);
    } else {
        updateSidebarLayoutFast(window,
            static_cast<int>(client.right), static_cast<int>(client.bottom));
    }
}

} // namespace

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
    const int tabBarHeight = tabBar ? scaleDip(kTabBarHeightDip) : 0;
    const int ribbon = scaleDip(PdfPP::ModernWin32::Layout::ribbonHeight);
    const int status = scaleDip(PdfPP::ModernWin32::Layout::statusHeight);
    const int bodyHeight = std::max(0, height - ribbon - tabBarHeight - status);
    const int sidebar = effectiveSidebarWidthPixels(width);
    const int splitterWidth = sidebarSplitterWidthPixels();
    const int tools = effectiveToolsWidthPixels(width);
    const int contentLeft = sidebar + splitterWidth;
    const int contentRight = (std::max)(contentLeft, width - tools);
    // Repaint the parent surface once after all child bounds have changed.
    // This explicitly invalidates areas uncovered by shrinking/moving children.
    InvalidateRect(mainWindow, nullptr, TRUE);
    HDWP mainBatch = BeginDeferWindowPos(28);
    const int controlY = scaleDip((PdfPP::ModernWin32::Layout::ribbonHeight - PdfPP::ModernWin32::Layout::controlHeight) / 2);
    const int controlHeight = scaleDip(PdfPP::ModernWin32::Layout::controlHeight);
    // The tab strip sits above the page canvas and starts at the splitter
    // (right edge of the Table of Contents sidebar), so tabs never draw
    // over the sidebar.
    if (tabBar) {
        mainBatch = deferWindowBounds(mainBatch, tabBar, contentLeft, ribbon,
            std::max(0, contentRight - contentLeft), tabBarHeight);
    }
    // ---- Compact toolbar, ordered left to right: ----
    // Open | Print || Prev Page Next || Zoom out % Zoom in Fit || Select Hand
    // Find is intentionally a transient floating panel instead of a permanent
    // ribbon control, matching browser and Acrobat PDF viewers.
    const int dOpen = scaleDip(50) + scaleDip(2);
    const int dPrint = scaleDip(48) + scaleDip(12);

    auto measureTextWidth = [](const HWND window, const std::wstring& value) -> int {
        HDC dc = GetDC(window);
        if (!dc) return 0;
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
        SIZE size{};
        GetTextExtentPoint32W(dc, value.c_str(), static_cast<int>(value.size()), &size);
        if (oldFont) SelectObject(dc, oldFont);
        ReleaseDC(window, dc);
        return size.cx;
    };

    const std::wstring currentPageText = std::to_wstring((std::max)(1, pageIndex + 1));
    const std::wstring totalPagesText = std::to_wstring((std::max)(1, pageCount));
    const int pageCaptionWidth = measureTextWidth(pageCaption, L"Page:") + scaleDip(4);
    // Size the edit to the value it actually contains. The old layout used the
    // total-page digit count plus generous padding, which made a one-digit page
    // number look like a large empty field.
    const int pageEditWidth = std::clamp(
        measureTextWidth(pageEdit, currentPageText) + scaleDip(12),
        scaleDip(28), scaleDip(52));
    const int pageLabelWidth = (std::max)(scaleDip(20),
        measureTextWidth(pageLabel, L"/ " + totalPagesText) + scaleDip(5));
    const int pageTextHeight = scaleDip(20);
    const int pageEditHeight = scaleDip(24);
    const int pageGroupCenterY = controlY + controlHeight / 2;
    const int pageTextTop = pageGroupCenterY - pageTextHeight / 2;
    // Position the single-line native EDIT by center line so it visually aligns
    // with the surrounding labels and arrow buttons.
    const int pageEditTop = pageGroupCenterY - pageEditHeight / 2;
    const int pageGroupGap = scaleDip(4);

    int left = scaleDip(8);
    mainBatch = deferWindowBounds(mainBatch, openButton, left, controlY, scaleDip(50), controlHeight);
    left += dOpen;
    mainBatch = deferWindowBounds(mainBatch, printButton, left, controlY, scaleDip(48), controlHeight);
    left += dPrint;
    ribbonSep1 = left - scaleDip(6);
    mainBatch = deferWindowBounds(mainBatch, previousButton, left, controlY, scaleDip(30), controlHeight);
    left += scaleDip(30) + pageGroupGap;
    mainBatch = deferWindowBounds(mainBatch, pageCaption, left, pageTextTop, pageCaptionWidth, pageTextHeight);
    left += pageCaptionWidth + pageGroupGap;
    mainBatch = deferWindowBounds(mainBatch, pageEdit, left, pageEditTop, pageEditWidth, pageEditHeight);
    left += pageEditWidth + pageGroupGap;
    mainBatch = deferWindowBounds(mainBatch, pageLabel, left, pageTextTop, pageLabelWidth, pageTextHeight);
    left += pageLabelWidth + pageGroupGap;
    mainBatch = deferWindowBounds(mainBatch, nextButton, left, controlY, scaleDip(30), controlHeight);
    left += scaleDip(30) + scaleDip(10);
    ribbonSep2 = left - scaleDip(6);
    mainBatch = deferWindowBounds(mainBatch, zoomOutButton, left, controlY, scaleDip(34), controlHeight);
    left += scaleDip(34) + scaleDip(2);
    mainBatch = deferWindowBounds(mainBatch, zoomLabel, left, controlY + scaleDip(4), scaleDip(46), scaleDip(18));
    left += scaleDip(46) + scaleDip(2);
    mainBatch = deferWindowBounds(mainBatch, zoomInButton, left, controlY, scaleDip(34), controlHeight);
    left += scaleDip(34) + scaleDip(2);
    mainBatch = deferWindowBounds(mainBatch, fitButton, left, controlY, scaleDip(50), controlHeight);
    left += scaleDip(50) + scaleDip(12);
    ribbonSep3 = left - scaleDip(6);
    mainBatch = deferWindowBounds(mainBatch, selectButton, left, controlY, scaleDip(56), controlHeight);
    left += scaleDip(56) + scaleDip(4);
    mainBatch = deferWindowBounds(mainBatch, handButton, left, controlY, scaleDip(50), controlHeight);
    const int sidebarToggleLeft = (std::max)(left + scaleDip(68), width - scaleDip(50));
    const int toolsToggleLeft = (std::max)(left + scaleDip(8), sidebarToggleLeft - scaleDip(60));
    mainBatch = deferWindowBounds(mainBatch, toolsToggleButton,
        toolsToggleLeft, controlY, scaleDip(54), controlHeight);
    mainBatch = deferWindowBounds(mainBatch, sidebarToggleButton,
        sidebarToggleLeft, controlY, scaleDip(38), controlHeight);
    const int sidebarOuter = scaleDip(6);
    const int sidebarInner = scaleDip(8);
    const int sidebarHeader = scaleDip(36);
    const int sidebarPanelX = sidebarOuter;
    const int sidebarPanelY = ribbon + sidebarOuter;
    const int sidebarPanelWidth = (std::max)(0, sidebar - sidebarOuter * 2);
    const int sidebarPanelHeight = (std::max)(0,
        bodyHeight + tabBarHeight - sidebarOuter * 2);
    if (sidebarPanel) {
        mainBatch = deferWindowBounds(mainBatch, sidebarPanel, sidebarPanelX, sidebarPanelY,
            sidebarPanelWidth, sidebarPanelHeight);
    }

    // TOC controls are children of sidebarPanel and are laid out in panel
    // coordinates after the main-window batch completes.
    const int titleLeft = sidebarInner + scaleDip(4);
    const int closeSize = scaleDip(22);
    const int closeRight = sidebarPanelWidth - sidebarInner;
    const int listLeft = sidebarInner;
    const int listTop = sidebarHeader;
    const int listBottom = sidebarPanelHeight - sidebarInner;

    const int toolsOuter = scaleDip(6);
    const int toolsInner = scaleDip(10);
    const int toolsHeader = scaleDip(32);
    const int toolsSearchHeight = scaleDip(28);
    const int toolsPanelX = contentRight + toolsOuter;
    const int toolsPanelY = ribbon + toolsOuter;
    const int toolsPanelWidth = (std::max)(0, tools - toolsOuter * 2);
    const int toolsPanelHeight = (std::max)(0, bodyHeight + tabBarHeight - toolsOuter * 2);

    const int canvasTop = ribbon + tabBarHeight;
    const int canvasWidth = std::max(0, contentRight - contentLeft);
    mainBatch = deferWindowBounds(mainBatch, toolsPanel, toolsPanelX, toolsPanelY,
        toolsPanelWidth, toolsPanelHeight);
    mainBatch = deferWindowBounds(mainBatch, canvas, contentLeft, canvasTop, canvasWidth, bodyHeight);
    mainBatch = deferWindowBounds(mainBatch, statusLabel, scaleDip(16),
        height - status + scaleDip(4), std::max(0, width - scaleDip(32)), scaleDip(20));

    if (mainBatch) EndDeferWindowPos(mainBatch);

    HDWP sidebarBatch = BeginDeferWindowPos(3);
    sidebarBatch = deferWindowBounds(sidebarBatch, sidebarTitle, titleLeft, 0,
        (std::max)(0, closeRight - closeSize - scaleDip(8) - titleLeft),
        sidebarHeader);
    sidebarBatch = deferWindowBounds(sidebarBatch, bookmarkCloseButton,
        (std::max)(titleLeft, closeRight - closeSize),
        (sidebarHeader - closeSize) / 2, closeSize, closeSize);
    sidebarBatch = deferWindowBounds(sidebarBatch, pageList, listLeft, listTop,
        (std::max)(0, sidebarPanelWidth - sidebarInner * 2),
        (std::max)(0, listBottom - listTop));
    if (sidebarBatch) EndDeferWindowPos(sidebarBatch);

    HDWP toolsBatch = BeginDeferWindowPos(3);
    toolsBatch = deferWindowBounds(toolsBatch, toolsTitle, toolsInner, 0,
        (std::max)(0, toolsPanelWidth - toolsInner * 2), toolsHeader);
    toolsBatch = deferWindowBounds(toolsBatch, toolsSearchEdit, toolsInner, toolsHeader,
        (std::max)(0, toolsPanelWidth - toolsInner * 2), toolsSearchHeight);
    toolsBatch = deferWindowBounds(toolsBatch, toolsTree, toolsInner,
        toolsHeader + toolsSearchHeight + scaleDip(8),
        (std::max)(0, toolsPanelWidth - toolsInner * 2),
        (std::max)(0, toolsPanelHeight - toolsHeader - toolsSearchHeight - toolsInner - scaleDip(8)));
    if (toolsBatch) EndDeferWindowPos(toolsBatch);

    centerPageEditFormattingRect();

    // Hiding/showing the container is sufficient; child visibility is kept
    // stable and no z-order manipulation is needed.
    ShowWindow(sidebarPanel, sidebarVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(toolsPanel, toolsVisible ? SW_SHOW : SW_HIDE);

    // Acrobat-style floating panel. It is a child of the canvas and therefore
    // overlays the rendered page without affecting document layout.
    const int availablePanelWidth = std::max(scaleDip(120), canvasWidth - scaleDip(24));
    const int panelWidth = std::min(scaleDip(348), availablePanelWidth);
    const int panelHeight = scaleDip(50);
    const int panelLeft = std::max(scaleDip(8), canvasWidth - panelWidth - scaleDip(14));
    const int panelTop = scaleDip(8);
    setWindowBounds(findPanel, panelLeft, panelTop, panelWidth, panelHeight);
    if (findPanel && !readerWindowSizing) {
        const int radius = scaleDip(7);
        const HRGN region = CreateRoundRectRgn(
            0, 0, panelWidth + 1, panelHeight + 1, radius, radius);
        if (region && SetWindowRgn(findPanel, region, TRUE) == 0) {
            DeleteObject(region);
        }
    }

    // Child coordinates are relative to the find panel, not the canvas.
    const int contentInset = scaleDip(4);
    const int accentWidth = scaleDip(14);
    const int buttonSize = scaleDip(34);
    const int innerTop = scaleDip(7);
    const int closeLeft = panelWidth - contentInset - buttonSize;
    const int optionsLeft = closeLeft - buttonSize - scaleDip(2);
    const int editLeft = contentInset + accentWidth + scaleDip(3);
    HDWP findBatch = BeginDeferWindowPos(3);
    findBatch = deferWindowBounds(findBatch, searchEdit, editLeft, innerTop + scaleDip(1),
        std::max(scaleDip(90), optionsLeft - editLeft - scaleDip(4)),
        buttonSize - scaleDip(4));
    findBatch = deferWindowBounds(findBatch, findButton, optionsLeft, innerTop, buttonSize, buttonSize);
    findBatch = deferWindowBounds(findBatch, findCloseButton, closeLeft, innerTop, buttonSize, buttonSize);
    if (findBatch) EndDeferWindowPos(findBatch);
    if (findPanel) SetWindowPos(findPanel, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // EndDeferWindowPos delivers WM_SIZE to the canvas once, so its scrollbar
    // geometry is already current. Paint the TOC container hierarchy first,
    // then refresh the reader chrome. This keeps the TreeView visible without
    // asking an unrelated main-window redraw to recursively repaint it.
    if (sidebarPanel && sidebarVisible) {
        RedrawWindow(sidebarPanel, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    if (toolsPanel && toolsVisible) {
        RedrawWindow(toolsPanel, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    RedrawWindow(mainWindow, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

void showFindPanel(const bool show) {
    findPanelVisible = show && document != nullptr;
    if (findPanel) ShowWindow(findPanel, findPanelVisible ? SW_SHOW : SW_HIDE);
    if (findPanelVisible) {
        RECT client{};
        GetClientRect(mainWindow, &client);
        updateLayout(static_cast<int>(client.right), static_cast<int>(client.bottom));
        SetWindowPos(findPanel, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetFocus(searchEdit);
        SendMessageW(searchEdit, EM_SETSEL, 0, -1);
        InvalidateRect(findPanel, nullptr, TRUE);
    } else if (GetFocus() == searchEdit || GetFocus() == findButton ||
               GetFocus() == findCloseButton) {
        SetFocus(canvas);
    }
}

void showFindOptionsMenu() {
    if (!findButton) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, ID_FIND_SCOPE_WHOLE_PAGE, L"Whole Page");
    AppendMenuW(menu, MF_STRING, ID_FIND_SCOPE_CURRENT_PAGE, L"Current Page");
    CheckMenuRadioItem(menu, ID_FIND_SCOPE_WHOLE_PAGE, ID_FIND_SCOPE_CURRENT_PAGE,
        findScope == FindScope::WholePage
            ? ID_FIND_SCOPE_WHOLE_PAGE : ID_FIND_SCOPE_CURRENT_PAGE,
        MF_BYCOMMAND);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_FIND_CASE_SENSITIVE, L"Case Sensitive");
    AppendMenuW(menu, MF_STRING, ID_FIND_CASE_INSENSITIVE, L"Case Insensitive");
    CheckMenuRadioItem(menu, ID_FIND_CASE_SENSITIVE, ID_FIND_CASE_INSENSITIVE,
        findCaseMode == FindCaseMode::CaseSensitive
            ? ID_FIND_CASE_SENSITIVE : ID_FIND_CASE_INSENSITIVE,
        MF_BYCOMMAND);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_FIND_MODE_NORMAL, L"Normal");
    AppendMenuW(menu, MF_STRING, ID_FIND_MODE_REGEX, L"Regular Expression");
    CheckMenuRadioItem(menu, ID_FIND_MODE_NORMAL, ID_FIND_MODE_REGEX,
        findPatternMode == FindPatternMode::Normal
            ? ID_FIND_MODE_NORMAL : ID_FIND_MODE_REGEX,
        MF_BYCOMMAND);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (findIncludeComments ? MF_CHECKED : 0),
        ID_FIND_INCLUDE_COMMENTS, L"Include Comments");
    AppendMenuW(menu, MF_STRING | (findIncludeBookmarks ? MF_CHECKED : 0),
        ID_FIND_INCLUDE_BOOKMARKS, L"Include Bookmark");

    RECT button{};
    GetWindowRect(findButton, &button);
    TrackPopupMenuEx(menu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        button.right, button.bottom + scaleDip(2), mainWindow, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK findPanelProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        return SendMessageW(mainWindow, WM_COMMAND, wParam, lParam);
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(36, 36, 36));
        SetBkColor(dc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!draw) break;
        const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
        const HBRUSH background = CreateSolidBrush(
            pressed ? RGB(235, 235, 235) : RGB(255, 255, 255));
        FillRect(draw->hDC, &draw->rcItem, background);
        DeleteObject(background);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, RGB(30, 30, 30));
        wchar_t text[16]{};
        GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
        RECT label = draw->rcItem;
        DrawTextW(draw->hDC, text, -1, &label,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);

        // Shadow is kept inside the panel window so the child remains simple
        // and can still be clipped by the canvas like Acrobat's find bar.
        RECT shadow = client;
        shadow.left += scaleDip(3);
        shadow.top += scaleDip(3);
        const HBRUSH shadowBrush = CreateSolidBrush(RGB(190, 190, 190));
        FillRect(dc, &shadow, shadowBrush);
        DeleteObject(shadowBrush);

        RECT body = client;
        body.right -= scaleDip(3);
        body.bottom -= scaleDip(3);
        const HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &body, white);
        DeleteObject(white);
        const HBRUSH border = CreateSolidBrush(RGB(176, 176, 176));
        FrameRect(dc, &body, border);
        DeleteObject(border);

        const HPEN accent = CreatePen(PS_SOLID, std::max(2, scaleDip(3)), RGB(132, 132, 132));
        const HGDIOBJ previousPen = SelectObject(dc, accent);
        MoveToEx(dc, scaleDip(10), scaleDip(11), nullptr);
        LineTo(dc, scaleDip(10), body.bottom - scaleDip(9));
        SelectObject(dc, previousPen);
        DeleteObject(accent);

        if (searchEdit) {
            RECT edit{};
            GetWindowRect(searchEdit, &edit);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&edit), 2);
            const HPEN underline = CreatePen(PS_SOLID, 1, RGB(190, 190, 190));
            const HGDIOBJ old = SelectObject(dc, underline);
            MoveToEx(dc, edit.left + scaleDip(2), edit.bottom + scaleDip(1), nullptr);
            LineTo(dc, edit.right - scaleDip(2), edit.bottom + scaleDip(1));
            SelectObject(dc, old);
            DeleteObject(underline);
        }
        if (findCloseButton) {
            RECT close{};
            GetWindowRect(findCloseButton, &close);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&close), 2);
            const HPEN separator = CreatePen(PS_SOLID, 1, RGB(216, 216, 216));
            const HGDIOBJ old = SelectObject(dc, separator);
            MoveToEx(dc, close.left - scaleDip(2), scaleDip(10), nullptr);
            LineTo(dc, close.left - scaleDip(2), body.bottom - scaleDip(9));
            SelectObject(dc, old);
            DeleteObject(separator);
        }
        EndPaint(window, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        return SendMessageW(mainWindow, WM_COMMAND, wParam, lParam);
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const int clientWidth = std::max(0L, client.right - client.left);
        const int clientHeight = std::max(0L, client.bottom - client.top);
        if (clientWidth <= 0 || clientHeight <= 0) {
            EndPaint(window, &paint);
            return 0;
        }

        // Live resize should be a presentation-only operation. Re-rendering or
        // even re-compositing a high-zoom PDF bitmap for every mouse message is
        // expensive, so scale the last complete viewport until the gesture ends.
        if ((sidebarSplitterDragging || readerWindowSizing) &&
            canvasBackBuffer.PresentResizedSnapshot(dc, clientWidth, clientHeight)) {
            EndPaint(window, &paint);
            return 0;
        }

        if (!canvasBackBuffer.Ensure(dc, clientWidth, clientHeight)) {
            EndPaint(window, &paint);
            return 0;
        }

        // Always compose one complete, internally consistent viewport. The
        // persistent back buffer avoids allocating a new bitmap for every wheel
        // tick, while a single final BitBlt prevents page-boundary frames from
        // exposing a mixture of old and new page positions.
        HDC mem = canvasBackBuffer.Dc();
        paintModernCanvasSurface(mem, client);

        auto drawBitmap = [&](const std::uint8_t* sourcePixels,
                              const int sourceWidth, const int sourceHeight,
                              const int sourceStride, const int left, const int top,
                              const int destinationWidth, const int destinationHeight) {
            if (!sourcePixels || sourceWidth <= 0 || sourceHeight <= 0 || sourceStride <= 0 ||
                destinationWidth <= 0 || destinationHeight <= 0) {
                return;
            }

            // Draw the complete page bitmap. The renderer and layout now use
            // the same DIP dimensions, so clipping to an estimated layout slot
            // is both unnecessary and harmful: it used to hide the lower part
            // of a page whenever the slot was smaller than the raster.
            const RECT pageRect{ left, top, left + destinationWidth,
                top + destinationHeight };
            if (settings.showPageShadow) {
                paintModernPageShadow(mem, pageRect, client);
            }

            RECT clipped{};
            if (!IntersectRect(&clipped, &client, &pageRect)) return;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = sourceWidth;
            info.bmiHeader.biHeight = -sourceHeight;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            // Draw from the complete source bitmap at its real destination
            // position and let the DC clip the off-screen area. Cropping the
            // source rectangle manually is unreliable for a top-down DIB when
            // `top` becomes negative: the destination shrinks but GDI can keep
            // sampling an effectively stationary source region, making the
            // upper page appear pinned while the next page covers it. Full-
            // bitmap drawing preserves one document-space transform for every
            // scroll position and matches the behavior of the original reader.
            const int saved = SaveDC(mem);
            if (saved != 0) {
                IntersectClipRect(mem, clipped.left, clipped.top,
                    clipped.right, clipped.bottom);
                StretchDIBits(mem, left, top, destinationWidth, destinationHeight,
                    0, 0, sourceWidth, sourceHeight, sourcePixels, &info,
                    DIB_RGB_COLORS, SRCCOPY);
                RestoreDC(mem, saved);
            }
            paintModernPageBorder(mem, pageRect, client);
        };

        const bool zoomPreview = zoomRenderPending && document &&
            pixelWidth > 0 && pixelHeight > 0 && pixelStride > 0 && !pixels.empty() &&
            pixelPage == pageIndex && pixelZoom > 0.0 && pixelDpi == currentDpi;
        if (zoomPreview) {
            const double previewScale = zoom / pixelZoom;
            const int previewWidth = std::max(1,
                static_cast<int>(std::lround(pixelWidth * previewScale)));
            const int previewHeight = std::max(1,
                static_cast<int>(std::lround(pixelHeight * previewScale)));
            const int left = zoomAnchor.valid
                ? static_cast<int>(std::lround(zoomAnchor.clientX - zoomAnchor.pageX * previewWidth))
                : pageLeft(client, previewWidth) - scrollX;
            const int top = zoomAnchor.valid
                ? static_cast<int>(std::lround(zoomAnchor.clientY - zoomAnchor.pageY * previewHeight))
                : (pageIndex >= 0 && pageIndex < static_cast<int>(pagePixelOffsets.size())
                    ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] - scrollY
                    : scaleDip(12) - scrollY);
            drawBitmap(pixels.data(), pixelWidth, pixelHeight, pixelStride,
                left, top, previewWidth, previewHeight);
        }
        else if (pageLayoutMode == PageLayoutMode::ContinuousNavigation && document) {
            const int firstPage = std::max(0, pageAtScrollOffset(scrollY) - 1);
            const int lastPage = std::min(pageCount - 1,
                pageAtScrollOffset(scrollY + client.bottom) + 1);
            for (int page = firstPage; page <= lastPage && page < pageCount; ++page) {
                const PageBitmap* bitmap = pageCache.Peek(page, zoom, currentDpi);
                const std::uint8_t* sourcePixels = nullptr;
                int sourceWidth = 0;
                int sourceHeight = 0;
                int sourceStride = 0;
                if (bitmap && bitmap->IsValid()) {
                    sourcePixels = bitmap->pixels.data();
                    sourceWidth = bitmap->width;
                    sourceHeight = bitmap->height;
                    sourceStride = bitmap->stride;
                }
                else if (page == pageIndex && pixelWidth > 0 && pixelHeight > 0 &&
                         pixelStride > 0 && !pixels.empty() && pixelPage == page &&
                         std::abs(pixelZoom - zoom) < 1.0e-9 && pixelDpi == currentDpi) {
                    sourcePixels = pixels.data();
                    sourceWidth = pixelWidth;
                    sourceHeight = pixelHeight;
                    sourceStride = pixelStride;
                }
                if (!sourcePixels || page >= static_cast<int>(pagePixelOffsets.size())) continue;

                // Use the shared absolute document geometry directly for every
                // page. Do not alter a page's top during painting based on which
                // neighbouring bitmaps happen to be cached; doing so makes the
                // visible position change as pages enter or leave the cache.
                const int top = pagePixelOffsets[static_cast<std::size_t>(page)] - scrollY;
                const int left = pageLeft(client, sourceWidth) - scrollX;
                drawBitmap(sourcePixels, sourceWidth, sourceHeight, sourceStride,
                    left, top, sourceWidth, sourceHeight);

                if (page == pageIndex) {
                    if (activeCommentObjectNumber != 0 && !currentPageComments.empty()) {
                        for (const auto& comment : currentPageComments) {
                            if (comment.objectNumber != activeCommentObjectNumber) continue;
                            RECT highlightRect{};
                            if (annotationClientRect(comment, pageIndex, client, highlightRect)) {
                                fillOverlay(mem, highlightRect, 78, RGB(255, 196, 0));
                            }
                        }
                    }
                    for (const std::size_t index : searchHighlights) {
                        if (index < textChunks.size()) {
                            fillOverlay(mem, chunkClientRect(textChunks[index], client, top),
                                96, RGB(255, 220, 40));
                        }
                    }
                    // Acrobat/Chrome-style selection: paint only the selected
                    // character spans, not a rubber-band rectangle or every
                    // complete text chunk touched by the pointer.
                    for (const TextSelectionSpan& span : selectedTextSpans) {
                        const RECT selected = selectionSpanClientRect(span, client, top);
                        fillOverlay(mem, selected, 108, RGB(51, 103, 209));
                    }
                }
            }
        }
        else if (document && pixelWidth > 0 && pixelHeight > 0 && pixelStride > 0 &&
                 !pixels.empty() && pixelPage == pageIndex &&
                 std::abs(pixelZoom - zoom) < 1.0e-9 && pixelDpi == currentDpi) {
            const int left = pageLeft(client, pixelWidth) - scrollX;
            const int top = scaleDip(12) - scrollY;
            drawBitmap(pixels.data(), pixelWidth, pixelHeight, pixelStride,
                left, top, pixelWidth, pixelHeight);
            if (activeCommentObjectNumber != 0 && !currentPageComments.empty()) {
                        for (const auto& comment : currentPageComments) {
                            if (comment.objectNumber != activeCommentObjectNumber) continue;
                            RECT highlightRect{};
                            if (annotationClientRect(comment, pageIndex, client, highlightRect)) {
                                fillOverlay(mem, highlightRect, 78, RGB(255, 196, 0));
                            }
                        }
                    }
            for (const std::size_t index : searchHighlights) {
                if (index < textChunks.size()) {
                    fillOverlay(mem, chunkClientRect(textChunks[index], client, top),
                        96, RGB(255, 220, 40));
                }
            }
            for (const TextSelectionSpan& span : selectedTextSpans) {
                fillOverlay(mem, selectionSpanClientRect(span, client, top),
                    108, RGB(51, 103, 209));
            }
        }

        BitBlt(dc, 0, 0, clientWidth, clientHeight, mem, 0, 0, SRCCOPY);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        canvasBackBuffer.Reset();
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_SETCURSOR) {
        const bool leftHandDrag = panning && handTool &&
            (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (leftHandDrag) {
            SetCursor(handDragCursor());
        } else if (panning || handTool) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        } else {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            RECT client{};
            GetClientRect(window, &client);
            const int pageTop = pageIndex >= 0 &&
                pageIndex < static_cast<int>(pagePixelOffsets.size())
                ? pagePixelOffsets[static_cast<std::size_t>(pageIndex)] - scrollY
                : scaleDip(12) - scrollY;
            const TextPosition hit = hitTestTextPosition(point, client, pageTop, false);
            SetCursor(LoadCursorW(nullptr, hit.valid ? IDC_IBEAM : IDC_ARROW));
        }
        return TRUE;
    }
    if (message == WM_SIZE) {
        updateCanvasScrollbars();
        // Live splitter resizing explicitly presents one complete canvas frame
        // after all dependent windows have moved. Avoid queuing a second paint
        // here for every intermediate WM_SIZE.
        if (!sidebarSplitterDragging && !readerWindowSizing) {
            InvalidateRect(window, nullptr, FALSE);
        }
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
        if (control && wParam == 'A' && !handTool) {
            selectAllText();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_ESCAPE && !selectedTextSpans.empty()) {
            clearTextSelection();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
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
    if (message == WM_LBUTTONDBLCLK && !handTool) {
        const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        selectingText = false;
        selectWordAt(point);
        SetFocus(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN) {
        if (message == WM_LBUTTONDOWN) {
            RECT client{};
            GetClientRect(window, &client);
            const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            // Annotation activation takes priority over Select/Hand. Acrobat
            // also allows comment annotations to be opened while the hand tool
            // is active; only empty page areas begin panning.
            if (const auto hitComment = hitTestCommentAtPoint(point, client)) {
                showCommentsPanelForPage(hitComment->page, hitComment->objectNumber);
                SetFocus(window);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (!handTool) {
                const bool extend = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                beginTextSelection(point, extend);
                if (selectingText) SetCapture(window);
                SetFocus(window);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
        }
        panning = true;
        panStart = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        panStartX = scrollX;
        panStartY = scrollY;
        SetCapture(window);
        SetFocus(window);
        if (message == WM_LBUTTONDOWN && handTool) {
            SetCursor(handDragCursor());
        } else {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
        return 0;
    }
    if (message == WM_MOUSEMOVE && panning) {
        if (handTool && (GetKeyState(VK_LBUTTON) & 0x8000) != 0) {
            SetCursor(handDragCursor());
        } else {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
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
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
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
            // Precision touchpads can emit many tiny wheel deltas. The former
            // implementation treated every message as a full 10% step, which
            // caused excessive invalidation and queued stale renders. Accumulate
            // a small fraction of a notch and use multiplicative zoom so both a
            // classic wheel and a high-resolution touchpad feel predictable.
            static double controlZoomRemainder{};
            controlZoomRemainder += static_cast<double>(delta);
            constexpr double kMinimumControlZoomDelta = WHEEL_DELTA / 8.0;
            if (std::abs(controlZoomRemainder) < kMinimumControlZoomDelta) return 0;
            const double wheelNotches = controlZoomRemainder / WHEEL_DELTA;
            controlZoomRemainder = 0.0;
            setZoomAtPoint(zoom * std::pow(1.15, wheelNotches), point);
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
    if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        if (sidebarSplitterDragging || pointHitsSidebarSplitter(window, point)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return TRUE;
        }
    }
    if (message == WM_LBUTTONDOWN) {
        const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (pointHitsSidebarSplitter(window, point)) {
            sidebarSplitterDragging = true;
            SetCapture(window);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return 0;
        }
    }
    if (message == WM_MOUSEMOVE && sidebarSplitterDragging) {
        // Collapse a burst of high-rate mouse messages into the newest cursor
        // position. This prevents splitter input from outrunning WM_PAINT.
        int latestX = GET_X_LPARAM(lParam);
        MSG queued{};
        while (PeekMessageW(&queued, window, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE)) {
            latestX = GET_X_LPARAM(queued.lParam);
        }
        resizeSidebarFromClientX(window, latestX);
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return 0;
    }
    if (message == WM_LBUTTONUP && sidebarSplitterDragging) {
        const int finalX = GET_X_LPARAM(lParam);
        // Clear the live-resize flag before the final layout so the canvas
        // replaces the temporary scaled snapshot with a fully composed frame.
        sidebarSplitterDragging = false;
        resizeSidebarFromClientX(window, finalX, true);
        if (GetCapture() == window) ReleaseCapture();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return 0;
    }
    if (message == WM_CAPTURECHANGED && sidebarSplitterDragging) {
        sidebarSplitterDragging = false;
        RECT client{};
        GetClientRect(window, &client);
        updateLayout(static_cast<int>(client.right), static_cast<int>(client.bottom));
        return 0;
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
            tab.toc = loaded.toc;
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
        currentToc = loaded.toc;
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
            tab.toc = currentToc;
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
    if (message == WM_TIMER && wParam == TEXT_GEOMETRY_TIMER) {
        KillTimer(window, TEXT_GEOMETRY_TIMER);
        // Native text extraction and page rendering share the same document
        // object. Never block the UI (or risk concurrent parser access) while
        // a render/prefetch is active; retry after the renderer settles.
        if (renderThread.joinable() || zoomRenderPending) {
            SetTimer(window, TEXT_GEOMETRY_TIMER, kTextGeometryDebounceMs, nullptr);
            return 0;
        }
        const int requestedPage = textGeometryRequestPage;
        textGeometryRequestPage = -1;
        if (document && requestedPage == pageIndex) {
            refreshTextGeometry();
            InvalidateRect(canvas, nullptr, FALSE);
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
                if (success) {
                    zoomRenderPending = false;
                    applyCachedPage(pageIndex, zoom);
                }
                else {
                    zoomRenderPending = false;
                    renderPage();
                }
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
            zoomRenderPending = false;
            setStatus(L"Render failed: " + utf8ToWide(result.error.c_str()));
        }
        else {
            zoomRenderPending = false;
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
        const int tabBarHeight = tabBar ? scaleDip(kTabBarHeightDip) : 0;
        const int contentTop = ribbonHeight + tabBarHeight;
        RECT ribbon{ 0, 0, client.right, ribbonHeight };
        // Soft two-stop gradient gives the toolbar a slightly raised look.
        const HBRUSH ribbonBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::toolbar);
        FillRect(dc, &ribbon, ribbonBrush);
        DeleteObject(ribbonBrush);
        const int sidebarWidth = effectiveSidebarWidthPixels(client.right);
        const int splitterWidth = sidebarSplitterWidthPixels();
        RECT sidebar{ 0, ribbonHeight,
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
        if (sidebarVisible && splitterWidth > 0) {
            RECT splitterRect{ sidebarWidth, ribbonHeight,
                sidebarWidth + splitterWidth, client.bottom - statusHeight };
            const HBRUSH splitterBrush = CreateSolidBrush(RGB(242, 244, 247));
            FillRect(dc, &splitterRect, splitterBrush);
            DeleteObject(splitterBrush);

            // Draw a subtle grip centered in the hit area. The full 5-DIP
            // strip remains draggable while the visual divider stays light.
            const int splitterCenter = sidebarWidth + splitterWidth / 2;
            SelectObject(dc, separator);
            MoveToEx(dc, splitterCenter, ribbonHeight, nullptr);
            LineTo(dc, splitterCenter, client.bottom - statusHeight);
        }
        if (toolsVisible) {
            const int toolsWidth = effectiveToolsWidthPixels(client.right);
            if (toolsWidth > 0) {
                const int toolsLeft = client.right - toolsWidth;
                MoveToEx(dc, toolsLeft, ribbonHeight, nullptr);
                LineTo(dc, toolsLeft, client.bottom - statusHeight);
            }
        }
        SelectObject(dc, oldPen);
        DeleteObject(separator);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const bool sidebarContent = control == pageList || control == sidebarTitle;
        const bool toolsContent = control == toolsTitle || control == toolsTree || control == toolsSearchEdit;
        const COLORREF background = control == statusLabel ? PdfPP::ModernWin32::Theme::status
            : sidebarContent ? RGB(255, 255, 255)
            : toolsContent ? RGB(248, 249, 251)
            : PdfPP::ModernWin32::Theme::toolbar;
        PdfPP::ModernWin32::SetControlColors(dc, background, PdfPP::ModernWin32::Theme::text);
        static HBRUSH sidebarContentBrush = CreateSolidBrush(RGB(255, 255, 255));
        static HBRUSH toolsContentBrush = CreateSolidBrush(RGB(248, 249, 251));
        static HBRUSH toolbarBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::toolbar);
        return reinterpret_cast<LRESULT>(sidebarContent ? sidebarContentBrush : toolsContent ? toolsContentBrush : toolbarBrush);
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
        if (header && header->hwndFrom == toolsTree && header->code == TVN_SELCHANGEDW &&
            rightPanelMode == RightPanelMode::Comments) {
            const auto* change = reinterpret_cast<const NMTREEVIEWW*>(lParam);
            const LPARAM param = change ? change->itemNew.lParam : 0;
            if (param > 0) {
                const std::size_t index = static_cast<std::size_t>(param - 1);
                if (index < currentPageComments.size()) {
                    activeCommentObjectNumber = currentPageComments[index].objectNumber;
                    InvalidateRect(canvas, nullptr, FALSE);
                }
            }
            return 0;
        }
        if (header && header->hwndFrom == toolsTree &&
            (header->code == NM_DBLCLK || header->code == NM_RETURN)) {
            const HTREEITEM selectedItem = TreeView_GetSelection(toolsTree);
            if (selectedItem) {
                TVITEMW item{};
                item.mask = TVIF_PARAM;
                item.hItem = selectedItem;
                if (TreeView_GetItem(toolsTree, &item) && item.lParam > 0) {
                    if (rightPanelMode == RightPanelMode::Comments) {
                        const std::size_t index = static_cast<std::size_t>(item.lParam - 1);
                        if (index < currentPageComments.size()) {
                            activeCommentObjectNumber = currentPageComments[index].objectNumber;
                            InvalidateRect(canvas, nullptr, FALSE);
                        }
                    } else {
                        executeToolCommand(static_cast<int>(item.lParam));
                    }
                }
            }
            return 0;
        }
    }
    if (message == WM_COMMAND &&
        (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0 || HIWORD(wParam) == EN_CHANGE)) {
        switch (LOWORD(wParam)) {
        case ID_TOOLS_SEARCH:
            if (HIWORD(wParam) == EN_CHANGE) {
                populateToolsTree();
            }
            return 0;
        case ID_OPEN:
            // Opening when a document is already loaded opens a new tab.
            openDocument();
            return 0;
        case ID_PRINT: printCurrentPage(); return 0;
        case ID_MERGE_PDFS: mergePdfDocuments(); return 0;
        case ID_EXTRACT_PAGES: extractPdfPages(); return 0;
        case ID_SPLIT_PDF: splitPdfDocument(); return 0;
        case ID_DELETE_PAGES: deletePdfPages(); return 0;
        case ID_DUPLICATE_PAGES: duplicatePdfPages(); return 0;
        case ID_MOVE_PAGE: moveCurrentPdfPage(); return 0;
        case ID_REORDER_PAGES: reorderPdfPages(); return 0;
        case ID_REVERSE_PAGES: reversePdfPages(); return 0;
        case ID_CRACK_PASSWORD: crackPasswordAndOpenPdf(); return 0;
        case ID_ADD_PASSWORD: addPdfPassword(); return 0;
        case ID_REMOVE_PASSWORD: removePdfPassword(); return 0;
        case ID_CHANGE_PASSWORD: changePdfPassword(); return 0;
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
            showFindPanel(true);
            return 0;
        case ID_SEARCH_NEXT:
            if (!findPanelVisible) showFindPanel(true);
            findText();
            return 0;
        case ID_FIND_CLOSE:
            showFindPanel(false);
            return 0;
        case ID_FIND_OPTIONS:
            showFindOptionsMenu();
            return 0;
        case ID_FIND_SCOPE_WHOLE_PAGE:
            findScope = FindScope::WholePage;
            ++findOptionsRevision;
            resetFindSearchState();
            setStatus(L"Find scope: Whole Page");
            return 0;
        case ID_FIND_SCOPE_CURRENT_PAGE:
            findScope = FindScope::CurrentPage;
            ++findOptionsRevision;
            resetFindSearchState();
            setStatus(L"Find scope: Current Page");
            return 0;
        case ID_FIND_CASE_SENSITIVE:
            findCaseMode = FindCaseMode::CaseSensitive;
            ++findOptionsRevision;
            resetFindSearchState();
            setStatus(L"Find: Case Sensitive");
            return 0;
        case ID_FIND_CASE_INSENSITIVE:
            findCaseMode = FindCaseMode::CaseInsensitive;
            ++findOptionsRevision;
            resetFindSearchState();
            setStatus(L"Find: Case Insensitive");
            return 0;
        case ID_FIND_MODE_NORMAL:
            findPatternMode = FindPatternMode::Normal;
            ++findOptionsRevision;
            resetFindSearchState();
            setStatus(L"Find mode: Normal");
            return 0;
        case ID_FIND_MODE_REGEX:
            findPatternMode = FindPatternMode::RegularExpression;
            ++findOptionsRevision;
            resetFindSearchState();
            setStatus(L"Find mode: Regular Expression");
            return 0;
        case ID_FIND_INCLUDE_COMMENTS:
            findIncludeComments = !findIncludeComments;
            ++findOptionsRevision;
            resetFindSearchState();
            setStatus(findIncludeComments
                ? L"Find includes comments" : L"Find excludes comments");
            return 0;
        case ID_FIND_INCLUDE_BOOKMARKS:
            findIncludeBookmarks = !findIncludeBookmarks;
            ++findOptionsRevision;
            resetFindSearchState();
            setStatus(findIncludeBookmarks
                ? L"Find includes bookmarks" : L"Find excludes bookmarks");
            return 0;
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
        case ID_SHOW_PAGE_SHADOW:
            settings.showPageShadow = !settings.showPageShadow;
            persistSettings();
            updateCommandState();
            InvalidateRect(canvas, nullptr, FALSE);
            setStatus(settings.showPageShadow
                ? L"Page shadow enabled" : L"Page shadow disabled");
            return 0;
        case ID_ABOUT: showAboutDialog(); return 0;
        case ID_BOOKMARKS:
            sidebarVisible = !sidebarVisible;
            { RECT client{}; GetClientRect(window, &client); updateLayout(client.right, client.bottom); }
            updateCommandState();
            persistSettings();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case ID_SIDEBAR_TOGGLE:
            sidebarVisible = !sidebarVisible;
            { RECT client{}; GetClientRect(window, &client); updateLayout(client.right, client.bottom); }
            updateCommandState();
            persistSettings();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case ID_TOOLS_TOGGLE:
            toolsVisible = !toolsVisible;
            { RECT client{}; GetClientRect(window, &client); updateLayout(client.right, client.bottom); }
            updateCommandState();
            persistSettings();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case ID_BOOKMARK_CLOSE:
            sidebarVisible = false;
            { RECT client{}; GetClientRect(window, &client); updateLayout(client.right, client.bottom); }
            updateCommandState();
            persistSettings();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case ID_ADD_FAVORITE: toggleCurrentFavorite(); return 0;
        case ID_TOOL_PDF_TEXT_EDITOR:
        case ID_TOOL_COMPARE:
        case ID_TOOL_COMPRESS:
        case ID_TOOL_CONVERT:
        case ID_TOOL_OCR:
        case ID_TOOL_REDACT:
        case ID_TOOL_MULTI_TOOL:
        case ID_TOOL_SIGN_WITH_CERTIFICATE:
        case ID_TOOL_TIMESTAMP_PDF:
        case ID_TOOL_SIGN:
        case ID_TOOL_SHARED_SIGNING:
        case ID_TOOL_ADD_WATERMARK:
        case ID_TOOL_ADD_STAMP:
        case ID_TOOL_SANITIZE:
        case ID_TOOL_FLATTEN:
        case ID_TOOL_UNLOCK_FORMS:
        case ID_TOOL_CHANGE_PERMISSIONS:
        case ID_TOOL_GET_ALL_INFO:
        case ID_TOOL_VALIDATE_SIGNATURE:
        case ID_TOOL_CHANGE_METADATA:
        case ID_TOOL_EDIT_TABLE_OF_CONTENTS:
        case ID_TOOL_READ:
            executeToolCommand(LOWORD(wParam));
            return 0;
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
    if (message == WM_ENTERSIZEMOVE) {
        readerWindowSizing = true;
        return 0;
    }
    if (message == WM_EXITSIZEMOVE) {
        readerWindowSizing = false;
        RECT client{};
        GetClientRect(window, &client);
        updateLayout(static_cast<int>(client.right), static_cast<int>(client.bottom));
        if (canvas) {
            RedrawWindow(canvas, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
        return 0;
    }
    if (message == WM_SIZE) {
        updateLayout(LOWORD(lParam), HIWORD(lParam));
        return 0;
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
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE && findPanelVisible) {
        showFindPanel(false); return 0;
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
