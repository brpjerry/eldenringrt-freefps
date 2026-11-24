// SPDX-License-Identifier: BSD-3-Clause
// Exercise real DLL loading and add-on discovery using the software D3D12 adapter.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(static_cast<unsigned>(result)));
}
fs::path module_path(HMODULE module) {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    require(length && length < path.size(), "Cannot identify module path");
    path.resize(length);
    return path;
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2, "Pass the absolute runtime DLL path");
        const fs::path runtime_path = fs::absolute(argv[1]);
        require(!fs::equivalent(runtime_path.parent_path(), module_path(nullptr).parent_path()),
                "Test host must live outside the package folder");
        require(!fs::exists(module_path(nullptr).parent_path() / "ReShade.ini"),
                "Host folder must not contain a ReShade config");
        SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE", nullptr);
        SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", nullptr);
        SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK", nullptr);
        const HMODULE runtime = LoadLibraryW(runtime_path.c_str());
        require(runtime != nullptr, "Runtime DLL failed to load");
        for (const char* name : {"ERBatchTag", "ERBatchControl", "ERBatchStatus", "ERBatchTick", "ERBatchTrace"})
            require(GetProcAddress(runtime, name) != nullptr, "Missing native batching export");
        const auto base_path = reinterpret_cast<void(*)(char*, size_t*)>(GetProcAddress(runtime, "ReShadeGetBasePath"));
        require(base_path != nullptr, "Missing ReShade base-path API");
        size_t length = 0;
        base_path(nullptr, &length);
        require(length > 1 && length < 32768, "Invalid runtime base path");
        std::vector<char> base(length);
        base_path(base.data(), &length);
        require(fs::equivalent(fs::u8path(base.data()), runtime_path.parent_path()),
                "Runtime searched the host directory instead of its package directory");
        const auto config_value = reinterpret_cast<bool(*)(void*, void*, const char*, const char*, char*, size_t*)>(
            GetProcAddress(runtime, "ReShadeGetConfigValue"));
        require(config_value != nullptr, "Missing ReShade configuration API");
        char value[32] = {}; size_t value_size = sizeof(value);
        require(config_value(nullptr, nullptr, "GENERAL", "PerformanceMode", value, &value_size) && std::string(value) == "1",
                "ReShade.ini was not read from the package");

        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
        ComPtr<IDXGIAdapter> adapter;
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
        ComPtr<ID3D12Device> device;
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice(WARP)");
        const HMODULE addon = GetModuleHandleW(L"RT-Pass-Probe.addon64");
        require(addon && fs::equivalent(module_path(addon), runtime_path.parent_path() / "RT-Pass-Probe.addon64"),
                "Companion add-on was not loaded from the package");
        const auto control = reinterpret_cast<unsigned(*)(unsigned)>(GetProcAddress(runtime, "ERBatchControl"));
        require(control(99) == 2, "Add-on failed to connect the batching bridge or read BatchMode=2");
        std::ifstream log(runtime_path.parent_path() / "probe.log");
        const std::string messages((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
        require(messages.find("native bridge=1") != std::string::npos, "Add-on did not report a working bridge");
        require(messages.find("D3D12 device registered") != std::string::npos, "Add-on did not receive the device callback");
        device.Reset(); adapter.Reset(); factory.Reset();
        require(FreeLibrary(runtime) != FALSE, "Runtime failed to unload");
        std::cout << "PASS: portable DLL base path, settings, all bridge exports, automatic add-on load and WARP device callback.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
