#include "dwrt_friendly_fire.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dwrt::host {
namespace {

constexpr std::uint32_t kModeNone = 0;
constexpr std::uint32_t kModeVictimTeamSpoof = 1;
constexpr std::uint32_t kScopeObjectives = 1;

// Current-build schema evidence, not public API:
// CBaseEntity::m_iTeamNum = 0x33c (schema registration 0x180e7ee20).
constexpr std::size_t kCBaseEntityTeamNumOffset = 0x33c;

struct FriendlyFireState {
    std::atomic<std::uint32_t> enabled{0};
    std::atomic<std::uint32_t> mode{kModeNone};
    std::atomic<std::uint32_t> scope{kScopeObjectives};
    std::atomic<std::uint32_t> local_team{2};
    std::atomic<std::uint64_t> damage_callbacks{0};
    std::atomic<std::uint64_t> skipped_recursive{0};
    std::atomic<std::uint64_t> missing_identity{0};
    std::atomic<std::uint64_t> missing_designer_name{0};
    std::atomic<std::uint64_t> non_objective_victims{0};
    std::atomic<std::uint64_t> objective_candidates{0};
    std::atomic<std::uint64_t> invalid_team{0};
    std::atomic<std::uint64_t> team_spoof_attempts{0};
    std::atomic<std::uint64_t> team_spoof_applied{0};
    std::atomic<std::uint64_t> team_spoof_restored{0};
};

struct EntityIdentityLite {
    void* instance;
    void* entity_class;
    std::uint32_t ehandle;
    std::int32_t name_stringable_index;
    const char* name;
    const char* designer_name;
};

FriendlyFireState g_friendly_fire;

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
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(buffer, &end, 10);
    if (end == buffer || value > 255UL) {
        return fallback;
    }
    return static_cast<std::uint32_t>(value);
}

EntityIdentityLite* entity_identity(void* entity) {
    if (entity == nullptr) {
        return nullptr;
    }
    auto* bytes = static_cast<std::byte*>(entity);
    return *reinterpret_cast<EntityIdentityLite**>(bytes + 0x10);
}

bool is_objective_designer_name(const char* designer_name) {
    if (designer_name == nullptr || designer_name[0] == '\0') {
        return false;
    }
    const std::string_view name(designer_name);
    return name == "npc_boss_tier1" ||
        name == "alt_npc_boss_tier1" ||
        name == "CNPC_Boss_Tier1" ||
        name == "npc_boss_tier2" ||
        name == "alt_npc_boss_tier2" ||
        name == "CNPC_Boss_Tier2" ||
        name == "npc_boss_tier3" ||
        name == "alt_npc_boss_tier3" ||
        name == "CNPC_Boss_Tier3" ||
        name == "npc_trooper_boss" ||
        name == "alt_npc_trooper_boss" ||
        name == "CNPC_TrooperBoss" ||
        name == "CNPC_BarrackBoss";
}

std::uint8_t opposing_team(std::uint8_t team) {
    if (team == 2) {
        return 3;
    }
    if (team == 3) {
        return 2;
    }
    return 0;
}

std::uint8_t* team_field(void* entity) {
    return reinterpret_cast<std::uint8_t*>(static_cast<std::byte*>(entity) + kCBaseEntityTeamNumOffset);
}

}  // namespace

void configure_friendly_fire_from_environment() {
    const bool enabled = env_flag_enabled(L"DWRT_FRIENDLY_FIRE_EXPERIMENT") ||
        env_flag_enabled(L"DWRT_FRIENDLY_FIRE_OBJECTIVE_TEAM_SPOOF");
    g_friendly_fire.enabled.store(enabled ? 1U : 0U, std::memory_order_release);
    g_friendly_fire.mode.store(enabled ? kModeVictimTeamSpoof : kModeNone, std::memory_order_release);
    g_friendly_fire.scope.store(kScopeObjectives, std::memory_order_release);
    g_friendly_fire.local_team.store(env_u32_or(L"DWRT_FRIENDLY_FIRE_LOCAL_TEAM", 2), std::memory_order_release);
}

void reset_friendly_fire_counters() {
    g_friendly_fire.damage_callbacks.store(0, std::memory_order_relaxed);
    g_friendly_fire.skipped_recursive.store(0, std::memory_order_relaxed);
    g_friendly_fire.missing_identity.store(0, std::memory_order_relaxed);
    g_friendly_fire.missing_designer_name.store(0, std::memory_order_relaxed);
    g_friendly_fire.non_objective_victims.store(0, std::memory_order_relaxed);
    g_friendly_fire.objective_candidates.store(0, std::memory_order_relaxed);
    g_friendly_fire.invalid_team.store(0, std::memory_order_relaxed);
    g_friendly_fire.team_spoof_attempts.store(0, std::memory_order_relaxed);
    g_friendly_fire.team_spoof_applied.store(0, std::memory_order_relaxed);
    g_friendly_fire.team_spoof_restored.store(0, std::memory_order_relaxed);
}

