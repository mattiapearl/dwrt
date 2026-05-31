#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dwrt::host {

struct SignatureDescriptor {
    const char* name;
    const char* module;
    const char* surface;
    const char* pattern;
    std::uint32_t expected_rva;
    bool required;
    const char* reference;
};

std::span<const SignatureDescriptor> default_probe_signatures();

}  // namespace dwrt::host
