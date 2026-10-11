# Building on Windows

## Toolchain

Use Visual Studio 2022 with Desktop development with C++, MSVC v143 supporting C++23, a Windows 10/11 SDK and CMake 3.25+. Choose the CMake tools component or install CMake separately. Windows x64 and the Visual Studio 17 2022 x64 generator are required. Compiler options include `/W4 /WX /permissive- /utf-8`; warnings fail the build.

No package manager is required. All external link libraries are supplied by the Windows SDK. Presets do not require a vcpkg installation or toolchain.

## Daily Debug app build

From Developer PowerShell in the repository root:

```powershell
cmake --preset windows-x64-app
cmake --build --preset debug-app-x64
```

`windows-x64-app` configures `build/windows-x64-app` with `CMAKE_CONFIGURATION_TYPES=Debug` and `BUILD_TESTING=OFF`. The executable is `build/windows-x64-app/Debug/Yinamp.exe`. Test executables and their intermediate files are not generated in this app tree.

## Validation and CI

Use the separate tests tree for the complete offline suite:

```powershell
cmake --preset windows-x64-tests
cmake --build --preset debug-tests-x64
ctest --preset debug-tests-x64 --no-tests=error
```

`windows-x64-tests` configures `build/windows-x64-tests` with `BUILD_TESTING=ON` and builds the app plus all existing test targets. Both configure presets use Visual Studio 2022 x64 and Debug only; both build presets select Debug. For explicit CMake/CTest invocations always specify `--config Debug` / `-C Debug`. GitHub Actions uses the tests presets without filtering the suite.

## Clean generated outputs

From the repository root:

```powershell
./scripts/Clean-Workspace.ps1 -WhatIf
./scripts/Clean-Workspace.ps1
```

The default command removes only the repository's generated `build/`, `.vs/` and `out/` directories, resolved relative to the script. It refuses symbolic links/junctions within those directories. Source, assets, docs, `tests/`, `.git` and user media/cache storage are outside its cleanup scope. Keep personal files outside generated output directories.

The old `build/windows-x64` tree and `windows-x64` / `debug-x64` presets are obsolete. Cleanup removes that old tree; configure the app and tests presets again afterwards. A fresh configure also avoids stale toolchain or skin paths.

## Bundled skin

The MIT-licensed Classic skin is included under `assets/skins/classic`. Startup and skin-dependent tests use these loose BMP atlases and PLEDIT.TXT without extra setup. `.wsz` loading is absent. The [skin README](../assets/skins/classic/README.md) documents atlas dimensions; see [third-party notices](../THIRD_PARTY_NOTICES.md) for the current MIT status.

The application prefers adjacent `assets/skins/classic` when an `assets` directory exists beside the executable, otherwise it uses the configured source directory. An optional compatible external atlas directory may be selected with `-DYINAMP_SKIN_DIRECTORY=<directory>`. Generated build headers contain resolved paths; distribute source archives separately from build caches.

## Distribution

Source version is currently 0.1.0. A public binary release and its version are a separate maintainer decision. No installer or binary packaging target is currently configured; do not use older packages as installation instructions.
