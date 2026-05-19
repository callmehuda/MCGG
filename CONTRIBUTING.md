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

Build the pinned OpenSSL, libpsl `0.21.5`, and curl submodules before the
native module:

```sh
bash jni/build-curl-android.sh
```

## Development Guidelines

- Keep source changes focused on the requested feature or fix.
- Use `dump/dump.cs` to verify IL2CPP class names, method signatures, return
  types, and fields before changing native calls.
- Regular instance field helpers should use resolved field offsets for hot
  typed reads and non-pointer writes, while retaining raw IL2CPP fallbacks.
  Keep static fields on the IL2CPP static APIs and preserve write barriers for
  managed-object pointer writes.
- Keep the default target `arm64-v8a`.
- Keep Unity compatibility aligned with `2019.4.33f1`.
- Keep native language mode aligned with `c++26` unless the build configuration
  intentionally changes.
- Keep the curl, libpsl `0.21.5`, and OpenSSL submodules pinned and use
  `jni/build-curl-android.sh` to generate `obj/curl-install/lib/libcurl.a`,
  `obj/libpsl-install/lib/libpsl.a`, `obj/openssl-install/lib/libssl.a`, and
  `obj/openssl-install/lib/libcrypto.a` before native builds. Curl should be
  configured with the pinned OpenSSL `4.0.0` TLS backend, pinned libpsl
  `0.21.5` support from
  `https://github.com/rockdaboot/libpsl/releases/tag/0.21.5`, and without curl
  feature-disabling flags.
- Keep build metadata embedded through `MCGG_BUILD_REPOSITORY`,
  `MCGG_BUILD_VERSION`, `MCGG_BUILD_COMMIT`, and `MCGG_BUILD_REF`. CI prepares
  the release version before compiling and passes those constants into
  `ndk-build`; local builds should keep the Git-derived fallbacks usable.
- Keep `jni/Application.mk` app-wide stability flags aligned with the current
  release profile: stack protector, fortify, conservative alias/overflow/null
  check behavior, unwind tables, hidden inline visibility, RELRO, immediate
  binding, and `--as-needed`.
- When using internet, video, or guide research for game context, record durable
  mechanics rather than seasonal tier claims. Current public references describe
  MCGG as an 8-player auto-battler with hero recruitment/upgrades, Commander
  skills, Go Go Cards, economy/interest decisions, synergies, equipment,
  auctions, board placement, and round-specific supplies. Public context was
  last checked on 2026-05-19; Google Play showed 10M+ downloads, a May 9, 2026
  store update, S6 Dawnlight Celebration event context, Commander Ruby, Gold
  Rush mode, City Hero draw, and the Neolight Wheel event in the checked web
  region, while MOONTON Season 5 news documented Go Go Plaza, GOGO MOBA, Golden
  Month content, new synergies, GO1 esports momentum, and a 30M-download
  milestone after global launch. Treat those public details as product context
  rather than native binding assumptions.
- Prediction research should treat public scouting and positioning advice as
  weak heuristics. Runtime current-opponent data, invader order, recent-cycle
  learning, the completed-history seven-round cycle model adapted from
  `../MCGG_Predictor`, cycle-gap distance, and local history should drive
  predictions before any generic internet or video-derived assumption.
- Preserve the current startup order: process gate, setup thread, early
  `eglSwapBuffers` hook, `liblogic.so` wait, IL2CPP export resolution, setup
  thread attach, `UnityEngine.Input.GetTouch` hook, then lazy render-thread
  ImGui initialization.
- Keep startup waits inside the detached setup thread. The library constructor
  should only gate the process and launch setup so it does not freeze process
  loading.
- The render hook can run before IL2CPP is ready. Frame-time managed work must
  stay behind readiness checks and successful render-thread IL2CPP attachment.
- Keep frame-time managed work within the feature frame budget. If a frame is
  already busy, defer noncritical ticks to the next frame instead of stacking
  every automation path into one render pass. Heavy Auto-Play fallback opponent
  scans should also yield to the budget and retry on a later tick.
- Persistent managed-object references must be pinned before publication with
  `il2cpp_gchandle_new(obj, true)`. Keep those handles match-scoped, retain
  replaced objects until the active match has ended, and release all accumulated
  handles only from the match-ended cleanup path.
