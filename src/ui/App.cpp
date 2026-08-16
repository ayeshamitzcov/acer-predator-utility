#include "ui/App.h"

#include "acer/AcerService.h"
#include "acer/BatteryHealth.h"
#include "acer/GamingWmi.h"
#include "acer/KillPredatorSense.h"
#include "acer/RgbKb.h"
#include "common/Log.h"
#include "common/Settings.h"
#include "common/Startup.h"
#include "display/DisplayMode.h"
#include "power/DiscreteGpu.h"
#include "power/PowerPlans.h"
#include "sensors/NvapiOc.h"
#include "sensors/Sensors.h"
#include "ui/Hotkeys.h"
#include "ui/Overlay.h"
#include "ui/Tray.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace predator::ui {
namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
HWND g_hwnd = nullptr;
bool g_running = true;
bool g_restore_fans = false;
GamingWmi g_wmi;
Settings g_settings;
Sensors g_sensors;
Snapshot g_snap;
NvapiOc g_nvapi;
int g_cpu_fan_slider = 50;
int g_gpu_fan_slider = 50;
int g_fan_ui_mode = 0;  // 0 auto, 1 max, 2 manual
int g_thermal = 1;
bool g_charge_limit = false;
std::string g_status;
std::string g_probe;
auto g_last_poll = std::chrono::steady_clock::now();
bool g_last_ac = true;
bool g_dgpu_off_by_app = false;
bool g_power_known = false;
float g_auto_cpu_rpm = 2200;
float g_auto_gpu_rpm = 2200;
int g_saved_brightness = -1;
bool g_imgui_ready = false;
bool g_inside_imgui = false;
bool g_in_apply = false;
bool g_busy = false;
std::string g_busy_text = "Starting";
std::optional<ThermalMode> g_queued_thermal;
bool g_queued_fan = false;
bool g_starting = false;

void Status(const std::string& s) {
    g_status = s;
    Log(s);
}

void CreateRtv() {
    ID3D11Texture2D* bb = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_device->CreateRenderTargetView(bb, nullptr, &g_rtv);
    bb->Release();
}

bool CreateDevice(HWND h) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = h;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    IDXGIAdapter* intel = nullptr;
    IDXGIFactory1* fac = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&fac))) &&
        fac) {
        IDXGIAdapter* a = nullptr;
        for (UINT i = 0; fac->EnumAdapters(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC desc{};
            a->GetDesc(&desc);
            if (desc.VendorId == 0x8086) {
                intel = a;
                break;
            }
            a->Release();
        }
        fac->Release();
    }

    HRESULT hr = E_FAIL;
    if (intel) {
        hr = D3D11CreateDeviceAndSwapChain(intel, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &fl, &g_ctx);
        intel->Release();
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                           2, D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &fl,
                                           &g_ctx);
    }
    if (FAILED(hr)) {
        return false;
    }
    CreateRtv();
    return true;
}

void CleanupDevice() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
    if (g_swap) {
        g_swap->Release();
        g_swap = nullptr;
    }
    if (g_ctx) {
        g_ctx->Release();
        g_ctx = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
}

const char* ModeLabel(ThermalMode mode) {
    switch (mode) {
        case ThermalMode::Silent:
            return "Quiet";
        case ThermalMode::Eco:
            return "Battery saver";
        case ThermalMode::Balanced:
            return "Normal";
        case ThermalMode::Performance:
            return "ESports";
        case ThermalMode::Turbo:
            return "Turbo";
        default:
            return "mode";
    }
}

void DrawBusyOverlay() {
    if (!g_busy) {
        return;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("##boot", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.42f);
    const float t = static_cast<float>(ImGui::GetTime()) * 7.f;
    for (int i = 0; i < 12; ++i) {
        const float a = t + i * (6.2831853f / 12.f);
        const float alpha = 0.18f + 0.82f * (static_cast<float>(i) / 11.f);
        dl->AddCircleFilled(ImVec2(c.x + cosf(a) * 30.f, c.y + sinf(a) * 30.f), 4.4f,
                            IM_COL32(255, 140, 30, static_cast<int>(alpha * 255)));
    }
    ImGui::SetCursorScreenPos(ImVec2(c.x - 220.f, c.y + 52.f));
    ImGui::PushTextWrapPos(c.x + 220.f);
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextColored(ImVec4(1.f, 0.45f, 0.05f, 1), "PREDATOR UTILITY");
    ImGui::SetWindowFontScale(1.f);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextWrapped("%s", g_busy_text.c_str());
    ImGui::TextDisabled("Working — this can take a few seconds.");
    ImGui::PopTextWrapPos();
    ImGui::End();
}

void PumpBusyFrame() {
    if (!g_imgui_ready || !g_running || !g_swap) {
        return;
    }
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            g_running = false;
            return;
        }
    }
    g_inside_imgui = true;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawBusyOverlay();
    ImGui::Render();
    g_inside_imgui = false;
    const float clear[4] = {0.05f, 0.05f, 0.06f, 1};
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap->Present(1, 0);
}

void ShowBusy(const char* text) {
    g_busy = true;
    g_busy_text = text;
    PumpBusyFrame();
}

void HideBusy() {
    if (g_starting) {
        g_busy = true;
        return;
    }
    g_busy = false;
}

void FlushQueuedWork();

