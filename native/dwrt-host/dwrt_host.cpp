#include "dwrt_host_api.hpp"

#include "dwrt_hook_backend.hpp"
#include "dwrt_host_testpoints.hpp"
#include "dwrt_probe_manifest.hpp"
#include "dwrt_shadow_shim.hpp"
#include "dwrt_signature_scanner.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct HostResolvedSignature {
    const dwrt::host::SignatureDescriptor* descriptor = nullptr;
    std::vector<std::uint32_t> rvas;
    std::optional<std::uint32_t> rva;
    std::string error;
    bool unique = false;
    bool expected_rva_ok = false;
};

struct HostState {
    dwrt::shim::DwrtShadowShim runtime;
    bool initialized = false;
    bool runtime_loaded = false;
    bool runtime_probe_ok = false;
    bool signatures_checked = false;
    bool used_live_server_module = false;
    bool used_mapped_file_fallback = false;
    std::uint32_t hook_install_attempts = 0;
    std::uint32_t hooks_installed = 0;
    std::uint32_t hook_install_failures = 0;
    std::size_t signature_required_failures = 0;
    std::string last_error;
    std::vector<HostResolvedSignature> signatures;
    dwrt::host::HookBackend hooks;
};

using TakeDamageOldFn = void(__fastcall*)(void*, void*, void*);
using AcceptInputFn = bool(__fastcall*)(void*, const char*, void*, void*, void*, int, void*);
using FireOutputInternalFn = void(__fastcall*)(void*, void*, void*, const void*, float, void*, void*);

std::mutex g_state_mutex;
HostState g_state;
TakeDamageOldFn g_original_take_damage_old = nullptr;
AcceptInputFn g_original_accept_input = nullptr;
FireOutputInternalFn g_original_fire_output_internal = nullptr;

std::string hex_u64(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << value;
    return out.str();
}

std::string windows_error_message(const char* prefix) {
    std::ostringstream out;
    out << prefix << " (GetLastError=" << GetLastError() << ")";
    return out.str();
}

std::wstring config_string_or(const wchar_t* value, const wchar_t* fallback) {
    if (value == nullptr || value[0] == L'\0') {
        return fallback;
    }
    return value;
}

HostResolvedSignature resolve_mapped_signature(
    const dwrt::host::PeModuleView& module,
    const dwrt::host::SignatureDescriptor& descriptor) {
    HostResolvedSignature resolved;
    resolved.descriptor = &descriptor;

    std::string error;
    const std::optional<dwrt::host::CompiledPattern> pattern =
        dwrt::host::compile_pattern(descriptor.pattern, error);
    if (!pattern.has_value()) {
        resolved.error = error;
        return resolved;
    }

    resolved.rvas = dwrt::host::find_pattern_rvas(module, *pattern, 3);
    resolved.unique = resolved.rvas.size() == 1;
    if (resolved.unique) {
        resolved.rva = resolved.rvas[0];
        resolved.expected_rva_ok = descriptor.expected_rva == 0 || resolved.rva.value() == descriptor.expected_rva;
    }
    else if (resolved.rvas.empty()) {
        resolved.error = "signature not found in mapped module";
    }
    else {
        resolved.error = "signature matched more than once in mapped module";
    }
    return resolved;
}

bool exercise_probe_abi(dwrt::shim::DwrtShadowShim& runtime) {
    DwrtFastDamageNative damage{};
    DwrtFastEntityIoNative input{};
    const std::uint32_t no_interest = runtime.probe_record_damage(damage);
    runtime.set_probe_mount_mask(DWRT_PROBE_MOUNT_DAMAGE | DWRT_PROBE_MOUNT_ENTITY_INPUT);
    const std::uint32_t damage_route = runtime.probe_record_damage(damage);
    const std::uint32_t input_route = runtime.probe_record_entity_input(input);
    DwrtProbeCountersNative snapshot{};
    const std::uint8_t snapshot_ok = runtime.probe_snapshot(snapshot);
    runtime.set_probe_mount_mask(0);
    runtime.probe_reset_counters();
    return no_interest == DWRT_PROBE_ROUTE_NO_INTEREST &&
        damage_route == DWRT_PROBE_ROUTE_COUNTED &&
        input_route == DWRT_PROBE_ROUTE_COUNTED &&
        snapshot_ok != 0 &&
        snapshot.damage_seen == 1 &&
        snapshot.damage_counted == 1 &&
        snapshot.entity_input_seen == 1 &&
        snapshot.entity_input_counted == 1;
}

