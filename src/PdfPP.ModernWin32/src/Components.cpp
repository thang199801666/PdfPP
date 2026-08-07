#include <PdfPP/ModernWin32.hpp>

#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <new>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")

namespace PdfPP::ModernWin32 {
namespace {

constexpr UINT_PTR kTextBoxSubclassId = 0x50505458U; // PPTX
constexpr UINT_PTR kPanelSubclassId = 0x5050504EU;   // PPPN
constexpr UINT_PTR kComboSubclassId = 0x50504342U;   // PPCB
constexpr wchar_t kSliderClass[] = L"PdfPP.ModernWin32.MacSlider";
constexpr UINT WM_MACSLIDER_SETRANGE = WM_APP + 0x351;
constexpr UINT WM_MACSLIDER_SETVALUE = WM_APP + 0x352;
constexpr UINT WM_MACSLIDER_GETVALUE = WM_APP + 0x353;

struct TextBoxState final {
    TextBoxTemplate style{};
    HFONT font{};
    bool hover{};
};

struct PanelState final {
    PanelTemplate style{};
};

struct ComboState final {
    HFONT font{};
    bool hover{};
};

struct SliderState final {
    int minimum{};
    int maximum{ 100 };
    int value{};
    bool dragging{};
};

int dip(const HWND window, const int value) {
    const UINT dpi = window ? GetDpiForWindow(window) : GetDpiForSystem();
    return (std::max)(1, MulDiv(value, static_cast<int>(dpi), 96));
}

Gdiplus::Color colorOf(const COLORREF color, const BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void prepareGraphics(Gdiplus::Graphics& graphics) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
}

void roundedPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect,
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

void drawRoundedBorder(const HWND window, HDC dc, const COLORREF color,
                       const int radiusDip, const float width = 1.0F) {
    RECT client{};
    GetClientRect(window, &client);
    Gdiplus::Graphics graphics(dc);
    prepareGraphics(graphics);
    const float inset = width * 0.5F;
    Gdiplus::RectF bounds(
        inset, inset,
        (std::max)(1.0F, static_cast<float>(client.right - client.left) - width),
        (std::max)(1.0F, static_cast<float>(client.bottom - client.top) - width));
    Gdiplus::GraphicsPath path;
    roundedPath(path, bounds,
        (std::max)(1.0F, static_cast<float>(dip(window, radiusDip)) - inset));
    Gdiplus::Pen pen(colorOf(color), width);
    graphics.DrawPath(&pen, &path);
}

LRESULT CALLBACK textBoxSubclassProc(HWND window, UINT message,
                                     WPARAM wParam, LPARAM lParam,
                                     UINT_PTR subclassId,
                                     DWORD_PTR reference) {
    auto* state = reinterpret_cast<TextBoxState*>(reference);
    if (!state) return DefSubclassProc(window, message, wParam, lParam);

    switch (message) {
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
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_SETFONT:
        if (state->font) DeleteObject(state->font);
        state->font = UiFontForDpi(GetDpiForWindow(window),
            state->style.fontPointSize, false);
        return DefSubclassProc(window, WM_SETFONT,
            reinterpret_cast<WPARAM>(state->font), lParam);
    case WM_PAINT: {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        HDC dc = GetDC(window);
        if (dc) {
            COLORREF border = state->style.border;
            if (GetFocus() == window) border = state->style.focusBorder;
            else if (state->hover) border = state->style.hoverBorder;
            drawRoundedBorder(window, dc, border, state->style.cornerRadiusDip,
                GetFocus() == window ? 1.5F : 1.0F);
            ReleaseDC(window, dc);
        }
        return result;
    }
    case WM_ERASEBKGND:
        return DefSubclassProc(window, message, wParam, lParam);
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, textBoxSubclassProc, subclassId);
        if (state->font) DeleteObject(state->font);
        delete state;
        return DefSubclassProc(window, message, wParam, lParam);
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void paintPanel(HWND window, HDC target, const PanelTemplate& style) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = (std::max)(1, static_cast<int>(client.right));
    const int height = (std::max)(1, static_cast<int>(client.bottom));
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(target, width, height) : nullptr;
    HGDIOBJ oldBitmap = memory && bitmap ? SelectObject(memory, bitmap) : nullptr;
    HDC dc = memory && bitmap ? memory : target;

    const HBRUSH clear = CreateSolidBrush(style.surface);
    FillRect(dc, &client, clear);
    DeleteObject(clear);

    Gdiplus::Graphics graphics(dc);
    prepareGraphics(graphics);
    const int shadowOffset = style.drawShadow ? dip(window, style.shadowOffsetDip) : 0;
    if (style.drawShadow) {
        Gdiplus::GraphicsPath shadowPath;
        Gdiplus::RectF shadowBounds(
            static_cast<float>(shadowOffset), static_cast<float>(shadowOffset),
            static_cast<float>((std::max)(1, width - shadowOffset - 1)),
            static_cast<float>((std::max)(1, height - shadowOffset - 1)));
        roundedPath(shadowPath, shadowBounds,
            static_cast<float>(dip(window, style.cornerRadiusDip)));
        Gdiplus::SolidBrush shadowBrush(colorOf(style.shadow, 90));
        graphics.FillPath(&shadowBrush, &shadowPath);
    }

    const float rightInset = static_cast<float>(shadowOffset + 1);
    Gdiplus::RectF bounds(0.5F, 0.5F,
        (std::max)(1.0F, static_cast<float>(width) - rightInset),
        (std::max)(1.0F, static_cast<float>(height) - rightInset));
    Gdiplus::GraphicsPath body;
    roundedPath(body, bounds, static_cast<float>(dip(window, style.cornerRadiusDip)));
    Gdiplus::SolidBrush fill(colorOf(style.fill));
    graphics.FillPath(&fill, &body);
    Gdiplus::Pen border(colorOf(style.border), 1.0F);
    graphics.DrawPath(&border, &body);

    if (memory && bitmap) BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    if (oldBitmap) SelectObject(memory, oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
}

LRESULT CALLBACK panelSubclassProc(HWND window, UINT message,
                                   WPARAM wParam, LPARAM lParam,
                                   UINT_PTR subclassId,
                                   DWORD_PTR reference) {
    auto* state = reinterpret_cast<PanelState*>(reference);
    if (!state) return DefSubclassProc(window, message, wParam, lParam);
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        paintPanel(window, dc, state->style);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, panelSubclassProc, subclassId);
        delete state;
        return DefSubclassProc(window, message, wParam, lParam);
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK comboSubclassProc(HWND window, UINT message,
                                   WPARAM wParam, LPARAM lParam,
                                   UINT_PTR subclassId,
                                   DWORD_PTR reference) {
    auto* state = reinterpret_cast<ComboState*>(reference);
    if (!state) return DefSubclassProc(window, message, wParam, lParam);
    switch (message) {
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
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_PAINT: {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        HDC dc = GetDC(window);
        if (dc) {
            const COLORREF border = GetFocus() == window
                ? RGB(0, 122, 255)
                : (state->hover ? RGB(174, 177, 183) : RGB(205, 207, 211));
            drawRoundedBorder(window, dc, border, 7, GetFocus() == window ? 1.5F : 1.0F);
            ReleaseDC(window, dc);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, comboSubclassProc, subclassId);
        if (state->font) DeleteObject(state->font);
        delete state;
        return DefSubclassProc(window, message, wParam, lParam);
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

int sliderValueFromX(HWND window, const SliderState& state, int x) {
    RECT client{};
    GetClientRect(window, &client);
    const int radius = dip(window, 8);
    const int left = radius;
    const int right = (std::max)(left + 1, static_cast<int>(client.right) - radius);
    const int clamped = std::clamp(x, left, right);
    const double fraction = static_cast<double>(clamped - left) /
        static_cast<double>((std::max)(1, right - left));
    const int span = (std::max)(0, state.maximum - state.minimum);
    return state.minimum + static_cast<int>(std::lround(fraction * span));
}

void notifySlider(HWND window, const int code, const int value) {
    SendMessageW(GetParent(window), WM_HSCROLL,
        MAKEWPARAM(code, static_cast<WORD>(std::clamp(value, 0, 0xffff))),
        reinterpret_cast<LPARAM>(window));
}

void paintSlider(HWND window, HDC target, const SliderState& state) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = (std::max)(1, static_cast<int>(client.right));
    const int height = (std::max)(1, static_cast<int>(client.bottom));
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(target, width, height) : nullptr;
    HGDIOBJ oldBitmap = memory && bitmap ? SelectObject(memory, bitmap) : nullptr;
    HDC dc = memory && bitmap ? memory : target;
    const HBRUSH clear = CreateSolidBrush(Theme::window);
    FillRect(dc, &client, clear);
    DeleteObject(clear);

    Gdiplus::Graphics graphics(dc);
    prepareGraphics(graphics);
    const float thumbRadius = static_cast<float>(dip(window, 7));
    const float left = thumbRadius + 1.0F;
    const float right = (std::max)(left + 1.0F, static_cast<float>(width) - thumbRadius - 1.0F);
    const float centerY = static_cast<float>(height) * 0.5F;
    const float trackHeight = static_cast<float>(dip(window, 4));
    const double fraction = state.maximum > state.minimum
        ? static_cast<double>(state.value - state.minimum) /
          static_cast<double>(state.maximum - state.minimum)
        : 0.0;
    const float thumbX = left + static_cast<float>(fraction) * (right - left);

    Gdiplus::Pen basePen(colorOf(RGB(211, 214, 219)), trackHeight);
    basePen.SetStartCap(Gdiplus::LineCapRound);
    basePen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawLine(&basePen, left, centerY, right, centerY);

    Gdiplus::Pen activePen(colorOf(IsWindowEnabled(window) ? RGB(0, 122, 255)
                                                            : RGB(174, 190, 211)),
                           trackHeight);
    activePen.SetStartCap(Gdiplus::LineCapRound);
    activePen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawLine(&activePen, left, centerY, thumbX, centerY);

    Gdiplus::SolidBrush shadow(colorOf(RGB(0, 0, 0), 40));
    graphics.FillEllipse(&shadow, thumbX - thumbRadius + 1.0F,
        centerY - thumbRadius + 2.0F, thumbRadius * 2.0F, thumbRadius * 2.0F);
    Gdiplus::SolidBrush thumb(colorOf(RGB(255, 255, 255)));
    graphics.FillEllipse(&thumb, thumbX - thumbRadius,
        centerY - thumbRadius, thumbRadius * 2.0F, thumbRadius * 2.0F);
    Gdiplus::Pen thumbBorder(colorOf(RGB(184, 187, 193)), 1.0F);
    graphics.DrawEllipse(&thumbBorder, thumbX - thumbRadius,
        centerY - thumbRadius, thumbRadius * 2.0F, thumbRadius * 2.0F);

    if (GetFocus() == window) {
        Gdiplus::Pen focus(colorOf(RGB(0, 122, 255), 105), 2.0F);
        graphics.DrawEllipse(&focus, thumbX - thumbRadius - 2.0F,
            centerY - thumbRadius - 2.0F, thumbRadius * 2.0F + 4.0F,
            thumbRadius * 2.0F + 4.0F);
    }

    if (memory && bitmap) BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    if (oldBitmap) SelectObject(memory, oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
}

LRESULT CALLBACK sliderProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SliderState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = new (std::nothrow) SliderState{};
        if (!state) return FALSE;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        paintSlider(window, dc, *state);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (IsWindowEnabled(window)) {
            SetFocus(window);
            SetCapture(window);
            state->dragging = true;
            state->value = sliderValueFromX(window, *state, GET_X_LPARAM(lParam));
            InvalidateRect(window, nullptr, FALSE);
            notifySlider(window, TB_THUMBTRACK, state->value);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state->dragging && GetCapture() == window && (wParam & MK_LBUTTON)) {
            const int next = sliderValueFromX(window, *state, GET_X_LPARAM(lParam));
            if (next != state->value) {
                state->value = next;
                InvalidateRect(window, nullptr, FALSE);
                notifySlider(window, TB_THUMBTRACK, state->value);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state->dragging) {
            state->dragging = false;
            if (GetCapture() == window) ReleaseCapture();
            state->value = sliderValueFromX(window, *state, GET_X_LPARAM(lParam));
            InvalidateRect(window, nullptr, FALSE);
            notifySlider(window, TB_ENDTRACK, state->value);
        }
        return 0;
    case WM_KEYDOWN: {
        int next = state->value;
        if (wParam == VK_LEFT || wParam == VK_DOWN) --next;
        else if (wParam == VK_RIGHT || wParam == VK_UP) ++next;
        else if (wParam == VK_HOME) next = state->minimum;
        else if (wParam == VK_END) next = state->maximum;
        else break;
        next = std::clamp(next, state->minimum, state->maximum);
        if (next != state->value) {
            state->value = next;
            InvalidateRect(window, nullptr, FALSE);
            notifySlider(window, TB_ENDTRACK, state->value);
        }
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MACSLIDER_SETRANGE:
        state->minimum = static_cast<int>(wParam);
        state->maximum = (std::max)(state->minimum, static_cast<int>(lParam));
        state->value = std::clamp(state->value, state->minimum, state->maximum);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MACSLIDER_SETVALUE:
        state->value = std::clamp(static_cast<int>(wParam), state->minimum, state->maximum);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MACSLIDER_GETVALUE:
        return state->value;
    case WM_NCDESTROY:
        if (GetCapture() == window) ReleaseCapture();
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool ensureSliderClass(HINSTANCE instance) {
    WNDCLASSEXW existing{ sizeof(existing) };
    const HINSTANCE module = instance ? instance : GetModuleHandleW(nullptr);
    if (GetClassInfoExW(module, kSliderClass, &existing)) return true;
    WNDCLASSEXW cls{ sizeof(cls) };
    cls.hInstance = module;
    cls.lpfnWndProc = sliderProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = nullptr;
    cls.lpszClassName = kSliderClass;
    return RegisterClassExW(&cls) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

TextBoxTemplate MacTextBoxTemplate(const ControlSize size) {
    TextBoxTemplate result{};
    switch (size) {
    case ControlSize::Small:
        result.cornerRadiusDip = 6;
        result.fontPointSize = 9;
        result.horizontalPaddingDip = 7;
        break;
    case ControlSize::Large:
        result.cornerRadiusDip = 9;
        result.fontPointSize = 11;
        result.horizontalPaddingDip = 11;
        break;
    case ControlSize::Regular:
    default:
        break;
    }
    return result;
}

void ApplyTextBoxTemplate(const HWND window, const TextBoxTemplate& textBoxTemplate) {
    if (!window) return;
    DWORD_PTR reference{};
    if (GetWindowSubclass(window, textBoxSubclassProc, kTextBoxSubclassId, &reference)) {
        auto* state = reinterpret_cast<TextBoxState*>(reference);
        if (state) {
            state->style = textBoxTemplate;
            if (state->font) DeleteObject(state->font);
            state->font = UiFontForDpi(GetDpiForWindow(window), textBoxTemplate.fontPointSize, false);
            if (state->font) SendMessageW(window, WM_SETFONT,
                reinterpret_cast<WPARAM>(state->font), TRUE);
        }
        InvalidateRect(window, nullptr, TRUE);
        return;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    exStyle &= ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SetWindowTheme(window, L"", L"");

    auto* state = new (std::nothrow) TextBoxState{};
    if (!state) return;
    state->style = textBoxTemplate;
    state->font = UiFontForDpi(GetDpiForWindow(window), textBoxTemplate.fontPointSize, false);
    if (!SetWindowSubclass(window, textBoxSubclassProc, kTextBoxSubclassId,
                           reinterpret_cast<DWORD_PTR>(state))) {
        if (state->font) DeleteObject(state->font);
        delete state;
        return;
    }
    if (state->font) SendMessageW(window, WM_SETFONT,
        reinterpret_cast<WPARAM>(state->font), TRUE);
    const int padding = dip(window, textBoxTemplate.horizontalPaddingDip);
    SendMessageW(window, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(padding, padding));
    InvalidateRect(window, nullptr, TRUE);
}

void ApplyMacTextBoxStyle(const HWND window, const ControlSize size) {
    ApplyTextBoxTemplate(window, MacTextBoxTemplate(size));
}

HWND CreateMacTextBox(HWND parent, HINSTANCE instance, const wchar_t* text,
                      const int command, const int x, const int y,
                      const int width, const int height, const DWORD editStyle,
                      const ControlSize size) {
    HWND window = CreateWindowExW(0, L"EDIT", text ? text : L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | editStyle,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)), instance, nullptr);
    ApplyMacTextBoxStyle(window, size);
    return window;
}

PanelTemplate MacPanelTemplate(const bool floating) {
    PanelTemplate result{};
    result.surface = floating ? Theme::canvas : Theme::window;
    result.drawShadow = floating;
    result.cornerRadiusDip = floating ? 12 : 9;
    result.shadowOffsetDip = floating ? 3 : 1;
    return result;
}

void ApplyPanelTemplate(const HWND window, const PanelTemplate& panelTemplate) {
    if (!window) return;
    DWORD_PTR reference{};
    if (GetWindowSubclass(window, panelSubclassProc, kPanelSubclassId, &reference)) {
        auto* state = reinterpret_cast<PanelState*>(reference);
        if (state) state->style = panelTemplate;
        InvalidateRect(window, nullptr, TRUE);
        return;
    }
    auto* state = new (std::nothrow) PanelState{};
    if (!state) return;
    state->style = panelTemplate;
    if (!SetWindowSubclass(window, panelSubclassProc, kPanelSubclassId,
                           reinterpret_cast<DWORD_PTR>(state))) {
        delete state;
        return;
    }
    InvalidateRect(window, nullptr, TRUE);
}

void ApplyMacPanelStyle(const HWND window, const bool floating) {
    ApplyPanelTemplate(window, MacPanelTemplate(floating));
}

HWND CreateMacPanel(HWND parent, HINSTANCE instance, const int command,
                    const int x, const int y, const int width, const int height,
                    const bool floating) {
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)), instance, nullptr);
    ApplyMacPanelStyle(window, floating);
    return window;
}

void ApplyMacDialogStyle(const HWND window) {
    if (!window) return;
    ApplyDarkMode(window);
    constexpr DWORD kCornerPreferenceAttribute = 33;
    constexpr int kRoundCorners = 2;
    const int corners = kRoundCorners;
    DwmSetWindowAttribute(window, kCornerPreferenceAttribute, &corners, sizeof(corners));
    SetWindowTheme(window, L"Explorer", nullptr);
}

HWND CreateMacComboBox(HWND parent, HINSTANCE instance, const int command,
                       const int x, const int y, const int width, const int height,
                       const bool editable, const ControlSize size) {
    const DWORD comboStyle = editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST;
    HWND window = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | comboStyle,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)), instance, nullptr);
    if (!window) return nullptr;
    SetWindowTheme(window, L"Explorer", nullptr);
    auto* state = new (std::nothrow) ComboState{};
    if (!state) return window;
    const int points = size == ControlSize::Small ? 9 : (size == ControlSize::Large ? 11 : 10);
    state->font = UiFontForDpi(GetDpiForWindow(window), points, false);
    if (state->font) SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
    if (!SetWindowSubclass(window, comboSubclassProc, kComboSubclassId,
                           reinterpret_cast<DWORD_PTR>(state))) {
        if (state->font) DeleteObject(state->font);
        delete state;
    }
    SendMessageW(window, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
        dip(window, size == ControlSize::Small ? 22 : size == ControlSize::Large ? 30 : 26));
    SendMessageW(window, CB_SETITEMHEIGHT, 0,
        dip(window, size == ControlSize::Small ? 22 : size == ControlSize::Large ? 30 : 26));
    return window;
}

SpinnerControl CreateMacSpinner(HWND parent, HINSTANCE instance, const int command,
                                const int x, const int y, const int width, const int height,
                                const int minimum, const int maximum, const int value,
                                const ControlSize size) {
    SpinnerControl result{};
    result.edit = CreateMacTextBox(parent, instance, L"", command,
        x, y, width, height, ES_AUTOHSCROLL | ES_NUMBER | ES_CENTER, size);
    if (!result.edit) return result;
    result.stepper = CreateWindowExW(0, UPDOWN_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS |
        UDS_SETBUDDYINT | UDS_NOTHOUSANDS,
        x, y, dip(parent, 20), height, parent, nullptr, instance, nullptr);
    if (result.stepper) {
        SendMessageW(result.stepper, UDM_SETBUDDY, reinterpret_cast<WPARAM>(result.edit), 0);
        SendMessageW(result.stepper, UDM_SETRANGE32, minimum, maximum);
        SendMessageW(result.stepper, UDM_SETPOS32, 0, std::clamp(value, minimum, maximum));
        SetWindowTheme(result.stepper, L"Explorer", nullptr);
    }
    return result;
}

void SetMacSpinnerRange(const SpinnerControl& spinner, const int minimum, const int maximum) {
    if (spinner.stepper) SendMessageW(spinner.stepper, UDM_SETRANGE32, minimum, maximum);
}

void SetMacSpinnerValue(const SpinnerControl& spinner, const int value) {
    if (spinner.stepper) SendMessageW(spinner.stepper, UDM_SETPOS32, 0, value);
}

int MacSpinnerValue(const SpinnerControl& spinner) {
    if (!spinner.stepper) return 0;
    return static_cast<int>(SendMessageW(spinner.stepper, UDM_GETPOS32, 0, 0));
}

HWND CreateMacSlider(HWND parent, HINSTANCE instance, const int command,
                     const int x, const int y, const int width, const int height,
                     const int minimum, const int maximum, const int value) {
    if (!ensureSliderClass(instance)) return nullptr;
    HWND window = CreateWindowExW(0, kSliderClass, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)), instance, nullptr);
    if (window) {
        SetMacSliderRange(window, minimum, maximum);
        SetMacSliderValue(window, value);
    }
    return window;
}

void SetMacSliderRange(const HWND slider, const int minimum, const int maximum) {
    if (slider) SendMessageW(slider, WM_MACSLIDER_SETRANGE,
        static_cast<WPARAM>(minimum), static_cast<LPARAM>(maximum));
}

void SetMacSliderValue(const HWND slider, const int value) {
    if (slider) SendMessageW(slider, WM_MACSLIDER_SETVALUE,
        static_cast<WPARAM>(value), 0);
}

int MacSliderValue(const HWND slider) {
    return slider ? static_cast<int>(SendMessageW(slider, WM_MACSLIDER_GETVALUE, 0, 0)) : 0;
}

} // namespace PdfPP::ModernWin32
