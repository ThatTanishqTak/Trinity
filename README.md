# Trinity

A C++ 26 game engine with an editor (Forge), a project and version manager (Hub), and a player that
runs exported games (Runtime). Games are scripted in C# or C++.

| Target | What it is |
| --- | --- |
| `Trinity-Engine` | The engine. A shared library in Debug and Release (`TRINITY_SHARED_ENGINE`), static in Distribution. Everything else links it. |
| `Trinity-Forge` | The editor. |
| `Trinity-Hub` | Installs engine versions, creates and lists projects, and starts Forge for a project. |
| `Trinity-Runtime` | The player. Runs an exported game from its pack. |
| `Trinity-Sandbox` | Scratch executable for trying engine features. The Visual Studio startup project. |
| `Trinity-Tools` | Static library shared by Forge and Hub: finding .NET, CMake, Ninja, compilers, and project thumbnails. No game links it. |
| `Trinity-ScriptCore` | The C# API that game scripts build against (.NET 10). |
| `Trinity-ScriptGenerator` | Roslyn source generator that registers each C# script class and checks its fields. |
| `Trinity-InteropGen` | Build-time tool that generates ScriptCore's native bindings from the engine's definitions. |

The C# targets are only built when a .NET 10 SDK is found (see [Requirements](#requirements)).

## Status

Windows is the only platform with a native window and the only export target today. Linux and
macOS build, but have no native window or gamepads yet, so applications run headless there.

## Engine features

| Area | What there is |
| --- | --- |
| Rendering | An RHI with Direct3D 12 (Agility SDK, D3D12MA) and Vulkan 1.4 (volk, VMA) backends, plus a null backend. A frame graph and bindless resources. Renderer2D for sprites. Renderer3D with clustered lighting, metallic-roughness PBR, cascaded and spot shadows, a sky with image-based lighting, and tone mapping (Khronos PBR Neutral, EV100 exposure). Debug drawing and entity picking. A native Metal backend is planned; macOS uses Vulkan through MoltenVK until then. |
| Shaders | Written in Slang, compiled at build time by `slangc` to SPIR-V and DXIL. |
| Scenes | EnTT entities and components, prefabs and prefab variants, and YAML scene files. |
| Physics | Jolt for 3D and Box2D for 2D: rigid bodies, colliders, joints, triggers, character controllers and collision layers. |
| Audio | miniaudio, with a mixer of groups, snapshots and effects, sound events, and streamed clips. |
| UI | RmlUi for game UI, both on screen and in the world. Dear ImGui and ImGuizmo for the editor. |
| Input | Keyboard, mouse and gamepads (GameInput on Windows). Input actions with maps, local multiplayer and rebinding. |
| Assets | Import from PNG, JPEG, TGA, BMP, PSD, HDR, glTF/GLB, FBX, OBJ, DAE, WAV, FLAC, MP3, OGG, RML/RCSS, TTF and OTF. Textures are stored as KTX2 with Basis Universal, and meshes are optimised with meshoptimizer. |
| Files | A virtual file system that mounts directories, memory and `.trpak` packs (zstd), plus zip reading and writing and a directory watcher. |
| Core | A job system, a profiler with Chrome trace captures, console variables, a frame allocator, tagged memory tracking, and spdlog logging. |
| Scripting | C# on .NET 10 with hot reload in Forge and NativeAOT in exported games, and C++ game modules with hot reload. |

---

# Using Trinity

## Hub

Hub manages engine versions and projects.

- **Installs** downloads versions from this repository's GitHub releases, installs one from a
  `Trinity-<version>-<platform>-<arch>.zip` file, or locates a build tree you built yourself. Every
  zip is checked against its SHA-256 before it is unpacked. The page also lists what each version
  needs on this machine.
- **Projects** lists your projects as a list or as a grid of thumbnails, with favorites first. From
  here you open a project, open it with a newer version (the project is backed up and upgraded),
  run its last build, or show its folder.
