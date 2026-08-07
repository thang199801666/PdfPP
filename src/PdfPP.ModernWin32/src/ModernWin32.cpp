#include <PdfPP/ModernWin32.hpp>

#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <iterator>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")

namespace PdfPP::ModernWin32 {

namespace {
constexpr wchar_t kActionButtonClass[] = L"PdfPP.ModernWin32.ActionButton";
constexpr UINT_PTR kMacButtonSubclassId = 0x50445042U; // 'PDPB'
ULONG_PTR gGdiPlusToken{};

void ensureGdiPlus() {
    if (gGdiPlusToken != 0) return;
    Gdiplus::GdiplusStartupInput input{};
    Gdiplus::GdiplusStartup(&gGdiPlusToken, &input, nullptr);
}

struct ActionButtonState final {
    bool hover{};
    bool pressed{};
    ButtonStyle style{ ButtonStyle::Toolbar };
    ButtonTemplate buttonTemplate{};
    HFONT font{};
    bool ownsFont{};
};

struct NativeButtonState final {
    bool hover{};
    ButtonTemplate buttonTemplate{};
    HFONT font{};
};

int dipForWindow(const HWND window, const int value) {
    const UINT dpi = window ? GetDpiForWindow(window) : GetDpiForSystem();
    return (std::max)(1, MulDiv(value, static_cast<int>(dpi), 96));
}

Gdiplus::Color gpColor(const COLORREF color, const BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void addRoundedRectangle(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect,
                         float radius) {
    radius = (std::max)(0.0F, (std::min)(radius, (std::min)(rect.Width, rect.Height) * 0.5F));
    if (radius <= 0.5F) {
        path.AddRectangle(rect);
        return;
    }
    const float diameter = radius * 2.0F;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter,
                diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

void prepareHighQualityGraphics(Gdiplus::Graphics& graphics) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
}

void fillRoundRect(HDC dc, const RECT& rect, const COLORREF color, const int radius) {
    Gdiplus::Graphics graphics(dc);
    prepareHighQualityGraphics(graphics);
    Gdiplus::GraphicsPath path;
    const Gdiplus::RectF bounds(
        static_cast<Gdiplus::REAL>(rect.left), static_cast<Gdiplus::REAL>(rect.top),
        static_cast<Gdiplus::REAL>((std::max)(0L, rect.right - rect.left)),
        static_cast<Gdiplus::REAL>((std::max)(0L, rect.bottom - rect.top)));
    addRoundedRectangle(path, bounds, static_cast<float>(radius));
    Gdiplus::SolidBrush brush(gpColor(color));
    graphics.FillPath(&brush, &path);
}

void strokeRoundRect(HDC dc, const RECT& rect, const COLORREF color,
                     const int radius, const int width = 1) {
    Gdiplus::Graphics graphics(dc);
    prepareHighQualityGraphics(graphics);
    const float penWidth = static_cast<float>((std::max)(1, width));
    const float inset = penWidth * 0.5F;
    Gdiplus::GraphicsPath path;
    const Gdiplus::RectF bounds(
        static_cast<Gdiplus::REAL>(rect.left) + inset,
        static_cast<Gdiplus::REAL>(rect.top) + inset,
        static_cast<Gdiplus::REAL>((std::max)(0L, rect.right - rect.left)) - penWidth,
        static_cast<Gdiplus::REAL>((std::max)(0L, rect.bottom - rect.top)) - penWidth);
    addRoundedRectangle(path, bounds, (std::max)(0.0F, static_cast<float>(radius) - inset));
    Gdiplus::Pen pen(gpColor(color), penWidth);
    pen.SetAlignment(Gdiplus::PenAlignmentCenter);
    graphics.DrawPath(&pen, &path);
}

void paintMacButton(HWND window, HDC targetDc, const ButtonTemplate& buttonTemplate,
                    const bool hover, const bool pressed, HFONT preferredFont) {
    ensureGdiPlus();
    RECT client{};
    GetClientRect(window, &client);
    const int width = (std::max)(1, static_cast<int>(client.right - client.left));
    const int height = (std::max)(1, static_cast<int>(client.bottom - client.top));

    HDC memoryDc = CreateCompatibleDC(targetDc);
    HBITMAP bitmap = memoryDc ? CreateCompatibleBitmap(targetDc, width, height) : nullptr;
    HGDIOBJ oldBitmap = (memoryDc && bitmap) ? SelectObject(memoryDc, bitmap) : nullptr;
    HDC dc = (memoryDc && bitmap) ? memoryDc : targetDc;

    const bool enabled = IsWindowEnabled(window) != FALSE;
    const bool focused = GetFocus() == window;
    COLORREF fill = buttonTemplate.fill;
    COLORREF border = buttonTemplate.border;
    COLORREF text = buttonTemplate.text;
    if (!enabled) {
        fill = buttonTemplate.disabledFill;
        text = buttonTemplate.disabledText;
    } else if (pressed) {
        fill = buttonTemplate.pressedFill;
        border = buttonTemplate.pressedBorder;
    } else if (hover) {
        fill = buttonTemplate.hoverFill;
        border = buttonTemplate.hoverBorder;
    }

    const int radius = (std::min)(
        dipForWindow(window, buttonTemplate.cornerRadiusDip),
        (std::max)(1, (std::min)(width, height) / 2));
    const HBRUSH parentBrush = CreateSolidBrush(buttonTemplate.surface);
    FillRect(dc, &client, parentBrush);
    DeleteObject(parentBrush);

    RECT body = client;
    InflateRect(&body, -1, -1);
    fillRoundRect(dc, body, fill, radius);
    if (buttonTemplate.drawBorder) {
        strokeRoundRect(dc, body, border, radius, 1);
    }

    if (focused && enabled) {
        RECT focus = body;
        InflateRect(&focus, -2, -2);
        strokeRoundRect(dc, focus, buttonTemplate.focusRing,
            (std::max)(1, radius - 2), 1);
    }

    wchar_t labelText[512]{};
    GetWindowTextW(window, labelText, static_cast<int>(std::size(labelText)));
    HFONT font = preferredFont;
    HFONT temporaryFont{};
    if (!font) {
        temporaryFont = UiFontForDpi(GetDpiForWindow(window),
            buttonTemplate.fontPointSize, buttonTemplate.semibold);
        font = temporaryFont;
    }
    RECT label = client;
    label.left += dipForWindow(window, 6);
    label.right -= dipForWindow(window, 6);
    bool textDrawn = false;
    if (font) {
        Gdiplus::Graphics graphics(dc);
        prepareHighQualityGraphics(graphics);
        // Grayscale anti-aliased text is stable on off-screen buffers and avoids
        // the colored ClearType fringes that made the previous buttons look
        // pixelated when the back buffer was copied to a high-DPI monitor.
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::Font gpFont(dc, font);
        if (gpFont.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericDefault());
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            const Gdiplus::RectF textBounds(
                static_cast<Gdiplus::REAL>(label.left),
                static_cast<Gdiplus::REAL>(label.top),
                static_cast<Gdiplus::REAL>((std::max)(0L, label.right - label.left)),
                static_cast<Gdiplus::REAL>((std::max)(0L, label.bottom - label.top)));
            Gdiplus::SolidBrush textBrush(gpColor(text));
            textDrawn = graphics.DrawString(labelText, -1, &gpFont,
                textBounds, &format, &textBrush) == Gdiplus::Ok;
        }
    }
    if (!textDrawn) {
        HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, text);
        DrawTextW(dc, labelText, -1, &label,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (oldFont) SelectObject(dc, oldFont);
    }
    if (temporaryFont) DeleteObject(temporaryFont);

    if (memoryDc && bitmap) {
        BitBlt(targetDc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
    }
    if (oldBitmap) SelectObject(memoryDc, oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (memoryDc) DeleteDC(memoryDc);
}

LRESULT CALLBACK nativeButtonSubclassProc(HWND window, UINT message,
                                          WPARAM wParam, LPARAM lParam,
                                          UINT_PTR subclassId,
                                          DWORD_PTR reference) {
    auto* state = reinterpret_cast<NativeButtonState*>(reference);
    if (!state) return DefSubclassProc(window, message, wParam, lParam);

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        const LRESULT buttonState = SendMessageW(window, BM_GETSTATE, 0, 0);
        const bool pressed = (buttonState & BST_PUSHED) != 0;
        paintMacButton(window, dc, state->buttonTemplate, state->hover,
            pressed, state->font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!state->hover) {
            state->hover = true;
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        state->hover = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SETTEXT:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_SETFONT:
        // The template owns the visual font so a generic application-wide
        // WM_SETFONT does not shrink buttons back to the old toolbar font.
        if (state->font) DeleteObject(state->font);
        state->font = UiFontForDpi(GetDpiForWindow(window),
            state->buttonTemplate.fontPointSize, state->buttonTemplate.semibold);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, nativeButtonSubclassProc, subclassId);
        if (state->font) DeleteObject(state->font);
        delete state;
        return DefSubclassProc(window, message, wParam, lParam);
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void paintActionButton(HWND window, HDC dc, ActionButtonState& state) {
    paintMacButton(window, dc, state.buttonTemplate, state.hover,
        state.pressed, state.font);
}

LRESULT CALLBACK actionButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ActionButtonState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = new ActionButtonState{};
        state->buttonTemplate = MacButtonTemplate(ButtonStyle::Toolbar);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        paintActionButton(window, dc, *state);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SETFONT:
        // Keep the button template's own typography even when the application
        // refreshes the generic UI font (for example after a DPI change).
        if (state->ownsFont && state->font) DeleteObject(state->font);
        state->font = UiFontForDpi(GetDpiForWindow(window),
            state->buttonTemplate.fontPointSize, state->buttonTemplate.semibold);
        state->ownsFont = state->font != nullptr;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (!state->hover) {
            state->hover = true;
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
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
            SetFocus(window);
            SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == window) ReleaseCapture();
        if (state->pressed) {
            state->pressed = false;
            InvalidateRect(window, nullptr, FALSE);
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rect{};
            GetClientRect(window, &rect);
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
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SETTEXT:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        if (state->ownsFont && state->font) DeleteObject(state->font);
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

ButtonTemplate MacButtonTemplate(const ButtonStyle style) {
    ButtonTemplate result{};
    result.focusRing = RGB(64, 129, 255);
    switch (style) {
    case ButtonStyle::Primary:
        result.surface = Theme::window;
        result.fill = RGB(0, 122, 255);
        result.hoverFill = RGB(20, 132, 255);
        result.pressedFill = RGB(0, 105, 225);
        result.disabledFill = RGB(196, 214, 236);
        result.border = RGB(0, 110, 235);
        result.hoverBorder = RGB(0, 110, 235);
        result.pressedBorder = RGB(0, 88, 195);
        result.text = RGB(255, 255, 255);
        result.disabledText = RGB(244, 247, 251);
        result.cornerRadiusDip = 7;
        result.fontPointSize = 9;
        result.semibold = true;
        break;
    case ButtonStyle::Ghost:
        result.surface = Theme::window;
        result.fill = RGB(255, 255, 255);
        result.hoverFill = RGB(241, 242, 244);
        result.pressedFill = RGB(226, 228, 231);
        result.disabledFill = RGB(255, 255, 255);
        result.border = RGB(255, 255, 255);
        result.hoverBorder = RGB(226, 228, 232);
        result.pressedBorder = RGB(210, 213, 218);
        result.text = RGB(54, 57, 63);
        result.disabledText = RGB(174, 176, 181);
        result.cornerRadiusDip = 7;
        result.fontPointSize = 10;
        result.drawBorder = false;
        break;
    case ButtonStyle::Toolbar:
        result.surface = Theme::toolbar;
        result.fill = RGB(250, 250, 251);
        result.hoverFill = RGB(238, 239, 241);
        result.pressedFill = RGB(221, 223, 227);
        result.disabledFill = RGB(247, 247, 248);
        result.border = RGB(218, 219, 222);
        result.hoverBorder = RGB(205, 207, 211);
        result.pressedBorder = RGB(188, 191, 197);
        result.text = RGB(42, 44, 49);
        result.disabledText = RGB(164, 166, 171);
        result.cornerRadiusDip = 7;
        result.fontPointSize = 9;
        break;
    case ButtonStyle::Secondary:
    default:
        result.surface = Theme::window;
        result.fill = RGB(255, 255, 255);
        result.hoverFill = RGB(246, 246, 247);
        result.pressedFill = RGB(229, 230, 232);
        result.disabledFill = RGB(246, 246, 247);
        result.border = RGB(202, 204, 208);
        result.hoverBorder = RGB(183, 186, 192);
        result.pressedBorder = RGB(166, 170, 177);
        result.text = RGB(37, 39, 43);
        result.disabledText = RGB(160, 162, 167);
        result.cornerRadiusDip = 7;
        result.fontPointSize = 9;
        break;
    }
    return result;
}

void Initialize(const HINSTANCE instance) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ensureGdiPlus();
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES |
                                                   ICC_TREEVIEW_CLASSES | ICC_UPDOWN_CLASS };
    InitCommonControlsEx(&controls);

    WNDCLASSW buttonClass{};
    buttonClass.hInstance = instance ? instance : GetModuleHandleW(nullptr);
    buttonClass.lpfnWndProc = actionButtonProc;
    // A regular UI button should retain the normal arrow cursor. The custom
    // hand cursor is reserved for actual page/item dragging gestures.
    buttonClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    buttonClass.lpszClassName = kActionButtonClass;
    buttonClass.hbrBackground = nullptr;
    RegisterClassW(&buttonClass);
}

void ApplyDarkMode(HWND window) {
    if (!window) return;
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
    if (window) {
        const HFONT font = UiFontForDpi(GetDpiForWindow(window), pointSize, bold);
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void SetControlColors(const HDC dc, const COLORREF background, const COLORREF foreground) {
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, background);
    SetTextColor(dc, foreground);
}

void ApplyButtonTemplate(const HWND window, const ButtonTemplate& buttonTemplate) {
    if (!window) return;
    DWORD_PTR reference{};
    if (GetWindowSubclass(window, nativeButtonSubclassProc, kMacButtonSubclassId,
                          &reference)) {
        auto* existing = reinterpret_cast<NativeButtonState*>(reference);
        if (existing) {
            existing->buttonTemplate = buttonTemplate;
            if (existing->font) DeleteObject(existing->font);
            existing->font = UiFontForDpi(GetDpiForWindow(window),
                buttonTemplate.fontPointSize, buttonTemplate.semibold);
        }
        InvalidateRect(window, nullptr, FALSE);
        return;
    }

    auto* state = new NativeButtonState{};
    state->buttonTemplate = buttonTemplate;
    state->font = UiFontForDpi(GetDpiForWindow(window),
        buttonTemplate.fontPointSize, buttonTemplate.semibold);
    if (!SetWindowSubclass(window, nativeButtonSubclassProc, kMacButtonSubclassId,
                           reinterpret_cast<DWORD_PTR>(state))) {
        if (state->font) DeleteObject(state->font);
        delete state;
        return;
    }
    SetWindowTheme(window, L"", L"");
    InvalidateRect(window, nullptr, TRUE);
}

void ApplyMacButtonStyle(const HWND window, const ButtonStyle style) {
    ApplyButtonTemplate(window, MacButtonTemplate(style));
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
        SetActionButtonStyle(window, accent ? ButtonStyle::Primary : ButtonStyle::Toolbar);
    }
    return window;
}

void SetActionButtonAccent(const HWND window, const bool accent) {
    SetActionButtonStyle(window, accent ? ButtonStyle::Primary : ButtonStyle::Toolbar);
}

void SetActionButtonStyle(const HWND window, const ButtonStyle style) {
    if (!window) return;
    auto* state = reinterpret_cast<ActionButtonState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) return;
    state->style = style;
    state->buttonTemplate = MacButtonTemplate(style);
    // Action buttons are used by the reader toolbar/sidebar, so blend the
    // anti-aliased corner pixels against that surface. Native dialog buttons
    // keep the regular window surface through ApplyMacButtonStyle().
    state->buttonTemplate.surface = Theme::toolbar;
    if (state->ownsFont && state->font) {
        DeleteObject(state->font);
        state->font = nullptr;
    }
    state->font = UiFontForDpi(GetDpiForWindow(window),
        state->buttonTemplate.fontPointSize, state->buttonTemplate.semibold);
    state->ownsFont = state->font != nullptr;
    InvalidateRect(window, nullptr, FALSE);
}

void SetActionButtonText(const HWND window, const wchar_t* text) {
    if (window) SetWindowTextW(window, text ? text : L"");
}

} // namespace PdfPP::ModernWin32
