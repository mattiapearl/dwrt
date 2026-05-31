#pragma once

#include <stdint.h>

#ifdef _WIN32
#define DWRT_API __declspec(dllimport)
#else
#define DWRT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DWRT_ABI_VERSION = 1,

    DWRT_NET_INCOMING = 0,
    DWRT_NET_OUTGOING = 1,

    DWRT_ROUTE_NO_INTEREST = 0,
    DWRT_ROUTE_FAST_ONLY = 1,
    DWRT_ROUTE_SERIALIZED_ONLY = 2,
    DWRT_ROUTE_FAST_AND_SERIALIZED = 3,

    DWRT_USERCMD_ROUTE_NO_WORK = 0,
    DWRT_USERCMD_ROUTE_COUNT_ONLY = 1,
    DWRT_USERCMD_ROUTE_FAST_READ = 2,
    DWRT_USERCMD_ROUTE_FULL_PROTOBUF = 3,
    DWRT_USERCMD_ROUTE_FAST_AND_FULL = 4,

    DWRT_USERCMD_MOUNT_FULL_PROTOBUF = 1u << 0,
    DWRT_USERCMD_MOUNT_FAST_READ = 1u << 1,
    DWRT_USERCMD_MOUNT_BUTTON_TRIGGERS = 1u << 2,

    DWRT_PROBE_MOUNT_DAMAGE = 1u << 0,
    DWRT_PROBE_MOUNT_ENTITY_INPUT = 1u << 1,
    DWRT_PROBE_MOUNT_ENTITY_OUTPUT = 1u << 2,
    DWRT_PROBE_MOUNT_ENTITY_TOUCH = 1u << 3,

    DWRT_PROBE_ROUTE_NO_INTEREST = 0,
    DWRT_PROBE_ROUTE_COUNTED = 1,
};

typedef struct DwrtRuntime DwrtRuntime;

typedef struct DwrtFastDamageNative {
    uint32_t victim_handle;
    uint32_t attacker_handle;
    uint32_t inflictor_handle;
    uint32_t ability_handle;
    uint8_t victim_team;
    uint8_t attacker_team;
    uint8_t _pad0[2];
    uint32_t victim_class_hash;
    uint32_t attacker_class_hash;
    float damage;
    int32_t damage_type;
    uint64_t damage_flags;
} DwrtFastDamageNative;

typedef struct DwrtFastEntityIoNative {
    uint32_t entity_handle;
    uint32_t activator_handle;
    uint32_t caller_handle;
    uint32_t class_hash;
    uint32_t name_hash;
    uint32_t value_hash;
    uint8_t phase;
    uint8_t _pad0[7];
} DwrtFastEntityIoNative;

typedef struct DwrtFastEntityTouchNative {
    uint32_t entity_handle;
    uint32_t other_handle;
    uint32_t entity_class_hash;
    uint32_t other_class_hash;
    uint8_t phase;
    uint8_t _pad0[7];
} DwrtFastEntityTouchNative;

typedef struct DwrtProbeCountersNative {
    uint32_t mount_mask;
    uint32_t _pad0;
    uint64_t damage_seen;
    uint64_t damage_counted;
    uint64_t entity_input_seen;
    uint64_t entity_input_counted;
    uint64_t entity_output_seen;
    uint64_t entity_output_counted;
    uint64_t entity_touch_seen;
    uint64_t entity_touch_counted;
} DwrtProbeCountersNative;

DWRT_API uint32_t dwrt_abi_version(void);
DWRT_API DwrtRuntime *dwrt_runtime_new(void);
DWRT_API void dwrt_runtime_free(DwrtRuntime *runtime);

DWRT_API uint8_t dwrt_net_add_serialized(const DwrtRuntime *runtime, int32_t direction, int32_t msg_id);
DWRT_API uint8_t dwrt_net_remove_serialized(const DwrtRuntime *runtime, int32_t direction, int32_t msg_id);
DWRT_API uint8_t dwrt_net_add_fast(const DwrtRuntime *runtime, int32_t direction, int32_t msg_id);
DWRT_API uint8_t dwrt_net_remove_fast(const DwrtRuntime *runtime, int32_t direction, int32_t msg_id);
DWRT_API uint8_t dwrt_net_add_user_fast(const DwrtRuntime *runtime, int32_t user_msg_id);
DWRT_API uint8_t dwrt_net_add_user_serialized(const DwrtRuntime *runtime, int32_t user_msg_id);
DWRT_API uint32_t dwrt_net_route(
    const DwrtRuntime *runtime,
    int32_t direction,
    int32_t msg_id,
    uint8_t has_user_msg_id,
    int32_t user_msg_id);

DWRT_API void dwrt_usercmd_set_mount_mask(const DwrtRuntime *runtime, uint32_t mask);
DWRT_API uint32_t dwrt_usercmd_route(const DwrtRuntime *runtime);

DWRT_API void dwrt_probe_set_mount_mask(const DwrtRuntime *runtime, uint32_t mask);
DWRT_API uint32_t dwrt_probe_record_damage(const DwrtRuntime *runtime, const DwrtFastDamageNative *event);
DWRT_API uint32_t dwrt_probe_record_entity_input(const DwrtRuntime *runtime, const DwrtFastEntityIoNative *event);
DWRT_API uint32_t dwrt_probe_record_entity_output(const DwrtRuntime *runtime, const DwrtFastEntityIoNative *event);
DWRT_API uint32_t dwrt_probe_record_entity_touch(const DwrtRuntime *runtime, const DwrtFastEntityTouchNative *event);
DWRT_API uint8_t dwrt_probe_snapshot(const DwrtRuntime *runtime, DwrtProbeCountersNative *out);
DWRT_API void dwrt_probe_reset_counters(const DwrtRuntime *runtime);

#ifdef __cplusplus
}
#endif
