#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>

namespace PdfPP::ModernWin32 {

struct Theme final {
    // Acrobat-inspired light defaults. Keep all colors centralized so other
    // Win32 applications can reuse the same visual language.
    static constexpr COLORREF window = RGB(255, 255, 255);
    static constexpr COLORREF toolbar = RGB(246, 247, 249);
    static constexpr COLORREF sidebar = RGB(250, 251, 252);
    static constexpr COLORREF canvas = RGB(232, 235, 239);
    static constexpr COLORREF control = RGB(255, 255, 255);
    static constexpr COLORREF controlHover = RGB(232, 240, 252);
    static constexpr COLORREF text = RGB(31, 41, 51);
    static constexpr COLORREF mutedText = RGB(99, 109, 122);
    static constexpr COLORREF accent = RGB(20, 115, 230);
    static constexpr COLORREF border = RGB(207, 213, 221);
    static constexpr COLORREF button = RGB(255, 255, 255);
    static constexpr COLORREF buttonHover = RGB(232, 240, 252);
    static constexpr COLORREF buttonPressed = RGB(214, 228, 247);
    static constexpr COLORREF buttonAccent = RGB(20, 115, 230);
    static constexpr COLORREF status = RGB(244, 246, 248);
};

struct Layout final {
    static constexpr int menuHeight = 24;
    static constexpr int ribbonHeight = 58;
    static constexpr int sidebarWidth = 242;
    static constexpr int statusHeight = 26;
    static constexpr int controlHeight = 26;
    static constexpr int gutter = 8;
};

void Initialize(HINSTANCE instance);
void ApplyDarkMode(HWND window);
HFONT UiFontForDpi(UINT dpi, int pointSize = 9, bool bold = false);
HFONT UiFont(int pointSize = 9, bool bold = false);
void ApplyFont(HWND window, int pointSize = 9, bool bold = false);
void SetControlColors(HDC dc, COLORREF background, COLORREF foreground);
HWND CreateActionButton(HWND parent, HINSTANCE instance, const wchar_t* text, int command,
                        int x, int y, int width, int height, bool accent = false);
void SetActionButtonAccent(HWND window, bool accent);
void SetActionButtonText(HWND window, const wchar_t* text);

} // namespace PdfPP::ModernWin32