void RestoreSafety() {
    if (g_restore_fans && g_wmi.Ok()) {
        g_wmi.RestoreAutoFans();
        g_restore_fans = false;
    }
    if (g_dgpu_off_by_app) {
        DiscreteGpu::SetNvidiaEnabled(true);
        g_dgpu_off_by_app = false;
    }
    PowerPlans::SetCpuPolicy(5, 100, 1);
    g_nvapi.SetOffsetsMhz(0, 0);
}

void ApplyFanUiNow() {
    if (!g_wmi.Ok()) {
        Status("WMI not connected — run as Administrator");
        return;
    }
    ShowBusy(g_fan_ui_mode == 0 ? "Setting automatic fans"
                               : (g_fan_ui_mode == 1 ? "Setting maximum fans" : "Setting custom fans"));
    if (g_fan_ui_mode == 0) {
        if (g_wmi.SetFanMode(FanMode::Auto)) {
            g_restore_fans = false;
            Status("Fans set to automatic");
        } else {
            Status("Couldn't change fans — try running as Administrator");
        }
        HideBusy();
        return;
    }
    if (g_fan_ui_mode == 1) {
        if (g_wmi.SetFanMode(FanMode::Max)) {
            g_restore_fans = true;
            Status("Fans at maximum");
        } else {
            Status("Couldn't set max fans — try running as Administrator");
        }
        HideBusy();
        return;
    }
    int cpu = std::max(g_cpu_fan_slider, g_settings.min_fan_percent);
    int gpu = std::max(g_gpu_fan_slider, g_settings.min_fan_percent);
    if (g_settings.couple_fans) {
        if (std::abs(cpu - gpu) > 10) {
            if (cpu > gpu) {
                gpu = cpu - 10;
            } else {
                cpu = gpu - 10;
            }
        }
        g_cpu_fan_slider = cpu;
        g_gpu_fan_slider = gpu;
    }
    const bool mode_ok = g_wmi.SetFanMode(FanMode::Custom);
    const bool cpu_ok = g_wmi.SetFanPercent(GamingWmi::kCpuFanId, cpu);
    const bool gpu_ok = g_wmi.SetFanPercent(GamingWmi::kGpuFanId, gpu);
    if (mode_ok && cpu_ok && gpu_ok) {
        g_restore_fans = true;
        Status("Custom fan speed applied");
    } else {
        Status("Couldn't set custom fans — try running as Administrator");
    }
    HideBusy();
}

void ApplyFanUi() {
    if (g_in_apply) {
        return;
    }
    if (g_inside_imgui) {
        g_busy = true;
        g_busy_text = "Applying fans";
        g_queued_fan = true;
        return;
    }
    g_in_apply = true;
    ApplyFanUiNow();
    g_in_apply = false;
}

void ApplyRgb() {
    RgbSettings rgb;
    rgb.mode = g_settings.rgb_mode;
    rgb.speed = g_settings.rgb_speed;
    rgb.brightness = g_settings.rgb_brightness;
    rgb.direction = g_settings.rgb_dir;
    rgb.r = g_settings.rgb_r;
    rgb.g = g_settings.rgb_g;
    rgb.b = g_settings.rgb_b;
    if (ApplyKeyboardRgb(rgb)) {
        g_settings.Save();
        Status("Lighting applied");
    } else {
        Status("Couldn't change lighting");
    }
}

void ApplyKeyboardPreset(int mode, int r, int g, int b, int brightness, int speed) {
    g_settings.rgb_mode = mode;
    g_settings.rgb_r = r;
    g_settings.rgb_g = g;
    g_settings.rgb_b = b;
    g_settings.rgb_brightness = brightness;
    g_settings.rgb_speed = speed;
    RgbSettings rgb;
    rgb.mode = mode;
    rgb.r = r;
    rgb.g = g;
    rgb.b = b;
    rgb.brightness = brightness;
    rgb.speed = speed;
    rgb.direction = 1;
    ApplyKeyboardRgb(rgb);
}

int RpmToPercent(float rpm) {
    const int pct = static_cast<int>((std::max(rpm, 1200.f) - 1000.f) / 40.f);
    return std::clamp(pct, 25, 100);
}

void ApplyFanMax() {
    g_fan_ui_mode = 1;
    if (g_wmi.SetFanMode(FanMode::Max)) {
        g_restore_fans = true;
    }
}

void ApplyFanAuto() {
    g_fan_ui_mode = 0;
    if (g_wmi.SetFanMode(FanMode::Auto)) {
        g_restore_fans = false;
    }
}

void ApplyFanBoost(int extra_rpm) {
    const float cpu_t = std::max(g_auto_cpu_rpm, 1800.f) + static_cast<float>(extra_rpm);
    const float gpu_t = std::max(g_auto_gpu_rpm, 1800.f) + static_cast<float>(extra_rpm);
    const int cpu = RpmToPercent(cpu_t);
    const int gpu = RpmToPercent(gpu_t);
    g_fan_ui_mode = 2;
    g_cpu_fan_slider = cpu;
    g_gpu_fan_slider = gpu;
    if (g_wmi.SetFanMode(FanMode::Custom) && g_wmi.SetFanPercent(GamingWmi::kCpuFanId, cpu) &&
        g_wmi.SetFanPercent(GamingWmi::kGpuFanId, gpu)) {
        g_restore_fans = true;
    }
}

void RememberBrightness() {
    if (g_saved_brightness < 0) {
        const int b = DisplayMode::GetBrightness();
        g_saved_brightness = b > 0 ? b : 70;
    }
}