void __fastcall detour_take_damage_old(void* self, void* info, void* result) {
    dwrt::host::CallbackScope scope(1);
    if (!scope.recursive()) {
        DwrtFastDamageNative event{};
        g_state.runtime.probe_record_damage(event);
    }
    if (g_original_take_damage_old != nullptr) {
        g_original_take_damage_old(self, info, result);
    }
}

bool __fastcall detour_accept_input(
    void* self,
    const char* input_name,
    void* activator,
    void* caller,
    void* variant_value,
    int output_id,
    void* unknown) {
    dwrt::host::CallbackScope scope(2);
    if (!scope.recursive()) {
        DwrtFastEntityIoNative event{};
        g_state.runtime.probe_record_entity_input(event);
    }
    if (g_original_accept_input == nullptr) {
        return false;
    }
    return g_original_accept_input(self, input_name, activator, caller, variant_value, output_id, unknown);
}

void __fastcall detour_fire_output_internal(
    void* self,
    void* activator,
    void* caller,
    const void* value,
    float delay,
    void* unknown1,
    void* unknown2) {
    dwrt::host::CallbackScope scope(3);
    if (!scope.recursive()) {
        DwrtFastEntityIoNative event{};
        g_state.runtime.probe_record_entity_output(event);
    }
    if (g_original_fire_output_internal != nullptr) {
        g_original_fire_output_internal(self, activator, caller, value, delay, unknown1, unknown2);
    }
}

const HostResolvedSignature* find_resolved_signature(const HostState& state, std::string_view name) {
    for (const HostResolvedSignature& signature : state.signatures) {
        if (signature.descriptor != nullptr && name == signature.descriptor->name) {
            return &signature;
        }
    }
    return nullptr;
}

void* resolved_target_address(
    const dwrt::host::PeModuleView& module,
    const HostState& state,
    std::string_view name) {
    const HostResolvedSignature* signature = find_resolved_signature(state, name);
    if (signature == nullptr || !signature->rva.has_value()) {
        return nullptr;
    }
    return const_cast<std::uint8_t*>(module.base() + signature->rva.value());
}

std::uint32_t install_probe_hooks(const dwrt::host::PeModuleView& module, HostState& state) {
    state.hook_install_attempts = 3;
    state.hook_install_failures = 0;
    state.hooks_installed = 0;
    state.hooks.reset();
    g_original_take_damage_old = nullptr;
    g_original_accept_input = nullptr;
    g_original_fire_output_internal = nullptr;

    auto install = [&](const char* name, void* detour, void** original_out) {
        std::string error;
        void* target = resolved_target_address(module, state, name);
        if (!state.hooks.install_inline(name, target, detour, original_out, error)) {
            state.hook_install_failures += 1;
            if (!state.last_error.empty()) {
                state.last_error += "; ";
            }
            state.last_error += std::string(name) + ": " + error;
            return;
        }
        state.hooks_installed += 1;
    };

    install(
        "CBaseEntity::TakeDamageOld",
        reinterpret_cast<void*>(&detour_take_damage_old),
        reinterpret_cast<void**>(&g_original_take_damage_old));
    install(
        "CEntityInstance::AcceptInput",
        reinterpret_cast<void*>(&detour_accept_input),
        reinterpret_cast<void**>(&g_original_accept_input));
    install(
        "CEntityIOOutput::FireOutputInternal",
        reinterpret_cast<void*>(&detour_fire_output_internal),
        reinterpret_cast<void**>(&g_original_fire_output_internal));

    if (state.hook_install_failures != 0) {
        state.hooks.reset();
        g_original_take_damage_old = nullptr;
        g_original_accept_input = nullptr;
        g_original_fire_output_internal = nullptr;
        return DWRT_HOST_ERROR_HOOK_INSTALL_FAILED;
    }
    return DWRT_HOST_OK;
}

