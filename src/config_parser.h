#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <nlohmann/json.hpp>

namespace burl {
    struct BrowserConfig {
        std::wstring name;
        //wstring used for wide character's  / literal's works great with api's
        std::filesystem::path exe_path;
        std::wstring args;
    };
    struct Rule {
        std::wstring name;
        bool enabled;
        std::vector<std::string> domains;
        std::vector<std::string> fallback_chain;

    };
    struct AppConfig {
        int version;
        std::string default_browser;
        std::unordered_map<std::string, BrowserConfig> browsers;
        std::vector<Rule> rules;
    };
    class ConfigParser {

    public:
        static std::filesystem::path GetConfigPath();

        static std::filesystem::path GetDefaultConfigTemplatePath();

        static bool EnsureConfigExists();

        static std::optional<AppConfig> LoadConfig();

        static bool SaveConfig(const AppConfig& config);
    };
}
