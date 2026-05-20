# MCGG

[English](README.md) · [Bahasa Indonesia](README.id.md)

[![CI Build](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml/badge.svg)](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml)
[![MIT License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
![Android](https://img.shields.io/badge/Android-native-brightgreen)
![ABI](https://img.shields.io/badge/ABI-arm64--v8a-blue)
![Unity](https://img.shields.io/badge/Unity-2019.4.33f1-black)
![NDK](https://img.shields.io/badge/NDK-r29-orange)

Open-source native Android research project for Magic Chess Go Go, focused on Unity/IL2CPP runtime analysis, native Android build workflows, and ImGui-based runtime diagnostics.

This repository builds an `arm64-v8a` shared library for a Unity `2019.4.33f1` IL2CPP Android environment. It is intended for learning, defensive research, reverse engineering practice, and authorized experimentation only.

## Table of Contents

- [Responsible Use](#responsible-use)
- [Project Status](#project-status)
- [Dump Reference](#dump-reference)
- [Game Context](#game-context)
- [Features](#features)
- [Architecture](#architecture)
- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Build](#build)
- [Repository Layout](#repository-layout)
- [Build Configuration](#build-configuration)
- [CI Release Packaging](#ci-release-packaging)
- [Runtime Flow](#runtime-flow)
- [Runtime Audit Notes](#runtime-audit-notes)
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
- Unity version: `2019.4.33f1`
- Android NDK: `r29`
- Build system: `ndk-build`
- C++ standard: `c++26`
- Primary branch: `master`
- Current overlay tabs: Info, Combat, Shop, Arena, Appearance, Settings, and Test

## Dump Reference

`dump/dump.cs` is the current IL2CPP signature reference and was refreshed from
the latest local dump on 2026-05-20. The working artifact is 605,385 lines.
Because `dump/**` is stored through Git LFS, an ordinary tracked diff may show
only the pointer object ID and file size; review the full local artifact, and
compare it with the previous dump snapshot when one is available.

The refreshed dump still validates the core runtime contracts used by this
native overlay:

- `MCLogicBattleData`: `ILOGIC_GetAllBattleMgr()`,
  `ILOGIC_GetCurrentOpponentAccountID(UInt64)`,
  `ILOGIC_GetCrystalQualityByRound(UInt64, Int32)`,
  `ILOGIC_GetStPlayerData(UInt64)`, round/phase readers, economy readers,
  shop readers, and Recommendation Lineup readers.
- `LogicRoundMgr`: `SetRound(UInt32)` and `NextRound(Boolean)`.
- `SystemData.RoomData.bRobot`, `UnityEngine.Time.set_timeScale(Single)`,
  and the achievement record helpers used by Info and Arena diagnostics.

Addresses and RVAs in `dump/dump.cs` are per-build diagnostics. Native logic
should continue to bind by class, method name, return type, parameter count,
and parameter-name shape instead of copying address literals. Keep older dump
snapshots out of commits unless a task explicitly asks for a checked-in
historical reference.

## Game Context

External research checked on 2026-05-19 keeps the project context aligned with
the live game without treating current meta advice as stable native truth.

Primary public references:

- [Google Play: Magic Chess: Go Go](https://play.google.com/store/apps/details?id=com.mobilechess.gp)
  identifies the game as an auto-chess, multiplayer strategy title by Vizta
  Games, showed 10M+ downloads, a May 9, 2026 store update, and S6 Dawnlight
  Celebration events in the checked web region, and points to the official
  website and YouTube channel. Store counts, ratings, events, and update dates
  can vary by region or cache, so use the listing for product identity and
  links rather than native binding assumptions.
- [Official website](https://magicchessgogo.com/) describes the core loop as
  recruiting and upgrading MLBB-inspired heroes, forming lineups for 8-player
  battles, using Commander skills, selecting Go Go Cards at key stages, and
  building role/synergy combinations.
- [MOONTON global launch news](https://en.moonton.com/news/195.html) describes
  MCGG as an 8-player PvP auto-battler and documents durable systems such as
  synergies, combat buffs, and seasonal mechanics.
- [MOONTON Season 5 news](https://en.moonton.com/news/305.html) documents Go Go
  Plaza, GOGO MOBA, Golden Month content, new synergies, GO1 esports momentum,
  and a 30M-download milestone after global launch. Google Play's May 19, 2026
  checked "What's new" copy also names Commander Ruby, Gold Rush mode, City
  Hero draw, and the Neolight Wheel event.
- [Official YouTube channel](https://www.youtube.com/@MagicChessGoGo) and
  gameplay/guide material are useful for observing UI flow, shop behavior,
  Commander choices, board placement, economy pacing, Go Go Card picks, and
  meta terminology, but they should be treated as volatile references.

The important gameplay model for this repository is:

- Matches are 8-player auto-battler games where players recruit, merge, sell,
  deploy, and reposition heroes from a shared shop-like economy.
- Strategic pressure comes from gold interest, health preservation, level and
  population timing, shop refreshes, contested hero pools, Commander skills,
  equipment, synergies, Go Go Cards, auctions, and round-specific supplies.
- Current public notes around S6 Dawnlight Celebration, Ruby, Gold Rush, Go Go
  Plaza, GOGO MOBA, GO1 event content, and seasonal Commander/Card changes
  reinforce that names, lineups, and meta priorities move faster than the native
  binding layer.

Engineering implication: docs and diagnostics should describe durable runtime
surfaces such as battle managers, player economy, shop panel state, round
manager state, Commander/Go Go Card data, auction state, synergies, and board
units. Avoid hard-coding current public meta claims into native behavior unless
they are backed by `dump/dump.cs` and live runtime verification.

## Features

### Info

- Player and next-enemy table sorted with the local player first.
- Player names append ` (Bot)` when `SystemData.RoomData.bRobot` is true.
- Automatic GGC quality readout for every detected GGC round.
- Overlay status indicators for delayed or unavailable bindings.

### Combat

- Invisible Scout toggle.

### Appearance

- Theme selector with ImGui Dark, Catppuccin Mocha, and additional palettes
  inspired by [Dear ImGui issue #707](https://github.com/ocornut/imgui/issues/707),
  including Darcula, Cherry, Dracula, Visual Studio, Deep Dark, and Maroon.
- Default font and embedded Noto Sans CJK font selector.
- Menu language selector backed by the native i18n table, currently covering
  English and Indonesian labels for user-facing menu controls.
- Localized tooltips on interactive menu tabs, buttons, toggles, inputs, sliders,
  combos, and table-row controls so the overlay explains actions in the selected
  language without adding persistent instructional text.
- Font readiness status when the embedded Noto Sans CJK font is unavailable.

### Settings

- Menu size, optional fixed position, mobile-friendly tab navigation, and window interaction controls.
- Optional next-enemy HUD text rendered near the bottom center of the screen.
- Font scale, opacity, rounding, border, padding, spacing, scrollbar, and indentation controls.
- Save and load for visual, language, window, HUD, Combat, Shop, and Arena controls.
- Default config path under the running game package, resolved as `/data/data/<game-package>/files/mcgg_config.ini`.
- Library update indicator and collapsible `Updates / Changelog` view backed by
  GitHub Releases. It shows the embedded local version, commit/ref, latest
  release, release date, last check time, status, short summary, a manual
  refresh button, and scrollable per-version release notes.

### Shop

- Auto-buy free heroes.
- Auto-buy selected hero targets.
- Auto-buy every detected hero from the active Recommendation Lineup, with a
  separate target count for each recommended hero.
- Force Scavenger to leave the most expensive shop heroes by clearing cheaper
  heroes immediately after automatic regular shop refreshes when Scavenger is
  active at count 2 or higher.
- Auto-refresh shop while selected or Recommendation Lineup targets still need
  copies, with stop conditions for free heroes, selected targets, or
  Recommendation Lineup heroes that still need copies.
- Gold reserve threshold for safer automation.
- Hero target table with configurable target counts and no keyboard-dependent search field.
- Recommendation Lineup target table for advanced shop automation.
- Buy and refresh throttles that reduce repeated actions during continuous automation.
- Shop UI readiness checks that wait for an operable, non-delayed shop panel
  before selecting, buying, or refreshing.

### Arena

- Spawn heroes by table entry and star level.
- Grant equipment, including enhanced equipment.
- Force selected GogoCards.
- Force active synergies.
- Battle Power subtab for force defend win, HP-loss prevention, self
  attack-ratio/fight-value boosting, and enemy-board crippling.
- Level and population 99 helper.
- Outside-map placement helper.
- Enemy HP 1 helper.
- Force Complete Achievements Task helper that patches achievement reach/result
  checks and round achievement counters while enabled.
- Manual gold grant helper.
- Skip Round controls that move the local round manager to a selected target
  round, wait out fight/result phases during automatic skips, and suppress
  repeated requests for the same source and target round.
- SpeedHack controls backed by `UnityEngine.Time.set_timeScale`, with an
  explicit reset to `1.0x` when the feature leaves its active battle state.

### Test

- Runtime Status section for battle data, GGC, shop, Recommendation Lineup,
  update checks, Battle Power, arena, achievements, round skip, speedhack, test,
  spectator, synergy, and placement bindings.
- Manual binding retry and managed reference refresh controls.
- Account inspection by self, opponent, or explicit account ID.
- Fight prediction table with direct, manager-derived, invasion-pair,
  dump-derived invader order, queue/cycle, seven-round cycle-pattern, and
  opponent-history signals.
  `Will fight` is the chance that the row is the local player's opponent;
  `Current enemy` shows that row's observed opponent when available; `Recent`
  shows recent local meetings from the per-player opponent history.
- Tabbed runtime readouts for binding readiness, round state, player identity,
  rank, economy, shop state, battle manager fields, battle bridge state, shop
  panel state, behavior API state, and all manager entries.
- Shop diagnostic readiness is grouped across core shop diagnostic readers; each
  individual shop row still reports `Waiting` when its specific reader is not
  available.
- Test diagnostics and automation hot paths share a per-frame managed-work
  budget, so live IL2CPP/game readers can report `Waiting` for a frame instead
  of issuing an oversized burst of calls.
- Long Shop and Arena data tables render only visible rows to keep scrolling and
  tab switches responsive after table metadata is loaded.

Feature bindings are resolved against local reference artifacts and runtime
IL2CPP metadata. Missing methods and fields are retried periodically instead of
being permanently cached as unavailable. Empty method scans and missing field
lookups both use retry backoffs so the render thread does not rescan broad
metadata every frame. When a binding is not ready, the overlay reports a
`Waiting for ...` state.

Opponent prediction combines runtime sources before public heuristics. Live
current-opponent observations and reverse pair reads remain strongest, followed
by dump-backed invader ordering, learned recent-opponent cycles, a seven-round
cycle-pattern model adapted from `../MCGG_Predictor`, round-robin fallback,
bounded cycle-gap learning, and bounded history weights. The cycle-pattern
signal uses completed current-cycle history only: it treats R4 matching local R1
as the classic pattern, otherwise uses the shifted pattern where R5/R6/R7 derive
from the local R1 opponent's R4/R2/R3 matchups. Prediction rows are cached on
the 500 ms feature cadence so the Test tab and next-enemy HUD do not rebuild
managed state every render frame.

## Architecture

MCGG is organized around a small native runtime layer that coordinates Unity, IL2CPP, rendering, input forwarding, and feature binding.

At a high level, the project contains:

- A native Android module built with `ndk-build`.
- Unity `2019.4.33f1` IL2CPP API declarations.
- Runtime dynamic library lookup helpers.
- Dobby-based function hook integration.
- Dear ImGui rendering through OpenGL ES.
- Unity touch input forwarding into ImGui mouse input.
- Runtime appearance setup with disabled ImGui `.ini` persistence.
- Project-owned configuration persistence for overlay and feature state.
- A detached GitHub Releases update check that uses static libcurl only for
  public release metadata and keeps changelog data cached in memory for the
  session.
- Atomic primitive runtime state with dedicated mutex domains for IL2CPP
  caches, pinned managed-object handles, feature collections, and UI/config
  strings.
- Pinned `il2cpp_gchandle_new(obj, true)` ownership for persistent managed
  object references such as `MCBattleBridge`, the hero shop panel, the shop item
  list, and `LoadRes`, with all match handles released together only after the
  active match ends.
- Offset-backed typed helpers for regular instance field reads and non-pointer
  writes, with raw IL2CPP fallbacks and static fields kept on static IL2CPP
  accessors.
- Snapshot helpers for hero, equipment, GogoCard, selected shop target,
  opponent, and shop target data used by the overlay and throttled feature ticks.
- Local reference artifacts used for method, field, and type signature validation.
- Function-level comments are kept on project-owned runtime functions in
  `jni/Main.cpp` and shared layout helpers in `jni/structures/Structures.hpp`
  so future binding and layout reviews can start from explicit intent.

The project keeps most feature logic in `jni/Main.cpp` to make native entry points, runtime state, and retry behavior easy to inspect. Broader refactors should preserve the existing binding lifecycle unless the refactor explicitly changes that design.

Current shared state is split by ownership. `RuntimeMutex::CacheMutex` protects
method and field caches, `RuntimeMutex::ManagedHandleMutex` protects the pinned
managed-object handle registry, `RuntimeMutex::FeatureMutex` protects complex
feature collections such as table caches and selected shop targets, and
`RuntimeMutex::UiMutex` protects UI/config strings. Primitive runtime flags,
managed reference pointers, their published GC handle IDs, and feature counters
are stored as `std::atomic` values. Code that reads complex collections should
use the existing snapshot or access helpers and should not hold `FeatureMutex`
while calling managed IL2CPP APIs.

Frame-time feature work has a small render budget. If binding retries, managed
reference refresh, table loading, HUD refresh, or automation work has already
spent the budget for the current frame, lower-priority ticks defer to the next
frame. A separate managed-work unit budget caps how many IL2CPP, Unity, or game
readers a single render frame can issue; when that cap is reached, diagnostics
show `Waiting` and lower-priority automation waits for the next scheduled tick.
Table cache loading is demand-driven and runs only for table-backed tabs or active shop automation.

The current runtime cadence is intentionally split by responsibility:

- Binding retry: 2000 ms.
- Managed reference refresh: 100 ms.
- GGC Info refresh: 500 ms.
- Match state check: 500 ms.
- Table reload retry: 2000 ms.
- Arena feature tick: 100 ms.
- Shop automation tick: 100 ms.
- Combat power tick: 250 ms.
- Feature frame budget: 12 ms.
- Feature managed-work budget: 256 units per render frame; all-or-nothing table
  loading may use up to 2048 units before deferring.
- Opponent prediction history tick: 500 ms.
- Next-enemy HUD text refresh: 500 ms while the HUD is enabled.
- Cached opponent prediction row refresh: 500 ms while the Test tab or
  next-enemy HUD needs prediction data.
- GitHub release update check: once per overlay session, then no more than
  every 6 hours unless the user presses refresh. Network or metadata failures
  retry with bounded exponential backoff from 5 minutes up to 60 minutes.

Field metadata misses are also retried with a short backoff. This keeps late
Unity metadata retryable without letting missing field lookups rescan every
feature tick.

Typed regular instance field access resolves `il2cpp_field_get_offset` and
copies directly from the managed object when the offset is valid. Static fields,
unresolved offsets, and managed-object pointer writes keep using the IL2CPP
accessor path so fallback and write-barrier behavior stay intact.

## Requirements

Install the following tools before building:

- Git
- Git LFS
- Android SDK
- Android NDK r29
- Autotools for the curl and libpsl submodule build: `autoconf`, `automake`,
  `autopoint`, `gettext`, `libtool`, `pkg-config`, and `perl`
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
bash jni/build-curl-android.sh
ndk-build -C jni
```

The main native output is generated at:

```text
libs/arm64-v8a/libmain.so
```

## Build

The standard build command is:

```sh
bash jni/build-curl-android.sh
ndk-build -C jni
```

`jni/build-curl-android.sh` builds the pinned OpenSSL `4.0.0` submodule first,
then builds the pinned libpsl `0.21.5` release from
`https://github.com/rockdaboot/libpsl/releases/tag/0.21.5` and the pinned curl
submodule as static `arm64-v8a` libraries at
`obj/libpsl-install/lib/libpsl.a` and `obj/curl-install/lib/libcurl.a`. It also
installs curl headers under `obj/curl-install/include/`. `jni/Android.mk` links
those prebuilt archives into the `main` module, so rerun the script after
cleaning `obj/` or after changing the curl, libpsl, or OpenSSL submodules.

For a clean rebuild:

```sh
ndk-build -C jni clean
bash jni/build-curl-android.sh
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
jni/build-curl-android.sh     Static OpenSSL, libpsl, and curl build script for Android
jni/Main.cpp                  Hook setup, IL2CPP helpers, runtime state, and ImGui overlay
jni/structures/Structures.hpp Unity, Mono, delegate, event, and collection helper types
jni/curl/                     Pinned curl submodule used for static libcurl
jni/dobby/                    Dobby header and arm64 static library
jni/Il2CppVersions/           Unity IL2CPP headers and API declarations
jni/imgui/                    Dear ImGui source
jni/libpsl/                   Pinned libpsl 0.21.5 submodule for curl public suffix support
jni/openssl/                  Pinned OpenSSL 4.0.0 submodule for curl TLS
jni/xDL/                      xDL Android dynamic loader utilities
libs/                         Generated native shared library output
obj/                          Generated NDK intermediate build output
```

`libs/` and `obj/` are generated build directories and should not be committed.

## Build Configuration

The native module is defined in `jni/Android.mk`:

```make
LOCAL_MODULE := ssl
LOCAL_MODULE := crypto
LOCAL_MODULE := psl
LOCAL_MODULE := curl
...
LOCAL_MODULE := main
```

The `ssl`, `crypto`, `psl`, and `curl` modules are prebuilt static archives
generated by `jni/build-curl-android.sh` under `obj/openssl-install/`,
`obj/libpsl-install/`, and `obj/curl-install/`. Curl is configured with the
OpenSSL TLS backend and pinned libpsl `0.21.5` public suffix support, and the
script does not pass curl feature-disabling flags.

The active Android target is configured in `jni/Application.mk`:

```make
APP_ABI := arm64-v8a
APP_PLATFORM := android-21
APP_STL := c++_static
APP_OPTIM := release
APP_THIN_ARCHIVE := false
APP_PIE := true
APP_CFLAGS += -fstack-protector-strong -D_FORTIFY_SOURCE=2
APP_CPPFLAGS += -fvisibility-inlines-hidden
APP_LDFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,--as-needed
```

The active C++ language mode is configured in `jni/Android.mk`:

```make
-std=c++26
```

Default native C flags optimize for size with `-Oz` and define `NDEBUG`.
Debug-oriented NDK builds add `-O0` when `NDK_DEBUG=1`.
The app-wide flags also keep conservative runtime behavior with
`-fno-strict-aliasing`, `-fno-strict-overflow`,
`-fno-delete-null-pointer-checks`, and `-funwind-tables` for stability and
post-crash diagnostics.

Unity compatibility defines are configured in `jni/Android.mk`:

```make
-DUNITY_VERSION_MAJOR=2019
-DUNITY_VERSION_MINOR=4
-DUNITY_VERSION_PATCH=33
-DUNITY_VER=194
```

Keep these values aligned with the Unity headers under `jni/Il2CppVersions/`.

Build metadata is embedded into the native library through `jni/Android.mk`.
Local builds fall back to Git-derived values when available, while CI passes the
generated release metadata explicitly:

```make
-DMCGG_BUILD_REPOSITORY
-DMCGG_BUILD_VERSION
-DMCGG_BUILD_COMMIT
-DMCGG_BUILD_REF
```

The overlay uses those constants as a `BUILD_INFO.txt`-equivalent source for
the Settings update indicator and Test Runtime Status diagnostics.

## CI Release Packaging

The GitHub Actions workflow at `.github/workflows/build.yml` prepares the
UTC date-based release metadata before compiling, passes that metadata into
`ndk-build` as `MCGG_BUILD_*` constants, installs the curl/libpsl/OpenSSL build
prerequisites, builds the static OpenSSL, pinned libpsl `0.21.5`, and curl
archives, builds the native module with Android NDK `29.0.14206865`, uploads
the generated release zip as a workflow artifact, and publishes a GitHub release
for non-pull-request runs.

Release assets are named with the project prefix, UTC date-based version,
workflow run metadata, and short commit SHA. Each package includes
`BUILD_INFO.txt` with the repository, ref, commit, version, run, NDK, and module
metadata used for that build.

Release notes are generated from Git history. They include the repository/ref
context plus commit descriptions for the push range when GitHub provides one,
otherwise from the previous `v*` tag through the current commit, falling back to
the current commit only. Commit subjects and any commit body text are included.
Existing releases with the same generated tag are updated with the regenerated
notes before the asset is uploaded with `--clobber`.

At runtime, the overlay queries
`https://api.github.com/repos/Yan-0001/MCGG/releases?per_page=20` through
libcurl, filters out draft and prerelease entries, treats the first compatible
release as the latest version, and compares it with the embedded local version
or matching release target commit. The request uses only standard GitHub API
headers and a project user agent. The current native request disables libcurl
peer and host certificate verification and does not configure Android's system
CA path; keep that compatibility choice scoped to this informational metadata
check. It does not send gameplay state, account data, device identifiers,
credentials, or private runtime data, and it never downloads or applies release
assets automatically.

## Runtime Flow

At load time and during frame presentation, `jni/Main.cpp` performs the following sequence:

1. The constructor confirms the process command line contains `:UnityKillsMe`.
2. A detached setup thread starts after the process gate without sleeping in the
   constructor.
3. The setup thread owns startup waits, then resolves and hooks
   `eglSwapBuffers` first, so rendering can become the long-lived frame loop.
4. The setup thread waits for `liblogic.so`, resolves Unity `2019.4.33f1`
   IL2CPP API exports from the bundled API declarations, and attaches to the
   IL2CPP domain.
5. The setup thread resolves and hooks `UnityEngine.Input.GetTouch` when the
   method metadata is available.
6. `RuntimeState::Il2CppReady` is set, the setup thread performs the first
   guarded feature-binding pass, and later render-frame retries remain
   backoff-gated.
7. The first valid hooked frame creates the ImGui context, disables ImGui
   `.ini` persistence, resolves the config path from the game package name,
   loads saved project configuration when present, loads fonts, and applies the
   selected theme and style settings.
8. Each hooked frame attaches the render thread to IL2CPP when possible before
   managed feature work runs.
9. `TickFeatures()` retries missing bindings, refreshes battle bridge and shop
   panel references through pinned GC handles while a match is active, refreshes
   match state, and retries table cache loading.
10. Active Info, Shop, Arena, Settings HUD, and Test diagnostics refresh only the runtime data they need.
11. Arena, Shop, Combat, and opponent-history work run on their own bounded ticks rather than in every render pass. Busy frames defer lower-priority feature ticks instead of running all pending managed work at once.
12. Unity touch input is forwarded into ImGui mouse input through the hooked
    `GetTouch` path.
13. When the match state transitions to ended, all pinned managed-object handles
    accumulated for the match are freed together and cached managed reference
    pointers are cleared.

This order is intentional. The render hook can exist before IL2CPP is ready, so
managed feature logic must stay behind readiness checks. Rendering, input, and
feature binding are initialized separately so the overlay can report partial
runtime readiness while delayed IL2CPP objects continue to resolve.

## Runtime Audit Notes

Recent code review of `jni/Main.cpp`, `jni/structures/Structures.hpp`,
`jni/Android.mk`, `.github/workflows/build.yml`, and `dump/dump.cs` highlights
the following bug-prone areas:

- The render hook is installed before `liblogic.so` and IL2CPP are ready, so
  frame-time code must tolerate a non-ready managed runtime.
- Binding retries, table loads, prediction HUD refreshes, and diagnostics are
  budgeted. Delayed work should retry later instead of piling managed calls into
  one render pass.
- Persistent managed-object references must be pinned with
  `il2cpp_gchandle_new(obj, true)` and released only at match end.
- Table cache publication stays all-or-nothing for heroes, equipment, and
  GogoCards. Dependent UI should keep showing `Waiting for ...` while required
  tables are unavailable.
- Shop automation depends on live UI operability, including delay, spectate,
  and `CanOperate(Boolean)` checks when those bindings are available.
- Info bot labels come from `SystemData.RoomData.bRobot` through
  `ILOGIC_GetStPlayerData(UInt64)` and should degrade to ordinary player names
  while that binding or field metadata is unavailable.
- Opponent prediction should combine exact pair data first, then invader order,
  recent-cycle learning, seven-round cycle-pattern history, round-robin
  fallback, and per-player history. Only the exact local current opponent should
  be shown as `100%`.
- SpeedHack must reset Unity time scale to `1.0x` on disable, inactive battle
  state, and feature reset.

## Development Notes

- Keep feature work scoped to `jni/Main.cpp` unless a task explicitly asks for
  a multi-file refactor.
- Verify new IL2CPP methods, hook signatures, value types, and fields against
  `dump/dump.cs`; dump RVAs are diagnostics, not binding contracts.
- Preserve retryable method and field lookup behavior. Missing metadata should
  back off briefly and resolve later when runtime state is ready.
- Keep long table UIs clipped and demand-load table caches only for table-backed
  tabs or active automation that consumes table metadata.
- Run `git diff --check` for native or mixed changes. This repository has no
  dedicated unit test framework.

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

### `libcurl.a` or OpenSSL static libraries are missing

`ndk-build` expects generated static libraries at
`obj/curl-install/lib/libcurl.a`, `obj/libpsl-install/lib/libpsl.a`,
`obj/openssl-install/lib/libssl.a`, and `obj/openssl-install/lib/libcrypto.a`.
If those files are missing, initialize submodules and rebuild:

```sh
git submodule update --init --recursive
bash jni/build-curl-android.sh
```

The curl/libpsl build requires `autoconf`, `automake`, `autopoint`, `gettext`,
`libtool`, `pkg-config`, and `perl` because the pinned Git checkouts build from
source.

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

Missing bindings can be normal during early startup or before the expected managed state exists. The overlay reports these as `Waiting for ...` states and retries them periodically. Missing field lookups are throttled so unavailable metadata does not repeatedly scan from hot feature paths.

When adding or updating a binding, verify:

- Namespace and class name.
- Method name.
- Parameter count.
- Return type.
- Field name and declaring type.
- Static versus instance access.
- Whether the object exists only inside a match or specific UI state.

### Shop automation does not buy or refresh

Shop automation intentionally waits when required bindings, managed references,
coin data, target counts, or Recommendation Lineup data are not ready. Check the
Test tab's Runtime Status section and the Shop tab for `Waiting for ...`
messages.

When investigating continuous-use issues, verify:

- Shop select and shop automation bindings are ready.
- Shop Scavenger bindings are ready when Scavenger expensive-hero forcing is enabled.
- Shop refresh panel is ready when auto-refresh is enabled.
- Shop diagnostics are ready when at least one core shop diagnostic reader is
  available; missing individual shop values should still show `Waiting`.
- The shop panel is operable: not delayed, not in spectate refresh state, and
  accepted by `UIPanelBattleHeroShop.CanOperate(Boolean)`.
- Recommendation Lineup bindings are ready when recommendation buying or pause-refresh is enabled.
- Keep-gold reserve is not blocking the action.
- Scavenger active count is 2 or higher when expensive-hero forcing is enabled.
- Selected-target and per-hero Recommendation Lineup counts have not already
  been reached, and the target hero still has pool availability when that reader
  is ready.
- Buy and refresh cooldowns are not still active.

### Noto Sans CJK font is unavailable

The Appearance tab falls back to the default ImGui font when the embedded Noto Sans CJK font cannot be loaded. This does not block the overlay or native build.

### Configuration does not save or load

The default config path is resolved from the running game process and stored as `/data/data/<game-package>/files/mcgg_config.ini`. If the Settings tab reports a save or load failure, check that the process can read and write the game app data directory.

### Update check stays pending or failed

The Settings tab's `Updates / Changelog` section starts a detached GitHub
Releases request and keeps cached release metadata in memory for the session.
The Test tab Runtime Status row reports `Waiting for network check`, `Up to
date`, `Update available`, `GitHub request failed`, `Malformed release
metadata`, or `Unknown local version`.

If the request fails, verify that the target environment can reach
`api.github.com` over HTTPS. The current native request disables libcurl peer
and host certificate verification and does not set an Android CA path, so
certificate-store availability is not the expected failure cause for this
checker. Failures are retried with backoff, and the refresh button can start a
manual check. Unknown local version means the library was built without usable
`MCGG_BUILD_VERSION` metadata; rebuild through `ndk-build` or CI so the
`MCGG_BUILD_*` constants are defined.

### CI build failed

The workflow runs on pushes to `master` and pull requests targeting `master`.
Stacked or side branches still need local verification before merge.

Check the GitHub Actions log for:

- Android NDK version mismatch.
- Missing submodules.
- Missing Git LFS files.
- Missing curl/libpsl/OpenSSL build tools or generated static libraries under
  `obj/`.
- Compile errors in `jni/Main.cpp` or third-party native sources.
- Incorrect include paths in `jni/Android.mk`.

## Known Limitations

- Only `arm64-v8a` is supported by default.
- Unity compatibility is pinned to `2019.4.33f1`.
- Runtime bindings may change when the target application updates.
- Feature availability depends on current runtime state and loaded managed objects.
- The render hook can be active before managed metadata is ready, so early
  frames may show partial overlay readiness.
- IL2CPP method resolution is dump-guided but still name and parameter-shape
  based at runtime; overloaded or renamed game methods require manual
  validation against `dump/dump.cs`.
- Recommendation Lineup automation depends on the active match lineup data
  exposed by the runtime; listing every recommended hero requires the
  recommendation-membership binding and a loaded hero table filtered for
  commanders and known placeholder names.
- Opponent prediction is probabilistic when current-pair data is unavailable;
  live `m_CurPairDict` data still takes precedence when the runtime exposes it,
  and the seven-round cycle-pattern signal needs enough completed current-cycle
  observations to identify the pattern or key matchup.
- The embedded Noto Sans CJK font increases native source input size and font atlas build time.
- Curl is configured with the pinned OpenSSL `4.0.0` TLS backend, pinned libpsl
  `0.21.5` support, and without curl feature-disabling flags; optional features
  still depend on target libraries available to the configure step.
- The GitHub Releases checker currently disables libcurl peer and host
  certificate verification and does not configure an Android CA path. Keep that
  request limited to public release metadata and do not reuse it for sensitive
  data.
- Update availability depends on public GitHub Releases network access and
  embedded build metadata. The checker is informational and never installs or
  deploys a newer library.
- Termux is not maintained as an official build target.
- Documentation intentionally excludes runtime deployment and abuse-oriented instructions.

## Security

Do not report security issues through public issues if the report contains sensitive details, exploit paths, or information that could be misused. Use private communication with the maintainer where possible.

When contributing security-related changes, avoid including secrets, device-specific identifiers, private dumps, proprietary assets, or operational instructions that would enable unauthorized use.

Do not reuse the current update-check libcurl options for sensitive network
traffic while certificate verification is disabled.

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
- curl / libcurl
- libpsl
- OpenSSL
- xDL
- Unity IL2CPP headers or compatibility declarations
- Android NDK and platform headers

Each third-party component remains subject to its own license terms. The MIT license for this repository applies only to original project code unless a file or directory states otherwise.

Before redistributing binaries or source packages, review the licenses and notices for all bundled third-party components.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for the full text.
