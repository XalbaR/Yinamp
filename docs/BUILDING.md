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

Release always loads `assets/skins/classic` relative to the executable, independent of the current working directory. Debug prefers adjacent assets when present and otherwise uses the configured source directory. `-DYINAMP_SKIN_DIRECTORY=<directory>` selects that Debug/test fallback; the fallback is compiled out of Release.

## Distribution

Build the portable 0.1.0 app in its separate Release tree:

```powershell
cmake --preset windows-x64-release
cmake --build --preset release-app-x64
```

The preset uses Visual Studio 2022 x64, `BUILD_TESTING=OFF` and the static MSVC runtime (`/MT`). `/W4 /WX` remains enabled. The executable and bundled skin are placed in `build/windows-x64-release/Release`. Debug app/test/interactive presets retain their runtime settings and separate build trees.

The public ZIP contains the executable, required Classic skin assets and license/notices. It needs Windows 10 22H2 or Windows 11 x64 with Media Foundation (Media Feature Pack on N editions), but no separate VC++ Redistributable. User data still uses the Windows known folders described in [Storage](STORAGE.md). No installer or updater is provided.
