#pragma once

#include <string>

#include <vector>

#include <filesystem>

#include <optional>

#include <windows.h>

namespace burl {


    struct InstalledBrowser {
        std::string key;
        std::wstring display_name;
        std::filesystem::path exe_path;

    };


    class Win32Integration {
        public:
            static bool PathExists(const std::filesystem::path& path);
            static std::vector<InstalledBrowser> DetectInstalledBrowsers();
            static bool RegisterAsDefaultBrowser();
            static bool LaunchBrowser(const std::filesystem::path& exePath, const std::wstring& args, const std::wstring& url);
            static void OpenDefaultAppsSettings();


    };

}