std::uint32_t resolve_server_signatures(
    const DwrtHostConfig& config,
    HostState& state) {
    const std::wstring module_name = config_string_or(config.server_module_name, L"server.dll");
    HMODULE module_handle = GetModuleHandleW(module_name.c_str());
    HMODULE mapped_fallback = nullptr;
    std::filesystem::path module_path;

    if (module_handle != nullptr) {
        state.used_live_server_module = true;
        module_path = module_name;
    }
    else if ((config.flags & DWRT_HOST_FLAG_ALLOW_MAPPED_FILE_FALLBACK) != 0 &&
             config.server_path != nullptr && config.server_path[0] != L'\0') {
        module_path = config.server_path;
        mapped_fallback = LoadLibraryExW(
            config.server_path,
            nullptr,
            DONT_RESOLVE_DLL_REFERENCES);
        if (mapped_fallback == nullptr) {
            state.last_error = windows_error_message("failed to map server.dll fallback");
            return DWRT_HOST_ERROR_SERVER_MODULE_NOT_FOUND;
        }
        module_handle = mapped_fallback;
        state.used_mapped_file_fallback = true;
    }
    else {
        state.last_error = "server.dll is not loaded and no mapped file fallback was allowed";
        return DWRT_HOST_ERROR_SERVER_MODULE_NOT_FOUND;
    }

    std::string module_error;
    const std::optional<dwrt::host::PeModuleView> module =
        dwrt::host::PeModuleView::from_module_handle(module_handle, module_path, module_error);
    if (!module.has_value()) {
        state.last_error = module_error;
        if (mapped_fallback != nullptr) {
            FreeLibrary(mapped_fallback);
        }
        return DWRT_HOST_ERROR_SERVER_MODULE_NOT_FOUND;
    }

    state.signatures.clear();
    state.signature_required_failures = 0;
    state.signatures_checked = true;
    for (const dwrt::host::SignatureDescriptor& descriptor : dwrt::host::default_probe_signatures()) {
        HostResolvedSignature sig = resolve_mapped_signature(*module, descriptor);
        const bool acceptable_rva = sig.expected_rva_ok ||
            ((config.flags & DWRT_HOST_FLAG_ALLOW_EXPECTED_RVA_DRIFT) != 0);
        const bool ok = sig.unique && sig.rva.has_value() && acceptable_rva;
        if (descriptor.required && !ok) {
            state.signature_required_failures += 1;
        }
        state.signatures.push_back(std::move(sig));
    }

    if ((config.flags & DWRT_HOST_FLAG_REQUIRE_SIGNATURES) != 0 &&
        state.signature_required_failures != 0) {
        state.last_error = "required signature validation failed";
        if (mapped_fallback != nullptr) {
            FreeLibrary(mapped_fallback);
        }
        return DWRT_HOST_ERROR_SIGNATURE_VALIDATION_FAILED;
    }

    if ((config.flags & DWRT_HOST_FLAG_INSTALL_PROBE_HOOKS) != 0) {
        if (mapped_fallback != nullptr) {
            state.last_error = "refusing to install hooks into mapped-file fallback";
            FreeLibrary(mapped_fallback);
            return DWRT_HOST_ERROR_HOOK_INSTALL_FAILED;
        }
        const std::uint32_t hook_status = install_probe_hooks(*module, state);
        if (hook_status != DWRT_HOST_OK) {
            return hook_status;
        }
    }

    if (mapped_fallback != nullptr) {
        FreeLibrary(mapped_fallback);
    }
    return DWRT_HOST_OK;
}

