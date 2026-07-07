# AGENTS.md

## Build And Verify
- Use Visual Studio 2022/MSBuild v143 on Windows; the solution is `akebi-gc.sln`.
- CI builds with `msbuild /m /p:Configuration=Release ./akebi-gc.sln` and uploads `bin/Release-x64/*`.
- Focused project builds: `msbuild .\akebi-gc.sln /m /p:Configuration=Debug /p:Platform=x64 /t:cheat-library` or replace `cheat-library` with `cheat-base`/`injector`.
- There is no repo test, lint, or formatter command outside vendor subtrees; validation is an MSBuild build.
- Clone/setup must initialize submodules recursively; vendored deps live under `cheat-base/vendor/*` even though one `.gitmodules` entry is named `cheat-library/vendor/json`.

## Build Gotchas
- `cheat-library` Release has a custom build step that runs `$(OutDir)injector.exe` after building; CI disables custom build steps before MSBuild. Avoid accidentally launching the injector when doing local Release builds.
- Output paths are `bin/<Configuration>-<PlatformShortName>/`; `cheat-library` produces `CLibrary.dll`, and runtime expects it beside `injector.exe`.
- The solution has x86 configurations, but project mappings/build settings effectively target x64; prefer `/p:Platform=x64`.
- `Release_WS` defines `_PATTERN_SCANNER` and excludes `PacketSniffer`; normal `Release` includes PacketSniffer and uses static offsets unless checksums fail.

## Project Map
- `cheat-base`: static library with shared infrastructure: config fields/macros, events, hooks, rendering/UI helpers, logger, injection helpers, and vendored libraries.
- `injector`: admin console app; starts `GenshinImpact.exe` or `YuanShen.exe` suspended, injects `CLibrary.dll` (or argv[1]), then resumes the game thread. It persists game path data in `cfg.ini`.
- `cheat-library`: injected DLL; `src/framework/dllmain.cpp` starts `Run` in `src/user/main.cpp`, initializes `cfg.json`, waits for `UserAssembly.dll`, initializes IL2CPP offsets, then calls `cheat::Init()`.
- Feature registration and GUI section order are centralized in `cheat-library/src/user/cheat/cheat.cpp`; adding a feature usually requires adding the files to `cheat-library.vcxproj` and registering `GetInstance()` there.

## Code Conventions
- Main project code uses MSVC MultiByte charset and mostly C++20 in Release, but Debug/`Release_WS` for `cheat-library`/`injector` are C++17; keep changes compatible with both where those configs compile the file.
- Most feature settings use `NF`/`NFS`/`NFEX` macros from `cheat-base/src/cheat-base/config/Config.h`; `NFS`/shared fields are persisted outside the active profile.
- Precompiled headers matter: `cheat-library` sources generally include `pch-il2cpp.h`; `cheat-base` uses `pch.h`; `dllmain.cpp` explicitly does not use PCH.
- Avoid editing vendored submodule code under `cheat-base/vendor/*` unless the task is specifically about that dependency.
