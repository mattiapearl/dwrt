#include "dwrt_walker_patrol.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <system_error>

namespace dwrt::host {
namespace {

constexpr std::size_t kMaxWaypoints = 8;
constexpr std::uint32_t kTeleportVtableIndex = 163;
constexpr std::uint32_t kModeVelocity = 0;
constexpr std::uint32_t kModeOriginNudge = 1;

// Current-build schema evidence, not public API:
// - CBaseEntity::m_CBodyComponent = 0x30 (schema registration 0x180e7ee20)
// - CBodyComponent::m_pSceneNode = 0x8 (schema metadata string table)
// - CGameSceneNode::m_vecAbsOrigin = 0xc8 (schema metadata string table)
constexpr std::size_t kCBaseEntityBodyComponentOffset = 0x30;
constexpr std::size_t kCBodyComponentSceneNodeOffset = 0x8;
constexpr std::size_t kCGameSceneNodeAbsOriginOffset = 0xc8;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct WalkerPatrolState {
    std::atomic<std::uint32_t> enabled{0};
    std::atomic<std::uint32_t> stride{16};
    std::atomic<std::uint32_t> mode{kModeVelocity};
    std::array<Vec3, kMaxWaypoints> vectors{};
    std::atomic<std::uint32_t> waypoint_count{0};
    std::atomic<std::uint64_t> damage_callbacks{0};
    std::atomic<std::uint64_t> candidate_walkers{0};
    std::atomic<std::uint64_t> non_walker_victims{0};
    std::atomic<std::uint64_t> skipped_recursive{0};
    std::atomic<std::uint64_t> missing_identity{0};
    std::atomic<std::uint64_t> missing_designer_name{0};
    std::atomic<std::uint64_t> teleport_attempts{0};
    std::atomic<std::uint64_t> teleport_calls{0};
    std::atomic<std::uint64_t> body_component_missing{0};
    std::atomic<std::uint64_t> scene_node_missing{0};
    std::atomic<std::uint64_t> origin_read_attempts{0};
    std::atomic<std::uint64_t> origin_read_successes{0};
    std::atomic<std::uint64_t> origin_read_failures{0};
};

struct EntityIdentityLite {
    void* instance;
    void* entity_class;
    std::uint32_t ehandle;
    std::int32_t name_stringable_index;
    const char* name;
    const char* designer_name;
};

WalkerPatrolState g_walker_patrol;

bool env_flag_enabled(const wchar_t* name) {
    wchar_t buffer[32] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return false;
    }
    return buffer[0] == L'1' || buffer[0] == L't' || buffer[0] == L'T' ||
        buffer[0] == L'y' || buffer[0] == L'Y';
}

std::uint32_t env_u32_or(const wchar_t* name, std::uint32_t fallback) {
    wchar_t buffer[32] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return fallback;
    }
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(buffer, &end, 10);
    if (end == buffer || value > 100000UL) {
        return fallback;
    }
    return static_cast<std::uint32_t>(std::max<unsigned long>(1UL, value));
}

std::uint32_t env_mode_or_velocity() {
    if (env_flag_enabled(L"DWRT_WALKER_PATROL_ORIGIN_NUDGE")) {
        return kModeOriginNudge;
    }

    char buffer[32] = {};
    const DWORD length = GetEnvironmentVariableA("DWRT_WALKER_PATROL_MODE", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return kModeVelocity;
    }
    const std::string_view text(buffer, length);
    if (text == "origin" || text == "origin-nudge" || text == "OriginNudge" || text == "ORIGIN_NUDGE") {
        return kModeOriginNudge;
    }
    return kModeVelocity;
}

void set_default_vectors(std::uint32_t mode) {
    if (mode == kModeOriginNudge) {
        g_walker_patrol.vectors[0] = Vec3{500.0f, 0.0f, 0.0f};
        g_walker_patrol.vectors[1] = Vec3{0.0f, 500.0f, 0.0f};
        g_walker_patrol.vectors[2] = Vec3{-500.0f, 0.0f, 0.0f};
        g_walker_patrol.vectors[3] = Vec3{0.0f, -500.0f, 0.0f};
    }
    else {
        g_walker_patrol.vectors[0] = Vec3{900.0f, 0.0f, 0.0f};
        g_walker_patrol.vectors[1] = Vec3{0.0f, 900.0f, 0.0f};
        g_walker_patrol.vectors[2] = Vec3{-900.0f, 0.0f, 0.0f};
        g_walker_patrol.vectors[3] = Vec3{0.0f, -900.0f, 0.0f};
    }
    g_walker_patrol.waypoint_count.store(4, std::memory_order_release);
}

bool parse_float(std::string_view text, float& out) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

bool parse_vec3(std::string_view text, Vec3& out) {
    const std::size_t first_comma = text.find(',');
    if (first_comma == std::string_view::npos) {
        return false;
    }
    const std::size_t second_comma = text.find(',', first_comma + 1);
    if (second_comma == std::string_view::npos) {
        return false;
    }
    Vec3 parsed{};
    if (!parse_float(text.substr(0, first_comma), parsed.x) ||
        !parse_float(text.substr(first_comma + 1, second_comma - first_comma - 1), parsed.y) ||
        !parse_float(text.substr(second_comma + 1), parsed.z)) {
        return false;
    }
    out = parsed;
    return true;
}