std::string build_summary_json(const HostState& state) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"abiVersion\": " << DWRT_HOST_ABI_VERSION << ",\n";
    out << "  \"initialized\": " << (state.initialized ? "true" : "false") << ",\n";
    out << "  \"runtimeLoaded\": " << (state.runtime_loaded ? "true" : "false") << ",\n";
    out << "  \"runtimeProbeOk\": " << (state.runtime_probe_ok ? "true" : "false") << ",\n";
    out << "  \"signaturesChecked\": " << (state.signatures_checked ? "true" : "false") << ",\n";
    out << "  \"usedLiveServerModule\": " << (state.used_live_server_module ? "true" : "false") << ",\n";
    out << "  \"usedMappedFileFallback\": " << (state.used_mapped_file_fallback ? "true" : "false") << ",\n";
    out << "  \"signatureRequiredFailures\": " << state.signature_required_failures << ",\n";
    out << "  \"hookInstallAttempts\": " << state.hook_install_attempts << ",\n";
    out << "  \"hooksInstalled\": " << state.hooks_installed << ",\n";
    out << "  \"hookInstallFailures\": " << state.hook_install_failures << ",\n";
    const dwrt::host::HostTestpointSnapshot testpoints = dwrt::host::testpoint_snapshot();
    out << "  \"testpoints\": {\n";
    out << "    \"initializeCalls\": " << testpoints.initialize_calls << ",\n";
    out << "    \"initializeReentrantRejects\": " << testpoints.initialize_reentrant_rejects << ",\n";
    out << "    \"shutdownCalls\": " << testpoints.shutdown_calls << ",\n";
    out << "    \"callbackEntries\": " << testpoints.callback_entries << ",\n";
    out << "    \"callbackRecursiveEntries\": " << testpoints.callback_recursive_entries << ",\n";
    out << "    \"callbackCurrentDepth\": " << testpoints.callback_current_depth << ",\n";
    out << "    \"callbackMaxDepth\": " << testpoints.callback_max_depth << "\n";
    out << "  },\n";
    out << "  \"lastError\": \"" << dwrt::host::json_escape(state.last_error) << "\",\n";
    out << "  \"signatures\": [\n";
    for (std::size_t index = 0; index < state.signatures.size(); ++index) {
        const HostResolvedSignature& sig = state.signatures[index];
        const auto& desc = *sig.descriptor;
        out << "    {\n";
        out << "      \"name\": \"" << dwrt::host::json_escape(desc.name) << "\",\n";
        out << "      \"required\": " << (desc.required ? "true" : "false") << ",\n";
        out << "      \"matchCount\": " << sig.rvas.size() << ",\n";
        out << "      \"unique\": " << (sig.unique ? "true" : "false") << ",\n";
        out << "      \"rva\": ";
        if (sig.rva.has_value()) {
            out << "\"" << hex_u64(sig.rva.value()) << "\"";
        }
        else {
            out << "null";
        }
        out << ",\n";
        out << "      \"expectedRva\": \"" << hex_u64(desc.expected_rva) << "\",\n";
        out << "      \"expectedRvaOk\": " << (sig.expected_rva_ok ? "true" : "false") << ",\n";
        out << "      \"error\": \"" << dwrt::host::json_escape(sig.error) << "\"\n";
        out << "    }" << (index + 1 == state.signatures.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

std::uint32_t write_summary_if_requested(const DwrtHostConfig& config, const HostState& state) {
    if (config.summary_path == nullptr || config.summary_path[0] == L'\0') {
        return DWRT_HOST_OK;
    }

    const std::filesystem::path path(config.summary_path);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return DWRT_HOST_ERROR_SUMMARY_WRITE_FAILED;
    }
    output << build_summary_json(state);
    return DWRT_HOST_OK;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

DWRT_HOST_API std::uint32_t dwrt_host_abi_version() {
    return DWRT_HOST_ABI_VERSION;
}

DWRT_HOST_API std::uint32_t dwrt_host_initialize(const DwrtHostConfig* config) {
    dwrt::host::record_initialize_call();
    if (config == nullptr || config->abi_version != DWRT_HOST_ABI_VERSION) {
        return DWRT_HOST_ERROR_BAD_ARGUMENT;
    }

    std::scoped_lock lock(g_state_mutex);
    if (g_state.initialized) {
        dwrt::host::record_initialize_reentrant_reject();
        return DWRT_HOST_ERROR_ALREADY_INITIALIZED;
    }

    g_state.last_error.clear();
    g_state.runtime_loaded = false;
    g_state.runtime_probe_ok = false;
    g_state.signatures_checked = false;
    g_state.used_live_server_module = false;
    g_state.used_mapped_file_fallback = false;
    g_state.signature_required_failures = 0;
    g_state.hook_install_attempts = 0;
    g_state.hooks_installed = 0;
    g_state.hook_install_failures = 0;
    g_state.hooks.reset();
    g_original_take_damage_old = nullptr;
    g_original_accept_input = nullptr;
    g_original_fire_output_internal = nullptr;
    g_state.signatures.clear();

    if (config->runtime_path != nullptr && config->runtime_path[0] != L'\0') {
        if (!g_state.runtime.load(config->runtime_path, g_state.last_error)) {
            if ((config->flags & DWRT_HOST_FLAG_REQUIRE_RUNTIME) != 0) {
                return DWRT_HOST_ERROR_RUNTIME_LOAD_FAILED;
            }
        }
        else {
            g_state.runtime_loaded = true;
            if (g_state.runtime.abi_version() != DWRT_ABI_VERSION) {
                g_state.last_error = "unexpected DWRT runtime ABI version";
                if ((config->flags & DWRT_HOST_FLAG_REQUIRE_RUNTIME) != 0) {
                    return DWRT_HOST_ERROR_RUNTIME_ABI_MISMATCH;
                }
            }
            else if (!g_state.runtime.create_runtime(g_state.last_error)) {
                if ((config->flags & DWRT_HOST_FLAG_REQUIRE_RUNTIME) != 0) {
                    return DWRT_HOST_ERROR_RUNTIME_CREATE_FAILED;
                }
            }
            else {
                g_state.runtime_probe_ok = exercise_probe_abi(g_state.runtime);
                if (!g_state.runtime_probe_ok &&
                    (config->flags & DWRT_HOST_FLAG_REQUIRE_RUNTIME) != 0) {
                    g_state.last_error = "DWRT runtime probe ABI smoke mismatch";
                    return DWRT_HOST_ERROR_RUNTIME_PROBE_FAILED;
                }
            }
        }
    }
    else if ((config->flags & DWRT_HOST_FLAG_REQUIRE_RUNTIME) != 0) {
        g_state.last_error = "runtime path is required";
        return DWRT_HOST_ERROR_RUNTIME_LOAD_FAILED;
    }

    const std::uint32_t resolve_status = resolve_server_signatures(*config, g_state);
    if (resolve_status != DWRT_HOST_OK &&
        (config->flags & DWRT_HOST_FLAG_REQUIRE_SIGNATURES) != 0) {
        return resolve_status;
    }

    g_state.initialized = true;
    const std::uint32_t write_status = write_summary_if_requested(*config, g_state);
    if (write_status != DWRT_HOST_OK) {
        return write_status;
    }

    return DWRT_HOST_OK;
}

DWRT_HOST_API std::uint32_t dwrt_host_snapshot(DwrtHostSnapshot* out) {
    if (out == nullptr) {
        return DWRT_HOST_ERROR_BAD_ARGUMENT;
    }

    std::scoped_lock lock(g_state_mutex);
    out->abi_version = DWRT_HOST_ABI_VERSION;
    out->initialized = g_state.initialized ? 1U : 0U;
    out->runtime_loaded = g_state.runtime_loaded ? 1U : 0U;
    out->runtime_probe_ok = g_state.runtime_probe_ok ? 1U : 0U;
    out->signatures_checked = g_state.signatures_checked ? 1U : 0U;
    out->signature_required_failures = static_cast<std::uint32_t>(g_state.signature_required_failures);
    out->used_live_server_module = g_state.used_live_server_module ? 1U : 0U;
    out->used_mapped_file_fallback = g_state.used_mapped_file_fallback ? 1U : 0U;
    out->hook_install_attempts = g_state.hook_install_attempts;
    out->hooks_installed = g_state.hooks_installed;
    out->hook_install_failures = g_state.hook_install_failures;
    const dwrt::host::HostTestpointSnapshot testpoints = dwrt::host::testpoint_snapshot();
    out->initialize_calls = testpoints.initialize_calls;
    out->initialize_reentrant_rejects = testpoints.initialize_reentrant_rejects;
    out->shutdown_calls = testpoints.shutdown_calls;
    out->callback_entries = testpoints.callback_entries;
    out->callback_recursive_entries = testpoints.callback_recursive_entries;
    out->callback_current_depth = testpoints.callback_current_depth;
    out->callback_max_depth = testpoints.callback_max_depth;
    return DWRT_HOST_OK;
}

DWRT_HOST_API std::uint32_t dwrt_host_set_probe_mount_mask(std::uint32_t mount_mask) {
    std::scoped_lock lock(g_state_mutex);
    if (!g_state.initialized || !g_state.runtime.has_runtime()) {
        return DWRT_HOST_ERROR_NOT_INITIALIZED;
    }
    g_state.runtime.set_probe_mount_mask(mount_mask);
    return DWRT_HOST_OK;
}

DWRT_HOST_API std::uint32_t dwrt_host_reset_probe_counters() {
    std::scoped_lock lock(g_state_mutex);
    if (!g_state.initialized || !g_state.runtime.has_runtime()) {
        return DWRT_HOST_ERROR_NOT_INITIALIZED;
    }
    g_state.runtime.probe_reset_counters();
    return DWRT_HOST_OK;
}

DWRT_HOST_API std::uint32_t dwrt_host_probe_snapshot(DwrtProbeCountersNative* out) {
    if (out == nullptr) {
        return DWRT_HOST_ERROR_BAD_ARGUMENT;
    }
    std::scoped_lock lock(g_state_mutex);
    if (!g_state.initialized || !g_state.runtime.has_runtime()) {
        return DWRT_HOST_ERROR_NOT_INITIALIZED;
    }
    return g_state.runtime.probe_snapshot(*out) != 0 ? DWRT_HOST_OK : DWRT_HOST_ERROR_RUNTIME_PROBE_FAILED;
}

DWRT_HOST_API std::uint32_t dwrt_host_shutdown() {
    dwrt::host::record_shutdown_call();
    std::scoped_lock lock(g_state_mutex);
    g_state.hooks.reset();
    g_original_take_damage_old = nullptr;
    g_original_accept_input = nullptr;
    g_original_fire_output_internal = nullptr;
    g_state.runtime.unload();
    g_state.initialized = false;
    g_state.runtime_loaded = false;
    g_state.runtime_probe_ok = false;
    g_state.signatures_checked = false;
    g_state.used_live_server_module = false;
    g_state.used_mapped_file_fallback = false;
    g_state.signature_required_failures = 0;
    g_state.hook_install_attempts = 0;
    g_state.hooks_installed = 0;
    g_state.hook_install_failures = 0;
    g_state.last_error.clear();
    g_state.signatures.clear();
    return DWRT_HOST_OK;
}