void ApplyHighRefresh(bool overdrive, int brightness) {
    auto ds = DisplayMode::Query();
    DisplayMode::SetRefreshHz(ds.max_hz >= 100 ? ds.max_hz : ds.current_hz);
    g_wmi.SetLcdOverdrive(overdrive);
    AcerService::SetLcdOverdrive(overdrive);
    if (brightness >= 0) {
        DisplayMode::SetBrightness(brightness);
    }
}

void ApplyNormalRefresh(bool overdrive, int brightness) {
    auto ds = DisplayMode::Query();
    const int hz = ds.max_hz >= 100 ? ds.max_hz : ds.current_hz;
    DisplayMode::SetRefreshHz(hz);
    g_wmi.SetLcdOverdrive(overdrive);
    AcerService::SetLcdOverdrive(overdrive);
    if (brightness >= 0) {
        DisplayMode::SetBrightness(brightness);
    }
}

void EnsureRtxOn() {
    if (g_dgpu_off_by_app) {
        DiscreteGpu::SetNvidiaEnabled(true);
        g_dgpu_off_by_app = false;
    }
}

void ApplyThermalNow(ThermalMode mode, bool from_user) {
    (void)from_user;
    char msg[80];
    std::snprintf(msg, sizeof(msg), "Applying %s", ModeLabel(mode));
    ShowBusy(msg);

    const bool was_eco = g_thermal == static_cast<int>(ThermalMode::Eco);
    g_thermal = static_cast<int>(mode);

    if (mode != ThermalMode::Eco) {
        ShowBusy("Enabling RTX");
        EnsureRtxOn();
        PumpBusyFrame();
    }

    ShowBusy(msg);
    if (g_wmi.Ok()) {
        g_wmi.SetThermalMode(mode);
    }
    PumpBusyFrame();
    AcerService::SetOperatingMode(static_cast<uint8_t>(mode));
    PowerPlans::ApplyForThermal(mode);
    PumpBusyFrame();

    switch (mode) {
        case ThermalMode::Turbo:
            RememberBrightness();
            g_wmi.ApplyTurboHardware(true);
            PumpBusyFrame();
            ApplyFanMax();
            PumpBusyFrame();
            g_nvapi.SetOffsetsMhz(125, 250);
            PowerPlans::SetCpuPolicy(100, 100, 2);
            PowerPlans::SetUsbPowerSaving(false);
            ShowBusy("Setting display");
            ApplyHighRefresh(true, 100);
            PumpBusyFrame();
            ApplyKeyboardPreset(3, 255, 80, 0, 100, 9);
            break;
        case ThermalMode::Performance:
            RememberBrightness();
            g_wmi.ApplyTurboHardware(false);
            g_wmi.SetMiscSetting(0x05, 0x00);
            PumpBusyFrame();
            ApplyFanBoost(1500);
            PumpBusyFrame();
            g_nvapi.SetOffsetsMhz(50, 100);
            PowerPlans::SetCpuPolicy(5, 100, 1);
            PowerPlans::SetUsbPowerSaving(false);
            ShowBusy("Setting display");
            ApplyHighRefresh(true, 100);
            PumpBusyFrame();
            ApplyKeyboardPreset(0, 255, 255, 255, 100, 3);
            break;
        case ThermalMode::Balanced:
            g_wmi.ApplyTurboHardware(false);
            PumpBusyFrame();
            ApplyFanBoost(700);
            PumpBusyFrame();
            g_nvapi.SetOffsetsMhz(0, 0);
            PowerPlans::SetCpuPolicy(5, 100, 1);
            PowerPlans::SetUsbPowerSaving(false);
            ShowBusy("Setting display");
            ApplyNormalRefresh(false, g_saved_brightness > 0 ? g_saved_brightness : 70);
            PumpBusyFrame();
            ApplyKeyboardPreset(0, 255, 255, 255, 50, 3);
            break;
        case ThermalMode::Silent:
            g_wmi.ApplyTurboHardware(false);
            PumpBusyFrame();
            ApplyFanAuto();
            PumpBusyFrame();
            g_nvapi.SetOffsetsMhz(0, 0);
            PowerPlans::SetCpuPolicy(5, 99, 1);
            PowerPlans::SetUsbPowerSaving(false);
            ShowBusy("Setting display");
            ApplyNormalRefresh(false, g_saved_brightness > 0 ? g_saved_brightness : 70);
            PumpBusyFrame();
            ApplyKeyboardPreset(0, 255, 255, 255, 40, 3);
            break;
        case ThermalMode::Eco:
            RememberBrightness();
            g_wmi.ApplyTurboHardware(false);
            PumpBusyFrame();
            ApplyFanAuto();
            PumpBusyFrame();
            g_nvapi.SetOffsetsMhz(0, 0);
            PowerPlans::SetCpuPolicy(5, 70, 0);
            PowerPlans::SetUsbPowerSaving(true);
            ShowBusy("Setting display");
            {
                auto ds = DisplayMode::Query();
                DisplayMode::SetRefreshHz(ds.min_hz);
            }
            g_wmi.SetLcdOverdrive(false);
            AcerService::SetLcdOverdrive(false);
            DisplayMode::SetBrightness(50);
            PumpBusyFrame();
            ApplyKeyboardPreset(0, 255, 255, 255, 20, 3);
            if (g_settings.disable_dgpu_on_eco) {
                ShowBusy("Switching to Intel graphics");
                if (DiscreteGpu::SetNvidiaEnabled(false)) {
                    g_dgpu_off_by_app = true;
                }
                PumpBusyFrame();
            }
            break;
        default:
            break;
    }

    if (was_eco && mode != ThermalMode::Eco) {
        ShowBusy("Enabling RTX");
        EnsureRtxOn();
        PumpBusyFrame();
    }
    if (g_snap.on_ac) {
        g_settings.last_ac_mode = static_cast<int>(mode);
        g_settings.Save();
    }
    Status(std::string("Performance: ") + ModeLabel(mode));
    HideBusy();
}

