#pragma once

#include "Trinity/Audio/AudioSettings.hpp"
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
    class AudioEngine;
    class Scene;
    class SceneRuntime;

    // Runs one scene at a time, and switches once the next scene and every asset it uses are ready
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

        // The registry and the audio engine must outlive the manager
        explicit SceneManager(const PhysicsSettings& settings = {}, const AssetRegistry* registry = nullptr, UUID inputActions = {}, AudioEngine* audio = nullptr, const AudioSettings& audioSettings = {});
        ~SceneManager();

        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;

        [[nodiscard]] static SceneManager* GetActive();

        // Takes the scene and runs it at once, in place of any running
        void Run(Scope<Scene> scene, UUID asset = {});
        // By UUID or project path. A later request replaces one still loading. False, logged, if unknown
        bool Load(UUID scene);
        bool Load(std::string_view path);

        // Then the switch, once a loading scene is ready
        void Update(Timestep timestep);
        void Step();

        [[nodiscard]] Scene* GetScene() const;
        [[nodiscard]] SceneRuntime* GetRuntime() const;
        [[nodiscard]] UUID GetSceneAsset() const;
        [[nodiscard]] bool IsLoading() const;
        // It holds every asset it uses, each a script field names, and what is inside each prefab
        [[nodiscard]] bool Holds(UUID asset) const;
        [[nodiscard]] Statistics GetStatistics() const;

    private:
        struct State;

        void Advance();
        void Switch();

        State* m_State = nullptr;
    };
}