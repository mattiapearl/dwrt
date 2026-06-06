#include "dwrt_target_probe.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string_view>

namespace dwrt::host {
namespace {

constexpr std::size_t kCBaseEntityTeamNumOffset = 0x33c;
constexpr std::size_t kTakeDamageInfoInflictorHandleOffset = 0x38;
constexpr std::size_t kTakeDamageInfoAttackerHandleOffset = 0x3c;
constexpr std::uintptr_t kEntitySystemChunkTableRva = 0x032b3280;
constexpr std::uint32_t kInvalidTeam = 0xffffffffU;
constexpr std::uint32_t kInvalidUnitType = 0xffffffffU;
constexpr std::uint32_t kInvalidHandle = 0xffffffffU;

struct EntityIdentityLite {
    void* instance;
    void* entity_class;
    std::uint32_t ehandle;
    std::int32_t name_stringable_index;
    const char* name;
    const char* designer_name;
};

struct TargetProbeState {
    std::atomic<std::uint32_t> enabled{1};
    std::atomic<std::uint32_t> source_team_spoof_enabled{0};
    std::atomic<std::uint32_t> target_team_spoof_enabled{0};
    std::atomic<std::uint32_t> target_team_spoof_team{kInvalidTeam};
    std::atomic<std::uint32_t> target_bitset_allow_enabled{0};
    std::atomic<std::uint32_t> force_filter_same_team_allow_enabled{0};
    std::atomic<std::uint32_t> force_caller_same_team_allow_enabled{0};
    std::atomic<std::uint32_t> force_filter_objective_allow_enabled{0};
    std::atomic<std::uint32_t> force_caller_objective_allow_enabled{0};
    std::atomic<std::uint32_t> force_secondary_allow_enabled{0};
    std::atomic<std::uint32_t> neutral_simulation_enabled{0};
    std::atomic<std::uint32_t> classifier_spoof_enabled{0};
    std::atomic<std::uint32_t> classifier_spoof_bit{kInvalidUnitType};
    std::atomic<std::uint32_t> global_neutralize_enabled{0};
    std::atomic<std::uint32_t> global_neutralize_team{4};
    std::atomic<std::uint32_t> last_source_team{kInvalidTeam};
    std::atomic<std::uint32_t> last_target_team{kInvalidTeam};
    std::atomic<std::uint32_t> last_target_unit_type{kInvalidUnitType};
    std::atomic<std::uint32_t> last_filter_result{0};
    std::atomic<std::uint32_t> last_damage_victim_team{kInvalidTeam};
    std::atomic<std::uint32_t> last_damage_victim_unit_type{kInvalidUnitType};
    std::atomic<std::uint32_t> last_damage_attacker_handle{kInvalidHandle};
    std::atomic<std::uint32_t> last_damage_inflictor_handle{kInvalidHandle};
    std::atomic<std::uint32_t> last_damage_attacker_team{kInvalidTeam};
    std::atomic<std::uint32_t> last_damage_attacker_unit_type{kInvalidUnitType};
    std::atomic<std::uint64_t> filter_calls{0};
    std::atomic<std::uint64_t> filter_allowed{0};
    std::atomic<std::uint64_t> filter_denied{0};
    std::atomic<std::uint64_t> filter_same_team_calls{0};
    std::atomic<std::uint64_t> filter_same_team_allowed{0};
    std::atomic<std::uint64_t> filter_same_team_denied{0};
    std::atomic<std::uint64_t> filter_objective_target_calls{0};
    std::atomic<std::uint64_t> filter_objective_target_allowed{0};
    std::atomic<std::uint64_t> filter_objective_target_denied{0};
    std::atomic<std::uint64_t> filter_neutral_target_calls{0};
    std::atomic<std::uint64_t> filter_midboss_target_calls{0};
    std::atomic<std::uint64_t> filter_invalid_source_team{0};
    std::atomic<std::uint64_t> filter_invalid_target_team{0};
    std::atomic<std::uint64_t> source_spoof_attempts{0};
    std::atomic<std::uint64_t> source_spoof_applied{0};
    std::atomic<std::uint64_t> source_spoof_restored{0};
    std::atomic<std::uint64_t> source_spoof_allowed{0};
    std::atomic<std::uint64_t> source_spoof_denied{0};
    std::atomic<std::uint64_t> target_spoof_attempts{0};
    std::atomic<std::uint64_t> target_spoof_applied{0};
    std::atomic<std::uint64_t> target_spoof_restored{0};
    std::atomic<std::uint64_t> target_spoof_allowed{0};
    std::atomic<std::uint64_t> target_spoof_denied{0};
    std::atomic<std::uint64_t> bitset_allow_attempts{0};
    std::atomic<std::uint64_t> bitset_allow_applied{0};
    std::atomic<std::uint64_t> bitset_allow_failures{0};
    std::atomic<std::uint64_t> classifier_calls{0};
    std::atomic<std::uint64_t> classifier_invalid{0};
    std::atomic<std::uint64_t> classifier_spoof_attempts{0};
    std::atomic<std::uint64_t> classifier_spoof_applied{0};
    std::atomic<std::uint64_t> classifier_spoof_skipped{0};
    std::atomic<std::uint64_t> last_classifier_original_bit{kInvalidUnitType};
    std::atomic<std::uint64_t> last_classifier_final_bit{kInvalidUnitType};
    std::atomic<std::uint64_t> global_neutralize_attempts{0};
    std::atomic<std::uint64_t> global_neutralize_applied{0};
    std::atomic<std::uint64_t> global_neutralize_already{0};
    std::atomic<std::uint64_t> global_neutralize_null{0};
    std::atomic<std::uint64_t> global_neutralize_invalid_team{0};
    std::atomic<std::uint64_t> global_neutralize_write_failures{0};
    std::array<std::atomic<std::uint64_t>, 8> global_neutralize_original_team_counts{};
    std::atomic<std::uint64_t> global_neutralize_original_team_overflow{0};
    std::atomic<std::uint64_t> filter_forced_allowed{0};
    std::atomic<std::uint64_t> caller_forced_allowed{0};
    std::atomic<std::uint64_t> secondary_forced_allowed{0};
    std::atomic<std::uint64_t> secondary_calls{0};
    std::atomic<std::uint64_t> secondary_allowed{0};
    std::atomic<std::uint64_t> secondary_denied{0};
    std::atomic<std::uint64_t> caller_calls{0};
    std::atomic<std::uint64_t> caller_allowed{0};
    std::atomic<std::uint64_t> caller_denied{0};
    std::atomic<std::uint64_t> caller_unit_0x1a_bypass_candidates{0};
    std::array<std::atomic<std::uint64_t>, 32> filter_unit_type_counts{};
    std::atomic<std::uint64_t> filter_unit_type_overflow{0};
    std::atomic<std::uint64_t> damage_victim_calls{0};
    std::atomic<std::uint64_t> damage_victim_objective{0};
    std::atomic<std::uint64_t> damage_victim_neutral{0};
    std::atomic<std::uint64_t> damage_victim_midboss{0};
    std::atomic<std::uint64_t> damage_victim_invalid_team{0};
    std::array<std::atomic<std::uint64_t>, 64> damage_victim_unit_type_counts{};
    std::atomic<std::uint64_t> damage_victim_unit_type_overflow{0};
    std::atomic<std::uint64_t> damage_attacker_handle_valid{0};
    std::atomic<std::uint64_t> damage_attacker_handle_invalid{0};
    std::atomic<std::uint64_t> damage_inflictor_handle_valid{0};
    std::atomic<std::uint64_t> damage_inflictor_handle_invalid{0};
    std::atomic<std::uint64_t> damage_attacker_same_team{0};
    std::atomic<std::uint64_t> damage_attacker_opposing_team{0};
    std::atomic<std::uint64_t> damage_attacker_other_team{0};
    std::atomic<std::uint64_t> damage_attacker_self{0};
    std::atomic<std::uint64_t> damage_attacker_same_team_objective{0};
    std::array<std::atomic<std::uint64_t>, 64> damage_attacker_unit_type_counts{};
    std::atomic<std::uint64_t> damage_attacker_unit_type_overflow{0};
    std::array<std::atomic<std::uint64_t>, 64> damage_same_team_victim_unit_type_counts{};
    std::atomic<std::uint64_t> damage_same_team_victim_unit_type_overflow{0};
};

TargetProbeState g_target_probe;

struct ClassifierSpoofScope {
    void* target = nullptr;
    std::uint32_t bit = kInvalidUnitType;
    bool active = false;
};

thread_local ClassifierSpoofScope g_classifier_spoof_scope;

bool env_flag_enabled(const wchar_t* name) {
    wchar_t buffer[32] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return false;
    }
    return buffer[0] == L'1' || buffer[0] == L't' || buffer[0] == L'T' ||
        buffer[0] == L'y' || buffer[0] == L'Y';
}

std::uint32_t env_u32_or(const wchar_t* name, std::uint32_t fallback) {
    wchar_t buffer[32] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return fallback;
    }
    if (_wcsicmp(buffer, L"opposing") == 0) {
        return kInvalidTeam;
    }
    if (_wcsicmp(buffer, L"neutral") == 0) {
        return 0;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(buffer, &end, 0);
    if (end == buffer || parsed > 0xffUL) {
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint32_t env_classifier_bit_or(const wchar_t* name, std::uint32_t fallback) {
    wchar_t buffer[32] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return fallback;
    }
    if (_wcsicmp(buffer, L"enemy_hero") == 0 || _wcsicmp(buffer, L"enemy-hero") == 0) {
        return 8;
    }
    if (_wcsicmp(buffer, L"boss_enemy") == 0 || _wcsicmp(buffer, L"boss-enemy") == 0 ||
        _wcsicmp(buffer, L"midboss") == 0) {
        return 10;
    }
    if (_wcsicmp(buffer, L"neutral") == 0) {
        return 16;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(buffer, &end, 0);
    if (end == buffer || parsed > 31UL) {
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint32_t env_team_or(const wchar_t* name, std::uint32_t fallback) {
    wchar_t buffer[32] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return fallback;
    }
    if (_wcsicmp(buffer, L"neutral") == 0 || _wcsicmp(buffer, L"team4") == 0) {
        return 4;
    }
    if (_wcsicmp(buffer, L"zero") == 0 || _wcsicmp(buffer, L"team0") == 0) {
        return 0;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(buffer, &end, 0);
    if (end == buffer || parsed > 0xffUL) {
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint8_t* team_field(void* entity) {
    return reinterpret_cast<std::uint8_t*>(static_cast<std::byte*>(entity) + kCBaseEntityTeamNumOffset);
}

bool safe_read_team(void* entity, std::uint32_t& out) {
    out = kInvalidTeam;
    if (entity == nullptr) {
        return false;
    }
#if defined(_MSC_VER)
    __try {
        out = *team_field(entity);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = kInvalidTeam;
        return false;
    }
#else
    out = *team_field(entity);
    return true;
#endif
}

bool safe_write_team(void* entity, std::uint8_t team) {
    if (entity == nullptr) {
        return false;
    }
#if defined(_MSC_VER)
    __try {
        *team_field(entity) = team;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    *team_field(entity) = team;
    return true;
#endif
}

bool safe_read_damage_handle(void* info, std::size_t offset, std::uint32_t& out) {
    out = kInvalidHandle;
    if (info == nullptr) {
        return false;
    }
#if defined(_MSC_VER)
    __try {
        out = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(info) + offset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = kInvalidHandle;
        return false;
    }
#else
    out = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(info) + offset);
    return true;
#endif
}

bool resolve_entity_handle(std::uint32_t handle, void*& out) {
    out = nullptr;
    if (handle == kInvalidHandle || handle == 0xfffffffeU) {
        return false;
    }
#if defined(_MSC_VER)
    __try {
        const HMODULE server_module = GetModuleHandleW(L"server.dll");
        if (server_module == nullptr) {
            return false;
        }
        const auto table_global = reinterpret_cast<std::uintptr_t>(server_module) + kEntitySystemChunkTableRva;
        const std::uintptr_t table = *reinterpret_cast<std::uintptr_t*>(table_global);
        if (table == 0) {
            return false;
        }
        const std::uint32_t entry_index = handle & 0x7fffU;
        const std::uintptr_t chunk = *reinterpret_cast<std::uintptr_t*>(table + static_cast<std::uintptr_t>(entry_index >> 9U) * sizeof(std::uintptr_t));
        if (chunk == 0) {
            return false;
        }
        const std::uintptr_t entry = chunk + static_cast<std::uintptr_t>(entry_index & 0x1ffU) * 0x70U;
        if (*reinterpret_cast<std::uint32_t*>(entry + 0x10U) != handle) {
            return false;
        }
        out = *reinterpret_cast<void**>(entry);
        return out != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = nullptr;
        return false;
    }
#else
    return false;
#endif
}

bool safe_read_identity(void* entity, EntityIdentityLite*& out) {
    out = nullptr;
    if (entity == nullptr) {
        return false;
    }
#if defined(_MSC_VER)
    __try {
        out = *reinterpret_cast<EntityIdentityLite**>(static_cast<std::byte*>(entity) + 0x10);
        return out != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = nullptr;
        return false;
    }
#else
    out = *reinterpret_cast<EntityIdentityLite**>(static_cast<std::byte*>(entity) + 0x10);
    return out != nullptr;
#endif
}

std::uint32_t safe_unit_type(void* entity) {
    if (entity == nullptr) {
        return kInvalidUnitType;
    }
#if defined(_MSC_VER)
    __try {
        void** vtable = *static_cast<void***>(entity);
        using UnitTypeFn = int(__fastcall*)(void*);
        const auto fn = reinterpret_cast<UnitTypeFn>(vtable[0x200 / sizeof(void*)]);
        return static_cast<std::uint32_t>(fn(entity));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return kInvalidUnitType;
    }
#else
    void** vtable = *static_cast<void***>(entity);
    using UnitTypeFn = int(__fastcall*)(void*);
    const auto fn = reinterpret_cast<UnitTypeFn>(vtable[0x200 / sizeof(void*)]);
    return static_cast<std::uint32_t>(fn(entity));
#endif
}

std::uint8_t opposing_team(std::uint32_t team) {
    if (team == 2) {
        return 3;
    }
    if (team == 3) {
        return 2;
    }
    return 0;
}

bool is_objective_name(std::string_view name) {
    return name == "npc_boss_tier1" || name == "alt_npc_boss_tier1" || name == "CNPC_Boss_Tier1" ||
        name == "npc_boss_tier2" || name == "alt_npc_boss_tier2" || name == "CNPC_Boss_Tier2" ||
        name == "npc_boss_tier3" || name == "alt_npc_boss_tier3" || name == "CNPC_Boss_Tier3" ||
        name == "npc_trooper_boss" || name == "alt_npc_trooper_boss" || name == "CNPC_TrooperBoss" ||
        name == "CNPC_BarrackBoss";
}

bool has_prefix(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool is_neutral_name(std::string_view name) {
    return has_prefix(name, "npc_neutral") || has_prefix(name, "CNPC_Neutral") ||
        name == "npc_trooper_neutral" || name == "CNPC_TrooperNeutral";
}

bool is_midboss_name(std::string_view name) {
    return name == "npc_midboss" || name == "CNPC_MidBoss" || name == "midboss";
}

std::string_view designer_name_for(void* entity) {
    EntityIdentityLite* identity = nullptr;
    if (!safe_read_identity(entity, identity) || identity->designer_name == nullptr) {
        return {};
    }
    const char* name = identity->designer_name;
    std::size_t length = 0;
#if defined(_MSC_VER)
    __try {
        while (length < 128 && name[length] != '\0') {
            ++length;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return {};
    }
#else
    while (length < 128 && name[length] != '\0') {
        ++length;
    }
#endif
    return std::string_view(name, length);
}

void record_unit_type(std::uint32_t unit_type) {
    if (unit_type < g_target_probe.filter_unit_type_counts.size()) {
        g_target_probe.filter_unit_type_counts[unit_type].fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.filter_unit_type_overflow.fetch_add(1, std::memory_order_relaxed);
    }
}

void record_damage_unit_type(std::uint32_t unit_type) {
    if (unit_type < g_target_probe.damage_victim_unit_type_counts.size()) {
        g_target_probe.damage_victim_unit_type_counts[unit_type].fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.damage_victim_unit_type_overflow.fetch_add(1, std::memory_order_relaxed);
    }
}

void record_damage_attacker_unit_type(std::uint32_t unit_type) {
    if (unit_type < g_target_probe.damage_attacker_unit_type_counts.size()) {
        g_target_probe.damage_attacker_unit_type_counts[unit_type].fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.damage_attacker_unit_type_overflow.fetch_add(1, std::memory_order_relaxed);
    }
}

void record_same_team_victim_unit_type(std::uint32_t unit_type) {
    if (unit_type < g_target_probe.damage_same_team_victim_unit_type_counts.size()) {
        g_target_probe.damage_same_team_victim_unit_type_counts[unit_type].fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.damage_same_team_victim_unit_type_overflow.fetch_add(1, std::memory_order_relaxed);
    }
}

void record_global_neutralize_original_team(std::uint32_t team) {
    if (team < g_target_probe.global_neutralize_original_team_counts.size()) {
        g_target_probe.global_neutralize_original_team_counts[team].fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.global_neutralize_original_team_overflow.fetch_add(1, std::memory_order_relaxed);
    }
}

bool target_bitset_allow_enabled() {
    return g_target_probe.target_bitset_allow_enabled.load(std::memory_order_acquire) != 0 ||
        g_target_probe.neutral_simulation_enabled.load(std::memory_order_acquire) != 0;
}

bool force_filter_same_team_allow_enabled() {
    return g_target_probe.force_filter_same_team_allow_enabled.load(std::memory_order_acquire) != 0 ||
        g_target_probe.neutral_simulation_enabled.load(std::memory_order_acquire) != 0;
}

bool force_caller_same_team_allow_enabled() {
    return g_target_probe.force_caller_same_team_allow_enabled.load(std::memory_order_acquire) != 0 ||
        g_target_probe.neutral_simulation_enabled.load(std::memory_order_acquire) != 0;
}

bool force_filter_objective_allow_enabled() {
    return g_target_probe.force_filter_objective_allow_enabled.load(std::memory_order_acquire) != 0 ||
        g_target_probe.neutral_simulation_enabled.load(std::memory_order_acquire) != 0;
}

bool force_caller_objective_allow_enabled() {
    return g_target_probe.force_caller_objective_allow_enabled.load(std::memory_order_acquire) != 0 ||
        g_target_probe.neutral_simulation_enabled.load(std::memory_order_acquire) != 0;
}

bool force_secondary_allow_enabled() {
    return g_target_probe.force_secondary_allow_enabled.load(std::memory_order_acquire) != 0 ||
        g_target_probe.neutral_simulation_enabled.load(std::memory_order_acquire) != 0;
}

std::uint8_t configured_target_spoof_team(std::uint32_t source_team) {
    const std::uint32_t configured = g_target_probe.target_team_spoof_team.load(std::memory_order_acquire);
    if (configured <= 0xffU) {
        return static_cast<std::uint8_t>(configured);
    }
    return opposing_team(source_team);
}

void record_result_common(bool result, bool same_team, bool objective_target) {
    if (result) {
        g_target_probe.filter_allowed.fetch_add(1, std::memory_order_relaxed);
        if (same_team) {
            g_target_probe.filter_same_team_allowed.fetch_add(1, std::memory_order_relaxed);
        }
        if (objective_target) {
            g_target_probe.filter_objective_target_allowed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else {
        g_target_probe.filter_denied.fetch_add(1, std::memory_order_relaxed);
        if (same_team) {
            g_target_probe.filter_same_team_denied.fetch_add(1, std::memory_order_relaxed);
        }
        if (objective_target) {
            g_target_probe.filter_objective_target_denied.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

}  // namespace

void configure_target_probe_from_environment() {
    g_target_probe.enabled.store(env_flag_enabled(L"DWRT_DISABLE_TARGET_PROBE") ? 0U : 1U, std::memory_order_release);
    const bool spoof_enabled = env_flag_enabled(L"DWRT_TARGET_PROBE_SOURCE_TEAM_SPOOF") ||
        env_flag_enabled(L"DWRT_SOURCE_TEAM_SPOOF_EXPERIMENT");
    const bool target_spoof_enabled = env_flag_enabled(L"DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF");
    const bool target_bitset_allow_enabled = env_flag_enabled(L"DWRT_TARGET_PROBE_ALLOW_TARGET_BITSET");
    const bool neutral_simulation_enabled = env_flag_enabled(L"DWRT_TARGET_PROBE_NEUTRAL_SIMULATION");
    const bool classifier_spoof_enabled = env_flag_enabled(L"DWRT_TARGET_PROBE_CLASSIFIER_SPOOF");
    const bool global_neutralize_enabled = env_flag_enabled(L"DWRT_TARGET_PROBE_GLOBAL_NEUTRALIZE");
    g_target_probe.source_team_spoof_enabled.store(spoof_enabled ? 1U : 0U, std::memory_order_release);
    g_target_probe.target_team_spoof_enabled.store(target_spoof_enabled ? 1U : 0U, std::memory_order_release);
    g_target_probe.target_bitset_allow_enabled.store(target_bitset_allow_enabled ? 1U : 0U, std::memory_order_release);
    g_target_probe.target_team_spoof_team.store(
        env_u32_or(L"DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF_TEAM", kInvalidTeam),
        std::memory_order_release);
    g_target_probe.force_filter_same_team_allow_enabled.store(
        env_flag_enabled(L"DWRT_TARGET_PROBE_FORCE_FILTER_SAME_TEAM_ALLOW") ? 1U : 0U,
        std::memory_order_release);
    g_target_probe.force_caller_same_team_allow_enabled.store(
        env_flag_enabled(L"DWRT_TARGET_PROBE_FORCE_CALLER_SAME_TEAM_ALLOW") ? 1U : 0U,
        std::memory_order_release);
    g_target_probe.force_filter_objective_allow_enabled.store(
        env_flag_enabled(L"DWRT_TARGET_PROBE_FORCE_FILTER_OBJECTIVE_ALLOW") ? 1U : 0U,
        std::memory_order_release);
    g_target_probe.force_caller_objective_allow_enabled.store(
        env_flag_enabled(L"DWRT_TARGET_PROBE_FORCE_CALLER_OBJECTIVE_ALLOW") ? 1U : 0U,
        std::memory_order_release);
    g_target_probe.force_secondary_allow_enabled.store(
        env_flag_enabled(L"DWRT_TARGET_PROBE_FORCE_SECONDARY_ALLOW") ? 1U : 0U,
        std::memory_order_release);
    g_target_probe.neutral_simulation_enabled.store(neutral_simulation_enabled ? 1U : 0U, std::memory_order_release);
    g_target_probe.classifier_spoof_enabled.store(classifier_spoof_enabled ? 1U : 0U, std::memory_order_release);
    g_target_probe.classifier_spoof_bit.store(
        env_classifier_bit_or(L"DWRT_TARGET_PROBE_CLASSIFIER_SPOOF_BIT", 8),
        std::memory_order_release);
    g_target_probe.global_neutralize_enabled.store(global_neutralize_enabled ? 1U : 0U, std::memory_order_release);
    g_target_probe.global_neutralize_team.store(
        env_team_or(L"DWRT_TARGET_PROBE_GLOBAL_NEUTRAL_TEAM", 4),
        std::memory_order_release);
}

void reset_target_probe_counters() {
    g_target_probe.last_source_team.store(kInvalidTeam, std::memory_order_relaxed);
    g_target_probe.last_target_team.store(kInvalidTeam, std::memory_order_relaxed);
    g_target_probe.last_target_unit_type.store(kInvalidUnitType, std::memory_order_relaxed);
    g_target_probe.last_filter_result.store(0, std::memory_order_relaxed);
    g_target_probe.last_damage_victim_team.store(kInvalidTeam, std::memory_order_relaxed);
    g_target_probe.last_damage_victim_unit_type.store(kInvalidUnitType, std::memory_order_relaxed);
    g_target_probe.last_damage_attacker_handle.store(kInvalidHandle, std::memory_order_relaxed);
    g_target_probe.last_damage_inflictor_handle.store(kInvalidHandle, std::memory_order_relaxed);
    g_target_probe.last_damage_attacker_team.store(kInvalidTeam, std::memory_order_relaxed);
    g_target_probe.last_damage_attacker_unit_type.store(kInvalidUnitType, std::memory_order_relaxed);
    g_target_probe.filter_calls.store(0, std::memory_order_relaxed);
    g_target_probe.filter_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.filter_denied.store(0, std::memory_order_relaxed);
    g_target_probe.filter_same_team_calls.store(0, std::memory_order_relaxed);
    g_target_probe.filter_same_team_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.filter_same_team_denied.store(0, std::memory_order_relaxed);
    g_target_probe.filter_objective_target_calls.store(0, std::memory_order_relaxed);
    g_target_probe.filter_objective_target_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.filter_objective_target_denied.store(0, std::memory_order_relaxed);
    g_target_probe.filter_neutral_target_calls.store(0, std::memory_order_relaxed);
    g_target_probe.filter_midboss_target_calls.store(0, std::memory_order_relaxed);
    g_target_probe.filter_invalid_source_team.store(0, std::memory_order_relaxed);
    g_target_probe.filter_invalid_target_team.store(0, std::memory_order_relaxed);
    g_target_probe.source_spoof_attempts.store(0, std::memory_order_relaxed);
    g_target_probe.source_spoof_applied.store(0, std::memory_order_relaxed);
    g_target_probe.source_spoof_restored.store(0, std::memory_order_relaxed);
    g_target_probe.source_spoof_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.source_spoof_denied.store(0, std::memory_order_relaxed);
    g_target_probe.target_spoof_attempts.store(0, std::memory_order_relaxed);
    g_target_probe.target_spoof_applied.store(0, std::memory_order_relaxed);
    g_target_probe.target_spoof_restored.store(0, std::memory_order_relaxed);
    g_target_probe.target_spoof_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.target_spoof_denied.store(0, std::memory_order_relaxed);
    g_target_probe.bitset_allow_attempts.store(0, std::memory_order_relaxed);
    g_target_probe.bitset_allow_applied.store(0, std::memory_order_relaxed);
    g_target_probe.bitset_allow_failures.store(0, std::memory_order_relaxed);
    g_target_probe.classifier_calls.store(0, std::memory_order_relaxed);
    g_target_probe.classifier_invalid.store(0, std::memory_order_relaxed);
    g_target_probe.classifier_spoof_attempts.store(0, std::memory_order_relaxed);
    g_target_probe.classifier_spoof_applied.store(0, std::memory_order_relaxed);
    g_target_probe.classifier_spoof_skipped.store(0, std::memory_order_relaxed);
    g_target_probe.last_classifier_original_bit.store(kInvalidUnitType, std::memory_order_relaxed);
    g_target_probe.last_classifier_final_bit.store(kInvalidUnitType, std::memory_order_relaxed);
    g_target_probe.global_neutralize_attempts.store(0, std::memory_order_relaxed);
    g_target_probe.global_neutralize_applied.store(0, std::memory_order_relaxed);
    g_target_probe.global_neutralize_already.store(0, std::memory_order_relaxed);
    g_target_probe.global_neutralize_null.store(0, std::memory_order_relaxed);
    g_target_probe.global_neutralize_invalid_team.store(0, std::memory_order_relaxed);
    g_target_probe.global_neutralize_write_failures.store(0, std::memory_order_relaxed);
    for (std::atomic<std::uint64_t>& count : g_target_probe.global_neutralize_original_team_counts) {
        count.store(0, std::memory_order_relaxed);
    }
    g_target_probe.global_neutralize_original_team_overflow.store(0, std::memory_order_relaxed);
    g_target_probe.filter_forced_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.caller_forced_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.secondary_forced_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.secondary_calls.store(0, std::memory_order_relaxed);
    g_target_probe.secondary_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.secondary_denied.store(0, std::memory_order_relaxed);
    g_target_probe.caller_calls.store(0, std::memory_order_relaxed);
    g_target_probe.caller_allowed.store(0, std::memory_order_relaxed);
    g_target_probe.caller_denied.store(0, std::memory_order_relaxed);
    g_target_probe.caller_unit_0x1a_bypass_candidates.store(0, std::memory_order_relaxed);
    for (std::atomic<std::uint64_t>& count : g_target_probe.filter_unit_type_counts) {
        count.store(0, std::memory_order_relaxed);
    }
    g_target_probe.filter_unit_type_overflow.store(0, std::memory_order_relaxed);
    g_target_probe.damage_victim_calls.store(0, std::memory_order_relaxed);
    g_target_probe.damage_victim_objective.store(0, std::memory_order_relaxed);
    g_target_probe.damage_victim_neutral.store(0, std::memory_order_relaxed);
    g_target_probe.damage_victim_midboss.store(0, std::memory_order_relaxed);
    g_target_probe.damage_victim_invalid_team.store(0, std::memory_order_relaxed);
    for (std::atomic<std::uint64_t>& count : g_target_probe.damage_victim_unit_type_counts) {
        count.store(0, std::memory_order_relaxed);
    }
    g_target_probe.damage_victim_unit_type_overflow.store(0, std::memory_order_relaxed);
    g_target_probe.damage_attacker_handle_valid.store(0, std::memory_order_relaxed);
    g_target_probe.damage_attacker_handle_invalid.store(0, std::memory_order_relaxed);
    g_target_probe.damage_inflictor_handle_valid.store(0, std::memory_order_relaxed);
    g_target_probe.damage_inflictor_handle_invalid.store(0, std::memory_order_relaxed);
    g_target_probe.damage_attacker_same_team.store(0, std::memory_order_relaxed);
    g_target_probe.damage_attacker_opposing_team.store(0, std::memory_order_relaxed);
    g_target_probe.damage_attacker_other_team.store(0, std::memory_order_relaxed);
    g_target_probe.damage_attacker_self.store(0, std::memory_order_relaxed);
    g_target_probe.damage_attacker_same_team_objective.store(0, std::memory_order_relaxed);
    for (std::atomic<std::uint64_t>& count : g_target_probe.damage_attacker_unit_type_counts) {
        count.store(0, std::memory_order_relaxed);
    }
    g_target_probe.damage_attacker_unit_type_overflow.store(0, std::memory_order_relaxed);
    for (std::atomic<std::uint64_t>& count : g_target_probe.damage_same_team_victim_unit_type_counts) {
        count.store(0, std::memory_order_relaxed);
    }
    g_target_probe.damage_same_team_victim_unit_type_overflow.store(0, std::memory_order_relaxed);
}

void maybe_global_neutralize_entity(void* entity) {
    if (g_target_probe.enabled.load(std::memory_order_acquire) == 0 ||
        g_target_probe.global_neutralize_enabled.load(std::memory_order_acquire) == 0) {
        return;
    }

    g_target_probe.global_neutralize_attempts.fetch_add(1, std::memory_order_relaxed);
    if (entity == nullptr) {
        g_target_probe.global_neutralize_null.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::uint32_t original_team = kInvalidTeam;
    if (!safe_read_team(entity, original_team)) {
        g_target_probe.global_neutralize_invalid_team.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    record_global_neutralize_original_team(original_team);

    const std::uint32_t neutral_team = g_target_probe.global_neutralize_team.load(std::memory_order_acquire);
    if (original_team == neutral_team) {
        g_target_probe.global_neutralize_already.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (neutral_team > 0xffU) {
        g_target_probe.global_neutralize_write_failures.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (safe_write_team(entity, static_cast<std::uint8_t>(neutral_team))) {
        g_target_probe.global_neutralize_applied.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.global_neutralize_write_failures.fetch_add(1, std::memory_order_relaxed);
    }
}

TargetFilterScope begin_target_filter(void* source, void* target, bool recursive) {
    TargetFilterScope scope{};
    if (g_target_probe.enabled.load(std::memory_order_acquire) == 0 || recursive) {
        return scope;
    }

    maybe_global_neutralize_entity(source);
    maybe_global_neutralize_entity(target);
    g_target_probe.filter_calls.fetch_add(1, std::memory_order_relaxed);
    std::uint32_t source_team = kInvalidTeam;
    std::uint32_t target_team = kInvalidTeam;
    if (!safe_read_team(source, source_team)) {
        g_target_probe.filter_invalid_source_team.fetch_add(1, std::memory_order_relaxed);
    }
    if (!safe_read_team(target, target_team)) {
        g_target_probe.filter_invalid_target_team.fetch_add(1, std::memory_order_relaxed);
    }
    const std::uint32_t unit_type = safe_unit_type(target);
    const std::string_view target_name = designer_name_for(target);
    const bool objective_target = is_objective_name(target_name);
    const bool neutral_target = is_neutral_name(target_name);
    const bool midboss_target = is_midboss_name(target_name);
    const bool same_team = source_team != kInvalidTeam && target_team != kInvalidTeam && source_team == target_team;

    record_unit_type(unit_type);
    g_target_probe.last_source_team.store(source_team, std::memory_order_relaxed);
    g_target_probe.last_target_team.store(target_team, std::memory_order_relaxed);
    g_target_probe.last_target_unit_type.store(unit_type, std::memory_order_relaxed);
    if (same_team) {
        g_target_probe.filter_same_team_calls.fetch_add(1, std::memory_order_relaxed);
    }
    if (objective_target) {
        g_target_probe.filter_objective_target_calls.fetch_add(1, std::memory_order_relaxed);
    }
    if (neutral_target) {
        g_target_probe.filter_neutral_target_calls.fetch_add(1, std::memory_order_relaxed);
    }
    if (midboss_target) {
        g_target_probe.filter_midboss_target_calls.fetch_add(1, std::memory_order_relaxed);
    }

    scope.source = source;
    scope.target = target;
    scope.original_source_team = source_team <= 0xff ? static_cast<std::uint8_t>(source_team) : 0;
    scope.original_target_team = target_team <= 0xff ? static_cast<std::uint8_t>(target_team) : 0;
    scope.same_team = same_team ? 1 : 0;
    scope.target_objective = objective_target ? 1 : 0;

    if (g_target_probe.source_team_spoof_enabled.load(std::memory_order_acquire) != 0 && same_team && source != target) {
        const std::uint8_t spoofed_team = opposing_team(target_team);
        if (spoofed_team != 0) {
            g_target_probe.source_spoof_attempts.fetch_add(1, std::memory_order_relaxed);
            if (safe_write_team(source, spoofed_team)) {
                scope.source_spoof_applied = 1;
                g_target_probe.source_spoof_applied.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (g_target_probe.target_team_spoof_enabled.load(std::memory_order_acquire) != 0 && same_team && source != target) {
        const std::uint8_t spoofed_team = configured_target_spoof_team(source_team);
        g_target_probe.target_spoof_attempts.fetch_add(1, std::memory_order_relaxed);
        if (safe_write_team(target, spoofed_team)) {
            scope.target_spoof_applied = 1;
            g_target_probe.target_spoof_applied.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return scope;
}

bool maybe_allow_target_bitset(
    const TargetFilterScope& scope,
    void* target,
    void* bitset,
    TargetIdentityClassifierFn classifier) {
    if (g_target_probe.enabled.load(std::memory_order_acquire) == 0 ||
        !target_bitset_allow_enabled() ||
        (scope.same_team == 0 && scope.target_objective == 0)) {
        return false;
    }

    g_target_probe.bitset_allow_attempts.fetch_add(1, std::memory_order_relaxed);
    if (target == nullptr || bitset == nullptr || classifier == nullptr) {
        g_target_probe.bitset_allow_failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    std::uint32_t bit_index = kInvalidUnitType;
#if defined(_MSC_VER)
    __try {
        classifier(target, &bit_index);
        if (bit_index == kInvalidUnitType || bit_index >= 0x40000U) {
            g_target_probe.bitset_allow_failures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        auto* words = static_cast<std::uint32_t*>(bitset);
        words[bit_index >> 5] |= 1U << (bit_index & 0x1fU);
        g_target_probe.bitset_allow_applied.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_target_probe.bitset_allow_failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#else
    classifier(target, &bit_index);
    if (bit_index == kInvalidUnitType || bit_index >= 0x40000U) {
        g_target_probe.bitset_allow_failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    auto* words = static_cast<std::uint32_t*>(bitset);
    words[bit_index >> 5] |= 1U << (bit_index & 0x1fU);
    g_target_probe.bitset_allow_applied.fetch_add(1, std::memory_order_relaxed);
    return true;
#endif
}

void begin_target_classifier_spoof_scope(const TargetFilterScope& scope) {
    g_classifier_spoof_scope = {};
    if (g_target_probe.enabled.load(std::memory_order_acquire) == 0 ||
        g_target_probe.classifier_spoof_enabled.load(std::memory_order_acquire) == 0 ||
        scope.target == nullptr ||
        (scope.same_team == 0 && scope.target_objective == 0)) {
        return;
    }

    const std::uint32_t bit = g_target_probe.classifier_spoof_bit.load(std::memory_order_acquire);
    if (bit > 31U) {
        return;
    }

    g_classifier_spoof_scope.target = scope.target;
    g_classifier_spoof_scope.bit = bit;
    g_classifier_spoof_scope.active = true;
}

void end_target_classifier_spoof_scope() {
    g_classifier_spoof_scope = {};
}

void finish_target_identity_classifier(void* target, std::uint32_t* out) {
    if (g_target_probe.enabled.load(std::memory_order_acquire) == 0) {
        return;
    }

    maybe_global_neutralize_entity(target);
    g_target_probe.classifier_calls.fetch_add(1, std::memory_order_relaxed);
    if (out == nullptr) {
        g_target_probe.classifier_invalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const std::uint32_t original_bit = *out;
    std::uint32_t final_bit = original_bit;
    if (original_bit == kInvalidUnitType) {
        g_target_probe.classifier_invalid.fetch_add(1, std::memory_order_relaxed);
    }

    if (g_classifier_spoof_scope.active && target == g_classifier_spoof_scope.target) {
        g_target_probe.classifier_spoof_attempts.fetch_add(1, std::memory_order_relaxed);
        const std::uint32_t spoof_bit = g_classifier_spoof_scope.bit;
        if (spoof_bit <= 31U) {
#if defined(_MSC_VER)
            __try {
                *out = spoof_bit;
                final_bit = spoof_bit;
                g_target_probe.classifier_spoof_applied.fetch_add(1, std::memory_order_relaxed);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                g_target_probe.classifier_spoof_skipped.fetch_add(1, std::memory_order_relaxed);
            }
#else
            *out = spoof_bit;
            final_bit = spoof_bit;
            g_target_probe.classifier_spoof_applied.fetch_add(1, std::memory_order_relaxed);
#endif
        }
        else {
            g_target_probe.classifier_spoof_skipped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    g_target_probe.last_classifier_original_bit.store(original_bit, std::memory_order_relaxed);
    g_target_probe.last_classifier_final_bit.store(final_bit, std::memory_order_relaxed);
}

void record_damage_victim(void* victim, void* info, bool recursive) {
    if (g_target_probe.enabled.load(std::memory_order_acquire) == 0 || recursive) {
        return;
    }

    maybe_global_neutralize_entity(victim);
    g_target_probe.damage_victim_calls.fetch_add(1, std::memory_order_relaxed);
    std::uint32_t victim_team = kInvalidTeam;
    if (!safe_read_team(victim, victim_team)) {
        g_target_probe.damage_victim_invalid_team.fetch_add(1, std::memory_order_relaxed);
    }
    const std::uint32_t victim_unit_type = safe_unit_type(victim);
    const std::string_view target_name = designer_name_for(victim);
    const bool objective_victim = is_objective_name(target_name);

    g_target_probe.last_damage_victim_team.store(victim_team, std::memory_order_relaxed);
    g_target_probe.last_damage_victim_unit_type.store(victim_unit_type, std::memory_order_relaxed);
    record_damage_unit_type(victim_unit_type);

    if (objective_victim) {
        g_target_probe.damage_victim_objective.fetch_add(1, std::memory_order_relaxed);
    }
    if (is_neutral_name(target_name)) {
        g_target_probe.damage_victim_neutral.fetch_add(1, std::memory_order_relaxed);
    }
    if (is_midboss_name(target_name)) {
        g_target_probe.damage_victim_midboss.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint32_t attacker_handle = kInvalidHandle;
    std::uint32_t inflictor_handle = kInvalidHandle;
    safe_read_damage_handle(info, kTakeDamageInfoAttackerHandleOffset, attacker_handle);
    safe_read_damage_handle(info, kTakeDamageInfoInflictorHandleOffset, inflictor_handle);
    g_target_probe.last_damage_attacker_handle.store(attacker_handle, std::memory_order_relaxed);
    g_target_probe.last_damage_inflictor_handle.store(inflictor_handle, std::memory_order_relaxed);

    void* inflictor = nullptr;
    if (resolve_entity_handle(inflictor_handle, inflictor)) {
        g_target_probe.damage_inflictor_handle_valid.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.damage_inflictor_handle_invalid.fetch_add(1, std::memory_order_relaxed);
    }

    void* attacker = nullptr;
    if (!resolve_entity_handle(attacker_handle, attacker)) {
        g_target_probe.damage_attacker_handle_invalid.fetch_add(1, std::memory_order_relaxed);
        g_target_probe.last_damage_attacker_team.store(kInvalidTeam, std::memory_order_relaxed);
        g_target_probe.last_damage_attacker_unit_type.store(kInvalidUnitType, std::memory_order_relaxed);
        return;
    }

    g_target_probe.damage_attacker_handle_valid.fetch_add(1, std::memory_order_relaxed);
    std::uint32_t attacker_team = kInvalidTeam;
    safe_read_team(attacker, attacker_team);
    const std::uint32_t attacker_unit_type = safe_unit_type(attacker);
    g_target_probe.last_damage_attacker_team.store(attacker_team, std::memory_order_relaxed);
    g_target_probe.last_damage_attacker_unit_type.store(attacker_unit_type, std::memory_order_relaxed);
    record_damage_attacker_unit_type(attacker_unit_type);

    if (attacker == victim) {
        g_target_probe.damage_attacker_self.fetch_add(1, std::memory_order_relaxed);
    }
    if (attacker_team == kInvalidTeam || victim_team == kInvalidTeam) {
        g_target_probe.damage_attacker_other_team.fetch_add(1, std::memory_order_relaxed);
    }
    else if (attacker_team == victim_team) {
        g_target_probe.damage_attacker_same_team.fetch_add(1, std::memory_order_relaxed);
        record_same_team_victim_unit_type(victim_unit_type);
        if (objective_victim) {
            g_target_probe.damage_attacker_same_team_objective.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else if ((attacker_team == 2U && victim_team == 3U) || (attacker_team == 3U && victim_team == 2U)) {
        g_target_probe.damage_attacker_opposing_team.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.damage_attacker_other_team.fetch_add(1, std::memory_order_relaxed);
    }
}

bool finish_target_filter(const TargetFilterScope& scope, void*, void*, bool result) {
    bool final_result = result;
    if (!final_result &&
        ((scope.same_team != 0 && force_filter_same_team_allow_enabled()) ||
         (scope.target_objective != 0 && force_filter_objective_allow_enabled()))) {
        final_result = true;
        g_target_probe.filter_forced_allowed.fetch_add(1, std::memory_order_relaxed);
    }

    g_target_probe.last_filter_result.store(final_result ? 1U : 0U, std::memory_order_relaxed);
    record_result_common(final_result, scope.same_team != 0, scope.target_objective != 0);
    if (scope.source_spoof_applied != 0 && scope.source != nullptr) {
        if (final_result) {
            g_target_probe.source_spoof_allowed.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            g_target_probe.source_spoof_denied.fetch_add(1, std::memory_order_relaxed);
        }
        if (safe_write_team(scope.source, scope.original_source_team)) {
            g_target_probe.source_spoof_restored.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (scope.target_spoof_applied != 0 && scope.target != nullptr) {
        if (final_result) {
            g_target_probe.target_spoof_allowed.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            g_target_probe.target_spoof_denied.fetch_add(1, std::memory_order_relaxed);
        }
        if (safe_write_team(scope.target, scope.original_target_team)) {
            g_target_probe.target_spoof_restored.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return final_result;
}

bool record_target_filter_caller(void* source, void* target, bool result) {
    if (g_target_probe.enabled.load(std::memory_order_acquire) == 0) {
        return result;
    }

    maybe_global_neutralize_entity(source);
    maybe_global_neutralize_entity(target);
    std::uint32_t source_team = kInvalidTeam;
    std::uint32_t target_team = kInvalidTeam;
    const bool source_team_ok = safe_read_team(source, source_team);
    const bool target_team_ok = safe_read_team(target, target_team);
    const bool same_team = source_team_ok && target_team_ok && source_team == target_team;
    const std::string_view target_name = designer_name_for(target);
    const bool objective_target = is_objective_name(target_name);

    bool final_result = result;
    if (!final_result &&
        ((same_team && force_caller_same_team_allow_enabled()) ||
         (objective_target && force_caller_objective_allow_enabled()))) {
        final_result = true;
        g_target_probe.caller_forced_allowed.fetch_add(1, std::memory_order_relaxed);
    }

    g_target_probe.caller_calls.fetch_add(1, std::memory_order_relaxed);
    if (final_result) {
        g_target_probe.caller_allowed.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.caller_denied.fetch_add(1, std::memory_order_relaxed);
    }
    const std::uint32_t unit_type = safe_unit_type(target);
    if (unit_type == 0x1a) {
        g_target_probe.caller_unit_0x1a_bypass_candidates.fetch_add(1, std::memory_order_relaxed);
    }
    return final_result;
}

bool record_secondary_target_gate(void* source, void* maybe_attacker, void* maybe_target, bool result) {
    if (g_target_probe.enabled.load(std::memory_order_acquire) == 0) {
        return result;
    }

    maybe_global_neutralize_entity(source);
    maybe_global_neutralize_entity(maybe_attacker);
    maybe_global_neutralize_entity(maybe_target);
    bool final_result = result;
    if (!final_result && force_secondary_allow_enabled()) {
        final_result = true;
        g_target_probe.secondary_forced_allowed.fetch_add(1, std::memory_order_relaxed);
    }

    g_target_probe.secondary_calls.fetch_add(1, std::memory_order_relaxed);
    if (final_result) {
        g_target_probe.secondary_allowed.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        g_target_probe.secondary_denied.fetch_add(1, std::memory_order_relaxed);
    }
    return final_result;
}

DwrtTargetProbeSnapshot target_probe_snapshot() {
    DwrtTargetProbeSnapshot snapshot{};
    snapshot.enabled = g_target_probe.enabled.load(std::memory_order_acquire);
    snapshot.source_team_spoof_enabled = g_target_probe.source_team_spoof_enabled.load(std::memory_order_acquire);
    snapshot.target_team_spoof_enabled = g_target_probe.target_team_spoof_enabled.load(std::memory_order_acquire);
    snapshot.target_team_spoof_team = g_target_probe.target_team_spoof_team.load(std::memory_order_acquire);
    snapshot.target_bitset_allow_enabled = g_target_probe.target_bitset_allow_enabled.load(std::memory_order_acquire);
    snapshot.force_filter_same_team_allow_enabled = g_target_probe.force_filter_same_team_allow_enabled.load(std::memory_order_acquire);
    snapshot.force_caller_same_team_allow_enabled = g_target_probe.force_caller_same_team_allow_enabled.load(std::memory_order_acquire);
    snapshot.force_filter_objective_allow_enabled = g_target_probe.force_filter_objective_allow_enabled.load(std::memory_order_acquire);
    snapshot.force_caller_objective_allow_enabled = g_target_probe.force_caller_objective_allow_enabled.load(std::memory_order_acquire);
    snapshot.force_secondary_allow_enabled = g_target_probe.force_secondary_allow_enabled.load(std::memory_order_acquire);
    snapshot.neutral_simulation_enabled = g_target_probe.neutral_simulation_enabled.load(std::memory_order_acquire);
    snapshot.classifier_spoof_enabled = g_target_probe.classifier_spoof_enabled.load(std::memory_order_acquire);
    snapshot.classifier_spoof_bit = g_target_probe.classifier_spoof_bit.load(std::memory_order_acquire);
    snapshot.global_neutralize_enabled = g_target_probe.global_neutralize_enabled.load(std::memory_order_acquire);
    snapshot.global_neutralize_team = g_target_probe.global_neutralize_team.load(std::memory_order_acquire);
    snapshot.last_source_team = g_target_probe.last_source_team.load(std::memory_order_relaxed);
    snapshot.last_target_team = g_target_probe.last_target_team.load(std::memory_order_relaxed);
    snapshot.last_target_unit_type = g_target_probe.last_target_unit_type.load(std::memory_order_relaxed);
    snapshot.last_filter_result = g_target_probe.last_filter_result.load(std::memory_order_relaxed);
    snapshot.last_damage_victim_team = g_target_probe.last_damage_victim_team.load(std::memory_order_relaxed);
    snapshot.last_damage_victim_unit_type = g_target_probe.last_damage_victim_unit_type.load(std::memory_order_relaxed);
    snapshot.last_damage_attacker_handle = g_target_probe.last_damage_attacker_handle.load(std::memory_order_relaxed);
    snapshot.last_damage_inflictor_handle = g_target_probe.last_damage_inflictor_handle.load(std::memory_order_relaxed);
    snapshot.last_damage_attacker_team = g_target_probe.last_damage_attacker_team.load(std::memory_order_relaxed);
    snapshot.last_damage_attacker_unit_type = g_target_probe.last_damage_attacker_unit_type.load(std::memory_order_relaxed);
    snapshot.filter_calls = g_target_probe.filter_calls.load(std::memory_order_relaxed);
    snapshot.filter_allowed = g_target_probe.filter_allowed.load(std::memory_order_relaxed);
    snapshot.filter_denied = g_target_probe.filter_denied.load(std::memory_order_relaxed);
    snapshot.filter_same_team_calls = g_target_probe.filter_same_team_calls.load(std::memory_order_relaxed);
    snapshot.filter_same_team_allowed = g_target_probe.filter_same_team_allowed.load(std::memory_order_relaxed);
    snapshot.filter_same_team_denied = g_target_probe.filter_same_team_denied.load(std::memory_order_relaxed);
    snapshot.filter_objective_target_calls = g_target_probe.filter_objective_target_calls.load(std::memory_order_relaxed);
    snapshot.filter_objective_target_allowed = g_target_probe.filter_objective_target_allowed.load(std::memory_order_relaxed);
    snapshot.filter_objective_target_denied = g_target_probe.filter_objective_target_denied.load(std::memory_order_relaxed);
    snapshot.filter_neutral_target_calls = g_target_probe.filter_neutral_target_calls.load(std::memory_order_relaxed);
    snapshot.filter_midboss_target_calls = g_target_probe.filter_midboss_target_calls.load(std::memory_order_relaxed);
    snapshot.filter_invalid_source_team = g_target_probe.filter_invalid_source_team.load(std::memory_order_relaxed);
    snapshot.filter_invalid_target_team = g_target_probe.filter_invalid_target_team.load(std::memory_order_relaxed);
    snapshot.source_spoof_attempts = g_target_probe.source_spoof_attempts.load(std::memory_order_relaxed);
    snapshot.source_spoof_applied = g_target_probe.source_spoof_applied.load(std::memory_order_relaxed);
    snapshot.source_spoof_restored = g_target_probe.source_spoof_restored.load(std::memory_order_relaxed);
    snapshot.source_spoof_allowed = g_target_probe.source_spoof_allowed.load(std::memory_order_relaxed);
    snapshot.source_spoof_denied = g_target_probe.source_spoof_denied.load(std::memory_order_relaxed);
    snapshot.target_spoof_attempts = g_target_probe.target_spoof_attempts.load(std::memory_order_relaxed);
    snapshot.target_spoof_applied = g_target_probe.target_spoof_applied.load(std::memory_order_relaxed);
    snapshot.target_spoof_restored = g_target_probe.target_spoof_restored.load(std::memory_order_relaxed);
    snapshot.target_spoof_allowed = g_target_probe.target_spoof_allowed.load(std::memory_order_relaxed);
    snapshot.target_spoof_denied = g_target_probe.target_spoof_denied.load(std::memory_order_relaxed);
    snapshot.bitset_allow_attempts = g_target_probe.bitset_allow_attempts.load(std::memory_order_relaxed);
    snapshot.bitset_allow_applied = g_target_probe.bitset_allow_applied.load(std::memory_order_relaxed);
    snapshot.bitset_allow_failures = g_target_probe.bitset_allow_failures.load(std::memory_order_relaxed);
    snapshot.classifier_calls = g_target_probe.classifier_calls.load(std::memory_order_relaxed);
    snapshot.classifier_invalid = g_target_probe.classifier_invalid.load(std::memory_order_relaxed);
    snapshot.classifier_spoof_attempts = g_target_probe.classifier_spoof_attempts.load(std::memory_order_relaxed);
    snapshot.classifier_spoof_applied = g_target_probe.classifier_spoof_applied.load(std::memory_order_relaxed);
    snapshot.classifier_spoof_skipped = g_target_probe.classifier_spoof_skipped.load(std::memory_order_relaxed);
    snapshot.last_classifier_original_bit = g_target_probe.last_classifier_original_bit.load(std::memory_order_relaxed);
    snapshot.last_classifier_final_bit = g_target_probe.last_classifier_final_bit.load(std::memory_order_relaxed);
    snapshot.global_neutralize_attempts = g_target_probe.global_neutralize_attempts.load(std::memory_order_relaxed);
    snapshot.global_neutralize_applied = g_target_probe.global_neutralize_applied.load(std::memory_order_relaxed);
    snapshot.global_neutralize_already = g_target_probe.global_neutralize_already.load(std::memory_order_relaxed);
    snapshot.global_neutralize_null = g_target_probe.global_neutralize_null.load(std::memory_order_relaxed);
    snapshot.global_neutralize_invalid_team = g_target_probe.global_neutralize_invalid_team.load(std::memory_order_relaxed);
    snapshot.global_neutralize_write_failures =
        g_target_probe.global_neutralize_write_failures.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < g_target_probe.global_neutralize_original_team_counts.size(); ++index) {
        snapshot.global_neutralize_original_team_counts[index] =
            g_target_probe.global_neutralize_original_team_counts[index].load(std::memory_order_relaxed);
    }
    snapshot.global_neutralize_original_team_overflow =
        g_target_probe.global_neutralize_original_team_overflow.load(std::memory_order_relaxed);
    snapshot.filter_forced_allowed = g_target_probe.filter_forced_allowed.load(std::memory_order_relaxed);
    snapshot.caller_forced_allowed = g_target_probe.caller_forced_allowed.load(std::memory_order_relaxed);
    snapshot.secondary_forced_allowed = g_target_probe.secondary_forced_allowed.load(std::memory_order_relaxed);
    snapshot.secondary_calls = g_target_probe.secondary_calls.load(std::memory_order_relaxed);
    snapshot.secondary_allowed = g_target_probe.secondary_allowed.load(std::memory_order_relaxed);
    snapshot.secondary_denied = g_target_probe.secondary_denied.load(std::memory_order_relaxed);
    snapshot.caller_calls = g_target_probe.caller_calls.load(std::memory_order_relaxed);
    snapshot.caller_allowed = g_target_probe.caller_allowed.load(std::memory_order_relaxed);
    snapshot.caller_denied = g_target_probe.caller_denied.load(std::memory_order_relaxed);
    snapshot.caller_unit_0x1a_bypass_candidates = g_target_probe.caller_unit_0x1a_bypass_candidates.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < g_target_probe.filter_unit_type_counts.size(); ++index) {
        snapshot.filter_unit_type_counts[index] =
            g_target_probe.filter_unit_type_counts[index].load(std::memory_order_relaxed);
    }
    snapshot.filter_unit_type_overflow = g_target_probe.filter_unit_type_overflow.load(std::memory_order_relaxed);
    snapshot.damage_victim_calls = g_target_probe.damage_victim_calls.load(std::memory_order_relaxed);
    snapshot.damage_victim_objective = g_target_probe.damage_victim_objective.load(std::memory_order_relaxed);
    snapshot.damage_victim_neutral = g_target_probe.damage_victim_neutral.load(std::memory_order_relaxed);
    snapshot.damage_victim_midboss = g_target_probe.damage_victim_midboss.load(std::memory_order_relaxed);
    snapshot.damage_victim_invalid_team = g_target_probe.damage_victim_invalid_team.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < g_target_probe.damage_victim_unit_type_counts.size(); ++index) {
        snapshot.damage_victim_unit_type_counts[index] =
            g_target_probe.damage_victim_unit_type_counts[index].load(std::memory_order_relaxed);
    }
    snapshot.damage_victim_unit_type_overflow =
        g_target_probe.damage_victim_unit_type_overflow.load(std::memory_order_relaxed);
    snapshot.damage_attacker_handle_valid = g_target_probe.damage_attacker_handle_valid.load(std::memory_order_relaxed);
    snapshot.damage_attacker_handle_invalid = g_target_probe.damage_attacker_handle_invalid.load(std::memory_order_relaxed);
    snapshot.damage_inflictor_handle_valid = g_target_probe.damage_inflictor_handle_valid.load(std::memory_order_relaxed);
    snapshot.damage_inflictor_handle_invalid = g_target_probe.damage_inflictor_handle_invalid.load(std::memory_order_relaxed);
    snapshot.damage_attacker_same_team = g_target_probe.damage_attacker_same_team.load(std::memory_order_relaxed);
    snapshot.damage_attacker_opposing_team = g_target_probe.damage_attacker_opposing_team.load(std::memory_order_relaxed);
    snapshot.damage_attacker_other_team = g_target_probe.damage_attacker_other_team.load(std::memory_order_relaxed);
    snapshot.damage_attacker_self = g_target_probe.damage_attacker_self.load(std::memory_order_relaxed);
    snapshot.damage_attacker_same_team_objective =
        g_target_probe.damage_attacker_same_team_objective.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < g_target_probe.damage_attacker_unit_type_counts.size(); ++index) {
        snapshot.damage_attacker_unit_type_counts[index] =
            g_target_probe.damage_attacker_unit_type_counts[index].load(std::memory_order_relaxed);
    }
    snapshot.damage_attacker_unit_type_overflow =
        g_target_probe.damage_attacker_unit_type_overflow.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < g_target_probe.damage_same_team_victim_unit_type_counts.size(); ++index) {
        snapshot.damage_same_team_victim_unit_type_counts[index] =
            g_target_probe.damage_same_team_victim_unit_type_counts[index].load(std::memory_order_relaxed);
    }
    snapshot.damage_same_team_victim_unit_type_overflow =
        g_target_probe.damage_same_team_victim_unit_type_overflow.load(std::memory_order_relaxed);
    return snapshot;
}

}  // namespace dwrt::host
