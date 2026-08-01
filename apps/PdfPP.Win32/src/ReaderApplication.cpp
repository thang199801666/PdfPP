#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <PdfPP/ModernWin32.hpp>
#include <PdfPP/Win32/AppCommands.hpp>
#include <PdfPP/Win32/NativePdf.hpp>
#include <PdfPP/Win32/PageCache.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <mutex>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {
using namespace PdfPP::Win32;
using namespace PdfPP::Win32::Command;

HWND mainWindow{};
HWND pageEdit{};
HWND pageLabel{};
HWND searchEdit{};
HWND statusLabel{};
HWND canvas{};
HWND pageList{};
HWND sidebarTitle{};
HWND bookmarkCloseButton{};
HWND pageCaption{};
HWND findLabel{};
HWND openButton{};
HWND previousButton{};
HWND nextButton{};
HWND zoomOutButton{};
HWND zoomInButton{};
HWND fitButton{};
HWND handButton{};
HWND findButton{};
HWND zoomLabel{};
HMENU mainMenu{};
std::shared_ptr<NativePdfDocument> document;
int pageCount{};
int pageIndex{};
double zoom{1.0};
int scrollX{};
int scrollY{};
std::vector<std::uint8_t> pixels;
std::vector<HTREEITEM> pageItems;
HTREEITEM bookmarkRoot{};
bool sidebarVisible{true};
bool updatingBookmarkSelection{};
std::wstring lastSearchQuery;
int lastSearchPage{-1};
int pixelWidth{};
int pixelHeight{};
int pixelStride{};
std::thread renderThread;
std::mutex renderMutex;
std::atomic_bool renderReady{false};
std::thread openThread;
std::mutex openMutex;
std::atomic_bool openReady{false};
UINT currentDpi{USER_DEFAULT_SCREEN_DPI};
HFONT regularUiFont{};
HFONT boldUiFont{};

struct RenderResult final {
    std::shared_ptr<NativePdfDocument> document;
    PageBitmap bitmap;
    bool prefetch{};
    std::string error;
};
RenderResult renderResult;

struct OpenResult final {
    std::shared_ptr<NativePdfDocument> document;
    int pageCount{};
    std::wstring title;
    std::string error;
};
OpenResult openResult;

PageCache pageCache{4};

struct ZoomAnchor final {
    bool valid{};
    int clientX{};
    int clientY{};
    double pageX{};
    double pageY{};
};
ZoomAnchor zoomAnchor;
bool handTool{true};
bool panning{};
POINT panStart{};
int panStartX{};
int panStartY{};
bool fullscreen{};
LONG fullscreenStyle{};
LONG fullscreenExStyle{};
WINDOWPLACEMENT fullscreenPlacement{sizeof(fullscreenPlacement)};

void updateCanvasScrollbars();
void updateLayout(int width, int height);
void renderPage();
void updateCommandState();

