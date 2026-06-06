#include "dwrt_host_api.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr const wchar_t* kDefaultServerExe =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Deadlock\\game\\bin\\win64\\deadlock.exe";
constexpr const wchar_t* kDefaultServerDll =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Deadlock\\game\\citadel\\bin\\win64\\server.dll";
constexpr const wchar_t* kDefaultServerArgs =
    L"-dedicated -dev -insecure -allow_no_lobby_connect +tv_citadel_auto_record 0 "
    L"+spec_replay_enable 0 +tv_enable 0 +citadel_upload_replay_enabled 0 "
    L"+hostport 27068 +map dl_midtown";

struct Options {
    std::filesystem::path server_exe = kDefaultServerExe;
    std::filesystem::path server_dll = kDefaultServerDll;
    std::filesystem::path host_dll;
    std::filesystem::path runtime_dll;
    std::filesystem::path host_summary_path;
    std::filesystem::path injector_summary_path;
    std::wstring server_args = kDefaultServerArgs;
    std::uint32_t wait_server_module_seconds = 60;
    std::uint32_t hold_seconds = 5;
    std::uint32_t poll_seconds = 0;
    std::uint32_t probe_mount_mask = 0;
    std::filesystem::path snapshot_jsonl_path;
    std::filesystem::path stop_file_path;
    bool install_probe_hooks = false;
    bool allow_recursive_callbacks = false;
};

struct RemoteAllocation {
    HANDLE process = nullptr;
    void* address = nullptr;
    std::size_t size = 0;

    RemoteAllocation() = default;
    RemoteAllocation(HANDLE process_handle, void* remote_address, std::size_t allocation_size)
        : process(process_handle), address(remote_address), size(allocation_size) {}
    RemoteAllocation(const RemoteAllocation&) = delete;
    RemoteAllocation& operator=(const RemoteAllocation&) = delete;
    RemoteAllocation(RemoteAllocation&& other) noexcept
        : process(other.process), address(other.address), size(other.size) {
        other.process = nullptr;
        other.address = nullptr;
        other.size = 0;
    }
    RemoteAllocation& operator=(RemoteAllocation&& other) noexcept {
        if (this != &other) {
            reset();
            process = other.process;
            address = other.address;
            size = other.size;
            other.process = nullptr;
            other.address = nullptr;
            other.size = 0;
        }
        return *this;
    }
    ~RemoteAllocation() { reset(); }

    void reset() {
        if (process != nullptr && address != nullptr) {
            VirtualFreeEx(process, address, 0, MEM_RELEASE);
        }
        process = nullptr;
        address = nullptr;
        size = 0;
    }
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

std::string json_escape(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output += ch; break;
        }
    }
    return output;
}

std::string windows_error_message(const char* prefix) {
    std::ostringstream out;
    out << prefix << " (GetLastError=" << GetLastError() << ")";
    return out.str();
}

int fail(int code, const std::string& message) {
    std::cerr << "[dwrt-live-server-smoke] ERROR: " << message << '\n';
    return code;
}

void print_usage() {
    std::cerr << "usage: dwrt_live_server_smoke.exe --host <dwrt_host.dll> --runtime <dwrt_runtime.dll> "
              << "--host-summary <host.json> --injector-summary <injector.json> "
              << "[--server-exe <deadlock.exe>] [--server-dll <server.dll>] "
              << "[--server-args <args>] [--wait-server-module-seconds <n>] [--hold-seconds <n>] "
              << "[--probe-mount-mask <mask>] [--poll-seconds <n>] [--snapshot-jsonl <path>] "
              << "[--stop-file <path>] [--install-probe-hooks] [--allow-recursive-callbacks]\n";
}

std::optional<std::wstring> require_value(int argc, wchar_t** argv, int& index, const wchar_t* name, std::string& error) {
    if (index + 1 >= argc) {
        error = "missing value for " + narrow(name);
        return std::nullopt;
    }
    ++index;
    return std::wstring(argv[index]);
}

std::optional<Options> parse_options(int argc, wchar_t** argv, std::string& error) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring arg = argv[index];
        if (arg == L"--server-exe") {
            const auto value = require_value(argc, argv, index, L"--server-exe", error);
            if (!value.has_value()) { return std::nullopt; }
            options.server_exe = *value;
        }
        else if (arg == L"--server-dll") {
            const auto value = require_value(argc, argv, index, L"--server-dll", error);
            if (!value.has_value()) { return std::nullopt; }
            options.server_dll = *value;
        }
        else if (arg == L"--host") {
            const auto value = require_value(argc, argv, index, L"--host", error);
            if (!value.has_value()) { return std::nullopt; }
            options.host_dll = *value;
        }
        else if (arg == L"--runtime") {
            const auto value = require_value(argc, argv, index, L"--runtime", error);
            if (!value.has_value()) { return std::nullopt; }
            options.runtime_dll = *value;
        }
        else if (arg == L"--host-summary") {
            const auto value = require_value(argc, argv, index, L"--host-summary", error);
            if (!value.has_value()) { return std::nullopt; }
            options.host_summary_path = *value;
        }
        else if (arg == L"--injector-summary") {
            const auto value = require_value(argc, argv, index, L"--injector-summary", error);
            if (!value.has_value()) { return std::nullopt; }
            options.injector_summary_path = *value;
        }
        else if (arg == L"--server-args") {
            const auto value = require_value(argc, argv, index, L"--server-args", error);
            if (!value.has_value()) { return std::nullopt; }
            options.server_args = *value;
        }
        else if (arg == L"--wait-server-module-seconds") {
            const auto value = require_value(argc, argv, index, L"--wait-server-module-seconds", error);
            if (!value.has_value()) { return std::nullopt; }
            options.wait_server_module_seconds = static_cast<std::uint32_t>(std::wcstoul(value->c_str(), nullptr, 10));
        }
        else if (arg == L"--hold-seconds") {
            const auto value = require_value(argc, argv, index, L"--hold-seconds", error);
            if (!value.has_value()) { return std::nullopt; }
            options.hold_seconds = static_cast<std::uint32_t>(std::wcstoul(value->c_str(), nullptr, 10));
        }
        else if (arg == L"--poll-seconds") {
            const auto value = require_value(argc, argv, index, L"--poll-seconds", error);
            if (!value.has_value()) { return std::nullopt; }
            options.poll_seconds = static_cast<std::uint32_t>(std::wcstoul(value->c_str(), nullptr, 10));
        }
        else if (arg == L"--probe-mount-mask") {
            const auto value = require_value(argc, argv, index, L"--probe-mount-mask", error);
            if (!value.has_value()) { return std::nullopt; }
            options.probe_mount_mask = static_cast<std::uint32_t>(std::wcstoul(value->c_str(), nullptr, 0));
        }
        else if (arg == L"--snapshot-jsonl") {
            const auto value = require_value(argc, argv, index, L"--snapshot-jsonl", error);
            if (!value.has_value()) { return std::nullopt; }
            options.snapshot_jsonl_path = *value;
        }
        else if (arg == L"--stop-file") {
            const auto value = require_value(argc, argv, index, L"--stop-file", error);
            if (!value.has_value()) { return std::nullopt; }
            options.stop_file_path = *value;
        }
        else if (arg == L"--install-probe-hooks") {
            options.install_probe_hooks = true;
        }
        else if (arg == L"--allow-recursive-callbacks") {
            options.allow_recursive_callbacks = true;
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

    if (options.host_dll.empty() || options.runtime_dll.empty() ||
        options.host_summary_path.empty() || options.injector_summary_path.empty()) {
        error = "--host, --runtime, --host-summary, and --injector-summary are required";
        return std::nullopt;
    }
    return options;
}

bool file_exists(const std::filesystem::path& path, std::string& error) {
    if (std::filesystem::exists(path)) {
        return true;
    }
    error = "missing path: " + path.string();
    return false;
}

bool write_text_file(const std::filesystem::path& path, const std::string& content, std::string& error) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "failed to open output path: " + path.string();
        return false;
    }
    output << content;
    return true;
}

bool append_text_file(const std::filesystem::path& path, const std::string& content, std::string& error) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        error = "failed to open append path: " + path.string();
        return false;
    }
    output << content;
    return true;
}

std::uintptr_t module_base_in_process(DWORD process_id, const wchar_t* module_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, module_name) == 0) {
                CloseHandle(snapshot);
                return reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return 0;
}

std::uintptr_t wait_for_module(DWORD process_id, const wchar_t* module_name, DWORD timeout_ms) {
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < timeout_ms) {
        const std::uintptr_t base = module_base_in_process(process_id, module_name);
        if (base != 0) {
            return base;
        }
        Sleep(250);
    }
    return 0;
}

std::optional<RemoteAllocation> remote_alloc_write(
    HANDLE process,
    const void* data,
    std::size_t size,
    DWORD protect,
    std::string& error) {
    void* remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, protect);
    if (remote == nullptr) {
        error = windows_error_message("VirtualAllocEx failed");
        return std::nullopt;
    }
    RemoteAllocation allocation(process, remote, size);
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, data, size, &written) || written != size) {
        error = windows_error_message("WriteProcessMemory failed");
        return std::nullopt;
    }
    return allocation;
}