- Use the Test tab, including its Runtime Status section, to validate binding
  readiness, update-check status, managed references, round state, player
  economy/rank/shop state, battle manager fields, battle bridge state, shop
  panel state, shop diagnostic reader readiness, behavior API state, Auto-Play
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
  The seven-round cycle-pattern signal learned from `../MCGG_Predictor` should
  use only completed current-cycle history: unknown pattern can tentatively
  predict R4 from local R1, classic pattern predicts R4/R5 from local R1/R3,
  and shifted pattern predicts R5/R6/R7 from the local R1 opponent's R4/R2/R3
  matchups. Recent-cycle distance and cycle-pattern signals may bias
  probabilities, but they must stay weaker than exact current-opponent or
  reverse-pair reads.
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
  Built-in AI startup should be opt-in, limited to safe non-fight/non-result
  phases, and stateful so `StartAI` is not replayed continuously for the same
  account, with only a long-gated refresh to recover dropped internal AI state.
  Keep built-in deployment and smart formation on separate cooldowns so formation
  movement cannot starve `TryAutoDeploy`, and keep budget checks between
  card/auction/AI/formation/level-up action groups after planning.
  Auto-Play should not enable or disable Arena SpeedHack; SpeedHack remains an
  explicit Arena control.
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
- Arena achievement forcing depends on
  `MCLogicAchievementRecordComp.AchievementDataBase.GetResult`,
  `canRecordAchievementData`, `JudgeFinalRelation`,
  `JudgeReachCondition(List<MCLogicPlayer>)`,
  `MCLogicAchievementRecordComp.AchievementRoundData.GetResult`,
  `AchievementRoundData.RefreshData`, and
  `m_roundAchievementCount`/`m_roundSuccessCount`. Verify these against
  `dump/dump.cs` before changing achievement hooks or counter writes.
- GGC Info depends on
  `MCLogicBattleData.ILOGIC_GetCrystalQualityByRound(UInt64, Int32)`. Verify it
  against `dump/dump.cs`, keep the round scan bounded, and keep the readout on
  its throttled refresh cadence.
- Settings includes a persisted next-enemy HUD toggle. Keep the HUD as
  lightweight bottom-center foreground text and throttle current-opponent or
  prediction refreshes instead of doing prediction work every render frame.
- Keep Settings save/load behavior scoped to the project config file under the
  running game package directory, normally
  `/data/data/<game-package>/files/mcgg_config.ini`.
- Settings also includes an informational `Updates / Changelog` section. Keep
  GitHub Releases checks asynchronous, cached in memory under
  `RuntimeMutex::UpdateMutex`, throttled to the 6-hour refresh cadence with
  bounded retry backoff, and limited to public release metadata. Do not send
  gameplay state, account data, device identifiers, credentials, or private
  runtime data, and do not add automatic download, deployment, forced update,
  bypass, or evasion behavior.
- Do not commit generated `libs/` or `obj/` output, including curl/libpsl/OpenSSL
  build output under `obj/curl-*`, `obj/libpsl-*`, `obj/openssl-*`, and related
  install dirs.
- Do not edit vendored directories such as `jni/Il2CppVersions/`, `jni/imgui/`,
  `jni/xDL/`, `jni/curl/`, `jni/libpsl/`, or `jni/openssl/` unless the change
  explicitly requires it.

Current user-facing overlay areas are Info, Combat, Auto-Play, Shop, Arena,
Appearance, Settings, and Test. Info includes the player/enemy table and
automatic GGC quality readout for every detected GGC round. Auto-Play includes
adaptive strategy pressure, opponent-aware board analysis, advanced role-aware
formation moves, selected shop target promotion, GogoCard scoring, auction
scoring, gold-interest economy decisions, and optional coordination of Combat
and Arena assists. Shop currently includes free-hero buying, manual target
buying, Recommendation Lineup buying, auto-refresh pause conditions, keep-gold
reserve, and target counts. Combat includes Invisible Scout. Arena includes
hero/item/card granting, Battle Power controls for force-win, HP-loss
prevention, attack-ratio boosting, fight-value boosting, and enemy-board
crippling, active synergy forcing, level/population forcing, enemy HP pressure,
achievement task forcing, passive gold, free economy, unlimited hero pool,
shop-lock bypass helpers, Skip Round, and SpeedHack.
Appearance includes ImGui
Dark, Catppuccin Mocha, and additional palettes inspired by Dear ImGui issue #707.
Settings includes the optional next-enemy HUD and GitHub release update status
alongside menu size, position, style, and save/load controls.
Test diagnostics are split into tabbed sections for prediction, bindings, round
state, player data, battle managers, battle bridge, shop UI, behavior API, and
all-manager views. New user-facing controls should report delayed runtime
dependencies with a clear `Waiting for ...` state where practical.
Shop diagnostics use grouped readiness over core shop diagnostic readers; each
individual row should still show `Waiting` while its specific reader is missing.

