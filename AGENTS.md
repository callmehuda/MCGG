# Repository Guidelines

## Project Structure & Module Organization

This repository is a native Android modding project for Magic Chess Go Go.
Primary native code lives in `jni/Main.cpp`. Build settings are in
`jni/Android.mk` and `jni/Application.mk`. Shared Unity, IL2CPP, Mono, delegate,
event, collection, and string layouts are kept in
`jni/structures/Structures.hpp`.

`jni/Main.cpp` contains the process gate, setup thread, IL2CPP helpers, feature
binding resolver, runtime caches, GitHub release update checker, Dobby hooks,
ImGui rendering, and feature logic. It also owns the Info, Combat, Auto-Play,
Shop, Arena, Appearance, Settings, and Test overlay tabs. Keep feature work in
this file unless the user explicitly requests a multi-file refactor.

The current boot order is process gate, detached setup thread, early
`eglSwapBuffers` hook, `liblogic.so` wait, IL2CPP export resolution, setup
thread attach, `UnityEngine.Input.GetTouch` hook, and lazy render-thread ImGui
initialization. The render hook can exist before IL2CPP is ready, so any
frame-time managed work must stay behind readiness and thread-attach checks.
Startup waits belong in the detached setup thread, not the library constructor,
so process loading is not blocked by hook delays.

`dump/dump.cs` is the IL2CPP signature reference. Use it before changing native
method pointers, hook signatures, value-type layouts, or field offsets. The
current dump refresh was checked locally on 2026-05-20 and is 605,385 lines.
Because `dump/**` is Git LFS-managed, tracked diffs can show only pointer object
ID and file-size changes; inspect the full local artifact and compare against a
previous dump snapshot when one is available. Treat dump addresses and RVAs as
per-build diagnostics, not binding contracts, and do not commit old dump backup
files unless explicitly requested.
Vendored or external components live under `jni/dobby/`, `jni/imgui/`,
`jni/xDL/`, `jni/curl/`, `jni/libpsl/`, `jni/openssl/`, and
`jni/Il2CppVersions/`. The project-owned `jni/build-curl-android.sh` script
builds OpenSSL, pinned libpsl `0.21.5`, and curl into `obj/openssl-install/`,
`obj/libpsl-install/`, and `obj/curl-install/` before `ndk-build`. Build output
is written to `libs/` and `obj/`; do not treat these as source modules.

## Game Context & External Research

When a task asks for broader Magic Chess Go Go analysis, include internet or
video-context research, then translate it into durable engineering notes instead
of volatile meta instructions. Current public context checked on 2026-05-19:

- Google Play identifies Magic Chess: Go Go as a Vizta Games auto-chess,
  multiplayer strategy game, showed 10M+ downloads, a May 9, 2026 update, and
  S6 Dawnlight Celebration events in the checked web region, and points to
  `magicchessgogo.com` and the official YouTube channel. Store counts, ratings,
  events, and update dates can vary by region/cache, so use them as product
  context rather than native binding facts.
- The official site describes the game around 8-player battles, hero
  recruitment/upgrades, Commander skills, Go Go Cards, roles, synergies, and
  classic/ranked/custom play.
- Public video and guide material is useful for observing UI flow, economy,
  shop refreshes, board placement, Commander/Card choices, and common meta
  vocabulary, but seasonal lineups and tier claims are not stable native facts.
- MOONTON's public news around Season 5 documented Go Go Plaza, GOGO MOBA,
  Golden Month content, Neobeasts/Exorcist/Mystic Meow/Heartbond synergies, and
  GO1 esports momentum. Google Play's May 19, 2026 checked "What's new" context
  mentions Commander Ruby, Gold Rush mode, City Hero draw, and the Neolight
  Wheel event. Treat these as live product context and vocabulary, not native
  binding contracts.
- Public positioning and scouting guides emphasize adapting to the opponents'
  boards, while older Magic Chess pairing discussion describes a turn-like
  opponent order where recent opponents are deprioritized until the cycle
  advances. Treat that as a prediction heuristic only; live runtime pair data,
  invader-order reads, recent-cycle distance, and local opponent history remain
  stronger evidence.