void toggleFullscreen(HWND window) {
    if (!window) return;
    if (!fullscreen) {
        fullscreenStyle = GetWindowLongW(window, GWL_STYLE);
        fullscreenExStyle = GetWindowLongW(window, GWL_EXSTYLE);
        fullscreenPlacement.length = sizeof(fullscreenPlacement);
        if (!GetWindowPlacement(window, &fullscreenPlacement)) return;

        MONITORINFO monitor{sizeof(monitor)};
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
    } else {
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

int scaleDip(const int value) {
    return MulDiv(value, static_cast<int>(currentDpi), USER_DEFAULT_SCREEN_DPI);
}

std::wstring utf8ToWide(const char* text) {
    if (!text || !*text) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    return result;
}

void setStatus(const std::wstring& value) { SetWindowTextW(statusLabel, value.c_str()); }

void updateZoomLabel() {
    if (!zoomLabel) return;
    wchar_t value[32]{};
    swprintf_s(value, L"%.0f%%", zoom * 100.0);
    SetWindowTextW(zoomLabel, value);
}

void updateCommandState() {
    if (!mainMenu) return;
    const bool hasDocument = document && pageCount > 0;
    const bool hasPrevious = hasDocument && pageIndex > 0;
    const bool hasNext = hasDocument && pageIndex + 1 < pageCount;

    EnableMenuItem(mainMenu, ID_CLOSE, MF_BYCOMMAND | (hasDocument ? MF_ENABLED : MF_GRAYED));
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
}

void selectBookmarkPage() {
    if (!pageList || pageIndex < 0 || pageIndex >= static_cast<int>(pageItems.size())) return;
    if (TreeView_GetSelection(pageList) == pageItems[pageIndex]) return;
    updatingBookmarkSelection = true;
    TreeView_SelectItem(pageList, pageItems[pageIndex]);
    updatingBookmarkSelection = false;
}

void populateBookmarkTree(const std::wstring& title) {
    if (!pageList) return;
    TreeView_DeleteAllItems(pageList);
    pageItems.clear();
    bookmarkRoot = {};
    std::wstring rootText = title.empty() ? L"Document" : title;
    if (rootText.size() > 32U) rootText.resize(32U);
    TVINSERTSTRUCTW root{};
    root.hParent = TVI_ROOT;
    root.hInsertAfter = TVI_ROOT;
    root.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    root.item.pszText = rootText.data();
    root.item.lParam = -1;
    root.item.cChildren = 1;
    bookmarkRoot = TreeView_InsertItem(pageList, &root);
    for (int index = 0; index < pageCount; ++index) {
        wchar_t label[64]{};
        swprintf_s(label, L"Page %d", index + 1);
        TVINSERTSTRUCTW item{};
        item.hParent = bookmarkRoot;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;
        item.item.pszText = label;
        item.item.lParam = index;
        pageItems.push_back(TreeView_InsertItem(pageList, &item));
    }
    TreeView_Expand(pageList, bookmarkRoot, TVE_EXPAND);
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

int pageLeft(const RECT& client, const int width) {
    return std::max(scaleDip(12), static_cast<int>((client.right - width) / 2));
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

bool hasCachedPage(const int requestedPage, const double requestedZoom) {
    return pageCache.Contains(requestedPage, requestedZoom, currentDpi);
}

void rememberNativePage(RenderResult& result) {
    pageCache.Store(std::move(result.bitmap));
}

void finishPageLayout() {
    RECT client{};
    GetClientRect(canvas, &client);
    if (zoomAnchor.valid) {
        const int left = pageLeft(client, pixelWidth);
        const int top = scaleDip(12);
        scrollX = static_cast<int>(std::lround(left + zoomAnchor.pageX * pixelWidth - zoomAnchor.clientX));
        scrollY = static_cast<int>(std::lround(top + zoomAnchor.pageY * pixelHeight - zoomAnchor.clientY));
        zoomAnchor.valid = false;
    } else {
        scrollX = 0;
        scrollY = 0;
    }
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
    finishPageLayout();
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
    if (!document || nextPage >= pageCount || renderThread.joinable() || hasCachedPage(nextPage, zoom)) return;
    startRender(nextPage, true);
}

void updateCanvasScrollbars() {
    if (!canvas) return;
    RECT client{}; GetClientRect(canvas, &client);
    const int viewportWidth = std::max(1L, client.right);
    const int viewportHeight = std::max(1L, client.bottom);
    const int contentWidth = std::max(1, pixelWidth + scaleDip(24));
    const int contentHeight = std::max(1, pixelHeight + scaleDip(24));
    const int maxHorizontal = std::max(0, contentWidth - viewportWidth);
    const int maxVertical = std::max(0, contentHeight - viewportHeight);
    scrollX = std::clamp(scrollX, 0, maxHorizontal);
    scrollY = std::clamp(scrollY, 0, maxVertical);
    SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                          contentWidth - 1, static_cast<UINT>(viewportWidth), scrollX};
    SCROLLINFO vertical{sizeof(vertical), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                        contentHeight - 1, static_cast<UINT>(viewportHeight), scrollY};
    SetScrollInfo(canvas, SB_HORZ, &horizontal, TRUE);
    SetScrollInfo(canvas, SB_VERT, &vertical, TRUE);
}

void setCanvasScroll(const int bar, int position) {
    if (!canvas) return;
    RECT client{}; GetClientRect(canvas, &client);
    const int viewport = bar == SB_VERT ? std::max(1L, client.bottom) : std::max(1L, client.right);
    const int content = bar == SB_VERT ? std::max(1, pixelHeight + scaleDip(24))
                                      : std::max(1, pixelWidth + scaleDip(24));
    const int maximum = std::max(0, content - viewport);
    position = std::clamp(position, 0, maximum);
    const int previous = bar == SB_VERT ? scrollY : scrollX;
    if (previous == position) {
        // Wheel/track messages continue to arrive at the edge of the
        // document. Avoid invalidating an unchanged canvas, otherwise the
        // page and its shadow visibly flicker at the scroll limit.
        return;
    }
    if (bar == SB_VERT) scrollY = position; else scrollX = position;
    SetScrollPos(canvas, bar, position, TRUE);
    InvalidateRect(canvas, nullptr, FALSE);
}

void applyRenderedPage(PageBitmap bitmap) {
    if (!bitmap.IsValid()) return;
    pixelWidth = bitmap.width;
    pixelHeight = bitmap.height;
    pixelStride = bitmap.stride;
    pixels = std::move(bitmap.pixels);
    rememberCurrentPage();
    finishPageLayout();
}

void renderPage() {
    if (!document || pageIndex < 0 || pageIndex >= pageCount) return;
    // A render already in flight owns the current document safely. Let it
    // finish in the background; WM_RENDER_COMPLETE will discard stale
    // results and schedule the latest page/zoom request.
    if (renderThread.joinable()) {
        SetTimer(mainWindow, RENDER_TIMER, 15, nullptr);
        return;
    }
    if (applyCachedPage(pageIndex, zoom)) return;
    startRender(pageIndex, false);
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
    document.reset(); pageCount = 0; pageIndex = 0; pixels.clear(); pageCache.Clear();
    pixelWidth = pixelHeight = pixelStride = 0;
    zoomAnchor = {};
    lastSearchQuery.clear();
    lastSearchPage = -1;
    TreeView_DeleteAllItems(pageList);
    pageItems.clear();
    bookmarkRoot = {};
    updatePageControls();
    InvalidateRect(canvas, nullptr, TRUE);
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
        zoomAnchor.pageY = std::clamp(
            static_cast<double>(point.y + scrollY - scaleDip(12)) / pixelHeight, 0.0, 1.0);
    }
    zoom = next;
    renderPage();
}

void openDocument() {
    if (openThread.joinable()) return;
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = mainWindow;
    dialog.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return;
    const std::wstring selectedPath = path;
    setStatus(L"Opening PDF...");
    EnableWindow(openButton, FALSE);
    openReady.store(false, std::memory_order_release);
    openThread = std::thread([selectedPath] {
        OpenResult result;
        result.document = NativePdfDocument::Open(selectedPath, result.error);
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
            pageIndex = page;
            renderPage();
            setStatus(L"Found on page " + std::to_wstring(page + 1));
            return;
        }
    }
    lastSearchPage = -1;
    setStatus(L"Text not found");
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

HMENU createMainMenu() {
    const HMENU menu = CreateMenu();
    const HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_OPEN, L"Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_CLOSE, L"Close");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, SC_CLOSE, L"Exit");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File");

    const HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, ID_BOOKMARKS, L"Bookmarks");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_VIEW_FIT_PAGE, L"Fit Page");
    AppendMenuW(view, MF_STRING, ID_FIT_WIDTH, L"Fit Width");
    AppendMenuW(view, MF_STRING, ID_VIEW_ACTUAL, L"Actual Size (100%)");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_FULLSCREEN, L"Fullscreen\tF11");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"View");

    const HMENU goTo = CreatePopupMenu();
    AppendMenuW(goTo, MF_STRING, ID_FIRST_PAGE, L"First Page\tHome");
    AppendMenuW(goTo, MF_STRING, ID_PREVIOUS, L"Previous Page\tPage Up");
    AppendMenuW(goTo, MF_STRING, ID_NEXT, L"Next Page\tPage Down");
    AppendMenuW(goTo, MF_STRING, ID_LAST_PAGE, L"Last Page\tEnd");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(goTo), L"Go To");

    const HMENU zoomMenu = CreatePopupMenu();
    AppendMenuW(zoomMenu, MF_STRING, ID_ZOOM_OUT, L"Zoom Out\tCtrl+-");
    AppendMenuW(zoomMenu, MF_STRING, ID_ZOOM_IN, L"Zoom In\tCtrl++");
    AppendMenuW(zoomMenu, MF_STRING, ID_VIEW_ACTUAL, L"Actual Size (100%)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(zoomMenu), L"Zoom");

    const HMENU settings = CreatePopupMenu();
    AppendMenuW(settings, MF_STRING, ID_HAND_TOOL, L"Hand Tool");
    AppendMenuW(settings, MF_STRING, ID_DOC_PROPERTIES, L"Document Properties");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), L"Settings");

    const HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"About Pdf++");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Help");
    return menu;
}

