#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace PdfPP::Win32 {

struct Favorite final {
    std::wstring path;
    int page{ -1 };
    std::wstring title;
};

struct AppSettings final {
    static constexpr std::size_t kMaxRecentFiles = 10;
    static constexpr std::size_t kMaxFavorites = 32;
    // Keep reader zoom limits in one place so runtime input and persisted
    // settings cannot drift apart. 800% is intentionally high enough for
    // detailed inspection without the extreme bitmap sizes of 1600%+.
    static constexpr double kMinimumZoom = 0.25;
    static constexpr double kMaximumZoom = 8.00;

    double zoom{ 1.0 };
    bool continuous{ true };
    bool sidebarVisible{ true };
    int sidebarWidth{ 242 };
    bool handTool{};
    bool toolsVisible{ true };
    int toolsWidth{ 292 };
    bool showPageShadow{ true };
    std::vector<std::wstring> recentFiles;
    std::vector<Favorite> favorites;

    [[nodiscard]] bool HasRecent(const std::wstring& path) const;
    void AddRecent(const std::wstring& path);
    [[nodiscard]] bool HasFavorite(const std::wstring& path, int page) const;
    void AddFavorite(Favorite favorite);
    void RemoveFavorite(const std::wstring& path, int page);
};

// Loads and saves application settings as a small UTF-8 text file next to the
// executable (SumatraPDF-style settings file). Failures are silent and keep
// the default state.
void LoadSettings(AppSettings& settings);
void SaveSettings(const AppSettings& settings);

} // namespace PdfPP::Win32
