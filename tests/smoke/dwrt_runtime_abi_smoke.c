#include <stdint.h>
#include "dwrt_runtime.h"

int main(void) {
    if (dwrt_abi_version() != DWRT_ABI_VERSION) return 1;

    DwrtRuntime *rt = dwrt_runtime_new();
    if (!rt) return 2;

    if (dwrt_usercmd_route(rt) != DWRT_USERCMD_ROUTE_COUNT_ONLY) return 3;

    dwrt_usercmd_set_mount_mask(rt, DWRT_USERCMD_MOUNT_FAST_READ);
    if (dwrt_usercmd_route(rt) != DWRT_USERCMD_ROUTE_FAST_READ) return 4;

    if (dwrt_net_route(rt, DWRT_NET_OUTGOING, 72, 1, 314) != DWRT_ROUTE_NO_INTEREST) return 5;

    if (dwrt_net_add_user_fast(rt, 314) != 1) return 6;
    if (dwrt_net_route(rt, DWRT_NET_OUTGOING, 72, 1, 314) != DWRT_ROUTE_FAST_ONLY) return 7;

    if (dwrt_net_add_serialized(rt, DWRT_NET_OUTGOING, 72) != 1) return 8;
    if (dwrt_net_route(rt, DWRT_NET_OUTGOING, 72, 1, 314) != DWRT_ROUTE_FAST_AND_SERIALIZED) return 9;

    if (dwrt_net_remove_serialized(rt, DWRT_NET_OUTGOING, 72) != 1) return 10;
    if (dwrt_net_route(rt, DWRT_NET_OUTGOING, 72, 1, 314) != DWRT_ROUTE_FAST_ONLY) return 11;

    DwrtFastDamageNative damage = {0};
    DwrtFastEntityIoNative input = {0};
    if (dwrt_probe_record_damage(rt, &damage) != DWRT_PROBE_ROUTE_NO_INTEREST) return 12;
    dwrt_probe_set_mount_mask(rt, DWRT_PROBE_MOUNT_DAMAGE | DWRT_PROBE_MOUNT_ENTITY_INPUT);
    if (dwrt_probe_record_damage(rt, &damage) != DWRT_PROBE_ROUTE_COUNTED) return 13;
    if (dwrt_probe_record_entity_input(rt, &input) != DWRT_PROBE_ROUTE_COUNTED) return 14;
    if (dwrt_probe_record_entity_output(rt, &input) != DWRT_PROBE_ROUTE_NO_INTEREST) return 15;

    DwrtProbeCountersNative counters = {0};
    if (dwrt_probe_snapshot(rt, &counters) != 1) return 16;
    if (counters.damage_seen != 1 || counters.damage_counted != 1) return 17;
    if (counters.entity_input_seen != 1 || counters.entity_input_counted != 1) return 18;
    if (counters.entity_output_seen != 0 || counters.entity_output_counted != 0) return 19;

    dwrt_runtime_free(rt);
    return 0;
}
