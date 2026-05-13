# MCGG

[![CI Build](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml/badge.svg)](https://github.com/Yan-0001/MCGG/actions/workflows/build.yml)
[![MIT License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
![Android](https://img.shields.io/badge/Android-native-brightgreen)
![ABI](https://img.shields.io/badge/ABI-arm64--v8a-blue)
![Unity](https://img.shields.io/badge/Unity-2019.4.22f1-black)
![NDK](https://img.shields.io/badge/NDK-r29-orange)

Open-source native Android modding project for Magic Chess Go Go.

MCGG is built to stay simple, readable, and easy to understand. It builds an
`arm64-v8a` shared library for a Unity IL2CPP runtime and provides a small
native modding foundation with IL2CPP method lookup, field helpers, Dobby hooks,
xDL symbol lookup, Unity touch forwarding, and a Dear ImGui overlay.

## Responsible Use

This repository is for learning, research, reverse engineering practice, and
authorized local testing. Use it only with devices, accounts, and builds that
you are allowed to inspect or modify.

Before using this project, review and follow the Magic Chess Go Go Terms of
Service:

https://us.skystone.games/mcgg-tos

This repository does not include:

- Game APKs
- Copyrighted game assets
- Paid content
- Bypasses
- Instructions for abusing online services

## Features

- Enemy Predictor overlay logic

## Requirements

- Git
- Git LFS
- Android SDK
- Android NDK r29
- `ndk-build` available in `PATH`
- An `arm64-v8a` Android target environment

The CI workflow uses:

```sh
ANDROID_NDK_VERSION=29.0.14206865
```

## Quick Start

Clone the repository with submodules:

```sh
git clone --recursive https://github.com/Yan-0001/MCGG.git
cd MCGG
```

If the repository was cloned without submodules:

```sh
git submodule update --init --recursive
```

Pull Git LFS files:

```sh
git lfs install
git lfs pull
```

Build from the repository root:

```sh
ndk-build -C jni
```

The main output is:

```text
libs/arm64-v8a/libmain.so
```

## Repository Layout

```text
.github/workflows/            GitHub Actions build workflow
dump/dump.cs                  IL2CPP dump used as a signature reference
jni/Android.mk                Native module build configuration
jni/Application.mk            ABI, platform, STL, and NDK settings
jni/Main.cpp                  Hook setup, IL2CPP helpers, and ImGui overlay
jni/structures/Structures.hpp Unity, Mono, delegate, event, and collection types
jni/dobby/                    Dobby header and arm64 static library
jni/Il2CppVersions/           Unity IL2CPP headers and API declarations
jni/imgui/                    Dear ImGui source
jni/xDL/                      xDL Android dynamic loader utilities
libs/                         Native shared library output
obj/                          NDK intermediate build output
```

## Build Configuration

The native module is defined in `jni/Android.mk`:

```make
LOCAL_MODULE := main
```

The active Android target is configured in `jni/Application.mk`:

```make
APP_ABI := arm64-v8a
APP_PLATFORM := android-21
APP_STL := c++_static
```

Unity compatibility defines are configured in `jni/Android.mk`:

```make
-DUNITY_VERSION_MAJOR=2019
-DUNITY_VERSION_MINOR=4
-DUNITY_VERSION_PATCH=22
-DUNITY_VER=194
```

## Runtime Flow

At load time, `jni/Main.cpp`:

1. Confirms the current process is the Unity target process.
2. Starts a setup thread.
3. Waits for `libil2cpp.so` and `liblogic.so`.
4. Resolves IL2CPP API exports.
5. Attaches to the IL2CPP domain.
6. Resolves required game logic methods.
7. Hooks `eglSwapBuffers`.
8. Hooks `UnityEngine.Input.GetTouch`.
9. Renders the ImGui overlay during frame presentation.
10. Forwards Unity touch input into ImGui mouse input.

## Development Notes

- Keep native changes focused and easy to inspect.
- Check class names, method names, parameters, and return types against
  `dump/dump.cs` before adding IL2CPP calls.
- Keep the default build target `arm64-v8a`.
- Keep Unity compatibility aligned with `2019.4.22f1`.
- Do not commit generated `obj/`, `libs/` output.

## CI Build

GitHub Actions builds the native library on pushes and pull requests to
`master`. The workflow installs NDK r29, runs:

```sh
ndk-build -C jni
```

and uploads the generated native libraries as the `native-libs` artifact.

## Troubleshooting

If submodule files are missing:

```sh
git submodule update --init --recursive
```

If `dump/dump.cs` is missing or appears as a Git LFS pointer:

```sh
git lfs pull
```

If `ndk-build` is not found:

```sh
export ANDROID_SDK_ROOT=/path/to/android-sdk
export PATH="$ANDROID_SDK_ROOT/ndk/29.0.14206865:$PATH"
```

If Dobby cannot be linked, confirm this file exists:

```text
jni/dobby/lib/arm64-v8a/libdobby.a
```

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for the
full text.
