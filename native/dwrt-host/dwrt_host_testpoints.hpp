#pragma once

#include <cstdint>

namespace dwrt::host {

struct HostTestpointSnapshot {
    std::uint64_t initialize_calls = 0;
    std::uint64_t initialize_reentrant_rejects = 0;
    std::uint64_t shutdown_calls = 0;
    std::uint64_t callback_entries = 0;
    std::uint64_t callback_recursive_entries = 0;
    std::uint32_t callback_current_depth = 0;
    std::uint32_t callback_max_depth = 0;
};

void record_initialize_call();
void record_initialize_reentrant_reject();
void record_shutdown_call();
void reset_testpoints();
HostTestpointSnapshot testpoint_snapshot();

class CallbackScope {
public:
    explicit CallbackScope(std::uint32_t point_id);
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;
    ~CallbackScope();

    [[nodiscard]] bool recursive() const;
    [[nodiscard]] std::uint32_t depth() const;
    [[nodiscard]] std::uint32_t point_id() const;

private:
    std::uint32_t point_id_ = 0;
    std::uint32_t depth_ = 0;
    bool recursive_ = false;
};

}  // namespace dwrt::host