- The sibling `../MCGG_Predictor` app models two seven-round pairing patterns
  from completed history: unknown pattern can tentatively predict R4 from local
  R1, classic pattern repeats local R1 at R4 and uses local R3 at R5, and shifted
  pattern derives R5/R6/R7 from the local R1 opponent's R4/R2/R3 matchups. Treat
  this as a bounded runtime-history signal below exact pair and invader-order
  reads, not as a native binding contract.

Engineering takeaway: prefer documenting stable runtime surfaces such as battle
managers, player economy, shop panel state, round manager state, Commander and
Go Go Card data, auction state, synergies, board units, and opponent pairing.
Do not hard-code public meta claims into native feature logic without
`dump/dump.cs` and live runtime verification.

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
./jni/build-curl-android.sh
```

Builds the pinned OpenSSL `4.0.0`, libpsl `0.21.5`, and curl submodules as
static Android libraries at `obj/openssl-install/`,
`obj/libpsl-install/lib/libpsl.a`, and `obj/curl-install/lib/libcurl.a`. The
libpsl submodule should track
`https://github.com/rockdaboot/libpsl/releases/tag/0.21.5`. Run this before
`ndk-build` after a fresh clone, after cleaning `obj/`, or after changing the
curl, libpsl, or OpenSSL submodules.

```sh
ndk-build -C jni
```

Builds the native `main` module and outputs `libs/arm64-v8a/libmain.so`.
The current build uses ABI `arm64-v8a`, platform `android-21`, STL
`c++_static`, optimization `release`, thin archives disabled, PIE enabled, and
C++ mode `c++26`. `jni/Android.mk` optimizes native sources with `-Oz` by
default and adds `-O0` when `NDK_DEBUG=1`. `jni/Application.mk` adds app-wide
hardening and stability flags: stack protector, `_FORTIFY_SOURCE=2`,
no-strict-aliasing/overflow assumptions, preserved null checks, unwind tables,
hidden inline visibility, RELRO, immediate binding, and `--as-needed`.

The curl build script configures curl with the pinned OpenSSL `4.0.0` TLS
backend and pinned libpsl `0.21.5` support, and should not pass curl
feature-disabling flags.

`.github/workflows/build.yml` is the CI release workflow. It installs the curl
and libpsl autotools prerequisites, builds the static OpenSSL, pinned libpsl
`0.21.5`, and curl archives, prepares UTC date-based release metadata before
compilation, passes the generated `MCGG_BUILD_*` metadata constants into
`ndk-build`, builds with Android NDK `29.0.14206865`, packages the generated
`libs/` output with `BUILD_INFO.txt`, uploads the zip as a workflow artifact,
and publishes or updates GitHub releases whose notes include commit descriptions
from Git history.

## Coding Style & Naming Conventions

Follow the existing C++ style in `jni/Main.cpp`: 4-space indentation, concise
helper functions, explicit pointer types, and short comments placed directly
above functions or complex blocks. Current project-owned native function
definitions in `jni/Main.cpp` and `jni/structures/Structures.hpp` carry a short
contract comment; preserve that coverage when adding or changing functions.
Keep Unity compatibility macros named with the `UNITY_` prefix. Prefer `void*`
for managed object instances unless a local structure layout is required.

Keep runtime sections clear and local: IL2CPP resolution, managed reference
refresh, table caches, appearance setup, Settings persistence, feature ticks,
hooks, test diagnostics, and ImGui tabs should remain easy to scan. Keep
function comments focused on contract, safety boundary, or layout meaning, and
avoid line-by-line narration of obvious control flow.

Route user-facing overlay menu labels through the native i18n table in
`jni/Main.cpp`. New interactive menu controls must provide English and
Indonesian labels, preserve hidden ImGui ID suffixes such as `##id`, and expose
a localized tooltip through the existing menu wrappers. Dynamic runtime values
and dump-derived diagnostics may fall back to English when stable translation
keys would add noise.

