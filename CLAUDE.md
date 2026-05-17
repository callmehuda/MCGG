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
- The GitHub Actions release workflow is `.github/workflows/build.yml`; it builds with Android NDK `29.0.14206865`, packages `libs/`, and publishes release notes that include commit descriptions.

## Code Architecture & Standards

### High-Level Architecture
- **Core Logic**: `jni/Main.cpp` handles the entire mod lifecycle: process verification → setup thread → early `eglSwapBuffers` hook → dependency resolution (`liblogic.so`) → IL2CPP export resolution and setup-thread attachment → `UnityEngine.Input.GetTouch` hook → lazy render-thread ImGui initialization → render-thread IL2CPP attach → retryable game method and field resolution → managed reference refresh → feature ticks → overlay rendering.
- **Feature Binding**: `ResolveFeatureBindings()` resolves game methods and hooks. Missing methods and fields are retried periodically because Unity metadata and battle objects may not be ready during first setup. Field misses use a short retry backoff so hot feature paths do not rescan missing metadata every frame.
- **Hooking Strategy**: Uses Dobby to hook `eglSwapBuffers` for frame-by-frame UI injection, `UnityEngine.Input.GetTouch` for touch-to-mouse forwarding, and selected game methods for Combat visibility and Arena behavior.
- **Runtime Ticks**: Arena effects and Shop automation run on separate 100 ms ticks. Combat power and Auto-Play run on separate 250 ms ticks. Opponent prediction history and next-enemy HUD text refresh on 500 ms cadences. Auto-Play builds a shared gold-interest plan and uses bounded cooldowns for stateful built-in AI startup, deployment/formation moves, level-up actions, and auction bidding. Shop automation uses bounded cooldowns for buy, repeat-buy, refresh, target-worth, and Recommendation Lineup checks, and waits for the shop panel to be operable before UI actions.
- **Runtime Caches**: Managed references are cached through atomic pointers. Hero/equipment/GogoCard table data is collected locally and published under `RuntimeMutex::FeatureMutex` when entering a new match.
- **Diagnostics**: Runtime Status and Test tabs expose binding readiness, Auto-Play readiness, Recommendation Lineup readiness, managed reference refresh, Battle Power readiness, round state, Arena round-manager readiness, Unity timeScale readiness, player economy/rank/shop state, battle manager fields, battle bridge state, shop panel state, behavior API state, all manager entries, and opponent prediction signals. In the prediction table, `Will fight` is local-player opponent probability; `Current enemy` is the observed opponent for that row; `Recent` comes from the per-player opponent history.
- **Configuration**: Settings saves and loads visual, window, HUD, Auto-Play, Combat, Shop, and Arena controls from `/data/data/<game-package>/files/mcgg_config.ini`.
- **CI Releases**: `.github/workflows/build.yml` creates UTC date-based release tags, packages `libs/` with `BUILD_INFO.txt`, and generates release notes from commit subjects and body text in the push range or release-tag fallback.
- **Memory Mapping**: `jni/structures/Structures.hpp` defines the layout of Unity/Mono types to allow native interaction with managed objects.
- **Reference**: `dump/dump.cs` serves as the source of truth for the target game's internal C# structure.

### Current Feature Areas
- **Info**: runtime status, player/enemy table, and GGC round 7/13 quality display.
- **Combat**: Invisible Scout.
- **Auto-Play**: binary-side adaptive strategy controller. It reads round,
  phase, HP, gold, level, population, lineup worth, fight value,
  Recommendation Lineup, star-up target, all battle managers, current opponent,
  live board unit positions, hero table metadata, GogoCard choices, and auction
  slots. It chooses Economy/Balanced/Aggressive pressure, publishes selected
  shop targets, scores advanced role-aware formation moves using enemy column
  threat and ally cover, scores GogoCards, scores auction bids, protects 10-gold
  interest breakpoints through a shared reserve/spend budget plan, and can
  coordinate existing shop/economy/combat/arena assist controls. It does not
  control Arena SpeedHack.
- **Appearance**: ImGui Dark, Catppuccin Mocha, and Dear ImGui issue #707-inspired theme selection plus Default/Noto Sans CJK font selection.
- **Settings**: menu size, fixed position, mobile-friendly TabBar helpers, next-enemy HUD text, font scale, style tuning, and save/load configuration, including Auto-Play state.
- **Shop**: auto-buy free heroes, auto-buy selected targets, auto-buy Recommendation Lineup heroes, auto-refresh, pause-refresh conditions, keep-gold threshold, manual target counts, Recommendation Lineup target counts, and shop-panel operability gates before buy/refresh UI actions.
- **Arena**: hero spawn, equipment grant, GogoCard forcing, Battle Power controls, active synergy forcing, level/population 99, outside-map placement, enemy HP 1, passive gold, free economy, unlimited hero pool, shop-lock bypass helpers, fight/result-aware Skip Round, and SpeedHack with reset-to-normal handling.
- **Test**: manual binding retry, account inspection, fight prediction, binding, round, player, manager, bridge, shop UI, behavior API, and all-manager diagnostics. Only the exact local current opponent should be locked to `100%` in `Will fight`; every player's enemy history and dump-backed invader order should remain available for weighted predictions.

