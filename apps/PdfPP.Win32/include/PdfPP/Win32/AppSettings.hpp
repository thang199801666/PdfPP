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

    double zoom{ 1.0 };
    bool continuous{ true };
    bool sidebarVisible{ true };
    bool handTool{};
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
