# Repository Guidelines

## Project Structure & Module Organization

This repository is a native Android modding project for Magic Chess Go Go.
Primary native code lives in `jni/Main.cpp`. Build settings are in
`jni/Android.mk` and `jni/Application.mk`. Shared Unity, IL2CPP, Mono, delegate,
event, collection, and string layouts are kept in
`jni/structures/Structures.hpp`.

`jni/Main.cpp` contains the process gate, setup thread, IL2CPP helpers, feature
binding resolver, runtime caches, Dobby hooks, ImGui rendering, and feature
logic. It also owns the Info, Combat, Appearance, Settings, Shop, Arena, and
Test overlay tabs. Keep feature work in this file unless the user explicitly
requests a multi-file refactor.

`dump/dump.cs` is the IL2CPP signature reference. Use it before changing native
method pointers, hook signatures, value-type layouts, or field offsets.
Vendored or external components live under `jni/dobby/`, `jni/imgui/`,
`jni/xDL/`, and `jni/Il2CppVersions/`. Build output is written to `libs/` and
`obj/`; do not treat these as source modules.

## Build, Test, and Development Commands

```sh
git submodule update --init --recursive
```

Initializes required submodules after cloning.

```sh
git lfs pull
```

Downloads Git LFS-managed files such as large dumps or binary assets.

```sh
ndk-build -C jni
```

Builds the native `main` module and outputs `libs/arm64-v8a/libmain.so`.
The current build uses ABI `arm64-v8a`, platform `android-21`, STL
`c++_static`, optimization `release`, thin archives disabled, PIE enabled, and
C++ mode `c++26`. `jni/Android.mk` optimizes native sources with `-Oz` by
default and adds `-O0` when `NDK_DEBUG=1`.

## Coding Style & Naming Conventions

Follow the existing C++ style in `jni/Main.cpp`: 4-space indentation, concise
helper functions, explicit pointer types, and short comments placed directly
above functions or complex blocks. Keep Unity compatibility macros named with
the `UNITY_` prefix. Prefer `void*` for managed object instances unless a local
structure layout is required.

Keep runtime sections clear and local: IL2CPP resolution, managed reference
refresh, table caches, appearance setup, Settings persistence, feature ticks,
hooks, test diagnostics, and ImGui tabs should remain easy to scan. Add concise
comments for risky IL2CPP calls or value-type assumptions, not for obvious
control flow.

Do not convert retryable lookups into one-shot failures. Method and field
resolution can happen before the target metadata is ready, so missing entries
must be allowed to resolve later. Preserve the separate 100 ms shop and arena
feature ticks unless the task explicitly changes timing.

Shared state is split across `RuntimeMutex::CacheMutex`,
`RuntimeMutex::FeatureMutex`, and `RuntimeMutex::UiMutex`. Primitive feature
flags, counters, and managed reference pointers use `std::atomic`. Guard direct
access to complex feature collections such as `FeatureState::Heroes`,
`FeatureState::Equips`, `FeatureState::Cards`, and
`FeatureState::ShopSelectedHeroes` with `FeatureMutex` or the existing
snapshot/access helpers. Do not hold `FeatureMutex` while calling managed IL2CPP
APIs; gather local data first and publish results under the lock.

Shop automation is intentionally single-threaded and throttled on the frame
tick. Preserve the existing buy, repeat-buy, refresh, target-worth, and
Recommendation Lineup cooldowns. Read selected target state through
`GetShopHeroTargetsSnapshot()` or `GetSelectedShopHeroTargetsSnapshot()` and
keep scans bounded by the existing runtime limits. Do not add unbounded scans or
immediate retry loops to the shop hot path unless the task explicitly changes
that design.

## Testing Guidelines

There is no dedicated unit test framework in this repository. For native changes,
the required verification is a successful:

```sh
ndk-build -C jni
```

When changing IL2CPP calls, verify signatures against `dump/dump.cs` and confirm
the target remains `arm64-v8a`, Unity `2019.4.22f1`, and native C++ mode
`c++26`.

When changing Recommendation Lineup automation, verify the related
`MCLogicBattleData` and `MCBattleBridge` signatures against `dump/dump.cs` and
keep the overlay in a `Waiting for ...` state while runtime data is unavailable.

For documentation-only changes, at minimum inspect the rendered Markdown diff.
For native or mixed changes, also run:

```sh
git diff --check
```

## Commit & Pull Request Guidelines

Recent commits use short, imperative summaries such as `Add enemy predictor and
project documentation`. Keep commit messages direct and focused on the change.

Pull requests should include a clear description, the build command result,
affected files, and any relevant notes about IL2CPP signatures or runtime hooks.
Link issues when applicable. Screenshots are useful only for visible ImGui
overlay changes.

## Agent-Specific Instructions

Keep changes scoped. Do not modify vendored directories such as
`jni/Il2CppVersions/`, `jni/imgui/`, or `jni/xDL/` unless explicitly requested.
Do not revert unrelated local changes in the working tree.

Current user-facing feature areas are Info, Combat, Appearance, Settings, Shop,
Arena, and Test. If a feature binding is missing at runtime, the overlay should
show a `Waiting for ...` state rather than failing silently. Shop currently
includes free-hero buying, selected target buying, Recommendation Lineup buying,
auto-refresh pause conditions, keep-gold reserve, and target counts. Use the
Runtime Status and Test tabs when checking binding readiness, managed
references, round state, player economy/rank/shop state, battle manager fields,
battle bridge state, shop panel state, behavior API state, Recommendation
Lineup state, or opponent prediction logic. Test diagnostics should stay
read-only unless the task explicitly requests an action.
Keep mobile accessibility changes compatible with the main ImGui TabBar. Helper
buttons may select tabs, but the TabBar should remain visible and authoritative.
Settings config should default to the running game package directory as
`/data/data/<game-package>/files/mcgg_config.ini`.