std::optional<RemoteAllocation> remote_alloc_string(
    HANDLE process,
    const std::wstring& value,
    std::string& error) {
    const std::size_t bytes = (value.size() + 1) * sizeof(wchar_t);
    return remote_alloc_write(process, value.c_str(), bytes, PAGE_READWRITE, error);
}

bool run_remote_thread(
    HANDLE process,
    std::uintptr_t function_address,
    void* parameter,
    DWORD timeout_ms,
    DWORD& exit_code,
    std::string& error) {
    HANDLE thread = CreateRemoteThread(
        process,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(function_address),
        parameter,
        0,
        nullptr);
    if (thread == nullptr) {
        error = windows_error_message("CreateRemoteThread failed");
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        CloseHandle(thread);
        error = "remote thread timed out";
        return false;
    }
    if (!GetExitCodeThread(thread, &exit_code)) {
        CloseHandle(thread);
        error = windows_error_message("GetExitCodeThread failed");
        return false;
    }
    CloseHandle(thread);
    return true;
}

std::uintptr_t remote_export_address(
    DWORD process_id,
    const wchar_t* module_name,
    const std::filesystem::path& local_module_path,
    const char* export_name,
    std::string& error) {
    HMODULE local_module = LoadLibraryW(local_module_path.c_str());
    if (local_module == nullptr) {
        error = windows_error_message("LoadLibraryW for local export inspection failed");
        return 0;
    }
    FARPROC local_export = GetProcAddress(local_module, export_name);
    if (local_export == nullptr) {
        FreeLibrary(local_module);
        error = std::string("missing export: ") + export_name;
        return 0;
    }
    const auto local_base = reinterpret_cast<std::uintptr_t>(local_module);
    const auto export_rva = reinterpret_cast<std::uintptr_t>(local_export) - local_base;
    FreeLibrary(local_module);

    const std::uintptr_t remote_base = module_base_in_process(process_id, module_name);
    if (remote_base == 0) {
        error = "remote module not found while resolving export";
        return 0;
    }
    return remote_base + export_rva;
}

std::uintptr_t remote_kernel32_export(DWORD process_id, const char* export_name, std::string& error) {
    HMODULE local_kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (local_kernel32 == nullptr) {
        error = "kernel32.dll not loaded locally";
        return 0;
    }
    FARPROC local_export = GetProcAddress(local_kernel32, export_name);
    if (local_export == nullptr) {
        error = std::string("missing kernel32 export: ") + export_name;
        return 0;
    }
    const auto local_base = reinterpret_cast<std::uintptr_t>(local_kernel32);
    const auto export_rva = reinterpret_cast<std::uintptr_t>(local_export) - local_base;
    const std::uintptr_t remote_base = module_base_in_process(process_id, L"kernel32.dll");
    if (remote_base == 0) {
        error = "kernel32.dll not found in remote process";
        return 0;
    }
    return remote_base + export_rva;
}

bool assign_kill_on_close_job(HANDLE process, HANDLE& job, std::string& error) {
    job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        error = windows_error_message("CreateJobObjectW failed");
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        error = windows_error_message("SetInformationJobObject failed");
        CloseHandle(job);
        job = nullptr;
        return false;
    }
    if (!AssignProcessToJobObject(job, process)) {
        error = windows_error_message("AssignProcessToJobObject failed");
        CloseHandle(job);
        job = nullptr;
        return false;
    }
    return true;
}

