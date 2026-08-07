#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

namespace PdfPP::ModernWin32 {

struct Theme final {
    // Acrobat-inspired light defaults. Keep all colors centralized so other
    // Win32 applications can reuse the same visual language.
    static constexpr COLORREF window = RGB(255, 255, 255);
    static constexpr COLORREF toolbar = RGB(248, 249, 250);
    static constexpr COLORREF sidebar = RGB(248, 249, 250);
    static constexpr COLORREF canvas = RGB(235, 237, 240);
    static constexpr COLORREF control = RGB(255, 255, 255);
    static constexpr COLORREF controlHover = RGB(229, 234, 241);
    static constexpr COLORREF text = RGB(31, 41, 51);
    static constexpr COLORREF mutedText = RGB(99, 109, 122);
    static constexpr COLORREF accent = RGB(20, 115, 230);
    static constexpr COLORREF border = RGB(214, 218, 224);
    static constexpr COLORREF button = RGB(255, 255, 255);
    static constexpr COLORREF buttonHover = RGB(229, 234, 241);
    static constexpr COLORREF buttonPressed = RGB(216, 222, 230);
    static constexpr COLORREF buttonAccent = RGB(20, 115, 230);
    static constexpr COLORREF status = RGB(248, 249, 250);
};

struct Layout final {
    static constexpr int menuHeight = 24;
    static constexpr int ribbonHeight = 36;
    static constexpr int sidebarWidth = 242;
    static constexpr int statusHeight = 26;
    static constexpr int controlHeight = 24;
    static constexpr int gutter = 6;
};

// Reusable macOS-inspired button templates.  These styles are intentionally
// platform-neutral at the API level so every Win32 surface can use the same
// hover/pressed/focus/disabled behavior without duplicating owner-draw code.
enum class ButtonStyle {
    Secondary,
    Primary,
    Toolbar,
    Ghost
};


// Shared sizing for macOS-inspired Win32 controls.  The implementation keeps
// all dimensions DPI-aware so callers can use the same API on 100-300% scale.
enum class ControlSize {
    Small,
    Regular,
    Large
};

struct TextBoxTemplate final {
    COLORREF fill{ RGB(255, 255, 255) };
    COLORREF border{ RGB(205, 207, 211) };
    COLORREF hoverBorder{ RGB(174, 177, 183) };
    COLORREF focusBorder{ RGB(0, 122, 255) };
    COLORREF text{ RGB(35, 37, 41) };
    COLORREF disabledText{ RGB(155, 158, 164) };
    int cornerRadiusDip{ 7 };
    int fontPointSize{ 10 };
    int horizontalPaddingDip{ 9 };
};

struct PanelTemplate final {
    COLORREF surface{ Theme::window };
    COLORREF fill{ RGB(255, 255, 255) };
    COLORREF border{ RGB(219, 221, 225) };
    COLORREF shadow{ RGB(205, 207, 212) };
    int cornerRadiusDip{ 10 };
    int shadowOffsetDip{ 2 };
    bool drawShadow{ true };
};

struct SpinnerControl final {
    HWND edit{};
    HWND stepper{};
};
struct ButtonTemplate final {
    // Surface is painted behind the rounded body. Keeping it explicit avoids
    // dark/jagged corner pixels when the button is rendered off-screen.
    COLORREF surface{ Theme::window };
    COLORREF fill{};
    COLORREF hoverFill{};
    COLORREF pressedFill{};
    COLORREF disabledFill{};
    COLORREF border{};
    COLORREF hoverBorder{};
    COLORREF pressedBorder{};
    COLORREF text{};
    COLORREF disabledText{};
    COLORREF focusRing{};
    int cornerRadiusDip{ 7 };
    int fontPointSize{ 9 };
    bool semibold{};
    bool drawBorder{ true };
};

void Initialize(HINSTANCE instance);
void ApplyDarkMode(HWND window);
HFONT UiFontForDpi(UINT dpi, int pointSize = 9, bool bold = false);
HFONT UiFont(int pointSize = 9, bool bold = false);
void ApplyFont(HWND window, int pointSize = 9, bool bold = false);
void SetControlColors(HDC dc, COLORREF background, COLORREF foreground);

[[nodiscard]] ButtonTemplate MacButtonTemplate(ButtonStyle style);
void ApplyButtonTemplate(HWND window, const ButtonTemplate& buttonTemplate);
void ApplyMacButtonStyle(HWND window, ButtonStyle style = ButtonStyle::Secondary);

// macOS-inspired component kit -------------------------------------------------
// These helpers deliberately wrap native Win32 controls where possible, so the
// application keeps normal accessibility/keyboard semantics while sharing one
// visual system.
[[nodiscard]] TextBoxTemplate MacTextBoxTemplate(ControlSize size = ControlSize::Regular);
void ApplyTextBoxTemplate(HWND window, const TextBoxTemplate& textBoxTemplate);
void ApplyMacTextBoxStyle(HWND window, ControlSize size = ControlSize::Regular);
HWND CreateMacTextBox(HWND parent, HINSTANCE instance, const wchar_t* text, int command,
                      int x, int y, int width, int height,
                      DWORD editStyle = ES_AUTOHSCROLL,
                      ControlSize size = ControlSize::Regular);

[[nodiscard]] PanelTemplate MacPanelTemplate(bool floating = false);
void ApplyPanelTemplate(HWND window, const PanelTemplate& panelTemplate);
void ApplyMacPanelStyle(HWND window, bool floating = false);
HWND CreateMacPanel(HWND parent, HINSTANCE instance, int command,
                    int x, int y, int width, int height, bool floating = false);

void ApplyMacDialogStyle(HWND window);

HWND CreateMacComboBox(HWND parent, HINSTANCE instance, int command,
                       int x, int y, int width, int height,
                       bool editable = false,
                       ControlSize size = ControlSize::Regular);

SpinnerControl CreateMacSpinner(HWND parent, HINSTANCE instance, int command,
                                int x, int y, int width, int height,
                                int minimum, int maximum, int value,
                                ControlSize size = ControlSize::Regular);
void SetMacSpinnerRange(const SpinnerControl& spinner, int minimum, int maximum);
void SetMacSpinnerValue(const SpinnerControl& spinner, int value);
[[nodiscard]] int MacSpinnerValue(const SpinnerControl& spinner);

HWND CreateMacSlider(HWND parent, HINSTANCE instance, int command,
                     int x, int y, int width, int height,
                     int minimum, int maximum, int value);
void SetMacSliderRange(HWND slider, int minimum, int maximum);
void SetMacSliderValue(HWND slider, int value);
[[nodiscard]] int MacSliderValue(HWND slider);

HWND CreateActionButton(HWND parent, HINSTANCE instance, const wchar_t* text, int command,
                        int x, int y, int width, int height, bool accent = false);
void SetActionButtonAccent(HWND window, bool accent);
void SetActionButtonStyle(HWND window, ButtonStyle style);
void SetActionButtonText(HWND window, const wchar_t* text);

} // namespace PdfPP::ModernWin32
