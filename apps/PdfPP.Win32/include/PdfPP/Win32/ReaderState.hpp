#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <PdfPP/ModernWin32.hpp>
#include <PdfPP/Win32/AppCommands.hpp>
#include <PdfPP/Win32/AppSettings.hpp>
#include <PdfPP/Win32/NativePdf.hpp>
#include <PdfPP/Win32/PageCache.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Shared application state and cross-module declarations. All reader state is
// intentionally module-global (as the original monolithic file had it); the
// split into .cpp files is purely organizational. See ARCHITECTURE.md.

namespace PdfPP::Win32 {

// Command IDs (ID_OPEN, ID_PRINT, WM_RENDER_COMPLETE, ...) are used pervasively
// across the reader modules; expose them unqualified inside this namespace.
using namespace PdfPP::Win32::Command;

// --- Window handles -------------------------------------------------------
inline HWND mainWindow{};
inline HWND pageEdit{};
inline HWND pageLabel{};
inline HWND searchEdit{};
inline HWND statusLabel{};
inline HWND canvas{};
inline HWND pageList{};
inline HWND sidebarTitle{};
inline HWND bookmarkCloseButton{};
inline HWND pageCaption{};
inline HWND findLabel{};
inline HWND openButton{};
inline HWND printButton{};
inline HWND previousButton{};
inline HWND nextButton{};
inline HWND zoomOutButton{};
inline HWND zoomInButton{};
inline HWND fitButton{};
inline HWND selectButton{};
inline HWND handButton{};
inline HWND findButton{};
inline HWND sidebarToggleButton{};
inline HWND zoomLabel{};
inline HMENU mainMenu{};
inline HICON appIcon{};

// --- Document / view state ------------------------------------------------
inline std::shared_ptr<NativePdfDocument> document;
inline int pageCount{};
inline int pageIndex{};
inline double zoom{ 1.0 };
inline int scrollX{};
inline int scrollY{};
// Height in pixels of every page at the current zoom/dpi, plus the running
// offset (top edge) of each page. Used to scale the scrollbar to the whole
// document and to translate a thumb drag into an absolute page position.
inline std::vector<int> pagePixelHeights;
inline std::vector<int> pagePixelOffsets;
inline int documentPixelHeight{};
inline int gapBetweenPages{};
inline int pendingScrollY{ -1 };
// The zoom/dpi the geometry was computed for. Recomputing every page size
// is O(pageCount) native calls, which stalls the UI thread; recompute only
// when the document or its scale actually changed.
inline double geometryZoom{ -1.0 };
inline unsigned int geometryDpi{ 0 };
inline std::shared_ptr<NativePdfDocument> geometryDocument;
inline std::vector<std::uint8_t> pixels;
inline std::vector<TextChunk> textChunks;
inline std::vector<std::size_t> searchHighlights;
inline std::vector<std::size_t> selectedChunks;
inline std::vector<HTREEITEM> tocItems;
inline HTREEITEM bookmarkRoot{};
inline bool sidebarVisible{ true };
inline bool updatingBookmarkSelection{};
inline std::wstring lastSearchQuery;
inline int lastSearchPage{ -1 };
inline int pixelWidth{};
inline int pixelHeight{};
inline int pixelStride{};

// --- Async render / open ---------------------------------------------------
inline std::thread renderThread;
inline std::mutex renderMutex;
inline std::atomic_bool renderReady{ false };
inline std::thread openThread;
inline std::mutex openMutex;
inline std::atomic_bool openReady{ false };

// --- UI metrics -------------------------------------------------------------
inline UINT currentDpi{ USER_DEFAULT_SCREEN_DPI };
inline HFONT regularUiFont{};
inline HFONT boldUiFont{};
// Ribbon group separator x positions, recomputed by updateLayout so they
// track the search field width as the window resizes.
inline int ribbonSep1{};
inline int ribbonSep2{};
inline int ribbonSep3{};

// --- Async results -----------------------------------------------------------
struct RenderResult final {
    std::shared_ptr<NativePdfDocument> document;
    PageBitmap bitmap;
    bool prefetch{};
    std::string error;
};
inline RenderResult renderResult;

struct OpenResult final {
    std::shared_ptr<NativePdfDocument> document;
    int pageCount{};
    std::wstring title;
    std::wstring path;
    std::string error;
};
inline OpenResult openResult;

// --- Caches / settings ---------------------------------------------------------
// Keep enough neighboring pages for a normal viewport; continuous painting
// reads these entries without moving ownership out of the cache.
inline PageCache pageCache{ 12 };
inline std::optional<PageBitmap> continuousNextPage;
inline AppSettings settings;
inline HMENU recentMenu{};
inline HMENU favoritesMenuHandle{};
inline std::wstring currentFilePath;
inline int openPageAfterLoad{ -1 };

enum class PageLayoutMode { SinglePage, ContinuousNavigation };

struct TabState final {
    std::wstring path;
    std::wstring title;
    std::shared_ptr<NativePdfDocument> document;
    int pageCount{};
    int pageIndex{};
    double zoom{ 1.0 };
    int scrollX{};
    int scrollY{};
    std::vector<std::uint8_t> pixels;
    int pixelWidth{};
    int pixelHeight{};
    int pixelStride{};
    std::vector<TextChunk> textChunks;
    std::vector<std::size_t> searchHighlights;
    std::vector<std::size_t> selectedChunks;
    std::wstring lastSearchQuery;
    int lastSearchPage{ -1 };
    PageLayoutMode layout{ PageLayoutMode::ContinuousNavigation };
};

inline std::vector<TabState> tabs;
inline int activeTab{ -1 };
inline HWND tabBar{};
inline bool pendingOpenCreatesTab{};
inline bool switchingTab{};
inline std::wstring startupOpenPath;
inline int hoverCloseTab{ -1 };
inline int pendingCloseTabIndex{ -1 };

// --- Input / view interaction ------------------------------------------------
struct ZoomAnchor final {
    bool valid{};
    int clientX{};
    int clientY{};
    double pageX{};
    double pageY{};
};
inline ZoomAnchor zoomAnchor;
inline bool zoomRenderPending{};
inline ULONGLONG zoomRequestTick{};
inline constexpr UINT kZoomDebounceMs = 80;

enum class VerticalPageArrival { Top, Bottom };

struct PageArrivalRequest final {
    int page{};
    VerticalPageArrival vertical{ VerticalPageArrival::Top };
};

inline bool handTool{};
// Default to continuous scrolling, matching Adobe Acrobat's default
// "Scrolling" page layout, instead of a single static page per view.
inline PageLayoutMode pageLayoutMode{ PageLayoutMode::ContinuousNavigation };
inline std::optional<PageArrivalRequest> pageArrivalRequest;
inline bool panning{};
inline bool selectingText{};
inline POINT panStart{};
inline int panStartX{};
inline int panStartY{};
inline POINT selectionStart{};
inline POINT selectionEnd{};
inline bool fullscreen{};
inline LONG fullscreenStyle{};
inline LONG fullscreenExStyle{};
inline WINDOWPLACEMENT fullscreenPlacement{ sizeof(fullscreenPlacement) };

// Guards against an inertia/momentum "settle" wheel or pan tick landing
// right after a page-boundary crossing.
inline ULONGLONG lastPageCrossTick{};
inline constexpr ULONGLONG kPageCrossDebounceMs = 40;

// --- Cross-module declarations (defined in the module .cpp files) -------------
bool tryCrossPageBoundary();
void updateCanvasScrollbars();
void updatePageGeometry();
void syncRenderedPageHeight(int page);
void updateLayout(int width, int height);
void renderPage();
void updateCommandState();
void prefetchNextPage();
void prefetchFurtherPage();
int pageLeft(const RECT& client, int width);
int pageAtScrollOffset(int offset);
bool navigatePage(int targetPage, VerticalPageArrival arrival);
void goToPage(int targetPage);
void setCanvasScroll(int bar, int position);
bool scrollContinuousBy(int delta);
int wheelScrollDistance(int viewport);
void captureActiveTabState();
void applyTabState(TabState& tab);
void rebuildTabBar();
void switchToTab(int index);
void closeActiveTab();
void closeTabAt(int index);
void requestCloseTab(int index);
void openPath(const std::wstring& path);
void openDocument();
void toggleFullscreen(HWND window);
int scaleDip(const int value);
void addWheelDelta(double& remainder, const int rawDelta, const int viewport);
std::wstring utf8ToWide(const char* text);
void setStatus(const std::wstring& value);
void rebuildRecentMenu();
void persistSettings();
void rememberRecentFile(const std::wstring& path);
void rebuildFavoritesMenu();
void toggleCurrentFavorite();
void updateZoomLabel();
double textPixelScale();
RECT chunkClientRect(const TextChunk& chunk, const RECT& client, const int pageTop);
void updateSearchHighlights();
void refreshTextGeometry();
void selectTextChunks(const RECT& selection, const RECT& client, const int pageTop);
void updateLiveSelection();
void fillOverlay(HDC dc, const RECT& rect, BYTE alpha, COLORREF color);
void copySelectedText();
void selectBookmarkPage();
void populateBookmarkTree(const std::wstring&);
void updatePageControls();
void rememberCurrentPage();
void cacheCurrentPageAndRelease();
bool hasCachedPage(const int requestedPage, const double requestedZoom);
void rememberNativePage(RenderResult& result);
void finishPageLayout();
bool applyCachedPage(const int requestedPage, const double requestedZoom);
void startRender(const int requestedPage, const bool prefetch);
void applyRenderedPage(PageBitmap bitmap);
void closeDocument();
void saveSettingsOnExit();
void setZoom(const double value);
void setZoomAtPoint(const double value, POINT point);
void requestZoomRender();
void printCurrentPage();
std::wstring tabLabelFor(const TabState& tab);
void findText();
void fitToWidth();
void fitToPage();
void showAboutDialog();
void showDocumentProperties();
HICON loadApplicationIcon();
HMENU createMainMenu();
void refreshApplicationFonts(UINT dpi);
void captureCanvasCenterAnchor();
void applyDpiChange(HWND window, UINT dpi);
int tabStripWidth(const int clientWidth, const int count);
RECT tabItemRectFor(const int index, const int count, const int clientWidth);
RECT tabCloseRectFor(const RECT& item);
void paintTabItem(HWND, HDC dc, const int index, const RECT& item);
int tabHitTest(const int x, const int clientWidth);

LRESULT CALLBACK tabBarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

} // namespace PdfPP::Win32