void refreshApplicationFonts(const UINT dpi) {
    HFONT newRegular = PdfPP::ModernWin32::UiFontForDpi(dpi, 7, false);
    HFONT newBold = PdfPP::ModernWin32::UiFontForDpi(dpi, 8, true);
    if (!newRegular || !newBold) {
        if (newRegular) DeleteObject(newRegular);
        if (newBold) DeleteObject(newBold);
        return;
    }

    for (const HWND control : {openButton, previousButton, nextButton, pageEdit, pageCaption,
                               pageLabel, zoomOutButton, zoomLabel, zoomInButton, fitButton,
                               handButton, findLabel, searchEdit, findButton, statusLabel,
                               pageList, bookmarkCloseButton}) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newRegular), TRUE);
    }
    if (sidebarTitle)
        SendMessageW(sidebarTitle, WM_SETFONT, reinterpret_cast<WPARAM>(newBold), TRUE);

    const HFONT oldRegular = regularUiFont;
    const HFONT oldBold = boldUiFont;
    regularUiFont = newRegular;
    boldUiFont = newBold;
    if (oldRegular) DeleteObject(oldRegular);
    if (oldBold) DeleteObject(oldBold);
}

void captureCanvasCenterAnchor() {
    if (!canvas || pixelWidth <= 0 || pixelHeight <= 0) return;
    RECT client{};
    GetClientRect(canvas, &client);
    const POINT point{client.right / 2, client.bottom / 2};
    const int left = pageLeft(client, pixelWidth);
    zoomAnchor.valid = true;
    zoomAnchor.clientX = point.x;
    zoomAnchor.clientY = point.y;
    zoomAnchor.pageX = std::clamp(
        static_cast<double>(point.x + scrollX - left) / pixelWidth, 0.0, 1.0);
    zoomAnchor.pageY = std::clamp(
        static_cast<double>(point.y + scrollY - scaleDip(12)) / pixelHeight, 0.0, 1.0);
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

void updateLayout(const int width, const int height) {
    const int ribbon = scaleDip(PdfPP::ModernWin32::Layout::ribbonHeight);
    const int status = scaleDip(PdfPP::ModernWin32::Layout::statusHeight);
    const int bodyHeight = std::max(0, height - ribbon - status);
    const int sidebar = sidebarVisible ? scaleDip(PdfPP::ModernWin32::Layout::sidebarWidth) : 0;
    const int controlY = scaleDip(14);
    const int controlHeight = scaleDip(PdfPP::ModernWin32::Layout::controlHeight);
    MoveWindow(openButton, scaleDip(12), controlY, scaleDip(48), controlHeight, TRUE);
    MoveWindow(pageCaption, scaleDip(68), controlY + scaleDip(4), scaleDip(34), scaleDip(18), TRUE);
    MoveWindow(pageEdit, scaleDip(104), controlY + scaleDip(1), scaleDip(42), scaleDip(23), TRUE);
    MoveWindow(pageLabel, scaleDip(150), controlY + scaleDip(4), scaleDip(42), scaleDip(18), TRUE);
    MoveWindow(previousButton, scaleDip(196), controlY, scaleDip(26), controlHeight, TRUE);
    MoveWindow(nextButton, scaleDip(228), controlY, scaleDip(26), controlHeight, TRUE);
    MoveWindow(zoomOutButton, scaleDip(264), controlY, scaleDip(28), controlHeight, TRUE);
    MoveWindow(zoomInButton, scaleDip(298), controlY, scaleDip(28), controlHeight, TRUE);
    MoveWindow(zoomLabel, scaleDip(332), controlY + scaleDip(4), scaleDip(48), scaleDip(18), TRUE);
    MoveWindow(fitButton, scaleDip(388), controlY, scaleDip(48), controlHeight, TRUE);
    MoveWindow(handButton, scaleDip(442), controlY, scaleDip(58), controlHeight, TRUE);
    MoveWindow(findLabel, scaleDip(512), controlY + scaleDip(4), scaleDip(36), scaleDip(18), TRUE);
    MoveWindow(searchEdit, scaleDip(552), controlY + scaleDip(1), scaleDip(180), scaleDip(23), TRUE);
    MoveWindow(findButton, scaleDip(740), controlY, scaleDip(50), controlHeight, TRUE);
    const int sidebarHeader = scaleDip(28);
    MoveWindow(sidebarTitle, 0, ribbon, sidebar, sidebarHeader, TRUE);
    MoveWindow(bookmarkCloseButton, std::max(0, sidebar - scaleDip(28)), ribbon + scaleDip(2),
               scaleDip(24), scaleDip(24), TRUE);
    MoveWindow(pageList, 0, ribbon + sidebarHeader, sidebar,
               std::max(0, bodyHeight - sidebarHeader), TRUE);
    ShowWindow(sidebarTitle, sidebarVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(bookmarkCloseButton, sidebarVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(pageList, sidebarVisible ? SW_SHOW : SW_HIDE);
    MoveWindow(canvas, sidebar, ribbon, std::max(0, width - sidebar), bodyHeight, TRUE);
    MoveWindow(statusLabel, scaleDip(16), height - status + scaleDip(4),
               std::max(0, width - scaleDip(32)), scaleDip(20), TRUE);
    updateCanvasScrollbars();
    // The canvas can move without receiving a size change (for example when
    // the sidebar is toggled). Repaint it after every parent relayout so the
    // page is positioned against the new client rectangle immediately.
    if (canvas) InvalidateRect(canvas, nullptr, FALSE);
}

LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        const HBRUSH background = CreateSolidBrush(PdfPP::ModernWin32::Theme::canvas);
        FillRect(dc, &client, background);
        DeleteObject(background);
        if (pixelWidth > 0 && !pixels.empty()) {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = pixelWidth; info.bmiHeader.biHeight = -pixelHeight;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            const int left = pageLeft(client, pixelWidth) - scrollX;
            const int top = scaleDip(12) - scrollY;
            // Scroll/resize can invalidate only a small exposed strip. Copy
            // that strip instead of transferring the entire high-resolution
            // page on every WM_PAINT. StretchDIBits accepts an explicit source
            // rectangle for a top-down DIB; SetDIBitsToDevice's scan-line
            // parameters do not and caused the page to jump after zooming.
            RECT pageRect{left, top, left + pixelWidth, top + pixelHeight};
            RECT shadowRect{pageRect.left + 5, pageRect.top + 5,
                            pageRect.right + 5, pageRect.bottom + 5};
            const HBRUSH shadow = CreateSolidBrush(RGB(193, 198, 205));
            FillRect(dc, &shadowRect, shadow);
            DeleteObject(shadow);
            RECT clipped{};
            if (IntersectRect(&clipped, &paint.rcPaint, &pageRect)) {
                const int sourceX = clipped.left - left;
                const int sourceY = clipped.top - top;
                const int copyWidth = clipped.right - clipped.left;
                const int copyHeight = clipped.bottom - clipped.top;
                StretchDIBits(dc, clipped.left, clipped.top, copyWidth, copyHeight,
                              sourceX, sourceY, copyWidth, copyHeight,
                              pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            }
        }
        EndPaint(window, &paint); return 0;
    }
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, handTool ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    if (message == WM_SIZE) {
        updateCanvasScrollbars();
        // A resize exposes newly available canvas pixels. Repaint even when
        // the document scroll position itself did not change.
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
        switch (wParam) {
        case VK_PRIOR: if (pageIndex > 0) { --pageIndex; renderPage(); } return 0;
        case VK_NEXT: if (pageIndex + 1 < pageCount) { ++pageIndex; renderPage(); } return 0;
        case VK_HOME: if (pageCount > 0) { pageIndex = 0; renderPage(); } return 0;
        case VK_END: if (pageCount > 0) { pageIndex = pageCount - 1; renderPage(); } return 0;
        case VK_F3: findText(); return 0;
        }
    }
    if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN) {
        if (message == WM_LBUTTONDOWN && !handTool) return DefWindowProcW(window, message, wParam, lParam);
        panning = true;
        panStart = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
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
        setCanvasScroll(SB_VERT, panStartY - (y - panStart.y));
        return 0;
    }
    if (message == WM_LBUTTONUP || message == WM_MBUTTONUP) {
        if (GetCapture() == window) ReleaseCapture();
        panning = false;
        return 0;
    }
    if (message == WM_VSCROLL || message == WM_HSCROLL) {
        const int bar = message == WM_VSCROLL ? SB_VERT : SB_HORZ;
        const int old = bar == SB_VERT ? scrollY : scrollX;
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(window, bar, &info);
        int next = old;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: next -= scaleDip(32); break;
        case SB_LINEDOWN: next += scaleDip(32); break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: next = info.nTrackPos; break;
        }
        next = std::clamp(next, 0, std::max(0, info.nMax - static_cast<int>(info.nPage) + 1));
        setCanvasScroll(bar, next);
        return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
            setZoomAtPoint(zoom + (delta > 0 ? 0.10 : -0.10), point);
            return 0;
        }
        static int wheelRemainder{};
        wheelRemainder += delta;
        const int notches = wheelRemainder / WHEEL_DELTA;
        wheelRemainder -= notches * WHEEL_DELTA;
        if (notches != 0) {
            const int bar = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0 ? SB_HORZ : SB_VERT;
            // A positive Win32 wheel delta is the standard forward/away
            // gesture. PDF viewers map it to scrolling the document down.
            setCanvasScroll(bar, (bar == SB_VERT ? scrollY : scrollX) + notches * scaleDip(48));
        }
        return 0;
    }
    if (message == WM_MOUSEHWHEEL) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        setCanvasScroll(SB_HORZ, scrollX + (delta > 0 ? scaleDip(48) : -scaleDip(48)));
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
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
        closeDocument();
        document = loaded.document;
        pageCount = loaded.pageCount;
        pageIndex = 0;
        populateBookmarkTree(loaded.title);
        SetWindowTextW(mainWindow, (loaded.title + L" - Pdf++ Reader").c_str());
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
        if (result.prefetch) {
            const bool success = result.error.empty();
            if (success) rememberNativePage(result);
            if (current) {
                if (success) applyCachedPage(pageIndex, zoom);
                else renderPage();
            }
            else if (result.document == document && !hasCachedPage(pageIndex, zoom)) renderPage();
        } else if (!current) {
            renderPage();
        } else if (!result.error.empty()) {
            setStatus(L"Render failed: " + utf8ToWide(result.error.c_str()));
        } else {
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
        RECT ribbon{0, 0, client.right, ribbonHeight};
        const HBRUSH ribbonBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::toolbar);
        FillRect(dc, &ribbon, ribbonBrush);
        DeleteObject(ribbonBrush);
        const int sidebarWidth = sidebarVisible ? scaleDip(PdfPP::ModernWin32::Layout::sidebarWidth) : 0;
        RECT sidebar{0, ribbonHeight,
                     sidebarWidth,
                     client.bottom - statusHeight};
        const HBRUSH sidebarBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::sidebar);
        FillRect(dc, &sidebar, sidebarBrush);
        DeleteObject(sidebarBrush);
        RECT status{0, client.bottom - statusHeight,
                    client.right, client.bottom};
        const HBRUSH statusBrush = CreateSolidBrush(PdfPP::ModernWin32::Theme::status);
        FillRect(dc, &status, statusBrush);
        DeleteObject(statusBrush);

        SetBkMode(dc, TRANSPARENT);
        HPEN separator = CreatePen(PS_SOLID, 1, PdfPP::ModernWin32::Theme::border);
        const auto oldPen = SelectObject(dc, separator);
        MoveToEx(dc, scaleDip(96), scaleDip(9), nullptr);
        LineTo(dc, scaleDip(96), scaleDip(49));
        MoveToEx(dc, scaleDip(270), scaleDip(9), nullptr);
        LineTo(dc, scaleDip(270), scaleDip(49));
        MoveToEx(dc, scaleDip(400), scaleDip(9), nullptr);
        LineTo(dc, scaleDip(400), scaleDip(49));
        MoveToEx(dc, 0, ribbonHeight - 1, nullptr);
        LineTo(dc, client.right, ribbonHeight - 1);
        if (sidebarVisible) {
            MoveToEx(dc, sidebarWidth, ribbonHeight, nullptr);
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
                pageIndex = selected;
                renderPage();
            }
            return 0;
        }
    }
    if (message == WM_COMMAND &&
        (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0)) {
        switch (LOWORD(wParam)) {
        case ID_OPEN: openDocument(); return 0;
        case ID_CLOSE: closeDocument(); return 0;
        case SC_CLOSE: PostMessageW(window, WM_CLOSE, 0, 0); return 0;
        case ID_FIRST_PAGE: if (document && pageCount > 0) { pageIndex = 0; renderPage(); } return 0;
        case ID_PREVIOUS: if (pageIndex > 0) { --pageIndex; renderPage(); } return 0;
        case ID_NEXT: if (pageIndex + 1 < pageCount) { ++pageIndex; renderPage(); } return 0;
        case ID_LAST_PAGE: if (document && pageCount > 0) { pageIndex = pageCount - 1; renderPage(); } return 0;
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
        case ID_FULLSCREEN: toggleFullscreen(window); return 0;
        case ID_HAND_TOOL:
            handTool = !handTool;
            PdfPP::ModernWin32::SetActionButtonAccent(handButton, handTool);
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
        case ID_BOOKMARK_CLOSE:
            sidebarVisible = false;
            { RECT client{}; GetClientRect(window, &client); updateLayout(client.right, client.bottom); }
            updateCommandState();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        }
    }
    if (message == WM_SIZE) {
        updateLayout(LOWORD(lParam), HIWORD(lParam));
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_RETURN && GetFocus() == pageEdit) {
        wchar_t text[32]{}; GetWindowTextW(pageEdit, text, 32); const int page = _wtoi(text) - 1;
        if (page >= 0 && page < pageCount) { pageIndex = page; renderPage(); } return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_RETURN && GetFocus() == searchEdit) {
        findText(); return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_F11) {
        toggleFullscreen(window); return 0;
    }
    if (message == WM_DESTROY) { closeDocument(); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}

}

