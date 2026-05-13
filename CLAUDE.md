# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Build & Setup
- **Initialize submodules**: `git submodule update --init --recursive`
- **Pull LFS assets**: `git lfs pull`
- **Build native library**: `ndk-build -C jni`
- **Build Output**: `libs/arm64-v8a/libmain.so`

### Verification
- There is no unit test framework. Verification is a successful build: `ndk-build -C jni`.
- For IL2CPP changes, verify method signatures and field offsets against `dump/dump.cs`.
- Run `git diff --check` for native or mixed code changes.
- For documentation-only edits, inspect the Markdown diff before finishing.

## Code Architecture & Standards

### High-Level Architecture
- **Core Logic**: `jni/Main.cpp` handles the entire mod lifecycle: process verification → setup thread → dependency resolution (`libil2cpp.so`, `liblogic.so`) → IL2CPP API attachment → retryable game method and field resolution → function hooking (via Dobby) → managed reference refresh → feature ticks → ImGui overlay rendering.
- **Feature Binding**: `ResolveFeatureBindings()` resolves game methods and hooks. Missing methods and fields are retried periodically because Unity metadata and battle objects may not be ready during first setup.
- **Hooking Strategy**: Uses Dobby to hook `eglSwapBuffers` for frame-by-frame UI injection, `UnityEngine.Input.GetTouch` for touch-to-mouse forwarding, and selected game methods for Combat and Arena behavior.
- **Runtime Ticks**: Shop automation and Arena effects run on separate 100 ms ticks for stability and responsiveness.
- **Runtime Caches**: Managed references and hero/equipment/GogoCard table data are cached, with table caches refreshed when entering a new match.
- **Memory Mapping**: `jni/structures/Structures.hpp` defines the layout of Unity/Mono types to allow native interaction with managed objects.
- **Reference**: `dump/dump.cs` serves as the source of truth for the target game's internal C# structure.

### Current Feature Areas
- **Info**: player/enemy table and GGC round 7/13 quality display.
- **Combat**: Invisible Scout toggle.
- **Shop**: auto-buy free heroes, auto-buy selected targets, auto-refresh, stop conditions, keep-gold threshold, and hero target counts.
- **Arena**: hero spawn, equipment grant, GogoCard forcing, active synergy forcing, level 99, outside-map placement, enemy HP 1, and gold grant helpers.

### Project Constraints
- **Target ABI**: `arm64-v8a`
- **Unity Version**: `2019.4.22f1` (Refer to `UNITY_` macros in `jni/Android.mk`)
- **C++ Standard**: C++26 (defined in `jni/Android.mk`)

### Coding Style
- **Indentation**: 4 spaces.
- **Naming**: Unity compatibility macros must use the `UNITY_` prefix.
- **Pointers**: Prefer `void*` for managed objects unless a specific structure from `Structures.hpp` is required.
- **Single-file feature work**: Keep feature runtime changes in `jni/Main.cpp` unless explicitly asked to split files.
- **Retry behavior**: Do not permanently cache failed IL2CPP method or field lookups. Missing bindings should retry and user-facing controls should show `Waiting for ...` states where practical.
- **Comments**: Add concise comments before risky IL2CPP calls, hook signatures, value-type layouts, or timing-sensitive blocks.
- **Scope**: Do not modify vendored directories (`jni/imgui/`, `jni/xDL/`, `jni/dobby/`, `jni/Il2CppVersions/`) unless explicitly requested.
