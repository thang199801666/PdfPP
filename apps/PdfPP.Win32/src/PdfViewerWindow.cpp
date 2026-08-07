#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>

#include <PdfViewerWindow.hpp>
#include <PdfPP/ModernWin32.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Rendering/PdfPageRenderer.hpp>

#include "../resources/resource.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "bcrypt.lib")

namespace PdfPP::Win32 {
namespace {

constexpr wchar_t kViewerWindowClass[] = L"PdfPP.Win32.ReaderWindow";
constexpr wchar_t kCanvasWindowClass[] = L"PdfPP.Win32.PageCanvas";
constexpr wchar_t kPasswordWindowClass[] = L"PdfPP.Win32.PasswordPrompt";
constexpr UINT kRenderCompletedMessage = WM_APP + 41U;
constexpr UINT_PTR kResizeRenderTimer = 201U;
constexpr int kToolbarHeight = 46;
constexpr int kStatusHeight = 25;
constexpr int kSidebarWidth = 236;
constexpr int kPageMargin = 24;
constexpr double kMinimumZoom = 0.10;
constexpr double kMaximumZoom = 8.00;

[[nodiscard]] std::wstring utf8ToWide(const std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count > 0) {
        std::wstring result(static_cast<std::size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), count);
        return result;
    }
    const int legacyCount = MultiByteToWideChar(CP_ACP, 0,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (legacyCount <= 0) return {};
    std::wstring result(static_cast<std::size_t>(legacyCount), L'\0');
    MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()),
        result.data(), legacyCount);
    return result;
}

[[nodiscard]] std::string wideToUtf8(const std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

[[nodiscard]] std::wstring errorToWide(const std::exception& error) {
    return utf8ToWide(error.what());
}

void showError(const HWND owner, const wchar_t* title, const std::wstring& message) {
    MessageBoxW(owner, message.empty() ? L"Unknown error." : message.c_str(), title,
                MB_OK | MB_ICONERROR);
}

struct PasswordPromptState final {
    HWND window{};
    HWND edit{};
    bool finished{};
    bool accepted{};
    std::wstring value;
};

LRESULT CALLBACK passwordWindowProcedure(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PasswordPromptState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<PasswordPromptState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = window;
    }
    if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(window, GWLP_HINSTANCE));
        const HWND label = CreateWindowExW(0, L"STATIC",
            L"This PDF is encrypted. Enter the password:",
            WS_CHILD | WS_VISIBLE, 16, 16, 338, 20, window, nullptr, instance, nullptr);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
            16, 43, 338, 25, window, reinterpret_cast<HMENU>(1001), instance, nullptr);
        const HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            194, 82, 76, 27, window, reinterpret_cast<HMENU>(IDOK), instance, nullptr);
        const HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            278, 82, 76, 27, window, reinterpret_cast<HMENU>(IDCANCEL), instance, nullptr);
        PdfPP::ModernWin32::ApplyFont(label);
        PdfPP::ModernWin32::ApplyFont(state->edit);
        PdfPP::ModernWin32::ApplyFont(ok);
        PdfPP::ModernWin32::ApplyFont(cancel);
        SetFocus(state->edit);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            const int length = GetWindowTextLengthW(state->edit);
            std::wstring value(static_cast<std::size_t>((std::max)(length, 0)) + 1U, L'\0');
            if (length > 0) GetWindowTextW(state->edit, value.data(), length + 1);
            value.resize(static_cast<std::size_t>((std::max)(length, 0)));
            state->value = std::move(value);
            state->accepted = true;
            state->finished = true;
            DestroyWindow(window);
            return 0;
        }
        case IDCANCEL:
            state->accepted = false;
            state->finished = true;
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        state->accepted = false;
        state->finished = true;
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

[[nodiscard]] std::optional<std::wstring> promptForPassword(
    const HWND owner, const HINSTANCE instance) {
    PasswordPromptState state;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    constexpr int width = 382;
    constexpr int height = 153;
    const int x = static_cast<int>(ownerRect.left)
        + (std::max)(0, (static_cast<int>(ownerRect.right) - static_cast<int>(ownerRect.left) - width) / 2);
    const int y = static_cast<int>(ownerRect.top)
        + (std::max)(0, (static_cast<int>(ownerRect.bottom) - static_cast<int>(ownerRect.top) - height) / 2);

    HWND prompt = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kPasswordWindowClass, L"PDF password",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, owner, nullptr, instance, &state);
    if (prompt == nullptr) return std::nullopt;

    EnableWindow(owner, FALSE);
    ShowWindow(prompt, SW_SHOW);
    UpdateWindow(prompt);

    bool quitSeen = false;
    int quitCode = 0;
    MSG message{};
    while (!state.finished) {
        const BOOL status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            if (status == 0) {
                quitSeen = true;
                quitCode = static_cast<int>(message.wParam);
            }
            state.finished = true;
            state.accepted = false;
            break;
        }
        if (!IsDialogMessageW(prompt, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (quitSeen) PostQuitMessage(quitCode);
    if (!state.accepted) return std::nullopt;
    return state.value;
}

[[nodiscard]] HMENU createApplicationMenu() {
    HMENU menu = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(fileMenu, MF_STRING, ID_FILE_CLOSE, L"&Close");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, ID_FILE_EXIT, L"E&xit");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");

    HMENU viewMenu = CreatePopupMenu();
    AppendMenuW(viewMenu, MF_STRING, ID_VIEW_PREVIOUS_PAGE, L"Previous page\tLeft");
    AppendMenuW(viewMenu, MF_STRING, ID_VIEW_NEXT_PAGE, L"Next page\tRight");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, ID_VIEW_ZOOM_OUT, L"Zoom out\tCtrl+-");
    AppendMenuW(viewMenu, MF_STRING, ID_VIEW_ZOOM_IN, L"Zoom in\tCtrl++");
    AppendMenuW(viewMenu, MF_STRING, ID_VIEW_ACTUAL_SIZE, L"Actual size\tCtrl+0");
    AppendMenuW(viewMenu, MF_STRING, ID_VIEW_FIT_PAGE, L"Fit page\tCtrl+1");
    AppendMenuW(viewMenu, MF_STRING, ID_VIEW_FIT_WIDTH, L"Fit width\tCtrl+2");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING | MF_CHECKED, ID_VIEW_TOGGLE_BOOKMARKS,
                L"Bookmarks\tCtrl+B");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"&View");
    return menu;
}

} // namespace

