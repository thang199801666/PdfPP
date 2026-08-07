#include <PdfPP/Win32/AppSettings.hpp>

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::wstring settingsFilePath() {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L"";
    const std::filesystem::path directory =
        std::filesystem::path(modulePath).parent_path();
    return (directory / L"PdfPP.Settings.txt").wstring();
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
        nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(),
        length, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1,
        nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), length);
    return result;
}

// Percent-encode characters that would otherwise be ambiguous in the flat
// "key=value;value;value" settings lines.
std::string encodeValue(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte == '=' || byte == ';' || byte == '%' || byte == '\\') {
            const char hex[] = "0123456789ABCDEF";
            result += '%';
            result += hex[(byte >> 4) & 0x0F];
            result += hex[byte & 0x0F];
        } else {
            result += static_cast<char>(byte);
        }
    }
    return result;
}

std::string decodeValue(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const char high = value[index + 1];
            const char low = value[index + 2];
            auto hexValue = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            const int hi = hexValue(high);
            const int lo = hexValue(low);
            if (hi >= 0 && lo >= 0) {
                result += static_cast<char>((hi << 4) | lo);
                index += 2;
                continue;
            }
        }
        result += value[index];
    }
    return result;
}

void writeSettings(const PdfPP::Win32::AppSettings& settings, const std::wstring& path) {
    std::ofstream stream(path, std::ios::out | std::ios::trunc);
    if (!stream) return;
    stream << "[PdfPP Reader]\n";
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "zoom=%.4f\n", settings.zoom);
    stream << buffer;
    stream << "continuous=" << (settings.continuous ? "1" : "0") << "\n";
    stream << "sidebar=" << (settings.sidebarVisible ? "1" : "0") << "\n";
    stream << "sidebarWidth=" << settings.sidebarWidth << "\n";
    stream << "handtool=" << (settings.handTool ? "1" : "0") << "\n";
    stream << "toolsPanel=" << (settings.toolsVisible ? "1" : "0") << "\n";
    stream << "toolsWidth=" << settings.toolsWidth << "\n";
    stream << "pageShadow=" << (settings.showPageShadow ? "1" : "0") << "\n";
    stream << "recent=";
    for (std::size_t index = 0; index < settings.recentFiles.size(); ++index) {
        if (index != 0) stream << ";";
        stream << encodeValue(wideToUtf8(settings.recentFiles[index]));
    }
    stream << "\n";
    for (const auto& favorite : settings.favorites) {
        stream << "favorite=" << favorite.page << "|"
               << encodeValue(wideToUtf8(favorite.title)) << "|"
               << encodeValue(wideToUtf8(favorite.path)) << "\n";
    }
}

void parseSettings(PdfPP::Win32::AppSettings& settings, const std::wstring& path) {
    std::ifstream stream(path, std::ios::in);
    if (!stream) return;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() == '[') continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "zoom") {
            settings.zoom = std::clamp(
                static_cast<double>(std::strtod(value.c_str(), nullptr)),
                PdfPP::Win32::AppSettings::kMinimumZoom,
                PdfPP::Win32::AppSettings::kMaximumZoom);
        } else if (key == "continuous") {
            settings.continuous = value == "1";
        } else if (key == "sidebar") {
            settings.sidebarVisible = value == "1";
        } else if (key == "sidebarWidth") {
            settings.sidebarWidth = std::clamp(std::atoi(value.c_str()), 170, 520);
        } else if (key == "handtool") {
            settings.handTool = value == "1";
        } else if (key == "toolsPanel") {
            settings.toolsVisible = value == "1";
        } else if (key == "toolsWidth") {
            settings.toolsWidth = std::clamp(std::atoi(value.c_str()), 240, 420);
        } else if (key == "pageShadow") {
            settings.showPageShadow = value == "1";
        } else if (key == "recent") {
            settings.recentFiles.clear();
            std::size_t start = 0;
            while (start < value.size()) {
                const std::size_t end = value.find(';', start);
                const std::string encoded = value.substr(start,
                    end == std::string::npos ? std::string::npos : end - start);
                if (!encoded.empty()) {
                    const std::wstring decoded = utf8ToWide(decodeValue(encoded));
                    if (std::filesystem::exists(decoded)) {
                        settings.recentFiles.push_back(decoded);
                    }
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
        } else if (key == "favorite") {
            const std::size_t firstBar = value.find('|');
            if (firstBar == std::string::npos) continue;
            const std::size_t secondBar = value.find('|', firstBar + 1);
            if (secondBar == std::string::npos) continue;
            PdfPP::Win32::Favorite favorite;
            favorite.page = std::atoi(value.substr(0, firstBar).c_str());
            favorite.title = utf8ToWide(decodeValue(
                value.substr(firstBar + 1, secondBar - firstBar - 1)));
            favorite.path = utf8ToWide(decodeValue(value.substr(secondBar + 1)));
            if (!favorite.path.empty() && favorite.page >= 0) {
                settings.favorites.push_back(std::move(favorite));
            }
        }
    }
}

} // namespace

namespace PdfPP::Win32 {

bool AppSettings::HasRecent(const std::wstring& path) const {
    return std::find(recentFiles.begin(), recentFiles.end(), path) != recentFiles.end();
}

void AppSettings::AddRecent(const std::wstring& path) {
    auto existing = std::find(recentFiles.begin(), recentFiles.end(), path);
    if (existing != recentFiles.end()) {
        recentFiles.erase(existing);
    }
    recentFiles.insert(recentFiles.begin(), path);
    while (recentFiles.size() > kMaxRecentFiles) recentFiles.pop_back();
}

bool AppSettings::HasFavorite(const std::wstring& path, int page) const {
    return std::find_if(favorites.begin(), favorites.end(),
        [&](const Favorite& favorite) {
            return favorite.path == path && favorite.page == page;
        }) != favorites.end();
}

void AppSettings::AddFavorite(Favorite favorite) {
    if (HasFavorite(favorite.path, favorite.page)) return;
    favorites.push_back(std::move(favorite));
    while (favorites.size() > kMaxFavorites) favorites.pop_back();
}

void AppSettings::RemoveFavorite(const std::wstring& path, int page) {
    favorites.erase(std::remove_if(favorites.begin(), favorites.end(),
        [&](const Favorite& favorite) {
            return favorite.path == path && favorite.page == page;
        }), favorites.end());
}

void LoadSettings(AppSettings& settings) {
    const std::wstring path = settingsFilePath();
    if (path.empty() || !std::filesystem::exists(path)) return;
    parseSettings(settings, path);
}

void SaveSettings(const AppSettings& settings) {
    const std::wstring path = settingsFilePath();
    if (path.empty()) return;
    writeSettings(settings, path);
}

} // namespace PdfPP::Win32
