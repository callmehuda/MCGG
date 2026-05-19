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
- Current overlay tabs: Info, Combat, Auto-Play, Shop, Arena, Appearance,
  Settings, and Test

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
- Automatic GGC quality readout for every detected GGC round.
- Overlay status indicators for delayed or unavailable bindings.

### Combat

- Invisible Scout toggle.

### Auto-Play

- Binary-side controller that reads the current round, phase, HP, gold, level,
  population, lineup worth, fight value, Recommendation Lineup target, star-up
  target, and current opponent.
- Adaptive strategy pressure model that shifts between Economy, Balanced, and
  Aggressive based on round progression, HP loss, gold state, own fight value,
  current opponent fight value, and strongest observed opponent.
- Gold-interest planner that evaluates 10-gold interest tiers, next interest
  breakpoints, configured reserve, spend budget, population pressure, HP
  pressure, fight-value deficits, and strategy before allowing shop spending,
  auction bids, level-up actions, passive gold targets, or free-economy assists.
- Opponent-aware scan across battle managers to count opponents, detect target
  contesting, track the current opponent, and compare the local board against
  the strongest board.
- Advanced formation scorer that reads live managed chess units from
  `LogicHeroContainer.m_ChessList`, evaluates hero ID, star, grid position,
  tank/carry role metadata, synergy groups, enemy centroid, enemy column threat,
  ally frontline cover, backline protection, and column crowding, then performs
  bounded one-move-per-cooldown battlefield repositioning.
- Shop target selection that promotes the current best hero or star-up target
  into selected shop targets while preserving existing buy/refresh throttles.
- GogoCard scoring that prefers resource, EXP/economy, hero/shop, star-up,
  synergy, equipment, and combat cards according to round, HP pressure, focus
  synergy, and opponent strength.
- Auction scoring that reads auction phase, slot state, bid price, reward item
  data, hero/equipment rewards, and special upgrade effects before placing a
  bounded bid on the highest-value option.
- Built-in AI startup is opt-in, safe-phase, stateful, and cooldown-gated:
  `StartAI` is not replayed continuously for the same account, it is skipped
  during fight, fight-result, and monster phases, a long-gated refresh can
  restart dropped internal AI state, and `StopAI` is called when Auto-Play is
  disabled or the live battle snapshot is no longer actionable.
- Built-in deployment and smart formation use separate cooldown clocks so board
  movement cannot starve `TryAutoDeploy`.
- Optional controls for built-in battle AI, shop, economy, combat power, arena
  assists, smart formation, auction scoring, and GogoCard scoring.

### Appearance