struct PdfViewerWindow::Impl final {
    enum class ZoomMode { Custom, FitPage, FitWidth };

    struct RenderRequest final {
        std::uint64_t generation{};
        std::filesystem::path path;
        std::string password;
        std::size_t pageIndex{};
        double dpi{96.0};
    };

    struct RenderedPage final {
        std::uint64_t generation{};
        std::size_t pageIndex{};
        int width{};
        int height{};
        int stride{};
        std::vector<std::uint8_t> bgra;
        std::wstring error;
    };

    PdfViewerWindow* owner{};
    HINSTANCE instance{};
    HWND window{};
    HWND openButton{};
    HWND previousButton{};
    HWND nextButton{};
    HWND zoomOutButton{};
    HWND zoomInButton{};
    HWND actualButton{};
    HWND fitPageButton{};
    HWND fitWidthButton{};
    HWND pageEdit{};
    HWND pageCountLabel{};
    HWND bookmarkTree{};
    HWND canvas{};
    HWND statusText{};

    HBRUSH toolbarBrush{CreateSolidBrush(PdfPP::ModernWin32::Theme::toolbar)};
    HBRUSH canvasBrush{CreateSolidBrush(PdfPP::ModernWin32::Theme::canvas)};
    HBRUSH controlBrush{CreateSolidBrush(PdfPP::ModernWin32::Theme::control)};

    std::unique_ptr<CPPPdf::PdfDocument> document;
    std::filesystem::path path;
    std::string password;
    std::size_t pageCount{};
    std::size_t currentPage{};
    double zoom{1.0};
    ZoomMode zoomMode{ZoomMode::FitPage};
    bool bookmarksVisible{true};
    bool rendering{};
    int horizontalScroll{};
    int verticalScroll{};
    std::unique_ptr<RenderedPage> renderedPage;

    std::mutex renderMutex;
    std::condition_variable renderCondition;
    std::optional<RenderRequest> pendingRender;
    std::jthread renderThread;
    std::atomic_bool shuttingDown{false};
    std::uint64_t renderGeneration{};

    explicit Impl(PdfViewerWindow* ownerValue) : owner(ownerValue) {}

    ~Impl() {
        stopWorker();
        if (toolbarBrush) DeleteObject(toolbarBrush);
        if (canvasBrush) DeleteObject(canvasBrush);
        if (controlBrush) DeleteObject(controlBrush);
    }

    void startWorker() {
        if (renderThread.joinable()) return;
        renderThread = std::jthread([this](const std::stop_token stopToken) {
            renderWorker(stopToken);
        });
    }

    void stopWorker() {
        if (!renderThread.joinable()) return;
        shuttingDown.store(true, std::memory_order_release);
        renderThread.request_stop();
        renderCondition.notify_all();
        renderThread.join();

        if (window != nullptr) {
            MSG message{};
            while (PeekMessageW(&message, window, kRenderCompletedMessage,
                                kRenderCompletedMessage, PM_REMOVE)) {
                delete reinterpret_cast<RenderedPage*>(message.lParam);
            }
        }
    }

