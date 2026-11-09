#include "win32_integration.h"

#include <shellapi.h>
#include <algorithm>
#include <system_error>

namespace burl {

namespace {

std::optional<std::wstring> ReadRegistryString(HKEY rootKey, const std::wstring& subKey, const std::wstring& valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(rootKey, subKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    DWORD bytesNeeded = 0;
    DWORD type = 0;
    const wchar_t* valPtr = valueName.empty() ? nullptr : valueName.c_str();
    if (RegQueryValueExW(hKey, valPtr, nullptr, &type, nullptr, &bytesNeeded) != ERROR_SUCCESS || bytesNeeded == 0) {
        RegCloseKey(hKey);
        return std::nullopt;
    }
    std::wstring result(bytesNeeded / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(hKey, valPtr, nullptr, &type, reinterpret_cast<LPBYTE>(result.data()), &bytesNeeded) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return std::nullopt;
    }
    RegCloseKey(hKey);
    while (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

bool WriteRegistryString(HKEY rootKey, const std::wstring& subKey, const std::wstring& valueName, const std::wstring& data) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(rootKey, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const wchar_t* valPtr = valueName.empty() ? nullptr : valueName.c_str();
    DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    LONG res = RegSetValueExW(hKey, valPtr, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), bytes);
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

std::filesystem::path SanitizeExePath(std::wstring raw) {
    while (!raw.empty() && (raw.front() == L' ' || raw.front() == L'\t')) {
        raw.erase(raw.begin());
    }
    if (raw.starts_with(L"\"")) {
        size_t endQuote = raw.find(L'\"', 1);
        if (endQuote != std::wstring::npos) {
            return raw.substr(1, endQuote - 1);
        }
    }
    std::wstring lower = raw;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    size_t exePos = lower.find(L".exe");
    if (exePos != std::wstring::npos) {
        return raw.substr(0, exePos + 4);
    }
    return raw;
}

std::string NormalizeBrowserKey(const std::wstring& str) {
    std::wstring lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    if (lower.find(L"brave") != std::wstring::npos) return "brave";
    if (lower.find(L"chrome") != std::wstring::npos) return "chrome";
    if (lower.find(L"edge") != std::wstring::npos || lower.find(L"msedge") != std::wstring::npos) return "edge";
    if (lower.find(L"firefox") != std::wstring::npos) return "firefox";
    if (lower.find(L"opera") != std::wstring::npos) return "opera";
    if (lower.find(L"vivaldi") != std::wstring::npos) return "vivaldi";
    std::string res;
    for (wchar_t c : lower) {
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) {
            res.push_back(static_cast<char>(c));
        }
    }
    return res.empty() ? "browser" : res;
}

}

bool Win32Integration::PathExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::vector<InstalledBrowser> Win32Integration::DetectInstalledBrowsers() {
    std::vector<InstalledBrowser> results;
    const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    const std::wstring baseKey = L"SOFTWARE\\Clients\\StartMenuInternet";
    for (HKEY root : roots) {
        HKEY hClients = nullptr;
        if (RegOpenKeyExW(root, baseKey.c_str(), 0, KEY_READ, &hClients) != ERROR_SUCCESS) {
            continue;
        }
        DWORD index = 0;
        wchar_t subKeyName[256];
        DWORD subKeyLen = 256;
        while (RegEnumKeyExW(hClients, index++, subKeyName, &subKeyLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            std::wstring browserKeyName(subKeyName, subKeyLen);
            subKeyLen = 256;
            std::wstring commandKey = baseKey + L"\\" + browserKeyName + L"\\shell\\open\\command";
            auto rawCmd = ReadRegistryString(root, commandKey, L"");
            if (!rawCmd.has_value() || rawCmd->empty()) {
                continue;
            }
            std::filesystem::path exePath = SanitizeExePath(*rawCmd);
            if (!PathExists(exePath)) {
                continue;
            }
            auto displayNameOpt = ReadRegistryString(root, baseKey + L"\\" + browserKeyName, L"");
            std::wstring displayName = (displayNameOpt.has_value() && !displayNameOpt->empty()) ? *displayNameOpt : browserKeyName;
            std::string key = NormalizeBrowserKey(browserKeyName + L" " + displayName);
            bool exists = false;
            std::error_code eqEc;
            for (const auto& b : results) {
                if (b.key == key || std::filesystem::equivalent(b.exe_path, exePath, eqEc)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                results.push_back(InstalledBrowser{key, displayName, exePath});
            }
        }
        RegCloseKey(hClients);
    }
    return results;
}

bool Win32Integration::RegisterAsDefaultBrowser() {
    std::wstring exeBuffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, exeBuffer.data(), static_cast<DWORD>(exeBuffer.size()));
    if (len == 0) {
        return false;
    }
    exeBuffer.resize(len);
    std::wstring quotedExe = L"\"" + exeBuffer + L"\"";
    std::wstring openCmd = quotedExe + L" \"%1\"";
    std::wstring iconVal = exeBuffer + L",0";
    bool ok = true;
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burlHTML", L"", L"burl HTML Document");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burlHTML\\Application", L"ApplicationName", L"burl");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burlHTML\\Application", L"ApplicationDescription", L"burl link router");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burlHTML\\Application", L"ApplicationIcon", iconVal);
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burlHTML\\DefaultIcon", L"", iconVal);
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burlHTML\\shell\\open\\command", L"", openCmd);
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burl\\Capabilities", L"ApplicationName", L"burl");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burl\\Capabilities", L"ApplicationDescription", L"burl zero-latency link router");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burl\\Capabilities\\URLAssociations", L"http", L"burlHTML");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burl\\Capabilities\\URLAssociations", L"https", L"burlHTML");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burl\\Capabilities\\FileAssociations", L".htm", L"burlHTML");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\Classes\\burl\\Capabilities\\FileAssociations", L".html", L"burlHTML");
    ok = ok && WriteRegistryString(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"burl", L"Software\\Classes\\burl\\Capabilities");
    return ok;
}

bool Win32Integration::LaunchBrowser(const std::filesystem::path& exePath, const std::wstring& args, const std::wstring& url) {
    std::wstring cmdLine = L"\"" + exePath.wstring() + L"\" ";
    if (!args.empty()) {
        cmdLine += args + L" ";
    }
    cmdLine += L"\"" + url + L"\"";
    std::vector<wchar_t> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL success = CreateProcessW(exePath.c_str(), cmdBuffer.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (success) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

void Win32Integration::OpenDefaultAppsSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
}

}