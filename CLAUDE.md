# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Build & Setup
- **Initialize submodules**: `git submodule update --init --recursive`
- **Pull LFS assets**: `git lfs pull`
- **Build OpenSSL, libpsl `0.21.5`, and curl static libraries**: `bash jni/build-curl-android.sh`
- **Build native library**: `ndk-build -C jni`
- **Build Output**: `libs/arm64-v8a/libmain.so`

### Verification
- There is no unit test framework. Verification is a successful curl and native
  build: `bash jni/build-curl-android.sh` then `ndk-build -C jni`.
- For IL2CPP changes, verify method signatures and field offsets against `dump/dump.cs`.
- Run `git diff --check` for native or mixed code changes.
- For documentation-only edits, inspect the Markdown diff before finishing.
- The GitHub Actions release workflow is `.github/workflows/build.yml`; it
  prepares release metadata before compilation, passes `MCGG_BUILD_*` constants
  into `ndk-build`, installs curl/libpsl/OpenSSL build tools, builds the static
  OpenSSL, pinned libpsl `0.21.5`, and curl archives, builds with Android NDK
  `29.0.14206865`, packages `libs/`, and publishes release notes that include commit
  descriptions.
- `jni/Application.mk` carries app-wide stability flags for stack protection,
  fortify checks, conservative alias/overflow/null-check behavior, unwind
  tables, hidden inline visibility, RELRO, immediate binding, and `--as-needed`.

## Code Architecture & Standards

### High-Level Architecture
- **Core Logic**: `jni/Main.cpp` handles the entire mod lifecycle: process verification → detached setup thread with startup waits → early `eglSwapBuffers` hook → dependency resolution (`liblogic.so`) → IL2CPP export resolution and setup-thread attachment → guarded first feature-binding pass → `UnityEngine.Input.GetTouch` hook → lazy render-thread ImGui initialization → render-thread IL2CPP attach → retryable game method and field resolution → managed reference refresh → feature ticks → overlay rendering.
- **Feature Binding**: `ResolveFeatureBindings()` resolves game methods and hooks. Missing methods and fields are retried periodically because Unity metadata and battle objects may not be ready during first setup. Empty method scans and field misses use short retry backoffs so hot feature paths do not rescan missing metadata every frame. Binding resolution is single-flight so the setup thread and render thread do not scan IL2CPP metadata at the same time.
- **Hooking Strategy**: Uses Dobby to hook `eglSwapBuffers` for frame-by-frame UI injection, `UnityEngine.Input.GetTouch` for touch-to-mouse forwarding, and selected game methods for Combat visibility and Arena behavior.
- **Runtime Ticks**: Arena effects and Shop automation run on separate 100 ms ticks. Combat power and Auto-Play run on separate 250 ms ticks. GGC Info, opponent prediction history, and next-enemy HUD text refresh on 500 ms cadences. Frame-time feature work has a small render budget plus a per-frame managed-work unit budget so lower-priority ticks, hot loops, and Test diagnostics defer instead of stacking heavy IL2CPP/Unity/game calls into one render pass. Auto-Play builds a shared gold-interest plan and uses bounded cooldowns for opt-in safe-phase built-in AI startup, long-gated AI refresh, built-in deployment, separate smart formation moves, level-up actions, and auction bidding. Shop automation uses bounded cooldowns for buy, repeat-buy, refresh, target-worth, and Recommendation Lineup checks, and waits for the shop panel to be operable before UI actions.
- **Runtime Caches**: Managed references are cached through atomic pointers. Hero table rows filtered for commanders and known placeholder names, equipment table data, GogoCard table data, and Recommendation Lineup hero IDs are collected locally and published under `RuntimeMutex::FeatureMutex` when entering a new match. GitHub release metadata is cached in memory under `RuntimeMutex::UpdateMutex` for the session.
- **Pinned Managed References**: Persistent managed-object caches are published
  only after `il2cpp_gchandle_new(obj, true)` succeeds. The pinned handle set is
  match-scoped: refreshed objects add handles without freeing old ones, and all
  accumulated handles are released only when the match ends.
- **Field Access**: Typed regular instance field reads and non-pointer writes
  use resolved `il2cpp_field_get_offset` values for direct bounded copies on
  hot paths. Raw IL2CPP get/set fallbacks remain for unresolved offsets, static
  fields stay on static IL2CPP APIs, and managed-object pointer writes should
  preserve IL2CPP write barriers.