    void renderWorker(const std::stop_token stopToken) {
        for (;;) {
            RenderRequest request;
            {
                std::unique_lock lock(renderMutex);
                renderCondition.wait(lock, [&] {
                    return stopToken.stop_requested() || pendingRender.has_value();
                });
                if (stopToken.stop_requested()) return;
                request = std::move(*pendingRender);
                pendingRender.reset();
            }

            auto result = std::make_unique<RenderedPage>();
            result->generation = request.generation;
            result->pageIndex = request.pageIndex;
            try {
                CPPPdf::PdfReaderOptions openOptions;
                openOptions.password = request.password;
                CPPPdf::PdfDocument renderDocument =
                    CPPPdf::PdfDocument::Open(request.path, openOptions);
                if (renderDocument.IsPasswordRequired()) {
                    throw CPPPdf::PdfException(CPPPdf::PdfErrorCode::PasswordRequired,
                        "The PDF requires a password.");
                }

                CPPPdf::PdfRenderOptions renderOptions;
                renderOptions.dpi = request.dpi;
                renderOptions.antiAliasSamples = 1U;
                renderOptions.maximumDimension = 16384U;
                CPPPdf::PdfBitmap bitmap = CPPPdf::PdfPageRenderer::Render(
                    renderDocument, request.pageIndex, renderOptions);

                result->width = static_cast<int>(bitmap.GetWidth());
                result->height = static_cast<int>(bitmap.GetHeight());
                result->stride = result->width * 4;
                const auto pixels = bitmap.GetPixels();
                result->bgra.resize(pixels.size());
                const auto* source = reinterpret_cast<const std::uint8_t*>(pixels.data());
                for (std::size_t offset = 0; offset + 3U < pixels.size(); offset += 4U) {
                    result->bgra[offset] = source[offset + 2U];
                    result->bgra[offset + 1U] = source[offset + 1U];
                    result->bgra[offset + 2U] = source[offset];
                    result->bgra[offset + 3U] = 255U;
                }
            } catch (const std::exception& error) {
                result->error = errorToWide(error);
            }

            if (stopToken.stop_requested() || shuttingDown.load(std::memory_order_acquire)) return;
            RenderedPage* raw = result.release();
            if (!PostMessageW(window, kRenderCompletedMessage, 0,
                              reinterpret_cast<LPARAM>(raw))) {
                delete raw;
            }
        }
    }

    [[nodiscard]] bool createControls() {
        SetMenu(window, createApplicationMenu());

        openButton = PdfPP::ModernWin32::CreateActionButton(
            window, instance, L"Open", ID_FILE_OPEN, 8, 8, 64, 29, true);
        previousButton = PdfPP::ModernWin32::CreateActionButton(
            window, instance, L"<", ID_VIEW_PREVIOUS_PAGE, 82, 8, 34, 29);
        nextButton = PdfPP::ModernWin32::CreateActionButton(
            window, instance, L">", ID_VIEW_NEXT_PAGE, 120, 8, 34, 29);

        pageEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
            164, 10, 48, 25, window,
            reinterpret_cast<HMENU>(ID_PAGE_EDIT), instance, nullptr);
        pageCountLabel = CreateWindowExW(0, L"STATIC", L"of 0",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            217, 9, 56, 27, window,
            reinterpret_cast<HMENU>(ID_PAGE_COUNT), instance, nullptr);

        zoomOutButton = PdfPP::ModernWin32::CreateActionButton(
            window, instance, L"-", ID_VIEW_ZOOM_OUT, 285, 8, 34, 29);
        zoomInButton = PdfPP::ModernWin32::CreateActionButton(
            window, instance, L"+", ID_VIEW_ZOOM_IN, 323, 8, 34, 29);
        actualButton = PdfPP::ModernWin32::CreateActionButton(
            window, instance, L"100%", ID_VIEW_ACTUAL_SIZE, 365, 8, 56, 29);
        fitPageButton = PdfPP::ModernWin32::CreateActionButton(
            window, instance, L"Fit page", ID_VIEW_FIT_PAGE, 427, 8, 72, 29);
        fitWidthButton = PdfPP::ModernWin32::CreateActionButton(
            window, instance, L"Fit width", ID_VIEW_FIT_WIDTH, 505, 8, 76, 29);

        bookmarkTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT |
            TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
            0, kToolbarHeight, kSidebarWidth, 400, window,
            reinterpret_cast<HMENU>(ID_BOOKMARK_TREE), instance, nullptr);
        SetWindowTheme(bookmarkTree, L"Explorer", nullptr);

        canvas = CreateWindowExW(0, kCanvasWindowClass, L"",
            WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_CLIPCHILDREN,
            kSidebarWidth, kToolbarHeight, 600, 400, window,
            reinterpret_cast<HMENU>(ID_CANVAS), instance, this);

        statusText = CreateWindowExW(0, L"STATIC",
            L"Open a PDF file to begin.", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            8, 500, 600, kStatusHeight, window,
            reinterpret_cast<HMENU>(ID_STATUS_TEXT), instance, nullptr);

        if (!openButton || !previousButton || !nextButton || !pageEdit ||
            !pageCountLabel || !zoomOutButton || !zoomInButton || !actualButton ||
            !fitPageButton || !fitWidthButton || !bookmarkTree || !canvas || !statusText) {
            return false;
        }

        PdfPP::ModernWin32::ApplyFont(pageEdit);
        PdfPP::ModernWin32::ApplyFont(pageCountLabel);
        PdfPP::ModernWin32::ApplyFont(bookmarkTree);
        PdfPP::ModernWin32::ApplyFont(statusText);
        SetWindowSubclass(pageEdit, pageEditSubclassProcedure, 1U,
                          reinterpret_cast<DWORD_PTR>(this));
        updateDocumentControls();
        return true;
    }

    static LRESULT CALLBACK pageEditSubclassProcedure(
        HWND edit, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData) {
        auto* self = reinterpret_cast<Impl*>(referenceData);
        if (message == WM_KEYDOWN && wParam == VK_RETURN && self != nullptr) {
            self->goToPageFromEdit();
            SetFocus(self->canvas);
            return 0;
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(edit, pageEditSubclassProcedure, subclassId);
        }
        return DefSubclassProc(edit, message, wParam, lParam);
    }

    void layoutControls(const int width, const int height) {
        const int contentTop = kToolbarHeight;
        const int contentBottom = (std::max)(contentTop, height - kStatusHeight);
        const int contentHeight = (std::max)(0, contentBottom - contentTop);
        const int sidebar = bookmarksVisible ? (std::min)(kSidebarWidth, width / 2) : 0;

        MoveWindow(bookmarkTree, 0, contentTop, sidebar, contentHeight, TRUE);
        ShowWindow(bookmarkTree, bookmarksVisible ? SW_SHOW : SW_HIDE);
        MoveWindow(canvas, sidebar, contentTop, (std::max)(0, width - sidebar),
                   contentHeight, TRUE);
        MoveWindow(statusText, 8, contentBottom,
                   (std::max)(0, width - 16), kStatusHeight, TRUE);
        updateCanvasScrollbars();
    }

    void updateDocumentControls() {
        const bool hasDocument = document != nullptr && pageCount > 0U;
        EnableWindow(previousButton, hasDocument && currentPage > 0U);
        EnableWindow(nextButton, hasDocument && currentPage + 1U < pageCount);
        EnableWindow(pageEdit, hasDocument);
        EnableWindow(zoomOutButton, hasDocument);
        EnableWindow(zoomInButton, hasDocument);
        EnableWindow(actualButton, hasDocument);
        EnableWindow(fitPageButton, hasDocument);
        EnableWindow(fitWidthButton, hasDocument);

        wchar_t pageText[32]{};
        wchar_t countText[48]{};
        if (hasDocument) {
            swprintf_s(pageText, L"%zu", currentPage + 1U);
            swprintf_s(countText, L"of %zu", pageCount);
        } else {
            wcscpy_s(pageText, L"0");
            wcscpy_s(countText, L"of 0");
        }
        SetWindowTextW(pageEdit, pageText);
        SetWindowTextW(pageCountLabel, countText);
    }

    void updateStatus(const std::wstring& prefix = {}) {
        if (statusText == nullptr) return;
        if (document == nullptr) {
            SetWindowTextW(statusText,
                prefix.empty() ? L"Open a PDF file to begin." : prefix.c_str());
            return;
        }
        wchar_t zoomText[32]{};
        swprintf_s(zoomText, L"%.0f%%", zoom * 100.0);
        std::wstring text;
        if (!prefix.empty()) {
            text = prefix;
            text += L"   |   ";
        }
        text += path.filename().wstring();
        text += L"   |   Page ";
        text += std::to_wstring(currentPage + 1U);
        text += L" of ";
        text += std::to_wstring(pageCount);
        text += L"   |   ";
        text += zoomText;
        text += L"   |   PDF ";
        text += utf8ToWide(document->GetVersion());
        if (document->IsEncrypted()) text += L"   |   Encrypted";
        SetWindowTextW(statusText, text.c_str());
    }

    void populateBookmarks(const std::vector<CPPPdf::PdfOutlineEntry>& outlines) {
        TreeView_DeleteAllItems(bookmarkTree);
        std::wstring rootText = path.filename().wstring();
        TVINSERTSTRUCTW rootInsert{};
        rootInsert.hParent = TVI_ROOT;
        rootInsert.hInsertAfter = TVI_LAST;
        rootInsert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
        rootInsert.item.pszText = rootText.data();
        rootInsert.item.lParam = 0;
        rootInsert.item.stateMask = TVIS_EXPANDED;
        rootInsert.item.state = TVIS_EXPANDED;
        const HTREEITEM root = TreeView_InsertItem(bookmarkTree, &rootInsert);

        if (outlines.empty()) {
            std::wstring emptyText = L"No bookmarks";
            TVINSERTSTRUCTW emptyInsert{};
            emptyInsert.hParent = root;
            emptyInsert.hInsertAfter = TVI_LAST;
            emptyInsert.item.mask = TVIF_TEXT;
            emptyInsert.item.pszText = emptyText.data();
            TreeView_InsertItem(bookmarkTree, &emptyInsert);
            TreeView_Expand(bookmarkTree, root, TVE_EXPAND);
            return;
        }

        std::vector<HTREEITEM> parents;
        parents.push_back(root);
        for (const auto& outline : outlines) {
            const std::size_t depth = std::min<std::size_t>(outline.depth, 64U);
            while (parents.size() > depth + 1U) parents.pop_back();
            const HTREEITEM parent = parents.empty() ? root : parents.back();
            std::wstring title = utf8ToWide(outline.title);
            if (title.empty()) title = L"Untitled bookmark";

            TVINSERTSTRUCTW insert{};
            insert.hParent = parent;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM;
            insert.item.pszText = title.data();
            insert.item.lParam = outline.destinationPageIndex.has_value()
                ? static_cast<LPARAM>(*outline.destinationPageIndex + 1U) : 0;
            const HTREEITEM item = TreeView_InsertItem(bookmarkTree, &insert);
            if (item != nullptr) {
                if (parents.size() == depth + 1U) parents.push_back(item);
                else if (parents.size() > depth + 1U) parents[depth + 1U] = item;
            }
        }
        TreeView_Expand(bookmarkTree, root, TVE_EXPAND);
    }

    [[nodiscard]] bool openDocument(const std::filesystem::path& newPath) {
        if (newPath.empty()) return false;
        updateStatus(L"Opening document...");
        UpdateWindow(window);

        try {
            auto opened = std::make_unique<CPPPdf::PdfDocument>(
                CPPPdf::PdfDocument::Open(newPath));
            std::string suppliedPassword;
            while (opened->IsPasswordRequired()) {
                const auto entered = promptForPassword(window, instance);
                if (!entered.has_value()) {
                    updateStatus(L"Open cancelled");
                    return false;
                }
                suppliedPassword = wideToUtf8(*entered);
                if (!opened->AuthenticatePassword(suppliedPassword)) {
                    MessageBoxW(window, L"The password is not valid.",
                                L"Pdf++ Reader", MB_OK | MB_ICONWARNING);
                }
            }

            const std::size_t newPageCount = opened->GetPageCount();
            if (newPageCount == 0U) {
                throw CPPPdf::PdfException(CPPPdf::PdfErrorCode::InvalidPageTree,
                                           "The PDF does not contain any pages.");
            }

            std::vector<CPPPdf::PdfOutlineEntry> outlines;
            try {
                outlines = opened->GetOutlines();
            } catch (...) {
                outlines.clear();
            }
            const CPPPdf::PdfDocumentInfo info = opened->GetDocumentInfo();

            document = std::move(opened);
            path = newPath;
            password = std::move(suppliedPassword);
            pageCount = newPageCount;
            currentPage = 0U;
            zoomMode = ZoomMode::FitPage;
            zoom = 1.0;
            horizontalScroll = 0;
            verticalScroll = 0;
            renderedPage.reset();
            populateBookmarks(outlines);
            updateDocumentControls();

            std::wstring title = utf8ToWide(info.title);
            if (title.empty()) title = path.filename().wstring();
            title += L" - Pdf++ Reader";
            SetWindowTextW(window, title.c_str());
            applyFitZoom();
            requestRender();
            return true;
        } catch (const std::exception& error) {
            updateStatus(L"Could not open document");
            showError(window, L"Could not open PDF", errorToWide(error));
            return false;
        }
    }

    void closeDocument() {
        ++renderGeneration;
        {
            std::lock_guard lock(renderMutex);
            pendingRender.reset();
        }
        document.reset();
        path.clear();
        password.clear();
        pageCount = 0U;
        currentPage = 0U;
        renderedPage.reset();
        rendering = false;
        horizontalScroll = 0;
        verticalScroll = 0;
        TreeView_DeleteAllItems(bookmarkTree);
        SetWindowTextW(window, L"Pdf++ Reader");
        updateDocumentControls();
        updateStatus();
        updateCanvasScrollbars();
        InvalidateRect(canvas, nullptr, TRUE);
    }

    void chooseAndOpenDocument() {
        wchar_t fileName[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.hInstance = instance;
        dialog.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = fileName;
        dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                       OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        dialog.lpstrDefExt = L"pdf";
        if (GetOpenFileNameW(&dialog)) openDocument(std::filesystem::path(fileName));
    }

    void goToPageFromEdit() {
        if (document == nullptr || pageCount == 0U) return;
        wchar_t buffer[64]{};
        GetWindowTextW(pageEdit, buffer, static_cast<int>(std::size(buffer)));
        wchar_t* end = nullptr;
        const unsigned long long requested = wcstoull(buffer, &end, 10);
        if (end == buffer || requested == 0ULL) {
            updateDocumentControls();
            return;
        }
        navigateTo(static_cast<std::size_t>(std::min<unsigned long long>(
            requested - 1ULL, pageCount - 1U)));
    }

    void navigateTo(const std::size_t pageIndex) {
        if (document == nullptr || pageCount == 0U) return;
        const std::size_t clamped = (std::min)(pageIndex, pageCount - 1U);
        if (clamped == currentPage && renderedPage != nullptr) return;
        currentPage = clamped;
        horizontalScroll = 0;
        verticalScroll = 0;
        updateDocumentControls();
        if (zoomMode != ZoomMode::Custom) applyFitZoom();
        requestRender();
    }

    void setCustomZoom(const double value) {
        if (document == nullptr) return;
        zoomMode = ZoomMode::Custom;
        zoom = std::clamp(value, kMinimumZoom, kMaximumZoom);
        horizontalScroll = 0;
        verticalScroll = 0;
        requestRender();
    }

    void setZoomMode(const ZoomMode mode) {
        if (document == nullptr) return;
        zoomMode = mode;
        applyFitZoom();
        horizontalScroll = 0;
        verticalScroll = 0;
        requestRender();
    }

    void applyFitZoom() {
        if (document == nullptr || zoomMode == ZoomMode::Custom) return;
        RECT client{};
        GetClientRect(canvas, &client);
        const double availableWidth = static_cast<double>((std::max)(1,
            static_cast<int>(client.right) - static_cast<int>(client.left) - 2 * kPageMargin));
        const double availableHeight = static_cast<double>((std::max)(1,
            static_cast<int>(client.bottom) - static_cast<int>(client.top) - 2 * kPageMargin));
        try {
            const CPPPdf::PdfRectangle box = document->GetPageCropBox(currentPage);
            double pageWidth = std::abs(box.width());
            double pageHeight = std::abs(box.height());
            const int rotation = document->GetPageRotation(currentPage);
            if (rotation == 90 || rotation == 270 || rotation == -90 || rotation == -270) {
                std::swap(pageWidth, pageHeight);
            }
            const double baseWidth = (std::max)(1.0, pageWidth * 96.0 / 72.0);
            const double baseHeight = (std::max)(1.0, pageHeight * 96.0 / 72.0);
            const double widthZoom = availableWidth / baseWidth;
            const double heightZoom = availableHeight / baseHeight;
            zoom = zoomMode == ZoomMode::FitWidth
                ? widthZoom : (std::min)(widthZoom, heightZoom);
            zoom = std::clamp(zoom, kMinimumZoom, kMaximumZoom);
        } catch (...) {
            zoom = 1.0;
        }
    }

    void requestRender() {
        if (document == nullptr || path.empty() || pageCount == 0U) return;
        RenderRequest request;
        request.generation = ++renderGeneration;
        request.path = path;
        request.password = password;
        request.pageIndex = currentPage;
        request.dpi = 96.0 * zoom;
        rendering = true;
        updateStatus(L"Rendering...");
        {
            std::lock_guard lock(renderMutex);
            pendingRender = std::move(request);
        }
        renderCondition.notify_one();
        InvalidateRect(canvas, nullptr, FALSE);
    }

    void acceptRenderedPage(std::unique_ptr<RenderedPage> result) {
        if (!result || result->generation != renderGeneration || document == nullptr) return;
        rendering = false;
        if (!result->error.empty()) {
            updateStatus(L"Render failed");
            showError(window, L"Could not render page", result->error);
            return;
        }
        renderedPage = std::move(result);
        horizontalScroll = 0;
        verticalScroll = 0;
        updateCanvasScrollbars();
        updateStatus();
        InvalidateRect(canvas, nullptr, TRUE);
    }

    void updateCanvasScrollbars() {
        if (canvas == nullptr) return;
        RECT client{};
        GetClientRect(canvas, &client);
        const int clientWidth = (std::max)(0,
            static_cast<int>(client.right) - static_cast<int>(client.left));
        const int clientHeight = (std::max)(0,
            static_cast<int>(client.bottom) - static_cast<int>(client.top));
        const int contentWidth = renderedPage
            ? renderedPage->width + 2 * kPageMargin : clientWidth;
        const int contentHeight = renderedPage
            ? renderedPage->height + 2 * kPageMargin : clientHeight;

        horizontalScroll = std::clamp(horizontalScroll, 0,
            (std::max)(0, contentWidth - clientWidth));
        verticalScroll = std::clamp(verticalScroll, 0,
            (std::max)(0, contentHeight - clientHeight));

        SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS};
        horizontal.nMin = 0;
        horizontal.nMax = (std::max)(0, contentWidth - 1);
        horizontal.nPage = static_cast<UINT>(clientWidth);
        horizontal.nPos = horizontalScroll;
        SetScrollInfo(canvas, SB_HORZ, &horizontal, TRUE);

        SCROLLINFO vertical{sizeof(vertical), SIF_RANGE | SIF_PAGE | SIF_POS};
        vertical.nMin = 0;
        vertical.nMax = (std::max)(0, contentHeight - 1);
        vertical.nPage = static_cast<UINT>(clientHeight);
        vertical.nPos = verticalScroll;
        SetScrollInfo(canvas, SB_VERT, &vertical, TRUE);
    }

    void scrollCanvas(const int bar, const int command, const int trackPosition) {
        RECT client{};
        GetClientRect(canvas, &client);
        const int page = bar == SB_HORZ
            ? (std::max)(1, static_cast<int>(client.right) - static_cast<int>(client.left))
            : (std::max)(1, static_cast<int>(client.bottom) - static_cast<int>(client.top));
        int& position = bar == SB_HORZ ? horizontalScroll : verticalScroll;
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE};
        GetScrollInfo(canvas, bar, &info);
        const int maximum = (std::max)(0, info.nMax - static_cast<int>(info.nPage) + 1);
        switch (command) {
        case SB_LINEUP:
            position -= 40;
            break;
        case SB_LINEDOWN:
            position += 40;
            break;
        case SB_PAGEUP:
            position -= page;
            break;
        case SB_PAGEDOWN:
            position += page;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            position = trackPosition;
            break;
        case SB_TOP:
            position = 0;
            break;
        case SB_BOTTOM:
            position = maximum;
            break;
        default:
            return;
        }
        position = std::clamp(position, 0, maximum);
        SetScrollPos(canvas, bar, position, TRUE);
        InvalidateRect(canvas, nullptr, FALSE);
    }

    void paintCanvas(const HDC dc) {
        RECT client{};
        GetClientRect(canvas, &client);
        FillRect(dc, &client, canvasBrush);

        if (renderedPage == nullptr) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, PdfPP::ModernWin32::Theme::mutedText);
            RECT messageRect = client;
            const wchar_t* text = document == nullptr
                ? L"Open a PDF file to display it here."
                : (rendering ? L"Rendering page..." : L"No rendered page available.");
            DrawTextW(dc, text, -1, &messageRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return;
        }

        const int clientWidth = client.right - client.left;
        const int clientHeight = client.bottom - client.top;
        int x = kPageMargin - horizontalScroll;
        int y = kPageMargin - verticalScroll;
        if (renderedPage->width + 2 * kPageMargin <= clientWidth) {
            x = (clientWidth - renderedPage->width) / 2;
        }
        if (renderedPage->height + 2 * kPageMargin <= clientHeight) {
            y = (clientHeight - renderedPage->height) / 2;
        }

        RECT shadow{x + 5, y + 5, x + renderedPage->width + 5,
                    y + renderedPage->height + 5};
        const HBRUSH shadowBrush = CreateSolidBrush(RGB(178, 181, 187));
        FillRect(dc, &shadow, shadowBrush);
        DeleteObject(shadowBrush);

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = renderedPage->width;
        bitmapInfo.bmiHeader.biHeight = -renderedPage->height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, x, y,
            static_cast<DWORD>(renderedPage->width),
            static_cast<DWORD>(renderedPage->height),
            0, 0, 0, static_cast<UINT>(renderedPage->height),
            renderedPage->bgra.data(), &bitmapInfo, DIB_RGB_COLORS);

        if (rendering) {
            RECT badge{x + 12, y + 12, x + 124, y + 40};
            const HBRUSH badgeBrush = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(dc, &badge, badgeBrush);
            DeleteObject(badgeBrush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, PdfPP::ModernWin32::Theme::text);
            DrawTextW(dc, L"Rendering...", -1, &badge,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    LRESULT handleCanvasMessage(HWND canvasWindow, UINT message,
                                WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(canvasWindow, &paint);
            paintCanvas(dc);
            EndPaint(canvasWindow, &paint);
            return 0;
        }
        case WM_SIZE:
            updateCanvasScrollbars();
            return 0;
        case WM_HSCROLL:
            scrollCanvas(SB_HORZ, LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_VSCROLL:
            scrollCanvas(SB_VERT, LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_MOUSEWHEEL:
            if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
                const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                setCustomZoom(zoom * (delta > 0 ? 1.20 : 1.0 / 1.20));
            } else {
                const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                scrollCanvas(SB_VERT, delta > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
            }
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_HOME) {
                navigateTo(0U);
                return 0;
            }
            if (wParam == VK_END && pageCount > 0U) {
                navigateTo(pageCount - 1U);
                return 0;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(canvasWindow, message, wParam, lParam);
    }

    LRESULT handleWindowMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            if (!createControls()) return -1;
            startWorker();
            PdfPP::ModernWin32::ApplyDarkMode(window);
            return 0;
        case WM_SIZE:
            layoutControls(LOWORD(lParam), HIWORD(lParam));
            if (document != nullptr && zoomMode != ZoomMode::Custom) {
                SetTimer(window, kResizeRenderTimer, 160U, nullptr);
            }
            return 0;
        case WM_TIMER:
            if (wParam == kResizeRenderTimer) {
                KillTimer(window, kResizeRenderTimer);
                if (document != nullptr && zoomMode != ZoomMode::Custom) {
                    applyFitZoom();
                    requestRender();
                }
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case ID_FILE_OPEN:
                chooseAndOpenDocument();
                return 0;
            case ID_FILE_CLOSE:
                closeDocument();
                return 0;
            case ID_FILE_EXIT:
                DestroyWindow(window);
                return 0;
            case ID_VIEW_PREVIOUS_PAGE:
                if (currentPage > 0U) navigateTo(currentPage - 1U);
                return 0;
            case ID_VIEW_NEXT_PAGE:
                if (currentPage + 1U < pageCount) navigateTo(currentPage + 1U);
                return 0;
            case ID_VIEW_ZOOM_OUT:
                setCustomZoom(zoom / 1.25);
                return 0;
            case ID_VIEW_ZOOM_IN:
                setCustomZoom(zoom * 1.25);
                return 0;
            case ID_VIEW_ACTUAL_SIZE:
                setCustomZoom(1.0);
                return 0;
            case ID_VIEW_FIT_PAGE:
                setZoomMode(ZoomMode::FitPage);
                return 0;
            case ID_VIEW_FIT_WIDTH:
                setZoomMode(ZoomMode::FitWidth);
                return 0;
            case ID_VIEW_TOGGLE_BOOKMARKS: {
                bookmarksVisible = !bookmarksVisible;
                HMENU menu = GetMenu(window);
                CheckMenuItem(menu, ID_VIEW_TOGGLE_BOOKMARKS,
                    MF_BYCOMMAND | (bookmarksVisible ? MF_CHECKED : MF_UNCHECKED));
                RECT client{};
                GetClientRect(window, &client);
                layoutControls(client.right, client.bottom);
                if (document != nullptr && zoomMode != ZoomMode::Custom) {
                    applyFitZoom();
                    requestRender();
                }
                return 0;
            }
            default:
                break;
            }
            break;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header != nullptr && header->hwndFrom == bookmarkTree &&
                header->code == TVN_SELCHANGEDW) {
                const auto* change = reinterpret_cast<const NMTREEVIEWW*>(lParam);
                if (change->itemNew.lParam > 0) {
                    navigateTo(static_cast<std::size_t>(change->itemNew.lParam - 1));
                }
                return 0;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            const HDC dc = reinterpret_cast<HDC>(wParam);
            const HWND control = reinterpret_cast<HWND>(lParam);
            if (control == statusText || control == pageCountLabel) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, PdfPP::ModernWin32::Theme::text);
                return reinterpret_cast<LRESULT>(toolbarBrush);
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            const HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, PdfPP::ModernWin32::Theme::control);
            SetTextColor(dc, PdfPP::ModernWin32::Theme::text);
            return reinterpret_cast<LRESULT>(controlBrush);
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            RECT toolbar{0, 0, client.right, kToolbarHeight};
            RECT status{0, (std::max)(kToolbarHeight,
                            static_cast<int>(client.bottom) - kStatusHeight),
                        client.right, client.bottom};
            FillRect(dc, &toolbar, toolbarBrush);
            FillRect(dc, &status, toolbarBrush);
            const HPEN separator = CreatePen(PS_SOLID, 1,
                PdfPP::ModernWin32::Theme::border);
            const HGDIOBJ previousPen = SelectObject(dc, separator);
            MoveToEx(dc, 0, kToolbarHeight - 1, nullptr);
            LineTo(dc, client.right, kToolbarHeight - 1);
            MoveToEx(dc, 0, status.top, nullptr);
            LineTo(dc, client.right, status.top);
            SelectObject(dc, previousPen);
            DeleteObject(separator);
            EndPaint(window, &paint);
            return 0;
        }
        case kRenderCompletedMessage: {
            std::unique_ptr<RenderedPage> result(
                reinterpret_cast<RenderedPage*>(lParam));
            acceptRenderedPage(std::move(result));
            return 0;
        }
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_DESTROY:
            stopWorker();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

