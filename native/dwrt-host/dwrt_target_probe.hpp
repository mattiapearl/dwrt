#pragma once

#include <cstdint>

struct DwrtTargetProbeSnapshot {
    std::uint32_t enabled;
    std::uint32_t source_team_spoof_enabled;
    std::uint32_t target_team_spoof_enabled;
    std::uint32_t target_team_spoof_team;
    std::uint32_t target_bitset_allow_enabled;
    std::uint32_t force_filter_same_team_allow_enabled;
    std::uint32_t force_caller_same_team_allow_enabled;
    std::uint32_t force_filter_objective_allow_enabled;
    std::uint32_t force_caller_objective_allow_enabled;
    std::uint32_t force_secondary_allow_enabled;
    std::uint32_t neutral_simulation_enabled;
    std::uint32_t classifier_spoof_enabled;
    std::uint32_t classifier_spoof_bit;
    std::uint32_t global_neutralize_enabled;
    std::uint32_t global_neutralize_team;
    std::uint32_t last_source_team;
    std::uint32_t last_target_team;
    std::uint32_t last_target_unit_type;
    std::uint32_t last_filter_result;
    std::uint32_t last_damage_victim_team;
    std::uint32_t last_damage_victim_unit_type;
    std::uint32_t last_damage_attacker_handle;
    std::uint32_t last_damage_inflictor_handle;
    std::uint32_t last_damage_attacker_team;
    std::uint32_t last_damage_attacker_unit_type;
    std::uint32_t _pad0;
    std::uint32_t _pad1;
    std::uint64_t filter_calls;
    std::uint64_t filter_allowed;
    std::uint64_t filter_denied;
    std::uint64_t filter_same_team_calls;
    std::uint64_t filter_same_team_allowed;
    std::uint64_t filter_same_team_denied;
    std::uint64_t filter_objective_target_calls;
    std::uint64_t filter_objective_target_allowed;
    std::uint64_t filter_objective_target_denied;
    std::uint64_t filter_neutral_target_calls;
    std::uint64_t filter_midboss_target_calls;
    std::uint64_t filter_invalid_source_team;
    std::uint64_t filter_invalid_target_team;
    std::uint64_t source_spoof_attempts;
    std::uint64_t source_spoof_applied;
    std::uint64_t source_spoof_restored;
    std::uint64_t source_spoof_allowed;
    std::uint64_t source_spoof_denied;
    std::uint64_t target_spoof_attempts;
    std::uint64_t target_spoof_applied;
    std::uint64_t target_spoof_restored;
    std::uint64_t target_spoof_allowed;
    std::uint64_t target_spoof_denied;
    std::uint64_t bitset_allow_attempts;
    std::uint64_t bitset_allow_applied;
    std::uint64_t bitset_allow_failures;
    std::uint64_t classifier_calls;
    std::uint64_t classifier_invalid;
    std::uint64_t classifier_spoof_attempts;
    std::uint64_t classifier_spoof_applied;
    std::uint64_t classifier_spoof_skipped;
    std::uint64_t last_classifier_original_bit;
    std::uint64_t last_classifier_final_bit;
    std::uint64_t global_neutralize_attempts;
    std::uint64_t global_neutralize_applied;
    std::uint64_t global_neutralize_already;
    std::uint64_t global_neutralize_null;
    std::uint64_t global_neutralize_invalid_team;
    std::uint64_t global_neutralize_write_failures;
    std::uint64_t global_neutralize_original_team_counts[8];
    std::uint64_t global_neutralize_original_team_overflow;
    std::uint64_t filter_forced_allowed;
    std::uint64_t caller_forced_allowed;
    std::uint64_t secondary_forced_allowed;
    std::uint64_t secondary_calls;
    std::uint64_t secondary_allowed;
    std::uint64_t secondary_denied;
    std::uint64_t caller_calls;
    std::uint64_t caller_allowed;
    std::uint64_t caller_denied;
    std::uint64_t caller_unit_0x1a_bypass_candidates;
    std::uint64_t filter_unit_type_counts[32];
    std::uint64_t filter_unit_type_overflow;
    std::uint64_t damage_victim_calls;
    std::uint64_t damage_victim_objective;
    std::uint64_t damage_victim_neutral;
    std::uint64_t damage_victim_midboss;
    std::uint64_t damage_victim_invalid_team;
    std::uint64_t damage_victim_unit_type_counts[64];
    std::uint64_t damage_victim_unit_type_overflow;
    std::uint64_t damage_attacker_handle_valid;
    std::uint64_t damage_attacker_handle_invalid;
    std::uint64_t damage_inflictor_handle_valid;
    std::uint64_t damage_inflictor_handle_invalid;
    std::uint64_t damage_attacker_same_team;
    std::uint64_t damage_attacker_opposing_team;
    std::uint64_t damage_attacker_other_team;
    std::uint64_t damage_attacker_self;
    std::uint64_t damage_attacker_same_team_objective;
    std::uint64_t damage_attacker_unit_type_counts[64];
    std::uint64_t damage_attacker_unit_type_overflow;
    std::uint64_t damage_same_team_victim_unit_type_counts[64];
    std::uint64_t damage_same_team_victim_unit_type_overflow;
};

namespace dwrt::host {

struct TargetFilterScope {
    void* source = nullptr;
    void* target = nullptr;
    std::uint8_t original_source_team = 0;
    std::uint8_t original_target_team = 0;
    std::uint8_t source_spoof_applied = 0;
    std::uint8_t target_spoof_applied = 0;
    std::uint8_t same_team = 0;
    std::uint8_t target_objective = 0;
};

using TargetIdentityClassifierFn = std::uint32_t*(__fastcall*)(void*, std::uint32_t*);

void configure_target_probe_from_environment();
void reset_target_probe_counters();
void maybe_global_neutralize_entity(void* entity);
TargetFilterScope begin_target_filter(void* source, void* target, bool recursive);
bool maybe_allow_target_bitset(
    const TargetFilterScope& scope,
    void* target,
    void* bitset,
    TargetIdentityClassifierFn classifier);
void begin_target_classifier_spoof_scope(const TargetFilterScope& scope);
void end_target_classifier_spoof_scope();
void finish_target_identity_classifier(void* target, std::uint32_t* out);
void record_damage_victim(void* victim, void* info, bool recursive);
bool finish_target_filter(const TargetFilterScope& scope, void* source, void* target, bool result);
bool record_target_filter_caller(void* source, void* target, bool result);
bool record_secondary_target_gate(void* source, void* maybe_attacker, void* maybe_target, bool result);
DwrtTargetProbeSnapshot target_probe_snapshot();

}  // namespace dwrt::host