- Theme selector with ImGui Dark, Catppuccin Mocha, and additional palettes
  inspired by [Dear ImGui issue #707](https://github.com/ocornut/imgui/issues/707),
  including Darcula, Cherry, Dracula, Visual Studio, Deep Dark, and Maroon.
- Default font and embedded Noto Sans CJK font selector.
- Font readiness status when the embedded Noto Sans CJK font is unavailable.

### Settings

- Menu size, optional fixed position, mobile-friendly tab navigation, and window interaction controls.
- Optional next-enemy HUD text rendered near the bottom center of the screen.
- Font scale, opacity, rounding, border, padding, spacing, scrollbar, and indentation controls.
- Save and load for visual, window, HUD, Auto-Play, Combat, Shop, and Arena controls.
- Default config path under the running game package, resolved as `/data/data/<game-package>/files/mcgg_config.ini`.
- Library update indicator and collapsible `Updates / Changelog` view backed by
  GitHub Releases. It shows the embedded local version, commit/ref, latest
  release, release date, last check time, status, short summary, a manual
  refresh button, and scrollable per-version release notes.

### Shop

- Auto-buy free heroes.
- Auto-buy selected hero targets.
- Auto-buy heroes from the active Recommendation Lineup.
- Auto-refresh shop with stop conditions for free heroes, selected targets, or Recommendation Lineup heroes.
- Gold reserve threshold for safer automation.
- Hero target table with configurable target counts and no keyboard-dependent search field.
- Recommendation Lineup target count for advanced shop automation.
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
- Manual and passive gold helpers.
- Free shop/upgrade economy, unlimited hero pool, and shop-lock bypass helpers.
- Skip Round controls that move the local round manager to a selected target
  round, wait out fight/result phases during automatic skips, and suppress
  repeated requests for the same source and target round.
- SpeedHack controls backed by `UnityEngine.Time.set_timeScale`, with an
  explicit reset to `1.0x` when the feature leaves its active battle state.

### Test

- Runtime Status section for battle data, GGC, shop, Recommendation Lineup,
  update checks, Battle Power, arena, round skip, speedhack, test, spectator,
  synergy, and placement bindings.
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
  opponent, board-unit, auction, and strategy data used by the overlay and
  throttled feature ticks.
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

Auto-Play uses the same bounded tick model as the other runtime features. It
gathers local snapshots first, builds one gold-interest plan, scores
strategy/formation/shop/card/auction options from local data, publishes only
compact counters and selected targets under `FeatureMutex`, and avoids holding
project locks while calling managed IL2CPP APIs. Built-in deploy and smart
formation use separate cooldowns inside the 250 ms tick. The built-in AI bridge
is disabled by default, stays stateful when explicitly enabled, and only calls
`StartAI` from safe non-fight/non-result phases with a long refresh interval for
recovery.

Frame-time feature work has a small render budget. If binding retries, managed
reference refresh, table loading, HUD refresh, or automation work has already
spent the budget for the current frame, lower-priority ticks defer to the next
frame. Table cache loading is demand-driven and runs only for table-backed tabs
or active Auto-Play.

The current runtime cadence is intentionally split by responsibility:

- Binding retry: 2000 ms.
- Managed reference refresh: 100 ms.
- GGC Info refresh: 500 ms.
- Match state check: 500 ms.
- Table reload retry: 2000 ms.
- Arena feature tick: 100 ms.
- Shop automation tick: 100 ms.
- Combat power tick: 250 ms.
- Auto-Play tick: 250 ms.
- Feature frame budget: 12 ms.
- Auto-Play AI start retry: 2000 ms.
- Auto-Play AI refresh: 8000 ms.
- Auto-Play built-in deploy cooldown: 750 ms.
- Auto-Play smart formation cooldown: 1000 ms.
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
headers and a project user agent. It does not send gameplay state, account data,
device identifiers, credentials, or private runtime data, and it never downloads
or applies release assets automatically.

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
10. Active Info, Shop, Arena, Auto-Play, Settings HUD, and Test diagnostics
    refresh only the runtime data they need.
11. Auto-Play, Arena, Shop, Combat, and opponent-history work run on their own
    bounded ticks rather than in every render pass; Auto-Play keeps built-in
    deploy, smart formation, AI refresh, level-up, and auction cooldowns
    independent. Busy frames defer lower-priority feature ticks instead of
    running all pending managed work at once.
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

- The render hook is installed before `liblogic.so` and IL2CPP are ready.
  Frame-time code must tolerate a non-ready managed runtime and should not call
  IL2CPP APIs unless `AttachRenderIl2CppThread()` succeeded.
- Startup waits should remain in the detached setup thread, not the constructor,
  so loading the native library does not block Unity startup longer than needed.
- Render-frame work is budgeted. Binding retries, table loads, prediction HUD
  refreshes, and heavier Auto-Play board/opponent scans may defer later
  automation ticks to the next frame, but those ticks remain retryable.
- Persistent managed-object references are published only after they are pinned
  with `il2cpp_gchandle_new(obj, true)`. The handle registry is match-scoped:
  handles stay alive through object refreshes and are released only when the
  active match has ended, so transient reference changes do not leave cached raw
  objects vulnerable to GC movement or collection.
- Auto-Play action groups after planning are also budget-gated. Card scoring,
  auction bids, built-in AI, smart formation, and level-up work should not all
  stack into one render pass when the frame budget is already spent.
- Method lookup caches successful method vectors as reusable results and stores
  empty scans behind a short miss backoff. Field lookup also caches misses only
  behind the binding retry backoff. Do not turn these into permanent failures.
- Method matching uses class name, method name, parameter count, and
  case-insensitive parameter-name containment. Any new overload-sensitive
  binding needs a dump-backed signature check, not just a successful compile.
- Table cache publication is all-or-nothing for hero, equipment, and GogoCard
  data. If one table is unavailable after a game update, dependent UI should
  keep showing `Waiting for ...` rather than assuming an empty game table.
- Shop automation depends on live UI operability, not only battle-data methods.
  Buy and refresh actions should continue to require a non-delayed,
  non-spectate shop panel accepted by `CanOperate(Boolean)`.
- Auto-Play temporarily owns selected Shop, Arena, and Combat assists through a
  captured policy backup. User edits to those assist toggles while Auto-Play is
  active can be restored to the pre-Auto-Play backup when Auto-Play stops.
- Built-in AI coordination is opt-in. Keep direct `StartAI` calls out of fight,
  fight-result, and monster phases, and preserve the default-disabled setting so
  enabling Auto-Play itself does not immediately invoke the game's AI entry
  point.
- Opponent prediction combines exact pair data, invasion manager state,
  dump-backed invader order, learned recent cycles, the bounded seven-round
  cycle-pattern signal, round-robin fallback, recent-cycle distance, and recent
  meeting history. Only the exact local current opponent should be shown as
  `100%`.
- SpeedHack changes global Unity time scale. It must continue to reset to
  `1.0x` when disabled, when the active battle state is gone, or when feature
  state is reset.
- The update checker is informational only. Keep it asynchronous, keep release
  metadata cached behind `RuntimeMutex::UpdateMutex`, keep retries throttled,
  and do not add automatic download, deployment, forced update, bypass, or
  gameplay-data upload behavior.
- Function comments now cover all project-owned native function definitions in
  `jni/Main.cpp` and `jni/structures/Structures.hpp`; new helpers should keep
  that coverage intact rather than relying on section-level comments alone.

## Development Notes

- Keep native changes focused and easy to review.
- Validate class names, method names, parameter counts, return types, and field layouts against local reference artifacts before adding IL2CPP calls.
- Use the shared typed field helpers for regular instance fields so hot paths
  benefit from offset access, and keep raw IL2CPP/static helpers for static
  fields or runtime-managed setter behavior.
- Keep feature runtime code in `jni/Main.cpp` unless a refactor is explicitly requested.
- Use clear local sections and keep the concise function comments current,
  especially around risky IL2CPP calls, hooks, value-type layouts, and timing
  boundaries.
- Preserve the current boot order: process gate, setup thread, early
  `eglSwapBuffers` hook, `liblogic.so` wait, IL2CPP export resolution, setup
  thread attach, `GetTouch` hook, then render-thread overlay initialization.
- Keep constructor work non-blocking: process gate, launch setup thread, return.
- Use the Test tab, including its Runtime Status section, when validating new
  bindings or investigating delayed runtime state.
- For Arena Skip Round changes, verify `MCLogicBattleData.get_logicRoundMgr`,
  `LogicRoundMgr.SetRound(UInt32)`, and `LogicRoundMgr.NextRound(Boolean)`
  against `dump/dump.cs`; keep missing pieces visible as `Waiting for ...`.
- For Arena SpeedHack changes, verify `UnityEngine.Time.set_timeScale(Single)`
  against `dump/dump.cs` and reset the scale to normal when the feature is disabled.
- For GGC Info changes, verify
  `MCLogicBattleData.ILOGIC_GetCrystalQualityByRound(UInt64, Int32)` against
  `dump/dump.cs`, keep the round scan bounded, and keep the readout on its
  throttled refresh cadence.
- Opponent prediction should prefer dump-backed runtime state such as
  `LogicInvasionMgr`, `LogicRealPlayerInvader.lbmList`,
  `PairGenRoundTable`/`PairGenTwoPlayerMode`, `lastRoundEnemy`, and
  `prevRealPlayerEnemy` before falling back to heuristic ordering.
- The seven-round cycle-pattern signal comes from completed per-player history
  learned from `../MCGG_Predictor`; keep it below exact pair and invader-order
  evidence and ignore current-round entries so predictions do not leak live
  observations as completed history.
- Public scouting and positioning guides support recent-cycle and board-read
  heuristics, but they should not override exact runtime current-opponent data.
- Keep Test diagnostics read-only unless a task explicitly requests an action,
  and verify each added binding against `dump/dump.cs`.
- Keep the main overlay accessible on mobile displays while preserving the
  primary TabBar navigation.
- Keep Settings persistence scoped to project-owned config files rather than enabling ImGui `.ini` persistence.
- Preserve retryable binding behavior. Do not permanently cache unresolved methods or fields as missing.
- Preserve separate 100 ms ticks for shop automation and arena effects, the
  250 ms ticks for Combat and Auto-Play, and the 500 ms GGC Info,
  opponent-history, and HUD refresh cadences unless timing changes are part of
  the task.
- Preserve built-in AI as an opt-in Auto-Play assist that is phase-gated and
  stateful; do not make enabling Auto-Play itself call `StartAI` immediately.
- Preserve shop automation throttles for buy, repeat-buy, refresh, target-worth, and Recommendation Lineup checks.
- Keep table cache loading demand-driven and clip long data tables so table UI
  does not walk every row every frame.
- Guard direct access to `FeatureState::Heroes`, `FeatureState::Equips`,
  `FeatureState::Cards`, and `FeatureState::ShopSelectedHeroes` with
  `RuntimeMutex::FeatureMutex` or the existing snapshot/access helpers.
- Avoid holding `RuntimeMutex::FeatureMutex` across managed IL2CPP calls.
  Collect local data first, then publish the result under the lock.
- Keep method and field cache changes under `RuntimeMutex::CacheMutex`, and
  guard UI/config strings with `RuntimeMutex::UiMutex`. Update-check release
  metadata belongs under `RuntimeMutex::UpdateMutex`.
- Keep shop selected-target scans bounded and snapshot-based.
- Keep Appearance theme names and palette entries aligned when adding themes.
  Existing configs expect Catppuccin Mocha to remain theme index `1`.
- Keep the default ABI as `arm64-v8a`.
- Keep Unity compatibility aligned with `2019.4.33f1`.
- Keep the native language mode aligned with `c++26` unless the build configuration changes intentionally.
- Keep the curl, libpsl `0.21.5`, and OpenSSL submodules pinned and rebuild
  `obj/openssl-install/`, `obj/libpsl-install/`, and `obj/curl-install/` with
  `jni/build-curl-android.sh` before running `ndk-build`.
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
- Shop refresh panel is ready when auto-refresh is enabled.
- Shop diagnostics are ready when at least one core shop diagnostic reader is
  available; missing individual shop values should still show `Waiting`.
- The shop panel is operable: not delayed, not in spectate refresh state, and
  accepted by `UIPanelBattleHeroShop.CanOperate(Boolean)`.
- Recommendation Lineup bindings are ready when recommendation buying or pause-refresh is enabled.
- Keep-gold reserve is not blocking the action.
- Target counts have not already been reached.
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
`api.github.com` over HTTPS and that Android's system certificate directory is
available to OpenSSL. Failures are retried with backoff, and the refresh button
can start a manual check. Unknown local version means the library was built
without usable `MCGG_BUILD_VERSION` metadata; rebuild through `ndk-build` or CI
so the `MCGG_BUILD_*` constants are defined.

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
- Recommendation Lineup automation depends on the active match lineup data exposed by the runtime.
- Opponent prediction is probabilistic when current-pair data is unavailable;
  live `m_CurPairDict` data still takes precedence when the runtime exposes it,
  and the seven-round cycle-pattern signal needs enough completed current-cycle
  observations to identify the pattern or key matchup.
- The embedded Noto Sans CJK font increases native source input size and font atlas build time.
- Curl is configured with the pinned OpenSSL `4.0.0` TLS backend, pinned libpsl
  `0.21.5` support, and without curl feature-disabling flags; optional features
  still depend on target libraries available to the configure step.
- Update availability depends on public GitHub Releases network access and
  embedded build metadata. The checker is informational and never installs or
  deploys a newer library.
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