LRESULT CALLBACK canvasWindowProcedure(HWND window, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
    auto* impl = reinterpret_cast<PdfViewerWindow::Impl*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        impl = static_cast<PdfViewerWindow::Impl*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
    }
    if (impl == nullptr) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        const LRESULT result = impl->handleCanvasMessage(window, message, wParam, lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return result;
    }
    return impl->handleCanvasMessage(window, message, wParam, lParam);
}

PdfViewerWindow::PdfViewerWindow() : impl_(std::make_unique<Impl>(this)) {}
PdfViewerWindow::~PdfViewerWindow() = default;

bool PdfViewerWindow::Create(const HINSTANCE instance, const int showCommand) {
    impl_->instance = instance;

    WNDCLASSEXW viewerClass{sizeof(viewerClass)};
    viewerClass.style = CS_HREDRAW | CS_VREDRAW;
    viewerClass.lpfnWndProc = WindowProcedure;
    viewerClass.hInstance = instance;
    viewerClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PDFPP_APP));
    viewerClass.hIconSm = viewerClass.hIcon;
    viewerClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    viewerClass.hbrBackground = nullptr;
    viewerClass.lpszClassName = kViewerWindowClass;
    if (!RegisterClassExW(&viewerClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSEXW canvasClass{sizeof(canvasClass)};
    canvasClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    canvasClass.lpfnWndProc = canvasWindowProcedure;
    canvasClass.hInstance = instance;
    canvasClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    canvasClass.hbrBackground = nullptr;
    canvasClass.lpszClassName = kCanvasWindowClass;
    if (!RegisterClassExW(&canvasClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSEXW passwordClass{sizeof(passwordClass)};
    passwordClass.lpfnWndProc = passwordWindowProcedure;
    passwordClass.hInstance = instance;
    passwordClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    passwordClass.hbrBackground = reinterpret_cast<HBRUSH>(
        static_cast<INT_PTR>(COLOR_WINDOW + 1));
    passwordClass.lpszClassName = kPasswordWindowClass;
    if (!RegisterClassExW(&passwordClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    impl_->window = CreateWindowExW(0, kViewerWindowClass, L"Pdf++ Reader",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 790,
        nullptr, nullptr, instance, this);
    if (impl_->window == nullptr) return false;

    ShowWindow(impl_->window, showCommand);
    UpdateWindow(impl_->window);
    return true;
}

void PdfViewerWindow::OpenInitialDocument(const std::filesystem::path& path) {
    if (impl_->window != nullptr && !path.empty()) impl_->openDocument(path);
}

HWND PdfViewerWindow::GetWindow() const noexcept {
    return impl_->window;
}

LRESULT CALLBACK PdfViewerWindow::WindowProcedure(HWND window, UINT message,
                                                  WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<PdfViewerWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<PdfViewerWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->impl_->window = window;
    }
    if (self == nullptr || self->impl_ == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    const LRESULT result = self->impl_->handleWindowMessage(message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->impl_->window = nullptr;
    }
    return result;
}

} // namespace PdfPP::Win32
