# Contributing

Thanks for helping improve MCGG. This project is focused on native Android
modding research for Magic Chess Go Go, so contributions should stay readable,
scoped, and easy to verify.

## Responsible Use

Only contribute changes intended for learning, research, reverse engineering
practice, and authorized local testing. Do not add game APKs, copyrighted game
assets, paid content, bypasses, or
instructions for abusing online services.

## Project Setup

Clone with submodules:

```sh
git clone --recursive https://github.com/Yan-0001/MCGG.git
cd MCGG
```

If submodules are missing:

```sh
git submodule update --init --recursive
```

Pull Git LFS files:

```sh
git lfs install
git lfs pull
```

## Development Guidelines

- Keep source changes focused on the requested feature or fix.
- Use `dump/dump.cs` to verify IL2CPP class names, method signatures, return
  types, and fields before changing native calls.
- Keep the default target `arm64-v8a`.
- Keep Unity compatibility aligned with `2019.4.33f1`.
- Keep native language mode aligned with `c++26` unless the build configuration
  intentionally changes.
- Preserve the current startup order: process gate, setup thread, early
  `eglSwapBuffers` hook, `liblogic.so` wait, IL2CPP export resolution, setup
  thread attach, `UnityEngine.Input.GetTouch` hook, then lazy render-thread
  ImGui initialization.
- The render hook can run before IL2CPP is ready. Frame-time managed work must
  stay behind readiness checks and successful render-thread IL2CPP attachment.
- Use the Runtime Status and Test tabs to validate binding readiness, managed
  references, round state, player economy/rank/shop state, battle manager
  fields, battle bridge state, shop panel state, behavior API state, Auto-Play
  state, auction state, GogoCard state, board formation state, and opponent
  prediction behavior after feature changes.
- In the Test prediction table, `Will fight` is the local player's opponent
  probability. Only the exact local current opponent should be forced to
  `100%`; other rows should stay weighted even when their `Current enemy`
  value is known. `Recent` is derived from the per-player opponent history.
- Opponent prediction should use dump-backed runtime state first. Prefer
  `LogicInvasionMgr`, `LogicRealPlayerInvader.lbmList`,
  `PairGenRoundTable`/`PairGenTwoPlayerMode`, `lastRoundEnemy`, and
  `prevRealPlayerEnemy` before falling back to heuristic account ordering.
- For Shop changes, preserve the existing throttled automation model: buy,
  repeat-buy, refresh, target-worth, and Recommendation Lineup checks must stay
  bounded, snapshot-based, and retryable. Buy and refresh UI actions should also
  wait for a non-delayed, non-spectate, operable shop panel when those panel
  bindings are available.
- For Auto-Play changes, preserve the 250 ms tick and bounded action model:
  built-in AI startup, deployment/formation moves, level-up actions, and auction
  bids must stay cooldown-based. Use runtime snapshots, table metadata, current
  opponent data, board-unit scans, and selected-target helpers instead of
  unbounded searches or long lock holds. Keep gold-interest decisions centralized
  in the Auto-Play gold plan so shop spending, auction bids, passive gold,
  free-economy assists, and level-up actions share the same reserve logic.
  Built-in AI startup should be stateful so `StartAI` is not replayed
  continuously for the same account. Auto-Play should not enable or disable
  Arena SpeedHack; SpeedHack remains an explicit Arena control.
- Auto-Play depends on dump-backed bindings for
  `MCLogicBattleManager.StartAI`, `TryAutoDeploy`, `OnPlayerLvlUp`,
  `GetLineupWorth`, `CalcCurrentFightValue`,
  `MoveHeroInBattleField(UInt32, Byte, Byte, Boolean)`,
  `MCLogicBattleData.ILOGIC_GetAllBattleMgr`,
  `MCLogicBattleData.ILOGIC_GetCurrentOpponentAccountID`,
  `LogicRoundMgr.get_m_AuctionComp`,
  `MCLogicAuctionComp.Bid(MCLogicAuctionSlotInfo, UInt64, UInt32)`, and
  `MCLogicGoGoCardComp.get_m_CurrData`. Verify these against `dump/dump.cs`
  before changing related function pointers or field reads.