void ApplyThermal(ThermalMode mode, bool from_user = true) {
    if (g_in_apply) {
        g_queued_thermal = mode;
        return;
    }
    if (g_inside_imgui) {
        g_busy = true;
        g_busy_text = std::string("Applying ") + ModeLabel(mode);
        g_queued_thermal = mode;
        return;
    }
    g_in_apply = true;
    ApplyThermalNow(mode, from_user);
    g_in_apply = false;
}

void FlushQueuedWork() {
    if (g_queued_thermal) {
        const ThermalMode m = *g_queued_thermal;
        g_queued_thermal.reset();
        ApplyThermal(m);
    }
    if (g_queued_fan) {
        g_queued_fan = false;
        ApplyFanUi();
    }
}

void ApplyAutoForPower(bool on_ac) {
    if (!g_settings.eco_on_battery) {
        return;
    }
    ApplyThermal(on_ac ? ThermalMode::Performance : ThermalMode::Eco, false);
}

void HandlePowerSource(bool on_ac) {
    if (!g_power_known) {
        g_power_known = true;
        g_last_ac = on_ac;
        ApplyAutoForPower(on_ac);
        return;
    }
    if (on_ac == g_last_ac) {
        return;
    }
    g_last_ac = on_ac;
    ApplyAutoForPower(on_ac);
    Status(on_ac ? "Plugged in - ESports" : "On battery - Battery saver");
}

void StylePredator() {
    ImGui::StyleColorsDark();
    auto& st = ImGui::GetStyle();
    st.WindowRounding = 0;
    st.ChildRounding = 10;
    st.FrameRounding = 8;
    st.GrabRounding = 8;
    st.TabRounding = 8;
    st.ScrollbarRounding = 8;
    st.WindowPadding = ImVec2(18, 16);
    st.FramePadding = ImVec2(12, 8);
    st.ItemSpacing = ImVec2(12, 10);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.ScrollbarSize = 14;
    ImGui::GetIO().FontGlobalScale = 1.12f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.04f, 0.045f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.09f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.08f, 1);
    c[ImGuiCol_Border] = ImVec4(0.22f, 0.12f, 0.04f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.13f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.12f, 0.04f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.16f, 0.04f, 1);
    c[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.06f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.06f, 0.02f, 1);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.09f, 0.02f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.85f, 0.38f, 0.04f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.45f, 0.05f, 1);
    c[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.45f, 0.05f, 1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.62f, 0.18f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.45f, 0.05f, 1);
    c[ImGuiCol_Header] = ImVec4(0.28f, 0.12f, 0.02f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.85f, 0.38f, 0.04f, 1);
    c[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.45f, 0.05f, 1);
    c[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.11f, 1);
    c[ImGuiCol_TabHovered] = ImVec4(0.85f, 0.38f, 0.04f, 1);
    c[ImGuiCol_TabSelected] = ImVec4(0.85f, 0.38f, 0.04f, 1);
    c[ImGuiCol_Text] = ImVec4(0.96f, 0.94f, 0.90f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.52f, 0.48f, 1);
    c[ImGuiCol_Separator] = ImVec4(0.28f, 0.14f, 0.04f, 1);
}

ImVec4 TempAccent(float c) {
    if (c < 0) {
        return ImVec4(0.6f, 0.6f, 0.6f, 1);
    }
    if (c < 60) {
        return ImVec4(0.35f, 0.85f, 0.45f, 1);
    }
    if (c < 80) {
        return ImVec4(1.0f, 0.55f, 0.12f, 1);
    }
    return ImVec4(1.0f, 0.28f, 0.18f, 1);
}

bool BigMode(const char* label, bool selected, ImVec2 size) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.45f, 0.05f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.55f, 0.12f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1));
    }
    const bool hit = ImGui::Button(label, size);
    if (selected) {
        ImGui::PopStyleColor(3);
    }
    return hit;
}

