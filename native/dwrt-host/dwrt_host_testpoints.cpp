#include "dwrt_host_testpoints.hpp"

#include <algorithm>
#include <atomic>

namespace dwrt::host {
namespace {

std::atomic<std::uint64_t> g_initialize_calls{0};
std::atomic<std::uint64_t> g_initialize_reentrant_rejects{0};
std::atomic<std::uint64_t> g_shutdown_calls{0};
std::atomic<std::uint64_t> g_callback_entries{0};
std::atomic<std::uint64_t> g_callback_recursive_entries{0};
std::atomic<std::uint32_t> g_callback_max_depth{0};
thread_local std::uint32_t t_callback_depth = 0;

void update_max_depth(std::uint32_t depth) {
    std::uint32_t previous = g_callback_max_depth.load(std::memory_order_relaxed);
    while (depth > previous &&
           !g_callback_max_depth.compare_exchange_weak(
               previous,
               depth,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

}  // namespace

void record_initialize_call() {
    g_initialize_calls.fetch_add(1, std::memory_order_relaxed);
}

void record_initialize_reentrant_reject() {
    g_initialize_reentrant_rejects.fetch_add(1, std::memory_order_relaxed);
}

void record_shutdown_call() {
    g_shutdown_calls.fetch_add(1, std::memory_order_relaxed);
}

void reset_testpoints() {
    g_initialize_calls.store(0, std::memory_order_relaxed);
    g_initialize_reentrant_rejects.store(0, std::memory_order_relaxed);
    g_shutdown_calls.store(0, std::memory_order_relaxed);
    g_callback_entries.store(0, std::memory_order_relaxed);
    g_callback_recursive_entries.store(0, std::memory_order_relaxed);
    g_callback_max_depth.store(0, std::memory_order_relaxed);
    t_callback_depth = 0;
}

HostTestpointSnapshot testpoint_snapshot() {
    HostTestpointSnapshot snapshot;
    snapshot.initialize_calls = g_initialize_calls.load(std::memory_order_relaxed);
    snapshot.initialize_reentrant_rejects =
        g_initialize_reentrant_rejects.load(std::memory_order_relaxed);
    snapshot.shutdown_calls = g_shutdown_calls.load(std::memory_order_relaxed);
    snapshot.callback_entries = g_callback_entries.load(std::memory_order_relaxed);
    snapshot.callback_recursive_entries =
        g_callback_recursive_entries.load(std::memory_order_relaxed);
    snapshot.callback_current_depth = t_callback_depth;
    snapshot.callback_max_depth = g_callback_max_depth.load(std::memory_order_relaxed);
    return snapshot;
}

CallbackScope::CallbackScope(std::uint32_t point_id)
    : point_id_(point_id), depth_(t_callback_depth + 1), recursive_(t_callback_depth != 0) {
    t_callback_depth = depth_;
    g_callback_entries.fetch_add(1, std::memory_order_relaxed);
    if (recursive_) {
        g_callback_recursive_entries.fetch_add(1, std::memory_order_relaxed);
    }
    update_max_depth(depth_);
}

CallbackScope::~CallbackScope() {
    if (t_callback_depth != 0) {
        t_callback_depth -= 1;
    }
}

bool CallbackScope::recursive() const {
    return recursive_;
}

std::uint32_t CallbackScope::depth() const {
    return depth_;
}

std::uint32_t CallbackScope::point_id() const {
    return point_id_;
}

}  // namespace dwrt::host
