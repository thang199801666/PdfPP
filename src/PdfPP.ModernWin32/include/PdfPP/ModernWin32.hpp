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
