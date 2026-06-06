#include "dwrt_host_api.hpp"

#include "dwrt_friendly_fire.hpp"
#include "dwrt_hook_backend.hpp"
#include "dwrt_host_testpoints.hpp"
#include "dwrt_probe_manifest.hpp"
#include "dwrt_shadow_shim.hpp"
#include "dwrt_signature_scanner.hpp"
#include "dwrt_target_probe.hpp"
#include "dwrt_walker_patrol.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
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
using TargetFriendlyFireFilterFn = bool(__fastcall*)(void*, void*, void*, void*);
using TargetFriendlyFireCallerFn = bool(__fastcall*)(void*, void*, void*, void*);
using TargetSecondaryFriendlyFireGateFn = bool(__fastcall*)(void*, void*, void*);
using TargetIdentityClassifierFn = std::uint32_t*(__fastcall*)(void*, std::uint32_t*);

std::mutex g_state_mutex;
HostState g_state;
TakeDamageOldFn g_original_take_damage_old = nullptr;
AcceptInputFn g_original_accept_input = nullptr;
FireOutputInternalFn g_original_fire_output_internal = nullptr;
TargetFriendlyFireFilterFn g_original_target_friendly_fire_filter = nullptr;
TargetFriendlyFireCallerFn g_original_target_friendly_fire_caller = nullptr;
TargetSecondaryFriendlyFireGateFn g_original_target_secondary_friendly_fire_gate = nullptr;
TargetIdentityClassifierFn g_original_target_identity_classifier = nullptr;

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
    dwrt::host::record_damage_victim(self, info, scope.recursive());
    dwrt::host::maybe_apply_walker_patrol_on_damage(self, scope.recursive());
    const dwrt::host::FriendlyFireDamageScope friendly_fire_scope =
        dwrt::host::begin_friendly_fire_damage(self, scope.recursive());
    if (!scope.recursive()) {
        DwrtFastDamageNative event{};
        g_state.runtime.probe_record_damage(event);
    }
    if (g_original_take_damage_old != nullptr) {
        g_original_take_damage_old(self, info, result);
    }
    dwrt::host::end_friendly_fire_damage(friendly_fire_scope);
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
        dwrt::host::maybe_global_neutralize_entity(self);
        dwrt::host::maybe_global_neutralize_entity(activator);
        dwrt::host::maybe_global_neutralize_entity(caller);
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
        dwrt::host::maybe_global_neutralize_entity(self);
        dwrt::host::maybe_global_neutralize_entity(activator);
        dwrt::host::maybe_global_neutralize_entity(caller);
        DwrtFastEntityIoNative event{};
        g_state.runtime.probe_record_entity_output(event);
    }
    if (g_original_fire_output_internal != nullptr) {
        g_original_fire_output_internal(self, activator, caller, value, delay, unknown1, unknown2);
    }
}

bool __fastcall detour_target_friendly_fire_filter(void* source, void* target, void* context, void* bitset) {
    dwrt::host::CallbackScope scope(4);
    const dwrt::host::TargetFilterScope target_scope =
        dwrt::host::begin_target_filter(source, target, false);
    dwrt::host::maybe_allow_target_bitset(
        target_scope,
        target,
        bitset,
        reinterpret_cast<dwrt::host::TargetIdentityClassifierFn>(g_original_target_identity_classifier));
    bool result = true;
    dwrt::host::begin_target_classifier_spoof_scope(target_scope);
    if (g_original_target_friendly_fire_filter != nullptr) {
        result = g_original_target_friendly_fire_filter(source, target, context, bitset);
    }
    dwrt::host::end_target_classifier_spoof_scope();
    return dwrt::host::finish_target_filter(target_scope, source, target, result);
}

bool __fastcall detour_target_friendly_fire_caller(void* source, void* target, void* context, void* bitset) {
    dwrt::host::CallbackScope scope(5);
    bool result = true;
    if (g_original_target_friendly_fire_caller != nullptr) {
        result = g_original_target_friendly_fire_caller(source, target, context, bitset);
    }
    if (!scope.recursive()) {
        return dwrt::host::record_target_filter_caller(source, target, result);
    }
    return result;
}

