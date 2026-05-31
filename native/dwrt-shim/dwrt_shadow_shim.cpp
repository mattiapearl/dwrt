#include "dwrt_shadow_shim.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iterator>
#include <sstream>
#include <type_traits>

namespace dwrt::shim {
namespace {

std::string last_error_message(const char* prefix) {
    const DWORD error = GetLastError();
    std::ostringstream out;
    out << prefix << " (GetLastError=" << error << ")";
    return out.str();
}

FARPROC required_proc(HMODULE module, const char* name, std::string& error) {
    FARPROC proc = GetProcAddress(module, name);
    if (proc == nullptr) {
        std::ostringstream out;
        out << "missing DWRT runtime export: " << name;
        error = out.str();
    }
    return proc;
}

void count_route(std::uint64_t* counters, std::size_t length, std::uint32_t route) {
    if (route < length) {
        counters[route] += 1;
    }
}

std::uint64_t qpc_now_ticks() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

void record_duration(
    std::uint64_t elapsed_ticks,
    std::uint64_t slow_threshold_ticks,
    std::uint64_t& total_ticks,
    std::uint64_t& max_ticks,
    std::uint64_t& slow_calls) {
    total_ticks += elapsed_ticks;
    if (elapsed_ticks > max_ticks) {
        max_ticks = elapsed_ticks;
    }
    if (slow_threshold_ticks != 0 && elapsed_ticks >= slow_threshold_ticks) {
        slow_calls += 1;
    }
}

}  // namespace

DwrtShadowShim::DwrtShadowShim() {
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency)) {
        counters_.qpc_ticks_per_second = static_cast<std::uint64_t>(frequency.QuadPart);
    }
}

DwrtShadowShim::~DwrtShadowShim() {
    unload();
}

bool DwrtShadowShim::load(const wchar_t* runtime_path, std::string& error) {
    unload();

    HMODULE module = LoadLibraryW(runtime_path);
    if (module == nullptr) {
        error = last_error_message("failed to load DWRT runtime DLL");
        return false;
    }

    auto bind = [&](auto& target, const char* name) -> bool {
        FARPROC proc = required_proc(module, name, error);
        if (proc == nullptr) {
            return false;
        }
        target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(proc);
        return true;
    };

    if (!bind(abi_version_, "dwrt_abi_version") ||
        !bind(runtime_new_, "dwrt_runtime_new") ||
        !bind(runtime_free_, "dwrt_runtime_free") ||
        !bind(net_add_serialized_, "dwrt_net_add_serialized") ||
        !bind(net_add_user_fast_, "dwrt_net_add_user_fast") ||
        !bind(net_route_, "dwrt_net_route") ||
        !bind(usercmd_set_mount_mask_, "dwrt_usercmd_set_mount_mask") ||
        !bind(usercmd_route_, "dwrt_usercmd_route") ||
        !bind(probe_set_mount_mask_, "dwrt_probe_set_mount_mask") ||
        !bind(probe_record_damage_, "dwrt_probe_record_damage") ||
        !bind(probe_record_entity_input_, "dwrt_probe_record_entity_input") ||
        !bind(probe_record_entity_output_, "dwrt_probe_record_entity_output") ||
        !bind(probe_record_entity_touch_, "dwrt_probe_record_entity_touch") ||
        !bind(probe_snapshot_, "dwrt_probe_snapshot") ||
        !bind(probe_reset_counters_, "dwrt_probe_reset_counters")) {
        FreeLibrary(module);
        abi_version_ = nullptr;
        runtime_new_ = nullptr;
        runtime_free_ = nullptr;
        net_add_serialized_ = nullptr;
        net_add_user_fast_ = nullptr;
        net_route_ = nullptr;
        usercmd_set_mount_mask_ = nullptr;
        usercmd_route_ = nullptr;
        probe_set_mount_mask_ = nullptr;
        probe_record_damage_ = nullptr;
        probe_record_entity_input_ = nullptr;
        probe_record_entity_output_ = nullptr;
        probe_record_entity_touch_ = nullptr;
        probe_snapshot_ = nullptr;
        probe_reset_counters_ = nullptr;
        return false;
    }

    module_ = module;
    counters_.runtime_loads += 1;
    return true;
}