- Recommendation Lineup automation depends on both `MCLogicBattleData` and
  `MCBattleBridge` bindings. Verify signatures against `dump/dump.cs` before
  changing related method pointers.
- Arena Skip Round depends on `MCLogicBattleData.get_logicRoundMgr`,
  `LogicRoundMgr.SetRound(UInt32)`, and `LogicRoundMgr.NextRound(Boolean)`.
  Keep the UI in a `Waiting for ...` state while those bindings are missing.
  Automatic skip should avoid fight/result phases and suppress repeated
  requests for the same source round and target round.
- Arena SpeedHack depends on `UnityEngine.Time.set_timeScale(Single)`. Reset
  the time scale when disabling the feature, leaving active battle state, or
  resetting feature state.
- Settings includes a persisted next-enemy HUD toggle. Keep the HUD as
  lightweight bottom-center foreground text and throttle current-opponent or
  prediction refreshes instead of doing prediction work every render frame.
- Keep Settings save/load behavior scoped to the project config file under the
  running game package directory, normally
  `/data/data/<game-package>/files/mcgg_config.ini`.
- Do not commit generated `libs/` or `obj/` output.
- Do not edit vendored directories such as `jni/Il2CppVersions/`, `jni/imgui/`,
  or `jni/xDL/` unless the change explicitly requires it.

Current user-facing overlay areas are Info, Combat, Auto-Play, Shop, Arena,
Appearance, Settings, and Test. Auto-Play includes adaptive strategy pressure,
opponent-aware board analysis, advanced role-aware formation moves, selected
shop target promotion, GogoCard scoring, auction scoring, gold-interest economy
decisions, and optional coordination of Combat and Arena assists. Shop currently
includes free-hero buying, manual target buying, Recommendation Lineup buying,
auto-refresh pause conditions, keep-gold reserve, and target counts. Combat
includes Invisible Scout. Arena includes hero/item/card granting, Battle Power controls for
force-win, HP-loss prevention, attack-ratio boosting, fight-value boosting, and
enemy-board crippling, active synergy forcing, level/population forcing, enemy
HP pressure, passive gold, free economy, unlimited hero pool, shop-lock bypass
helpers, Skip Round, and SpeedHack.
Appearance includes ImGui
Dark, Catppuccin Mocha, and additional palettes inspired by Dear ImGui issue #707.
Settings includes the optional next-enemy HUD alongside menu size, position,
style, and save/load controls.
Test diagnostics are split into tabbed sections for prediction, bindings, round
state, player data, battle managers, battle bridge, shop UI, behavior API, and
all-manager views. New user-facing controls should report delayed runtime
dependencies with a clear `Waiting for ...` state where practical.

## Threading and Shared State

- `RuntimeMutex::CacheMutex` guards IL2CPP method and field caches.
- `RuntimeMutex::FeatureMutex` guards complex feature collections such as
  `FeatureState::Heroes`, `FeatureState::Equips`, `FeatureState::Cards`, and
  `FeatureState::ShopSelectedHeroes`.
- Use existing snapshot and access helpers such as `GetSortedHeroes()`,
  `GetSortedEquips()`, `GetSortedCards()`, `TryGetHeroTableEntry()`,
  `GetShopHeroTargetsSnapshot()`, and
  `GetSelectedShopHeroTargetsSnapshot()` instead of ad hoc unlocked map reads.
- Do not hold `RuntimeMutex::FeatureMutex` while calling managed IL2CPP APIs.
  Gather local data first, then publish the result under the lock.
- `RuntimeMutex::UiMutex` guards UI/config strings and config save/load status.
  Primitive feature flags, counters, and managed reference pointers are
  `std::atomic`; follow the existing `.load()` and assignment patterns.

## Coding Style

Follow the existing C++ style in `jni/Main.cpp`:

