#include "Export/BuildWalk.hpp"

#include <algorithm>
#include <deque>
#include <format>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace
{
    constexpr std::string_view c_BuildScenes = "the build's scenes";
    constexpr std::string_view c_ProjectInputActions = "the project's input actions";
    constexpr std::string_view c_ProjectMixer = "the project's audio mixer";

    // Where a path under /cache lies in the pack, which mounts there
    std::string ToPackPath(std::string_view cachePath)
    {
        return std::string(cachePath.substr(Trinity::Project::c_CacheMount.size() + 1));
    }

    // Where the pack keeps the file of an asset a game reads as the project has it: by its UUID, in a folder for its kind, with its own extension
    std::string GetPackedSourcePath(const Trinity::AssetRecord& record, std::string_view folder)
    {
        const std::string_view l_Path = record.Path;
        const std::size_t l_Name = l_Path.rfind('/');
        const std::size_t l_Dot = l_Path.rfind('.');
        const std::string_view l_Extension = l_Dot != std::string_view::npos && (l_Name == std::string_view::npos || l_Dot > l_Name) ? l_Path.substr(l_Dot) : std::string_view();

        return std::format("{}/{}/{}{}", Trinity::Project::c_CacheMount, folder, record.ID, l_Extension);
    }

    std::string_view GetPackedSourceFolder(std::string_view importer)
    {
        if (importer == Trinity::SoundEventAsset::c_AssetType)
        {
            return "SoundEvents";
        }

        if (importer == Trinity::AudioMixerAsset::c_AssetType)
        {
            return "Mixers";
        }

        if (importer == Trinity::InputActionsAsset::c_AssetType)
        {
            return "InputActions";
        }

        return "Files";
    }

    class Walk
    {
    public:
        Walk(const Trinity::Project& project, const Trinity::AssetRegistry& registry) : m_Project(project), m_Registry(registry)
        {

        }

        // An asset to ship, named by what reaches it, each gone through once. A built-in asset is the engine's own, so it ships with no file, and one the registry lacks is listed as broken under each that names it
        void Reach(Trinity::UUID id, std::string_view namedBy)
        {
            if (!id || Trinity::AssetManager::IsBuiltIn(id) || m_Seen.contains(id))
            {
                return;
            }

            const Trinity::AssetRecord* l_Record = m_Registry.Find(id);
            if (l_Record == nullptr)
            {
                Break(id, namedBy, "no asset has this UUID");

                return;
            }

            m_Seen.insert(id);
            m_Pending.push_back({ l_Record, std::string(namedBy) });
        }

        void Break(Trinity::UUID id, std::string_view namedBy, std::string reason)
        {
            BrokenReference l_Broken{ id, std::string(namedBy), std::move(reason) };
            if (std::ranges::find(m_Contents.Broken, l_Broken) == m_Contents.Broken.end())
            {
                m_Contents.Broken.push_back(std::move(l_Broken));
            }
        }

        [[nodiscard]] BuildContents Run()
        {
            while (!m_Pending.empty())
            {
                const Pending l_Next = std::move(m_Pending.front());
                m_Pending.pop_front();
                Visit(*l_Next.Record, l_Next.NamedBy);
            }

            m_Contents.Files.push_back({ std::string(Trinity::AssetTable::c_FileName), Trinity::AssetTable::Write(m_Contents.Records) });

            return std::move(m_Contents);
        }

    private:
        struct Pending
        {
            const Trinity::AssetRecord* Record = nullptr;
            std::string NamedBy;
        };

        void Visit(const Trinity::AssetRecord& record, std::string_view namedBy)
        {
            const std::string_view l_Type = record.Importer;
            if (l_Type == "Scene")
            {
                VisitScene(record, namedBy);
            }
            else if (l_Type == Trinity::PrefabAsset::c_AssetType)
            {
                VisitPrefab(record, namedBy);
            }
            else if (l_Type == Trinity::c_ModelAssetType)
            {
                VisitModel(record, namedBy);
            }
            else if (l_Type == Trinity::MaterialAsset::c_AssetType)
            {
                VisitMaterial(record, namedBy);
            }
            else if (l_Type == Trinity::AudioClipAsset::c_AssetType)
            {
                VisitAudioClip(record, namedBy);
            }
            else if (l_Type == Trinity::SoundEventAsset::c_AssetType)
            {
                VisitSoundEvent(record, namedBy);
            }
            else if (l_Type == Trinity::MeshAsset::c_AssetType)
            {
                ShipCached(record, Trinity::GetCookedMeshPath(record.ID), namedBy);
            }
            else if (l_Type == Trinity::TextureAsset::c_AssetType)
            {
                ShipCached(record, Trinity::GetCookedTexturePath(record.ID), namedBy);
            }
            else if (l_Type == Trinity::EnvironmentAsset::c_AssetType)
            {
                ShipCached(record, Trinity::GetCookedEnvironmentPath(record.ID), namedBy);
            }
            else if (l_Type == Trinity::ConvexHullAsset::c_AssetType || l_Type == Trinity::CollisionMeshAsset::c_AssetType)
            {
                ShipCached(record, Trinity::GetCookedCollisionShapePath(record.ID), namedBy);
            }
            else
            {
                // A mixer, input actions, and anything else whose loader reads its file as the project has it
                ShipSource(record, GetPackedSourcePath(record, GetPackedSourceFolder(l_Type)), namedBy);
            }
        }

        // Read from its file and flattened, so what its instances' prefabs hold ships, and those prefabs only if something else names them
        void VisitScene(const Trinity::AssetRecord& record, std::string_view namedBy)
        {
            const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(record.Path);
            if (!l_Text)
            {
                Break(record.ID, namedBy, std::format("it could not be read: {}", Trinity::ToString(l_Text.GetError())));

                return;
            }

            Trinity::Scene l_Scene;
            if (const Trinity::Expected<void, std::string> l_Loaded = Trinity::SceneSerializer::LoadFromText(l_Scene, *l_Text); !l_Loaded)
            {
                Break(record.ID, namedBy, std::format("it could not be loaded: {}", l_Loaded.GetError()));

                return;
            }

            ReachContents(l_Scene, record.Path, false);

            const std::string l_CookedPath = std::format("{}/Scenes/{}.trscene", Trinity::Project::c_CacheMount, record.ID);
            m_Contents.Files.push_back({ ToPackPath(l_CookedPath), Trinity::SceneSerializer::SaveToBinary(l_Scene, true) });
            Ship(record, l_CookedPath);
        }

        // Cooked whole, its nested instances still instances, so an instance made in the game has the UUIDs one made in Forge has, and so the prefabs nested in it ship too
        void VisitPrefab(const Trinity::AssetRecord& record, std::string_view namedBy)
        {
            Trinity::Scene* l_Template = Trinity::SceneSerializer::FindPrefabTemplate(record.ID);
            if (l_Template == nullptr)
            {
                Break(record.ID, namedBy, "it cannot be read as a prefab");

                return;
            }

            ReachContents(*l_Template, record.Path, true);

            const std::string l_CookedPath = std::format("{}/Prefabs/{}{}", Trinity::Project::c_CacheMount, record.ID, Trinity::Prefab::c_Extension);
            m_Contents.Files.push_back({ ToPackPath(l_CookedPath), Trinity::SceneSerializer::SaveToBinary(*l_Template) });
            Ship(record, l_CookedPath);
        }

        // Reached only as a prefab a script makes instances of, as an instance a scene places is flattened, so its cooked hierarchy ships with what that prefab holds
        void VisitModel(const Trinity::AssetRecord& record, std::string_view namedBy)
        {
            Trinity::Scene* l_Template = Trinity::SceneSerializer::FindPrefabTemplate(record.ID);
            if (l_Template == nullptr)
            {
                Break(record.ID, namedBy, "it is not imported, or its hierarchy cannot be read");

                return;
            }

            ReachContents(*l_Template, record.Path, true);
            ShipCached(record, Trinity::GetCookedModelPath(record.ID), namedBy);
        }

        // A model's material is cooked with its overrides, and one of the project's own ships as its file is
        void VisitMaterial(const Trinity::AssetRecord& record, std::string_view namedBy)
        {
            const std::string l_CookedPath = Trinity::GetCookedMaterialPath(record.ID);
            const bool l_Imported = record.Parent.IsValid();
            const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(l_Imported ? l_CookedPath : record.Path);
            const Trinity::Expected<Trinity::MaterialData, std::string> l_Data = l_Text ? Trinity::ParseMaterialData(*l_Text) : Trinity::Expected<Trinity::MaterialData, std::string>(Trinity::Unexpected{ std::string(Trinity::ToString(l_Text.GetError())) });
            if (!l_Data)
            {
                Break(record.ID, namedBy, std::format("it could not be read: {}", l_Data.GetError()));

                return;
            }

            for (const Trinity::UUID it_Texture : l_Data->GetTextures())
            {
                Reach(it_Texture, record.Path);
            }

            if (l_Imported)
            {
                ShipCached(record, l_CookedPath, namedBy);
            }
            else
            {
                ShipSource(record, l_CookedPath, namedBy);
            }
        }

        // Its header, and its samples under the encoding the header names
        void VisitAudioClip(const Trinity::AssetRecord& record, std::string_view namedBy)
        {
            const std::string l_HeaderPath = Trinity::GetCookedAudioClipPath(record.ID);
            const Trinity::Expected<Trinity::FileBuffer, Trinity::FileError> l_File = Trinity::FileSystem::ReadFile(l_HeaderPath);
            const Trinity::Expected<Trinity::AudioClipHeader, std::string> l_Header = l_File ? Trinity::ReadAudioClipHeader(*l_File) : Trinity::Expected<Trinity::AudioClipHeader, std::string>(Trinity::Unexpected{ std::string("its header is missing") });
            if (!l_Header)
            {
                Break(record.ID, namedBy, std::format("it is not imported, or {} cannot be read: {}", l_HeaderPath, l_Header.GetError()));

                return;
            }

            if (AddCached(record, l_HeaderPath, namedBy) && AddCached(record, Trinity::GetCookedAudioDataPath(record.ID, l_Header->Encoding), namedBy))
            {
                Ship(record);
            }
        }

        void VisitSoundEvent(const Trinity::AssetRecord& record, std::string_view namedBy)
        {
            const Trinity::Expected<std::string, Trinity::FileError> l_Text = Trinity::FileSystem::ReadText(record.Path);
            const Trinity::Expected<Trinity::SoundEventData, std::string> l_Data = l_Text ? Trinity::ParseSoundEvent(*l_Text) : Trinity::Expected<Trinity::SoundEventData, std::string>(Trinity::Unexpected{ std::string(Trinity::ToString(l_Text.GetError())) });
            if (!l_Data)
            {
                Break(record.ID, namedBy, std::format("it could not be read: {}", l_Data.GetError()));

                return;
            }

            for (const Trinity::UUID it_Clip : l_Data->GetClips())
            {
                Reach(it_Clip, record.Path);
            }

            ShipSource(record, GetPackedSourcePath(record, GetPackedSourceFolder(record.Importer)), namedBy);
        }

        // What a scene or a template names, as a running scene gathers it, with each instance whose prefab is missing listed as broken, since a game could never make it
        void ReachContents(Trinity::Scene& scene, std::string_view namedBy, bool instances)
        {
            Trinity::SceneAssets::AssetList l_Assets;
            Trinity::SceneAssets::Gather(scene, &m_Registry, l_Assets);
            if (instances)
            {
                Trinity::SceneAssets::GatherInstancePrefabs(scene, l_Assets);
            }

            for (const auto [it_Entity, it_Instance] : scene.GetRegistry().view<Trinity::PrefabInstanceComponent>().each())
            {
                if (it_Instance.IsMissing())
                {
                    Break(it_Instance.Prefab, namedBy, "it places an instance of this prefab, which is missing or cannot be read");
                }
            }

            for (const Trinity::UUID it_Asset : l_Assets)
            {
                Reach(it_Asset, namedBy);
            }
        }

        // A file the importer cooked into the project's cache, which a game reads at the same path. False, listed as broken, for one not imported yet
        bool AddCached(const Trinity::AssetRecord& record, const std::string& cachePath, std::string_view namedBy)
        {
            std::filesystem::path l_Native = m_Project.ToNativePath(cachePath);
            std::error_code l_Error;
            if (!std::filesystem::is_regular_file(l_Native, l_Error))
            {
                Break(record.ID, namedBy, std::format("it is not imported, as {} is missing", cachePath));

                return false;
            }

            m_Contents.Files.push_back({ ToPackPath(cachePath), std::move(l_Native) });

            return true;
        }

        void ShipCached(const Trinity::AssetRecord& record, const std::string& cachePath, std::string_view namedBy)
        {
            if (AddCached(record, cachePath, namedBy))
            {
                Ship(record);
            }
        }

        // The asset's file as the project has it, at the cooked path a game reads it from
        void ShipSource(const Trinity::AssetRecord& record, const std::string& cookedPath, std::string_view namedBy)
        {
            std::filesystem::path l_Native = m_Project.ToNativePath(record.Path);
            std::error_code l_Error;
            if (!std::filesystem::is_regular_file(l_Native, l_Error))
            {
                Break(record.ID, namedBy, "its file is missing");

                return;
            }

            m_Contents.Files.push_back({ ToPackPath(cookedPath), std::move(l_Native) });
            Ship(record, cookedPath);
        }

        // As the asset table lists it, without the settings and sub-assets a game never reads
        void Ship(const Trinity::AssetRecord& record, std::string cookedPath = {})
        {
            Trinity::AssetRecord& l_Shipped = m_Contents.Records.emplace_back();
            l_Shipped.ID = record.ID;
            l_Shipped.Path = record.Path;
            l_Shipped.Importer = record.Importer;
            l_Shipped.Parent = record.Parent;
            l_Shipped.CookedPath = std::move(cookedPath);
        }

        const Trinity::Project& m_Project;
        const Trinity::AssetRegistry& m_Registry;
        std::unordered_set<Trinity::UUID> m_Seen;
        std::deque<Pending> m_Pending;
        BuildContents m_Contents;
    };
}

namespace BuildWalk
{
    BuildContents Gather(const Trinity::Project& project, const Trinity::AssetRegistry& registry, std::span<const Trinity::UUID> scenes)
    {
        TR_PROFILE_FUNCTION();

        Walk l_Walk(project, registry);
        for (const Trinity::UUID it_Scene : scenes)
        {
            const Trinity::AssetRecord* l_Record = registry.Find(it_Scene);
            if (l_Record != nullptr && l_Record->Importer != "Scene")
            {
                l_Walk.Break(it_Scene, c_BuildScenes, "it is not a scene");

                continue;
            }

            l_Walk.Reach(it_Scene, c_BuildScenes);
        }

        l_Walk.Reach(project.GetInputActions(), c_ProjectInputActions);
        l_Walk.Reach(project.GetAudioSettings().Mixer, c_ProjectMixer);

        return l_Walk.Run();
    }
}