- **Diagnostics**: The Test tab houses Runtime Status plus binding readiness, update-check status, Auto-Play readiness, Recommendation Lineup readiness, managed reference refresh, Battle Power readiness, round state, Arena round-manager readiness, achievement readiness, Unity timeScale readiness, player economy/rank/shop state, grouped shop diagnostic reader readiness, battle manager fields, battle bridge state, shop panel state, behavior API state, all manager entries, and opponent prediction signals. Shop diagnostics become ready when any core shop diagnostic reader resolves, while individual rows keep their own `Waiting` states. In the prediction table, `Will fight` is local-player opponent probability; `Current enemy` is the observed opponent for that row; `Recent` comes from the per-player opponent history, while the seven-round cycle-pattern signal is folded into the weighted prediction.
- **Configuration**: Settings saves and loads visual, menu language, window, HUD, Auto-Play, Combat, Shop, and Arena controls from `/data/data/<game-package>/files/mcgg_config.ini`.
- **Updates / Changelog**: Settings includes an informational GitHub Releases
  checker. It uses embedded `MCGG_BUILD_REPOSITORY`, `MCGG_BUILD_VERSION`,
  `MCGG_BUILD_COMMIT`, and `MCGG_BUILD_REF` metadata, queries public releases
  through libcurl on a detached worker, filters draft/prerelease entries,
  compares the latest compatible release against the local version or target
  commit, and shows status, release date, summary, last check time, refresh
  control, and scrollable release notes. It sends no gameplay/account/device
  data and must not download, deploy, inject, bypass, or force updates.
- **curl Build**: `jni/curl`, `jni/libpsl`, and `jni/openssl` are pinned
  submodules built by `jni/build-curl-android.sh` into `obj/curl-install/`,
  `obj/libpsl-install/`, and `obj/openssl-install/`. The libpsl submodule is
  pinned to upstream release `0.21.5` from
  `https://github.com/rockdaboot/libpsl/releases/tag/0.21.5`.
  `jni/Android.mk` links `libcurl.a`, `libpsl.a`, `libssl.a`, and `libcrypto.a`
  as prebuilt static libraries before the main module build.
- **CI Releases**: `.github/workflows/build.yml` creates UTC date-based release tags before build, embeds that version into the native library, packages `libs/` with `BUILD_INFO.txt`, and generates release notes from commit subjects and body text in the push range or release-tag fallback.
- **Memory Mapping**: `jni/structures/Structures.hpp` defines the layout of Unity/Mono types to allow native interaction with managed objects. Function-level comments document the shared layout helpers so offset and value-type reviews do not rely on names alone.
- **Reference**: `dump/dump.cs` serves as the source of truth for the target game's internal C# structure.

### Game Context From External Research

- Current external research was checked on 2026-05-19 using the Google Play
  listing, official website, official YouTube channel, and gameplay/guide
  material.
- Google Play currently identifies the title as a Vizta Games strategy
  auto-chess game, showed 10M+ downloads, a May 9, 2026 store update, and S6
  Dawnlight Celebration events in the checked web region, and links to the
  official site and YouTube channel. Treat those figures and event labels as
  product context because store metadata can vary by region/cache.
- MOONTON's public Season 5 news documents Go Go Plaza, GOGO MOBA, Golden Month
  content, Neobeasts/Exorcist/Mystic Meow/Heartbond synergies, GO1 esports
  momentum, and a 30M-download milestone after global launch. Google Play's
  May 19, 2026 checked "What's new" context mentions Commander Ruby, Gold Rush
  mode, City Hero draw, and the Neolight Wheel event.
- Public sources frame MCGG as an 8-player auto-battler built around recruiting,
  merging, deploying, and repositioning MLBB-inspired heroes while managing
  gold, level, population, HP, equipment, synergies, Commander skills, Go Go
  Cards, auctions, and round supplies.
- Public positioning/scouting guides and video posts reinforce that opponent
  board reads matter, while older Magic Chess pairing discussion points to a
  rotating opponent order that deprioritizes recent opponents until a cycle
  advances. Use that only as a heuristic beneath live runtime pair observations
  and dump-backed invader reads.
- The sibling `../MCGG_Predictor` app contributes a bounded seven-round
  completed-history model: unknown pattern can tentatively predict R4 from local
  R1, classic pattern repeats local R1 at R4 and uses local R3 at R5, and shifted
  pattern predicts R5/R6/R7 from the local R1 opponent's R4/R2/R3 matchups. Keep
  that signal below exact current-opponent, reverse-pair, and invader-order
  evidence.
- Google Play and third-party store mirrors can disagree on exact update dates
  by region/cache. Treat store metadata as product context, not stable binding
  assumptions.
