#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwrt::host {

struct PatternByte {
    std::uint8_t value = 0;
    bool wildcard = false;
};

struct CompiledPattern {
    std::vector<PatternByte> bytes;
};

struct SectionRange {
    std::string name;
    std::uint32_t virtual_address = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t raw_offset = 0;
    std::uint32_t raw_size = 0;
    std::uint32_t characteristics = 0;
};

class PeImageFile {
public:
    static std::optional<PeImageFile> load(const std::filesystem::path& path, std::string& error);

    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const;
    [[nodiscard]] std::uint64_t image_base() const;
    [[nodiscard]] std::uint32_t image_size() const;
    [[nodiscard]] std::uint32_t timestamp() const;
    [[nodiscard]] const std::vector<SectionRange>& sections() const;
    [[nodiscard]] std::optional<std::uint32_t> rva_from_file_offset(std::size_t offset) const;

private:
    std::filesystem::path path_;
    std::vector<std::uint8_t> bytes_;
    std::uint64_t image_base_ = 0;
    std::uint32_t image_size_ = 0;
    std::uint32_t timestamp_ = 0;
    std::vector<SectionRange> sections_;
};

class PeModuleView {
public:
    static std::optional<PeModuleView> from_module_handle(
        void* module_handle,
        std::filesystem::path path_hint,
        std::string& error);

    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] const std::uint8_t* base() const;
    [[nodiscard]] std::uint64_t image_base() const;
    [[nodiscard]] std::uint32_t image_size() const;
    [[nodiscard]] std::uint32_t timestamp() const;
    [[nodiscard]] const std::vector<SectionRange>& sections() const;

private:
    std::filesystem::path path_;
    const std::uint8_t* base_ = nullptr;
    std::uint64_t image_base_ = 0;
    std::uint32_t image_size_ = 0;
    std::uint32_t timestamp_ = 0;
    std::vector<SectionRange> sections_;
};

std::optional<CompiledPattern> compile_pattern(std::string_view pattern, std::string& error);
std::vector<std::size_t> find_pattern(
    std::span<const std::uint8_t> haystack,
    const CompiledPattern& pattern,
    std::size_t max_matches);

std::vector<std::uint32_t> find_pattern_rvas(
    const PeModuleView& module,
    const CompiledPattern& pattern,
    std::size_t max_matches);

std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes);
std::string json_escape(std::string_view value);

}  // namespace dwrt::host
