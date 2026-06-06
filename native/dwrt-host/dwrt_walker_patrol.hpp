#pragma once

#include <cstdint>

struct DwrtWalkerPatrolSnapshot {
    std::uint32_t enabled;
    std::uint32_t stride;
    std::uint32_t waypoint_count;
    std::uint32_t mode;
    std::uint64_t damage_callbacks;
    std::uint64_t candidate_walkers;
    std::uint64_t non_walker_victims;
    std::uint64_t skipped_recursive;
    std::uint64_t missing_identity;
    std::uint64_t missing_designer_name;
    std::uint64_t teleport_attempts;
    std::uint64_t teleport_calls;
    std::uint64_t body_component_missing;
    std::uint64_t scene_node_missing;
    std::uint64_t origin_read_attempts;
    std::uint64_t origin_read_successes;
    std::uint64_t origin_read_failures;
};

namespace dwrt::host {

void configure_walker_patrol_from_environment();
void reset_walker_patrol_counters();
void maybe_apply_walker_patrol_on_damage(void* entity, bool recursive);
DwrtWalkerPatrolSnapshot walker_patrol_snapshot();

}  // namespace dwrt::host
