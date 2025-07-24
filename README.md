# Trinity

## Phase 0: Foundations

**Duration:** 1–2 weeks
**Goal:** Repo, toolchain, core `Application` & CI in place.

| Task              | Details                                                           | Deliverable                                     |
| ----------------- | ----------------------------------------------------------------- | ----------------------------------------------- |
| Repo & Submodules | Add all third‑party under `external/` via Git submodules.         | `external/` populated, recursive update passes. |
| CMake Bootstrap   | Root + `src/Engine` CMakeLists; Vulkan SDK discovery; MSVC flags. | `YourEngineCore` builds.                        |
| CLI & Working Dir | `Application(argc,argv)` parsing `--working-dir` & `--headless`.  | Sample “hello” logs CWD and exits.              |
| CI Skeleton       | GitHub Actions: configure, build, run headless smoke test.        | Green badge on repo.                            |

---

## Phase 1: Hello Triangle Sample

**Goal:** Prove Vulkan + GLFW + Assimp integration.

| Task               | Details                                                             | Deliverable                 |
| ------------------ | ------------------------------------------------------------------- | --------------------------- |
| SampleApp Subclass | `Samples/HelloTriangle/SampleApp` derives from `Core::Application`. | `HelloTriangle` executable. |
| Vulkan Init        | Instance, device, debug layers, swapchain.                          | Triangle renders to window. |
| Assimp Mesh Load   | Load a simple OBJ via Assimp, upload to GPU.                        | Mesh‑based triangle.        |
| Validation & Logs  | spdlog diagnostics + validation layers enabled by default.          | No errors in Debug.         |

---

## Phase 2: ECS & Scene Serialization

**Goal:** EnTT registry + YAML‑CPP round‑trip.

| Task          | Details                                                | Deliverable                  |
| ------------- | ------------------------------------------------------ | ---------------------------- |
| EnTT Registry | Define components: `Transform`, `MeshRenderer`, etc.   | Minimal ECS demo.            |
| YAML Schema   | Draft scene format (entities + components).            | Example `scene.yaml`.        |
| SceneLoader   | `YamlSceneLoader::Load()` → populate `entt::registry`. | YAML load spawns entities.   |
| Save Function | Dump registry back to YAML.                            | Round‑trip YAML test passes. |

---

## Phase 3: Render System, Resource Manager & Packaging API

**Goal:** ECS→Renderer wiring, robust Resource Manager, and early Packaging API stub.

| Task                   | Details                                                                   | Deliverable                                                               |
| ---------------------- | ------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| Render System          | Iterate `view<Transform,MeshRenderer>`, bind pipelines, draw.             | Scene.yaml renders correctly.                                             |
| Shader Manager         | Offline SPIR‑V compile + live‑reload.                                     | GLSL edits hot‑reload in running engine.                                  |
| **Resource Manager**   | Handle‑based API for textures/meshes/shaders/audio; lifetime & caching.   | `auto tex = ResMgr::LoadTexture("foo.png");` single GPU upload per asset. |
| Async Loading Stub     | Signature `co_await ResMgr::LoadMeshAsync("bar.obj")`.                    | Coroutine stub returns when “ready.”                                      |
| Hot‑Reload Boilerplate | File‑watcher skeleton + reload callbacks.                                 | CI modifies asset → live reload.                                          |
| Packaging API Stub     | `BuildSystem::BuildPackage(outputDir)` copies exe + assets into a folder. | `Engine.BuildPackage("GameBuild")` yields folder skeleton.                |

---

## Phase 4: GUI & Editor Tools

**Goal:** ImGuiLayer + ImGuizmo in core & sample.

| Task               | Details                                                    | Deliverable                         |
| ------------------ | ---------------------------------------------------------- | ----------------------------------- |
| ImGuiLayer         | Dockspace, style, frame lifecycle in `Application::Run()`. | Sample shows ImGui demo window.     |
| Panels API         | Interface for registering custom panels.                   | “Hello” panel prints FPS.           |
| ImGuizmo           | Wire gizmos to `Transform` components.                     | Drag gizmo moves mesh in scene.     |
| EditorApp Skeleton | Create `YourEngineEditor` target with empty dockspace.     | Blank editor window with dockspace. |

---

## Phase 5: Lua Scripting Integration

**Goal:** Embed Lua, expose engine APIs, let users script gameplay.

| Task               | Details                                                                              | Deliverable                                           |
| ------------------ | ------------------------------------------------------------------------------------ | ----------------------------------------------------- |
| Lua Bindings       | Choose binding (sol2 or LuaBridge) and integrate Lua interpreter into `Core`.        | `LuaState` initialized in `OnInit()`.                 |
| Expose Engine APIs | Register functions: entity creation, component access (`GetTransform`, `SetMesh`).   | Lua scripts can spawn entities and modify components. |
| Script Component   | Create `ScriptComponent{ std::string scriptPath; }`; engine loads & runs each frame. | Drag‑drop script in editor → attaches to entity.      |
| Hot‑Reload Scripts | Watch `.lua` files; reload and rebind on change.                                     | Edit script → behavior updates in real time.          |
| Sample Game Script | Write sample `player.lua` to move entity with keyboard input.                        | Demo of scripted movement in HelloTriangle sample.    |

---

## Phase 6: Physics Integration

**Goal:** PhysX into ECS, sync transforms.

