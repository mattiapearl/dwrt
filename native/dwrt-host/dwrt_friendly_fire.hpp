#pragma once

#include <cstdint>

struct DwrtFriendlyFireSnapshot {
    std::uint32_t enabled;
    std::uint32_t mode;
    std::uint32_t scope;
    std::uint32_t local_team;
    std::uint64_t damage_callbacks;
    std::uint64_t skipped_recursive;
    std::uint64_t missing_identity;
    std::uint64_t missing_designer_name;
    std::uint64_t non_objective_victims;
    std::uint64_t objective_candidates;
    std::uint64_t invalid_team;
    std::uint64_t team_spoof_attempts;
    std::uint64_t team_spoof_applied;
    std::uint64_t team_spoof_restored;
};

namespace dwrt::host {

struct FriendlyFireDamageScope {
    void* entity = nullptr;
    std::uint8_t original_team = 0;
    std::uint8_t applied = 0;
    std::uint8_t _pad0 = 0;
    std::uint16_t _pad1 = 0;
};

void configure_friendly_fire_from_environment();
void reset_friendly_fire_counters();
FriendlyFireDamageScope begin_friendly_fire_damage(void* entity, bool recursive);
void end_friendly_fire_damage(const FriendlyFireDamageScope& scope);
DwrtFriendlyFireSnapshot friendly_fire_snapshot();

}  // namespace dwrt::host
