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

- Keep changes scoped to the requested feature area and follow the existing
  large-file organization in `jni/Main.cpp`.
- Verify new IL2CPP methods, fields, hook signatures, and value-type layouts
  against `dump/dump.cs` before changing native function pointer types or field
  reads.
- Preserve retryable binding behavior. Missing metadata should back off and
  resolve later, not become a one-shot failure.
- Keep frame-time managed work behind IL2CPP readiness, render-thread attach,
  and the existing managed-work budget.
- Info player bot labels should read `SystemData.RoomData.bRobot` through
  `ILOGIC_GetStPlayerData(UInt64)` and degrade to ordinary names when runtime
  data is unavailable.

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

- Use 4-space indentation and explicit pointer types.
- Keep helper functions small, local, and named after the runtime contract they
  protect.
- Add short contract comments above project-owned native functions when adding
  or changing function definitions.
- Route user-facing overlay labels through the native i18n table, including
  English and Indonesian text for interactive controls.
- Keep long ImGui table views clipped and avoid per-frame scans of every loaded
  row.

## Runtime Audit Checklist

Use this checklist when looking for hidden bugs or logic flaws:

- Confirm early-frame paths do not call managed APIs before IL2CPP is ready.
- Confirm new method and field bindings are dump-backed and retryable.
- Confirm cached managed references are pinned and released only after the
  active match ends.
- Confirm Info bot labels use `SystemData.RoomData.bRobot` and do not block the
  player list when that optional reader is missing.
- Confirm shop buy and refresh actions still require an operable shop panel.
- Confirm table caches are demand-loaded and published only after required
  hero, equipment, and GogoCard data is available.
- Confirm opponent prediction keeps exact current-opponent evidence above
  heuristics and only locks the local exact opponent to `100%`.
- Confirm SpeedHack reset paths restore Unity time scale to `1.0x`.

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

Scope is optional but recommended for clarity. Common scopes include `main`, `ui`, `shop`, `arena`, `hud`, `appearance`, `test`, and `readme`.

## Pull Requests

Pull requests should include:

- A short summary of the change.
- The files or areas affected.
- The build command result, usually `ndk-build -C jni`.
- Notes about any IL2CPP signatures, fields, or hooks changed.
- Notes about any overlay tab, theme, font, or runtime status behavior changed.
- Screenshots only when the ImGui overlay changes visually.

Small, focused pull requests are easiest to review and merge.
