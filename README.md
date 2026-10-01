# Trinity

A C++26 game engine.

| Target | What it is |
| --- | --- |
| `Trinity-Engine` | The engine, a static library. Everything else links it. |
| `Trinity-Forge` | The editor. |
| `Trinity-Hub` | Project and engine-version manager; starts Forge for a project. |
| `Trinity-Sandbox` | Scratch executable for trying engine features. |

This repository is the starting scaffold: build system, dependency pins, the Core-App skeleton
(application, layers, events, input, logging, asserts), a native Win32 window and three
executables built on it. There is no renderer yet.

## Requirements

| | |
| --- | --- |
| Windows 11 | Visual Studio 2026 with the *Desktop development with C++* workload, Git |
| CMake | 3.30 or newer - but see the note below |
| Linux | Clang 17+ or GCC 14+, Ninja |

**CMake version.** The project's floor is CMake 3.30, and the Ninja presets work with it. The
`Visual Studio 18 2026` generator was only added in CMake 4.2, so generating a Visual Studio 2026
solution (the `windows-vs2026` preset) needs CMake 4.2 or newer. Installing a current CMake covers
both.

**C++26.** Trinity targets are compiled as C++26. With MSVC that is `/std:c++latest` - CMake has no
C++26 mapping for MSVC, and `/std:c++latest` is the mode MSVC ships its C++26 features under. MSVC's
C++26 support is partial, so check a feature before relying on it.

## First build (Windows)

```bat
Scripts\Setup.bat
```

This fetches the dependencies as git submodules (`Scripts\Bootstrap.ps1`) and generates
`Build\windows-vs2026`. Open the solution there and build; `Trinity-Sandbox` is the startup
project.

Without a solution, from a *Developer PowerShell for VS 2026*:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Bootstrap.ps1
cmake --preset windows-ninja
cmake --build --preset windows-ninja-debug
bin\Debug-Windows-x86_64\Trinity\Trinity-Sandbox.exe
```

Visual Studio's *Open Folder* mode also picks up the presets directly.

## First build (Linux)

```sh
bash Scripts/Bootstrap.sh
cmake --preset linux-clang
cmake --build --preset linux-clang-debug
./bin/Debug-Linux-x86_64/Trinity/Trinity-Sandbox --frames=3
```

Linux has no native window yet, so applications run headless there.

## Configurations

| | |
| --- | --- |
| `Debug` | No optimisation, asserts, full logging. |
| `Release` | Optimised, with asserts, logging and debug symbols. The everyday configuration. |
| `Distribution` | What ships. No asserts, no trace/info logging, no console window on Windows. |

Build presets are named `<configure preset>-<configuration>`, e.g. `windows-vs2026-release`.

Binaries go to `bin/<configuration>-<system>-<architecture>/Trinity/`, for example
`bin/Debug-Windows-x86_64/Trinity/`. Project files and intermediates stay under `Build/<preset>/`.

## Dependencies

Every third-party library is a git submodule under `Vendor`, and every submodule points at a fork
under `github.com/ThatTanishqTak`. Nothing is fetched from an upstream repository.

`Scripts/Vendor.lock` lists each fork with the exact commit it is pinned to. The pin is a commit
hash, not a tag, because a fork made with GitHub's default *Copy the main branch only* has no tags.
The bootstrap script adds each submodule and checks out that commit. It is safe to run again at any
time: it re-points a submodule whose URL changed in the lock file and moves it to the pinned commit.

After a fresh clone of a repository that already has the submodules committed:

```sh
git submodule update --init --depth 1
```

Build every dependency once on a new machine, or after changing a pin:

```sh
cmake --build --preset <preset> --target Trinity-Vendor
```

A dependency is only compiled as part of a normal build once a Trinity target links it. The engine
currently links spdlog, glm and EnTT; the others are added as the systems that use them are written.

### Adding or upgrading a dependency

1. Fork the repository on GitHub. Untick *Copy the main branch only* if the commit you want is not
   on the default branch (release branches and tags are otherwise left behind).
2. To upgrade, bring the new upstream commit into the fork first: *Sync fork* on GitHub for the
   default branch, or fetch the upstream branch and push it to the fork for any other.
3. Put the fork's URL and the full commit hash in `Scripts/Vendor.lock`.
4. Run the bootstrap script, rebuild `Trinity-Vendor`, and commit `.gitmodules` and the submodule.

The script reports a fork that does not exist or does not contain the pinned commit.

## Command-line options

Every Trinity executable understands:

| | |
| --- | --- |
| `--headless` | Run without a window or renderer. |
| `--frames=<n>` | Exit after `n` frames. |
| `--d3d12`, `--vulkan` | Choose the rendering backend. |

`Trinity-Forge` additionally takes `--project=<path>`.

Logs are written to `Logs/<executable>.log` in the working directory.

## Layout

```
CMake/                    build modules: configurations, platform detection, compiler options
CMakePresets.json         configure and build presets
Scripts/                  Bootstrap (dependencies), Setup.bat, Vendor.lock
Vendor/                   third-party submodules and their build wiring
Trinity-Engine/
  Include/                public headers - what applications may include
    Trinity.hpp           umbrella header
    Trinity/
      Core/               Application, Layer, LayerStack, Window, Log, Assert, entry point
      Events/             event types and dispatcher
      Input/              key and mouse codes, polling
      Renderer/           backend selection (the renderer itself is not written yet)
  Internal/               private headers - the engine's own sources only
    TrinityPCH.hpp        precompiled header
    Trinity/
      Core/               Platform.hpp (process-wide platform setup)
      Platform/           one directory per platform; the only headers that touch the OS
  Source/                 implementation, mirroring the two header trees
    Trinity/
      Core/  Input/  Renderer/  Platform/
Trinity-Forge/            editor         (Include/, Source/)
Trinity-Hub/              launcher       (Include/, Source/)
Trinity-Sandbox/          test bed       (Include/, Source/)
```

### File conventions

| Folder | Holds | Extension |
| --- | --- | --- |
| `Include/` | Public headers | `.hpp` |
| `Internal/` | Private headers | `.hpp` |
| `Source/` | Implementation | `.cpp` |

The three trees mirror each other, so `Include/Trinity/Core/Window.hpp` is implemented by
`Source/Trinity/Core/Window.cpp`. Headers are included by their path under the tree root,
`#include "Trinity/Core/Window.hpp"`, whichever tree they live in.

`Include/` is on the include path of everything that links the engine. `Internal/` is on the
engine's own include path only, so an application that tries to include an internal header fails
to compile. A header belongs in `Internal/` unless an application needs it; platform headers always
do. New files are listed in the target's `CMakeLists.txt`.

## Writing an application

```cpp
#include <Trinity.hpp>
#include <Trinity/Core/EntryPoint.hpp>   // in exactly one source file

class GameLayer final : public Trinity::Layer
{
public:
    void OnUpdate(Trinity::Timestep timestep) override { /* ... */ }
    void OnEvent(Trinity::Event& event) override { /* ... */ }
};

class Game final : public Trinity::Application
{
public:
    explicit Game(Trinity::ApplicationSpecification specification)
        : Application(std::move(specification))
    {
        PushLayer<GameLayer>();
    }
};

Trinity::Application* Trinity::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification specification;
    specification.Name = "My Game";
    specification.CommandLineArgs = args;
    return new Game(std::move(specification));
}
```
