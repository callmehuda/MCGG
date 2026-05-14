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
- **Core Logic**: `jni/Main.cpp` handles the entire mod lifecycle: process verification → setup thread → dependency resolution (`liblogic.so`) → IL2CPP API attachment → retryable game method and field resolution → function hooking (via Dobby) → managed reference refresh → feature ticks → appearance/config setup → ImGui overlay rendering.
- **Feature Binding**: `ResolveFeatureBindings()` resolves game methods and hooks. Missing methods and fields are retried periodically because Unity metadata and battle objects may not be ready during first setup.
- **Hooking Strategy**: Uses Dobby to hook `eglSwapBuffers` for frame-by-frame UI injection, `UnityEngine.Input.GetTouch` for touch-to-mouse forwarding, and selected game methods for Combat and Arena behavior.
- **Runtime Ticks**: Shop automation and Arena effects run on separate 100 ms ticks for stability and responsiveness. Shop automation also uses bounded cooldowns for buy, repeat-buy, refresh, target-worth, and Recommendation Lineup checks.
- **Runtime Caches**: Managed references are cached through atomic pointers. Hero/equipment/GogoCard table data is collected locally and published under `RuntimeMutex::FeatureMutex` when entering a new match.
- **Diagnostics**: Runtime Status and Test tabs expose binding readiness, Recommendation Lineup readiness, managed reference refresh, round state, battle manager fields, behavior API state, all manager entries, and opponent prediction signals.
- **Configuration**: Settings saves and loads visual settings plus Combat, Shop, and Arena state from `/data/data/<game-package>/files/mcgg_config.ini`.
- **Memory Mapping**: `jni/structures/Structures.hpp` defines the layout of Unity/Mono types to allow native interaction with managed objects.
- **Reference**: `dump/dump.cs` serves as the source of truth for the target game's internal C# structure.

### Current Feature Areas
- **Info**: runtime status, player/enemy table, and GGC round 7/13 quality display.
- **Combat**: Invisible Scout toggle.
- **Appearance**: ImGui Dark/Catppuccin Mocha theme selection plus Default/Noto Sans CJK font selection.
- **Settings**: menu size, fixed position, font scale, style tuning, and save/load configuration.
- **Shop**: auto-buy free heroes, auto-buy selected targets, auto-buy Recommendation Lineup heroes, auto-refresh, pause-refresh conditions, keep-gold threshold, manual target counts, and Recommendation Lineup target counts.
- **Arena**: hero spawn, equipment grant, GogoCard forcing, active synergy forcing, level 99, outside-map placement, enemy HP 1, and gold grant helpers.
- **Test**: manual binding retry, account inspection, fight prediction, round state, battle manager, behavior API, and all-manager diagnostics.

### Project Constraints
- **Target ABI**: `arm64-v8a`
- **Android Platform**: `android-21`
- **STL**: `c++_static`
- **Unity Version**: `2019.4.22f1` (Refer to `UNITY_` macros in `jni/Android.mk`)
- **C++ Standard**: C++26 (defined in `jni/Android.mk`)
- **NDK App Settings**: `APP_OPTIM := release`, `APP_THIN_ARCHIVE := false`, `APP_PIE := true`
- **Native C Flags**: `-Oz` and `-DNDEBUG` by default; `NDK_DEBUG=1` adds `-O0`

### Shared State Discipline

- `RuntimeMutex::CacheMutex` protects IL2CPP method and field caches.
- `RuntimeMutex::FeatureMutex` protects complex feature collections, including `FeatureState::Heroes`, `FeatureState::Equips`, `FeatureState::Cards`, and `FeatureState::ShopSelectedHeroes`.
- `RuntimeMutex::UiMutex` protects UI/config strings and config save/load status.
- Primitive feature flags, counters, runtime readiness flags, and managed reference pointers use `std::atomic`; follow the existing `.load()` and assignment patterns.
- Use existing snapshot/access helpers such as `GetSortedHeroes()`, `GetSortedEquips()`, `GetSortedCards()`, `TryGetHeroTableEntry()`, `GetShopHeroTargetsSnapshot()`, and `GetSelectedShopHeroTargetsSnapshot()` instead of unlocked map reads.
- Do not hold `RuntimeMutex::FeatureMutex` while calling managed IL2CPP APIs. Gather local data first, then publish under the lock.

### Coding Style
- **Indentation**: 4 spaces.
- **Naming**: Unity compatibility macros must use the `UNITY_` prefix.
- **Pointers**: Prefer `void*` for managed objects unless a specific structure from `Structures.hpp` is required.
- **Single-file feature work**: Keep feature runtime changes in `jni/Main.cpp` unless explicitly asked to split files.
- **Retry behavior**: Do not permanently cache failed IL2CPP method or field lookups. Missing bindings should retry and user-facing controls should show `Waiting for ...` states where practical.
- **Shop automation**: Preserve the single-threaded, throttled frame-tick model. Use existing atomic toggles/counters and selected-target snapshot helpers. Avoid unbounded scans, immediate retry loops, or holding locks across managed calls in the hot path unless a future design explicitly requires them.
- **Comments**: Add concise comments before risky IL2CPP calls, hook signatures, value-type layouts, or timing-sensitive blocks.
- **Scope**: Do not modify vendored directories (`jni/imgui/`, `jni/xDL/`, `jni/dobby/`, `jni/Il2CppVersions/`) unless explicitly requested.
- **Appearance**: Keep theme/font changes in the existing appearance setup and preserve fallback to the default ImGui font when Noto Sans CJK is unavailable.
- **Settings**: Keep persistence in the project config file under the game app data directory; do not re-enable ImGui `.ini` persistence unless explicitly requested.