- Use videos and guides to understand UI flows and player vocabulary, but keep
  implementation anchored to `dump/dump.cs`, runtime diagnostics, bounded
  managed reads, and `Waiting for ...` states.

### Current Feature Areas
- **Info**: player/enemy table and automatic GGC quality readout for every detected GGC round.
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
  control Arena SpeedHack. Built-in deploy and smart formation use separate
  cooldown clocks. Direct `StartAI` coordination is opt-in, skipped during
  fight/fight-result/monster phases, and may be refreshed only on a long
  interval to recover if the game drops its internal AI state.
- **Appearance**: ImGui Dark, Catppuccin Mocha, Dear ImGui issue #707-inspired theme selection, Default/Noto Sans CJK font selection, English/Indonesian menu language selection, and localized tooltips for interactive overlay controls.
- **Settings**: menu size, fixed position, mobile-friendly TabBar helpers, next-enemy HUD text, font scale, style tuning, GitHub release update/changelog status, and save/load configuration, including language and Auto-Play state.
- **Shop**: auto-buy free heroes, auto-buy selected targets, auto-buy all detected Recommendation Lineup heroes with per-hero target counts, Scavenger expensive-hero forcing after automatic regular-shop refreshes, auto-refresh, pause-refresh conditions, keep-gold threshold, manual target counts, and shop-panel operability gates before buy/refresh UI actions.
- **Arena**: hero spawn, equipment grant, GogoCard forcing, Battle Power controls, active synergy forcing, level/population 99, outside-map placement, enemy HP 1, achievement task forcing, manual gold grant, fight/result-aware Skip Round, and SpeedHack with reset-to-normal handling.
- **Test**: Runtime Status, manual binding retry, account inspection, fight prediction, binding, round, player, manager, bridge, shop UI, behavior API, and all-manager diagnostics. Only the exact local current opponent should be locked to `100%` in `Will fight`; every player's enemy history and dump-backed invader order should remain available for weighted predictions.

### Project Constraints
- **Target ABI**: `arm64-v8a`
- **Android Platform**: `android-21`
- **STL**: `c++_static`
- **Unity Version**: `2019.4.33f1` (Refer to `UNITY_` macros in `jni/Android.mk`)
- **C++ Standard**: C++26 (defined in `jni/Android.mk`)
- **NDK App Settings**: `APP_OPTIM := release`, `APP_THIN_ARCHIVE := false`, `APP_PIE := true`
- **NDK App Flags**: `-fstack-protector-strong`, `_FORTIFY_SOURCE=2`,
  `-fno-strict-aliasing`, `-fno-strict-overflow`,
  `-fno-delete-null-pointer-checks`, `-funwind-tables`,
  `-fvisibility-inlines-hidden`, `-Wl,-z,relro`, `-Wl,-z,now`, and
  `-Wl,--as-needed`
- **Native C Flags**: `-Oz` and `-DNDEBUG` by default; `NDK_DEBUG=1` adds `-O0`
- **Build Metadata Defines**: `MCGG_BUILD_REPOSITORY`, `MCGG_BUILD_VERSION`,
  `MCGG_BUILD_COMMIT`, and `MCGG_BUILD_REF` are embedded by `jni/Android.mk`.
  Local builds use Git-derived fallbacks when available; CI overrides them with
  the generated release metadata.
- **curl Static Library**: pinned curl submodule at `jni/curl`, pinned libpsl
  `0.21.5` submodule at `jni/libpsl`, pinned OpenSSL `4.0.0` submodule at
  `jni/openssl`,
  generated archives at `obj/curl-install/lib/libcurl.a`,
  `obj/libpsl-install/lib/libpsl.a`, `obj/openssl-install/lib/libssl.a`, and
  `obj/openssl-install/lib/libcrypto.a`. Curl is configured with OpenSSL TLS,
  pinned libpsl `0.21.5` support, and without curl feature-disabling flags.

### Runtime Audit Focus

- The render hook is installed before `liblogic.so` and IL2CPP are ready. Keep managed calls behind `IsIl2CppRuntimeReady()` and render-thread attach checks.
- Keep startup sleeps in the detached setup thread, not the constructor, so the
  loader thread is not blocked before Unity continues startup.
- Runtime method matching is dump-guided but still name, parameter-count, and parameter-name-shape based. Treat overload-sensitive bindings as unsafe until checked in `dump/dump.cs`.
- Cached managed objects such as `MCBattleBridge`, `UIPanelBattleHeroShop`, the
  shop item list, and `LoadRes` should stay behind pinned GC handles. Never free
  those handles on ordinary reference refresh, feature reset, or object
  replacement; release them only from the match-ended cleanup path.
