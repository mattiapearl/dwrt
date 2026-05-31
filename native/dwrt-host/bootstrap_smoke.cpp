#include "dwrt_host_api.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

struct Options {
    std::filesystem::path host_path;
    std::filesystem::path runtime_path;
    std::filesystem::path server_path;
    std::filesystem::path output_path;
};

using HostAbiVersionFn = std::uint32_t (*)();
using HostInitializeFn = std::uint32_t (*)(const DwrtHostConfig*);
using HostSnapshotFn = std::uint32_t (*)(DwrtHostSnapshot*);
using HostShutdownFn = std::uint32_t (*)();

std::string narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string output(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        needed,
        nullptr,
        nullptr);
    return output;
}

std::string windows_error_message(const char* prefix) {
    std::ostringstream out;
    out << prefix << " (GetLastError=" << GetLastError() << ")";
    return out.str();
}

int fail(int code, const std::string& message) {
    std::cerr << "[dwrt-host-bootstrap-smoke] ERROR: " << message << '\n';
    return code;
}

void print_usage() {
    std::cerr << "usage: dwrt_host_bootstrap_smoke.exe --host <dwrt_host.dll> "
              << "--runtime <dwrt_runtime.dll> --server <server.dll> --output <summary.json>\n";
}

std::optional<Options> parse_options(int argc, wchar_t** argv, std::string& error) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring arg = argv[index];
        auto require_value = [&](const wchar_t* name) -> std::optional<std::wstring> {
            if (index + 1 >= argc) {
                error = "missing value for " + narrow(name);
                return std::nullopt;
            }
            ++index;
            return std::wstring(argv[index]);
        };

        if (arg == L"--host") {
            const auto value = require_value(L"--host");
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.host_path = *value;
        }
        else if (arg == L"--runtime") {
            const auto value = require_value(L"--runtime");
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.runtime_path = *value;
        }
        else if (arg == L"--server") {
            const auto value = require_value(L"--server");
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.server_path = *value;
        }
        else if (arg == L"--output") {
            const auto value = require_value(L"--output");
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.output_path = *value;
        }
        else if (arg == L"--help" || arg == L"-h") {
            print_usage();
            return std::nullopt;
        }
        else {
            error = "unknown argument: " + narrow(arg);
            return std::nullopt;
        }
    }

    if (options.host_path.empty() || options.runtime_path.empty() ||
        options.server_path.empty() || options.output_path.empty()) {
        error = "--host, --runtime, --server, and --output are required";
        return std::nullopt;
    }
    return options;
}

FARPROC required_proc(HMODULE module, const char* name, std::string& error) {
    FARPROC proc = GetProcAddress(module, name);
    if (proc == nullptr) {
        error = std::string("missing host export: ") + name;
    }
    return proc;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::string parse_error;
    const std::optional<Options> options = parse_options(argc, argv, parse_error);
    if (!options.has_value()) {
        if (!parse_error.empty()) {
            print_usage();
            return fail(64, parse_error);
        }
        return 0;
    }

    HMODULE host = LoadLibraryW(options->host_path.c_str());
    if (host == nullptr) {
        return fail(1, windows_error_message("failed to load dwrt_host.dll"));
    }

    std::string bind_error;
    const auto abi_version = reinterpret_cast<HostAbiVersionFn>(required_proc(host, "dwrt_host_abi_version", bind_error));
    const auto initialize = reinterpret_cast<HostInitializeFn>(required_proc(host, "dwrt_host_initialize", bind_error));
    const auto snapshot = reinterpret_cast<HostSnapshotFn>(required_proc(host, "dwrt_host_snapshot", bind_error));
    const auto shutdown = reinterpret_cast<HostShutdownFn>(required_proc(host, "dwrt_host_shutdown", bind_error));
    if (abi_version == nullptr || initialize == nullptr || snapshot == nullptr || shutdown == nullptr) {
        FreeLibrary(host);
        return fail(2, bind_error);
    }

    if (abi_version() != DWRT_HOST_ABI_VERSION) {
        FreeLibrary(host);
        return fail(3, "unexpected dwrt_host ABI version");
    }

    DwrtHostConfig config{};
    config.abi_version = DWRT_HOST_ABI_VERSION;
    config.flags = DWRT_HOST_FLAG_REQUIRE_RUNTIME |
        DWRT_HOST_FLAG_REQUIRE_SIGNATURES |
        DWRT_HOST_FLAG_ALLOW_MAPPED_FILE_FALLBACK;
    config.runtime_path = options->runtime_path.c_str();
    config.server_module_name = L"server.dll";
    config.server_path = options->server_path.c_str();
    config.summary_path = options->output_path.c_str();

    const std::uint32_t init_status = initialize(&config);
    if (init_status != DWRT_HOST_OK) {
        shutdown();
        FreeLibrary(host);
        std::ostringstream out;
        out << "dwrt_host_initialize failed with status " << init_status;
        return fail(4, out.str());
    }

    DwrtHostSnapshot state{};
    const std::uint32_t snapshot_status = snapshot(&state);
    if (snapshot_status != DWRT_HOST_OK) {
        shutdown();
        FreeLibrary(host);
        return fail(5, "dwrt_host_snapshot failed");
    }

    if (state.initialized == 0 || state.runtime_loaded == 0 || state.runtime_probe_ok == 0 ||
        state.signatures_checked == 0 || state.signature_required_failures != 0) {
        shutdown();
        FreeLibrary(host);
        return fail(6, "dwrt_host snapshot did not report a healthy initialized host");
    }

    const std::uint32_t second_init_status = initialize(&config);
    if (second_init_status != DWRT_HOST_ERROR_ALREADY_INITIALIZED) {
        shutdown();
        FreeLibrary(host);
        return fail(7, "dwrt_host did not reject a second initialize call");
    }
    const std::uint32_t second_snapshot_status = snapshot(&state);
    if (second_snapshot_status != DWRT_HOST_OK || state.initialize_reentrant_rejects != 1) {
        shutdown();
        FreeLibrary(host);
        return fail(8, "dwrt_host did not record a reentrant initialize rejection");
    }

    shutdown();
    FreeLibrary(host);
    std::cout << "[dwrt-host-bootstrap-smoke] OK\n";
    return 0;
}
