# Repository Guidelines

## Project Structure & Module Organization

This repository is a native Android modding project for Magic Chess Go Go.
Primary native code lives in `jni/Main.cpp`. Build settings are in
`jni/Android.mk` and `jni/Application.mk`. Shared Unity, IL2CPP, Mono, delegate,
event, collection, and string layouts are kept in
`jni/structures/Structures.hpp`.

`jni/Main.cpp` contains the process gate, setup thread, IL2CPP helpers, feature
binding resolver, runtime caches, Dobby hooks, ImGui rendering, and feature
logic. Keep feature work in this file unless the user explicitly requests a
multi-file refactor.

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

## Coding Style & Naming Conventions

Follow the existing C++ style in `jni/Main.cpp`: 4-space indentation, concise
helper functions, explicit pointer types, and short comments placed directly
above functions or complex blocks. Keep Unity compatibility macros named with
the `UNITY_` prefix. Prefer `void*` for managed object instances unless a local
structure layout is required.

Keep runtime sections clear and local: IL2CPP resolution, managed reference
refresh, table caches, feature ticks, hooks, and ImGui tabs should remain easy
to scan. Add concise comments for risky IL2CPP calls or value-type assumptions,
not for obvious control flow.

Do not convert retryable lookups into one-shot failures. Method and field
resolution can happen before the target metadata is ready, so missing entries
must be allowed to resolve later. Preserve the separate 100 ms shop and arena
feature ticks unless the task explicitly changes timing.

## Testing Guidelines

There is no dedicated unit test framework in this repository. For native changes,
the required verification is a successful:

```sh
ndk-build -C jni
```

When changing IL2CPP calls, verify signatures against `dump/dump.cs` and confirm
the target remains `arm64-v8a` and Unity `2019.4.22f1`.

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

Current user-facing feature areas are Info, Combat, Shop, and Arena. If a
feature binding is missing at runtime, the overlay should show a `Waiting for
...` state rather than failing silently.