- Regular instance field helper work should keep offset-based direct access
  limited to validated field offsets and retain IL2CPP fallback behavior for
  unresolved metadata or barrier-sensitive writes.
- Table cache loading publishes only after hero rows are filtered for
  commanders and known placeholder names, and equipment and GogoCard data are
  all present. Dependent UI should keep `Waiting for ...` states while any table
  is missing.
- Table cache loading should be deferred until a table-backed tab or active
  automation needs it, and long Shop/Arena table views should render visible
  rows with `ImGuiListClipper` rather than walking every row each frame.
- Auto-Play owns selected Shop, Arena, and Combat assist settings plus Recommendation Lineup target defaults through a backup while it is active; preserve capture/restore semantics.
- Opponent prediction may display exact data and weighted guesses together. Only the exact local current opponent should be forced to `100%`.
- Opponent prediction rows should be built on the throttled 500 ms feature tick,
  not inside the ImGui draw path. Weight live current-opponent data first, then
  invader order, recent-cycle learning, the completed-history seven-round
  cycle-pattern signal, cycle-gap distance, round-robin fallback, and history.
- GGC Info should keep `ILOGIC_GetCrystalQualityByRound(UInt64, Int32)`
  dump-verified, scan only the bounded configured round range, and refresh on
  its 500 ms cadence rather than every render frame.
- SpeedHack changes global Unity time scale and must reset to `1.0x` when disabled, when battle state is unavailable, or when feature state resets.
- The update checker is informational only. Keep it detached/asynchronous,
  cached under `RuntimeMutex::UpdateMutex`, throttled to the 6-hour refresh and
  bounded retry backoff, and free of gameplay/account/device/private data
  collection or automatic download/deployment behavior.
- Repository-wide documentation work should update the top-level Markdown files only: `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `README.md`, and `README.id.md`. Leave `goal.md` and submodule Markdown untouched.

### Shared State Discipline

- `RuntimeMutex::CacheMutex` protects IL2CPP method and field caches.
- `RuntimeMutex::ManagedHandleMutex` protects the pinned managed-object handle
  registry for cached references.
- `RuntimeMutex::FeatureMutex` protects complex feature collections, including `FeatureState::Heroes`, `FeatureState::Equips`, `FeatureState::Cards`, `FeatureState::ShopSelectedHeroes`, Recommendation Lineup target counts, and cached Recommendation Lineup hero IDs.
- `RuntimeMutex::UpdateMutex` protects GitHub release update/check metadata and
  cached changelog entries.
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
- **Field helpers**: Prefer the shared typed `GetField`/`SetField` helpers for
  regular instance fields so hot paths use offset access automatically. Use raw
  IL2CPP/static helpers for static fields or cases that need runtime-managed
  setter behavior.
- **HUD diagnostics**: Keep the next-enemy HUD as lightweight foreground text near the bottom center. Reuse throttled prediction/current-opponent data and avoid rebuilding prediction tables every render frame.
- **Function comments**: Keep the current function-level comment coverage for
  `jni/Main.cpp` and `jni/structures/Structures.hpp`. Comments should explain a
  function's contract, safety boundary, or layout meaning, not narrate obvious
  line-by-line control flow.
- **Overlay i18n**: Route new user-facing menu labels through the native
  `MenuI18nEntry` table and localized wrappers. Add English and Indonesian copy
  plus a tooltip for each interactive tab, button, checkbox, combo, slider,
  input, or table-row control, and preserve hidden ImGui ID suffixes such as
  `##id`. Dynamic runtime diagnostics can remain English when they are not
  stable menu copy.
