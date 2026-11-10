#include "router.h"

#include "win32_integration.h"

#include <algorithm>

#include <windows.h>

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

        }


        std::string Router::ExtractHost(std::string_view url) {
            size_t protoEnd = url.find("://");
            if (protoEnd != std::string_view::npos) {
                url.remove_prefix(protoEnd + 3);
            } else if (url.starts_with("//")) {
                url.remove_prefix(2);
            }
            size_t authEnd = url.find('@');
            size_t slashPos = url.find('/');
            if (authEnd != std::string_view::npos && (slashPos == std::string_view::npos || authEnd < slashPos)) {
                url.remove_prefix(authEnd + 1);
            }
            size_t end = url.find_first_of("/?#:");
            if (end != std::string_view::npos) {
                url = url.substr(0, end);
            }
            while (url.ends_with('.')) {
                url.remove_suffix(1);
            }
            std::string host(url);
            std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return host;
        }


        bool Router::DomainMatches(std::string_view host, std::string_view pattern) {
            std::string pat(pattern);
            std::transform(pat.begin(), pat.end(), pat.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string_view p = pat;
            if (p.starts_with("*.")) {
                p.remove_prefix(2);
            }
            if (host == p) {
                return true;
            }
            if (host.ends_with(p)) {
                size_t prefixLen = host.size() - p.size();
                if (prefixLen > 0 && host[prefixLen - 1] == '.') {
                    return true;
                }
            }
            return false;
        }


        std::optional<RouteResult> Router::ResolveRoute(const AppConfig& config, std::string_view rawUrl) {
            std::string host = ExtractHost(rawUrl);
            std::wstring wideUrl = Utf8ToWide(rawUrl);
            for (const auto& rule : config.rules) {
                if (!rule.enabled) {
                    continue;
                }
                for (const auto& domain : rule.domains) {
                    if (DomainMatches(host, domain)) {
                        for (const auto& browserKey : rule.fallback_chain) {
                            auto it = config.browsers.find(browserKey);
                            if (it != config.browsers.end() && Win32Integration::PathExists(it->second.exe_path)) {
                                return RouteResult{rule.name, browserKey, it->second, wideUrl};
                            }
                        }
                    }
                }
            }
            auto defIt = config.browsers.find(config.default_browser);
            if (defIt != config.browsers.end() && Win32Integration::PathExists(defIt->second.exe_path)) {
                return RouteResult{"Default", config.default_browser, defIt->second, wideUrl};
            }
            for (const auto& [key, browser] : config.browsers) {
                if (Win32Integration::PathExists(browser.exe_path)) {
                    return RouteResult{"Fallback", key, browser, wideUrl};
                }
            }
            return std::nullopt;
        }


        bool Router::RouteAndLaunch(const AppConfig& config, const std::wstring& rawUrl) {
            std::string urlUtf8 = WideToUtf8(rawUrl);
            auto route = ResolveRoute(config, urlUtf8);
            if (!route.has_value()) {
                return false;
            }
        return Win32Integration::LaunchBrowser(route->browser.exe_path, route->browser.args, rawUrl);
    }

}