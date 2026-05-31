#include "dwrt_signature_scanner.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace dwrt::host {
namespace {

bool is_hex_digit(char value) {
    return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

std::optional<std::uint8_t> parse_hex_byte(std::string_view token) {
    if (token.size() != 2 || !is_hex_digit(token[0]) || !is_hex_digit(token[1])) {
        return std::nullopt;
    }

    auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(10 + value - 'a');
        }
        return static_cast<std::uint8_t>(10 + value - 'A');
    };

    return static_cast<std::uint8_t>((nibble(token[0]) << 4U) | nibble(token[1]));
}

std::string section_name(const IMAGE_SECTION_HEADER& section) {
    char buffer[9] = {};
    std::copy_n(reinterpret_cast<const char*>(section.Name), 8, buffer);
    return std::string(buffer);
}

}  // namespace

std::optional<PeImageFile> PeImageFile::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        std::ostringstream out;
        out << "failed to open PE image: " << path.string();
        error = out.str();
        return std::nullopt;
    }

    const std::streamoff end = input.tellg();
    if (end <= 0 || end > static_cast<std::streamoff>(std::numeric_limits<std::uint32_t>::max())) {
        error = "PE image has invalid size";
        return std::nullopt;
    }

    PeImageFile image;
    image.path_ = path;
    image.bytes_.resize(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(image.bytes_.data()), end);
    if (!input) {
        error = "failed to read PE image";
        return std::nullopt;
    }

    if (image.bytes_.size() < sizeof(IMAGE_DOS_HEADER)) {
        error = "PE image too small for DOS header";
        return std::nullopt;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.bytes_.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        error = "PE image missing MZ signature";
        return std::nullopt;
    }
    if (dos->e_lfanew < 0 || static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image.bytes_.size()) {
        error = "PE image has invalid NT header offset";
        return std::nullopt;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.bytes_.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        error = "PE image missing NT signature";
        return std::nullopt;
    }
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        error = "PE image is not PE32+";
        return std::nullopt;
    }

    image.image_base_ = nt->OptionalHeader.ImageBase;
    image.image_size_ = nt->OptionalHeader.SizeOfImage;
    image.timestamp_ = nt->FileHeader.TimeDateStamp;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (std::uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        SectionRange range;
        range.name = section_name(section[i]);
        range.virtual_address = section[i].VirtualAddress;
        range.virtual_size = section[i].Misc.VirtualSize;
        range.raw_offset = section[i].PointerToRawData;
        range.raw_size = section[i].SizeOfRawData;
        range.characteristics = section[i].Characteristics;
        image.sections_.push_back(range);
    }

    return image;
}

const std::filesystem::path& PeImageFile::path() const {
    return path_;
}

std::span<const std::uint8_t> PeImageFile::bytes() const {
    return bytes_;
}

std::uint64_t PeImageFile::image_base() const {
    return image_base_;
}

std::uint32_t PeImageFile::image_size() const {
    return image_size_;
}

std::uint32_t PeImageFile::timestamp() const {
    return timestamp_;
}

const std::vector<SectionRange>& PeImageFile::sections() const {
    return sections_;
}

std::optional<std::uint32_t> PeImageFile::rva_from_file_offset(std::size_t offset) const {
    if (offset > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }

    for (const SectionRange& section : sections_) {
        const std::size_t raw_begin = section.raw_offset;
        const std::size_t raw_end = raw_begin + section.raw_size;
        if (offset >= raw_begin && offset < raw_end) {
            const std::size_t delta = offset - raw_begin;
            if (delta > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - section.virtual_address)) {
                return std::nullopt;
            }
            return section.virtual_address + static_cast<std::uint32_t>(delta);
        }
    }

    const std::size_t first_section_offset = sections_.empty() ? 0 : sections_.front().raw_offset;
    if (offset < first_section_offset) {
        return static_cast<std::uint32_t>(offset);
    }
    return std::nullopt;
}

std::optional<PeModuleView> PeModuleView::from_module_handle(
    void* module_handle,
    std::filesystem::path path_hint,
    std::string& error) {
    if (module_handle == nullptr) {
        error = "module handle is null";
        return std::nullopt;
    }

    const auto* base = static_cast<const std::uint8_t*>(module_handle);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        error = "loaded module missing MZ signature";
        return std::nullopt;
    }
    if (dos->e_lfanew < 0) {
        error = "loaded module has invalid NT header offset";
        return std::nullopt;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        error = "loaded module missing NT signature";
        return std::nullopt;
    }
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        error = "loaded module is not PE32+";
        return std::nullopt;
    }

    PeModuleView module;
    module.path_ = std::move(path_hint);
    module.base_ = base;
    module.image_base_ = reinterpret_cast<std::uint64_t>(base);
    module.image_size_ = nt->OptionalHeader.SizeOfImage;
    module.timestamp_ = nt->FileHeader.TimeDateStamp;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (std::uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        SectionRange range;
        range.name = section_name(section[i]);
        range.virtual_address = section[i].VirtualAddress;
        range.virtual_size = section[i].Misc.VirtualSize;
        range.raw_offset = section[i].PointerToRawData;
        range.raw_size = section[i].SizeOfRawData;
        range.characteristics = section[i].Characteristics;
        module.sections_.push_back(range);
    }

    return module;
}