- Use 4-space indentation.
- Prefer explicit pointer types.
- Keep helper code small and direct.
- Add short comments above non-obvious functions or risky blocks.
- Keep existing UI element names, method names, and hook names stable unless a
  rename is part of the requested change.
- Keep appearance changes local to the existing theme/font setup unless a
  broader UI refactor is part of the requested change.
- When adding Appearance themes, keep `kAppearanceThemes` and
  `Issue707ThemePalette` entries aligned and preserve Catppuccin Mocha at theme
  index `1` for existing configs.
- Keep mobile menu accessibility changes compatible with the main ImGui TabBar;
  helper controls may select tabs, but the TabBar should remain the primary
  navigation surface.
- Keep config parsing simple, bounded, and compatible with the existing
  key-value Settings file format.
- Keep retryable IL2CPP field misses throttled instead of permanently failed or
  repeatedly rescanned from hot feature paths.
- Keep runtime cadence split by responsibility: 100 ms for Shop and Arena,
  250 ms for Combat and Auto-Play, and 500 ms for opponent prediction history
  and the next-enemy HUD refresh.

## Runtime Audit Checklist

Use this checklist when looking for hidden bugs or logic flaws:

- Confirm every new IL2CPP method pointer, hook signature, value type, and field
  read against `dump/dump.cs`, especially overloaded methods where runtime
  resolution only checks method name, parameter count, and parameter-name shape.
- Keep early-frame paths safe when `eglSwapBuffers` is hooked but IL2CPP is not
  ready or the render thread has not attached.
- Keep method misses retryable and field misses backed off rather than
  permanently cached as unavailable.
- Treat table caches as all-or-nothing for heroes, equipment, and GogoCards.
  UI and automation should show `Waiting for ...` while any required table is
  unavailable.
- Keep shop buy and refresh actions gated on the live shop panel being
  non-delayed, non-spectate, and accepted by `CanOperate(Boolean)`.
- Preserve Auto-Play policy backup and restore behavior for Shop, Arena, and
  Combat assist toggles.
- Keep opponent prediction exactness narrow: only the local player's exact
  current opponent should be forced to `100%`.
- Reset Unity time scale to `1.0x` on every SpeedHack disable, inactive-battle,
  and feature-reset path.
- For repository-wide documentation refreshes, update top-level Markdown only
  and leave `goal.md` plus submodule Markdown untouched.

## Build Verification

Run this before submitting native code changes:

```sh
ndk-build -C jni
```

The expected output is:

```text
libs/arm64-v8a/libmain.so
```

Documentation-only changes do not require a native build, but mention that in
the pull request. Inspect the Markdown diff and run `git diff --check` before
submitting documentation-only repository refreshes.

## Release Workflow

The `.github/workflows/build.yml` workflow runs for pushes to `master`, pull
requests targeting `master`, and manual dispatches. It builds with Android NDK
`29.0.14206865`, packages `libs/`, writes `BUILD_INFO.txt`, and uploads the zip
as a workflow artifact.

For non-pull-request runs, the workflow publishes or updates a GitHub release.
Release notes are generated from Git history and include commit descriptions
for the GitHub push range when available, otherwise from the previous `v*` tag
through the current commit, falling back to the current commit only. Commit
subjects and body text are included where present, so write commit messages with
enough context to stand alone in release notes.

## Commit Messages

Use short typed commit messages when possible:

```text
feat/main: add player sorting
fix/main: guard missing battle data
perf/main: optimize player table rendering
docs/readme: update setup guide
chore/repo: ignore build outputs
```

## Pull Requests

Pull requests should include:

- A short summary of the change.
- The files or areas affected.
- The build command result, usually `ndk-build -C jni`.
- Notes about any IL2CPP signatures, fields, or hooks changed.
- Notes about any overlay tab, theme, font, or runtime status behavior changed.
- Screenshots only when the ImGui overlay changes visually.

Small, focused pull requests are easiest to review and merge.
