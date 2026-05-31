#include "dwrt_probe_manifest.hpp"

#include <array>

namespace dwrt::host {
namespace {

constexpr std::array<SignatureDescriptor, 13> kDefaultProbeSignatures{{
    {
        "CBaseEntity::TakeDamageOld",
        "server.dll",
        "EntitySimulation/DamagePolicy",
        "40 55 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 48 8D 6C 24 ?? 48 89 9D ?? ?? ?? ?? 45 33 ED",
        0x00c6ba60,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntityInstance::AcceptInput",
        "server.dll",
        "EntityIo",
        "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B F0 48 8B D9",
        0x01f176c0,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntityIOOutput::FireOutputInternal",
        "server.dll",
        "EntityIo",
        "4C 89 4C 24 ?? 48 89 4C 24 ?? 53 56",
        0x01f1cee0,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CModifierProperty::AddModifier",
        "server.dll",
        "EntitySimulation/Modifiers",
        "44 89 44 24 ?? 48 89 54 24 ?? 53 55 56 57 41 55",
        0x014d5d30,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntitySystem::CreateEntityByName",
        "server.dll",
        "MapEntities",
        "48 83 EC ?? 48 8B 0D ?? ?? ?? ?? 41 8B C0",
        0x017c33e0,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntitySystem::QueueSpawnEntity",
        "server.dll",
        "MapEntities",
        "40 56 57 41 56 48 83 EC ?? F7 42",
        0x01f0ddb0,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntitySystem::ExecuteQueuedCreation",
        "server.dll",
        "MapEntities",
        "48 89 5C 24 ?? 57 48 81 EC ?? ?? ?? ?? FF 81 ?? ?? ?? ?? 48 8D 44 24",
        0x01f06120,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CTakeDamageInfo::Ctor",
        "server.dll",
        "DamagePolicy",
        "40 53 48 83 EC 50 F3 0F 10 84 24 80 00 00 00 48 8D 05",
        0x01addd40,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CCitadelGameRules::BuildGameSessionManifest",
        "server.dll",
        "GameRules/Resources",
        "48 89 54 24 ?? 48 89 4C 24 ?? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? ?? ?? ?? 4C 8B FA",
        0x008f7410,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CBasePlayerController::ProcessUsercmds",
        "server.dll",
        "UsercmdPipeline",
        "48 8B C4 44 88 48 ?? 44 89 40 ?? 48 89 50 ?? 53",
        0x0174ac50,
        true,
        "Deadworks mem config + existing DWRT usercmd shadow path",
    },
    {
        "CCitadelPlayerPawn::AbilityThink",
        "server.dll",
        "Ability/TargetingProbe",
        "48 89 4C 24 ?? 55 53 56 41 55 41 57",
        0x00a2e060,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "TraceShape",
        "server.dll",
        "Physics",
        "48 89 5C 24 20 48 89 4C 24 08 55 57 41 54 41 55 41 56 48 8D AC 24",
        0x01803c00,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CBaseEntity::EmitSoundParams",
        "server.dll",
        "EntitySimulation/Audio",
        "48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 48 8B EC 48 81 EC ?? ?? ?? ?? 33 C0",
        0x018cd210,
        false,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
}};

}  // namespace

std::span<const SignatureDescriptor> default_probe_signatures() {
    return kDefaultProbeSignatures;
}

}  // namespace dwrt::host