const std::filesystem::path& PeModuleView::path() const {
    return path_;
}

const std::uint8_t* PeModuleView::base() const {
    return base_;
}

std::uint64_t PeModuleView::image_base() const {
    return image_base_;
}

std::uint32_t PeModuleView::image_size() const {
    return image_size_;
}

std::uint32_t PeModuleView::timestamp() const {
    return timestamp_;
}

const std::vector<SectionRange>& PeModuleView::sections() const {
    return sections_;
}

std::optional<CompiledPattern> compile_pattern(std::string_view pattern, std::string& error) {
    CompiledPattern compiled;
    std::size_t pos = 0;
    while (pos < pattern.size()) {
        while (pos < pattern.size() && std::isspace(static_cast<unsigned char>(pattern[pos])) != 0) {
            ++pos;
        }
        if (pos >= pattern.size()) {
            break;
        }

        const std::size_t start = pos;
        while (pos < pattern.size() && std::isspace(static_cast<unsigned char>(pattern[pos])) == 0) {
            ++pos;
        }
        const std::string_view token = pattern.substr(start, pos - start);

        if (token == "?" || token == "??") {
            compiled.bytes.push_back(PatternByte{0, true});
            continue;
        }

        const std::optional<std::uint8_t> byte = parse_hex_byte(token);
        if (!byte.has_value()) {
            std::ostringstream out;
            out << "invalid signature token: " << token;
            error = out.str();
            return std::nullopt;
        }
        compiled.bytes.push_back(PatternByte{*byte, false});
    }

    if (compiled.bytes.empty()) {
        error = "empty signature pattern";
        return std::nullopt;
    }
    return compiled;
}

std::vector<std::size_t> find_pattern(
    std::span<const std::uint8_t> haystack,
    const CompiledPattern& pattern,
    std::size_t max_matches) {
    std::vector<std::size_t> matches;
    if (pattern.bytes.empty() || haystack.size() < pattern.bytes.size() || max_matches == 0) {
        return matches;
    }

    const std::size_t last_start = haystack.size() - pattern.bytes.size();
    for (std::size_t offset = 0; offset <= last_start; ++offset) {
        bool matched = true;
        for (std::size_t index = 0; index < pattern.bytes.size(); ++index) {
            const PatternByte& byte = pattern.bytes[index];
            if (!byte.wildcard && haystack[offset + index] != byte.value) {
                matched = false;
                break;
            }
        }
        if (matched) {
            matches.push_back(offset);
            if (matches.size() >= max_matches) {
                break;
            }
        }
    }
    return matches;
}

std::vector<std::uint32_t> find_pattern_rvas(
    const PeModuleView& module,
    const CompiledPattern& pattern,
    std::size_t max_matches) {
    std::vector<std::uint32_t> matches;
    if (pattern.bytes.empty() || max_matches == 0 || module.base() == nullptr) {
        return matches;
    }

    for (const SectionRange& section : module.sections()) {
        if ((section.characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE)) == 0) {
            continue;
        }

        const std::uint32_t section_size = section.virtual_size != 0 ? section.virtual_size : section.raw_size;
        if (section_size == 0 || section.virtual_address >= module.image_size()) {
            continue;
        }

        const std::uint32_t max_size = module.image_size() - section.virtual_address;
        const std::uint32_t scan_size = std::min(section_size, max_size);
        if (scan_size < pattern.bytes.size()) {
            continue;
        }

        const auto* section_base = module.base() + section.virtual_address;
        const std::span<const std::uint8_t> bytes(section_base, scan_size);
        const std::vector<std::size_t> section_matches =
            find_pattern(bytes, pattern, max_matches - matches.size());
        for (const std::size_t offset : section_matches) {
            if (offset <= std::numeric_limits<std::uint32_t>::max() - section.virtual_address) {
                matches.push_back(section.virtual_address + static_cast<std::uint32_t>(offset));
            }
            if (matches.size() >= max_matches) {
                return matches;
            }
        }
    }

    return matches;
}

std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string json_escape(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) {
                std::ostringstream escaped;
                escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch));
                output += escaped.str();
            }
            else {
                output += ch;
            }
            break;
        }
    }
    return output;
}

}  // namespace dwrt::host
