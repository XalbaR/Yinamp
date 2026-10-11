# Contributing to Yinamp

Yinamp supports Windows x64. Development, validation and CI use **Debug only**.

## Set up a checkout

Follow [Getting started](docs/GETTING_STARTED.md) for clone, configure, Debug build, test and first launch. [Building](docs/BUILDING.md) covers toolchain and alternate build directories. The bundled MIT-licensed Classic skin requires no additional setup; no package manager is required.

## Development workflow

Use C++23, four-space indentation and the checked-in `.clang-format` and `.editorconfig`. Match surrounding code and avoid unrelated formatting changes. clang-format is an optional contributor tool, not a build dependency. Keep includes and layer boundaries consistent with [Architecture](docs/ARCHITECTURE.md).

The build enables `/W4 /WX`, `/permissive-` and UTF-8. Warnings are errors; fix them instead of weakening the policy. Do not change configurations to work around a failure. For explicit commands use `--config Debug` and `ctest -C Debug`.

Close the Debug executable if it locks a build output. Never commit generated binaries, CMake caches, local presets, screenshots from personal sessions, downloaded media, user playlists or session/cache files. Use relative or temporary fixture paths; do not commit secrets, credentials, certificates, local usernames or machine paths. Do not submit copyrighted skins/media/test fixtures without verified redistribution permission.

## Tests and pull requests

Create a focused branch, make the change, run the full deterministic Debug CTest suite and inspect `git diff --check`. Add meaningful offline coverage for behavior changes. Tests should create synthetic media and isolated temporary data; live YouTube/CDN requests belong outside standard CI. Explain any failures or platform-dependent limitations. See [Testing](docs/TESTING.md).

Open a pull request against `main` with the problem, changed behavior, validation commands/results and any user-facing risks. Update the appropriate public guide when behavior changes. Avoid milestone diaries and fixed test-count claims in documentation. Obtain provenance and license information for any new asset. Discuss dependencies or broad architecture changes before implementation.

Contributions to Yinamp source code are made under the project [MIT License](LICENSE). New third-party assets must retain their own attribution and compatible redistribution terms.