Do not convert retryable lookups into one-shot failures. Method and field
resolution can happen before the target metadata is ready, so missing entries
must be allowed to resolve later. Field misses may be cached only with a short
retry backoff so hot feature paths do not rescan metadata every frame. Method
resolution is name, parameter-count, and parameter-name-shape based; verify
overload-sensitive changes against `dump/dump.cs`. Preserve the separate 100 ms
shop and arena ticks, 250 ms Combat and Auto-Play ticks, and 500 ms GGC Info,
opponent history, and HUD cadences unless the task explicitly changes timing.
Frame-time feature work is guarded by a small render budget; when a frame is
already busy, lower-priority ticks should defer to the next frame instead of
stacking managed calls into one render pass. Hot loops and Test diagnostics
also consume a per-frame managed-work unit budget so IL2CPP, Unity, and game
function/field reads defer without changing the existing tick delays.

Typed regular instance field helpers should prefer resolved
`il2cpp_field_get_offset` access and bounded direct copies for hot reads and
non-pointer writes. Keep IL2CPP raw get/set fallbacks available, leave static
fields on the IL2CPP static APIs, and avoid bypassing IL2CPP write barriers for
managed-object pointer writes.

Persistent managed-object references must be pinned with
`il2cpp_gchandle_new(obj, true)` before they are published into cached runtime
state. Keep the handle registry match-scoped: if a cached object changes during
the match, pin the new object but keep the old handle alive, then release all
accumulated handles only after the match has ended.

Shared state is split across `RuntimeMutex::CacheMutex`,
`RuntimeMutex::ManagedHandleMutex`, `RuntimeMutex::FeatureMutex`,
`RuntimeMutex::UpdateMutex`, and `RuntimeMutex::UiMutex`. Primitive feature
flags, counters, managed reference pointers, and published GC handle IDs use
`std::atomic`. Guard direct access to complex feature collections such as
`FeatureState::Heroes`, `FeatureState::Equips`, `FeatureState::Cards`, and
`FeatureState::ShopSelectedHeroes` with `FeatureMutex` or the existing
snapshot/access helpers. Guard cached GitHub release metadata with
`UpdateMutex`. Do not hold `FeatureMutex` while calling managed IL2CPP APIs;
gather local data first and publish results under the lock.

The update checker is informational only. It embeds local build metadata through
`MCGG_BUILD_REPOSITORY`, `MCGG_BUILD_VERSION`, `MCGG_BUILD_COMMIT`, and
`MCGG_BUILD_REF`, queries public GitHub Releases with libcurl on a detached
worker, filters draft/prerelease entries, caches release notes in memory, and
uses a 6-hour refresh cadence with 5-to-60-minute retry backoff. It must not
send gameplay state, account data, device identifiers, credentials, or private
runtime data, and must not add automatic download, deployment, injection,
forced update, bypass, or evasion behavior.

Shop automation is intentionally single-threaded and throttled on the frame
tick. Preserve the existing buy, repeat-buy, refresh, target-worth, and
Recommendation Lineup cooldowns. Scavenger expensive-hero forcing is the
exception that may run immediately from an automatic regular-shop refresh hook;
it must still stay bounded to the five visible shop slots, require the
Scavenger/Shadow Mercenary active count to be 2 or higher, respect affordability
and keep-gold settings, and defer if the shop panel is not operable. Read
selected target state through
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
dictionary limit, keep built-in deploy and smart formation on separate cooldowns,
keep battlefield movement to one chosen action per formation cooldown,
keep shop, auction, and level-up decisions aligned
with the shared gold plan, keep built-in AI startup opt-in, stateful, and limited
to safe non-fight/non-result phases instead of replaying `StartAI` on every tick,
allow only a long-gated `StartAI` refresh to recover from dropped internal AI
state, keep SpeedHack as an explicit Arena-only control, and do not hold
`FeatureMutex` while calling managed IL2CPP APIs. Managed Auto-Play action groups
after planning should keep checking the frame and managed-work budgets so card,
auction, AI, formation, and level-up work do not stack into one overloaded
render pass.

Large table-backed UI surfaces such as Shop hero targets and Arena hero, item,
and GogoCard lists should render through clipping or another visible-row pattern
so scrolling a loaded table does not process every row every frame. Table cache
loading should run only when a table-backed tab or active automation actually
needs the data.

