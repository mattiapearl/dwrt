# Native third-party hook backend

This directory contains local native third-party dependencies used by the DWRT host hook backend.

- `safetyhook.hpp` / `safetyhook.cpp`: amalgamated SafetyHook source copied from the local Deadworks vendor tree for DWRT-owned hook installation experiments.
- `Zydis.h` / `Zydis.c`: amalgamated Zydis dependency used by SafetyHook. The Zydis amalgamation includes its MIT license header.

Before a public release, re-check upstream SafetyHook license/version metadata and replace this copy with an explicit package/submodule or committed license file.
