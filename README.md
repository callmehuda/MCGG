# MCGG

[English](README.md) · [Bahasa Indonesia](README.id.md)

[![CI Build](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml/badge.svg)](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml)
[![MIT License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
![Android](https://img.shields.io/badge/Android-native-brightgreen)
![ABI](https://img.shields.io/badge/ABI-arm64--v8a-blue)
![Unity](https://img.shields.io/badge/Unity-2019.4.22f1-black)
![NDK](https://img.shields.io/badge/NDK-r29-orange)

Open-source native Android research project for Magic Chess Go Go, focused on Unity/IL2CPP runtime analysis, native Android build workflows, and ImGui-based runtime diagnostics.

This repository builds an `arm64-v8a` shared library for a Unity `2019.4.22f1` IL2CPP Android environment. It is intended for learning, defensive research, reverse engineering practice, and authorized experimentation only.

## Table of Contents

- [Responsible Use](#responsible-use)
- [Project Status](#project-status)
- [Features](#features)
- [Architecture](#architecture)
- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Build](#build)
- [Repository Layout](#repository-layout)
- [Build Configuration](#build-configuration)
- [Runtime Flow](#runtime-flow)
- [Development Notes](#development-notes)
- [Troubleshooting](#troubleshooting)
- [Known Limitations](#known-limitations)
- [Security](#security)
- [Contributing](#contributing)
- [Third-Party Components](#third-party-components)
- [License](#license)

## Responsible Use

This project is provided for educational and research purposes only.

Before using or modifying this project, review and follow the Magic Chess Go Go Terms of Service:

https://us.skystone.games/mcgg-tos

Do not use this repository to harm live services, disrupt other players, bypass access controls, violate platform rules, or perform unauthorized activity. Any testing should be limited to environments and devices that you own or are explicitly authorized to analyze.

This README intentionally documents the build process, code structure, and engineering workflow. It does not provide runtime deployment, injection, evasion, bypass, or operational abuse instructions.

## Project Status

MCGG is an experimental native Android project. Internal game symbols, metadata, managed layouts, and Unity runtime details can change between game releases. Because of that, feature bindings are treated as retryable and may appear as unavailable until the expected runtime state exists.

The default supported target is:

- Android ABI: `arm64-v8a`
- Unity version: `2019.4.22f1`
- Android NDK: `r29`
- Build system: `ndk-build`
- C++ standard: `c++26`
- Primary branch: `master`
- Current overlay tabs: Info, Combat, Appearance, Settings, Shop, Arena, and Test

## Features

### Info

- Runtime status table for battle data, GGC, shop, Recommendation Lineup, arena, test, spectator, synergy, and placement bindings.
- Player and next-enemy table sorted with the local player first.
- GGC quality readout for round 7 and round 13.
- Overlay status indicators for delayed or unavailable bindings.

### Combat

- Invisible Scout toggle.

### Appearance

- ImGui Dark and Catppuccin Mocha theme selector.
- Default font and embedded Noto Sans CJK font selector.
- Font readiness status when the embedded Noto Sans CJK font is unavailable.

### Settings

- Menu size, optional fixed position, and window interaction controls.
- Font scale, opacity, rounding, border, padding, spacing, scrollbar, and indentation controls.
- Save and load for visual settings plus Combat, Shop, and Arena state.
- Default config path under the running game package, resolved as `/data/data/<game-package>/files/mcgg_config.ini`.

### Shop

- Auto-buy free heroes.
- Auto-buy selected hero targets.
- Auto-buy heroes from the active Recommendation Lineup.
- Auto-refresh shop with stop conditions for free heroes, selected targets, or Recommendation Lineup heroes.
- Gold reserve threshold for safer automation.
- Hero target table with configurable target counts.
- Recommendation Lineup target count for advanced shop automation.
- Buy and refresh throttles that reduce repeated actions during continuous automation.

### Arena

- Spawn heroes by table entry and star level.
- Grant equipment, including enhanced equipment.
- Force selected GogoCards.
- Force active synergies.
- Level 99 helper.
- Outside-map placement helper.
- Enemy HP 1 helper.
- Gold grant helper.

### Test

- Manual binding retry and managed reference refresh controls.
- Account inspection by self, opponent, or explicit account ID.
- Fight prediction table with direct, manager-derived, invasion-pair, and round-robin signals.
- Runtime readouts for round state, battle manager fields, behavior API state, and all manager entries.

Feature bindings are resolved against local reference artifacts and runtime IL2CPP metadata. Missing methods and fields are retried periodically instead of being permanently cached as unavailable. When a binding is not ready, the overlay reports a `Waiting for ...` state.

## Architecture

MCGG is organized around a small native runtime layer that coordinates Unity, IL2CPP, rendering, input forwarding, and feature binding.

At a high level, the project contains:

- A native Android module built with `ndk-build`.
- Unity `2019.4.22f1` IL2CPP API declarations.
- Runtime dynamic library lookup helpers.
- Dobby-based function hook integration.
- Dear ImGui rendering through OpenGL ES.
- Unity touch input forwarding into ImGui mouse input.
- Runtime appearance setup with disabled ImGui `.ini` persistence.
- Project-owned configuration persistence for overlay and feature state.
- Atomic primitive runtime state with dedicated mutex domains for IL2CPP
  caches, feature collections, and UI/config strings.
- Snapshot helpers for hero, equipment, GogoCard, and selected shop target
  data used by the overlay and throttled feature ticks.
- Local reference artifacts used for method, field, and type signature validation.

The project keeps most feature logic in `jni/Main.cpp` to make native entry points, runtime state, and retry behavior easy to inspect. Broader refactors should preserve the existing binding lifecycle unless the refactor explicitly changes that design.

Current shared state is split by ownership. `RuntimeMutex::CacheMutex` protects
method and field caches, `RuntimeMutex::FeatureMutex` protects complex feature
collections such as table caches and selected shop targets, and
`RuntimeMutex::UiMutex` protects UI/config strings. Primitive runtime flags,
managed reference pointers, and feature counters are stored as `std::atomic`
values. Code that reads complex collections should use the existing snapshot or
access helpers and should not hold `FeatureMutex` while calling managed IL2CPP
APIs.

## Requirements

Install the following tools before building:

- Git
- Git LFS
- Android SDK
- Android NDK r29
- `ndk-build` available in `PATH`
- An Android `arm64-v8a` target environment

The CI workflow uses:

```sh
ANDROID_NDK_VERSION=29.0.14206865
```

Termux is not an official build target for this repository.

## Quick Start

Clone the repository with submodules:

```sh
git clone --recursive https://github.com/Yan-0001/MCGG.git
cd MCGG
```

If the repository was cloned without submodules, initialize them manually:

```sh
git submodule update --init --recursive
```

Install and pull Git LFS assets:

```sh
git lfs install
git lfs pull
```

Build from the repository root:

```sh
ndk-build -C jni
```

The main native output is generated at:

```text
libs/arm64-v8a/libmain.so
```

## Build

The standard build command is:

```sh
ndk-build -C jni
```

For a clean rebuild:

```sh
ndk-build -C jni clean
ndk-build -C jni
```

If `ndk-build` is not available in your shell, export the Android SDK and NDK paths first:

```sh
export ANDROID_SDK_ROOT=/path/to/android-sdk
export PATH="$ANDROID_SDK_ROOT/ndk/29.0.14206865:$PATH"
```

Then verify that the correct tool is resolved:

```sh
which ndk-build
ndk-build --version
```

## Repository Layout

```text
.github/workflows/            GitHub Actions build workflow
jni/Android.mk                Native module build configuration
jni/Application.mk            ABI, platform, STL, and NDK settings
jni/Main.cpp                  Hook setup, IL2CPP helpers, runtime state, and ImGui overlay
jni/structures/Structures.hpp Unity, Mono, delegate, event, and collection helper types
jni/dobby/                    Dobby header and arm64 static library
jni/Il2CppVersions/           Unity IL2CPP headers and API declarations
jni/imgui/                    Dear ImGui source
jni/xDL/                      xDL Android dynamic loader utilities
libs/                         Generated native shared library output
obj/                          Generated NDK intermediate build output
```

`libs/` and `obj/` are generated build directories and should not be committed.

## Build Configuration

The native module is defined in `jni/Android.mk`:

```make
LOCAL_MODULE := main
```

The active Android target is configured in `jni/Application.mk`:

```make
APP_ABI := arm64-v8a
APP_PLATFORM := android-21
APP_STL := c++_static
APP_OPTIM := release
APP_THIN_ARCHIVE := false
APP_PIE := true
```

The active C++ language mode is configured in `jni/Android.mk`:

```make
-std=c++26
```

Default native C flags optimize for size with `-Oz` and define `NDEBUG`.
Debug-oriented NDK builds add `-O0` when `NDK_DEBUG=1`.

Unity compatibility defines are configured in `jni/Android.mk`:

```make
-DUNITY_VERSION_MAJOR=2019
-DUNITY_VERSION_MINOR=4
-DUNITY_VERSION_PATCH=22
-DUNITY_VER=194
```

Keep these values aligned with the Unity headers under `jni/Il2CppVersions/`.

## Runtime Flow

At load time and during frame presentation, `jni/Main.cpp` performs the following sequence:

1. Confirms the current process is the expected Unity target process.
2. Starts a setup thread.
3. Waits for `liblogic.so`.
4. Resolves IL2CPP API exports.
5. Attaches the native thread to the IL2CPP domain.
6. Hooks `eglSwapBuffers`.
7. Creates the ImGui context and resolves the config path from the game package name.
8. Loads saved project configuration when the config file exists.
9. Loads appearance fonts and applies the selected theme and style settings.
10. Renders the ImGui overlay during frame presentation.
11. Hooks `UnityEngine.Input.GetTouch`.
12. Forwards Unity touch input into ImGui mouse input.
13. Resolves feature methods and fields through `ResolveFeatureBindings()`.
14. Retries missing method and field bindings periodically.
15. Refreshes managed references such as battle bridge and shop panel state and
    publishes them through atomic shared state.
16. Refreshes match state and reloads hero, equipment, and GogoCard table
    caches through local snapshots before locked publication.
17. Runs throttled shop automation and arena effects on separate 100 ms ticks,
    using bounded selected-target snapshots for shop decisions.

This order is intentional. Rendering and input are initialized separately from feature binding so the overlay can report partial runtime readiness while delayed IL2CPP objects continue to resolve.

## Development Notes

- Keep native changes focused and easy to review.
- Validate class names, method names, parameter counts, return types, and field layouts against local reference artifacts before adding IL2CPP calls.
- Keep feature runtime code in `jni/Main.cpp` unless a refactor is explicitly requested.
- Use clear local sections and concise comments around risky IL2CPP calls.
- Use the Runtime Status and Test tabs when validating new bindings or investigating delayed runtime state.
- Keep Settings persistence scoped to project-owned config files rather than enabling ImGui `.ini` persistence.
- Preserve retryable binding behavior. Do not permanently cache unresolved methods or fields as missing.
- Preserve separate 100 ms ticks for shop automation and arena effects unless timing changes are part of the task.
- Preserve shop automation throttles for buy, repeat-buy, refresh, target-worth, and Recommendation Lineup checks.
- Guard direct access to `FeatureState::Heroes`, `FeatureState::Equips`,
  `FeatureState::Cards`, and `FeatureState::ShopSelectedHeroes` with
  `RuntimeMutex::FeatureMutex` or the existing snapshot/access helpers.
- Avoid holding `RuntimeMutex::FeatureMutex` across managed IL2CPP calls.
  Collect local data first, then publish the result under the lock.
- Keep method and field cache changes under `RuntimeMutex::CacheMutex`, and
  guard UI/config strings with `RuntimeMutex::UiMutex`.
- Keep shop selected-target scans bounded and snapshot-based.
- Keep the default ABI as `arm64-v8a`.
- Keep Unity compatibility aligned with `2019.4.22f1`.
- Keep the native language mode aligned with `c++26` unless the build configuration changes intentionally.
- Do not commit generated `obj/` or `libs/` output.
- Avoid adding runtime deployment or abuse-oriented instructions to project documentation.

## Troubleshooting

### Submodule files are missing

Run:

```sh
git submodule update --init --recursive
```

### Git LFS files appear as pointer files

Install and pull LFS assets:

```sh
git lfs install
git lfs pull
```

### `ndk-build` is not found

Export your Android SDK and NDK paths:

```sh
export ANDROID_SDK_ROOT=/path/to/android-sdk
export PATH="$ANDROID_SDK_ROOT/ndk/29.0.14206865:$PATH"
```

Then check:

```sh
which ndk-build
```

### Wrong ABI output

Confirm `jni/Application.mk` contains:

```make
APP_ABI := arm64-v8a
```

Then clean and rebuild:

```sh
ndk-build -C jni clean
ndk-build -C jni
```

### Missing runtime bindings

Missing bindings can be normal during early startup or before the expected managed state exists. The overlay reports these as `Waiting for ...` states and retries them periodically.

When adding or updating a binding, verify:

- Namespace and class name.
- Method name.
- Parameter count.
- Return type.
- Field name and declaring type.
- Static versus instance access.
- Whether the object exists only inside a match or specific UI state.

### Shop automation does not buy or refresh

Shop automation intentionally waits when required bindings, managed references, coin data, target counts, or Recommendation Lineup data are not ready. Check the Runtime Status and Shop tabs for `Waiting for ...` messages.

When investigating continuous-use issues, verify:

- Shop select and shop automation bindings are ready.
- Shop refresh panel is ready when auto-refresh is enabled.
- Recommendation Lineup bindings are ready when recommendation buying or pause-refresh is enabled.
- Keep-gold reserve is not blocking the action.
- Target counts have not already been reached.
- Buy and refresh cooldowns are not still active.

### Noto Sans CJK font is unavailable

The Appearance tab falls back to the default ImGui font when the embedded Noto Sans CJK font cannot be loaded. This does not block the overlay or native build.

### Configuration does not save or load

The default config path is resolved from the running game process and stored as `/data/data/<game-package>/files/mcgg_config.ini`. If the Settings tab reports a save or load failure, check that the process can read and write the game app data directory.

### CI build failed

The workflow runs on pushes to `master` and pull requests targeting `master`.
Stacked or side branches still need local verification before merge.

Check the GitHub Actions log for:

- Android NDK version mismatch.
- Missing submodules.
- Missing Git LFS files.
- Compile errors in `jni/Main.cpp` or third-party native sources.
- Incorrect include paths in `jni/Android.mk`.

## Known Limitations

- Only `arm64-v8a` is supported by default.
- Unity compatibility is pinned to `2019.4.22f1`.
- Runtime bindings may change when the target application updates.
- Feature availability depends on current runtime state and loaded managed objects.
- Recommendation Lineup automation depends on the active match lineup data exposed by the runtime.
- The embedded Noto Sans CJK font increases native source input size and font atlas build time.
- Termux is not maintained as an official build target.
- Documentation intentionally excludes runtime deployment and abuse-oriented instructions.

## Security

Do not report security issues through public issues if the report contains sensitive details, exploit paths, or information that could be misused. Use private communication with the maintainer where possible.

When contributing security-related changes, avoid including secrets, device-specific identifiers, private dumps, proprietary assets, or operational instructions that would enable unauthorized use.

## Contributing

Contributions are welcome when they improve code quality, build reliability, documentation clarity, or project maintainability.

Before opening a pull request:

1. Build the project locally with `ndk-build -C jni`.
2. Keep changes scoped to a clear purpose.
3. Avoid committing generated build output.
4. Avoid committing proprietary assets or private runtime data.
5. Document behavior changes in the README or code comments when relevant.

Good contribution candidates include:

- Build fixes.
- Safer error handling.
- Documentation improvements.
- Refactors that preserve runtime behavior.
- Better status reporting for delayed bindings.
- CI workflow maintenance.

Do not submit changes that add abuse-oriented deployment instructions, service disruption behavior, stealth logic, credential handling, or unauthorized access workflows.

## Third-Party Components

This repository may include or reference third-party components such as:

- Dear ImGui
- Dobby
- xDL
- Unity IL2CPP headers or compatibility declarations
- Android NDK and platform headers

Each third-party component remains subject to its own license terms. The MIT license for this repository applies only to original project code unless a file or directory states otherwise.

Before redistributing binaries or source packages, review the licenses and notices for all bundled third-party components.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for the full text.