Opponent prediction should not rebuild managed prediction rows from the draw
path. Reuse the 500 ms prediction tick, keep live current-opponent observations
highest priority, then combine invader order, recent opponent cycle learning,
the completed-history seven-round cycle-pattern signal, cycle-gap distance,
round-robin fallback, and per-player history as weaker signals.

Auto-Play temporarily owns selected Shop, Arena, and Combat assists through its
policy backup while enabled. If a change touches those assist toggles or the
Recommendation Lineup target defaults, preserve the capture/restore behavior and
make it clear in user-facing docs when manual edits made during Auto-Play can be
restored to the pre-Auto-Play values.

## Testing Guidelines

There is no dedicated unit test framework in this repository. For native changes,
the required verification is a successful:

```sh
./jni/build-curl-android.sh
ndk-build -C jni
```

When changing IL2CPP calls, verify signatures against `dump/dump.cs` and confirm
the target remains `arm64-v8a`, Unity `2019.4.33f1`, and native C++ mode
`c++26`. For regular instance field helper changes, verify
`il2cpp_field_get_offset` availability for the target Unity API and preserve
the raw IL2CPP fallback path for unresolved, static, or barrier-sensitive
fields.

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
The 2026-05-20 dump refresh still exposes these signatures with the same
parameter shapes; re-check them again before changing native function pointer
types because future dumps can drift independently of public game context.

When changing Arena Skip Round automation, verify
`MCLogicBattleData.get_logicRoundMgr`, `LogicRoundMgr.SetRound(UInt32)`, and
`LogicRoundMgr.NextRound(Boolean)` against `dump/dump.cs`. Keep the overlay in
a `Waiting for ...` state while round-manager bindings are unavailable. Automatic
skip requests should avoid fight/result phases and suppress repeated requests
for the same source round and target round.

When changing Arena SpeedHack, verify `UnityEngine.Time.set_timeScale(Single)`
against `dump/dump.cs` and reset the time scale when disabling the feature or
resetting feature state.

When changing Arena Force Complete Achievements Task, verify
`MCLogicAchievementRecordComp.AchievementDataBase.GetResult`,
`canRecordAchievementData`, `JudgeFinalRelation`,
`JudgeReachCondition(List<MCLogicPlayer>)`,
`MCLogicAchievementRecordComp.AchievementRoundData.GetResult`,
`AchievementRoundData.RefreshData`, and
`m_roundAchievementCount`/`m_roundSuccessCount` against `dump/dump.cs`. Keep
the overlay in a `Waiting for ...` state while achievement bindings are
unavailable.

When changing GGC Info, verify
`MCLogicBattleData.ILOGIC_GetCrystalQualityByRound(UInt64, Int32)` against
`dump/dump.cs`, keep the round scan bounded, and keep the readout on its
throttled refresh cadence.

For documentation-only changes, at minimum inspect the rendered Markdown diff.
For native or mixed changes, also run:

```sh
git diff --check
```

For repository-wide documentation refreshes, update only top-level Markdown
files unless explicitly asked to edit submodules. Leave `goal.md`, generated
outputs, and vendored Markdown under `jni/Il2CppVersions/`, `jni/imgui/`,
`jni/xDL/`, `jni/curl/`, `jni/libpsl/`, and `jni/openssl/` untouched.

## Commit & Pull Request Guidelines

