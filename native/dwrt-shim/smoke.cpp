#include "dwrt_shadow_shim.hpp"

#include <iostream>
#include <string>

namespace {

int fail(int code, const std::string& message) {
    std::cerr << "[dwrt-shim-smoke] ERROR: " << message << '\n';
    return code;
}

void print_counters(const dwrt::shim::DwrtShadowCounters& counters) {
    std::cout << "[dwrt-shim-smoke] counters"
              << " net_no_interest=" << counters.net_routes[DWRT_ROUTE_NO_INTEREST]
              << " net_fast=" << counters.net_routes[DWRT_ROUTE_FAST_ONLY]
              << " net_serialized=" << counters.net_routes[DWRT_ROUTE_SERIALIZED_ONLY]
              << " net_fast_serialized=" << counters.net_routes[DWRT_ROUTE_FAST_AND_SERIALIZED]
              << " usercmd_no_work=" << counters.usercmd_routes[DWRT_USERCMD_ROUTE_NO_WORK]
              << " usercmd_count=" << counters.usercmd_routes[DWRT_USERCMD_ROUTE_COUNT_ONLY]
              << " usercmd_fast=" << counters.usercmd_routes[DWRT_USERCMD_ROUTE_FAST_READ]
              << " usercmd_full=" << counters.usercmd_routes[DWRT_USERCMD_ROUTE_FULL_PROTOBUF]
              << " usercmd_fast_full=" << counters.usercmd_routes[DWRT_USERCMD_ROUTE_FAST_AND_FULL]
              << " qpc_hz=" << counters.qpc_ticks_per_second
              << " net_max_ticks=" << counters.net_route_max_ticks
              << " usercmd_max_ticks=" << counters.usercmd_route_max_ticks
              << " net_slow=" << counters.net_route_slow_calls
              << " usercmd_slow=" << counters.usercmd_route_slow_calls
              << '\n';
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return fail(64, "usage: dwrt_shadow_smoke.exe <path-to-dwrt_runtime.dll>");
    }

    std::string error;
    dwrt::shim::DwrtShadowShim shim;
    if (!shim.load(argv[1], error)) {
        return fail(1, error);
    }

    const std::uint32_t abi = shim.abi_version();
    std::cout << "[DWRT] loaded runtime abi=" << abi << '\n';
    if (abi != DWRT_ABI_VERSION) {
        return fail(2, "unexpected ABI version");
    }

    if (!shim.create_runtime(error)) {
        return fail(3, error);
    }
    shim.set_timing_enabled(true);
    shim.set_slow_threshold_ns(100'000);
    std::cout << "[DWRT] runtime created\n";

    const std::uint32_t net_no_interest = shim.shadow_route_net(DWRT_NET_OUTGOING, 72, 0, 0);
    if (net_no_interest != DWRT_ROUTE_NO_INTEREST) {
        return fail(4, "expected no-interest net route");
    }

    if (!shim.add_net_user_fast(314)) {
        return fail(5, "failed to add user-message fast interest");
    }
    const std::uint32_t net_fast = shim.shadow_route_net(DWRT_NET_OUTGOING, 72, 1, 314);
    if (net_fast != DWRT_ROUTE_FAST_ONLY) {
        return fail(6, "expected fast-only net route");
    }

    if (!shim.add_net_serialized(DWRT_NET_OUTGOING, 72)) {
        return fail(7, "failed to add outgoing serialized interest");
    }
    const std::uint32_t net_fast_serialized = shim.shadow_route_net(DWRT_NET_OUTGOING, 72, 1, 314);
    if (net_fast_serialized != DWRT_ROUTE_FAST_AND_SERIALIZED) {
        return fail(8, "expected fast+serialized net route");
    }

    const std::uint32_t usercmd_count = shim.shadow_route_usercmd();
    if (usercmd_count != DWRT_USERCMD_ROUTE_COUNT_ONLY) {
        return fail(9, "expected count-only usercmd route");
    }

    shim.set_usercmd_mount_mask(DWRT_USERCMD_MOUNT_FAST_READ);
    const std::uint32_t usercmd_fast = shim.shadow_route_usercmd();
    if (usercmd_fast != DWRT_USERCMD_ROUTE_FAST_READ) {
        return fail(10, "expected fast-read usercmd route");
    }

    shim.set_usercmd_mount_mask(DWRT_USERCMD_MOUNT_FAST_READ | DWRT_USERCMD_MOUNT_FULL_PROTOBUF);
    const std::uint32_t usercmd_fast_full = shim.shadow_route_usercmd();
    if (usercmd_fast_full != DWRT_USERCMD_ROUTE_FAST_AND_FULL) {
        return fail(11, "expected fast+full usercmd route");
    }

    std::cout << "[DWRT] shadow net route no_interest=" << net_no_interest
              << " fast=" << net_fast
              << " fast_serialized=" << net_fast_serialized << '\n';
    std::cout << "[DWRT] shadow usercmd route count=" << usercmd_count
              << " fast=" << usercmd_fast
              << " fast_full=" << usercmd_fast_full << '\n';
    print_counters(shim.counters());
    std::cout << "[dwrt-shim-smoke] OK\n";

    return 0;
}
