#include "config_parser.h"
#include "router.h"
#include "win32_integration.h"
#include "settings_window.h"

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    int numArgs = 0;
    LPWSTR* szArgList = CommandLineToArgvW(GetCommandLineW(), &numArgs);

    if (szArgList != nullptr && numArgs > 1) {
        std::wstring arg1 = szArgList[1];
        LocalFree(szArgList);

        if (arg1 == L"--register") {
            burl::Win32Integration::RegisterAsDefaultBrowser();
            return 0;
        }

        if (arg1 == L"--settings") {
            return burl::SettingsWindow::Run();
        }

        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        auto config = burl::ConfigParser::LoadConfig();

        if (config.has_value()) {
            if (!burl::Router::RouteAndLaunch(*config, arg1)) {
                ShellExecuteW(nullptr, L"open", arg1.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else {
            ShellExecuteW(nullptr, L"open", arg1.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        CoUninitialize();
        return 0;
    }

    if (szArgList != nullptr) {
        LocalFree(szArgList);
    }

    return burl::SettingsWindow::Run();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)lpCmdLine;
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}