void parse_vector_environment(std::uint32_t mode) {
    char buffer[512] = {};
    const DWORD length = GetEnvironmentVariableA("DWRT_WALKER_PATROL_VELOCITIES", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        set_default_vectors(mode);
        return;
    }

    std::string_view remaining(buffer, length);
    std::array<Vec3, kMaxWaypoints> parsed{};
    std::uint32_t count = 0;
    while (!remaining.empty() && count < kMaxWaypoints) {
        const std::size_t separator = remaining.find(';');
        const std::string_view item = separator == std::string_view::npos ? remaining : remaining.substr(0, separator);
        Vec3 value{};
        if (!item.empty() && parse_vec3(item, value)) {
            parsed[count] = value;
            count += 1;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(separator + 1);
    }

    if (count == 0) {
        set_default_vectors(mode);
        return;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        g_walker_patrol.vectors[index] = parsed[index];
    }
    g_walker_patrol.waypoint_count.store(count, std::memory_order_release);
}

EntityIdentityLite* entity_identity(void* entity) {
    if (entity == nullptr) {
        return nullptr;
    }
    auto* bytes = static_cast<std::byte*>(entity);
    return *reinterpret_cast<EntityIdentityLite**>(bytes + 0x10);
}

bool is_walker_designer_name(const char* designer_name) {
    if (designer_name == nullptr || designer_name[0] == '\0') {
        return false;
    }
    const std::string_view name(designer_name);
    return name == "npc_boss_tier2" ||
        name == "alt_npc_boss_tier2" ||
        name == "CNPC_Boss_Tier2";
}

bool safe_read_pointer(void* base, std::size_t offset, void*& out) {
    out = nullptr;
    if (base == nullptr) {
        return false;
    }
#if defined(_MSC_VER)
    __try {
        out = *reinterpret_cast<void**>(static_cast<std::byte*>(base) + offset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = nullptr;
        return false;
    }
#else
    out = *reinterpret_cast<void**>(static_cast<std::byte*>(base) + offset);
    return true;
#endif
}

bool safe_read_vec3(void* base, std::size_t offset, Vec3& out) {
    out = Vec3{};
    if (base == nullptr) {
        return false;
    }
#if defined(_MSC_VER)
    __try {
        out = *reinterpret_cast<Vec3*>(static_cast<std::byte*>(base) + offset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = Vec3{};
        return false;
    }
#else
    out = *reinterpret_cast<Vec3*>(static_cast<std::byte*>(base) + offset);
    return true;
#endif
}

bool plausible_origin(const Vec3& origin) {
    return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z) &&
        std::abs(origin.x) < 1000000.0f && std::abs(origin.y) < 1000000.0f && std::abs(origin.z) < 1000000.0f;
}

bool read_entity_abs_origin(void* entity, Vec3& out) {
    g_walker_patrol.origin_read_attempts.fetch_add(1, std::memory_order_relaxed);

    void* body_component = nullptr;
    if (!safe_read_pointer(entity, kCBaseEntityBodyComponentOffset, body_component) || body_component == nullptr) {
        g_walker_patrol.body_component_missing.fetch_add(1, std::memory_order_relaxed);
        g_walker_patrol.origin_read_failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    void* scene_node = nullptr;
    if (!safe_read_pointer(body_component, kCBodyComponentSceneNodeOffset, scene_node) || scene_node == nullptr) {
        g_walker_patrol.scene_node_missing.fetch_add(1, std::memory_order_relaxed);
        g_walker_patrol.origin_read_failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    Vec3 origin{};
    if (!safe_read_vec3(scene_node, kCGameSceneNodeAbsOriginOffset, origin) || !plausible_origin(origin)) {
        g_walker_patrol.origin_read_failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    out = origin;
    g_walker_patrol.origin_read_successes.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void teleport_entity(void* entity, const Vec3* position, const Vec3* velocity) {
    using TeleportFn = void(__fastcall*)(void*, const Vec3*, const Vec3*, const Vec3*);
    void** vtable = *static_cast<void***>(entity);
    const auto fn = reinterpret_cast<TeleportFn>(vtable[kTeleportVtableIndex]);
    fn(entity, position, nullptr, velocity);
}

}  // namespace

void configure_walker_patrol_from_environment() {
    const std::uint32_t mode = env_mode_or_velocity();
    g_walker_patrol.enabled.store(env_flag_enabled(L"DWRT_WALKER_PATROL_EXPERIMENT") ? 1U : 0U, std::memory_order_release);
    g_walker_patrol.stride.store(env_u32_or(L"DWRT_WALKER_PATROL_STRIDE", 16), std::memory_order_release);
    g_walker_patrol.mode.store(mode, std::memory_order_release);
    parse_vector_environment(mode);
}

void reset_walker_patrol_counters() {
    g_walker_patrol.damage_callbacks.store(0, std::memory_order_relaxed);
    g_walker_patrol.candidate_walkers.store(0, std::memory_order_relaxed);
    g_walker_patrol.non_walker_victims.store(0, std::memory_order_relaxed);
    g_walker_patrol.skipped_recursive.store(0, std::memory_order_relaxed);
    g_walker_patrol.missing_identity.store(0, std::memory_order_relaxed);
    g_walker_patrol.missing_designer_name.store(0, std::memory_order_relaxed);
    g_walker_patrol.teleport_attempts.store(0, std::memory_order_relaxed);
    g_walker_patrol.teleport_calls.store(0, std::memory_order_relaxed);
    g_walker_patrol.body_component_missing.store(0, std::memory_order_relaxed);
    g_walker_patrol.scene_node_missing.store(0, std::memory_order_relaxed);
    g_walker_patrol.origin_read_attempts.store(0, std::memory_order_relaxed);
    g_walker_patrol.origin_read_successes.store(0, std::memory_order_relaxed);
    g_walker_patrol.origin_read_failures.store(0, std::memory_order_relaxed);
}

void maybe_apply_walker_patrol_on_damage(void* entity, bool recursive) {
    if (g_walker_patrol.enabled.load(std::memory_order_acquire) == 0) {
        return;
    }
    g_walker_patrol.damage_callbacks.fetch_add(1, std::memory_order_relaxed);
    if (recursive) {
        g_walker_patrol.skipped_recursive.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const EntityIdentityLite* identity = entity_identity(entity);
    if (identity == nullptr) {
        g_walker_patrol.missing_identity.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const char* designer_name = identity->designer_name;
    if (designer_name == nullptr || designer_name[0] == '\0') {
        g_walker_patrol.missing_designer_name.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!is_walker_designer_name(designer_name)) {
        g_walker_patrol.non_walker_victims.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const std::uint64_t walker_index = g_walker_patrol.candidate_walkers.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint32_t stride = std::max(1U, g_walker_patrol.stride.load(std::memory_order_relaxed));
    if ((walker_index % stride) != 0) {
        return;
    }

    const std::uint32_t waypoint_count = g_walker_patrol.waypoint_count.load(std::memory_order_acquire);
    if (waypoint_count == 0) {
        return;
    }
    const Vec3 vector = g_walker_patrol.vectors[((walker_index / stride) - 1) % waypoint_count];

    g_walker_patrol.teleport_attempts.fetch_add(1, std::memory_order_relaxed);
    if (g_walker_patrol.mode.load(std::memory_order_acquire) == kModeOriginNudge) {
        Vec3 origin{};
        if (!read_entity_abs_origin(entity, origin)) {
            return;
        }
        const Vec3 target{origin.x + vector.x, origin.y + vector.y, origin.z + vector.z};
        teleport_entity(entity, &target, nullptr);
    }
    else {
        teleport_entity(entity, nullptr, &vector);
    }
    g_walker_patrol.teleport_calls.fetch_add(1, std::memory_order_relaxed);
}

DwrtWalkerPatrolSnapshot walker_patrol_snapshot() {
    DwrtWalkerPatrolSnapshot snapshot{};
    snapshot.enabled = g_walker_patrol.enabled.load(std::memory_order_acquire);
    snapshot.stride = g_walker_patrol.stride.load(std::memory_order_relaxed);
    snapshot.waypoint_count = g_walker_patrol.waypoint_count.load(std::memory_order_acquire);
    snapshot.mode = g_walker_patrol.mode.load(std::memory_order_acquire);
    snapshot.damage_callbacks = g_walker_patrol.damage_callbacks.load(std::memory_order_relaxed);
    snapshot.candidate_walkers = g_walker_patrol.candidate_walkers.load(std::memory_order_relaxed);
    snapshot.non_walker_victims = g_walker_patrol.non_walker_victims.load(std::memory_order_relaxed);
    snapshot.skipped_recursive = g_walker_patrol.skipped_recursive.load(std::memory_order_relaxed);
    snapshot.missing_identity = g_walker_patrol.missing_identity.load(std::memory_order_relaxed);
    snapshot.missing_designer_name = g_walker_patrol.missing_designer_name.load(std::memory_order_relaxed);
    snapshot.teleport_attempts = g_walker_patrol.teleport_attempts.load(std::memory_order_relaxed);
    snapshot.teleport_calls = g_walker_patrol.teleport_calls.load(std::memory_order_relaxed);
    snapshot.body_component_missing = g_walker_patrol.body_component_missing.load(std::memory_order_relaxed);
    snapshot.scene_node_missing = g_walker_patrol.scene_node_missing.load(std::memory_order_relaxed);
    snapshot.origin_read_attempts = g_walker_patrol.origin_read_attempts.load(std::memory_order_relaxed);
    snapshot.origin_read_successes = g_walker_patrol.origin_read_successes.load(std::memory_order_relaxed);
    snapshot.origin_read_failures = g_walker_patrol.origin_read_failures.load(std::memory_order_relaxed);
    return snapshot;
}

}  // namespace dwrt::host