## Threading and Shared State

- `RuntimeMutex::CacheMutex` guards IL2CPP method and field caches.
- `RuntimeMutex::ManagedHandleMutex` guards pinned managed-object handle
  ownership for cached references such as the battle bridge, shop panel, shop
  item list, and `LoadRes`.
- `RuntimeMutex::FeatureMutex` guards complex feature collections such as
  `FeatureState::Heroes`, `FeatureState::Equips`, `FeatureState::Cards`, and
  `FeatureState::ShopSelectedHeroes`.
- `RuntimeMutex::UpdateMutex` guards GitHub release update-check state and the
  cached changelog entries used by Settings and Test.
- Use existing snapshot and access helpers such as `GetSortedHeroes()`,
  `GetSortedEquips()`, `GetSortedCards()`, `TryGetHeroTableEntry()`,
  `GetShopHeroTargetsSnapshot()`, and
  `GetSelectedShopHeroTargetsSnapshot()` instead of ad hoc unlocked map reads.
- Do not hold `RuntimeMutex::FeatureMutex` while calling managed IL2CPP APIs.
  Gather local data first, then publish the result under the lock.
- Do not clear or replace pinned GC handles during a live match. Cache refreshes
  may publish a newer pinned object, but the old handle remains owned until the
  match-ended cleanup releases the complete handle set.
- `RuntimeMutex::UiMutex` guards UI/config strings and config save/load status.
  Primitive feature flags, counters, and managed reference pointers are
  `std::atomic`; follow the existing `.load()` and assignment patterns.

## Coding Style

Follow the existing C++ style in `jni/Main.cpp`:

- Use 4-space indentation.
- Prefer explicit pointer types.
- Keep helper code small and direct.
- Keep the existing short comment above each project-owned native function in
  `jni/Main.cpp` and `jni/structures/Structures.hpp`; add extra block comments
  only where risky IL2CPP calls, hooks, value-type layouts, or timing-sensitive
  behavior need more context.
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
- Keep offset-based field access bounded and fallback-capable. A missing or
  invalid offset should degrade to the IL2CPP raw access path, not to a
  one-shot failure.
- Keep runtime cadence split by responsibility: 100 ms for Shop and Arena,
  250 ms for Combat and Auto-Play, and 500 ms for GGC Info, opponent prediction
  history, and the next-enemy HUD refresh.
- Preserve Auto-Play's sub-cooldowns inside that 250 ms tick: stateful
  opt-in `StartAI`, long-gated AI refresh, built-in deploy, separate smart
  formation, level-up, and auction actions should not share one retry clock.
- Confirm built-in AI remains default-disabled and phase-gated so enabling
  Auto-Play itself does not immediately invoke `MCLogicBattleManager.StartAI`.
- Keep table cache loading demand-driven. Unrelated tabs should not repeatedly
  perform heavy table scans, and long Shop/Arena table views should use visible
  row clipping.
- Keep opponent prediction table rows cached on the 500 ms prediction cadence.
  Drawing the Test tab or next-enemy HUD should reuse cached rows instead of
  rebuilding managed prediction state every render frame.

## Runtime Audit Checklist

Use this checklist when looking for hidden bugs or logic flaws:

- Separate stable game systems from volatile public meta. Guides and videos are
  useful for UI flow and terminology, but native bindings must still be verified
  through `dump/dump.cs` and runtime diagnostics.
- Confirm every new IL2CPP method pointer, hook signature, value type, and field
  read against `dump/dump.cs`, especially overloaded methods where runtime
  resolution only checks method name, parameter count, and parameter-name shape.
- Confirm regular instance field offset use against the target Unity API. Static
  fields and managed-object pointer writes should continue through IL2CPP APIs
  when runtime setter behavior matters.
- Keep early-frame paths safe when `eglSwapBuffers` is hooked but IL2CPP is not
  ready or the render thread has not attached.
- Confirm render-frame budget checks still let delayed work retry on later
  frames and do not turn retryable runtime state into a one-shot failure.