### Project Constraints
- **Target ABI**: `arm64-v8a`
- **Android Platform**: `android-21`
- **STL**: `c++_static`
- **Unity Version**: `2019.4.33f1` (Refer to `UNITY_` macros in `jni/Android.mk`)
- **C++ Standard**: C++26 (defined in `jni/Android.mk`)
- **NDK App Settings**: `APP_OPTIM := release`, `APP_THIN_ARCHIVE := false`, `APP_PIE := true`
- **Native C Flags**: `-Oz` and `-DNDEBUG` by default; `NDK_DEBUG=1` adds `-O0`

### Runtime Audit Focus

- The render hook is installed before `liblogic.so` and IL2CPP are ready. Keep managed calls behind `IsIl2CppRuntimeReady()` and render-thread attach checks.
- Runtime method matching is dump-guided but still name, parameter-count, and parameter-name-shape based. Treat overload-sensitive bindings as unsafe until checked in `dump/dump.cs`.
- Table cache loading publishes only after hero, equipment, and GogoCard data are all present. Dependent UI should keep `Waiting for ...` states while any table is missing.
- Auto-Play owns selected Shop, Arena, and Combat assist settings through a backup while it is active; preserve capture/restore semantics.
- Opponent prediction may display exact data and weighted guesses together. Only the exact local current opponent should be forced to `100%`.
- SpeedHack changes global Unity time scale and must reset to `1.0x` when disabled, when battle state is unavailable, or when feature state resets.
- Repository-wide documentation work should update the top-level Markdown files only: `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `README.md`, and `README.id.md`. Leave `goal.md` and submodule Markdown untouched.

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
- **Retry behavior**: Do not permanently cache failed IL2CPP method or field lookups. Missing bindings should retry, field miss retries should stay throttled, and user-facing controls should show `Waiting for ...` states where practical.
- **HUD diagnostics**: Keep the next-enemy HUD as lightweight foreground text near the bottom center. Reuse throttled prediction/current-opponent data and avoid rebuilding prediction tables every render frame.
- **Test diagnostics**: Keep Test additions read-only unless explicitly requested otherwise, and verify class names, method names, parameter counts, return types, and fields against `dump/dump.cs`.
- **Mobile menu**: Keep mobile accessibility helpers compatible with the main ImGui TabBar. Helper controls may select tabs, but the TabBar should remain visible.
- **Shop automation**: Preserve the single-threaded, throttled frame-tick model. Use existing atomic toggles/counters and selected-target snapshot helpers. Wait for non-delayed, non-spectate, operable shop panel state before buy or refresh UI calls. Avoid unbounded scans, immediate retry loops, or holding locks across managed calls in the hot path unless a future design explicitly requires them.
- **Auto-Play automation**: Preserve the 250 ms tick and bounded cooldowns. Use
  `ReadAutoPlaySnapshot()`, `BuildAutoPlayGoldPlan()`,
  `CollectAutoPlayBoardUnits()`, `BuildAutoPlayBoardPlan()`, and the existing
  table/target helpers. Keep opponent scans bounded to the battle manager
  dictionary limit, keep board placement to one move per cooldown, keep shop,
  auction, passive-gold, free-economy, and level-up decisions on the shared
  gold plan, keep built-in AI startup stateful, keep SpeedHack as a manual
  Arena-only control, and never hold `FeatureMutex` while calling managed IL2CPP APIs.
- **Auto-Play signatures**: Verify `MCLogicBattleManager.StartAI`,
  `TryAutoDeploy`, `OnPlayerLvlUp`, `GetLineupWorth`,
  `CalcCurrentFightValue`, `MoveHeroInBattleField(UInt32, Byte, Byte,
  Boolean)`, `LogicRoundMgr.get_m_AuctionComp`,
  `MCLogicAuctionComp.Bid(MCLogicAuctionSlotInfo, UInt64, UInt32)`, and
  `MCLogicGoGoCardComp.get_m_CurrData` against `dump/dump.cs` before changing
  the controller.
- **Arena round control**: Preserve the separate Arena tick and bounded Skip Round cooldown. Verify `MCLogicBattleData.get_logicRoundMgr`, `LogicRoundMgr.SetRound(UInt32)`, and `LogicRoundMgr.NextRound(Boolean)` against `dump/dump.cs`. Automatic skip should wait out fight/result phases and avoid repeating the same source/target request.
- **Arena SpeedHack**: Use `UnityEngine.Time.set_timeScale(Single)` and reset time scale to normal when the feature is disabled, leaves active battle state, or feature state is reset.
- **Comments**: Add concise comments before risky IL2CPP calls, hook signatures, value-type layouts, or timing-sensitive blocks.
- **Scope**: Do not modify vendored directories (`jni/imgui/`, `jni/xDL/`, `jni/dobby/`, `jni/Il2CppVersions/`) unless explicitly requested.
- **Appearance**: Keep theme/font changes in the existing appearance setup and preserve fallback to the default ImGui font when Noto Sans CJK is unavailable. When adding themes, keep `kAppearanceThemes` and `Issue707ThemePalette` entries aligned and preserve Catppuccin Mocha at theme index `1` for existing configs.
- **Settings**: Keep persistence in the project config file under the game app data directory; do not re-enable ImGui `.ini` persistence unless explicitly requested.
