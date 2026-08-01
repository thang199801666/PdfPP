#include <PdfPP/ModernWin32.hpp>

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <iterator>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace PdfPP::ModernWin32 {

namespace {
constexpr wchar_t kActionButtonClass[] = L"PdfPP.ModernWin32.ActionButton";

struct ActionButtonState final {
    bool hover{};
    bool pressed{};
    bool accent{};
    HFONT font{};
    bool ownsFont{};
};

void paintActionButton(HWND window, HDC dc, ActionButtonState& state) {
    RECT rect{};
    GetClientRect(window, &rect);
    const bool enabled = IsWindowEnabled(window) != FALSE;
    COLORREF fill = Theme::button;
    if (!enabled) fill = Theme::status;
    else if (state.pressed) fill = Theme::buttonPressed;
    else if (state.hover) fill = state.accent ? Theme::buttonAccent : Theme::buttonHover;
    else if (state.accent) fill = Theme::buttonAccent;

    const HBRUSH brush = CreateSolidBrush(fill);
    const HPEN pen = CreatePen(PS_SOLID, 1, state.accent ? Theme::buttonAccent : Theme::border);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, pen);
    const int scaledCorner = MulDiv(7, static_cast<int>(GetDpiForWindow(window)), 96);
    const int corner = scaledCorner > 0 ? scaledCorner : 1;
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, corner, corner);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    wchar_t text[256]{};
    GetWindowTextW(window, text, static_cast<int>(std::size(text)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? Theme::text : Theme::mutedText);
    DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

LRESULT CALLBACK actionButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ActionButtonState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = new ActionButtonState{};
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        const auto oldFont = state->font ? SelectObject(dc, state->font) : nullptr;
        paintActionButton(window, dc, *state);
        if (oldFont) SelectObject(dc, oldFont);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SETFONT:
        if (state->ownsFont && state->font && state->font != reinterpret_cast<HFONT>(wParam)) {
            DeleteObject(state->font);
        }
        state->font = reinterpret_cast<HFONT>(wParam);
        state->ownsFont = false;
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_MOUSEMOVE:
        if (!state->hover) {
            state->hover = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        state->hover = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        if (IsWindowEnabled(window)) {
            state->pressed = true;
            SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == window) ReleaseCapture();
        if (state->pressed) {
            state->pressed = false;
            InvalidateRect(window, nullptr, FALSE);
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rect{}; GetClientRect(window, &rect);
            if (PtInRect(&rect, point) && IsWindowEnabled(window)) {
                SendMessageW(GetParent(window), WM_COMMAND,
                             MAKEWPARAM(GetDlgCtrlID(window), BN_CLICKED),
                             reinterpret_cast<LPARAM>(window));
            }
        }
        return 0;
    case WM_KEYDOWN:
        if ((wParam == VK_RETURN || wParam == VK_SPACE) && IsWindowEnabled(window)) {
            SendMessageW(GetParent(window), WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(window), BN_CLICKED),
                         reinterpret_cast<LPARAM>(window));
            return 0;
        }
        break;
    case WM_ENABLE:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        if (state->ownsFont && state->font) DeleteObject(state->font);
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

void Initialize(const HINSTANCE instance) {
    // DPI awareness must be selected before common controls or any app window
    // can be created, otherwise Windows may lock the process to system DPI.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES |
                                                   ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSW buttonClass{};
    buttonClass.hInstance = instance ? instance : GetModuleHandleW(nullptr);
    buttonClass.lpfnWndProc = actionButtonProc;
    buttonClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    buttonClass.lpszClassName = kActionButtonClass;
    buttonClass.hbrBackground = nullptr;
    RegisterClassW(&buttonClass);
}

void ApplyDarkMode(HWND window) {
    if (!window) return;
    // Kept as a source-compatible API for existing clients. The default
    // ModernWin32 theme is now light, so explicitly request a light title bar.
    const BOOL dark = FALSE;
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(window, 19, &dark, sizeof(dark));
    SetWindowTheme(window, L"Explorer", nullptr);
}

HFONT UiFontForDpi(const UINT dpi, const int pointSize, const bool bold) {
    return CreateFontW(-MulDiv(pointSize, dpi, 72), 0, 0, 0,
                       bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

HFONT UiFont(const int pointSize, const bool bold) {
    return UiFontForDpi(GetDpiForSystem(), pointSize, bold);
}

void ApplyFont(const HWND window, const int pointSize, const bool bold) {
    if (window) SendMessageW(window, WM_SETFONT,
                             reinterpret_cast<WPARAM>(
                                 UiFontForDpi(GetDpiForWindow(window), pointSize, bold)), TRUE);
}

void SetControlColors(const HDC dc, const COLORREF background, const COLORREF foreground) {
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, background);
    SetTextColor(dc, foreground);
}

HWND CreateActionButton(HWND parent, HINSTANCE instance, const wchar_t* text, const int command,
                        const int x, const int y, const int width, const int height,
                        const bool accent) {
    HWND window = CreateWindowExW(0, kActionButtonClass, text,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  x, y, width, height, parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)),
                                  instance, nullptr);
    if (window) {
        SetActionButtonAccent(window, accent);
        auto* state = reinterpret_cast<ActionButtonState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state) {
            state->font = UiFontForDpi(GetDpiForWindow(window), 7, false);
            state->ownsFont = state->font != nullptr;
        }
    }
    return window;
}

void SetActionButtonAccent(const HWND window, const bool accent) {
    if (!window) return;
    auto* state = reinterpret_cast<ActionButtonState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state) state->accent = accent;
    InvalidateRect(window, nullptr, FALSE);
}

void SetActionButtonText(const HWND window, const wchar_t* text) {
    if (window) SetWindowTextW(window, text ? text : L"");
}

} // namespace PdfPP::ModernWin32
