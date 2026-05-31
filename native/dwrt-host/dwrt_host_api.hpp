#pragma once

#include <cstdint>

#include "dwrt_runtime.h"

#ifdef _WIN32
#ifdef DWRT_HOST_BUILD
#define DWRT_HOST_API extern "C" __declspec(dllexport)
#else
#define DWRT_HOST_API extern "C"
#endif
#else
#define DWRT_HOST_API extern "C"
#endif

enum : std::uint32_t {
    DWRT_HOST_ABI_VERSION = 1,

    DWRT_HOST_OK = 0,
    DWRT_HOST_ERROR_BAD_ARGUMENT = 1,
    DWRT_HOST_ERROR_ALREADY_INITIALIZED = 2,
    DWRT_HOST_ERROR_RUNTIME_LOAD_FAILED = 3,
    DWRT_HOST_ERROR_RUNTIME_ABI_MISMATCH = 4,
    DWRT_HOST_ERROR_RUNTIME_CREATE_FAILED = 5,
    DWRT_HOST_ERROR_RUNTIME_PROBE_FAILED = 6,
    DWRT_HOST_ERROR_SERVER_MODULE_NOT_FOUND = 7,
    DWRT_HOST_ERROR_SIGNATURE_VALIDATION_FAILED = 8,
    DWRT_HOST_ERROR_SUMMARY_WRITE_FAILED = 9,
    DWRT_HOST_ERROR_HOOK_INSTALL_FAILED = 10,
    DWRT_HOST_ERROR_NOT_INITIALIZED = 11,

    DWRT_HOST_FLAG_REQUIRE_RUNTIME = 1u << 0,
    DWRT_HOST_FLAG_REQUIRE_SIGNATURES = 1u << 1,
    DWRT_HOST_FLAG_ALLOW_MAPPED_FILE_FALLBACK = 1u << 2,
    DWRT_HOST_FLAG_ALLOW_EXPECTED_RVA_DRIFT = 1u << 3,
    DWRT_HOST_FLAG_INSTALL_PROBE_HOOKS = 1u << 4,
};

struct DwrtHostConfig {
    std::uint32_t abi_version;
    std::uint32_t flags;
    const wchar_t* runtime_path;
    const wchar_t* server_module_name;
    const wchar_t* server_path;
    const wchar_t* summary_path;
};

struct DwrtHostSnapshot {
    std::uint32_t abi_version;
    std::uint32_t initialized;
    std::uint32_t runtime_loaded;
    std::uint32_t runtime_probe_ok;
    std::uint32_t signatures_checked;
    std::uint32_t signature_required_failures;
    std::uint32_t used_live_server_module;
    std::uint32_t used_mapped_file_fallback;
    std::uint32_t hook_install_attempts;
    std::uint32_t hooks_installed;
    std::uint32_t hook_install_failures;
    std::uint64_t initialize_calls;
    std::uint64_t initialize_reentrant_rejects;
    std::uint64_t shutdown_calls;
    std::uint64_t callback_entries;
    std::uint64_t callback_recursive_entries;
    std::uint32_t callback_current_depth;
    std::uint32_t callback_max_depth;
};

DWRT_HOST_API std::uint32_t dwrt_host_abi_version();
DWRT_HOST_API std::uint32_t dwrt_host_initialize(const DwrtHostConfig* config);
DWRT_HOST_API std::uint32_t dwrt_host_snapshot(DwrtHostSnapshot* out);
DWRT_HOST_API std::uint32_t dwrt_host_set_probe_mount_mask(std::uint32_t mount_mask);
DWRT_HOST_API std::uint32_t dwrt_host_reset_probe_counters();
DWRT_HOST_API std::uint32_t dwrt_host_probe_snapshot(DwrtProbeCountersNative* out);
DWRT_HOST_API std::uint32_t dwrt_host_shutdown();