namespace PdfPP::Win32 {

int RunReaderApplication(HINSTANCE instance, const int show) {
    PdfPP::ModernWin32::Initialize(instance);
    currentDpi = GetDpiForSystem();
    Gdiplus::GdiplusStartupInput gdiplusInput{}; ULONG_PTR gdiplusToken{};
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    WNDCLASSW canvasClass{}; canvasClass.hInstance = instance; canvasClass.lpfnWndProc = canvasProc;
    canvasClass.lpszClassName = L"PdfPP.Win32.Canvas"; canvasClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&canvasClass);
    WNDCLASSW windowClass{}; windowClass.hInstance = instance; windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = L"PdfPP.Win32.Reader"; windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr; RegisterClassW(&windowClass);
    mainWindow = CreateWindowW(windowClass.lpszClassName, L"Pdf++ Reader", WS_OVERLAPPEDWINDOW |
                               WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                               CW_USEDEFAULT, CW_USEDEFAULT, scaleDip(1200), scaleDip(850),
                               nullptr, nullptr, instance, nullptr);
    currentDpi = GetDpiForWindow(mainWindow);
    mainMenu = createMainMenu();
    SetMenu(mainWindow, mainMenu);
    openButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Open", ID_OPEN,
                                                         16, 47, 88, 30, true);
    previousButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"<", ID_PREVIOUS,
                                                             112, 47, 34, 30);
    nextButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L">", ID_NEXT,
                                                         152, 47, 34, 30);
    pageEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
                             WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER, 196, 49, 50, 26,
                             mainWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PAGE)), nullptr, nullptr);
    pageCaption = CreateWindowW(L"STATIC", L"Page:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                0, 0, 34, 18, mainWindow, nullptr, nullptr, nullptr);
    pageLabel = CreateWindowW(L"STATIC", L"/ 0", WS_CHILD | WS_VISIBLE, 252, 52, 52, 20, mainWindow, nullptr, nullptr, nullptr);
    zoomOutButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"−", ID_ZOOM_OUT,
                                                            310, 47, 34, 30);
    zoomLabel = CreateWindowW(L"STATIC", L"100%", WS_CHILD | WS_VISIBLE | SS_CENTER,
                              350, 52, 52, 20, mainWindow,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ZOOM_LABEL)), nullptr, nullptr);
    zoomInButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"+", ID_ZOOM_IN,
                                                          408, 47, 34, 30);
    fitButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Fit", ID_FIT_WIDTH,
                                                       448, 47, 76, 30);
    handButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Hand", ID_HAND_TOOL,
                                                        532, 47, 86, 30, true);
    findLabel = CreateWindowW(L"STATIC", L"Find:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                              0, 0, 36, 18, mainWindow, nullptr, nullptr, nullptr);
    searchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 700, 49, 240, 26,
                                 mainWindow, nullptr, nullptr, nullptr);
    findButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"Find", ID_FIND,
                                                         948, 47, 64, 30);
    statusLabel = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                16, 824, 1100, 20, mainWindow,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
    sidebarTitle = CreateWindowW(L"STATIC", L"Bookmarks", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                 0, PdfPP::ModernWin32::Layout::ribbonHeight,
                                 PdfPP::ModernWin32::Layout::sidebarWidth, 28, mainWindow,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SIDEBAR_TITLE)), nullptr, nullptr);
    bookmarkCloseButton = PdfPP::ModernWin32::CreateActionButton(mainWindow, instance, L"x", ID_BOOKMARK_CLOSE,
                                                                  0, 0, 24, 24);
    pageList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
                             WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                                 TVS_SHOWSELALWAYS | WS_VSCROLL,
                             0, PdfPP::ModernWin32::Layout::ribbonHeight + 28,
                             PdfPP::ModernWin32::Layout::sidebarWidth, 760, mainWindow,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PAGE_LIST)), instance, nullptr);
    TreeView_SetBkColor(pageList, PdfPP::ModernWin32::Theme::sidebar);
    TreeView_SetTextColor(pageList, PdfPP::ModernWin32::Theme::text);
    TreeView_SetLineColor(pageList, PdfPP::ModernWin32::Theme::border);
    canvas = CreateWindowW(canvasClass.lpszClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                           PdfPP::ModernWin32::Layout::sidebarWidth,
                           PdfPP::ModernWin32::Layout::ribbonHeight, 990, 760, mainWindow,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CANVAS)), instance, nullptr);
    PdfPP::ModernWin32::ApplyDarkMode(mainWindow);
    for (const HWND control : {pageEdit, pageLabel, searchEdit, statusLabel, sidebarTitle, pageList, zoomLabel,
                               pageCaption, findLabel}) {
        PdfPP::ModernWin32::ApplyDarkMode(control);
    }
    refreshApplicationFonts(currentDpi);
    PdfPP::ModernWin32::ApplyDarkMode(canvas);
    PdfPP::ModernWin32::SetActionButtonAccent(handButton, true);
    ShowWindow(mainWindow, show); UpdateWindow(mainWindow);
    RECT client{}; GetClientRect(mainWindow, &client); updateLayout(client.right, client.bottom);
    const ACCEL accelerators[] = {
        {FCONTROL | FVIRTKEY, 'O', ID_OPEN},
        {FCONTROL | FVIRTKEY, 'F', ID_FIND},
        {FVIRTKEY, VK_F3, ID_SEARCH_NEXT},
        {FVIRTKEY, VK_F11, ID_FULLSCREEN},
        {FVIRTKEY, VK_HOME, ID_FIRST_PAGE},
        {FVIRTKEY, VK_PRIOR, ID_PREVIOUS},
        {FVIRTKEY, VK_NEXT, ID_NEXT},
        {FVIRTKEY, VK_END, ID_LAST_PAGE},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_ZOOM_OUT},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_ZOOM_IN},
    };
    const HACCEL acceleratorTable = CreateAcceleratorTableW(
        const_cast<LPACCEL>(accelerators), static_cast<int>(std::size(accelerators)));
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
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
    Gdiplus::GdiplusShutdown(gdiplusToken); return static_cast<int>(message.wParam);
}

} // namespace PdfPP::Win32
