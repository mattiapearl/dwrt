#include "dwrt_hook_backend.hpp"

#include <sstream>
#include <utility>

namespace dwrt::host {

HookBackend::~HookBackend() {
    reset();
}

bool HookBackend::install_inline(
    std::string name,
    void* target,
    void* detour,
    void** original_out,
    std::string& error) {
    if (target == nullptr || detour == nullptr || original_out == nullptr) {
        error = "inline hook target/detour/original_out is null";
        return false;
    }

    auto hook_result = SafetyHookInline::create(target, detour, SafetyHookInline::StartDisabled);
    if (!hook_result.has_value()) {
        error = safetyhook_inline_error_to_string(hook_result.error());
        return false;
    }

    InlineHookRecord record;
    record.name = std::move(name);
    record.target = target;
    record.detour = detour;
    record.hook = std::move(*hook_result);
    record.original = record.hook.original<void*>();
    *original_out = record.original;

    auto enable_result = record.hook.enable();
    if (!enable_result.has_value()) {
        *original_out = nullptr;
        error = safetyhook_inline_error_to_string(enable_result.error());
        return false;
    }

    records_.push_back(std::move(record));
    return true;
}

void HookBackend::reset() {
    records_.clear();
}

std::size_t HookBackend::installed_count() const {
    return records_.size();
}

const std::vector<InlineHookRecord>& HookBackend::records() const {
    return records_;
}

std::string safetyhook_inline_error_to_string(const SafetyHookInline::Error& error) {
    const char* kind = "unknown";
    switch (error.type) {
    case SafetyHookInline::Error::BAD_ALLOCATION: kind = "bad allocation"; break;
    case SafetyHookInline::Error::FAILED_TO_DECODE_INSTRUCTION: kind = "failed to decode instruction"; break;
    case SafetyHookInline::Error::SHORT_JUMP_IN_TRAMPOLINE: kind = "short jump in trampoline"; break;
    case SafetyHookInline::Error::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE: kind = "ip-relative instruction out of range"; break;
    case SafetyHookInline::Error::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE: kind = "unsupported instruction in trampoline"; break;
    case SafetyHookInline::Error::FAILED_TO_UNPROTECT: kind = "failed to unprotect"; break;
    case SafetyHookInline::Error::NOT_ENOUGH_SPACE: kind = "not enough space"; break;
    default: break;
    }

    std::ostringstream out;
    out << kind;
    if (error.type != SafetyHookInline::Error::BAD_ALLOCATION) {
        out << " at " << static_cast<const void*>(error.ip);
    }
    return out.str();
}

}  // namespace dwrt::host