void DwrtShadowShim::unload() {
    destroy_runtime();

    if (module_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
        counters_.runtime_unloads += 1;
    }

    abi_version_ = nullptr;
    runtime_new_ = nullptr;
    runtime_free_ = nullptr;
    net_add_serialized_ = nullptr;
    net_add_user_fast_ = nullptr;
    net_route_ = nullptr;
    usercmd_set_mount_mask_ = nullptr;
    usercmd_route_ = nullptr;
    probe_set_mount_mask_ = nullptr;
    probe_record_damage_ = nullptr;
    probe_record_entity_input_ = nullptr;
    probe_record_entity_output_ = nullptr;
    probe_record_entity_touch_ = nullptr;
    probe_snapshot_ = nullptr;
    probe_reset_counters_ = nullptr;
}

bool DwrtShadowShim::create_runtime(std::string& error) {
    destroy_runtime();

    if (runtime_new_ == nullptr) {
        error = "DWRT runtime is not loaded";
        counters_.runtime_create_failures += 1;
        return false;
    }

    runtime_ = runtime_new_();
    if (runtime_ == nullptr) {
        error = "dwrt_runtime_new returned null";
        counters_.runtime_create_failures += 1;
        return false;
    }
    return true;
}

void DwrtShadowShim::destroy_runtime() {
    if (runtime_ != nullptr && runtime_free_ != nullptr) {
        runtime_free_(runtime_);
    }
    runtime_ = nullptr;
}

bool DwrtShadowShim::loaded() const {
    return module_ != nullptr;
}

bool DwrtShadowShim::has_runtime() const {
    return runtime_ != nullptr;
}

std::uint32_t DwrtShadowShim::abi_version() const {
    if (abi_version_ == nullptr) {
        return 0;
    }
    return abi_version_();
}

void DwrtShadowShim::set_timing_enabled(bool enabled) {
    timing_enabled_ = enabled;
}

void DwrtShadowShim::set_slow_threshold_ns(std::uint64_t threshold_ns) {
    if (counters_.qpc_ticks_per_second == 0 || threshold_ns == 0) {
        counters_.slow_threshold_ticks = 0;
        return;
    }
    counters_.slow_threshold_ticks =
        (threshold_ns * counters_.qpc_ticks_per_second) / 1'000'000'000ULL;
    if (counters_.slow_threshold_ticks == 0) {
        counters_.slow_threshold_ticks = 1;
    }
}

bool DwrtShadowShim::add_net_user_fast(std::int32_t user_msg_id) {
    return runtime_ != nullptr && net_add_user_fast_ != nullptr &&
           net_add_user_fast_(runtime_, user_msg_id) != 0;
}

bool DwrtShadowShim::add_net_serialized(std::int32_t direction, std::int32_t msg_id) {
    return runtime_ != nullptr && net_add_serialized_ != nullptr &&
           net_add_serialized_(runtime_, direction, msg_id) != 0;
}

void DwrtShadowShim::set_usercmd_mount_mask(std::uint32_t mask) {
    if (runtime_ != nullptr && usercmd_set_mount_mask_ != nullptr) {
        usercmd_set_mount_mask_(runtime_, mask);
    }
}

void DwrtShadowShim::set_probe_mount_mask(std::uint32_t mask) {
    if (runtime_ != nullptr && probe_set_mount_mask_ != nullptr) {
        probe_set_mount_mask_(runtime_, mask);
    }
}

std::uint32_t DwrtShadowShim::shadow_route_net(
    std::int32_t direction,
    std::int32_t msg_id,
    std::uint8_t has_user_msg_id,
    std::int32_t user_msg_id) {
    const std::uint64_t start = timing_enabled_ ? qpc_now_ticks() : 0;

    std::uint32_t route = DWRT_ROUTE_NO_INTEREST;
    if (runtime_ != nullptr && net_route_ != nullptr) {
        route = net_route_(runtime_, direction, msg_id, has_user_msg_id, user_msg_id);
    }
    count_route(counters_.net_routes, std::size(counters_.net_routes), route);

    if (timing_enabled_) {
        record_duration(
            qpc_now_ticks() - start,
            counters_.slow_threshold_ticks,
            counters_.net_route_total_ticks,
            counters_.net_route_max_ticks,
            counters_.net_route_slow_calls);
    }
    return route;
}

