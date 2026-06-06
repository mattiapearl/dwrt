#include "dwrt_probe_manifest.hpp"

#include <array>

namespace dwrt::host {
namespace {

constexpr std::array<SignatureDescriptor, 17> kDefaultProbeSignatures{{
    {
        "CBaseEntity::TakeDamageOld",
        "server.dll",
        "EntitySimulation/DamagePolicy",
        "40 55 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 48 8D 6C 24 ?? 48 89 9D ?? ?? ?? ?? 45 33 ED",
        0x00c6bcb0,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntityInstance::AcceptInput",
        "server.dll",
        "EntityIo",
        "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B F0 48 8B D9",
        0x01f17910,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntityIOOutput::FireOutputInternal",
        "server.dll",
        "EntityIo",
        "4C 89 4C 24 ?? 48 89 4C 24 ?? 53 56",
        0x01f1d130,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CModifierProperty::AddModifier",
        "server.dll",
        "EntitySimulation/Modifiers",
        "44 89 44 24 ?? 48 89 54 24 ?? 53 55 56 57 41 55",
        0x014d5f80,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntitySystem::CreateEntityByName",
        "server.dll",
        "MapEntities",
        "48 83 EC ?? 48 8B 0D ?? ?? ?? ?? 41 8B C0",
        0x017c3630,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntitySystem::QueueSpawnEntity",
        "server.dll",
        "MapEntities",
        "40 56 57 41 56 48 83 EC ?? F7 42",
        0x01f0e000,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CEntitySystem::ExecuteQueuedCreation",
        "server.dll",
        "MapEntities",
        "48 89 5C 24 ?? 57 48 81 EC ?? ?? ?? ?? FF 81 ?? ?? ?? ?? 48 8D 44 24",
        0x01f06370,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CTakeDamageInfo::Ctor",
        "server.dll",
        "DamagePolicy",
        "40 53 48 83 EC 50 F3 0F 10 84 24 80 00 00 00 48 8D 05",
        0x01addf90,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CCitadelGameRules::BuildGameSessionManifest",
        "server.dll",
        "GameRules/Resources",
        "48 89 54 24 ?? 48 89 4C 24 ?? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? ?? ?? ?? 4C 8B FA",
        0x008f7510,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CBasePlayerController::ProcessUsercmds",
        "server.dll",
        "UsercmdPipeline",
        "48 8B C4 44 88 48 ?? 44 89 40 ?? 48 89 50 ?? 53",
        0x0174aea0,
        true,
        "Deadworks mem config + existing DWRT usercmd shadow path",
    },
    {
        "CCitadelPlayerPawn::AbilityThink",
        "server.dll",
        "Ability/TargetingProbe",
        "48 89 4C 24 ?? 55 53 56 41 55 41 57",
        0x00a2e2b0,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "TraceShape",
        "server.dll",
        "Physics",
        "48 89 5C 24 20 48 89 4C 24 08 55 57 41 54 41 55 41 56 48 8D AC 24",
        0x01803e50,
        true,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CBaseEntity::EmitSoundParams",
        "server.dll",
        "EntitySimulation/Audio",
        "48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 48 8B EC 48 81 EC ?? ?? ?? ?? 33 C0",
        0x018cd460,
        false,
        "Deadworks mem config + DWRT RE 2026-05-30",
    },
    {
        "CitadelTargetFilter::FriendlyFire",
        "server.dll",
        "TargetPolicy/FriendlyFire",
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 81 EC ?? ?? ?? ?? 48 8B DA 48 8B F9 BA FF FF FF FF 48 8D 0D",
        0x018d9180,
        true,
        "DWRT RE mp_friendlyfire usage 2026-05-31",
    },
    {
        "CitadelTargetFilter::FriendlyFireCaller",
        "server.dll",
        "TargetPolicy/FriendlyFire",
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 48 8B 02 48 8B E9 48 8B CA 49 8B F9 49 8B F0 48 8B DA FF 90 ?? ?? ?? ?? 83 F8 1A 75 04 B0 01 EB 11 4C 8B CF 4C 8B C6 48 8B D3 48 8B CD E8 ?? ?? ?? ??",
        0x007839d0,
        true,
        "DWRT RE caller of mp_friendlyfire filter 2026-05-31",
    },
    {
        "CitadelTargetFilter::SecondaryFriendlyFireGate",
        "server.dll",
        "TargetPolicy/FriendlyFire",
        "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B D8 48 8B FA 48 8B F1 4D 85 C0 74 ?? 48 8B 01 FF 90 ?? ?? ?? ?? 83 F8 01 75 ?? BA FF FF FF FF 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0 75 ?? 48 8B 05 ?? ?? ?? ?? 48 8B 40 08 80 38 00 75 ?? 48 3B DF 74 ?? 32 C0",
        0x01aedb10,
        true,
        "DWRT RE secondary mp_friendlyfire usage 2026-06-01",
    },
    {
        "CEntityIdentity::IndexForEntityInstance",
        "server.dll",
        "TargetPolicy/EntityBitset",
        "48 83 EC ?? 4C 8B 0D ?? ?? ?? ?? 4C 8B DA 48 8B 49 ?? 49 83 C1 ?? 48 85 C9 75 ?? C7 02 FF FF FF FF 48 8B C2 48 83 C4 ?? C3 48 89 1C 24 45 33 D2",
        0x01f18bb0,
        true,
        "DWRT RE target bitset identity classifier 2026-06-01",
    },
}};

}  // namespace

std::span<const SignatureDescriptor> default_probe_signatures() {
    return kDefaultProbeSignatures;
}

}  // namespace dwrt::host
