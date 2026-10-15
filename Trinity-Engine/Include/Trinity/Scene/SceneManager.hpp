#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/Export.hpp"
#include "Trinity/Core/Timestep.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/Physics/PhysicsTypes.hpp"

#include <cstdint>
#include <string_view>

namespace Trinity
{
    class AssetRegistry;
    class Scene;
    class SceneRuntime;

    // Runs one scene at a time, a game's or Forge's copy to play, and switches to a scene asset a script or the game asks for once it is ready: its file is read on a worker and loaded into a scene of its own, then every mesh, material and its textures, sprite texture, environment, hull and collision mesh it uses, and each asset a script field names, is loaded, which the new scene holds while it runs. The switch comes at the end of the frame, after the scene's scripts and its transform pass: entities marked KeepAcrossScenes, with their subtrees, move into the new scene under their own UUIDs, their scripts keeping their instances, fields and waiting timers and coroutines, then the old scene's scripts have OnDestroy and it goes, and the new scene's scripts have OnCreate as it first runs. A scene that cannot be found or read is logged and the running one goes on. C# and SceneManager::GetActive reach the one made last that still lives. Main thread only
    class TRINITY_API SceneManager
    {
    public:
        struct Statistics
        {
            std::uint32_t Switches = 0;
            // Entities kept across the last switch, subtrees included
            std::uint32_t Kept = 0;
            // Assets the scene loading now still waits for
            std::uint32_t Waiting = 0;
        };

        // The physics a scene runs with, and the registry scene assets are found in, which must outlive the manager. Without one only a scene given to Run runs
        explicit SceneManager(const PhysicsSettings& settings = {}, const AssetRegistry* registry = nullptr);
        ~SceneManager();

        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;

        [[nodiscard]] static SceneManager* GetActive();

        // Runs the scene at once, taking it, in place of any running, as play begins. The asset is the scene asset it was made from, if any
        void Run(Scope<Scene> scene, UUID asset = {});
        // Begins loading the scene asset, by its UUID or its path in the project, such as Scenes/Level.trscene, to switch to once it and its assets are ready. One asked for while another loads takes its place. False, logged, for a scene the registry does not have, and the running scene goes on
        bool Load(UUID scene);
        bool Load(std::string_view path);

        // The running scene's frame, or its one step for a paused scene's Step, then the switch once a loading scene is ready
        void Update(Timestep timestep);
        void Step();

        [[nodiscard]] Scene* GetScene() const;
        [[nodiscard]] SceneRuntime* GetRuntime() const;
        [[nodiscard]] UUID GetSceneAsset() const;
        [[nodiscard]] bool IsLoading() const;
        [[nodiscard]] Statistics GetStatistics() const;

    private:
        struct State;

        void Advance();
        void Switch();

        State* m_State = nullptr;
    };
}