std::uint32_t DwrtShadowShim::shadow_route_usercmd() {
    const std::uint64_t start = timing_enabled_ ? qpc_now_ticks() : 0;

    std::uint32_t route = DWRT_USERCMD_ROUTE_NO_WORK;
    if (runtime_ != nullptr && usercmd_route_ != nullptr) {
        route = usercmd_route_(runtime_);
    }
    count_route(counters_.usercmd_routes, std::size(counters_.usercmd_routes), route);

    if (timing_enabled_) {
        record_duration(
            qpc_now_ticks() - start,
            counters_.slow_threshold_ticks,
            counters_.usercmd_route_total_ticks,
            counters_.usercmd_route_max_ticks,
            counters_.usercmd_route_slow_calls);
    }
    return route;
}

std::uint32_t DwrtShadowShim::probe_record_damage(const DwrtFastDamageNative& event) {
    const std::uint64_t start = timing_enabled_ ? qpc_now_ticks() : 0;

    std::uint32_t route = DWRT_PROBE_ROUTE_NO_INTEREST;
    if (runtime_ != nullptr && probe_record_damage_ != nullptr) {
        route = probe_record_damage_(runtime_, &event);
    }
    count_route(counters_.probe_routes, std::size(counters_.probe_routes), route);

    if (timing_enabled_) {
        record_duration(
            qpc_now_ticks() - start,
            counters_.slow_threshold_ticks,
            counters_.probe_route_total_ticks,
            counters_.probe_route_max_ticks,
            counters_.probe_route_slow_calls);
    }
    return route;
}

std::uint32_t DwrtShadowShim::probe_record_entity_input(const DwrtFastEntityIoNative& event) {
    const std::uint64_t start = timing_enabled_ ? qpc_now_ticks() : 0;

    std::uint32_t route = DWRT_PROBE_ROUTE_NO_INTEREST;
    if (runtime_ != nullptr && probe_record_entity_input_ != nullptr) {
        route = probe_record_entity_input_(runtime_, &event);
    }
    count_route(counters_.probe_routes, std::size(counters_.probe_routes), route);

    if (timing_enabled_) {
        record_duration(
            qpc_now_ticks() - start,
            counters_.slow_threshold_ticks,
            counters_.probe_route_total_ticks,
            counters_.probe_route_max_ticks,
            counters_.probe_route_slow_calls);
    }
    return route;
}

std::uint32_t DwrtShadowShim::probe_record_entity_output(const DwrtFastEntityIoNative& event) {
    const std::uint64_t start = timing_enabled_ ? qpc_now_ticks() : 0;

    std::uint32_t route = DWRT_PROBE_ROUTE_NO_INTEREST;
    if (runtime_ != nullptr && probe_record_entity_output_ != nullptr) {
        route = probe_record_entity_output_(runtime_, &event);
    }
    count_route(counters_.probe_routes, std::size(counters_.probe_routes), route);

    if (timing_enabled_) {
        record_duration(
            qpc_now_ticks() - start,
            counters_.slow_threshold_ticks,
            counters_.probe_route_total_ticks,
            counters_.probe_route_max_ticks,
            counters_.probe_route_slow_calls);
    }
    return route;
}

std::uint32_t DwrtShadowShim::probe_record_entity_touch(const DwrtFastEntityTouchNative& event) {
    const std::uint64_t start = timing_enabled_ ? qpc_now_ticks() : 0;

    std::uint32_t route = DWRT_PROBE_ROUTE_NO_INTEREST;
    if (runtime_ != nullptr && probe_record_entity_touch_ != nullptr) {
        route = probe_record_entity_touch_(runtime_, &event);
    }
    count_route(counters_.probe_routes, std::size(counters_.probe_routes), route);

    if (timing_enabled_) {
        record_duration(
            qpc_now_ticks() - start,
            counters_.slow_threshold_ticks,
            counters_.probe_route_total_ticks,
            counters_.probe_route_max_ticks,
            counters_.probe_route_slow_calls);
    }
    return route;
}

std::uint8_t DwrtShadowShim::probe_snapshot(DwrtProbeCountersNative& out) {
    if (runtime_ == nullptr || probe_snapshot_ == nullptr) {
        return 0;
    }
    return probe_snapshot_(runtime_, &out);
}

void DwrtShadowShim::probe_reset_counters() {
    if (runtime_ != nullptr && probe_reset_counters_ != nullptr) {
        probe_reset_counters_(runtime_);
    }
}

const DwrtShadowCounters& DwrtShadowShim::counters() const {
    return counters_;
}

}  // namespace dwrt::shim
