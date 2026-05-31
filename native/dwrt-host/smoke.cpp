#include "dwrt_probe_manifest.hpp"
#include "dwrt_shadow_shim.hpp"
#include "dwrt_signature_scanner.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const wchar_t* kDefaultServerDll =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Deadlock\\game\\citadel\\bin\\win64\\server.dll";

struct Options {
    std::filesystem::path server_path = kDefaultServerDll;
    std::filesystem::path runtime_path;
    std::filesystem::path output_path;
    bool require_runtime = false;
    bool allow_expected_rva_drift = false;
    bool mapped_module_check = false;
};

struct ResolvedSignature {
    const dwrt::host::SignatureDescriptor* descriptor = nullptr;
    std::string error;
    std::vector<std::size_t> file_offsets;
    std::optional<std::uint32_t> rva;
    bool unique = false;
    bool expected_rva_ok = false;
};

struct MappedResolvedSignature {
    const dwrt::host::SignatureDescriptor* descriptor = nullptr;
    std::string error;
    std::vector<std::uint32_t> rvas;
    std::optional<std::uint32_t> rva;
    bool unique = false;
    bool expected_rva_ok = false;
};

std::string narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string output(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        needed,
        nullptr,
        nullptr);
    return output;
}

std::string hex_u64(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << value;
    return out.str();
}

int fail(int code, const std::string& message) {
    std::cerr << "[dwrt-host-smoke] ERROR: " << message << '\n';
    return code;
}

std::string windows_error_message(const char* prefix) {
    std::ostringstream out;
    out << prefix << " (GetLastError=" << GetLastError() << ")";
    return out.str();
}

void print_usage() {
    std::cerr << "usage: dwrt_host_smoke.exe [--server <server.dll>] [--runtime <dwrt_runtime.dll>] "
              << "[--require-runtime] [--output <summary.json>] [--allow-expected-rva-drift] "
              << "[--mapped-module-check]\n";
}

std::optional<Options> parse_options(int argc, wchar_t** argv, std::string& error) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring arg = argv[index];
        auto require_value = [&](const wchar_t* name) -> std::optional<std::wstring> {
            if (index + 1 >= argc) {
                std::ostringstream out;
                out << "missing value for " << narrow(name);
                error = out.str();
                return std::nullopt;
            }
            ++index;
            return std::wstring(argv[index]);
        };

        if (arg == L"--server") {
            const auto value = require_value(L"--server");
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.server_path = *value;
        }
        else if (arg == L"--runtime") {
            const auto value = require_value(L"--runtime");
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.runtime_path = *value;
        }
        else if (arg == L"--output") {
            const auto value = require_value(L"--output");
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.output_path = *value;
        }
        else if (arg == L"--require-runtime") {
            options.require_runtime = true;
        }
        else if (arg == L"--allow-expected-rva-drift") {
            options.allow_expected_rva_drift = true;
        }
        else if (arg == L"--mapped-module-check") {
            options.mapped_module_check = true;
        }
        else if (arg == L"--help" || arg == L"-h") {
            print_usage();
            return std::nullopt;
        }
        else {
            error = "unknown argument: " + narrow(arg);
            return std::nullopt;
        }
    }
    return options;
}

ResolvedSignature resolve_signature(
    const dwrt::host::PeImageFile& image,
    const dwrt::host::SignatureDescriptor& descriptor) {
    ResolvedSignature resolved;
    resolved.descriptor = &descriptor;

    std::string error;
    const std::optional<dwrt::host::CompiledPattern> pattern = dwrt::host::compile_pattern(descriptor.pattern, error);
    if (!pattern.has_value()) {
        resolved.error = error;
        return resolved;
    }

    resolved.file_offsets = dwrt::host::find_pattern(image.bytes(), *pattern, 3);
    resolved.unique = resolved.file_offsets.size() == 1;
    if (resolved.unique) {
        resolved.rva = image.rva_from_file_offset(resolved.file_offsets[0]);
        resolved.expected_rva_ok = descriptor.expected_rva == 0 ||
            (resolved.rva.has_value() && resolved.rva.value() == descriptor.expected_rva);
        if (!resolved.rva.has_value()) {
            resolved.error = "unique match could not be converted to RVA";
        }
    }
    else if (resolved.file_offsets.empty()) {
        resolved.error = "signature not found";
    }
    else {
        resolved.error = "signature matched more than once";
    }
    return resolved;
}

