#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "dwrt_runtime.h"

namespace dwrt::shim {

struct DwrtShadowCounters {
    std::uint64_t runtime_loads = 0;
    std::uint64_t runtime_unloads = 0;
    std::uint64_t runtime_create_failures = 0;
    std::uint64_t net_routes[4] = {};
    std::uint64_t usercmd_routes[5] = {};
    std::uint64_t qpc_ticks_per_second = 0;
    std::uint64_t slow_threshold_ticks = 0;
    std::uint64_t net_route_total_ticks = 0;
    std::uint64_t net_route_max_ticks = 0;
    std::uint64_t net_route_slow_calls = 0;
    std::uint64_t usercmd_route_total_ticks = 0;
    std::uint64_t usercmd_route_max_ticks = 0;
    std::uint64_t usercmd_route_slow_calls = 0;
};

class DwrtShadowShim {
public:
    DwrtShadowShim();
    DwrtShadowShim(const DwrtShadowShim&) = delete;
    DwrtShadowShim& operator=(const DwrtShadowShim&) = delete;
    ~DwrtShadowShim();

    bool load(const wchar_t* runtime_path, std::string& error);
    void unload();

    bool create_runtime(std::string& error);
    void destroy_runtime();

    [[nodiscard]] bool loaded() const;
    [[nodiscard]] bool has_runtime() const;
    [[nodiscard]] std::uint32_t abi_version() const;

    void set_timing_enabled(bool enabled);
    void set_slow_threshold_ns(std::uint64_t threshold_ns);

    bool add_net_user_fast(std::int32_t user_msg_id);
    bool add_net_serialized(std::int32_t direction, std::int32_t msg_id);
    void set_usercmd_mount_mask(std::uint32_t mask);

    std::uint32_t shadow_route_net(
        std::int32_t direction,
        std::int32_t msg_id,
        std::uint8_t has_user_msg_id,
        std::int32_t user_msg_id);
    std::uint32_t shadow_route_usercmd();

    [[nodiscard]] const DwrtShadowCounters& counters() const;

private:
    using AbiVersionFn = std::uint32_t (*)();
    using RuntimeNewFn = DwrtRuntime* (*)();
    using RuntimeFreeFn = void (*)(DwrtRuntime*);
    using NetAddSerializedFn = std::uint8_t (*)(const DwrtRuntime*, std::int32_t, std::int32_t);
    using NetAddUserFastFn = std::uint8_t (*)(const DwrtRuntime*, std::int32_t);
    using NetRouteFn = std::uint32_t (*)(const DwrtRuntime*, std::int32_t, std::int32_t, std::uint8_t, std::int32_t);
    using UsercmdSetMountMaskFn = void (*)(const DwrtRuntime*, std::uint32_t);
    using UsercmdRouteFn = std::uint32_t (*)(const DwrtRuntime*);

    void* module_ = nullptr;
    DwrtRuntime* runtime_ = nullptr;
    DwrtShadowCounters counters_ = {};
    bool timing_enabled_ = false;

    AbiVersionFn abi_version_ = nullptr;
    RuntimeNewFn runtime_new_ = nullptr;
    RuntimeFreeFn runtime_free_ = nullptr;
    NetAddSerializedFn net_add_serialized_ = nullptr;
    NetAddUserFastFn net_add_user_fast_ = nullptr;
    NetRouteFn net_route_ = nullptr;
    UsercmdSetMountMaskFn usercmd_set_mount_mask_ = nullptr;
    UsercmdRouteFn usercmd_route_ = nullptr;
};

}  // namespace dwrt::shim
