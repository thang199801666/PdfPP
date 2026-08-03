#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#pragma comment(lib, "msimg32.lib")

namespace PdfPP::Win32 {

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

double textPixelScale() {
    return zoom * static_cast<double>(currentDpi) / 72.0;
}

int wheelScrollDistance(const int viewport) {
    UINT scrollLines = 3;
    if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0)) {
        scrollLines = 3;
    }
    if (scrollLines == WHEEL_PAGESCROLL) {
        return std::max(scaleDip(48), viewport - scaleDip(32));
    }
    return std::max(scaleDip(16),
        static_cast<int>(std::max(1U, scrollLines)) * scaleDip(16));
}

// Convert a raw wheel delta (units of WHEEL_DELTA) into a pixel distance.
// scrollY grows as the view moves toward the end of the document because
// painting subtracts it from the page origin, so wheel up (positive delta)
// must reduce scrollY: the delta is negated. Keeping the remainder as a
// double lets high-resolution trackpad deltas accumulate smoothly instead
// of quantizing to whole notches.
void addWheelDelta(double& remainder, const int rawDelta, const int viewport) {
    remainder += -static_cast<double>(rawDelta) * wheelScrollDistance(viewport) / WHEEL_DELTA;
    const int step = static_cast<int>(remainder);
    remainder -= step;
    if (step == 0) return;
    if (scrollContinuousBy(step)) return;
    setCanvasScroll(SB_VERT, scrollY + step);
}

RECT chunkClientRect(const TextChunk& chunk, const RECT& client, const int pageTop) {
    const double scale = textPixelScale();
    const int left = pageLeft(client, pixelWidth) - scrollX;
    const int x = left + static_cast<int>(std::lround(chunk.left * scale));
    const int y = pageTop + pixelHeight - static_cast<int>(std::lround(chunk.top * scale));
    const int width = std::max(1, static_cast<int>(std::lround((chunk.right - chunk.left) * scale)));
    const int height = std::max(1, static_cast<int>(std::lround((chunk.top - chunk.bottom) * scale)));
    return RECT{ x, y, x + width, y + height };
}

void fillOverlay(HDC dc, const RECT& rect, const BYTE alpha, const COLORREF color) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return;
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    if (!memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        return;
    }
    const HGDIOBJ previous = SelectObject(memory, bitmap);
    const HBRUSH brush = CreateSolidBrush(color);
    RECT local{ 0, 0, width, height };
    FillRect(memory, &local, brush);
    DeleteObject(brush);
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, alpha, 0 };
    AlphaBlend(dc, rect.left, rect.top, width, height,
        memory, 0, 0, width, height, blend);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

void copySelectedText() {
    if (selectedChunks.empty()) return;
    std::string utf8;
    for (const std::size_t index : selectedChunks) {
        if (index >= textChunks.size()) continue;
        if (!utf8.empty()) utf8.push_back(' ');
        utf8 += textChunks[index].text;
    }
    const std::wstring text = utf8ToWide(utf8.c_str());
    if (text.empty() || !OpenClipboard(mainWindow)) return;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1U) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        auto* destination = static_cast<wchar_t*>(GlobalLock(memory));
        if (destination) {
            std::memcpy(destination, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory) GlobalFree(memory);
    CloseClipboard();
}

} // namespace PdfPP::Win32