FriendlyFireDamageScope begin_friendly_fire_damage(void* entity, bool recursive) {
    FriendlyFireDamageScope scope{};
    if (g_friendly_fire.enabled.load(std::memory_order_acquire) == 0 ||
        g_friendly_fire.mode.load(std::memory_order_acquire) != kModeVictimTeamSpoof) {
        return scope;
    }

    g_friendly_fire.damage_callbacks.fetch_add(1, std::memory_order_relaxed);
    if (recursive) {
        g_friendly_fire.skipped_recursive.fetch_add(1, std::memory_order_relaxed);
        return scope;
    }

    const EntityIdentityLite* identity = entity_identity(entity);
    if (identity == nullptr) {
        g_friendly_fire.missing_identity.fetch_add(1, std::memory_order_relaxed);
        return scope;
    }
    const char* designer_name = identity->designer_name;
    if (designer_name == nullptr || designer_name[0] == '\0') {
        g_friendly_fire.missing_designer_name.fetch_add(1, std::memory_order_relaxed);
        return scope;
    }
    if (!is_objective_designer_name(designer_name)) {
        g_friendly_fire.non_objective_victims.fetch_add(1, std::memory_order_relaxed);
        return scope;
    }

    g_friendly_fire.objective_candidates.fetch_add(1, std::memory_order_relaxed);
    std::uint8_t* team = team_field(entity);
    const std::uint8_t original_team = *team;
    const std::uint32_t local_team = g_friendly_fire.local_team.load(std::memory_order_acquire);
    if (local_team != 0 && original_team != static_cast<std::uint8_t>(local_team)) {
        return scope;
    }
    const std::uint8_t spoofed_team = opposing_team(original_team);
    if (spoofed_team == 0) {
        g_friendly_fire.invalid_team.fetch_add(1, std::memory_order_relaxed);
        return scope;
    }

    g_friendly_fire.team_spoof_attempts.fetch_add(1, std::memory_order_relaxed);
    *team = spoofed_team;
    scope.entity = entity;
    scope.original_team = original_team;
    scope.applied = 1;
    g_friendly_fire.team_spoof_applied.fetch_add(1, std::memory_order_relaxed);
    return scope;
}

void end_friendly_fire_damage(const FriendlyFireDamageScope& scope) {
    if (scope.applied == 0 || scope.entity == nullptr) {
        return;
    }
    *team_field(scope.entity) = scope.original_team;
    g_friendly_fire.team_spoof_restored.fetch_add(1, std::memory_order_relaxed);
}

DwrtFriendlyFireSnapshot friendly_fire_snapshot() {
    DwrtFriendlyFireSnapshot snapshot{};
    snapshot.enabled = g_friendly_fire.enabled.load(std::memory_order_acquire);
    snapshot.mode = g_friendly_fire.mode.load(std::memory_order_acquire);
    snapshot.scope = g_friendly_fire.scope.load(std::memory_order_acquire);
    snapshot.local_team = g_friendly_fire.local_team.load(std::memory_order_acquire);
    snapshot.damage_callbacks = g_friendly_fire.damage_callbacks.load(std::memory_order_relaxed);
    snapshot.skipped_recursive = g_friendly_fire.skipped_recursive.load(std::memory_order_relaxed);
    snapshot.missing_identity = g_friendly_fire.missing_identity.load(std::memory_order_relaxed);
    snapshot.missing_designer_name = g_friendly_fire.missing_designer_name.load(std::memory_order_relaxed);
    snapshot.non_objective_victims = g_friendly_fire.non_objective_victims.load(std::memory_order_relaxed);
    snapshot.objective_candidates = g_friendly_fire.objective_candidates.load(std::memory_order_relaxed);
    snapshot.invalid_team = g_friendly_fire.invalid_team.load(std::memory_order_relaxed);
    snapshot.team_spoof_attempts = g_friendly_fire.team_spoof_attempts.load(std::memory_order_relaxed);
    snapshot.team_spoof_applied = g_friendly_fire.team_spoof_applied.load(std::memory_order_relaxed);
    snapshot.team_spoof_restored = g_friendly_fire.team_spoof_restored.load(std::memory_order_relaxed);
    return snapshot;
}

}  // namespace dwrt::host