- **Test diagnostics**: Keep Test additions read-only unless explicitly requested otherwise, and verify class names, method names, parameter counts, return types, and fields against `dump/dump.cs`.
- **Mobile menu**: Keep mobile accessibility helpers compatible with the main ImGui TabBar. Helper controls may select tabs, but the TabBar should remain visible.
- **Shop automation**: Preserve the single-threaded, throttled frame-tick model. Use existing atomic toggles/counters and selected-target snapshot helpers. Scavenger expensive-hero forcing may run immediately from `MCBattleBridge.OnRefreshShop` only for automatic regular-shop refreshes; keep it bounded to five shop slots, require active count 2+, and respect affordability plus keep-gold. Wait for non-delayed, non-spectate, operable shop panel state before buy or refresh UI calls. Avoid unbounded scans, immediate retry loops, or holding locks across managed calls in the hot path unless a future design explicitly requires them.
- **Auto-Play automation**: Preserve the 250 ms tick and bounded cooldowns. Use
  `ReadAutoPlaySnapshot()`, `BuildAutoPlayGoldPlan()`,
  `CollectAutoPlayBoardUnits()`, `BuildAutoPlayBoardPlan()`, and the existing
  table/target helpers. Keep opponent scans bounded to the battle manager
  dictionary limit, keep board placement to one move per cooldown, keep shop,
  auction, and level-up decisions on the shared gold plan, keep built-in AI
  startup opt-in, safe-phase, and stateful with only
  a long-gated refresh, keep built-in deploy and smart formation cooldowns
  separate, keep SpeedHack as a manual Arena-only control, and never hold
  `FeatureMutex` while calling managed IL2CPP APIs.
- **Freeze stability**: Keep frame-time managed work behind the feature frame
  budget, defer noncritical ticks when the budget is exhausted, avoid eager table
  cache loads on unrelated tabs, budget-gate Auto-Play action groups after
  planning, and clip long table views.
- **Auto-Play signatures**: Verify `MCLogicBattleManager.StartAI`,
  `TryAutoDeploy`, `OnPlayerLvlUp`, `GetLineupWorth`,
  `CalcCurrentFightValue`, `MoveHeroInBattleField(UInt32, Byte, Byte,
  Boolean)`, `LogicRoundMgr.get_m_AuctionComp`,
  `MCLogicAuctionComp.Bid(MCLogicAuctionSlotInfo, UInt64, UInt32)`, and
  `MCLogicGoGoCardComp.get_m_CurrData` against `dump/dump.cs` before changing
  the controller.
- **Arena round control**: Preserve the separate Arena tick and bounded Skip Round cooldown. Verify `MCLogicBattleData.get_logicRoundMgr`, `LogicRoundMgr.SetRound(UInt32)`, and `LogicRoundMgr.NextRound(Boolean)` against `dump/dump.cs`. Automatic skip should wait out fight/result phases and avoid repeating the same source/target request.
- **Arena SpeedHack**: Use `UnityEngine.Time.set_timeScale(Single)` and reset time scale to normal when the feature is disabled, leaves active battle state, or feature state is reset.
- **Arena achievements**: Verify `MCLogicAchievementRecordComp.AchievementDataBase.GetResult`, `canRecordAchievementData`, `JudgeFinalRelation`, `JudgeReachCondition(List<MCLogicPlayer>)`, `MCLogicAchievementRecordComp.AchievementRoundData.GetResult`, `AchievementRoundData.RefreshData`, and `m_roundAchievementCount`/`m_roundSuccessCount` against `dump/dump.cs` before changing achievement hooks or round-counter writes.
- **Comments**: Preserve concise comments before project-owned function
  definitions and add extra notes only for risky IL2CPP calls, hook signatures,
  value-type layouts, or timing-sensitive blocks.
- **Commit Messages**: Follow the [Conventional Commits](https://www.conventionalcommits.org/) specification. Use short typed messages such as `feat(autoplay): add gold interest planning`, `fix(ui): resolve main menu tab switching`, or `docs: update runtime audit guidance`. Accepted types: `feat`, `fix`, `perf`, `refactor`, `docs`, `build`, `ci`, `chore`, `revert`, `test`. Common scopes: `main`, `ui`, `shop`, `arena`, `autoplay`, `hud`, `appearance`, `test`, `readme`. Release notes are generated from commit subjects and body text, so include enough context for commits to stand alone.
- **Scope**: Do not modify vendored directories (`jni/imgui/`, `jni/xDL/`, `jni/dobby/`, `jni/Il2CppVersions/`, `jni/curl/`, `jni/libpsl/`, `jni/openssl/`) unless explicitly requested.
- **Appearance**: Keep theme/font changes in the existing appearance setup and preserve fallback to the default ImGui font when Noto Sans CJK is unavailable. When adding themes, keep `kAppearanceThemes` and `Issue707ThemePalette` entries aligned and preserve Catppuccin Mocha at theme index `1` for existing configs.
- **Settings**: Keep persistence in the project config file under the game app data directory; do not re-enable ImGui `.ini` persistence unless explicitly requested.
- **Update checker**: Keep GitHub release checks on a detached worker or an
  equally non-blocking path. Preserve in-memory caching, retry/backoff, explicit
  failure states, scrollable changelog rendering, privacy guarantees, and
  informational-only behavior.
