#include <PdfPP/Win32/ReaderState.hpp>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>

#pragma comment(lib, "msimg32.lib")

namespace PdfPP::Win32 {

int scaleDip(const int value) {
    return MulDiv(value, static_cast<int>(currentDpi), USER_DEFAULT_SCREEN_DPI);
}

HCURSOR handDragCursor() {
    static HCURSOR cursor = []() -> HCURSOR {
        wchar_t executable[MAX_PATH]{};
        constexpr DWORD capacity = static_cast<DWORD>(
            sizeof(executable) / sizeof(executable[0]));
        const DWORD length = GetModuleFileNameW(nullptr, executable, capacity);
        if (length > 0 && length < capacity) {
            std::wstring cursorPath(executable, executable + length);
            const std::size_t separator = cursorPath.find_last_of(L"\\/");
            if (separator != std::wstring::npos) {
                cursorPath.resize(separator + 1U);
                cursorPath += L"resources\\Hand.cur";
                if (HCURSOR loaded = LoadCursorFromFileW(cursorPath.c_str())) {
                    return loaded;
                }
            }
        }
        return LoadCursorW(nullptr, IDC_HAND);
    }();
    return cursor;
}

std::wstring utf8ToWide(const char* text) {
    if (!text || !*text) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    result.resize(static_cast<std::size_t>(length - 1));
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
    // A 1x1 source is stretched by AlphaBlend. The former implementation
    // allocated a bitmap as large as every selected word on every mouse move,
    // which made long selections noticeably laggy.
    HBITMAP bitmap = CreateCompatibleBitmap(dc, 1, 1);
    if (!memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        return;
    }
    const HGDIOBJ previous = SelectObject(memory, bitmap);
    SetPixelV(memory, 0, 0, color);
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, alpha, 0 };
    AlphaBlend(dc, rect.left, rect.top, width, height,
        memory, 0, 0, 1, 1, blend);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

namespace {

bool chunksShareLine(const TextChunk& first, const TextChunk& second) {
    const double firstHeight = std::max(0.1, first.top - first.bottom);
    const double secondHeight = std::max(0.1, second.top - second.bottom);
    const double overlap = std::min(first.top, second.top) -
        std::max(first.bottom, second.bottom);
    return overlap >= std::min(firstHeight, secondHeight) * 0.35;
}

bool beginsWithWhitespace(const std::wstring& value) {
    return !value.empty() && std::iswspace(value.front()) != 0;
}

bool endsWithWhitespace(const std::wstring& value) {
    return !value.empty() && std::iswspace(value.back()) != 0;
}

} // namespace

void copySelectedText() {
    if (selectedTextSpans.empty()) return;

    std::wstring text;
    const TextChunk* previousChunk = nullptr;
    std::wstring previousPiece;
    for (const TextSelectionSpan& span : selectedTextSpans) {
        if (span.chunk >= textChunks.size()) continue;
        const TextChunk& chunk = textChunks[span.chunk];
        const std::wstring chunkText = utf8ToWide(chunk.text.c_str());
        const std::size_t begin = std::min(span.begin, chunkText.size());
        const std::size_t end = std::min(std::max(span.end, begin), chunkText.size());
        if (begin == end) continue;
        const std::wstring piece = chunkText.substr(begin, end - begin);

        if (previousChunk && !text.empty()) {
            if (!chunksShareLine(*previousChunk, chunk)) {
                if (!endsWithWhitespace(text)) text.append(L"\r\n");
            }
            else if (!endsWithWhitespace(previousPiece) &&
                     !beginsWithWhitespace(piece)) {
                // Text chunks are usually words or short runs. Preserve a
                // natural separator when adjacent runs on the same line do
                // not already contain whitespace.
                text.push_back(L' ');
            }
        }
        text += piece;
        previousChunk = &chunk;
        previousPiece = piece;
    }

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
