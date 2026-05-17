# Repository Guidelines

## Project Structure & Module Organization

This repository is a native Android modding project for Magic Chess Go Go.
Primary native code lives in `jni/Main.cpp`. Build settings are in
`jni/Android.mk` and `jni/Application.mk`. Shared Unity, IL2CPP, Mono, delegate,
event, collection, and string layouts are kept in
`jni/structures/Structures.hpp`.

`jni/Main.cpp` contains the process gate, setup thread, IL2CPP helpers, feature
binding resolver, runtime caches, Dobby hooks, ImGui rendering, and feature
logic. It also owns the Info, Combat, Auto-Play, Shop, Arena, Appearance,
Settings, and Test overlay tabs. Keep feature work in this file unless the user
explicitly requests a multi-file refactor.

The current boot order is process gate, detached setup thread, early
`eglSwapBuffers` hook, `liblogic.so` wait, IL2CPP export resolution, setup
thread attach, `UnityEngine.Input.GetTouch` hook, and lazy render-thread ImGui
initialization. The render hook can exist before IL2CPP is ready, so any
frame-time managed work must stay behind readiness and thread-attach checks.

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

`.github/workflows/build.yml` is the CI release workflow. It builds with Android
NDK `29.0.14206865`, packages the generated `libs/` output with
`BUILD_INFO.txt`, uploads the zip as a workflow artifact, and publishes or
updates GitHub releases whose notes include commit descriptions from Git
history.

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
must be allowed to resolve later. Field misses may be cached only with a short
retry backoff so hot feature paths do not rescan metadata every frame. Method
resolution is name, parameter-count, and parameter-name-shape based; verify
overload-sensitive changes against `dump/dump.cs`. Preserve the separate 100 ms
shop and arena ticks, 250 ms Combat and Auto-Play ticks, and 500 ms opponent
history/HUD cadence unless the task explicitly changes timing.

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
keep scans bounded by the existing runtime limits. Buy and refresh actions
should wait for an operable shop panel, including delay/spectate/CanOperate
checks when those bindings are available. Do not add unbounded scans or
immediate retry loops to the shop hot path unless the task explicitly changes
that design.

Auto-Play automation is intentionally snapshot-based and throttled on a separate
250 ms tick. Preserve the bounded built-in AI startup, deployment/formation,
level-up, and auction cooldowns. The controller should read runtime state through
`ReadAutoPlaySnapshot()`, build shared interest-aware spend decisions through
`BuildAutoPlayGoldPlan()`, collect board units through
`CollectAutoPlayBoardUnits()`, score strategy and formation with
`BuildAutoPlayBoardPlan()`, and publish selected shop targets through the
existing target helpers. Keep opponent scans bounded to the battle manager
dictionary limit, keep battlefield movement to one chosen action per cooldown,
keep shop, auction, passive-gold, free-economy, and level-up decisions aligned
with the shared gold plan, keep built-in AI startup stateful instead of replaying
`StartAI` on every cooldown, keep SpeedHack as an explicit Arena-only control,
and do not hold `FeatureMutex` while calling managed IL2CPP APIs.

Auto-Play temporarily owns selected Shop, Arena, and Combat assists through its
policy backup while enabled. If a change touches those assist toggles, preserve
the capture/restore behavior and make it clear in user-facing docs when manual
edits made during Auto-Play can be restored to the pre-Auto-Play values.

## Testing Guidelines

There is no dedicated unit test framework in this repository. For native changes,
the required verification is a successful:

```sh
ndk-build -C jni
```

When changing IL2CPP calls, verify signatures against `dump/dump.cs` and confirm
the target remains `arm64-v8a`, Unity `2019.4.33f1`, and native C++ mode
`c++26`.

When changing Recommendation Lineup automation, verify the related
`MCLogicBattleData` and `MCBattleBridge` signatures against `dump/dump.cs` and
keep the overlay in a `Waiting for ...` state while runtime data is unavailable.

When changing Auto-Play automation, verify the related
`MCLogicBattleManager.StartAI`, `StopAI`, `TryAutoDeploy`, `OnPlayerLvlUp`,
`GetLineupWorth`, `CalcCurrentFightValue`,
`MoveHeroInBattleField(UInt32, Byte, Byte, Boolean)`,
`MCLogicBattleData.ILOGIC_GetAllBattleMgr`,
`MCLogicBattleData.ILOGIC_GetCurrentOpponentAccountID`,
`LogicRoundMgr.get_m_AuctionComp`,
`MCLogicAuctionComp.Bid(MCLogicAuctionSlotInfo, UInt64, UInt32)`, and
`MCLogicGoGoCardComp.get_m_CurrData` signatures against `dump/dump.cs`. Keep
the overlay in a `Waiting for ...` state while required runtime data is
unavailable.

