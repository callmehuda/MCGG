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
- `dump/dump.cs` is Git LFS-managed. A dump refresh can show only a pointer
  object ID and file-size change in Git, so inspect the full local artifact and
  compare against the previous dump snapshot when one is available. Do not
  commit old dump backup files unless explicitly requested.
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
- **Info**: player/enemy table, `(Bot)` labels from `SystemData.RoomData.bRobot`, and automatic GGC quality readout for every detected GGC round.
- **Combat**: Invisible Scout.
- **Appearance**: ImGui Dark, Catppuccin Mocha, Dear ImGui issue #707-inspired theme selection, Default/Noto Sans CJK font selection, English/Indonesian menu language selection, and localized tooltips for interactive overlay controls.
- **Settings**: menu size, fixed position, mobile-friendly TabBar helpers, next-enemy HUD text, font scale, style tuning, GitHub release update/changelog status, and save/load configuration, including language state.
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

- Keep early-render paths guarded by IL2CPP readiness and render-thread attach
  checks.
- Keep retryable method and field lookups backed off instead of permanently
  failed.
- Pin persistent managed-object references with match-scoped GC handles and
  release the accumulated handles only after the match ends.
- Keep Info bot labeling sourced from `SystemData.RoomData.bRobot` through
  `ILOGIC_GetStPlayerData(UInt64)` and tolerate missing binding or field
  metadata.
- Keep Shop automation single-threaded, bounded to visible shop slots, and
  gated on shop panel operability before buy or refresh actions.
- Keep table cache loading demand-driven and long tables clipped.
- Keep opponent prediction exactness narrow: only the local player's exact
  current opponent is forced to `100%`.
- Reset Unity time scale to `1.0x` on every SpeedHack disable, inactive-battle,
  and feature-reset path.
- Keep the update checker informational only and free of gameplay, account,
  device, credential, or private runtime data.

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
