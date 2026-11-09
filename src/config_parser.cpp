#include "config_parser.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <system_error>

namespace burl {

namespace {

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (sizeNeeded <= 0) {
        return {};
    }
    std::wstring result(sizeNeeded, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), sizeNeeded);
    return result;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) {
        return {};
    }
    std::string result(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), sizeNeeded, nullptr, nullptr);
    return result;
}

template<typename T>
T GetValue(const nlohmann::json& j, const std::string& key, const T& defaultValue) {
    if (j.contains(key) && !j[key].is_null()) {
        try {
            return j[key].get<T>();
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

}

std::filesystem::path ConfigParser::GetConfigPath() {
    PWSTR roamingPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roamingPath);
    if (SUCCEEDED(hr) && roamingPath != nullptr) {
        std::filesystem::path configPath(roamingPath);
        CoTaskMemFree(roamingPath);
        return configPath / L"burl" / L"config.json";
    }
    return std::filesystem::current_path() / L"config.json";
}

std::filesystem::path ConfigParser::GetDefaultConfigTemplatePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        return std::filesystem::current_path() / L"config" / L"default_rules.json";
    }
    buffer.resize(length);
    std::filesystem::path exePath(buffer);
    return exePath.parent_path() / L"config" / L"default_rules.json";
}

bool ConfigParser::EnsureConfigExists() {
    try {
        const std::filesystem::path configPath = GetConfigPath();
        if (std::filesystem::exists(configPath)) {
            return true;
        }
        std::filesystem::create_directories(configPath.parent_path());
        const std::filesystem::path templatePath = GetDefaultConfigTemplatePath();
        if (std::filesystem::exists(templatePath)) {
            std::filesystem::copy_file(templatePath, configPath, std::filesystem::copy_options::overwrite_existing);
            return true;
        }
        const char* minimalFallback = R"({
  "version": 1,
  "default_browser": "edge",
  "browsers": {},
  "rules": []
})";
        std::ofstream fallbackFile(configPath);
        if (!fallbackFile.is_open()) {
            return false;
        }
        fallbackFile << minimalFallback;
        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}

std::optional<AppConfig> ConfigParser::LoadConfig() {
    if (!EnsureConfigExists()) {
        return std::nullopt;
    }
    const std::filesystem::path configPath = GetConfigPath();
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return std::nullopt;
    }
    try {
        nlohmann::json data = nlohmann::json::parse(file);
        AppConfig config;
        config.version = GetValue<int>(data, "version", 1);
        config.default_browser = GetValue<std::string>(data, "default_browser", "edge");
        if (data.contains("browsers") && data["browsers"].is_object()) {
            for (const auto& [key, val] : data["browsers"].items()) {
                if (!val.is_object()) continue;
                BrowserConfig browser;
                browser.name = Utf8ToWide(GetValue<std::string>(val, "name", ""));
                std::string exeUtf8 = GetValue<std::string>(val, "exe_path", "");
                browser.exe_path = std::filesystem::path(Utf8ToWide(exeUtf8));
                browser.args = Utf8ToWide(GetValue<std::string>(val, "args", ""));
                config.browsers[key] = std::move(browser);
            }
        }
        if (data.contains("rules") && data["rules"].is_array()) {
            for (const auto& item : data["rules"]) {
                if (!item.is_object()) continue;
                Rule rule;
                rule.name = GetValue<std::string>(item, "name", "");
                rule.enabled = GetValue<bool>(item, "enabled", true);
                if (item.contains("domains") && item["domains"].is_array()) {
                    for (const auto& domain : item["domains"]) {
                        if (domain.is_string()) {
                            rule.domains.push_back(domain.get<std::string>());
                        }
                    }
                }
                if (item.contains("fallback_chain") && item["fallback_chain"].is_array()) {
                    for (const auto& browserKey : item["fallback_chain"]) {
                        if (browserKey.is_string()) {
                            rule.fallback_chain.push_back(browserKey.get<std::string>());
                        }
                    }
                }
                config.rules.push_back(std::move(rule));
            }
        }
        return config;
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

bool ConfigParser::SaveConfig(const AppConfig& config) {
    try {
        nlohmann::json root;
        root["version"] = config.version;
        root["default_browser"] = config.default_browser;
        nlohmann::json browsersObj = nlohmann::json::object();
        for (const auto& [key, browser] : config.browsers) {
            browsersObj[key] = {
                {"name", WideToUtf8(browser.name)},
                {"exe_path", WideToUtf8(browser.exe_path.wstring())},
                {"args", WideToUtf8(browser.args)}
            };
        }
        root["browsers"] = browsersObj;
        nlohmann::json rulesArray = nlohmann::json::array();
        for (const auto& rule : config.rules) {
            rulesArray.push_back({
                {"name", rule.name},
                {"enabled", rule.enabled},
                {"domains", rule.domains},
                {"fallback_chain", rule.fallback_chain}
            });
        }
        root["rules"] = rulesArray;
        const std::filesystem::path configPath = GetConfigPath();
        std::ofstream file(configPath);
        if (!file.is_open()) {
            return false;
        }
        file << root.dump(4);
        return file.good();
    }
    catch (const std::exception&) {
        return false;
    }
}

}