MappedResolvedSignature resolve_mapped_signature(
    const dwrt::host::PeModuleView& module,
    const dwrt::host::SignatureDescriptor& descriptor) {
    MappedResolvedSignature resolved;
    resolved.descriptor = &descriptor;

    std::string error;
    const std::optional<dwrt::host::CompiledPattern> pattern = dwrt::host::compile_pattern(descriptor.pattern, error);
    if (!pattern.has_value()) {
        resolved.error = error;
        return resolved;
    }

    resolved.rvas = dwrt::host::find_pattern_rvas(module, *pattern, 3);
    resolved.unique = resolved.rvas.size() == 1;
    if (resolved.unique) {
        resolved.rva = resolved.rvas[0];
        resolved.expected_rva_ok = descriptor.expected_rva == 0 || resolved.rva.value() == descriptor.expected_rva;
    }
    else if (resolved.rvas.empty()) {
        resolved.error = "signature not found in mapped module";
    }
    else {
        resolved.error = "signature matched more than once in mapped module";
    }
    return resolved;
}

std::string build_json_summary(
    const Options& options,
    const dwrt::host::PeImageFile& image,
    const std::vector<ResolvedSignature>& signatures,
    const std::vector<MappedResolvedSignature>& mapped_signatures,
    bool mapped_module_checked,
    bool mapped_module_loaded,
    const std::string& mapped_module_error,
    std::size_t mapped_required_failures,
    bool runtime_loaded,
    std::uint32_t abi_version,
    const std::string& runtime_error,
    bool runtime_probe_checked,
    bool runtime_probe_ok,
    const std::string& runtime_probe_error,
    std::size_t required_failures) {
    const std::uint64_t image_hash = dwrt::host::fnv1a64(image.bytes());

    std::ostringstream out;
    const bool summary_ok = required_failures == 0 && mapped_required_failures == 0 &&
        (!runtime_probe_checked || runtime_probe_ok);

    out << "{\n";
    out << "  \"ok\": " << (summary_ok ? "true" : "false") << ",\n";
    out << "  \"requiredFailures\": " << required_failures << ",\n";
    out << "  \"mappedRequiredFailures\": " << mapped_required_failures << ",\n";
    out << "  \"server\": {\n";
    out << "    \"path\": \"" << dwrt::host::json_escape(image.path().string()) << "\",\n";
    out << "    \"sizeBytes\": " << image.bytes().size() << ",\n";
    out << "    \"imageBase\": \"" << hex_u64(image.image_base()) << "\",\n";
    out << "    \"imageSize\": \"" << hex_u64(image.image_size()) << "\",\n";
    out << "    \"timestamp\": \"" << hex_u64(image.timestamp()) << "\",\n";
    out << "    \"fnv1a64\": \"" << hex_u64(image_hash) << "\"\n";
    out << "  },\n";
    out << "  \"runtime\": {\n";
    out << "    \"path\": \"" << dwrt::host::json_escape(options.runtime_path.string()) << "\",\n";
    out << "    \"loaded\": " << (runtime_loaded ? "true" : "false") << ",\n";
    out << "    \"abiVersion\": " << abi_version << ",\n";
    out << "    \"probeChecked\": " << (runtime_probe_checked ? "true" : "false") << ",\n";
    out << "    \"probeOk\": " << (runtime_probe_ok ? "true" : "false") << ",\n";
    out << "    \"probeError\": \"" << dwrt::host::json_escape(runtime_probe_error) << "\",\n";
    out << "    \"error\": \"" << dwrt::host::json_escape(runtime_error) << "\"\n";
    out << "  },\n";
    out << "  \"mappedModule\": {\n";
    out << "    \"checked\": " << (mapped_module_checked ? "true" : "false") << ",\n";
    out << "    \"loaded\": " << (mapped_module_loaded ? "true" : "false") << ",\n";
    out << "    \"requiredFailures\": " << mapped_required_failures << ",\n";
    out << "    \"error\": \"" << dwrt::host::json_escape(mapped_module_error) << "\",\n";
    out << "    \"signatures\": [\n";
    for (std::size_t index = 0; index < mapped_signatures.size(); ++index) {
        const MappedResolvedSignature& sig = mapped_signatures[index];
        const auto& desc = *sig.descriptor;
        out << "      {\n";
        out << "        \"name\": \"" << dwrt::host::json_escape(desc.name) << "\",\n";
        out << "        \"required\": " << (desc.required ? "true" : "false") << ",\n";
        out << "        \"matchCount\": " << sig.rvas.size() << ",\n";
        out << "        \"unique\": " << (sig.unique ? "true" : "false") << ",\n";
        out << "        \"rva\": ";
        if (sig.rva.has_value()) {
            out << "\"" << hex_u64(sig.rva.value()) << "\"";
        }
        else {
            out << "null";
        }
        out << ",\n";
        out << "        \"expectedRva\": \"" << hex_u64(desc.expected_rva) << "\",\n";
        out << "        \"expectedRvaOk\": " << (sig.expected_rva_ok ? "true" : "false") << ",\n";
        out << "        \"error\": \"" << dwrt::host::json_escape(sig.error) << "\"\n";
        out << "      }" << (index + 1 == mapped_signatures.size() ? "\n" : ",\n");
    }
    out << "    ]\n";
    out << "  },\n";
    out << "  \"signatures\": [\n";

    for (std::size_t index = 0; index < signatures.size(); ++index) {
        const ResolvedSignature& sig = signatures[index];
        const auto& desc = *sig.descriptor;
        out << "    {\n";
        out << "      \"name\": \"" << dwrt::host::json_escape(desc.name) << "\",\n";
        out << "      \"module\": \"" << dwrt::host::json_escape(desc.module) << "\",\n";
        out << "      \"surface\": \"" << dwrt::host::json_escape(desc.surface) << "\",\n";
        out << "      \"required\": " << (desc.required ? "true" : "false") << ",\n";
        out << "      \"matchCount\": " << sig.file_offsets.size() << ",\n";
        out << "      \"unique\": " << (sig.unique ? "true" : "false") << ",\n";
        out << "      \"fileOffset\": ";
        if (sig.unique) {
            out << "\"" << hex_u64(sig.file_offsets[0]) << "\"";
        }
        else {
            out << "null";
        }
        out << ",\n";
        out << "      \"rva\": ";
        if (sig.rva.has_value()) {
            out << "\"" << hex_u64(sig.rva.value()) << "\"";
        }
        else {
            out << "null";
        }
        out << ",\n";
        out << "      \"va\": ";
        if (sig.rva.has_value()) {
            out << "\"" << hex_u64(image.image_base() + sig.rva.value()) << "\"";
        }
        else {
            out << "null";
        }
        out << ",\n";
        out << "      \"expectedRva\": \"" << hex_u64(desc.expected_rva) << "\",\n";
        out << "      \"expectedRvaOk\": " << (sig.expected_rva_ok ? "true" : "false") << ",\n";
        out << "      \"reference\": \"" << dwrt::host::json_escape(desc.reference) << "\",\n";
        out << "      \"error\": \"" << dwrt::host::json_escape(sig.error) << "\"\n";
        out << "    }" << (index + 1 == signatures.size() ? "\n" : ",\n");
    }

    out << "  ]\n";
    out << "}\n";
    return out.str();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::string parse_error;
    const std::optional<Options> parsed_options = parse_options(argc, argv, parse_error);
    if (!parsed_options.has_value()) {
        if (!parse_error.empty()) {
            print_usage();
            return fail(64, parse_error);
        }
        return 0;
    }
    const Options options = *parsed_options;

    bool runtime_loaded = false;
    std::uint32_t abi_version = 0;
    bool runtime_probe_checked = false;
    bool runtime_probe_ok = false;
    std::string runtime_error;
    std::string runtime_probe_error;
    dwrt::shim::DwrtShadowShim runtime;
    if (!options.runtime_path.empty()) {
        if (!runtime.load(options.runtime_path.c_str(), runtime_error)) {
            if (options.require_runtime) {
                return fail(1, runtime_error);
            }
        }
        else {
            runtime_loaded = true;
            abi_version = runtime.abi_version();
            if (abi_version != DWRT_ABI_VERSION) {
                runtime_error = "unexpected DWRT ABI version";
                if (options.require_runtime) {
                    return fail(2, runtime_error);
                }
            }
            else if (!runtime.create_runtime(runtime_error)) {
                if (options.require_runtime) {
                    return fail(3, runtime_error);
                }
            }
            else {
                runtime_probe_checked = true;
                DwrtFastDamageNative damage{};
                DwrtFastEntityIoNative input{};
                const std::uint32_t no_interest = runtime.probe_record_damage(damage);
                runtime.set_probe_mount_mask(DWRT_PROBE_MOUNT_DAMAGE | DWRT_PROBE_MOUNT_ENTITY_INPUT);
                const std::uint32_t damage_route = runtime.probe_record_damage(damage);
                const std::uint32_t input_route = runtime.probe_record_entity_input(input);
                DwrtProbeCountersNative snapshot{};
                const std::uint8_t snapshot_ok = runtime.probe_snapshot(snapshot);
                runtime_probe_ok = no_interest == DWRT_PROBE_ROUTE_NO_INTEREST &&
                    damage_route == DWRT_PROBE_ROUTE_COUNTED &&
                    input_route == DWRT_PROBE_ROUTE_COUNTED &&
                    snapshot_ok != 0 &&
                    snapshot.damage_seen == 1 &&
                    snapshot.damage_counted == 1 &&
                    snapshot.entity_input_seen == 1 &&
                    snapshot.entity_input_counted == 1;
                if (!runtime_probe_ok) {
                    runtime_probe_error = "DWRT runtime probe ABI smoke mismatch";
                    if (options.require_runtime) {
                        return fail(8, runtime_probe_error);
                    }
                }
            }
        }
    }
    else if (options.require_runtime) {
        return fail(4, "--require-runtime was specified without --runtime");
    }

    std::string image_error;
    const std::optional<dwrt::host::PeImageFile> image = dwrt::host::PeImageFile::load(options.server_path, image_error);
    if (!image.has_value()) {
        return fail(5, image_error);
    }

    std::vector<ResolvedSignature> resolved;
    std::size_t required_failures = 0;
    for (const dwrt::host::SignatureDescriptor& descriptor : dwrt::host::default_probe_signatures()) {
        ResolvedSignature sig = resolve_signature(*image, descriptor);
        const bool acceptable_rva = sig.expected_rva_ok || options.allow_expected_rva_drift;
        const bool ok = sig.unique && sig.rva.has_value() && acceptable_rva;
        if (descriptor.required && !ok) {
            required_failures += 1;
        }
        resolved.push_back(std::move(sig));
    }

    std::vector<MappedResolvedSignature> mapped_resolved;
    bool mapped_module_checked = options.mapped_module_check;
    bool mapped_module_loaded = false;
    std::string mapped_module_error;
    std::size_t mapped_required_failures = 0;
    if (options.mapped_module_check) {
        HMODULE mapped_module = LoadLibraryExW(
            options.server_path.c_str(),
            nullptr,
            DONT_RESOLVE_DLL_REFERENCES);
        if (mapped_module == nullptr) {
            mapped_module_error = windows_error_message("failed to map server.dll for live-module resolver check");
            for (const dwrt::host::SignatureDescriptor& descriptor : dwrt::host::default_probe_signatures()) {
                if (descriptor.required) {
                    mapped_required_failures += 1;
                }
            }
        }
        else {
            mapped_module_loaded = true;
            std::string module_error;
            const std::optional<dwrt::host::PeModuleView> module =
                dwrt::host::PeModuleView::from_module_handle(mapped_module, options.server_path, module_error);
            if (!module.has_value()) {
                mapped_module_error = module_error;
                for (const dwrt::host::SignatureDescriptor& descriptor : dwrt::host::default_probe_signatures()) {
                    if (descriptor.required) {
                        mapped_required_failures += 1;
                    }
                }
            }
            else {
                for (const dwrt::host::SignatureDescriptor& descriptor : dwrt::host::default_probe_signatures()) {
                    MappedResolvedSignature sig = resolve_mapped_signature(*module, descriptor);
                    const bool acceptable_rva = sig.expected_rva_ok || options.allow_expected_rva_drift;
                    const bool ok = sig.unique && sig.rva.has_value() && acceptable_rva;
                    if (descriptor.required && !ok) {
                        mapped_required_failures += 1;
                    }
                    mapped_resolved.push_back(std::move(sig));
                }
            }
            FreeLibrary(mapped_module);
        }
    }

    const std::string summary = build_json_summary(
        options,
        *image,
        resolved,
        mapped_resolved,
        mapped_module_checked,
        mapped_module_loaded,
        mapped_module_error,
        mapped_required_failures,
        runtime_loaded,
        abi_version,
        runtime_error,
        runtime_probe_checked,
        runtime_probe_ok,
        runtime_probe_error,
        required_failures);

    if (!options.output_path.empty()) {
        const std::filesystem::path parent = options.output_path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        std::ofstream output(options.output_path, std::ios::binary);
        if (!output) {
            return fail(6, "failed to open summary output path");
        }
        output << summary;
    }

    std::cout << summary;
    if (required_failures != 0) {
        return fail(7, "required signature validation failed");
    }
    if (mapped_required_failures != 0) {
        return fail(9, "mapped-module signature validation failed");
    }

    std::cout << "[dwrt-host-smoke] OK\n";
    return 0;
}