All commit messages follow the [Conventional Commits](https://www.conventionalcommits.org/) specification. Use short typed commit messages:

```text
feat: add enemy predictor and project documentation
docs: update contribution guide
fix(autoplay): add freeze safeguards
perf(shop): throttle automation actions
refactor: stabilize MCGG feature runtime
```

Keep commit messages direct and focused on the change. Release notes are
generated from commit subjects and body text, so include enough context in
commits for them to stand alone in automated releases.

Pull requests should include a clear description, the build command result,
affected files, and any relevant notes about IL2CPP signatures or runtime hooks.
Link issues when applicable. Screenshots are useful only for visible ImGui
overlay changes.

## Agent-Specific Instructions

Keep changes scoped. Do not modify vendored directories such as
`jni/Il2CppVersions/`, `jni/imgui/`, or `jni/xDL/` unless explicitly requested.
Do not revert unrelated local changes in the working tree.

Current user-facing feature areas are Info, Combat, Auto-Play, Shop, Arena,
Appearance, Settings, and Test. If a feature binding or update-check state is
missing at runtime, the overlay should show a `Waiting for ...` or explicit
failure state rather than failing silently.
Info includes the player/enemy table and automatic GGC quality readout for every
detected GGC round.
Auto-Play includes adaptive strategy pressure, opt-in built-in AI coordination,
opponent-aware board analysis, advanced role-aware formation moves, selected
shop target promotion, GogoCard scoring, auction scoring, gold-interest economy
decisions, and optional coordination of Combat and Arena assists. Shop currently
includes free-hero buying, selected target buying, Recommendation Lineup buying
with per-hero target counts, Scavenger expensive-hero forcing, auto-refresh pause
conditions, keep-gold reserve, and target counts. Combat includes Invisible Scout. Arena includes
hero/item/card granting, Battle Power controls
for force-win, HP-loss prevention, attack-ratio boosting, fight-value boosting,
and enemy-board crippling, active synergy forcing, level/population forcing,
enemy HP pressure, achievement task forcing, Skip Round, and SpeedHack. Use
the Test tab's Runtime Status
section and diagnostics when checking binding readiness, managed references,
round state, round-manager state, timeScale binding readiness, achievement
binding readiness, player
economy/rank/shop state, battle manager fields, battle bridge state, shop panel
state, shop diagnostic reader readiness, behavior API state, Recommendation
Lineup state, Auto-Play state, auction state, GogoCard state, board formation
state, or opponent prediction logic. Test
diagnostics should stay read-only unless the task explicitly requests an action.
Shop diagnostics are considered available when at least one core shop diagnostic
reader has resolved; individual Test rows should keep showing `Waiting` for
missing readers.
In the Test prediction table, `Will fight` is the local player's opponent
probability and `Current enemy` is the observed opponent for that row. Only the
exact local current opponent should be forced to `100%`; do not mark every row
as `100%` just because that row has a known current enemy. Preserve per-player
enemy history collection, completed-cycle pattern reads, and dump-backed invader
order reads so prediction weights can account for recent meetings, the
seven-round learned pattern, and the game's pairing list.
Appearance currently includes ImGui Dark, Catppuccin Mocha, and additional
palettes inspired by Dear ImGui issue #707. Keep `kAppearanceThemes` and
`Issue707ThemePalette` entries aligned, and preserve Catppuccin Mocha at theme
index `1` for existing configs. Appearance also owns the menu language selector
and localized menu tooltips; keep Catppuccin and language config compatibility
stable when changing visual settings. Keep mobile accessibility changes
compatible with the main ImGui TabBar. Helper buttons may select tabs, but the
TabBar should remain visible and authoritative.
Settings includes a persisted next-enemy HUD toggle that draws bottom-center
foreground text above the lower screen edge, plus the `Updates / Changelog`
section for release status and cached GitHub release notes. Keep HUD work
lightweight and throttle prediction refreshes instead of rebuilding predictions
every render frame.
Settings config should default to the running game package directory as
`/data/data/<game-package>/files/mcgg_config.ini`.

Known audit hotspots are early-render readiness, dump-backed signature drift,
match-scoped pinned managed-object handles, table cache all-or-nothing
publication with hero rows filtered for valid IDs, commanders, and placeholder
names, shop panel operability before buy or refresh, grouped shop
diagnostic readiness, Auto-Play policy ownership of assist toggles, opt-in
safe-phase built-in AI startup, separate Auto-Play deploy/formation cooldowns,
render-frame budget deferral between Auto-Play action groups, method-miss
backoff, guarded binding resolution, update-check thread/cache/backoff behavior,
clipped long tables, exact-opponent-only `100%` prediction rows,
completed-history seven-round prediction patterns, bounded GGC round scans, and
Unity timeScale reset paths.