bool __fastcall detour_target_secondary_friendly_fire_gate(void* source, void* maybe_attacker, void* maybe_target) {
    dwrt::host::CallbackScope scope(6);
    bool result = true;
    if (g_original_target_secondary_friendly_fire_gate != nullptr) {
        result = g_original_target_secondary_friendly_fire_gate(source, maybe_attacker, maybe_target);
    }
    if (!scope.recursive()) {
        return dwrt::host::record_secondary_target_gate(source, maybe_attacker, maybe_target, result);
    }
    return result;
}

std::uint32_t* __fastcall detour_target_identity_classifier(void* target, std::uint32_t* out) {
    std::uint32_t* result = out;
    if (g_original_target_identity_classifier != nullptr) {
        result = g_original_target_identity_classifier(target, out);
    }
    dwrt::host::finish_target_identity_classifier(target, out);
    return result;
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
    state.hook_install_attempts = 7;
    state.hook_install_failures = 0;
    state.hooks_installed = 0;
    state.hooks.reset();
    g_original_take_damage_old = nullptr;
    g_original_accept_input = nullptr;
    g_original_fire_output_internal = nullptr;
    g_original_target_friendly_fire_filter = nullptr;
    g_original_target_friendly_fire_caller = nullptr;
    g_original_target_secondary_friendly_fire_gate = nullptr;
    g_original_target_identity_classifier = nullptr;

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
    install(
        "CitadelTargetFilter::FriendlyFire",
        reinterpret_cast<void*>(&detour_target_friendly_fire_filter),
        reinterpret_cast<void**>(&g_original_target_friendly_fire_filter));
    install(
        "CitadelTargetFilter::FriendlyFireCaller",
        reinterpret_cast<void*>(&detour_target_friendly_fire_caller),
        reinterpret_cast<void**>(&g_original_target_friendly_fire_caller));
    install(
        "CitadelTargetFilter::SecondaryFriendlyFireGate",
        reinterpret_cast<void*>(&detour_target_secondary_friendly_fire_gate),
        reinterpret_cast<void**>(&g_original_target_secondary_friendly_fire_gate));
    install(
        "CEntityIdentity::IndexForEntityInstance",
        reinterpret_cast<void*>(&detour_target_identity_classifier),
        reinterpret_cast<void**>(&g_original_target_identity_classifier));

    if (state.hook_install_failures != 0) {
        state.hooks.reset();
        g_original_take_damage_old = nullptr;
        g_original_accept_input = nullptr;
        g_original_fire_output_internal = nullptr;
        g_original_target_friendly_fire_filter = nullptr;
        g_original_target_friendly_fire_caller = nullptr;
        g_original_target_secondary_friendly_fire_gate = nullptr;
        g_original_target_identity_classifier = nullptr;
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
    const DwrtWalkerPatrolSnapshot walker_patrol = dwrt::host::walker_patrol_snapshot();
    const DwrtFriendlyFireSnapshot friendly_fire = dwrt::host::friendly_fire_snapshot();
    const DwrtTargetProbeSnapshot target_probe = dwrt::host::target_probe_snapshot();
    out << "  \"walkerPatrol\": {\n";
    out << "    \"enabled\": " << walker_patrol.enabled << ",\n";
    out << "    \"stride\": " << walker_patrol.stride << ",\n";
    out << "    \"waypointCount\": " << walker_patrol.waypoint_count << ",\n";
    out << "    \"mode\": " << walker_patrol.mode << ",\n";
    out << "    \"damageCallbacks\": " << walker_patrol.damage_callbacks << ",\n";
    out << "    \"candidateWalkers\": " << walker_patrol.candidate_walkers << ",\n";
    out << "    \"nonWalkerVictims\": " << walker_patrol.non_walker_victims << ",\n";
    out << "    \"skippedRecursive\": " << walker_patrol.skipped_recursive << ",\n";
    out << "    \"missingIdentity\": " << walker_patrol.missing_identity << ",\n";
    out << "    \"missingDesignerName\": " << walker_patrol.missing_designer_name << ",\n";
    out << "    \"teleportAttempts\": " << walker_patrol.teleport_attempts << ",\n";
    out << "    \"teleportCalls\": " << walker_patrol.teleport_calls << ",\n";
    out << "    \"bodyComponentMissing\": " << walker_patrol.body_component_missing << ",\n";
    out << "    \"sceneNodeMissing\": " << walker_patrol.scene_node_missing << ",\n";
    out << "    \"originReadAttempts\": " << walker_patrol.origin_read_attempts << ",\n";
    out << "    \"originReadSuccesses\": " << walker_patrol.origin_read_successes << ",\n";
    out << "    \"originReadFailures\": " << walker_patrol.origin_read_failures << "\n";
    out << "  },\n";
    out << "  \"friendlyFire\": {\n";
    out << "    \"enabled\": " << friendly_fire.enabled << ",\n";
    out << "    \"mode\": " << friendly_fire.mode << ",\n";
    out << "    \"scope\": " << friendly_fire.scope << ",\n";
    out << "    \"localTeam\": " << friendly_fire.local_team << ",\n";
    out << "    \"damageCallbacks\": " << friendly_fire.damage_callbacks << ",\n";
    out << "    \"skippedRecursive\": " << friendly_fire.skipped_recursive << ",\n";
    out << "    \"missingIdentity\": " << friendly_fire.missing_identity << ",\n";
    out << "    \"missingDesignerName\": " << friendly_fire.missing_designer_name << ",\n";
    out << "    \"nonObjectiveVictims\": " << friendly_fire.non_objective_victims << ",\n";
    out << "    \"objectiveCandidates\": " << friendly_fire.objective_candidates << ",\n";
    out << "    \"invalidTeam\": " << friendly_fire.invalid_team << ",\n";
    out << "    \"teamSpoofAttempts\": " << friendly_fire.team_spoof_attempts << ",\n";
    out << "    \"teamSpoofApplied\": " << friendly_fire.team_spoof_applied << ",\n";
    out << "    \"teamSpoofRestored\": " << friendly_fire.team_spoof_restored << "\n";
    out << "  },\n";
    out << "  \"targetProbe\": {\n";
    out << "    \"enabled\": " << target_probe.enabled << ",\n";
    out << "    \"sourceTeamSpoofEnabled\": " << target_probe.source_team_spoof_enabled << ",\n";
    out << "    \"targetTeamSpoofEnabled\": " << target_probe.target_team_spoof_enabled << ",\n";
    out << "    \"targetTeamSpoofTeam\": " << target_probe.target_team_spoof_team << ",\n";
    out << "    \"targetBitsetAllowEnabled\": " << target_probe.target_bitset_allow_enabled << ",\n";
    out << "    \"forceFilterSameTeamAllowEnabled\": " << target_probe.force_filter_same_team_allow_enabled << ",\n";
    out << "    \"forceCallerSameTeamAllowEnabled\": " << target_probe.force_caller_same_team_allow_enabled << ",\n";
    out << "    \"forceFilterObjectiveAllowEnabled\": " << target_probe.force_filter_objective_allow_enabled << ",\n";
    out << "    \"forceCallerObjectiveAllowEnabled\": " << target_probe.force_caller_objective_allow_enabled << ",\n";
    out << "    \"forceSecondaryAllowEnabled\": " << target_probe.force_secondary_allow_enabled << ",\n";
    out << "    \"neutralSimulationEnabled\": " << target_probe.neutral_simulation_enabled << ",\n";
    out << "    \"classifierSpoofEnabled\": " << target_probe.classifier_spoof_enabled << ",\n";
    out << "    \"classifierSpoofBit\": " << target_probe.classifier_spoof_bit << ",\n";
    out << "    \"globalNeutralizeEnabled\": " << target_probe.global_neutralize_enabled << ",\n";
    out << "    \"globalNeutralizeTeam\": " << target_probe.global_neutralize_team << ",\n";
    out << "    \"lastSourceTeam\": " << target_probe.last_source_team << ",\n";
    out << "    \"lastTargetTeam\": " << target_probe.last_target_team << ",\n";
    out << "    \"lastTargetUnitType\": " << target_probe.last_target_unit_type << ",\n";
    out << "    \"lastFilterResult\": " << target_probe.last_filter_result << ",\n";
    out << "    \"lastDamageVictimTeam\": " << target_probe.last_damage_victim_team << ",\n";
    out << "    \"lastDamageVictimUnitType\": " << target_probe.last_damage_victim_unit_type << ",\n";
    out << "    \"lastDamageAttackerHandle\": " << target_probe.last_damage_attacker_handle << ",\n";
    out << "    \"lastDamageInflictorHandle\": " << target_probe.last_damage_inflictor_handle << ",\n";
    out << "    \"lastDamageAttackerTeam\": " << target_probe.last_damage_attacker_team << ",\n";
    out << "    \"lastDamageAttackerUnitType\": " << target_probe.last_damage_attacker_unit_type << ",\n";
    out << "    \"filterCalls\": " << target_probe.filter_calls << ",\n";
    out << "    \"filterAllowed\": " << target_probe.filter_allowed << ",\n";
    out << "    \"filterDenied\": " << target_probe.filter_denied << ",\n";
    out << "    \"filterSameTeamCalls\": " << target_probe.filter_same_team_calls << ",\n";
    out << "    \"filterSameTeamAllowed\": " << target_probe.filter_same_team_allowed << ",\n";
    out << "    \"filterSameTeamDenied\": " << target_probe.filter_same_team_denied << ",\n";
    out << "    \"filterObjectiveTargetCalls\": " << target_probe.filter_objective_target_calls << ",\n";
    out << "    \"filterObjectiveTargetAllowed\": " << target_probe.filter_objective_target_allowed << ",\n";
    out << "    \"filterObjectiveTargetDenied\": " << target_probe.filter_objective_target_denied << ",\n";
    out << "    \"filterNeutralTargetCalls\": " << target_probe.filter_neutral_target_calls << ",\n";
    out << "    \"filterMidbossTargetCalls\": " << target_probe.filter_midboss_target_calls << ",\n";
    out << "    \"filterInvalidSourceTeam\": " << target_probe.filter_invalid_source_team << ",\n";
    out << "    \"filterInvalidTargetTeam\": " << target_probe.filter_invalid_target_team << ",\n";
    out << "    \"sourceSpoofAttempts\": " << target_probe.source_spoof_attempts << ",\n";
    out << "    \"sourceSpoofApplied\": " << target_probe.source_spoof_applied << ",\n";
    out << "    \"sourceSpoofRestored\": " << target_probe.source_spoof_restored << ",\n";
    out << "    \"sourceSpoofAllowed\": " << target_probe.source_spoof_allowed << ",\n";
    out << "    \"sourceSpoofDenied\": " << target_probe.source_spoof_denied << ",\n";
    out << "    \"targetSpoofAttempts\": " << target_probe.target_spoof_attempts << ",\n";
    out << "    \"targetSpoofApplied\": " << target_probe.target_spoof_applied << ",\n";
    out << "    \"targetSpoofRestored\": " << target_probe.target_spoof_restored << ",\n";
    out << "    \"targetSpoofAllowed\": " << target_probe.target_spoof_allowed << ",\n";
    out << "    \"targetSpoofDenied\": " << target_probe.target_spoof_denied << ",\n";
    out << "    \"bitsetAllowAttempts\": " << target_probe.bitset_allow_attempts << ",\n";
    out << "    \"bitsetAllowApplied\": " << target_probe.bitset_allow_applied << ",\n";
    out << "    \"bitsetAllowFailures\": " << target_probe.bitset_allow_failures << ",\n";
    out << "    \"classifierCalls\": " << target_probe.classifier_calls << ",\n";
    out << "    \"classifierInvalid\": " << target_probe.classifier_invalid << ",\n";
    out << "    \"classifierSpoofAttempts\": " << target_probe.classifier_spoof_attempts << ",\n";
    out << "    \"classifierSpoofApplied\": " << target_probe.classifier_spoof_applied << ",\n";
    out << "    \"classifierSpoofSkipped\": " << target_probe.classifier_spoof_skipped << ",\n";
    out << "    \"lastClassifierOriginalBit\": " << target_probe.last_classifier_original_bit << ",\n";
    out << "    \"lastClassifierFinalBit\": " << target_probe.last_classifier_final_bit << ",\n";
    out << "    \"globalNeutralizeAttempts\": " << target_probe.global_neutralize_attempts << ",\n";
    out << "    \"globalNeutralizeApplied\": " << target_probe.global_neutralize_applied << ",\n";
    out << "    \"globalNeutralizeAlready\": " << target_probe.global_neutralize_already << ",\n";
    out << "    \"globalNeutralizeNull\": " << target_probe.global_neutralize_null << ",\n";
    out << "    \"globalNeutralizeInvalidTeam\": " << target_probe.global_neutralize_invalid_team << ",\n";
    out << "    \"globalNeutralizeWriteFailures\": " << target_probe.global_neutralize_write_failures << ",\n";
    out << "    \"globalNeutralizeOriginalTeamOverflow\": " << target_probe.global_neutralize_original_team_overflow << ",\n";
    out << "    \"globalNeutralizeOriginalTeamCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe.global_neutralize_original_team_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe.global_neutralize_original_team_counts[index];
    }
    out << "],\n";
    out << "    \"filterForcedAllowed\": " << target_probe.filter_forced_allowed << ",\n";
    out << "    \"callerForcedAllowed\": " << target_probe.caller_forced_allowed << ",\n";
    out << "    \"secondaryForcedAllowed\": " << target_probe.secondary_forced_allowed << ",\n";
    out << "    \"secondaryCalls\": " << target_probe.secondary_calls << ",\n";
    out << "    \"secondaryAllowed\": " << target_probe.secondary_allowed << ",\n";
    out << "    \"secondaryDenied\": " << target_probe.secondary_denied << ",\n";
    out << "    \"callerCalls\": " << target_probe.caller_calls << ",\n";
    out << "    \"callerAllowed\": " << target_probe.caller_allowed << ",\n";
    out << "    \"callerDenied\": " << target_probe.caller_denied << ",\n";
    out << "    \"callerUnit0x1aBypassCandidates\": " << target_probe.caller_unit_0x1a_bypass_candidates << ",\n";
    out << "    \"filterUnitTypeOverflow\": " << target_probe.filter_unit_type_overflow << ",\n";
    out << "    \"filterUnitTypeCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe.filter_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe.filter_unit_type_counts[index];
    }
    out << "],\n";
    out << "    \"damageVictimCalls\": " << target_probe.damage_victim_calls << ",\n";
    out << "    \"damageVictimObjective\": " << target_probe.damage_victim_objective << ",\n";
    out << "    \"damageVictimNeutral\": " << target_probe.damage_victim_neutral << ",\n";
    out << "    \"damageVictimMidboss\": " << target_probe.damage_victim_midboss << ",\n";
    out << "    \"damageVictimInvalidTeam\": " << target_probe.damage_victim_invalid_team << ",\n";
    out << "    \"damageVictimUnitTypeOverflow\": " << target_probe.damage_victim_unit_type_overflow << ",\n";
    out << "    \"damageVictimUnitTypeCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe.damage_victim_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe.damage_victim_unit_type_counts[index];
    }
    out << "],\n";
    out << "    \"damageAttackerHandleValid\": " << target_probe.damage_attacker_handle_valid << ",\n";
    out << "    \"damageAttackerHandleInvalid\": " << target_probe.damage_attacker_handle_invalid << ",\n";
    out << "    \"damageInflictorHandleValid\": " << target_probe.damage_inflictor_handle_valid << ",\n";
    out << "    \"damageInflictorHandleInvalid\": " << target_probe.damage_inflictor_handle_invalid << ",\n";
    out << "    \"damageAttackerSameTeam\": " << target_probe.damage_attacker_same_team << ",\n";
    out << "    \"damageAttackerOpposingTeam\": " << target_probe.damage_attacker_opposing_team << ",\n";
    out << "    \"damageAttackerOtherTeam\": " << target_probe.damage_attacker_other_team << ",\n";
    out << "    \"damageAttackerSelf\": " << target_probe.damage_attacker_self << ",\n";
    out << "    \"damageAttackerSameTeamObjective\": " << target_probe.damage_attacker_same_team_objective << ",\n";
    out << "    \"damageAttackerUnitTypeOverflow\": " << target_probe.damage_attacker_unit_type_overflow << ",\n";
    out << "    \"damageAttackerUnitTypeCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe.damage_attacker_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe.damage_attacker_unit_type_counts[index];
    }
    out << "],\n";
    out << "    \"damageSameTeamVictimUnitTypeOverflow\": " << target_probe.damage_same_team_victim_unit_type_overflow << ",\n";
    out << "    \"damageSameTeamVictimUnitTypeCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe.damage_same_team_victim_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe.damage_same_team_victim_unit_type_counts[index];
    }
    out << "]\n";
    out << "  },\n";
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
    g_original_target_friendly_fire_filter = nullptr;
    g_original_target_friendly_fire_caller = nullptr;
    g_original_target_secondary_friendly_fire_gate = nullptr;
    g_original_target_identity_classifier = nullptr;
    g_state.signatures.clear();
    dwrt::host::reset_walker_patrol_counters();
    dwrt::host::configure_walker_patrol_from_environment();
    dwrt::host::reset_friendly_fire_counters();
    dwrt::host::configure_friendly_fire_from_environment();
    dwrt::host::reset_target_probe_counters();
    dwrt::host::configure_target_probe_from_environment();

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
    dwrt::host::reset_walker_patrol_counters();
    dwrt::host::reset_friendly_fire_counters();
    dwrt::host::reset_target_probe_counters();
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

