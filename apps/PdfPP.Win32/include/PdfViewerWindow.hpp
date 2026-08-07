#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <memory>

namespace PdfPP::Win32 {

class PdfViewerWindow final {
public:
    struct Impl;
    PdfViewerWindow();
    ~PdfViewerWindow();

    PdfViewerWindow(const PdfViewerWindow&) = delete;
    PdfViewerWindow& operator=(const PdfViewerWindow&) = delete;

    [[nodiscard]] bool Create(HINSTANCE instance, int showCommand);
    void OpenInitialDocument(const std::filesystem::path& path);
    [[nodiscard]] HWND GetWindow() const noexcept;

private:
    std::unique_ptr<Impl> impl_;

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                            WPARAM wParam, LPARAM lParam);
};

} // namespace PdfPP::Win32