When changing Arena Skip Round automation, verify
`MCLogicBattleData.get_logicRoundMgr`, `LogicRoundMgr.SetRound(UInt32)`, and
`LogicRoundMgr.NextRound(Boolean)` against `dump/dump.cs`. Keep the overlay in
a `Waiting for ...` state while round-manager bindings are unavailable. Automatic
skip requests should avoid fight/result phases and suppress repeated requests
for the same source round and target round.

When changing Arena SpeedHack, verify `UnityEngine.Time.set_timeScale(Single)`
against `dump/dump.cs` and reset the time scale when disabling the feature or
resetting feature state.

For documentation-only changes, at minimum inspect the rendered Markdown diff.
For native or mixed changes, also run:

```sh
git diff --check
```

For repository-wide documentation refreshes, update only top-level Markdown
files unless explicitly asked to edit submodules. Leave `goal.md`, generated
outputs, and vendored Markdown under `jni/Il2CppVersions/`, `jni/imgui/`, and
`jni/xDL/` untouched.

## Commit & Pull Request Guidelines

Recent commits use short, imperative summaries such as `Add enemy predictor and
project documentation`. Keep commit messages direct and focused on the change.
Release notes are generated from commit subjects and body text, so include
enough context in commits for them to stand alone in automated releases.

Pull requests should include a clear description, the build command result,
affected files, and any relevant notes about IL2CPP signatures or runtime hooks.
Link issues when applicable. Screenshots are useful only for visible ImGui
overlay changes.

## Agent-Specific Instructions

Keep changes scoped. Do not modify vendored directories such as
`jni/Il2CppVersions/`, `jni/imgui/`, or `jni/xDL/` unless explicitly requested.
Do not revert unrelated local changes in the working tree.

Current user-facing feature areas are Info, Combat, Auto-Play, Shop, Arena,
Appearance, Settings, and Test. If a feature binding is missing at runtime, the
overlay should show a `Waiting for ...` state rather than failing silently.
Auto-Play includes adaptive strategy pressure, built-in AI coordination,
opponent-aware board analysis, advanced role-aware formation moves, selected
shop target promotion, GogoCard scoring, auction scoring, gold-interest economy
decisions, and optional coordination of Combat and Arena assists. Shop currently
includes free-hero buying, selected target buying, Recommendation Lineup buying,
auto-refresh pause conditions, keep-gold reserve, and target counts. Combat includes
Invisible Scout. Arena includes hero/item/card granting, Battle Power controls
for force-win, HP-loss prevention, attack-ratio boosting, fight-value boosting,
and enemy-board crippling, active synergy forcing, level/population forcing,
enemy HP pressure, passive gold, free economy, unlimited hero pool, shop-lock
bypass helpers, Skip Round, and SpeedHack. Use the Runtime Status and Test tabs
when checking binding
readiness, managed references, round state, round-manager state, timeScale
binding readiness, player economy/rank/shop state, battle manager fields, battle
bridge state, shop panel state, behavior API state, Recommendation Lineup state,
Auto-Play state, auction state, GogoCard state, board formation state, or
opponent prediction logic. Test
diagnostics should stay read-only unless the task explicitly requests an action.
In the Test prediction table, `Will fight` is the local player's opponent
probability and `Current enemy` is the observed opponent for that row. Only the
exact local current opponent should be forced to `100%`; do not mark every row
as `100%` just because that row has a known current enemy. Preserve per-player
enemy history collection and dump-backed invader order reads so prediction
weights can account for recent meetings and the game's pairing list.
Appearance currently includes ImGui Dark, Catppuccin Mocha, and additional
palettes inspired by Dear ImGui issue #707. Keep `kAppearanceThemes` and
`Issue707ThemePalette` entries aligned, and preserve Catppuccin Mocha at theme
index `1` for existing configs. Keep mobile accessibility changes compatible
with the main ImGui TabBar. Helper buttons may select tabs, but the TabBar
should remain visible and authoritative.
Settings includes a persisted next-enemy HUD toggle that draws bottom-center
foreground text above the lower screen edge. Keep it lightweight and throttle
prediction refreshes instead of rebuilding predictions every render frame.
Settings config should default to the running game package directory as
`/data/data/<game-package>/files/mcgg_config.ini`.

Known audit hotspots are early-render readiness, dump-backed signature drift,
table cache all-or-nothing publication, shop panel operability before buy or
refresh, Auto-Play policy ownership of assist toggles, exact-opponent-only
`100%` prediction rows, and Unity timeScale reset paths.
