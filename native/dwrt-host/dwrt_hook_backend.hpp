#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "safetyhook.hpp"

namespace dwrt::host {

struct InlineHookRecord {
    std::string name;
    void* target = nullptr;
    void* detour = nullptr;
    void* original = nullptr;
    std::string error;
    SafetyHookInline hook;

    InlineHookRecord() = default;
    InlineHookRecord(const InlineHookRecord&) = delete;
    InlineHookRecord& operator=(const InlineHookRecord&) = delete;
    InlineHookRecord(InlineHookRecord&&) noexcept = default;
    InlineHookRecord& operator=(InlineHookRecord&&) noexcept = default;
};

class HookBackend {
public:
    HookBackend() = default;
    HookBackend(const HookBackend&) = delete;
    HookBackend& operator=(const HookBackend&) = delete;
    ~HookBackend();

    bool install_inline(std::string name, void* target, void* detour, void** original_out, std::string& error);
    void reset();

    [[nodiscard]] std::size_t installed_count() const;
    [[nodiscard]] const std::vector<InlineHookRecord>& records() const;

    template <typename Fn>
    [[nodiscard]] Fn original(std::string_view name) const {
        for (const InlineHookRecord& record : records_) {
            if (record.name == name && record.original != nullptr) {
                return reinterpret_cast<Fn>(record.original);
            }
        }
        return nullptr;
    }

private:
    std::vector<InlineHookRecord> records_;
};

std::string safetyhook_inline_error_to_string(const SafetyHookInline::Error& error);

}  // namespace dwrt::host