- Confirm prediction changes preserve the source priority: exact live pair,
  reverse live pair, invader-order read, recent-cycle queue, seven-round
  cycle-pattern signal from completed history, round-robin fallback,
  recent-cycle distance, and only then generic history weighting.
- Keep method misses and field misses backed off rather than permanently cached
  as unavailable, and keep feature binding resolution single-flight so setup and
  render retries do not scan IL2CPP metadata at the same time.
- Treat table caches as all-or-nothing for heroes, equipment, and GogoCards.
  UI and automation should show `Waiting for ...` while any required table is
  unavailable.
- Keep shop buy and refresh actions gated on the live shop panel being
  non-delayed, non-spectate, and accepted by `CanOperate(Boolean)`.
- Keep shop diagnostics tied to grouped reader readiness instead of a single
  shop stat binding, and keep per-value Test rows individually retryable.
- Keep long table UIs clipped and demand-load table caches only for table-backed
  tabs or active automation that consumes table metadata.
- Preserve Auto-Play policy backup and restore behavior for Shop, Arena, and
  Combat assist toggles.
- Keep opponent prediction exactness narrow: only the local player's exact
  current opponent should be forced to `100%`.
- Keep GGC Info bounded: scan only the configured round range, ignore unknown
  quality values, and refresh on its 500 ms cadence rather than every render
  frame.
- Reset Unity time scale to `1.0x` on every SpeedHack disable, inactive-battle,
  and feature-reset path.
- For repository-wide documentation refreshes, update top-level Markdown only
  and leave `goal.md` plus submodule Markdown untouched.

## Build Verification

Run this before submitting native code changes:

```sh
bash jni/build-curl-android.sh
ndk-build -C jni
```

The expected outputs are:

```text
obj/openssl-install/lib/libssl.a
obj/openssl-install/lib/libcrypto.a
obj/libpsl-install/lib/libpsl.a
obj/curl-install/lib/libcurl.a
libs/arm64-v8a/libmain.so
```

Native build submissions should preserve the app-wide stability flags in
`jni/Application.mk` unless the task explicitly changes the release profile.

Documentation-only changes do not require a native build, but mention that in
the pull request. Inspect the Markdown diff and run `git diff --check` before
submitting documentation-only repository refreshes.

## Release Workflow

The `.github/workflows/build.yml` workflow runs for pushes to `master`, pull
requests targeting `master`, and manual dispatches. It installs the curl,
libpsl, and OpenSSL build prerequisites, builds the static OpenSSL, pinned
libpsl `0.21.5`, and curl archives, builds with Android NDK `29.0.14206865`,
packages `libs/`, writes `BUILD_INFO.txt`, and uploads the zip as a workflow
artifact.

For non-pull-request runs, the workflow publishes or updates a GitHub release.
Release notes are generated from Git history and include commit descriptions
for the GitHub push range when available, otherwise from the previous `v*` tag
through the current commit, falling back to the current commit only. Commit
subjects and body text are included where present, so write commit messages with
enough context to stand alone in release notes.

## Commit Messages

All commit messages follow the [Conventional Commits](https://www.conventionalcommits.org/) specification. Use short typed commit messages when possible:

```text
feat(main): add player sorting
fix(main): guard missing battle data
perf(main): optimize player table rendering
docs(readme): update setup guide
chore(repo): ignore build outputs
```

Accepted commit types:

- `feat`: new feature or feature area addition
- `fix`: bug fix or correctness fix
- `perf`: performance improvement
- `refactor`: code behavior-preserving refactor
- `docs`: documentation-only changes
- `build`: build system or NDK configuration changes
- `ci`: CI workflow or release packaging changes
- `chore`: maintenance, merge commits, or submodule updates
- `revert`: revert of a previous commit
- `test`: test diagnostics or test-tab improvements

Scope is optional but recommended for clarity. Common scopes include `main`, `ui`, `shop`, `arena`, `autoplay`, `hud`, `appearance`, `test`, and `readme`.

## Pull Requests

Pull requests should include:

- A short summary of the change.
- The files or areas affected.
- The build command result, usually `ndk-build -C jni`.
- Notes about any IL2CPP signatures, fields, or hooks changed.
- Notes about any overlay tab, theme, font, or runtime status behavior changed.
- Screenshots only when the ImGui overlay changes visually.

Small, focused pull requests are easiest to review and merge.