std::string build_injector_summary(
    const Options& options,
    DWORD process_id,
    std::uintptr_t server_base,
    std::uintptr_t host_base,
    DWORD init_status,
    DWORD snapshot_status,
    DWORD probe_mount_status,
    DWORD probe_reset_status,
    DWORD probe_snapshot_status,
    DWORD walker_patrol_snapshot_status,
    DWORD friendly_fire_snapshot_status,
    DWORD target_probe_snapshot_status,
    const DwrtHostSnapshot& snapshot,
    const DwrtProbeCountersNative& probe_snapshot,
    const DwrtWalkerPatrolSnapshot& walker_patrol_snapshot,
    const DwrtFriendlyFireSnapshot& friendly_fire_snapshot,
    const DwrtTargetProbeSnapshot& target_probe_snapshot,
    const std::string& finish_reason,
    const std::string& error) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"ok\": " << (error.empty() ? "true" : "false") << ",\n";
    out << "  \"processId\": " << process_id << ",\n";
    out << "  \"serverExe\": \"" << json_escape(options.server_exe.string()) << "\",\n";
    out << "  \"serverArgs\": \"" << json_escape(narrow(options.server_args)) << "\",\n";
    out << "  \"serverModuleBase\": \"0x" << std::hex << server_base << std::dec << "\",\n";
    out << "  \"hostModuleBase\": \"0x" << std::hex << host_base << std::dec << "\",\n";
    out << "  \"installProbeHooks\": " << (options.install_probe_hooks ? "true" : "false") << ",\n";
    out << "  \"allowRecursiveCallbacks\": " << (options.allow_recursive_callbacks ? "true" : "false") << ",\n";
    out << "  \"probeMountMask\": " << options.probe_mount_mask << ",\n";
    out << "  \"pollSeconds\": " << options.poll_seconds << ",\n";
    out << "  \"snapshotJsonl\": \"" << json_escape(options.snapshot_jsonl_path.string()) << "\",\n";
    out << "  \"stopFile\": \"" << json_escape(options.stop_file_path.string()) << "\",\n";
    out << "  \"finishReason\": \"" << json_escape(finish_reason) << "\",\n";
    out << "  \"initStatus\": " << init_status << ",\n";
    out << "  \"snapshotStatus\": " << snapshot_status << ",\n";
    out << "  \"probeMountStatus\": " << probe_mount_status << ",\n";
    out << "  \"probeResetStatus\": " << probe_reset_status << ",\n";
    out << "  \"probeSnapshotStatus\": " << probe_snapshot_status << ",\n";
    out << "  \"walkerPatrolSnapshotStatus\": " << walker_patrol_snapshot_status << ",\n";
    out << "  \"friendlyFireSnapshotStatus\": " << friendly_fire_snapshot_status << ",\n";
    out << "  \"targetProbeSnapshotStatus\": " << target_probe_snapshot_status << ",\n";
    out << "  \"snapshot\": {\n";
    out << "    \"initialized\": " << snapshot.initialized << ",\n";
    out << "    \"runtimeLoaded\": " << snapshot.runtime_loaded << ",\n";
    out << "    \"runtimeProbeOk\": " << snapshot.runtime_probe_ok << ",\n";
    out << "    \"signaturesChecked\": " << snapshot.signatures_checked << ",\n";
    out << "    \"signatureRequiredFailures\": " << snapshot.signature_required_failures << ",\n";
    out << "    \"usedLiveServerModule\": " << snapshot.used_live_server_module << ",\n";
    out << "    \"usedMappedFileFallback\": " << snapshot.used_mapped_file_fallback << ",\n";
    out << "    \"hookInstallAttempts\": " << snapshot.hook_install_attempts << ",\n";
    out << "    \"hooksInstalled\": " << snapshot.hooks_installed << ",\n";
    out << "    \"hookInstallFailures\": " << snapshot.hook_install_failures << ",\n";
    out << "    \"initializeCalls\": " << snapshot.initialize_calls << ",\n";
    out << "    \"initializeReentrantRejects\": " << snapshot.initialize_reentrant_rejects << ",\n";
    out << "    \"shutdownCalls\": " << snapshot.shutdown_calls << ",\n";
    out << "    \"callbackEntries\": " << snapshot.callback_entries << ",\n";
    out << "    \"callbackRecursiveEntries\": " << snapshot.callback_recursive_entries << ",\n";
    out << "    \"callbackCurrentDepth\": " << snapshot.callback_current_depth << ",\n";
    out << "    \"callbackMaxDepth\": " << snapshot.callback_max_depth << "\n";
    out << "  },\n";
    out << "  \"probeSnapshot\": {\n";
    out << "    \"mountMask\": " << probe_snapshot.mount_mask << ",\n";
    out << "    \"damageSeen\": " << probe_snapshot.damage_seen << ",\n";
    out << "    \"damageCounted\": " << probe_snapshot.damage_counted << ",\n";
    out << "    \"entityInputSeen\": " << probe_snapshot.entity_input_seen << ",\n";
    out << "    \"entityInputCounted\": " << probe_snapshot.entity_input_counted << ",\n";
    out << "    \"entityOutputSeen\": " << probe_snapshot.entity_output_seen << ",\n";
    out << "    \"entityOutputCounted\": " << probe_snapshot.entity_output_counted << ",\n";
    out << "    \"entityTouchSeen\": " << probe_snapshot.entity_touch_seen << ",\n";
    out << "    \"entityTouchCounted\": " << probe_snapshot.entity_touch_counted << "\n";
    out << "  },\n";
    out << "  \"walkerPatrol\": {\n";
    out << "    \"enabled\": " << walker_patrol_snapshot.enabled << ",\n";
    out << "    \"stride\": " << walker_patrol_snapshot.stride << ",\n";
    out << "    \"waypointCount\": " << walker_patrol_snapshot.waypoint_count << ",\n";
    out << "    \"mode\": " << walker_patrol_snapshot.mode << ",\n";
    out << "    \"damageCallbacks\": " << walker_patrol_snapshot.damage_callbacks << ",\n";
    out << "    \"candidateWalkers\": " << walker_patrol_snapshot.candidate_walkers << ",\n";
    out << "    \"nonWalkerVictims\": " << walker_patrol_snapshot.non_walker_victims << ",\n";
    out << "    \"skippedRecursive\": " << walker_patrol_snapshot.skipped_recursive << ",\n";
    out << "    \"missingIdentity\": " << walker_patrol_snapshot.missing_identity << ",\n";
    out << "    \"missingDesignerName\": " << walker_patrol_snapshot.missing_designer_name << ",\n";
    out << "    \"teleportAttempts\": " << walker_patrol_snapshot.teleport_attempts << ",\n";
    out << "    \"teleportCalls\": " << walker_patrol_snapshot.teleport_calls << ",\n";
    out << "    \"bodyComponentMissing\": " << walker_patrol_snapshot.body_component_missing << ",\n";
    out << "    \"sceneNodeMissing\": " << walker_patrol_snapshot.scene_node_missing << ",\n";
    out << "    \"originReadAttempts\": " << walker_patrol_snapshot.origin_read_attempts << ",\n";
    out << "    \"originReadSuccesses\": " << walker_patrol_snapshot.origin_read_successes << ",\n";
    out << "    \"originReadFailures\": " << walker_patrol_snapshot.origin_read_failures << "\n";
    out << "  },\n";
    out << "  \"friendlyFire\": {\n";
    out << "    \"enabled\": " << friendly_fire_snapshot.enabled << ",\n";
    out << "    \"mode\": " << friendly_fire_snapshot.mode << ",\n";
    out << "    \"scope\": " << friendly_fire_snapshot.scope << ",\n";
    out << "    \"localTeam\": " << friendly_fire_snapshot.local_team << ",\n";
    out << "    \"damageCallbacks\": " << friendly_fire_snapshot.damage_callbacks << ",\n";
    out << "    \"skippedRecursive\": " << friendly_fire_snapshot.skipped_recursive << ",\n";
    out << "    \"missingIdentity\": " << friendly_fire_snapshot.missing_identity << ",\n";
    out << "    \"missingDesignerName\": " << friendly_fire_snapshot.missing_designer_name << ",\n";
    out << "    \"nonObjectiveVictims\": " << friendly_fire_snapshot.non_objective_victims << ",\n";
    out << "    \"objectiveCandidates\": " << friendly_fire_snapshot.objective_candidates << ",\n";
    out << "    \"invalidTeam\": " << friendly_fire_snapshot.invalid_team << ",\n";
    out << "    \"teamSpoofAttempts\": " << friendly_fire_snapshot.team_spoof_attempts << ",\n";
    out << "    \"teamSpoofApplied\": " << friendly_fire_snapshot.team_spoof_applied << ",\n";
    out << "    \"teamSpoofRestored\": " << friendly_fire_snapshot.team_spoof_restored << "\n";
    out << "  },\n";
    out << "  \"targetProbe\": {\n";
    out << "    \"enabled\": " << target_probe_snapshot.enabled << ",\n";
    out << "    \"sourceTeamSpoofEnabled\": " << target_probe_snapshot.source_team_spoof_enabled << ",\n";
    out << "    \"targetTeamSpoofEnabled\": " << target_probe_snapshot.target_team_spoof_enabled << ",\n";
    out << "    \"targetTeamSpoofTeam\": " << target_probe_snapshot.target_team_spoof_team << ",\n";
    out << "    \"targetBitsetAllowEnabled\": " << target_probe_snapshot.target_bitset_allow_enabled << ",\n";
    out << "    \"forceFilterSameTeamAllowEnabled\": " << target_probe_snapshot.force_filter_same_team_allow_enabled << ",\n";
    out << "    \"forceCallerSameTeamAllowEnabled\": " << target_probe_snapshot.force_caller_same_team_allow_enabled << ",\n";
    out << "    \"forceFilterObjectiveAllowEnabled\": " << target_probe_snapshot.force_filter_objective_allow_enabled << ",\n";
    out << "    \"forceCallerObjectiveAllowEnabled\": " << target_probe_snapshot.force_caller_objective_allow_enabled << ",\n";
    out << "    \"forceSecondaryAllowEnabled\": " << target_probe_snapshot.force_secondary_allow_enabled << ",\n";
    out << "    \"neutralSimulationEnabled\": " << target_probe_snapshot.neutral_simulation_enabled << ",\n";
    out << "    \"classifierSpoofEnabled\": " << target_probe_snapshot.classifier_spoof_enabled << ",\n";
    out << "    \"classifierSpoofBit\": " << target_probe_snapshot.classifier_spoof_bit << ",\n";
    out << "    \"globalNeutralizeEnabled\": " << target_probe_snapshot.global_neutralize_enabled << ",\n";
    out << "    \"globalNeutralizeTeam\": " << target_probe_snapshot.global_neutralize_team << ",\n";
    out << "    \"lastSourceTeam\": " << target_probe_snapshot.last_source_team << ",\n";
    out << "    \"lastTargetTeam\": " << target_probe_snapshot.last_target_team << ",\n";
    out << "    \"lastTargetUnitType\": " << target_probe_snapshot.last_target_unit_type << ",\n";
    out << "    \"lastFilterResult\": " << target_probe_snapshot.last_filter_result << ",\n";
    out << "    \"lastDamageVictimTeam\": " << target_probe_snapshot.last_damage_victim_team << ",\n";
    out << "    \"lastDamageVictimUnitType\": " << target_probe_snapshot.last_damage_victim_unit_type << ",\n";
    out << "    \"lastDamageAttackerHandle\": " << target_probe_snapshot.last_damage_attacker_handle << ",\n";
    out << "    \"lastDamageInflictorHandle\": " << target_probe_snapshot.last_damage_inflictor_handle << ",\n";
    out << "    \"lastDamageAttackerTeam\": " << target_probe_snapshot.last_damage_attacker_team << ",\n";
    out << "    \"lastDamageAttackerUnitType\": " << target_probe_snapshot.last_damage_attacker_unit_type << ",\n";
    out << "    \"filterCalls\": " << target_probe_snapshot.filter_calls << ",\n";
    out << "    \"filterAllowed\": " << target_probe_snapshot.filter_allowed << ",\n";
    out << "    \"filterDenied\": " << target_probe_snapshot.filter_denied << ",\n";
    out << "    \"filterSameTeamCalls\": " << target_probe_snapshot.filter_same_team_calls << ",\n";
    out << "    \"filterSameTeamAllowed\": " << target_probe_snapshot.filter_same_team_allowed << ",\n";
    out << "    \"filterSameTeamDenied\": " << target_probe_snapshot.filter_same_team_denied << ",\n";
    out << "    \"filterObjectiveTargetCalls\": " << target_probe_snapshot.filter_objective_target_calls << ",\n";
    out << "    \"filterObjectiveTargetAllowed\": " << target_probe_snapshot.filter_objective_target_allowed << ",\n";
    out << "    \"filterObjectiveTargetDenied\": " << target_probe_snapshot.filter_objective_target_denied << ",\n";
    out << "    \"filterNeutralTargetCalls\": " << target_probe_snapshot.filter_neutral_target_calls << ",\n";
    out << "    \"filterMidbossTargetCalls\": " << target_probe_snapshot.filter_midboss_target_calls << ",\n";
    out << "    \"filterInvalidSourceTeam\": " << target_probe_snapshot.filter_invalid_source_team << ",\n";
    out << "    \"filterInvalidTargetTeam\": " << target_probe_snapshot.filter_invalid_target_team << ",\n";
    out << "    \"sourceSpoofAttempts\": " << target_probe_snapshot.source_spoof_attempts << ",\n";
    out << "    \"sourceSpoofApplied\": " << target_probe_snapshot.source_spoof_applied << ",\n";
    out << "    \"sourceSpoofRestored\": " << target_probe_snapshot.source_spoof_restored << ",\n";
    out << "    \"sourceSpoofAllowed\": " << target_probe_snapshot.source_spoof_allowed << ",\n";
    out << "    \"sourceSpoofDenied\": " << target_probe_snapshot.source_spoof_denied << ",\n";
    out << "    \"targetSpoofAttempts\": " << target_probe_snapshot.target_spoof_attempts << ",\n";
    out << "    \"targetSpoofApplied\": " << target_probe_snapshot.target_spoof_applied << ",\n";
    out << "    \"targetSpoofRestored\": " << target_probe_snapshot.target_spoof_restored << ",\n";
    out << "    \"targetSpoofAllowed\": " << target_probe_snapshot.target_spoof_allowed << ",\n";
    out << "    \"targetSpoofDenied\": " << target_probe_snapshot.target_spoof_denied << ",\n";
    out << "    \"bitsetAllowAttempts\": " << target_probe_snapshot.bitset_allow_attempts << ",\n";
    out << "    \"bitsetAllowApplied\": " << target_probe_snapshot.bitset_allow_applied << ",\n";
    out << "    \"bitsetAllowFailures\": " << target_probe_snapshot.bitset_allow_failures << ",\n";
    out << "    \"classifierCalls\": " << target_probe_snapshot.classifier_calls << ",\n";
    out << "    \"classifierInvalid\": " << target_probe_snapshot.classifier_invalid << ",\n";
    out << "    \"classifierSpoofAttempts\": " << target_probe_snapshot.classifier_spoof_attempts << ",\n";
    out << "    \"classifierSpoofApplied\": " << target_probe_snapshot.classifier_spoof_applied << ",\n";
    out << "    \"classifierSpoofSkipped\": " << target_probe_snapshot.classifier_spoof_skipped << ",\n";
    out << "    \"lastClassifierOriginalBit\": " << target_probe_snapshot.last_classifier_original_bit << ",\n";
    out << "    \"lastClassifierFinalBit\": " << target_probe_snapshot.last_classifier_final_bit << ",\n";
    out << "    \"globalNeutralizeAttempts\": " << target_probe_snapshot.global_neutralize_attempts << ",\n";
    out << "    \"globalNeutralizeApplied\": " << target_probe_snapshot.global_neutralize_applied << ",\n";
    out << "    \"globalNeutralizeAlready\": " << target_probe_snapshot.global_neutralize_already << ",\n";
    out << "    \"globalNeutralizeNull\": " << target_probe_snapshot.global_neutralize_null << ",\n";
    out << "    \"globalNeutralizeInvalidTeam\": " << target_probe_snapshot.global_neutralize_invalid_team << ",\n";
    out << "    \"globalNeutralizeWriteFailures\": " << target_probe_snapshot.global_neutralize_write_failures << ",\n";
    out << "    \"globalNeutralizeOriginalTeamOverflow\": " << target_probe_snapshot.global_neutralize_original_team_overflow << ",\n";
    out << "    \"globalNeutralizeOriginalTeamCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.global_neutralize_original_team_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe_snapshot.global_neutralize_original_team_counts[index];
    }
    out << "],\n";
    out << "    \"filterForcedAllowed\": " << target_probe_snapshot.filter_forced_allowed << ",\n";
    out << "    \"callerForcedAllowed\": " << target_probe_snapshot.caller_forced_allowed << ",\n";
    out << "    \"secondaryForcedAllowed\": " << target_probe_snapshot.secondary_forced_allowed << ",\n";
    out << "    \"secondaryCalls\": " << target_probe_snapshot.secondary_calls << ",\n";
    out << "    \"secondaryAllowed\": " << target_probe_snapshot.secondary_allowed << ",\n";
    out << "    \"secondaryDenied\": " << target_probe_snapshot.secondary_denied << ",\n";
    out << "    \"callerCalls\": " << target_probe_snapshot.caller_calls << ",\n";
    out << "    \"callerAllowed\": " << target_probe_snapshot.caller_allowed << ",\n";
    out << "    \"callerDenied\": " << target_probe_snapshot.caller_denied << ",\n";
    out << "    \"callerUnit0x1aBypassCandidates\": " << target_probe_snapshot.caller_unit_0x1a_bypass_candidates << ",\n";
    out << "    \"filterUnitTypeOverflow\": " << target_probe_snapshot.filter_unit_type_overflow << ",\n";
    out << "    \"filterUnitTypeCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.filter_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe_snapshot.filter_unit_type_counts[index];
    }
    out << "],\n";
    out << "    \"damageVictimCalls\": " << target_probe_snapshot.damage_victim_calls << ",\n";
    out << "    \"damageVictimObjective\": " << target_probe_snapshot.damage_victim_objective << ",\n";
    out << "    \"damageVictimNeutral\": " << target_probe_snapshot.damage_victim_neutral << ",\n";
    out << "    \"damageVictimMidboss\": " << target_probe_snapshot.damage_victim_midboss << ",\n";
    out << "    \"damageVictimInvalidTeam\": " << target_probe_snapshot.damage_victim_invalid_team << ",\n";
    out << "    \"damageVictimUnitTypeOverflow\": " << target_probe_snapshot.damage_victim_unit_type_overflow << ",\n";
    out << "    \"damageVictimUnitTypeCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.damage_victim_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe_snapshot.damage_victim_unit_type_counts[index];
    }
    out << "],\n";
    out << "    \"damageAttackerHandleValid\": " << target_probe_snapshot.damage_attacker_handle_valid << ",\n";
    out << "    \"damageAttackerHandleInvalid\": " << target_probe_snapshot.damage_attacker_handle_invalid << ",\n";
    out << "    \"damageInflictorHandleValid\": " << target_probe_snapshot.damage_inflictor_handle_valid << ",\n";
    out << "    \"damageInflictorHandleInvalid\": " << target_probe_snapshot.damage_inflictor_handle_invalid << ",\n";
    out << "    \"damageAttackerSameTeam\": " << target_probe_snapshot.damage_attacker_same_team << ",\n";
    out << "    \"damageAttackerOpposingTeam\": " << target_probe_snapshot.damage_attacker_opposing_team << ",\n";
    out << "    \"damageAttackerOtherTeam\": " << target_probe_snapshot.damage_attacker_other_team << ",\n";
    out << "    \"damageAttackerSelf\": " << target_probe_snapshot.damage_attacker_self << ",\n";
    out << "    \"damageAttackerSameTeamObjective\": " << target_probe_snapshot.damage_attacker_same_team_objective << ",\n";
    out << "    \"damageAttackerUnitTypeOverflow\": " << target_probe_snapshot.damage_attacker_unit_type_overflow << ",\n";
    out << "    \"damageAttackerUnitTypeCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.damage_attacker_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe_snapshot.damage_attacker_unit_type_counts[index];
    }
    out << "],\n";
    out << "    \"damageSameTeamVictimUnitTypeOverflow\": " << target_probe_snapshot.damage_same_team_victim_unit_type_overflow << ",\n";
    out << "    \"damageSameTeamVictimUnitTypeCounts\": [";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.damage_same_team_victim_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ", ") << target_probe_snapshot.damage_same_team_victim_unit_type_counts[index];
    }
    out << "]\n";
    out << "  },\n";
    out << "  \"error\": \"" << json_escape(error) << "\"\n";
    out << "}\n";
    return out.str();
}

