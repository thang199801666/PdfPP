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
#include <PdfPP/Win32/ReaderPdfDocument.hpp>
#include <PdfPP/Win32/PageCache.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Shared application state and cross-module declarations. All reader state is
// intentionally module-global (as the original monolithic file had it); the
// split into .cpp files is purely organizational. See ARCHITECTURE.md.

namespace PdfPP::Win32 {

// Command IDs (ID_OPEN, ID_PRINT, WM_RENDER_COMPLETE, ...) are used pervasively
// across the reader modules; expose them unqualified inside this namespace.
using namespace PdfPP::Win32::Command;

struct TextPosition final {
    bool valid{};
    std::size_t chunk{};
    std::size_t offset{};
};

struct TextSelectionSpan final {
    std::size_t chunk{};
    std::size_t begin{};
    std::size_t end{};
};


enum class FindScope { WholePage, CurrentPage };
enum class FindCaseMode { CaseInsensitive, CaseSensitive };
enum class FindPatternMode { Normal, RegularExpression };
enum class RightPanelMode { Tools, Comments };

// --- Window handles -------------------------------------------------------
inline HWND mainWindow{};
inline HWND pageEdit{};
inline HWND pageLabel{};
inline HWND searchEdit{};
inline HWND findPanel{};
inline HWND findCloseButton{};
inline HWND statusLabel{};
inline HWND canvas{};
inline HWND pageList{};
inline HWND sidebarPanel{};
inline HWND sidebarTitle{};
inline HWND bookmarkCloseButton{};
inline HWND toolsPanel{};
inline HWND toolsTitle{};
inline HWND toolsSearchEdit{};
inline HWND toolsTree{};
inline HWND pageCaption{};
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
inline bool findPanelVisible{};
inline FindScope findScope{ FindScope::WholePage };
inline FindCaseMode findCaseMode{ FindCaseMode::CaseInsensitive };
inline FindPatternMode findPatternMode{ FindPatternMode::Normal };
inline bool findIncludeComments{};
inline bool findIncludeBookmarks{};
inline std::uint64_t findOptionsRevision{};
inline std::uint64_t lastSearchOptionsRevision{};
inline HWND sidebarToggleButton{};
inline HWND toolsToggleButton{};
inline HWND zoomLabel{};
inline HMENU mainMenu{};
inline HICON appIcon{};

// --- Document / view state ------------------------------------------------
inline std::shared_ptr<ReaderPdfDocument> document;
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
// Page heights sampled once at a high fixed scale. Zoom changes can then
// rebuild document offsets with inexpensive arithmetic instead of issuing a
// native page-size query for every page on every Ctrl+wheel gesture.
inline std::vector<int> pageGeometryBaseHeights;
inline constexpr double kPageGeometryBaseScale = 16.0;
inline int documentPixelHeight{};
inline int gapBetweenPages{};
inline int pendingScrollY{ -1 };
// The zoom/dpi the geometry was computed for. Recomputing every page size
// is O(pageCount) native calls, which stalls the UI thread; recompute only
// when the document or its scale actually changed.
inline double geometryZoom{ -1.0 };
inline unsigned int geometryDpi{ 0 };
inline std::shared_ptr<ReaderPdfDocument> geometryDocument;
inline std::vector<std::uint8_t> pixels;
inline std::vector<TextChunk> textChunks;
inline std::vector<std::size_t> searchHighlights;
inline std::vector<TextSelectionSpan> selectedTextSpans;
inline TextPosition selectionAnchor;
inline TextPosition selectionFocus;
inline int textGeometryPage{ -1 };
inline int textGeometryRequestPage{ -1 };
inline constexpr UINT kTextGeometryDebounceMs = 75;
inline std::vector<HTREEITEM> tocItems;
// TOC data belongs to the active document/tab.  Keep it separate from the
// TreeView handles so switching tabs never reparses or accidentally reuses
// another document's outline resolver state.
inline std::vector<TocItem> currentToc;
inline HTREEITEM bookmarkRoot{};
inline bool sidebarVisible{ true };
inline bool toolsVisible{ true };
// Resizable divider between the Table of Contents and the document canvas.
// Keep the desired width in DIPs so it remains stable across DPI changes.
inline int sidebarWidthDip{ PdfPP::ModernWin32::Layout::sidebarWidth };
inline bool sidebarSplitterDragging{};
inline bool readerWindowSizing{};
inline constexpr int kSidebarSplitterWidthDip = 5;
inline constexpr int kSidebarMinWidthDip = 170;
inline constexpr int kSidebarMaxWidthDip = 520;
inline constexpr int kCanvasMinWidthDip = 260;
inline int toolsWidthDip{ 292 };
inline constexpr int kToolsMinWidthDip = 240;
inline constexpr int kToolsMaxWidthDip = 420;
inline bool updatingBookmarkSelection{};
inline std::wstring lastSearchQuery;
inline std::wstring toolsSearchQuery;
inline RightPanelMode rightPanelMode{ RightPanelMode::Tools };
inline std::vector<CommentItem> currentPageComments;
inline std::vector<HTREEITEM> commentItems;
inline int commentPage{ -1 };
inline std::uint32_t activeCommentObjectNumber{};
inline int lastSearchPage{ -1 };
inline int pixelWidth{};
inline int pixelHeight{};
inline int pixelStride{};
inline int pixelPage{ -1 };
// Scale identity of the bitmap currently held in `pixels`. During a Ctrl+
// wheel gesture `zoom` changes immediately, while this bitmap intentionally
// remains at the last completed render and is stretched as a lightweight
// preview until the debounced high-quality render finishes.
inline double pixelZoom{ 1.0 };
inline unsigned int pixelDpi{ USER_DEFAULT_SCREEN_DPI };

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
inline HFONT pageUiFont{};
inline HFONT pageBoldUiFont{};
// Ribbon group separator x positions, recomputed by updateLayout so they
// track the search field width as the window resizes.
inline int ribbonSep1{};
inline int ribbonSep2{};
inline int ribbonSep3{};

// --- Async results -----------------------------------------------------------
struct RenderResult final {
    std::shared_ptr<ReaderPdfDocument> document;
    PageBitmap bitmap;
    bool prefetch{};
    std::string error;
};
inline RenderResult renderResult;

struct OpenResult final {
    std::shared_ptr<ReaderPdfDocument> document;
    std::vector<TocItem> toc;
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
inline AppSettings settings;
inline HMENU recentMenu{};
inline HMENU favoritesMenuHandle{};
inline std::wstring currentFilePath;
inline int openPageAfterLoad{ -1 };

enum class PageLayoutMode { SinglePage, ContinuousNavigation };

struct TabState final {
    std::wstring path;
    std::wstring title;
    std::shared_ptr<ReaderPdfDocument> document;
    std::vector<TocItem> toc;
    int pageCount{};
    int pageIndex{};
    double zoom{ 1.0 };
    int scrollX{};
    int scrollY{};
    std::vector<std::uint8_t> pixels;
    int pixelWidth{};
    int pixelHeight{};
    int pixelStride{};
    int pixelPage{ -1 };
    double pixelZoom{ 1.0 };
    unsigned int pixelDpi{ USER_DEFAULT_SCREEN_DPI };
    std::vector<TextChunk> textChunks;
    std::vector<std::size_t> searchHighlights;
    std::vector<TextSelectionSpan> selectedTextSpans;
    TextPosition selectionAnchor;
    TextPosition selectionFocus;
    int textGeometryPage{ -1 };
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
inline int hoverTab{ -1 };
inline int hoverCloseTab{ -1 };
inline int pressedCloseTab{ -1 };
inline int pendingCloseTabIndex{ -1 };
inline constexpr int kTabBarHeightDip = 32;

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
inline constexpr UINT kZoomDebounceMs = 120;

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
void showFindPanel(bool show);
void showFindOptionsMenu();
void resetFindSearchState();
[[nodiscard]] bool findPatternMatches(std::wstring_view text);
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
HCURSOR handDragCursor();
void addWheelDelta(double& remainder, const int rawDelta, const int viewport);
std::wstring utf8ToWide(const char* text);
void setStatus(const std::wstring& value);
void rebuildRecentMenu();
void persistSettings();
void rememberRecentFile(const std::wstring& path);
void rebuildFavoritesMenu();
void toggleCurrentFavorite();
void populateToolsTree();
void populateCommentsTree();
void showCommentsPanelForPage(int page, std::optional<std::uint32_t> focusObjectNumber = std::nullopt);
void showToolsPanelCatalog();
void executeToolCommand(int command);
void updateZoomLabel();
double textPixelScale();
RECT chunkClientRect(const TextChunk& chunk, const RECT& client, const int pageTop);
void updateSearchHighlights();
void refreshTextGeometry();
void requestTextGeometryRefresh();
TextPosition hitTestTextPosition(POINT point, const RECT& client, int pageTop, bool nearest);
RECT selectionSpanClientRect(const TextSelectionSpan& span, const RECT& client, int pageTop);
void clearTextSelection();
void beginTextSelection(POINT point, bool extendExisting);
void selectWordAt(POINT point);
void selectAllText();
void updateLiveSelection();
void fillOverlay(HDC dc, const RECT& rect, BYTE alpha, COLORREF color);
void copySelectedText();
void selectBookmarkPage();
void populateBookmarkTree(const std::wstring&);
void updatePageControls();
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
void mergePdfDocuments();
void extractPdfPages();
void splitPdfDocument();
void deletePdfPages();
void duplicatePdfPages();
void moveCurrentPdfPage();
void reorderPdfPages();
void reversePdfPages();
void addPdfPassword();
void removePdfPassword();
void changePdfPassword();
void crackPasswordAndOpenPdf();
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

LRESULT CALLBACK findPanelProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK tabBarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

} // namespace PdfPP::Win32
