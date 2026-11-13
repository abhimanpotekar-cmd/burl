#include "settings_window.h"
#include "config_parser.h"
#include "win32_integration.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <d3d11.h>
#include <windows.h>
#include <vector>
#include <string>
#include <sstream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace burl {

namespace {

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
UINT g_ResizeWidth = 0;
UINT g_ResizeHeight = 0;

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) {
        return false;
    }
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }
    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
    return result;
}

}

int SettingsWindow::Run() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"BurlSettingsWindowClass";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowW(wc.lpszClassName, L"burl - Settings & Link Router", WS_OVERLAPPEDWINDOW, 100, 100, 960, 680, nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    auto loadedConfig = ConfigParser::LoadConfig();
    AppConfig config = loadedConfig.value_or(AppConfig{});

    std::vector<InstalledBrowser> detectedBrowsers = Win32Integration::DetectInstalledBrowsers();
    std::string statusMessage;
    char domainBuffer[256] = "";
    char newRuleName[128] = "New Rule";

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("burl Configuration", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "burl - Browser Link Router & Automated Fallback Chains");
        ImGui::Separator();

        if (ImGui::Button("Register burl as Default Browser")) {
            if (Win32Integration::RegisterAsDefaultBrowser()) {
                statusMessage = "Registered burl in Registry! Open Windows Settings to set as default.";
            } else {
                statusMessage = "Failed to register burl in Registry.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Windows Default Apps Settings")) {
            Win32Integration::OpenDefaultAppsSettings();
        }

        if (!statusMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", statusMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::CollapsingHeader("System Default Browser Fallback", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Global Default Browser (used when no rule matches):");
            if (ImGui::BeginCombo("##default_browser_combo", config.default_browser.c_str())) {
                for (const auto& [bKey, bVal] : config.browsers) {
                    bool isSelected = (config.default_browser == bKey);
                    std::string label = bKey + " (" + WideToUtf8(bVal.name) + ")";
                    if (ImGui::Selectable(label.c_str(), isSelected)) {
                        config.default_browser = bKey;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (ImGui::CollapsingHeader("Installed Browsers & Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Rescan Installed Browsers")) {
                detectedBrowsers = Win32Integration::DetectInstalledBrowsers();
                for (const auto& ib : detectedBrowsers) {
                    if (!config.browsers.contains(ib.key)) {
                        config.browsers[ib.key] = BrowserConfig{ib.display_name, ib.exe_path, L""};
                    }
                }
                statusMessage = "Rescanned and merged detected browsers.";
            }
            ImGui::Spacing();
            for (const auto& [bKey, bVal] : config.browsers) {
                bool exists = Win32Integration::PathExists(bVal.exe_path);
                ImGui::Bullet();
                ImGui::Text("%s - %s", bKey.c_str(), WideToUtf8(bVal.name).c_str());
                ImGui::SameLine();
                if (exists) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[Installed]");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[Not Found on Disk]");
                }
                ImGui::TextDisabled("  Path: %s", WideToUtf8(bVal.exe_path.wstring()).c_str());
            }
        }

        if (ImGui::CollapsingHeader("Domain Routing Rules & Fallback Chains", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t r = 0; r < config.rules.size(); ++r) {
                ImGui::PushID(static_cast<int>(r));
                Rule& rule = config.rules[r];
                ImGui::Checkbox("##rule_enabled", &rule.enabled);
                ImGui::SameLine();
                char ruleNameBuf[128];
                strncpy_s(ruleNameBuf, rule.name.c_str(), sizeof(ruleNameBuf));
                if (ImGui::InputText("##rule_name", ruleNameBuf, sizeof(ruleNameBuf))) {
                    rule.name = ruleNameBuf;
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Rule")) {
                    config.rules.erase(config.rules.begin() + r);
                    ImGui::PopID();
                    break;
                }

                ImGui::Text("  Domains:");
                for (size_t d = 0; d < rule.domains.size(); ++d) {
                    ImGui::PushID(static_cast<int>(d));
                    ImGui::BulletText("%s", rule.domains[d].c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) {
                        rule.domains.erase(rule.domains.begin() + d);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }

                ImGui::PushItemWidth(220);
                ImGui::InputText("##add_domain_input", domainBuffer, sizeof(domainBuffer));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Add Domain") && domainBuffer[0] != '\0') {
                    rule.domains.push_back(domainBuffer);
                    domainBuffer[0] = '\0';
                }

                ImGui::Text("  Fallback Chain Priority (Left/Top = First Attempt):");
                for (size_t f = 0; f < rule.fallback_chain.size(); ++f) {
                    ImGui::PushID(static_cast<int>(f + 100));
                    ImGui::Text("  %zu. %s", f + 1, rule.fallback_chain[f].c_str());
                    ImGui::SameLine();
                    if (f > 0 && ImGui::SmallButton("^")) {
                        std::swap(rule.fallback_chain[f], rule.fallback_chain[f - 1]);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::SameLine();
                    if (f + 1 < rule.fallback_chain.size() && ImGui::SmallButton("v")) {
                        std::swap(rule.fallback_chain[f], rule.fallback_chain[f + 1]);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("remove")) {
                        rule.fallback_chain.erase(rule.fallback_chain.begin() + f);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }

                if (ImGui::BeginCombo("##add_browser_combo", "+ Add Browser to Chain")) {
                    for (const auto& [bKey, _] : config.browsers) {
                        if (ImGui::Selectable(bKey.c_str())) {
                            rule.fallback_chain.push_back(bKey);
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::PushItemWidth(200);
            ImGui::InputText("##new_rule_name", newRuleName, sizeof(newRuleName));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Create New Rule") && newRuleName[0] != '\0') {
                Rule newRule;
                newRule.name = newRuleName;
                newRule.enabled = true;
                if (!config.default_browser.empty()) {
                    newRule.fallback_chain.push_back(config.default_browser);
                }
                config.rules.push_back(newRule);
                strncpy_s(newRuleName, "New Rule", sizeof(newRuleName));
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Save Configuration", ImVec2(180, 36))) {
            if (ConfigParser::SaveConfig(config)) {
                statusMessage = "Configuration saved successfully to %APPDATA%\\burl\\config.json!";
            } else {
                statusMessage = "Failed to save configuration.";
            }
        }

        ImGui::End();

        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

}