| Task                | Details                                            | Deliverable                               |
| ------------------- | -------------------------------------------------- | ----------------------------------------- |
| PhysX Facade        | Wrap PhysX init, scene, actors in `PhysicsSystem`. | Called each `OnUpdate()`.                 |
| RigidBody Component | Store mass, shape in ECS; load from YAML.          | YAML→`RigidBody` loads.                   |
| Transform Sync      | After PhysX step, update ECS `Transform`.          | Dropping cube falls and renders properly. |
| Editor Controls     | Expose mass/shape in Inspector.                    | Tweak mass → live simulation change.      |

---

## Phase 7: Audio System

**Goal:** FMOD event playback bound to ECS.

| Task               | Details                                        | Deliverable                        |
| ------------------ | ---------------------------------------------- | ---------------------------------- |
| FMOD Init          | Initialize FMOD in `AudioSystem`.              | Test bank plays on startup.        |
| AudioEmitter Comp. | Store bank & event names; YAML load support.   | YAML emitter triggers sound.       |
| 3D Emitter Sync    | Update FMOD 3D attrs from `Transform`.         | Sound pans with camera movement.   |
| Editor UI          | Inspector drop‑down lists events + play/pause. | Real‑time audio editing in editor. |

---

## Phase 8: Asset Pipeline & Async I/O

**Goal:** C++20 coroutine‑based async loading + hot‑reload for all asset types.

| Task            | Details                                           | Deliverable                            |
| --------------- | ------------------------------------------------- | -------------------------------------- |
| I/O Service     | Coroutine task queue for file reads.              | `co_await LoadMeshAsync()` works.      |
| Asset Manifest  | JSON/YAML list of assets for packaging.           | Engine can report missing assets.      |
| Hot‑Reload All  | Watch scenes, shaders, models, textures, scripts. | Any file change triggers reload event. |
| Editor Feedback | UI highlights reloaded assets.                    | Status messages & log entries.         |

---

## Phase 9: Multithreading & Job System

**Goal:** Offload physics, audio, and asset loading; keep UI smooth.

| Task                  | Details                                                      | Deliverable                                  |
| --------------------- | ------------------------------------------------------------ | -------------------------------------------- |
| Job Scheduler         | Worker threads + task queue, futures/promises or coroutines. | `SubmitJob(...)` returns `std::future`.      |
| Physics & Audio       | Offload PhysX/FM od updates.                                 | Main thread sync point aligned with frame.   |
| Resource Loading      | Dispatch loads to I/O threads.                               | Main thread poll for `ResourceReady` events. |
| Editor Responsiveness | Ensure ImGui never drops below \~60 FPS under load.          | Stress‑test logs performance.                |

---

## Phase 10: Editor Finalization & Plugins

**Goal:** Polish UX, undo/redo, plugin API, asset browser.

| Task             | Details                                                       | Deliverable                            |
| ---------------- | ------------------------------------------------------------- | -------------------------------------- |
| Undo/Redo        | Command pattern in `Core::Application`.                       | Move/rename entities undoable.         |
| Asset Browser    | File‑tree view, drag‑drop assets into scene.                  | Drag model → new ECS entity appears.   |
| Plugin Interface | C ABI header, DLL load at runtime.                            | Sample “HelloPlugin” loaded by editor. |
| Packaging UI     | Integrate Build button & config dialog in Editor’s File menu. | Progress bar + logs during build.      |

---

## Phase 11: In‑Engine Build System & Distribution Packaging

**Goal:** “Build” button that collects exe, assets, DLLs into `dist/` folder plus launcher script.

| Task                   | Details                                                                                         | Deliverable                                                                          |
| ---------------------- | ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| Build Config Format    | Define `build.config.yaml` (target, output dir, scenes, plugins, options).                      | Example config for “Win64 / Debug” and “Win64 / Release.”                            |
| BuildSystem API        | `BuildSystem::Build(const BuildConfig&)` in core + CLI entry point.                             | `Engine.Build({ ... })` from CLI or Editor triggers package.                         |
| Dependency Graph       | Use Resource Manager’s metadata to include only referenced assets.                              | Log listing packaged assets.                                                         |
| File Copy & Processing | Copy exe, DLLs (PhysX, FMOD), SPV shaders, assets into structured `dist/MyGame/`.               | Folder contains `/MyGame.exe`, `/Assets/`, `/Shaders/`, `/Plugins/`, `run_game.bat`. |
| Launcher Script        | Generate `run_game.bat` or `run_game.sh` that sets `--working-dir Assets` and any flags.        | Double‑clickable script to launch.                                                   |
| Editor Integration     | “Build” button in Editor; config dialog; async build task with UI feedback.                     | UI progress & logs during build.                                                     |
| CI/CD Packaging        | GitHub Actions step to invoke headless build and upload `dist/` as artifacts or GitHub Release. | Nightly build artifacts available automatically.                                     |
| Cross‑Platform Targets | Extend configs for macOS (`.app` bundle) and Linux (ELF + `.sh` launcher).                      | Additional presets in `build.config.yaml`; CMake toolchains for portability.         |

---

## Phase 12: Testing, Documentation & Release

**Goal:** Shipable engine + editor with docs, tests, tutorials.

| Task                | Details                                                                | Deliverable                                           |
| ------------------- | ---------------------------------------------------------------------- | ----------------------------------------------------- |
| Unit & Integration  | Tests for Core, Renderer utils, Scene I/O, ECS logic.                  | ≥ 80% coverage; green CI.                             |
| Doxygen & Guides    | Generate API docs; write Getting Started & Lua scripting guide.        | Hosted on GitHub Pages.                               |
| Tutorials & Samples | Step‑by‑step guides: Triangle, Lua script sample, Editor walk‑through. | Markdown in `docs/`; possible screencast scripts.     |
| Release Packaging   | Final Release config + automated packaging.                            | Versioned `.zip` or installer ready for distribution. |

---