- **New Project** creates a project from a version, a template and a language (C# or C++).
- **Settings** holds the theme, the UI scale, and where installs and projects are kept.

What a version needs depends on what you build with it. Hub's Installs page checks each of these:

| Requirement | Needed for |
| --- | --- |
| .NET 10 SDK | C# scripts, and compiling a C# game ahead of time when it is exported. |
| C++ compiler (on Windows, Visual Studio's *Desktop development with C++*) | C++ modules, the players of C++ games, and linking an exported C# game. |
| CMake 3.30+ and Ninja (Make is accepted outside Windows) | C++ modules and players. |
| GameInput runtime (Windows) | Gamepads. Every version carries the installer in its `Redist` folder. |
| Vulkan loader (Windows) | Drawing with Vulkan. Without it Forge and games use D3D12 only. |

## Projects

A new project starts from a template:

| Template | What it holds |
| --- | --- |
| Empty | An empty start scene. |
| 2D | An orthographic camera, a sprite, starter input actions, and a mixer. |
| 3D | A perspective camera, a sun with shadows, a generated sky, a ground and a cube with colliders, starter input actions, and a mixer. |

The starter input actions have *Gameplay* and *UI* maps, and the mixer has *Music*, *SFX* and *UI*
groups.

On disk a project looks like this:

```
MyGame/
  MyGame.trproj        the project file
  Assets/              scenes, materials, textures, models, audio, UI...
  Scripts/             C# projects: MyGame.csproj, with MyGame.slnx beside the project file
  Source/              C++ projects: CMakeLists.txt and the module's sources
  Cache/               thumbnail, logs, editor lock, last export
  Build/               script and module builds
  Builds/              exported games (the default output folder)
```

`Cache/`, `Build/`, `Builds/` and `Saved/` are in the `.gitignore` that every new project gets.

| Extension | What it is |
| --- | --- |
| `.trscene` | Scene |
| `.trprefab` | Prefab or prefab variant |
| `.trmat` | Material |
| `.trinput` | Input actions |
| `.trmixer` | Audio mixer |
| `.trsound` | Sound event |
| `.meta` | Beside every asset: its ID, importer and import settings |
| `.trpak`, `.trgame` | An exported game's pack, and the game settings inside it |

Forge upgrades projects made with an older version, backing them up first.

## Forge

| Panel | What it does |
| --- | --- |
| Scene | Edit the scene in 2D or 3D, with move, rotate and scale gizmos, snapping, and camera bookmarks. |
| Game | The scene through its primary camera. |
| Hierarchy | The entity tree, with search, renaming, hiding and prefab instances. |
| Properties | Components of the selection, and the import settings of the selected asset. |
| Content Browser | The project's assets, scripts and sources. Create, import, reimport and find references. Drag files in from the file manager to import them. |
| Console | The log with filters, console variables and a command line. |
| Build | Choose the platform, the scenes in the build and the output folder, then build or build and run. |
| Project Settings | Player, Window, Graphics, Audio, Physics and Collision Layers. |
| Input Actions, Audio Mixer, Sound Event | Editors for `.trinput`, `.trmixer` and `.trsound` assets. |

Editing is undoable, including asset edits. Unsaved scene changes are backed up every few minutes
(set in Preferences), and Forge offers to recover them after a crash. Panel arrangements can be
saved as named layouts. On Windows Forge can draw its own title bar, which holds the menus.

Every shortcut can be rebound in **Keyboard Shortcuts** (`Ctrl+/`). The defaults include:

| Keys | Action |
| --- | --- |
| `Ctrl+K` | Command palette: find and run any command |
| `Ctrl+P` / `Ctrl+Shift+P` / `Ctrl+Alt+P` | Play or stop / pause / step a frame |
| `Ctrl+S`, `Ctrl+Shift+S` | Save scene, save scene as |
| `Ctrl+Z`, `Ctrl+Y` | Undo, redo |
| `Ctrl+D`, `Delete` | Duplicate, delete |
| `Ctrl+Shift+N`, `Alt+Shift+N` | Create an empty entity, create an empty child |
| `W` / `E` / `R`, `X` | Move / rotate / scale gizmo, toggle local or world axes |
| `F`, `Shift+F` | Frame the selection, frame and follow it |
| `Ctrl+1`-`9`, `1`-`9` | Save a camera bookmark, return to it |
| `Shift+Space` | Maximise the panel under the mouse, and restore it |
| `Ctrl+B`, `Ctrl+Shift+B` | Build and run, build settings |
| `Ctrl+,` | Preferences |

## Scripting in C#

A script is a class that derives from `Trinity.Script`. Create one from the Content Browser
(*Create > Script*). Forge builds `Scripts/` with `dotnet build` whenever a file changes there, and
reloads the scripts without leaving the editor. Public fields, and fields marked `[Field]`, show in
Properties. `[Hide]` keeps a public field out.

```csharp
using Trinity;

namespace MyGame
{
    public class Mover : Script
    {
        public float Speed = 3.0f;
        public InputAction Move;

        public override void OnCreate()
        {
            if (Move.Path.Length == 0)
            {
                Move = InputAction.Find("Move");
            }

            Log.Info($"{Entity.Name} is ready");
        }

        public override void OnUpdate(float timestep)
        {
            Vector2 input = Move.ReadVector2();
            Entity.Position += new Vector3(input.X, 0.0f, -input.Y) * Speed * timestep;
        }
    }
}
```

The callbacks are:

| Callback | When it runs |
| --- | --- |
| `OnCreate`, `OnDestroy` | When the script starts and stops. |
| `OnReloaded` | In place of `OnCreate`, after a hot reload. |
| `OnFixedUpdate(step)` | Once for each physics step. |
| `OnUpdate(timestep)`, `OnLateUpdate(timestep)` | Once per frame. |
| `OnContactBegin`, `OnContactStay`, `OnContactEnd`, `OnTriggerEnter`, `OnTriggerExit`, `OnJointBreak`, `OnCharacterHit` | On physics events. |

Scripts can also run coroutines (`StartCoroutine`, which can yield `WaitForSeconds`,
`WaitForFrames`, `WaitUntil` and more) and timers (`Invoke`, `InvokeRepeating`). Components are
reached through `Entity.Get<T>()`, `TryGet`, `Add` and `Remove`. A callback that throws is logged,
and that script is not called again.

Forge can open the scripts in Visual Studio, Rider, VS Code or the system's default editor, and
writes the debugger settings for them.

## Scripting in C++

A C++ project builds a game module from `Source/` with CMake and Ninja, and Forge reloads it after
each build. Fields are the members listed in the class's `TR_SCRIPT` block. `TR_GAME_MODULE()`
appears once per module; the `CMakeLists.txt` and source file Forge writes for a new project
already contain it.

```cpp
#include <Trinity.hpp>

namespace MyGame
{
    class Mover : public Trinity::Script
    {
    public:
        float Speed = 3.0f;

        void OnUpdate(Trinity::Timestep timestep) override
        {
            GetEntity().Patch<Trinity::TransformComponent>([&](Trinity::TransformComponent& transform)
            {
                transform.Position.x += Speed * timestep;
            });
        }
    };
}

TR_SCRIPT(MyGame::Mover)
{
    TR_FIELD(Speed);
}
```

C++ scripts have the same callbacks as C# ones. Hot reload needs the shared engine, so it is not
available in a Distribution build of Forge.

## Exporting a game

Open the Build panel, tick the scenes to include, choose an output folder, and press **Build**.
An export holds:

- the player executable, with the project's icon and version information on Windows;
- `Game.trpak`, which holds the game settings and every asset that the scenes, input actions and
  mixer reach;
- the compiled shaders, the D3D12 Agility SDK runtime, and the GameInput installer.

C# is compiled with NativeAOT, so players do not need .NET installed. A normal export uses the
Distribution player, with the C++ module linked in. A *Development Build* uses the Release player
with the shared engine and module, and adds an overlay: `F3` shows stats and the latest log lines,
and `F4` opens the RmlUi debugger.

The same export can be run without the editor:

```bat
Trinity-Forge --project=MyGame\MyGame.trproj --export=MyGame\Builds\Windows
```

---

# Building from source

## Requirements

| | |
| --- | --- |
| Windows 11 | Visual Studio 2026 with the *Desktop development with C++* workload, and Git. |
| Linux | Clang 17+ or GCC 14+, Ninja and Git. |
| macOS | Apple Clang and Ninja. Vulkan runs through MoltenVK. |
| CMake | 3.30 or newer. The Visual Studio 2026 generator needs 4.2 or newer (see below). |
| Vulkan SDK 1.4.363.0 | Provides `slangc`, which compiles the shaders, and `spirv-val`. Set `VULKAN_SDK` to it. Without `slangc` the build goes on, but builds no shaders. |
| .NET 10 SDK | 10.0.100 or a newer feature band, as `global.json` pins it. Without it the build goes on with C# scripting turned off. |

**CMake version.** The project's floor is CMake 3.30, and the Ninja presets work with it. The
`Visual Studio 18 2026` generator was only added in CMake 4.2, so generating a Visual Studio 2026
solution (the `windows-vs2026` preset) needs CMake 4.2 or newer. Installing a current CMake covers
both.

**C++26.** Trinity targets are compiled as C++ 26. With MSVC that is `/std:c++latest`, because CMake
has no C++ 26 mapping for MSVC and `/std:c++latest` is the mode MSVC ships its C++ 26 features under.
MSVC's C++ 26 support is partial, so check a feature before relying on it.

**Vulkan SDK version.** The SDK's headers must match the pinned `Vulkan-Headers`, so that `slangc`
and the engine agree. CMake warns when `VULKAN_SDK` points at a different version.

## First build (Windows)

```bat
Scripts\Setup.bat
```

This fetches the dependencies (`Scripts\Bootstrap.ps1`) and generates the solution in
`out\windows-vs2026`. Open it and build; `Trinity-Sandbox` is the startup project.

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

Use `linux-gcc` in place of `linux-clang` to build with GCC, and `macos` on macOS. Linux and
macOS have no native window yet, so applications run headless there.

## Presets and configurations

| Configure preset | Generator |
| --- | --- |
| `windows-vs2026` | Visual Studio 2026 solution (CMake 4.2+) |
| `windows-ninja` | Ninja Multi-Config with MSVC |
| `linux-clang`, `linux-gcc` | Ninja Multi-Config with Clang or GCC |
| `macos` | Ninja Multi-Config with Apple Clang |

| Configuration | |
| --- | --- |
| `Debug` | No optimisation, asserts, full logging. |
| `Release` | Optimised, with asserts, logging and debug symbols. The everyday configuration. |
| `Distribution` | What ships. Static engine, no asserts, no trace or info logging, no console window on Windows. |

Build presets are named `<configure preset>-<configuration>`, for example
`windows-vs2026-release`.

Binaries go to `bin/<configuration>-<system>-<architecture>/Trinity/`, for example
`bin/Debug-Windows-x86_64/Trinity/`. Project files and intermediates stay under `out/<preset>/`.
A `bin` folder built this way is a complete engine version: Hub's *Locate* adds it to the
installs.

### CMake options

| Option | Default | |
| --- | --- | --- |
| `TRINITY_SHARED_ENGINE` | `ON` | Build the engine as a shared library in Debug and Release. `OFF` links it statically everywhere. |
| `TRINITY_ENABLE_DOTNET` | `ON` | Build the C# side when a .NET 10 SDK is found. |
| `TRINITY_RHI_D3D12`, `TRINITY_RHI_VULKAN` | per platform | Compile a rendering backend. Windows gets both, Linux and macOS get Vulkan. |
| `TRINITY_PHYSICS_AVX2` | `ON` | Build the physics libraries for AVX2, FMA, F16C, LZCNT and TZCNT (the engine checks the processor at startup). `OFF` builds them for SSE4.2. |
| `TRINITY_ENABLE_PROFILING` | `ON` | Compile profiling scopes into Debug and Release. |
| `TRINITY_TRACK_GLOBAL_ALLOCATIONS` | `ON` | Count every global `new` and `delete` under a memory tag. |
| `TRINITY_ENABLE_LTO` | `OFF` | Link-time optimisation in Distribution. |
| `TRINITY_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings in Trinity targets as errors. |
| `TRINITY_SLANGC` | found | Path to `slangc`, when it is neither in `$VULKAN_SDK/bin` nor on the `PATH`. |

## Dependencies

Every third-party library is a git submodule under `Vendor`, and every submodule points at a fork
under `github.com/ThatTanishqTak`. Nothing is fetched from an upstream repository.

| Area | Libraries |
| --- | --- |
| Core | spdlog, glm, EnTT, yaml-cpp, simdjson |
| Rendering | Vulkan-Headers, volk, VulkanMemoryAllocator, DirectX-Headers, D3D12MemoryAllocator |
| Physics | JoltPhysics, box2d |
| Assets | stb, KTX-Software, fastgltf, assimp, meshoptimizer |
| Audio | miniaudio |
| UI and fonts | imgui (docking), ImGuizmo, RmlUi, freetype |

`Scripts/Vendor.lock` lists each fork with the exact commit it is pinned to. The pin is a commit
hash, not a tag, because a fork made with GitHub's default *Copy the main branch only* has no tags.
The bootstrap script adds each submodule and checks out that commit. It is safe to run again at any
time: it re-points a submodule whose URL changed in the lock file and moves it to the pinned commit.

On Windows, `Bootstrap.ps1` also downloads two pinned NuGet packages into `Vendor/` (they are not
committed, for licensing reasons):

| Lock file | Package | Without it |
| --- | --- | --- |
| `Scripts/AgilitySDK.lock` | `Microsoft.Direct3D.D3D12`, the D3D12 Agility SDK | D3D12 uses Windows' own runtime. |
| `Scripts/GameInput.lock` | `Microsoft.GameInput` | The engine is built without gamepads. |

Each line pins a version and a SHA-256. The Agility SDK version must match the `DirectX-Headers`
pin, and CMake stops if the two differ.

After a fresh clone of a repository that already has the submodules committed:

```sh
git submodule update --init --depth 1
```

Build every dependency once on a new machine, or after changing a pin:

```sh
cmake --build --preset <preset> --target Trinity-Vendor
```

### Adding or upgrading a dependency

1. Fork the repository on GitHub. Untick *Copy the main branch only* if the commit you want is not
   on the default branch (release branches and tags are otherwise left behind).
2. To upgrade, bring the new upstream commit into the fork first: *Sync fork* on GitHub for the
   default branch, or fetch the upstream branch and push it to the fork for any other.
3. Put the fork's URL and the full commit hash in `Scripts/Vendor.lock`.
4. Run the bootstrap script, rebuild `Trinity-Vendor`, and commit `.gitmodules` and the submodule.

The script reports a fork that does not exist or does not contain the pinned commit.

### Test models

`Scripts/FetchSamples.sh` (or `.ps1`) downloads the glTF sample models that `Scripts/Samples.lock`
pins into `Trinity-Forge/Tests/Models`. Then `Trinity-Forge --import-test` imports them.

## Packaging a release

```sh
cmake -P Scripts/Package.cmake
```

This configures this platform's preset, builds Release and Distribution from the same commit, and
writes to `out/Packages/`:

- `Trinity-<version>-<platform>-<arch>.zip`: the engine version that Hub installs.
- `Trinity-<version>-<platform>-<arch>-symbols.zip`: the Release programs' debug symbols, to unzip
  over the version.
- `TrinityHub-<hub version>-<platform>-<arch>.zip`: Hub on its own.

Each zip gets a `.sha256` file beside it. At the end the script prints the `gh release create`
command that uploads them. It refuses to package uncommitted changes unless you pass
`-DALLOW_CHANGES=ON`. The other options are `-DPRESET=<name>`, `-DBUILD=OFF` (package existing
builds) and `-DOUTPUT=<folder>`. The engine's version is `project(VERSION ...)` in the root
`CMakeLists.txt`, and Hub's is `TRINITY_HUB_VERSION`. On Linux, packaging needs `bsdtar`
(libarchive-tools) and `objcopy` (binutils). macOS packaging comes with M16.

## Command-line options

Every Trinity executable understands:

| | |
| --- | --- |
| `--headless` | Run without a window or renderer. |
| `--frames=<n>` | Exit after `n` frames. |
| `--d3d12`, `--vulkan` | Choose the rendering backend. |
| `--set=<name>=<value>` | Set a console variable. Can be repeated. |
| `--log-dir=<path>`, `--saves=<path>`, `--cache-dir=<path>` | Override where logs, saves and the cache go. |
| `--profile` | Log a profiler summary on exit. |
| `--profile-capture[=<path>]`, `--profile-capture-frames=<n>` | Write a Chrome trace of `n` frames (default 300). |

Logs go to `<executable>.log` in the user's data folder (`%LOCALAPPDATA%\Trinity\Logs` on Windows)
unless `--log-dir` says otherwise. Hub starts Forge with the project's `Cache/Logs`.

`Trinity-Forge` also takes:

| | |
| --- | --- |
| `--project=<file>` | Open a project. |
| `--new-project=<folder> --language=CSharp\|Cpp [--template=<name>]` | Create a project. |
| `--upgrade-project=<file>` | Upgrade a project from an older version. |
| `--export=<folder>` | Export the project's game and exit with 0 on success or 1 on failure. |
| `--capture=<png>` | Save the scene through its primary camera as a PNG, then exit. |
| `--import-test[=<folder>]` | Import the test models. |

`Trinity-Runtime` takes `--pack=<path>` to play a pack from somewhere other than beside it, and
`--scene=<path or ID>` to start in another scene.

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

Games made in Forge are played by `Trinity-Runtime` and need no application code of their own. A
standalone program on the engine, like the Sandbox, defines an `Application` and pushes layers onto
it:

```cpp
#include <Trinity.hpp>
#include <Trinity/Core/EntryPoint.hpp>   // in exactly one source file

class GameLayer final : public Trinity::Layer
{
public:
    GameLayer() : Layer("Game") {}

    void OnAttach() override { /* ... */ }
    void OnUpdate(Trinity::Timestep timestep) override { /* ... */ }
    void OnEvent(Trinity::Event& event) override { /* ... */ }
    void OnImGuiRender() override { /* ... */ }
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
    specification.Window.Title = "My Game";
    specification.CommandLineArgs = args;
    return new Game(std::move(specification));
}
```

A layer can also draw through `OnRender(RHI::CommandList&)` or add passes in
`OnBuildFrameGraph`. `ApplicationSpecification` chooses the window, the graphics API order, and
whether to start .NET, audio and game UI.
