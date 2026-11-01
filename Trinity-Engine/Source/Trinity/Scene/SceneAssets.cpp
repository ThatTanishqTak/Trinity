#include "Trinity/Scene/SceneAssets.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Scene/Components.hpp"
#include "Trinity/Scene/Scene.hpp"
#include "Trinity/UI/UIAssets.hpp"

#include <algorithm>

namespace Trinity
{
    namespace
    {
        // Each asset a script's field names, with the record the registry has for it, if any
        template<typename Visit>
        void ForEachFieldAsset(Scene& scene, const AssetRegistry* registry, Visit&& visit)
        {
            for (const auto [it_Entity, it_Scripts] : scene.GetRegistry().view<ScriptComponent>().each())
            {
                for (const ScriptEntry& it_Script : it_Scripts.Scripts)
                {
                    for (const ScriptEntry::Field& it_Field : it_Script.Fields)
                    {
                        it_Field.Value.ForEachValue<UUID>(ScriptFieldType::Asset, [&visit, registry](const UUID& asset)
                        {
                            visit(asset, registry != nullptr && asset ? registry->Find(asset) : nullptr);
                        });
                    }
                }
            }
        }
    }

    namespace SceneAssets
    {
        void Add(AssetList& assets, UUID id)
        {
            if (id && std::ranges::find(assets, id) == assets.end())
            {
                assets.push_back(id);
            }
        }

        void Gather(Scene& scene, const AssetRegistry* registry, AssetList& assets)
        {
            SceneRegistry& l_Registry = scene.GetRegistry();
            for (const auto [it_Entity, it_Renderer] : l_Registry.view<MeshRendererComponent>().each())
            {
                Add(assets, it_Renderer.Mesh);
                for (const UUID it_Material : it_Renderer.Materials)
                {
                    Add(assets, it_Material);
                }
            }

            for (const auto [it_Entity, it_Sprite] : l_Registry.view<SpriteRendererComponent>().each())
            {
                Add(assets, it_Sprite.Texture);
            }

            for (const auto [it_Entity, it_Environment] : l_Registry.view<EnvironmentComponent>().each())
            {
                Add(assets, it_Environment.Environment);
            }

            for (const auto [it_Entity, it_Hull] : l_Registry.view<ConvexHullColliderComponent>().each())
            {
                Add(assets, it_Hull.Shape);
            }

            for (const auto [it_Entity, it_Mesh] : l_Registry.view<MeshColliderComponent>().each())
            {
                Add(assets, it_Mesh.Shape);
            }

            // A clip that preloads reads as loading until its samples are in, so the scene waits for those, and for the headers of the rest. A sound event's clips are held once it is in
            for (const auto [it_Entity, it_Source] : l_Registry.view<AudioSourceComponent>().each())
            {
                Add(assets, it_Source.Resource);
            }

            // An asset field, or each asset of a list of them, but not a scene, which loads only when switched to, nor a UI file. A cooked game's table lists only what its build reaches, so a field naming anything else there names a scene left out of the build, or an asset the build already listed as broken
            ForEachFieldAsset(scene, registry, [&assets, registry](UUID asset, const AssetRecord* record)
            {
                const bool l_Skipped = record != nullptr ? record->Importer == "Scene" || UIAssets::IsUIAsset(record->Importer) : registry != nullptr && registry->IsCooked();
                if (!l_Skipped)
                {
                    Add(assets, asset);
                }
            });
        }

        void GatherDocuments(Scene& scene, const AssetRegistry* registry, AssetList& assets)
        {
            for (const auto [it_Entity, it_Document] : scene.GetRegistry().view<UIDocumentComponent>().each())
            {
                Add(assets, it_Document.Document);
            }

            ForEachFieldAsset(scene, registry, [&assets](UUID asset, const AssetRecord* record)
            {
                if (record != nullptr && UIAssets::IsUIAsset(record->Importer))
                {
                    Add(assets, asset);
                }
            });
        }

        void GatherInstancePrefabs(Scene& scene, AssetList& assets)
        {
            for (const auto [it_Entity, it_Instance] : scene.GetRegistry().view<PrefabInstanceComponent>().each())
            {
                if (!it_Instance.IsMissing())
                {
                    Add(assets, it_Instance.Prefab);
                }
            }
        }
    }
}