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
};

typedef struct DwrtRuntime DwrtRuntime;

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

#ifdef __cplusplus
}
#endif