DWRT_HOST_API std::uint32_t dwrt_host_walker_patrol_snapshot(DwrtWalkerPatrolSnapshot* out) {
    if (out == nullptr) {
        return DWRT_HOST_ERROR_BAD_ARGUMENT;
    }
    *out = dwrt::host::walker_patrol_snapshot();
    return DWRT_HOST_OK;
}

DWRT_HOST_API std::uint32_t dwrt_host_friendly_fire_snapshot(DwrtFriendlyFireSnapshot* out) {
    if (out == nullptr) {
        return DWRT_HOST_ERROR_BAD_ARGUMENT;
    }
    *out = dwrt::host::friendly_fire_snapshot();
    return DWRT_HOST_OK;
}

DWRT_HOST_API std::uint32_t dwrt_host_target_probe_snapshot(DwrtTargetProbeSnapshot* out) {
    if (out == nullptr) {
        return DWRT_HOST_ERROR_BAD_ARGUMENT;
    }
    *out = dwrt::host::target_probe_snapshot();
    return DWRT_HOST_OK;
}

DWRT_HOST_API std::uint32_t dwrt_host_shutdown() {
    dwrt::host::record_shutdown_call();
    std::scoped_lock lock(g_state_mutex);
    g_state.hooks.reset();
    g_original_take_damage_old = nullptr;
    g_original_accept_input = nullptr;
    g_original_fire_output_internal = nullptr;
    g_original_target_friendly_fire_filter = nullptr;
    g_original_target_friendly_fire_caller = nullptr;
    g_original_target_secondary_friendly_fire_gate = nullptr;
    g_original_target_identity_classifier = nullptr;
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
    dwrt::host::reset_walker_patrol_counters();
    return DWRT_HOST_OK;
}
