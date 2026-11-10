#pragma once

#include "config_parser.h"

#include <string>

#include <string_view>

#include <optional>

namespace burl {


    struct RouteResult {
        std::string matched_rule_name;
        std::string browser_key;
        BrowserConfig browser;
        std::wstring target_url;
    };


    class Router {
    public:
        static std::string ExtractHost(std::string_view url);
        static bool DomainMatches(std::string_view host, std::string_view pattern);
        static std::optional<RouteResult> ResolveRoute(const AppConfig& config, std::string_view rawUrl);
        static bool RouteAndLaunch(const AppConfig& config, const std::wstring& rawUrl);
    };

}