std::string build_snapshot_jsonl(
    const char* phase,
    double elapsed_seconds,
    const DwrtHostSnapshot& snapshot,
    const DwrtProbeCountersNative& probe_snapshot,
    const DwrtWalkerPatrolSnapshot& walker_patrol_snapshot,
    const DwrtFriendlyFireSnapshot& friendly_fire_snapshot,
    const DwrtTargetProbeSnapshot& target_probe_snapshot) {
    std::ostringstream out;
    out << "{\"phase\":\"" << json_escape(phase) << "\",";
    out << "\"elapsedSeconds\":" << elapsed_seconds << ",";
    out << "\"host\":{";
    out << "\"initialized\":" << snapshot.initialized << ",";
    out << "\"runtimeLoaded\":" << snapshot.runtime_loaded << ",";
    out << "\"runtimeProbeOk\":" << snapshot.runtime_probe_ok << ",";
    out << "\"signaturesChecked\":" << snapshot.signatures_checked << ",";
    out << "\"signatureRequiredFailures\":" << snapshot.signature_required_failures << ",";
    out << "\"usedLiveServerModule\":" << snapshot.used_live_server_module << ",";
    out << "\"usedMappedFileFallback\":" << snapshot.used_mapped_file_fallback << ",";
    out << "\"hookInstallAttempts\":" << snapshot.hook_install_attempts << ",";
    out << "\"hooksInstalled\":" << snapshot.hooks_installed << ",";
    out << "\"hookInstallFailures\":" << snapshot.hook_install_failures << ",";
    out << "\"callbackEntries\":" << snapshot.callback_entries << ",";
    out << "\"callbackRecursiveEntries\":" << snapshot.callback_recursive_entries << ",";
    out << "\"callbackCurrentDepth\":" << snapshot.callback_current_depth << ",";
    out << "\"callbackMaxDepth\":" << snapshot.callback_max_depth;
    out << "},\"probe\":{";
    out << "\"mountMask\":" << probe_snapshot.mount_mask << ",";
    out << "\"damageSeen\":" << probe_snapshot.damage_seen << ",";
    out << "\"damageCounted\":" << probe_snapshot.damage_counted << ",";
    out << "\"entityInputSeen\":" << probe_snapshot.entity_input_seen << ",";
    out << "\"entityInputCounted\":" << probe_snapshot.entity_input_counted << ",";
    out << "\"entityOutputSeen\":" << probe_snapshot.entity_output_seen << ",";
    out << "\"entityOutputCounted\":" << probe_snapshot.entity_output_counted << ",";
    out << "\"entityTouchSeen\":" << probe_snapshot.entity_touch_seen << ",";
    out << "\"entityTouchCounted\":" << probe_snapshot.entity_touch_counted;
    out << "},\"walkerPatrol\":{";
    out << "\"enabled\":" << walker_patrol_snapshot.enabled << ",";
    out << "\"stride\":" << walker_patrol_snapshot.stride << ",";
    out << "\"waypointCount\":" << walker_patrol_snapshot.waypoint_count << ",";
    out << "\"mode\":" << walker_patrol_snapshot.mode << ",";
    out << "\"damageCallbacks\":" << walker_patrol_snapshot.damage_callbacks << ",";
    out << "\"candidateWalkers\":" << walker_patrol_snapshot.candidate_walkers << ",";
    out << "\"nonWalkerVictims\":" << walker_patrol_snapshot.non_walker_victims << ",";
    out << "\"skippedRecursive\":" << walker_patrol_snapshot.skipped_recursive << ",";
    out << "\"missingIdentity\":" << walker_patrol_snapshot.missing_identity << ",";
    out << "\"missingDesignerName\":" << walker_patrol_snapshot.missing_designer_name << ",";
    out << "\"teleportAttempts\":" << walker_patrol_snapshot.teleport_attempts << ",";
    out << "\"teleportCalls\":" << walker_patrol_snapshot.teleport_calls << ",";
    out << "\"bodyComponentMissing\":" << walker_patrol_snapshot.body_component_missing << ",";
    out << "\"sceneNodeMissing\":" << walker_patrol_snapshot.scene_node_missing << ",";
    out << "\"originReadAttempts\":" << walker_patrol_snapshot.origin_read_attempts << ",";
    out << "\"originReadSuccesses\":" << walker_patrol_snapshot.origin_read_successes << ",";
    out << "\"originReadFailures\":" << walker_patrol_snapshot.origin_read_failures;
    out << "},\"friendlyFire\":{";
    out << "\"enabled\":" << friendly_fire_snapshot.enabled << ",";
    out << "\"mode\":" << friendly_fire_snapshot.mode << ",";
    out << "\"scope\":" << friendly_fire_snapshot.scope << ",";
    out << "\"localTeam\":" << friendly_fire_snapshot.local_team << ",";
    out << "\"damageCallbacks\":" << friendly_fire_snapshot.damage_callbacks << ",";
    out << "\"skippedRecursive\":" << friendly_fire_snapshot.skipped_recursive << ",";
    out << "\"missingIdentity\":" << friendly_fire_snapshot.missing_identity << ",";
    out << "\"missingDesignerName\":" << friendly_fire_snapshot.missing_designer_name << ",";
    out << "\"nonObjectiveVictims\":" << friendly_fire_snapshot.non_objective_victims << ",";
    out << "\"objectiveCandidates\":" << friendly_fire_snapshot.objective_candidates << ",";
    out << "\"invalidTeam\":" << friendly_fire_snapshot.invalid_team << ",";
    out << "\"teamSpoofAttempts\":" << friendly_fire_snapshot.team_spoof_attempts << ",";
    out << "\"teamSpoofApplied\":" << friendly_fire_snapshot.team_spoof_applied << ",";
    out << "\"teamSpoofRestored\":" << friendly_fire_snapshot.team_spoof_restored;
    out << "},\"targetProbe\":{";
    out << "\"enabled\":" << target_probe_snapshot.enabled << ",";
    out << "\"sourceTeamSpoofEnabled\":" << target_probe_snapshot.source_team_spoof_enabled << ",";
    out << "\"targetTeamSpoofEnabled\":" << target_probe_snapshot.target_team_spoof_enabled << ",";
    out << "\"targetTeamSpoofTeam\":" << target_probe_snapshot.target_team_spoof_team << ",";
    out << "\"targetBitsetAllowEnabled\":" << target_probe_snapshot.target_bitset_allow_enabled << ",";
    out << "\"forceFilterSameTeamAllowEnabled\":" << target_probe_snapshot.force_filter_same_team_allow_enabled << ",";
    out << "\"forceCallerSameTeamAllowEnabled\":" << target_probe_snapshot.force_caller_same_team_allow_enabled << ",";
    out << "\"forceFilterObjectiveAllowEnabled\":" << target_probe_snapshot.force_filter_objective_allow_enabled << ",";
    out << "\"forceCallerObjectiveAllowEnabled\":" << target_probe_snapshot.force_caller_objective_allow_enabled << ",";
    out << "\"forceSecondaryAllowEnabled\":" << target_probe_snapshot.force_secondary_allow_enabled << ",";
    out << "\"neutralSimulationEnabled\":" << target_probe_snapshot.neutral_simulation_enabled << ",";
    out << "\"classifierSpoofEnabled\":" << target_probe_snapshot.classifier_spoof_enabled << ",";
    out << "\"classifierSpoofBit\":" << target_probe_snapshot.classifier_spoof_bit << ",";
    out << "\"globalNeutralizeEnabled\":" << target_probe_snapshot.global_neutralize_enabled << ",";
    out << "\"globalNeutralizeTeam\":" << target_probe_snapshot.global_neutralize_team << ",";
    out << "\"lastSourceTeam\":" << target_probe_snapshot.last_source_team << ",";
    out << "\"lastTargetTeam\":" << target_probe_snapshot.last_target_team << ",";
    out << "\"lastTargetUnitType\":" << target_probe_snapshot.last_target_unit_type << ",";
    out << "\"lastFilterResult\":" << target_probe_snapshot.last_filter_result << ",";
    out << "\"lastDamageVictimTeam\":" << target_probe_snapshot.last_damage_victim_team << ",";
    out << "\"lastDamageVictimUnitType\":" << target_probe_snapshot.last_damage_victim_unit_type << ",";
    out << "\"lastDamageAttackerHandle\":" << target_probe_snapshot.last_damage_attacker_handle << ",";
    out << "\"lastDamageInflictorHandle\":" << target_probe_snapshot.last_damage_inflictor_handle << ",";
    out << "\"lastDamageAttackerTeam\":" << target_probe_snapshot.last_damage_attacker_team << ",";
    out << "\"lastDamageAttackerUnitType\":" << target_probe_snapshot.last_damage_attacker_unit_type << ",";
    out << "\"filterCalls\":" << target_probe_snapshot.filter_calls << ",";
    out << "\"filterAllowed\":" << target_probe_snapshot.filter_allowed << ",";
    out << "\"filterDenied\":" << target_probe_snapshot.filter_denied << ",";
    out << "\"filterSameTeamCalls\":" << target_probe_snapshot.filter_same_team_calls << ",";
    out << "\"filterSameTeamAllowed\":" << target_probe_snapshot.filter_same_team_allowed << ",";
    out << "\"filterSameTeamDenied\":" << target_probe_snapshot.filter_same_team_denied << ",";
    out << "\"filterObjectiveTargetCalls\":" << target_probe_snapshot.filter_objective_target_calls << ",";
    out << "\"filterObjectiveTargetAllowed\":" << target_probe_snapshot.filter_objective_target_allowed << ",";
    out << "\"filterObjectiveTargetDenied\":" << target_probe_snapshot.filter_objective_target_denied << ",";
    out << "\"filterNeutralTargetCalls\":" << target_probe_snapshot.filter_neutral_target_calls << ",";
    out << "\"filterMidbossTargetCalls\":" << target_probe_snapshot.filter_midboss_target_calls << ",";
    out << "\"filterInvalidSourceTeam\":" << target_probe_snapshot.filter_invalid_source_team << ",";
    out << "\"filterInvalidTargetTeam\":" << target_probe_snapshot.filter_invalid_target_team << ",";
    out << "\"sourceSpoofAttempts\":" << target_probe_snapshot.source_spoof_attempts << ",";
    out << "\"sourceSpoofApplied\":" << target_probe_snapshot.source_spoof_applied << ",";
    out << "\"sourceSpoofRestored\":" << target_probe_snapshot.source_spoof_restored << ",";
    out << "\"sourceSpoofAllowed\":" << target_probe_snapshot.source_spoof_allowed << ",";
    out << "\"sourceSpoofDenied\":" << target_probe_snapshot.source_spoof_denied << ",";
    out << "\"targetSpoofAttempts\":" << target_probe_snapshot.target_spoof_attempts << ",";
    out << "\"targetSpoofApplied\":" << target_probe_snapshot.target_spoof_applied << ",";
    out << "\"targetSpoofRestored\":" << target_probe_snapshot.target_spoof_restored << ",";
    out << "\"targetSpoofAllowed\":" << target_probe_snapshot.target_spoof_allowed << ",";
    out << "\"targetSpoofDenied\":" << target_probe_snapshot.target_spoof_denied << ",";
    out << "\"bitsetAllowAttempts\":" << target_probe_snapshot.bitset_allow_attempts << ",";
    out << "\"bitsetAllowApplied\":" << target_probe_snapshot.bitset_allow_applied << ",";
    out << "\"bitsetAllowFailures\":" << target_probe_snapshot.bitset_allow_failures << ",";
    out << "\"classifierCalls\":" << target_probe_snapshot.classifier_calls << ",";
    out << "\"classifierInvalid\":" << target_probe_snapshot.classifier_invalid << ",";
    out << "\"classifierSpoofAttempts\":" << target_probe_snapshot.classifier_spoof_attempts << ",";
    out << "\"classifierSpoofApplied\":" << target_probe_snapshot.classifier_spoof_applied << ",";
    out << "\"classifierSpoofSkipped\":" << target_probe_snapshot.classifier_spoof_skipped << ",";
    out << "\"lastClassifierOriginalBit\":" << target_probe_snapshot.last_classifier_original_bit << ",";
    out << "\"lastClassifierFinalBit\":" << target_probe_snapshot.last_classifier_final_bit << ",";
    out << "\"globalNeutralizeAttempts\":" << target_probe_snapshot.global_neutralize_attempts << ",";
    out << "\"globalNeutralizeApplied\":" << target_probe_snapshot.global_neutralize_applied << ",";
    out << "\"globalNeutralizeAlready\":" << target_probe_snapshot.global_neutralize_already << ",";
    out << "\"globalNeutralizeNull\":" << target_probe_snapshot.global_neutralize_null << ",";
    out << "\"globalNeutralizeInvalidTeam\":" << target_probe_snapshot.global_neutralize_invalid_team << ",";
    out << "\"globalNeutralizeWriteFailures\":" << target_probe_snapshot.global_neutralize_write_failures << ",";
    out << "\"globalNeutralizeOriginalTeamOverflow\":" << target_probe_snapshot.global_neutralize_original_team_overflow << ",";
    out << "\"globalNeutralizeOriginalTeamCounts\":[";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.global_neutralize_original_team_counts); ++index) {
        out << (index == 0 ? "" : ",") << target_probe_snapshot.global_neutralize_original_team_counts[index];
    }
    out << "],";
    out << "\"filterForcedAllowed\":" << target_probe_snapshot.filter_forced_allowed << ",";
    out << "\"callerForcedAllowed\":" << target_probe_snapshot.caller_forced_allowed << ",";
    out << "\"secondaryForcedAllowed\":" << target_probe_snapshot.secondary_forced_allowed << ",";
    out << "\"secondaryCalls\":" << target_probe_snapshot.secondary_calls << ",";
    out << "\"secondaryAllowed\":" << target_probe_snapshot.secondary_allowed << ",";
    out << "\"secondaryDenied\":" << target_probe_snapshot.secondary_denied << ",";
    out << "\"callerCalls\":" << target_probe_snapshot.caller_calls << ",";
    out << "\"callerAllowed\":" << target_probe_snapshot.caller_allowed << ",";
    out << "\"callerDenied\":" << target_probe_snapshot.caller_denied << ",";
    out << "\"callerUnit0x1aBypassCandidates\":" << target_probe_snapshot.caller_unit_0x1a_bypass_candidates << ",";
    out << "\"filterUnitTypeOverflow\":" << target_probe_snapshot.filter_unit_type_overflow << ",";
    out << "\"filterUnitTypeCounts\":[";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.filter_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ",") << target_probe_snapshot.filter_unit_type_counts[index];
    }
    out << "],";
    out << "\"damageVictimCalls\":" << target_probe_snapshot.damage_victim_calls << ",";
    out << "\"damageVictimObjective\":" << target_probe_snapshot.damage_victim_objective << ",";
    out << "\"damageVictimNeutral\":" << target_probe_snapshot.damage_victim_neutral << ",";
    out << "\"damageVictimMidboss\":" << target_probe_snapshot.damage_victim_midboss << ",";
    out << "\"damageVictimInvalidTeam\":" << target_probe_snapshot.damage_victim_invalid_team << ",";
    out << "\"damageVictimUnitTypeOverflow\":" << target_probe_snapshot.damage_victim_unit_type_overflow << ",";
    out << "\"damageVictimUnitTypeCounts\":[";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.damage_victim_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ",") << target_probe_snapshot.damage_victim_unit_type_counts[index];
    }
    out << "],";
    out << "\"damageAttackerHandleValid\":" << target_probe_snapshot.damage_attacker_handle_valid << ",";
    out << "\"damageAttackerHandleInvalid\":" << target_probe_snapshot.damage_attacker_handle_invalid << ",";
    out << "\"damageInflictorHandleValid\":" << target_probe_snapshot.damage_inflictor_handle_valid << ",";
    out << "\"damageInflictorHandleInvalid\":" << target_probe_snapshot.damage_inflictor_handle_invalid << ",";
    out << "\"damageAttackerSameTeam\":" << target_probe_snapshot.damage_attacker_same_team << ",";
    out << "\"damageAttackerOpposingTeam\":" << target_probe_snapshot.damage_attacker_opposing_team << ",";
    out << "\"damageAttackerOtherTeam\":" << target_probe_snapshot.damage_attacker_other_team << ",";
    out << "\"damageAttackerSelf\":" << target_probe_snapshot.damage_attacker_self << ",";
    out << "\"damageAttackerSameTeamObjective\":" << target_probe_snapshot.damage_attacker_same_team_objective << ",";
    out << "\"damageAttackerUnitTypeOverflow\":" << target_probe_snapshot.damage_attacker_unit_type_overflow << ",";
    out << "\"damageAttackerUnitTypeCounts\":[";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.damage_attacker_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ",") << target_probe_snapshot.damage_attacker_unit_type_counts[index];
    }
    out << "],";
    out << "\"damageSameTeamVictimUnitTypeOverflow\":" << target_probe_snapshot.damage_same_team_victim_unit_type_overflow << ",";
    out << "\"damageSameTeamVictimUnitTypeCounts\":[";
    for (std::size_t index = 0; index < std::size(target_probe_snapshot.damage_same_team_victim_unit_type_counts); ++index) {
        out << (index == 0 ? "" : ",") << target_probe_snapshot.damage_same_team_victim_unit_type_counts[index];
    }
    out << "]";
    out << "}}\n";
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

    std::string error;
    if (!file_exists(options.server_exe, error) || !file_exists(options.server_dll, error) ||
        !file_exists(options.host_dll, error) || !file_exists(options.runtime_dll, error)) {
        return fail(1, error);
    }

    std::wstring command_line = L"\"" + options.server_exe.wstring() + L"\" " + options.server_args;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process_info{};
    const std::filesystem::path cwd = options.server_exe.parent_path();

    if (!CreateProcessW(
            options.server_exe.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NEW_CONSOLE,
            nullptr,
            cwd.c_str(),
            &startup,
            &process_info)) {
        return fail(2, windows_error_message("CreateProcessW failed"));
    }

    HANDLE job = nullptr;
    if (!assign_kill_on_close_job(process_info.hProcess, job, error)) {
        std::cerr << "[dwrt-live-server-smoke] warning: " << error
                  << "; falling back to direct TerminateProcess cleanup\n";
        job = nullptr;
    }

    DWORD init_status = 0;
    DWORD snapshot_status = 0;
    DWORD probe_mount_status = 0;
    DWORD probe_reset_status = 0;
    DWORD probe_snapshot_status = 0;
    DWORD walker_patrol_snapshot_status = 0;
    DWORD friendly_fire_snapshot_status = 0;
    DWORD target_probe_snapshot_status = 0;
    DwrtHostSnapshot host_snapshot{};
    DwrtProbeCountersNative probe_snapshot{};
    DwrtWalkerPatrolSnapshot walker_patrol_snapshot{};
    DwrtFriendlyFireSnapshot friendly_fire_snapshot{};
    DwrtTargetProbeSnapshot target_probe_snapshot{};
    std::uintptr_t server_base = 0;
    std::uintptr_t host_base = 0;
    int exit_code = 0;
    std::string completion_reason = "completed";

    auto finish = [&](int code, const std::string& finish_error) -> int {
        std::string write_error;
        const std::string finish_reason = finish_error.empty() ? completion_reason : "error";
        const std::string summary = build_injector_summary(
            options,
            process_info.dwProcessId,
            server_base,
            host_base,
            init_status,
            snapshot_status,
            probe_mount_status,
            probe_reset_status,
            probe_snapshot_status,
            walker_patrol_snapshot_status,
            friendly_fire_snapshot_status,
            target_probe_snapshot_status,
            host_snapshot,
            probe_snapshot,
            walker_patrol_snapshot,
            friendly_fire_snapshot,
            target_probe_snapshot,
            finish_reason,
            finish_error);
        if (!write_text_file(options.injector_summary_path, summary, write_error)) {
            std::cerr << "[dwrt-live-server-smoke] failed to write injector summary: " << write_error << '\n';
        }
        if (host_base != 0) {
            std::string shutdown_error;
            const std::uintptr_t shutdown_addr = remote_export_address(
                process_info.dwProcessId,
                L"dwrt_host.dll",
                options.host_dll,
                "dwrt_host_shutdown",
                shutdown_error);
            if (shutdown_addr != 0) {
                DWORD shutdown_status = 0;
                run_remote_thread(process_info.hProcess, shutdown_addr, nullptr, 10'000, shutdown_status, shutdown_error);
            }
        }
        if (process_info.hProcess != nullptr) {
            TerminateProcess(process_info.hProcess, 0);
        }
        if (job != nullptr) {
            CloseHandle(job);
        }
        if (!finish_error.empty()) {
            return fail(code, finish_error);
        }
        return 0;
    };

    server_base = wait_for_module(
        process_info.dwProcessId,
        L"server.dll",
        options.wait_server_module_seconds * 1000U);
    if (server_base == 0) {
        return finish(4, "server.dll did not load before timeout");
    }

    std::optional<RemoteAllocation> remote_host_path = remote_alloc_string(
        process_info.hProcess,
        options.host_dll.wstring(),
        error);
    if (!remote_host_path.has_value()) {
        return finish(5, error);
    }

    const std::uintptr_t load_library = remote_kernel32_export(
        process_info.dwProcessId,
        "LoadLibraryW",
        error);
    if (load_library == 0) {
        return finish(6, error);
    }

    DWORD load_status = 0;
    if (!run_remote_thread(
            process_info.hProcess,
            load_library,
            remote_host_path->address,
            30'000,
            load_status,
            error)) {
        return finish(7, error);
    }

    host_base = wait_for_module(process_info.dwProcessId, L"dwrt_host.dll", 10'000);
    if (host_base == 0) {
        return finish(8, "dwrt_host.dll did not appear in remote module list after LoadLibraryW");
    }

    std::optional<RemoteAllocation> remote_runtime_path = remote_alloc_string(
        process_info.hProcess,
        options.runtime_dll.wstring(),
        error);
    if (!remote_runtime_path.has_value()) {
        return finish(9, error);
    }
    std::optional<RemoteAllocation> remote_server_path = remote_alloc_string(
        process_info.hProcess,
        options.server_dll.wstring(),
        error);
    if (!remote_server_path.has_value()) {
        return finish(10, error);
    }
    std::optional<RemoteAllocation> remote_summary_path = remote_alloc_string(
        process_info.hProcess,
        options.host_summary_path.wstring(),
        error);
    if (!remote_summary_path.has_value()) {
        return finish(11, error);
    }
    std::optional<RemoteAllocation> remote_server_module_name = remote_alloc_string(
        process_info.hProcess,
        L"server.dll",
        error);
    if (!remote_server_module_name.has_value()) {
        return finish(12, error);
    }

    DwrtHostConfig config{};
    config.abi_version = DWRT_HOST_ABI_VERSION;
    config.flags = DWRT_HOST_FLAG_REQUIRE_RUNTIME | DWRT_HOST_FLAG_REQUIRE_SIGNATURES;
    if (options.install_probe_hooks) {
        config.flags |= DWRT_HOST_FLAG_INSTALL_PROBE_HOOKS;
    }
    config.runtime_path = static_cast<const wchar_t*>(remote_runtime_path->address);
    config.server_module_name = static_cast<const wchar_t*>(remote_server_module_name->address);
    config.server_path = static_cast<const wchar_t*>(remote_server_path->address);
    config.summary_path = static_cast<const wchar_t*>(remote_summary_path->address);

    std::optional<RemoteAllocation> remote_config = remote_alloc_write(
        process_info.hProcess,
        &config,
        sizeof(config),
        PAGE_READWRITE,
        error);
    if (!remote_config.has_value()) {
        return finish(13, error);
    }

    const std::uintptr_t initialize_addr = remote_export_address(
        process_info.dwProcessId,
        L"dwrt_host.dll",
        options.host_dll,
        "dwrt_host_initialize",
        error);
    if (initialize_addr == 0) {
        return finish(14, error);
    }

    if (!run_remote_thread(
            process_info.hProcess,
            initialize_addr,
            remote_config->address,
            30'000,
            init_status,
            error)) {
        return finish(15, error);
    }
    if (init_status != DWRT_HOST_OK) {
        std::ostringstream out;
        out << "dwrt_host_initialize returned " << init_status;
        return finish(16, out.str());
    }

    std::optional<RemoteAllocation> remote_snapshot = remote_alloc_write(
        process_info.hProcess,
        &host_snapshot,
        sizeof(host_snapshot),
        PAGE_READWRITE,
        error);
    if (!remote_snapshot.has_value()) {
        return finish(17, error);
    }
    std::optional<RemoteAllocation> remote_probe_snapshot = remote_alloc_write(
        process_info.hProcess,
        &probe_snapshot,
        sizeof(probe_snapshot),
        PAGE_READWRITE,
        error);
    if (!remote_probe_snapshot.has_value()) {
        return finish(18, error);
    }
    std::optional<RemoteAllocation> remote_walker_patrol_snapshot = remote_alloc_write(
        process_info.hProcess,
        &walker_patrol_snapshot,
        sizeof(walker_patrol_snapshot),
        PAGE_READWRITE,
        error);
    if (!remote_walker_patrol_snapshot.has_value()) {
        return finish(19, error);
    }
    std::optional<RemoteAllocation> remote_friendly_fire_snapshot = remote_alloc_write(
        process_info.hProcess,
        &friendly_fire_snapshot,
        sizeof(friendly_fire_snapshot),
        PAGE_READWRITE,
        error);
    if (!remote_friendly_fire_snapshot.has_value()) {
        return finish(19, error);
    }
    std::optional<RemoteAllocation> remote_target_probe_snapshot = remote_alloc_write(
        process_info.hProcess,
        &target_probe_snapshot,
        sizeof(target_probe_snapshot),
        PAGE_READWRITE,
        error);
    if (!remote_target_probe_snapshot.has_value()) {
        return finish(19, error);
    }

    const std::uintptr_t snapshot_addr = remote_export_address(
        process_info.dwProcessId,
        L"dwrt_host.dll",
        options.host_dll,
        "dwrt_host_snapshot",
        error);
    if (snapshot_addr == 0) {
        return finish(19, error);
    }
    const std::uintptr_t set_probe_mount_addr = remote_export_address(
        process_info.dwProcessId,
        L"dwrt_host.dll",
        options.host_dll,
        "dwrt_host_set_probe_mount_mask",
        error);
    if (set_probe_mount_addr == 0) {
        return finish(20, error);
    }
    const std::uintptr_t reset_probe_counters_addr = remote_export_address(
        process_info.dwProcessId,
        L"dwrt_host.dll",
        options.host_dll,
        "dwrt_host_reset_probe_counters",
        error);
    if (reset_probe_counters_addr == 0) {
        return finish(21, error);
    }
    const std::uintptr_t probe_snapshot_addr = remote_export_address(
        process_info.dwProcessId,
        L"dwrt_host.dll",
        options.host_dll,
        "dwrt_host_probe_snapshot",
        error);
    if (probe_snapshot_addr == 0) {
        return finish(22, error);
    }
    const std::uintptr_t walker_patrol_snapshot_addr = remote_export_address(
        process_info.dwProcessId,
        L"dwrt_host.dll",
        options.host_dll,
        "dwrt_host_walker_patrol_snapshot",
        error);
    if (walker_patrol_snapshot_addr == 0) {
        return finish(23, error);
    }
    const std::uintptr_t friendly_fire_snapshot_addr = remote_export_address(
        process_info.dwProcessId,
        L"dwrt_host.dll",
        options.host_dll,
        "dwrt_host_friendly_fire_snapshot",
        error);
    if (friendly_fire_snapshot_addr == 0) {
        return finish(23, error);
    }
    const std::uintptr_t target_probe_snapshot_addr = remote_export_address(
        process_info.dwProcessId,
        L"dwrt_host.dll",
        options.host_dll,
        "dwrt_host_target_probe_snapshot",
        error);
    if (target_probe_snapshot_addr == 0) {
        return finish(23, error);
    }

    auto refresh_host_snapshot = [&](std::string& snapshot_error) -> bool {
        if (!run_remote_thread(
                process_info.hProcess,
                snapshot_addr,
                remote_snapshot->address,
                30'000,
                snapshot_status,
                snapshot_error)) {
            return false;
        }
        if (snapshot_status != DWRT_HOST_OK) {
            std::ostringstream out;
            out << "dwrt_host_snapshot returned " << snapshot_status;
            snapshot_error = out.str();
            return false;
        }
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(
                process_info.hProcess,
                remote_snapshot->address,
                &host_snapshot,
                sizeof(host_snapshot),
                &bytes_read) || bytes_read != sizeof(host_snapshot)) {
            snapshot_error = windows_error_message("ReadProcessMemory for host snapshot failed");
            return false;
        }
        return true;
    };

    auto refresh_probe_snapshot = [&](std::string& snapshot_error) -> bool {
        if (!run_remote_thread(
                process_info.hProcess,
                probe_snapshot_addr,
                remote_probe_snapshot->address,
                30'000,
                probe_snapshot_status,
                snapshot_error)) {
            return false;
        }
        if (probe_snapshot_status != DWRT_HOST_OK) {
            std::ostringstream out;
            out << "dwrt_host_probe_snapshot returned " << probe_snapshot_status;
            snapshot_error = out.str();
            return false;
        }
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(
                process_info.hProcess,
                remote_probe_snapshot->address,
                &probe_snapshot,
                sizeof(probe_snapshot),
                &bytes_read) || bytes_read != sizeof(probe_snapshot)) {
            snapshot_error = windows_error_message("ReadProcessMemory for probe snapshot failed");
            return false;
        }
        return true;
    };

    auto refresh_walker_patrol_snapshot = [&](std::string& snapshot_error) -> bool {
        if (!run_remote_thread(
                process_info.hProcess,
                walker_patrol_snapshot_addr,
                remote_walker_patrol_snapshot->address,
                30'000,
                walker_patrol_snapshot_status,
                snapshot_error)) {
            return false;
        }
        if (walker_patrol_snapshot_status != DWRT_HOST_OK) {
            std::ostringstream out;
            out << "dwrt_host_walker_patrol_snapshot returned " << walker_patrol_snapshot_status;
            snapshot_error = out.str();
            return false;
        }
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(
                process_info.hProcess,
                remote_walker_patrol_snapshot->address,
                &walker_patrol_snapshot,
                sizeof(walker_patrol_snapshot),
                &bytes_read) || bytes_read != sizeof(walker_patrol_snapshot)) {
            snapshot_error = windows_error_message("ReadProcessMemory for walker patrol snapshot failed");
            return false;
        }
        return true;
    };

    auto refresh_friendly_fire_snapshot = [&](std::string& snapshot_error) -> bool {
        if (!run_remote_thread(
                process_info.hProcess,
                friendly_fire_snapshot_addr,
                remote_friendly_fire_snapshot->address,
                30'000,
                friendly_fire_snapshot_status,
                snapshot_error)) {
            return false;
        }
        if (friendly_fire_snapshot_status != DWRT_HOST_OK) {
            std::ostringstream out;
            out << "dwrt_host_friendly_fire_snapshot returned " << friendly_fire_snapshot_status;
            snapshot_error = out.str();
            return false;
        }
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(
                process_info.hProcess,
                remote_friendly_fire_snapshot->address,
                &friendly_fire_snapshot,
                sizeof(friendly_fire_snapshot),
                &bytes_read) || bytes_read != sizeof(friendly_fire_snapshot)) {
            snapshot_error = windows_error_message("ReadProcessMemory for friendly fire snapshot failed");
            return false;
        }
        return true;
    };

    auto refresh_target_probe_snapshot = [&](std::string& snapshot_error) -> bool {
        if (!run_remote_thread(
                process_info.hProcess,
                target_probe_snapshot_addr,
                remote_target_probe_snapshot->address,
                30'000,
                target_probe_snapshot_status,
                snapshot_error)) {
            return false;
        }
        if (target_probe_snapshot_status != DWRT_HOST_OK) {
            std::ostringstream out;
            out << "dwrt_host_target_probe_snapshot returned " << target_probe_snapshot_status;
            snapshot_error = out.str();
            return false;
        }
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(
                process_info.hProcess,
                remote_target_probe_snapshot->address,
                &target_probe_snapshot,
                sizeof(target_probe_snapshot),
                &bytes_read) || bytes_read != sizeof(target_probe_snapshot)) {
            snapshot_error = windows_error_message("ReadProcessMemory for target probe snapshot failed");
            return false;
        }
        return true;
    };

    if (!refresh_host_snapshot(error)) {
        return finish(23, error);
    }

    if (host_snapshot.initialized == 0 || host_snapshot.runtime_loaded == 0 ||
        host_snapshot.runtime_probe_ok == 0 || host_snapshot.signatures_checked == 0 ||
        host_snapshot.signature_required_failures != 0 ||
        host_snapshot.used_live_server_module == 0 ||
        host_snapshot.used_mapped_file_fallback != 0 ||
        (options.install_probe_hooks &&
            (host_snapshot.hook_install_attempts != 7 ||
             host_snapshot.hooks_installed != 7 ||
             host_snapshot.hook_install_failures != 0)) ||
        (!options.install_probe_hooks &&
            (host_snapshot.hook_install_attempts != 0 || host_snapshot.hooks_installed != 0)) ||
        host_snapshot.initialize_calls != 1 ||
        host_snapshot.initialize_reentrant_rejects != 0 ||
        host_snapshot.callback_recursive_entries != 0) {
        return finish(24, "host snapshot failed live-server validation gates");
    }

    if (!run_remote_thread(
            process_info.hProcess,
            set_probe_mount_addr,
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(options.probe_mount_mask)),
            30'000,
            probe_mount_status,
            error)) {
        return finish(25, error);
    }
    if (probe_mount_status != DWRT_HOST_OK) {
        std::ostringstream out;
        out << "dwrt_host_set_probe_mount_mask returned " << probe_mount_status;
        return finish(26, out.str());
    }
    if (!run_remote_thread(
            process_info.hProcess,
            reset_probe_counters_addr,
            nullptr,
            30'000,
            probe_reset_status,
            error)) {
        return finish(27, error);
    }
    if (probe_reset_status != DWRT_HOST_OK) {
        std::ostringstream out;
        out << "dwrt_host_reset_probe_counters returned " << probe_reset_status;
        return finish(28, out.str());
    }
    if (!refresh_probe_snapshot(error) || !refresh_walker_patrol_snapshot(error) || !refresh_friendly_fire_snapshot(error) || !refresh_target_probe_snapshot(error)) {
        return finish(29, error);
    }

    const DWORD session_start_tick = GetTickCount();
    auto elapsed_seconds = [&]() -> double {
        return static_cast<double>(GetTickCount() - session_start_tick) / 1000.0;
    };
    auto append_snapshot = [&](const char* phase) -> bool {
        if (options.snapshot_jsonl_path.empty()) {
            return true;
        }
        std::string write_error;
        if (!append_text_file(
                options.snapshot_jsonl_path,
                build_snapshot_jsonl(phase, elapsed_seconds(), host_snapshot, probe_snapshot, walker_patrol_snapshot, friendly_fire_snapshot, target_probe_snapshot),
                write_error)) {
            error = write_error;
            return false;
        }
        return true;
    };

    if (!append_snapshot("initial")) {
        return finish(30, error);
    }

    if (options.hold_seconds != 0) {
        std::cout << "[dwrt-live-server-smoke] session ready; serverArgs=\""
                  << narrow(options.server_args) << "\"; holdSeconds=" << options.hold_seconds
                  << ", probeMountMask=" << options.probe_mount_mask << '\n';
    }

    const DWORD hold_ms = options.hold_seconds * 1000U;
    const DWORD poll_ms = options.poll_seconds == 0 ? 0 : options.poll_seconds * 1000U;
    DWORD next_poll_ms = poll_ms;
    while (GetTickCount() - session_start_tick < hold_ms) {
        if (WaitForSingleObject(process_info.hProcess, 0) == WAIT_OBJECT_0) {
            return finish(31, "server process exited during hold");
        }
        if (!options.stop_file_path.empty()) {
            std::error_code exists_error;
            if (std::filesystem::exists(options.stop_file_path, exists_error)) {
                completion_reason = "stop-file";
                break;
            }
        }
        const DWORD elapsed_ms = GetTickCount() - session_start_tick;
        if (poll_ms != 0 && elapsed_ms >= next_poll_ms) {
            if (!refresh_host_snapshot(error) || !refresh_probe_snapshot(error) || !refresh_walker_patrol_snapshot(error) || !refresh_friendly_fire_snapshot(error) || !refresh_target_probe_snapshot(error)) {
                return finish(32, error);
            }
            if (!append_snapshot("poll")) {
                return finish(33, error);
            }
            next_poll_ms += poll_ms;
        }
        Sleep(250);
    }
    if (completion_reason == "completed") {
        completion_reason = "duration-elapsed";
    }

    if (!refresh_host_snapshot(error) || !refresh_probe_snapshot(error) || !refresh_walker_patrol_snapshot(error) || !refresh_friendly_fire_snapshot(error) || !refresh_target_probe_snapshot(error)) {
        return finish(34, error);
    }
    if (!append_snapshot("final")) {
        return finish(35, error);
    }
    if (!options.allow_recursive_callbacks && host_snapshot.callback_recursive_entries != 0) {
        return finish(36, "unexpected recursive callback entries during hold");
    }

    exit_code = finish(0, "");
    if (exit_code == 0) {
        std::cout << "[dwrt-live-server-smoke] OK\n";
    }
    return exit_code;
}