void StatCard(const char* id, const char* title, const char* value, ImVec4 accent, const char* sub,
              float width) {
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent.x, accent.y, accent.z, 0.45f));
    ImGui::BeginChild(id, ImVec2(width, 102), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("%s", title);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::SetWindowFontScale(1.45f);
    ImGui::TextUnformatted(value);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    if (sub && sub[0]) {
        ImGui::TextDisabled("%s", sub);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DrawUi() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Predator Utility", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextColored(ImVec4(1.f, 0.45f, 0.05f, 1), "PREDATOR");
    ImGui::SameLine();
    ImGui::TextUnformatted("Control");
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", g_snap.model.empty() ? "Acer Predator" : g_snap.model.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 280);
    ImGui::TextDisabled("%s", g_snap.on_ac ? "Plugged in" : "On battery");
    ImGui::Separator();

    if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Home")) {
            const float gap = 12.f;
            const float card_w = (ImGui::GetContentRegionAvail().x - gap * 3) / 4.f;
            char cpu_t[32], gpu_t[32], pwr[32], bat[32], cpu_sub[64], gpu_sub[80], pwr_sub[64],
                bat_sub[64];
            if (g_snap.cpu_temp_c >= 0) {
                snprintf(cpu_t, sizeof(cpu_t), "%.0f C", g_snap.cpu_temp_c);
            } else {
                snprintf(cpu_t, sizeof(cpu_t), "--");
            }
            snprintf(cpu_sub, sizeof(cpu_sub), "CPU  %.0f%% load", g_snap.cpu_util);
            if (g_settings.monitor_gpu && g_snap.gpu.temp_c > 0) {
                snprintf(gpu_t, sizeof(gpu_t), "%.0f C", g_snap.gpu.temp_c);
                snprintf(gpu_sub, sizeof(gpu_sub), "GPU  %.0f%%", g_snap.gpu.util_percent);
            } else {
                snprintf(gpu_t, sizeof(gpu_t), "Off");
                snprintf(gpu_sub, sizeof(gpu_sub), "Enable in Settings");
            }
            if (!g_snap.on_ac && (g_snap.battery_watts < -1.f || g_snap.battery_watts > 1.f)) {
                snprintf(pwr, sizeof(pwr), "%.0f W", std::abs(g_snap.battery_watts));
                if (g_snap.cpu_power_w >= 0) {
                    snprintf(pwr_sub, sizeof(pwr_sub), "From battery  CPU %.0f W", g_snap.cpu_power_w);
                } else {
                    snprintf(pwr_sub, sizeof(pwr_sub), "From battery");
                }
            } else if (g_snap.cpu_power_w >= 0) {
                snprintf(pwr, sizeof(pwr), "%.0f W", g_snap.cpu_power_w);
                snprintf(pwr_sub, sizeof(pwr_sub),
                         g_snap.cpu_voltage_v >= 0 ? "CPU package  %.2f V" : "CPU package",
                         g_snap.cpu_voltage_v);
            } else {
                snprintf(pwr, sizeof(pwr), "--");
                snprintf(pwr_sub, sizeof(pwr_sub), "CPU package");
            }
            if (g_snap.battery_percent >= 0) {
                snprintf(bat, sizeof(bat), "%.0f%%", g_snap.battery_percent);
            } else {
                snprintf(bat, sizeof(bat), "--");
            }
            snprintf(bat_sub, sizeof(bat_sub), "%+.0f W  %s", g_snap.battery_watts,
                     g_snap.on_ac ? "charging" : "draining");

            StatCard("c_cpu", "CPU TEMP", cpu_t, TempAccent(g_snap.cpu_temp_c), cpu_sub, card_w);
            ImGui::SameLine(0, gap);
            StatCard("c_gpu", "GPU TEMP", gpu_t,
                     g_settings.monitor_gpu ? TempAccent(g_snap.gpu.temp_c)
                                            : ImVec4(0.5f, 0.5f, 0.5f, 1),
                     gpu_sub, card_w);
            ImGui::SameLine(0, gap);
            StatCard("c_pwr", "POWER", pwr, ImVec4(1.f, 0.45f, 0.05f, 1), pwr_sub, card_w);
            ImGui::SameLine(0, gap);
            StatCard("c_bat", "BATTERY", bat, ImVec4(0.95f, 0.85f, 0.4f, 1), bat_sub, card_w);

            ImGui::Spacing();
            ImGui::TextUnformatted("Performance");
            const ImVec2 ms(148, 48);
            if (BigMode("Quiet##m", g_thermal == 0, ms)) {
                ApplyThermal(ThermalMode::Silent);
            }
            ImGui::SameLine();
            if (BigMode("Normal##m", g_thermal == 1, ms)) {
                ApplyThermal(ThermalMode::Balanced);
            }
            ImGui::SameLine();
            if (BigMode("ESports##m", g_thermal == 4, ms)) {
                ApplyThermal(ThermalMode::Performance);
            }
            ImGui::SameLine();
            if (BigMode("Turbo##m", g_thermal == 5, ms)) {
                ApplyThermal(ThermalMode::Turbo);
            }
            ImGui::SameLine();
            if (BigMode("Battery saver##m", g_thermal == 6, ms)) {
                ApplyThermal(ThermalMode::Eco);
            }
            ImGui::TextDisabled(
                "Each mode sets screen, lights, clocks, and fans. ESports skips CPU OC. Battery saver turns the RTX off.");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Fans")) {
            ImGui::TextUnformatted("Cooling");
            ImGui::TextDisabled("Automatic is best for everyday use. Maximum is for heavy games.");
            const ImVec2 fs(180, 46);
            if (BigMode("Automatic##f", g_fan_ui_mode == 0, fs)) {
                g_fan_ui_mode = 0;
                ApplyFanUi();
            }
            ImGui::SameLine();
            if (BigMode("Maximum##f", g_fan_ui_mode == 1, fs)) {
                g_fan_ui_mode = 1;
                ApplyFanUi();
            }
            ImGui::SameLine();
            if (BigMode("Custom##f", g_fan_ui_mode == 2, fs)) {
                g_fan_ui_mode = 2;
            }
            ImGui::Spacing();
            ImGui::Text("CPU fan  %.0f RPM", g_snap.cpu_fan_rpm > 0 ? g_snap.cpu_fan_rpm : 0);
            ImGui::Text("GPU fan  %.0f RPM", g_snap.gpu_fan_rpm > 0 ? g_snap.gpu_fan_rpm : 0);
            ImGui::BeginDisabled(g_fan_ui_mode != 2);
            ImGui::Checkbox("Keep both fans similar", &g_settings.couple_fans);
            ImGui::SliderInt("CPU fan speed", &g_cpu_fan_slider, g_settings.min_fan_percent, 100, "%d%%");
            ImGui::SliderInt("GPU fan speed", &g_gpu_fan_slider, g_settings.min_fan_percent, 100, "%d%%");
            if (ImGui::Button("Apply custom speed", ImVec2(220, 40))) {
                ApplyFanUi();
                g_settings.Save();
            }
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lighting")) {
            ImGui::TextUnformatted("Keyboard");
            const char* effects[] = {"Solid color", "Breathing", "Neon", "Wave", "Ripple", "Zoom"};
            ImGui::SetNextItemWidth(260);
            ImGui::Combo("Effect", &g_settings.rgb_mode, effects, IM_ARRAYSIZE(effects));
            float col[3] = {g_settings.rgb_r / 255.f, g_settings.rgb_g / 255.f, g_settings.rgb_b / 255.f};
            if (ImGui::ColorEdit3("Color", col, ImGuiColorEditFlags_PickerHueWheel)) {
                g_settings.rgb_r = static_cast<int>(col[0] * 255);
                g_settings.rgb_g = static_cast<int>(col[1] * 255);
                g_settings.rgb_b = static_cast<int>(col[2] * 255);
            }
            ImGui::SliderInt("Brightness", &g_settings.rgb_brightness, 0, 100, "%d%%");
            ImGui::BeginDisabled(g_settings.rgb_mode == 0);
            ImGui::SliderInt("Animation speed", &g_settings.rgb_speed, 1, 9);
            const char* dirs[] = {"Left to right", "Right to left"};
            int dir_idx = g_settings.rgb_dir == 2 ? 1 : 0;
            if (ImGui::Combo("Wave direction", &dir_idx, dirs, IM_ARRAYSIZE(dirs))) {
                g_settings.rgb_dir = dir_idx == 1 ? 2 : 1;
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Apply lighting", ImVec2(200, 44))) {
                ApplyRgb();
            }
            ImGui::SameLine();
            if (ImGui::Button("Turn lights off", ImVec2(180, 44))) {
                g_settings.rgb_brightness = 0;
                ApplyRgb();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Display")) {
            auto ds = DisplayMode::Query();
            ImGui::TextUnformatted("Laptop screen");
            ImGui::TextDisabled("Current refresh  %d Hz", ds.current_hz);
            ImGui::Spacing();
            const ImVec2 dsiz(160, 46);
            if (BigMode("60 Hz##d", ds.current_hz <= 75, dsiz)) {
                if (DisplayMode::SetRefreshHz(ds.min_hz)) {
                    g_wmi.SetLcdOverdrive(false);
                    AcerService::SetLcdOverdrive(false);
                    Status("Screen set to 60 Hz");
                } else {
                    Status("Couldn't change refresh rate");
                }
            }
            if (ds.max_hz >= 100) {
                ImGui::SameLine();
                char high_l[32];
                snprintf(high_l, sizeof(high_l), "%d Hz##d", ds.max_hz);
                if (BigMode(high_l, ds.current_hz >= 100, dsiz)) {
                    if (DisplayMode::SetRefreshHz(ds.max_hz)) {
                        g_wmi.SetLcdOverdrive(true);
                        AcerService::SetLcdOverdrive(true);
                        Status("High refresh + overdrive on");
                    } else {
                        Status("Couldn't change refresh rate");
                    }
                }
            }
            ImGui::Spacing();
            static bool od = ds.current_hz >= 100;
            if (ImGui::Checkbox("Overdrive", &od)) {
                const bool ok = g_wmi.SetLcdOverdrive(od) || AcerService::SetLcdOverdrive(od);
                Status(ok ? (od ? "Overdrive on" : "Overdrive off") : "Couldn't change overdrive");
            }
            ImGui::TextWrapped(
                "Overdrive reduces motion blur at high refresh. Leave it off at 60 Hz.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Battery")) {
            auto bh = BatteryHealth::Query();
            ImGui::TextUnformatted("Battery care");
            ImGui::TextDisabled("Stopping charge at 80%% is easier on the pack if you live on the charger.");
            if (bh.available) {
                g_charge_limit = bh.health_mode;
                if (ImGui::Checkbox("Limit charge to 80%", &g_charge_limit)) {
                    if (BatteryHealth::SetHealthMode(g_charge_limit)) {
                        Status(g_charge_limit ? "Charge stops at 80%" : "Charge to full");
                    } else {
                        Status("Couldn't change charge limit");
                        g_charge_limit = !g_charge_limit;
                    }
                }
            } else {
                ImGui::TextDisabled("Charge limit isn't available on this BIOS.");
            }
            ImGui::Spacing();
            ImGui::Text("Level  %.0f%%", g_snap.battery_percent);
            ImGui::Text("Power  %+.0f W", g_snap.battery_watts);
            if (g_snap.battery_voltage_v >= 0) {
                ImGui::Text("Voltage  %.2f V", g_snap.battery_voltage_v);
            }
            ImGui::Checkbox("Auto ESports on charger, Battery saver on battery",
                            &g_settings.eco_on_battery);
            ImGui::TextDisabled(
                "A manual mode stays until you plug in or unplug. Then auto starts again.");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                g_settings.Save();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings")) {
            ImGui::TextUnformatted("App");
            if (ImGui::Checkbox("Show on-screen overlay", &g_settings.osd)) {
                OverlaySetVisible(g_settings.osd);
                g_settings.Save();
            }
            if (ImGui::Checkbox("Start with Windows", &g_settings.start_with_windows)) {
                SetRunAtStartup(g_settings.start_with_windows, g_settings.start_minimized);
                g_settings.Save();
            }
            if (ImGui::Checkbox("Start minimized in the tray", &g_settings.start_minimized)) {
                if (g_settings.start_with_windows) {
                    SetRunAtStartup(true, g_settings.start_minimized);
                }
                g_settings.Save();
            }
            ImGui::TextDisabled("Startup uses a logon task so it can run as admin without a UAC prompt.");
            if (ImGui::Checkbox("Monitor graphics card (can raise idle heat)", &g_settings.monitor_gpu)) {
                g_settings.Save();
            }
            if (ImGui::Checkbox("Turn off RTX in Battery saver", &g_settings.disable_dgpu_on_eco)) {
                g_settings.Save();
                if (g_thermal == 6 && g_settings.disable_dgpu_on_eco && !g_dgpu_off_by_app) {
                    if (DiscreteGpu::SetNvidiaEnabled(false)) {
                        g_dgpu_off_by_app = true;
                        Status("RTX off");
                    }
                } else if (!g_settings.disable_dgpu_on_eco && g_dgpu_off_by_app) {
                    DiscreteGpu::SetNvidiaEnabled(true);
                    g_dgpu_off_by_app = false;
                    Status("RTX on");
                }
            }
            ImGui::TextWrapped(
                "Close this window to keep the app in the tray. Click the tray icon to open it again or Exit.");
            ImGui::Spacing();
            ImGui::TextUnformatted("Windows power plan");
            auto schemes = PowerPlans::List();
            for (const auto& sc : schemes) {
                ImGui::PushID(sc.guid.c_str());
                const char* label = sc.name.empty() ? sc.guid.c_str() : sc.name.c_str();
                if (ImGui::RadioButton(label, sc.active)) {
                    PowerPlans::ActivateGuid(sc.guid);
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Advanced")) {
                ImGui::TextDisabled("For troubleshooting only.");
                ImGui::SliderInt("Minimum custom fan %", &g_settings.min_fan_percent, 10, 50);
                ImGui::TextUnformatted("GPU clock offset");
                ImGui::SliderInt("Core MHz", &g_settings.gpu_core_offset_mhz, -200, 200);
                ImGui::SliderInt("Memory MHz", &g_settings.gpu_mem_offset_mhz, -500, 500);
                if (ImGui::Button("Apply GPU offsets")) {
                    if (g_nvapi.SetOffsetsMhz(g_settings.gpu_core_offset_mhz,
                                              g_settings.gpu_mem_offset_mhz)) {
                        g_settings.Save();
                        Status("GPU offsets applied");
                    } else {
                        Status("This laptop blocked GPU offsets");
                    }
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", g_status.empty() ? "Ready" : g_status.c_str());
    ImGui::End();
}

void RequestExit() {
    RestoreSafety();
    g_running = false;
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
    }
}

void ShowMainWindow() {
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

void ToggleAppVisibility() {
    if (IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd)) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        ShowMainWindow();
    }
}

void ToggleTurboKey() {
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (now - last < 500) {
        return;
    }
    last = now;
    if (g_thermal == static_cast<int>(ThermalMode::Turbo)) {
        ApplyThermal(ThermalMode::Balanced);
    } else {
        ApplyThermal(ThermalMode::Turbo);
    }
}

LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) {
        return true;
    }
    switch (msg) {
        case kMsgPredatorKey:
            ToggleAppVisibility();
            return 0;
        case kMsgTurboKey:
            ToggleTurboKey();
            return 0;
        case kMsgPredatorMode: {
            ThermalMode m = ThermalMode::Balanced;
            switch (static_cast<int>(w)) {
                case 1:
                    m = ThermalMode::Silent;
                    break;
                case 2:
                    m = ThermalMode::Balanced;
                    break;
                case 3:
                    m = ThermalMode::Performance;
                    break;
                case 4:
                    m = ThermalMode::Turbo;
                    break;
                case 5:
                    m = ThermalMode::Eco;
                    break;
                default:
                    break;
            }
            ApplyThermal(m);
            return 0;
        }
        case WM_INPUT:
            HotkeysHandleRawInput(l);
            return 0;
        case kTrayCallback:
            if (TrayIsClick(l)) {
                TrayPopupMenu(h, g_settings.osd, g_thermal);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(w)) {
                case IDM_TRAY_OPEN:
                    ShowMainWindow();
                    break;
                case IDM_TRAY_OVERLAY:
                    g_settings.osd = !g_settings.osd;
                    OverlaySetVisible(g_settings.osd);
                    g_settings.Save();
                    Status(g_settings.osd ? "Overlay on" : "Overlay off");
                    break;
                case IDM_TRAY_FANS_AUTO:
                    g_fan_ui_mode = 0;
                    ApplyFanUi();
                    break;
                case IDM_TRAY_SILENT:
                    ApplyThermal(ThermalMode::Silent);
                    break;
                case IDM_TRAY_BALANCED:
                    ApplyThermal(ThermalMode::Balanced);
                    break;
                case IDM_TRAY_ESPORTS:
                    ApplyThermal(ThermalMode::Performance);
                    break;
                case IDM_TRAY_TURBO:
                    ApplyThermal(ThermalMode::Turbo);
                    break;
                case IDM_TRAY_ECO:
                    ApplyThermal(ThermalMode::Eco);
                    break;
                case IDM_TRAY_EXIT:
                    RequestExit();
                    break;
                default:
                    break;
            }
            return 0;
        case WM_SIZE:
            if (g_device && w != SIZE_MINIMIZED) {
                if (g_rtv) {
                    g_rtv->Release();
                    g_rtv = nullptr;
                }
                g_swap->ResizeBuffers(0, (UINT)LOWORD(l), (UINT)HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
                CreateRtv();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((w & 0xfff0) == SC_MINIMIZE) {
                ShowWindow(h, SW_HIDE);
                return 0;
            }
            break;
        case WM_CLOSE:
            ShowWindow(h, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_POWERBROADCAST:
            if (w == PBT_APMPOWERSTATUSCHANGE) {
                SYSTEM_POWER_STATUS ps{};
                if (GetSystemPowerStatus(&ps)) {
                    const bool ac = ps.ACLineStatus == 1;
                    if (g_settings.eco_on_battery) {
                        HandlePowerSource(ac);
                    } else {
                        g_last_ac = ac;
                        g_power_known = true;
                    }
                }
                return TRUE;
            }
            break;
        case WM_QUERYENDSESSION:
            RestoreSafety();
            return TRUE;
        case WM_ENDSESSION:
            RestoreSafety();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(h, msg, w, l);
}


}  // namespace

int RunApp(bool start_minimized) {
    LogInit();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PredatorUtilityWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowW(wc.lpszClassName, L"Predator Utility", WS_OVERLAPPEDWINDOW, 80, 80, 1080,
                           740, nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDevice(g_hwnd)) {
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    StylePredator();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_ctx);
    g_imgui_ready = true;
    g_starting = true;
    if (!start_minimized) {
        ShowWindow(g_hwnd, SW_SHOWDEFAULT);
        UpdateWindow(g_hwnd);
        ShowBusy("Starting Predator Utility");
    }

    ShowBusy("Stopping PredatorSense");
    KillPredatorSense(PumpBusyFrame);

    ShowBusy("Loading settings");
    g_settings = Settings::Load();
    SetRunAtStartup(g_settings.start_with_windows, g_settings.start_minimized);
    PumpBusyFrame();

    ShowBusy("Connecting to firmware");
    g_wmi.Connect();
    if (auto m = g_wmi.GetThermalMode()) {
        g_thermal = static_cast<int>(*m);
    }
    PumpBusyFrame();

    ShowBusy("Starting tray and hotkeys");
    TrayCreate(g_hwnd);
    HotkeysStart(g_hwnd);
    OverlayCreate(g_hwnd);
    OverlaySetVisible(false);
    if (g_settings.osd) {
        OverlaySetVisible(true);
    }
    PumpBusyFrame();

    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps)) {
        if (g_settings.eco_on_battery) {
            HandlePowerSource(ps.ACLineStatus == 1);
        } else {
            g_last_ac = ps.ACLineStatus == 1;
            g_power_known = true;
        }
    }

    g_starting = false;
    HideBusy();
    if (start_minimized) {
        ShowWindow(g_hwnd, SW_HIDE);
    }

    MSG msg{};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                g_running = false;
            }
        }
        if (!g_running) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool visible = IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd);
        const int interval_ms = visible ? 2000 : 5000;
        if (!g_busy && now - g_last_poll > std::chrono::milliseconds(interval_ms)) {
            g_last_poll = now;
            const bool gpu = visible && g_settings.monitor_gpu;
            g_snap = g_sensors.Poll(gpu);
            if (g_fan_ui_mode == 0) {
                if (g_snap.cpu_fan_rpm > 800) {
                    g_auto_cpu_rpm = g_snap.cpu_fan_rpm;
                }
                if (g_snap.gpu_fan_rpm > 800) {
                    g_auto_gpu_rpm = g_snap.gpu_fan_rpm;
                }
            }
            if (g_settings.osd) {
                OverlayUpdate(g_snap.cpu_temp_c, g_snap.gpu.temp_c, g_snap.cpu_power_w,
                              g_snap.gpu.power_w, g_snap.battery_watts, g_snap.cpu_fan_rpm,
                              g_snap.gpu_fan_rpm, g_snap.on_ac);
            } else {
                OverlaySetVisible(false);
            }
            if (g_settings.eco_on_battery) {
                HandlePowerSource(g_snap.on_ac);
            } else {
                g_last_ac = g_snap.on_ac;
                g_power_known = true;
            }
        }

        g_inside_imgui = true;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawUi();
        DrawBusyOverlay();
        ImGui::Render();
        g_inside_imgui = false;
        const float clear[4] = {0.05f, 0.05f, 0.06f, 1};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
        FlushQueuedWork();
    }

    RestoreSafety();
    HotkeysStop();
    OverlayDestroy();
    TrayDestroy();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

}  // namespace predator::ui
