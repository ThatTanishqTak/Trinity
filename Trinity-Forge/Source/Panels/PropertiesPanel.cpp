#include "Panels/PropertiesPanel.hpp"

#include "EditorCommands.hpp"
#include "EditorPayloads.hpp"
#include "Panels/ConsolePanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    constexpr float c_LabelWidthInFonts = 8.0f;
    constexpr const char* c_TexturePickerPopup = "##TexturePicker";
    constexpr const char* c_EnvironmentPickerPopup = "##EnvironmentPicker";
    constexpr const char* c_MeshPickerPopup = "##MeshPicker";
    constexpr const char* c_MaterialPickerPopup = "##MaterialPicker";
    constexpr const char* c_ConvexHullPickerPopup = "##ConvexHullPicker";
    constexpr const char* c_CollisionMeshPickerPopup = "##CollisionMeshPicker";
    constexpr const char* c_AddComponentPopup = "##AddComponent";
    // Add Component's list is this many lines high at most, and scrolls past them
    constexpr float c_ComponentChoiceLines = 16.0f;
    constexpr const char* c_ScriptAssetPickerPopup = "##ScriptAssetPicker";
    constexpr const char* c_UnappliedPopup = "Unapplied Import Settings###ForgeUnappliedSettings";

    // Components with an editor of their own here. Transform is never removed, and Tag is the name at the top
    constexpr std::array<std::string_view, 22> c_EditedComponents{ Trinity::TagComponent::c_TypeName, Trinity::TransformComponent::c_TypeName, Trinity::CameraComponent::c_TypeName, Trinity::SpriteRendererComponent::c_TypeName, Trinity::MeshRendererComponent::c_TypeName, Trinity::LightComponent::c_TypeName, Trinity::EnvironmentComponent::c_TypeName, Trinity::RigidBodyComponent::c_TypeName, Trinity::BoxColliderComponent::c_TypeName, Trinity::SphereColliderComponent::c_TypeName, Trinity::CapsuleColliderComponent::c_TypeName, Trinity::CylinderColliderComponent::c_TypeName, Trinity::ConvexHullColliderComponent::c_TypeName, Trinity::MeshColliderComponent::c_TypeName, Trinity::JointComponent::c_TypeName, Trinity::RigidBody2DComponent::c_TypeName, Trinity::BoxCollider2DComponent::c_TypeName, Trinity::CircleCollider2DComponent::c_TypeName, Trinity::CapsuleCollider2DComponent::c_TypeName, Trinity::PolygonCollider2DComponent::c_TypeName, Trinity::Joint2DComponent::c_TypeName, Trinity::ScriptComponent::c_TypeName };

    // A label on the left and the widget filling the rest of the row
    void Label(const char* label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // The widget edits a copy, and a change becomes a command. While a widget stays active, as during a drag, its commands merge into one
    template<Trinity::Component T, typename F>
    void EditField(CommandStack& history, Trinity::Entity entity, std::string_view field, F widget)
    {
        T l_Value = entity.Get<T>();
        if (widget(l_Value))
        {
            history.Execute(Trinity::CreateScope<SetComponentCommand<T>>(entity.GetUUID(), std::move(l_Value), std::string(field)));
        }
    }

    // A section with the name of the component it shows, or its own, open by default
    bool BeginSection(std::string_view name, const char* icon)
    {
        return ImGui::CollapsingHeader(std::format("{} {}###{}", icon, GetComponentLabel(name), name).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    }

    std::string GetAssetName(const Trinity::AssetRecord& record)
    {
        return std::filesystem::path(record.Path).filename().string();
    }

    // What a slot shows, as Unity's object fields do: what it names, "None (Material)" while empty, and a warning while what it names is gone or is not the type it takes. The tooltip says where it is, or what is wrong, and is empty for an empty slot
    struct SlotText
    {
        std::string Text;
        std::string Tooltip;
        bool Broken = false;
    };

    // The type is the asset type the slot takes, or Asset for any. A built-in mesh is in every project
    SlotText DescribeAssetSlot(const Trinity::AssetRegistry* registry, std::string_view assetType, Trinity::UUID current)
    {
        const std::string_view l_Type = assetType.empty() ? std::string_view("Asset") : assetType;
        if (!current)
        {
            return { std::format("None ({})", l_Type),{}, false };
        }

        if (assetType.empty() || assetType == Trinity::MeshAsset::c_AssetType)
        {
            if (const std::optional<Trinity::BuiltInMesh> l_BuiltIn = Trinity::FindBuiltInMesh(current))
            {
                return { std::format("{} (Built-in)", Trinity::ToString(*l_BuiltIn)), "Built into the engine, so every project has it", false };
            }
        }

        const Trinity::AssetRecord* l_Record = registry != nullptr ? registry->Find(current) : nullptr;
        if (l_Record == nullptr)
        {
            return { std::format("{} Missing ({})", Trinity::Icons::c_Warning, l_Type), std::format("{} is not in the project: it was deleted, or moved outside Forge without its .meta file", current), true };
        }

        if (!assetType.empty() && l_Record->Importer != assetType)
        {
            return { std::format("{} Type Mismatch ({})", Trinity::Icons::c_Warning, l_Type), std::format("{} is a {}, and this slot takes a {}", l_Record->Path, l_Record->Importer, l_Type), true };
        }

        return { GetAssetName(*l_Record), l_Record->Path, false };
    }

    // An entity slot's text, as an asset slot's: the entity's name, what an empty slot means, and a warning while the entity is no longer in the scene
    SlotText DescribeEntitySlot(Trinity::Scene& scene, Trinity::UUID value, std::string_view empty)
    {
        if (!value)
        {
            return { std::string(empty),{}, false };
        }

        if (const Trinity::Entity l_Entity = scene.FindEntityByUUID(value))
        {
            return { GetEntityLabel(l_Entity),{}, false };
        }

        return { std::format("{} Missing (Entity)", Trinity::Icons::c_Warning), std::format("Entity {} is not in the scene: it was deleted, or is in another scene", value), true };
    }

    // Why the project's default input actions offer nothing to pick: the project names none, or its file cannot be read. Empty while they can be read
    std::string GetActionsUnavailable(Trinity::UUID asset, const EditorSession::InputActionsFile* file)
    {
        if (!asset)
        {
            return "The project has no default input actions, which Play reads. Open an input actions asset in the Input Actions panel and choose Set as Default";
        }

        if (file == nullptr)
        {
            return std::format("The project's default input actions, {}, are not in the project", asset);
        }

        return file->Data ? std::string() : file->Error;
    }

    // An input action field's text, as an asset slot's: its path, None while empty, and a warning while the actions of source, the project's default, lack it or cannot be read
    SlotText DescribeActionSlot(const Trinity::InputActionsData* data, std::string_view unavailable, std::string_view source, std::string_view path)
    {
        if (path.empty())
        {
            return { "None (Input Action)",{}, false };
        }

        if (data == nullptr)
        {
            return { std::format("{} {}", Trinity::Icons::c_Warning, path), std::string(unavailable), true };
        }

        if (const std::optional<std::pair<std::size_t, std::size_t>> l_Found = FindInputAction(*data, path))
        {
            return { std::string(path), std::format("A {} action of {}", Trinity::ToString(data->Maps[l_Found->first].Actions[l_Found->second].Type), source), false };
        }

        return { std::format("{} Missing ({})", Trinity::Icons::c_Warning, path), std::format("{} has no action {}, or more than one map has an action of that name, so Play reads it as released. It was deleted, or renamed outside the Input Actions panel", source, path), true };
    }

    // A broken slot in the Console's warning colour. Pushed just for the button or combo's own text, and popped as soon as it is drawn, before a combo's list
    void PushSlotColor(const SlotText& slot)
    {
        if (slot.Broken)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ConsolePanel::GetLevelColor(Trinity::LogLevel::Warn));
        }
    }

    void PopSlotColor(const SlotText& slot)
    {
        if (slot.Broken)
        {
            ImGui::PopStyleColor();
        }
    }

    // What a drag from the Content Browser gives a slot of the type, as Unity's object fields take it: the asset when it is of the type, or for a slot of any type, else the first of the type among those it holds, such as a model's first mesh, material or embedded texture. None for an asset without one, or for another drag
    Trinity::UUID GetDraggedAsset(const Trinity::AssetRegistry* registry, std::string_view assetType)
    {
        const ImGuiPayload* l_Payload = ImGui::GetDragDropPayload();
        if (registry == nullptr || l_Payload == nullptr || !l_Payload->IsDataType(c_AssetPayload))
        {
            return {};
        }

        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));
        const Trinity::AssetRecord* l_Dragged = registry->Find(Trinity::UUID(l_Value));
        if (l_Dragged == nullptr)
        {
            return {};
        }

        if (assetType.empty() || l_Dragged->Importer == assetType)
        {
            return l_Dragged->ID;
        }

        const auto a_Held = std::ranges::find_if(l_Dragged->SubAssets, [registry, assetType](const Trinity::SubAsset& subAsset) { return subAsset.Importer == assetType && registry->Find(subAsset.ID) != nullptr; });

        return a_Held != l_Dragged->SubAssets.end() ? a_Held->ID : Trinity::UUID();
    }

    // The entity a drag from the Hierarchy holds, or none for another drag
    Trinity::UUID GetDraggedEntity()
    {
        const ImGuiPayload* l_Payload = ImGui::GetDragDropPayload();
        if (l_Payload == nullptr || !l_Payload->IsDataType(c_EntityPayload))
        {
            return {};
        }

        std::uint64_t l_Value = 0;
        std::memcpy(&l_Value, l_Payload->Data, sizeof(l_Value));

        return Trinity::UUID(l_Value);
    }

    // Over a slot what is dragged does not fit, the no-entry cursor, as Unity's object fields show it, with no highlight. A script is left alone, since Properties takes one dropped anywhere on an entity
    void RefuseDrag()
    {
        const ImGuiPayload* l_Payload = ImGui::GetDragDropPayload();
        if (l_Payload != nullptr && !l_Payload->IsDataType(c_ScriptPayload))
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        }
    }

    // The shape of that kind the mesh's model cooked for it, found through the model's sub-asset keys, or none when the mesh is not a model's or the model's settings left it out
    Trinity::UUID FindCollisionShape(const Trinity::AssetRegistry* registry, Trinity::UUID mesh, Trinity::CollisionShapeKind kind)
    {
        const Trinity::AssetRecord* l_Mesh = registry != nullptr && mesh ? registry->Find(mesh) : nullptr;
        const Trinity::AssetRecord* l_Model = l_Mesh != nullptr ? registry->Find(l_Mesh->Parent) : nullptr;
        if (l_Model == nullptr)
        {
            return {};
        }

        const auto a_Mesh = std::ranges::find(l_Model->SubAssets, mesh, &Trinity::SubAsset::ID);
        if (a_Mesh == l_Model->SubAssets.end())
        {
            return {};
        }

        const auto a_Shape = std::ranges::find(l_Model->SubAssets, ModelImporter::GetCollisionKey(a_Mesh->Key, kind), &Trinity::SubAsset::Key);

        return a_Shape != l_Model->SubAssets.end() && a_Shape->Importer == Trinity::GetCollisionShapeAssetType(kind) ? a_Shape->ID : Trinity::UUID();
    }

    // Three boxes on one row, one for each axis
    bool AxisCheckboxes(const char* id, glm::bvec3& axes)
    {
        ImGui::PushID(id);
        bool l_Changed = ImGui::Checkbox("X", &axes.x);
        ImGui::SameLine();
        l_Changed |= ImGui::Checkbox("Y", &axes.y);
        ImGui::SameLine();
        l_Changed |= ImGui::Checkbox("Z", &axes.z);
        ImGui::PopID();

        return l_Changed;
    }

    // The project's layers by their names
    bool LayerCombo(std::uint32_t& layer, const Trinity::CollisionLayers& layers)
    {
        const auto a_Name = [&layers](std::uint32_t index) { return std::format("{}: {}", index, layers.GetName(index)); };

        bool l_Changed = false;
        if (ImGui::BeginCombo("##Layer", a_Name(layer).c_str()))
        {
            for (std::uint32_t it_Layer = 0; it_Layer < Trinity::c_MaxCollisionLayers; ++it_Layer)
            {
                const bool l_Selected = it_Layer == layer;
                ImGui::PushID(static_cast<int>(it_Layer));
                if (ImGui::Selectable(a_Name(it_Layer).c_str(), l_Selected) && !l_Selected)
                {
                    layer = it_Layer;
                    l_Changed = true;
                }

                if (l_Selected)
                {
                    ImGui::SetItemDefaultFocus();
                }

                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        return l_Changed;
    }

    // Whether an entity can be part of a 3D body or of a 2D one, for a joint to name
    bool Is3DBody(Trinity::Entity entity)
    {
        return entity.Has<Trinity::RigidBodyComponent>() || entity.Has<Trinity::BoxColliderComponent>() || entity.Has<Trinity::SphereColliderComponent>() || entity.Has<Trinity::CapsuleColliderComponent>() || entity.Has<Trinity::CylinderColliderComponent>() || entity.Has<Trinity::ConvexHullColliderComponent>() || entity.Has<Trinity::MeshColliderComponent>();
    }

    bool Is2DBody(Trinity::Entity entity)
    {
        return entity.Has<Trinity::RigidBody2DComponent>() || entity.Has<Trinity::BoxCollider2DComponent>() || entity.Has<Trinity::CircleCollider2DComponent>() || entity.Has<Trinity::CapsuleCollider2DComponent>() || entity.Has<Trinity::PolygonCollider2DComponent>();
    }

    // What a joint joins: the world, or an entity the test takes as part of a body, other than the joint's own, picked from the list or dropped from the Hierarchy
    bool ConnectedBodyCombo(Trinity::Scene& scene, Trinity::Entity entity, Trinity::UUID& connected, const SlotText& preview, bool (*isBody)(Trinity::Entity))
    {
        bool l_Changed = false;
        PushSlotColor(preview);
        const bool l_Open = ImGui::BeginCombo("##ConnectedBody", preview.Text.c_str());
        PopSlotColor(preview);
        // Only what the list offers lights it up
        if (!l_Open && ImGui::BeginDragDropTarget())
        {
            const Trinity::Entity l_Dragged = scene.FindEntityByUUID(GetDraggedEntity());
            if (!l_Dragged || l_Dragged == entity || !isBody(l_Dragged))
            {
                RefuseDrag();
            }
            else if (ImGui::AcceptDragDropPayload(c_EntityPayload) && l_Dragged.GetUUID() != connected)
            {
                connected = l_Dragged.GetUUID();
                l_Changed = true;
            }

            ImGui::EndDragDropTarget();
        }

        if (!l_Open)
        {
            return l_Changed;
        }

        if (ImGui::Selectable("World", !connected) && connected)
        {
            connected = {};
            l_Changed = true;
        }

        for (Trinity::Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            if (it_Entity == entity || !isBody(it_Entity))
            {
                continue;
            }

            const bool l_Selected = connected == it_Entity.GetUUID();
            ImGui::PushID(it_Entity.GetUUID().ToString().c_str());
            if (ImGui::Selectable(GetEntityLabel(it_Entity).c_str(), l_Selected) && !l_Selected)
            {
                connected = it_Entity.GetUUID();
                l_Changed = true;
            }

            if (l_Selected)
            {
                ImGui::SetItemDefaultFocus();
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();

        return l_Changed;
    }

    // Whether the text holds the pattern, whatever the case of either
    bool ContainsIgnoringCase(std::string_view text, std::string_view pattern)
    {
        return pattern.empty() || std::ranges::search(text, pattern, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); }).begin() != text.end();
    }

    // A type name with its words apart, as Unity shows them: MeshRenderer reads Mesh Renderer, and RigidBody2D reads Rigid Body 2D
    std::string SpaceWords(std::string_view name)
    {
        std::string l_Text;
        for (std::size_t it_Index = 0; it_Index < name.size(); ++it_Index)
        {
            const unsigned char l_Character = static_cast<unsigned char>(name[it_Index]);
            const unsigned char l_Previous = it_Index > 0 ? static_cast<unsigned char>(name[it_Index - 1]) : 0;
            if ((std::isupper(l_Character) && std::islower(l_Previous)) || (std::isdigit(l_Character) && std::isalpha(l_Previous)))
            {
                l_Text += ' ';
            }

            l_Text += name[it_Index];
        }

        return l_Text;
    }

    // Unity's Add Component categories for the engine's components, in the order the list shows them. A module's components go under their namespace, after these
    constexpr std::array<std::string_view, 4> c_ComponentCategories{ "Rendering", "Lighting", "Physics", "Physics 2D" };
    constexpr std::string_view c_OtherCategory = "Other";
    constexpr std::string_view c_ScriptsCategory = "Scripts";

    std::string_view GetComponentCategory(std::string_view name)
    {
        constexpr std::array<std::pair<std::string_view, std::string_view>, 19> c_Categories{ {
            { Trinity::CameraComponent::c_TypeName, "Rendering" },
            { Trinity::SpriteRendererComponent::c_TypeName, "Rendering" },
            { Trinity::MeshRendererComponent::c_TypeName, "Rendering" },
            { Trinity::LightComponent::c_TypeName, "Lighting" },
            { Trinity::EnvironmentComponent::c_TypeName, "Lighting" },
            { Trinity::RigidBodyComponent::c_TypeName, "Physics" },
            { Trinity::BoxColliderComponent::c_TypeName, "Physics" },
            { Trinity::SphereColliderComponent::c_TypeName, "Physics" },
            { Trinity::CapsuleColliderComponent::c_TypeName, "Physics" },
            { Trinity::CylinderColliderComponent::c_TypeName, "Physics" },
            { Trinity::ConvexHullColliderComponent::c_TypeName, "Physics" },
            { Trinity::MeshColliderComponent::c_TypeName, "Physics" },
            { Trinity::JointComponent::c_TypeName, "Physics" },
            { Trinity::RigidBody2DComponent::c_TypeName, "Physics 2D" },
            { Trinity::BoxCollider2DComponent::c_TypeName, "Physics 2D" },
            { Trinity::CircleCollider2DComponent::c_TypeName, "Physics 2D" },
            { Trinity::CapsuleCollider2DComponent::c_TypeName, "Physics 2D" },
            { Trinity::PolygonCollider2DComponent::c_TypeName, "Physics 2D" },
            { Trinity::Joint2DComponent::c_TypeName, "Physics 2D" }
        } };

        if (const auto a_Found = std::ranges::find(c_Categories, name, &std::pair<std::string_view, std::string_view>::first); a_Found != c_Categories.end())
        {
            return a_Found->second;
        }

        const std::size_t l_Dot = name.find('.');

        return l_Dot == std::string_view::npos || name.substr(0, l_Dot) == "Trinity" ? c_OtherCategory : name.substr(0, l_Dot);
    }

    // The engine's categories first, then a module's by name, then the rest, and the scripts last
    std::pair<int, std::string_view> GetCategoryOrder(std::string_view category)
    {
        if (const auto a_Found = std::ranges::find(c_ComponentCategories, category); a_Found != c_ComponentCategories.end())
        {
            return { static_cast<int>(a_Found - c_ComponentCategories.begin()),{} };
        }

        const int l_Rank = category == c_ScriptsCategory ? 3 : category == c_OtherCategory ? 2 : 1;

        return { static_cast<int>(c_ComponentCategories.size()) + l_Rank, category };
    }

    // One row of Add Component: a component by its registered name, or a script class
    struct ComponentChoice
    {
        std::string_view Category;
        std::string Label;
        std::string_view Component;
        const Trinity::ScriptClassDescription* Script = nullptr;
    };

    // What the entity can be given that the search matches, a component's spaced name or its type, and a script's class or label, in category order and by name within each
    std::vector<ComponentChoice> GetComponentChoices(Trinity::Entity entity, std::string_view filter)
    {
        std::vector<ComponentChoice> l_Choices;
        for (const std::string_view it_Name : Trinity::SceneSerializer::GetComponentNames())
        {
            if (it_Name == Trinity::TransformComponent::c_TypeName || it_Name == Trinity::ScriptComponent::c_TypeName || Trinity::SceneSerializer::HasComponent(entity, it_Name))
            {
                continue;
            }

            const std::string_view l_Type = GetComponentLabel(it_Name);
            std::string l_Label = SpaceWords(l_Type);
            if (ContainsIgnoringCase(l_Label, filter) || ContainsIgnoringCase(l_Type, filter))
            {
                l_Choices.push_back({ .Category = GetComponentCategory(it_Name), .Label = std::move(l_Label), .Component = it_Name, .Script = nullptr });
            }
        }

        for (const Trinity::ScriptClassDescription* it_Class : Trinity::ScriptClassRegistry::GetClasses())
        {
            const std::string_view l_Name(it_Class->Name);
            const std::size_t l_Separator = l_Name.find_last_of(".:");
            std::string l_Label = std::format("{} ({})", l_Separator == std::string_view::npos ? l_Name : l_Name.substr(l_Separator + 1), GetScriptLanguageLabel(it_Class->Language));
            if (ContainsIgnoringCase(l_Name, filter) || ContainsIgnoringCase(l_Label, filter))
            {
                l_Choices.push_back({ .Category = c_ScriptsCategory, .Label = std::move(l_Label), .Component = {}, .Script = it_Class });
            }
        }

        std::ranges::stable_sort(l_Choices, [](const ComponentChoice& left, const ComponentChoice& right)
        {
            const std::pair<int, std::string_view> l_Left = GetCategoryOrder(left.Category);
            const std::pair<int, std::string_view> l_Right = GetCategoryOrder(right.Category);

            return l_Left != l_Right ? l_Left < l_Right : left.Label < right.Label;
        });

        return l_Choices;
    }

    // The component's fields as a new one has them, which Reset puts back, read from an entity of a scene of its own. Every entity comes with a Transform
    std::string GetNewComponentText(std::string_view name)
    {
        Trinity::Scene l_Scene;
        const Trinity::Entity l_Entity = l_Scene.CreateEntity();
        if (!Trinity::SceneSerializer::HasComponent(l_Entity, name))
        {
            static_cast<void>(Trinity::SceneSerializer::AddComponent(l_Entity, name));
        }

        return Trinity::SceneSerializer::SaveComponentToText(l_Entity, name);
    }

    // An entity of the scene, or none, picked from the list in hierarchy order or dropped from the Hierarchy
    bool EntityCombo(Trinity::Scene& scene, Trinity::UUID& value)
    {
        const SlotText l_Preview = DescribeEntitySlot(scene, value, "None (Entity)");

        bool l_Changed = false;
        PushSlotColor(l_Preview);
        const bool l_Open = ImGui::BeginCombo("##Value", l_Preview.Text.c_str());
        PopSlotColor(l_Preview);
        if (!l_Open && l_Preview.Broken && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", l_Preview.Tooltip.c_str());
        }

        if (!l_Open && ImGui::BeginDragDropTarget())
        {
            const Trinity::Entity l_Dragged = scene.FindEntityByUUID(GetDraggedEntity());
            if (!l_Dragged)
            {
                RefuseDrag();
            }
            else if (ImGui::AcceptDragDropPayload(c_EntityPayload) && l_Dragged.GetUUID() != value)
            {
                value = l_Dragged.GetUUID();
                l_Changed = true;
            }

            ImGui::EndDragDropTarget();
        }

        if (!l_Open)
        {
            return l_Changed;
        }

        if (ImGui::Selectable("None", !value) && value)
        {
            value = {};
            l_Changed = true;
        }

        for (Trinity::Entity it_Entity = scene.GetFirstRoot(); it_Entity; it_Entity = scene.GetNextInHierarchyOrder(it_Entity))
        {
            const bool l_Selected = value == it_Entity.GetUUID();
            ImGui::PushID(it_Entity.GetUUID().ToString().c_str());
            if (ImGui::Selectable(GetEntityLabel(it_Entity).c_str(), l_Selected) && !l_Selected)
            {
                value = it_Entity.GetUUID();
                l_Changed = true;
            }

            if (l_Selected)
            {
                ImGui::SetItemDefaultFocus();
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();

        return l_Changed;
    }

    // A value as text, for a field that is shown and not edited. An entity and an asset show by their names, as their slots do
    std::string FormatScriptValue(Trinity::Scene& scene, const Trinity::AssetRegistry* registry, const Trinity::ScriptFieldValue& value)
    {
        switch (value.GetType())
        {
            case Trinity::ScriptFieldType::Entity:
            {
                return DescribeEntitySlot(scene, value.Get<Trinity::UUID>(), "None (Entity)").Text;
            }
            case Trinity::ScriptFieldType::Asset:
            {
                return DescribeAssetSlot(registry, {}, value.Get<Trinity::UUID>()).Text;
            }
            case Trinity::ScriptFieldType::String:
            {
                return std::format("\"{}\"", std::string_view(value.Get<Trinity::ScriptString>()));
            }
            case Trinity::ScriptFieldType::InputAction:
            {
                const std::string_view l_Path(value.Get<Trinity::ScriptString>());

                return l_Path.empty() ? std::string("None (Input Action)") : std::string(l_Path);
            }
            default:
            {
                break;
            }
        }

        return std::visit([](const auto& data) -> std::string
        {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, glm::vec2>)
            {
                return std::format("{}, {}", data.x, data.y);
            }
            else if constexpr (std::is_same_v<T, glm::vec3>)
            {
                return std::format("{}, {}, {}", data.x, data.y, data.z);
            }
            else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>)
            {
                return std::format("{}, {}, {}, {}", data.x, data.y, data.z, data.w);
            }
            else if constexpr (std::is_same_v<T, Trinity::ScriptString> || std::is_same_v<T, Trinity::UUID>)
            {
                return {};
            }
            else
            {
                return std::format("{}", data);
            }
        }, value.GetStorage());
    }

    // Greyed, with its type, since nothing here may change it
    void DrawReadOnlyField(Trinity::Scene& scene, const Trinity::AssetRegistry* registry, const char* name, const Trinity::ScriptFieldValue& value)
    {
        Label(name);
        ImGui::TextDisabled("%s", std::format("{}  ({})", FormatScriptValue(scene, registry, value), Trinity::ToString(value.GetType())).c_str());
    }

    // What every collider has, above its own shape. The offset's turn shows as X, Y and Z degrees worked out afresh each frame, which is enough for the small turns a collider takes. Layers show by the project's names for them
    template<typename T>
    void DrawColliderSettings(CommandStack& history, Trinity::Entity entity, const Trinity::CollisionLayers& layers)
    {
        Label("Offset");
        EditField<T>(history, entity, "Offset", [](T& collider) { return ImGui::DragFloat3("##Offset", &collider.Offset.x, 0.01f, 0.0f, 0.0f, "%.3f m"); });

        Label("Offset Rotation");
        EditField<T>(history, entity, "OffsetRotation", [](T& collider)
        {
            glm::vec3 l_Degrees = glm::degrees(glm::eulerAngles(collider.OffsetRotation));
            if (!ImGui::DragFloat3("##OffsetRotation", &l_Degrees.x, 0.5f, 0.0f, 0.0f, "%.2f\xC2\xB0"))
            {
                return false;
            }

            collider.OffsetRotation = glm::quat(glm::radians(l_Degrees));

            return true;
        });

        Label("Friction");
        EditField<T>(history, entity, "Friction", [](T& collider) { return ImGui::DragFloat("##Friction", &collider.Friction, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

        Label("Restitution");
        EditField<T>(history, entity, "Restitution", [](T& collider) { return ImGui::DragFloat("##Restitution", &collider.Restitution, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("How much of its speed a body keeps as it bounces off: 0 stops dead, 1 bounces back as fast");
        }

        Label("Trigger");
        EditField<T>(history, entity, "Trigger", [](T& collider) { return ImGui::Checkbox("##Trigger", &collider.Trigger); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Reports what enters and leaves it without pushing anything");
        }

        Label("Layer");
        EditField<T>(history, entity, "Layer", [&layers](T& collider) { return LayerCombo(collider.Layer, layers); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Edit > Project Settings names the layers and sets which collide");
        }
    }

    // As a 3D collider's, in its entity's XY plane, with its turn there in degrees counter-clockwise, kept as radians. 2D and 3D colliders share the project's layers
    template<typename T>
    void DrawCollider2DSettings(CommandStack& history, Trinity::Entity entity, const Trinity::CollisionLayers& layers)
    {
        Label("Offset");
        EditField<T>(history, entity, "Offset", [](T& collider) { return ImGui::DragFloat2("##Offset", &collider.Offset.x, 0.01f, 0.0f, 0.0f, "%.3f m"); });

        Label("Offset Angle");
        EditField<T>(history, entity, "OffsetAngle", [](T& collider)
        {
            float l_Degrees = glm::degrees(collider.OffsetAngle);
            if (!ImGui::DragFloat("##OffsetAngle", &l_Degrees, 0.5f, 0.0f, 0.0f, "%.2f\xC2\xB0"))
            {
                return false;
            }

            collider.OffsetAngle = glm::radians(l_Degrees);

            return true;
        });

        Label("Friction");
        EditField<T>(history, entity, "Friction", [](T& collider) { return ImGui::DragFloat("##Friction", &collider.Friction, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

        Label("Restitution");
        EditField<T>(history, entity, "Restitution", [](T& collider) { return ImGui::DragFloat("##Restitution", &collider.Restitution, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("How much of its speed a body keeps as it bounces off: 0 stops dead, 1 bounces back as fast");
        }

        Label("Trigger");
        EditField<T>(history, entity, "Trigger", [](T& collider) { return ImGui::Checkbox("##Trigger", &collider.Trigger); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Reports what enters and leaves it without pushing anything");
        }

        Label("Layer");
        EditField<T>(history, entity, "Layer", [&layers](T& collider) { return LayerCombo(collider.Layer, layers); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Edit > Project Settings names the layers and sets which collide, for 2D and 3D alike");
        }
    }
}

// Settings not applied are settled before the project closes, so none is left to drop when it does
PropertiesPanel::PropertiesPanel(EditorSession& session) : Panel("Properties", Trinity::Icons::c_Sliders, DockSlot::Right), m_Session(session)
{
    m_CloseListener = m_Session.AddCloseListener([this]
    {
        m_Preview = {};
        m_Material = {};
        m_SettingsAsset = {};
        m_Unapplied.reset();
        m_AfterUnapplied = {};
        // Its values name the project's assets
        m_CopiedComponent.reset();
    });

    m_Session.SetUnappliedEdits([this] { return m_Unapplied.has_value() || GetUnappliedSettings().has_value(); }, [this](std::move_only_function<void()> then) { AskUnapplied(std::move(then)); });
}

PropertiesPanel::~PropertiesPanel()
{
    m_Session.SetUnappliedEdits({}, {});
    m_Session.RemoveCloseListener(m_CloseListener);
}

// Unity's question: Apply imports the asset again with the settings, and Revert leaves it as it was. Whatever waited for the answer, such as Forge closing, goes on after either
void PropertiesPanel::DrawPopups()
{
    if (std::exchange(m_OpenUnappliedPopup, false))
    {
        ImGui::OpenPopup(c_UnappliedPopup);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(c_UnappliedPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    if (!m_Unapplied)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();

        return;
    }

    ImGui::TextUnformatted(std::format("{} Unapplied import settings for {}", Trinity::Icons::c_Warning, m_Unapplied->Path).c_str());
    ImGui::TextDisabled("Apply imports it again with them, and Revert leaves it as it was");
    ImGui::Spacing();

    const bool l_Apply = ImGui::Button("Apply");
    ImGui::SameLine();
    const bool l_Revert = ImGui::Button("Revert");
    if (l_Apply || l_Revert)
    {
        if (l_Apply)
        {
            static_cast<void>(m_Session.ApplyImportSettings(m_Unapplied->Asset, std::move(m_Unapplied->Settings)));
        }
        else
        {
            TR_INFO("Forge: the import settings edited for {} were reverted", m_Unapplied->Path);
        }

        m_Unapplied.reset();
        ImGui::CloseCurrentPopup();
        if (std::move_only_function<void()> l_Then = std::exchange(m_AfterUnapplied, {}))
        {
            l_Then();
        }
    }

    ImGui::EndPopup();
}

// The settings edited, for the asset they were edited for, when they differ from what its .meta holds
std::optional<PropertiesPanel::UnappliedSettings> PropertiesPanel::GetUnappliedSettings() const
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = m_SettingsAsset && l_Registry != nullptr ? l_Registry->Find(m_SettingsAsset) : nullptr;
    if (l_Record == nullptr)
    {
        return std::nullopt;
    }

    if (l_Record->Importer == TextureImporter::c_Importer && m_TextureSettings != TextureImporter::ReadSettings(*l_Record))
    {
        return UnappliedSettings{ l_Record->ID, l_Record->Path, TextureImporter::MakeSettings(m_TextureSettings) };
    }

    if (l_Record->Importer == ModelImporter::c_Importer && m_ModelSettings != ModelImporter::ReadSettings(*l_Record))
    {
        return UnappliedSettings{ l_Record->ID, l_Record->Path, ModelImporter::MakeSettings(m_ModelSettings, *l_Record) };
    }

    return std::nullopt;
}

// The asset shown is no longer the one whose settings were being edited, so edits not applied are asked about rather than dropped. A question already open keeps its own
void PropertiesPanel::KeepUnappliedSettings(Trinity::UUID shown)
{
    if (!m_SettingsAsset || m_SettingsAsset == shown)
    {
        return;
    }

    std::optional<UnappliedSettings> l_Unapplied = GetUnappliedSettings();
    m_SettingsAsset = {};
    if (!l_Unapplied)
    {
        return;
    }

    if (m_Unapplied)
    {
        TR_WARN("Forge: the import settings edited for {} were dropped, since those for {} are still being asked about", l_Unapplied->Path, m_Unapplied->Path);

        return;
    }

    m_Unapplied = std::move(l_Unapplied);
    m_OpenUnappliedPopup = true;
}

// Before Forge closes or another project opens: the question already open, or one about the settings being edited, and then what waited. With nothing to ask, it goes on at once
void PropertiesPanel::AskUnapplied(std::move_only_function<void()> then)
{
    if (!m_Unapplied)
    {
        m_Unapplied = GetUnappliedSettings();
        m_SettingsAsset = {};
    }

    if (!m_Unapplied)
    {
        then();

        return;
    }

    m_AfterUnapplied = std::move(then);
    m_OpenUnappliedPopup = true;
}

// The selected entity's components, each in its own section, under IDs of that entity's own. A change from a component's menu waits until every section is drawn, since the sections read the components
void PropertiesPanel::OnImGuiRender()
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Trinity::Entity l_Entity = l_Scene.FindEntityByUUID(m_Session.GetSelection());

    // An entity selected since an asset was, here or by an undo, takes Properties back from the asset
    if (m_Session.GetSelection() != m_LastSelection)
    {
        m_LastSelection = m_Session.GetSelection();
        if (m_LastSelection.IsValid())
        {
            m_Session.SetInspectedAsset({});
        }
    }

    // A name still being typed when the selection moved on is kept for the entity it was typed for
    if (m_NameActive && (!l_Entity || l_Entity.GetUUID() != m_NameEntity || m_Session.GetInspectedAsset()))
    {
        CommitName(l_Scene.FindEntityByUUID(m_NameEntity));
        m_NameActive = false;
    }

    KeepUnappliedSettings(m_Session.GetInspectedAsset());
    if (const Trinity::UUID l_Asset = m_Session.GetInspectedAsset())
    {
        ImGui::PushID(l_Asset.ToString().c_str());
        DrawAsset(l_Asset);
        ImGui::PopID();

        return;
    }

    m_Preview = {};
    m_Material = {};

    if (!l_Entity)
    {
        ImGui::TextDisabled("Select an entity in the Hierarchy or the Viewport");

        return;
    }

    // Shown as it moves, and editable again once it stops
    const bool l_Playing = m_Session.IsPlaying();
    if (l_Playing)
    {
        ImGui::TextDisabled("Playing a copy of the scene, so nothing can be edited until it stops");
    }

    ImGui::BeginDisabled(l_Playing);
    ImGui::PushID(l_Entity.GetUUID().ToString().c_str());
    DrawName(l_Entity);
    DrawTransform(l_Entity);
    DrawCamera(l_Entity);
    DrawSpriteRenderer(l_Entity);
    DrawMeshRenderer(l_Entity);
    DrawLight(l_Entity);
    DrawEnvironment(l_Entity);
    DrawRigidBody(l_Entity);
    DrawColliders(l_Entity);
    DrawJoint(l_Entity);
    DrawRigidBody2D(l_Entity);
    DrawColliders2D(l_Entity);
    DrawJoint2D(l_Entity);
    DrawScripts(l_Entity);
    DrawOtherComponents(l_Entity);
    DrawAddComponent(l_Entity);
    ImGui::PopID();
    ImGui::EndDisabled();

    // A script dropped anywhere on the entity's sections is added to it, as on Unity's Inspector
    if (!l_Playing && ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##AddScriptDrop")))
    {
        if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(c_ScriptPayload))
        {
            static_cast<void>(m_Session.AddScript(l_Entity.GetUUID(), static_cast<const char*>(l_Payload->Data)));
        }

        ImGui::EndDragDropTarget();
    }

    if (m_PendingCommand)
    {
        m_Session.GetHistory().Execute(std::move(m_PendingCommand));
    }

    // Nothing is being dragged or typed into, so the next change is a command of its own
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }
}

// The name follows the Tag while it is not being typed into, and becomes one command when the field is left, however it is left. Escape puts the text back, so nothing changes
void PropertiesPanel::DrawName(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::TagComponent>())
    {
        ImGui::TextDisabled("%s", std::format("{} No name, since the entity has no Tag", Trinity::Icons::c_CubeOutline).c_str());
    }
    else
    {
        if (!m_NameActive)
        {
            m_NameText = std::string(std::string_view(entity.Get<Trinity::TagComponent>().Tag));
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Trinity::Icons::c_CubeOutline);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##Name", &m_NameText);
        m_NameActive = ImGui::IsItemActive();
        m_NameEntity = entity.GetUUID();
        if (ImGui::IsItemDeactivated())
        {
            CommitName(entity);
        }
    }

    ImGui::TextDisabled("UUID %s", entity.GetUUID().ToString().c_str());
    ImGui::Spacing();
}

void PropertiesPanel::CommitName(Trinity::Entity entity)
{
    if (!entity || !entity.Has<Trinity::TagComponent>() || std::string_view(entity.Get<Trinity::TagComponent>().Tag) == m_NameText)
    {
        return;
    }

    Trinity::TagComponent l_Value = entity.Get<Trinity::TagComponent>();
    l_Value.Tag = m_NameText;

    // The command selects what it renamed, which is not what was clicked when the field was left by selecting something else
    const Trinity::UUID l_Selection = m_Session.GetSelection();
    CommandStack& l_History = m_Session.GetHistory();
    l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::TagComponent>>(entity.GetUUID(), std::move(l_Value), "Name"));
    l_History.EndMerge();
    m_Session.SetSelection(l_Selection);
}

// Rotation shows as X, Y and Z degrees, kept while the entity stays selected and its rotation is only changed here, so a drag never jumps between equivalent angles
void PropertiesPanel::DrawTransform(Trinity::Entity entity)
{
    if (!BeginComponent(entity, Trinity::TransformComponent::c_TypeName, Trinity::Icons::c_WindowRestore))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();
    const Trinity::TransformComponent& l_Transform = entity.Get<Trinity::TransformComponent>();
    if (m_EulerEntity != entity.GetUUID() || m_EulerRotation != l_Transform.Rotation)
    {
        m_EulerEntity = entity.GetUUID();
        m_EulerRotation = l_Transform.Rotation;
        m_EulerDegrees = glm::degrees(glm::eulerAngles(l_Transform.Rotation));
    }

    Label("Position");
    EditField<Trinity::TransformComponent>(l_History, entity, "Position", [](Trinity::TransformComponent& transform) { return ImGui::DragFloat3("##Position", &transform.Position.x, 0.05f, 0.0f, 0.0f, "%.3f"); });

    Label("Rotation");
    glm::vec3 l_Degrees = m_EulerDegrees;
    if (ImGui::DragFloat3("##Rotation", &l_Degrees.x, 0.5f, 0.0f, 0.0f, "%.2f\xC2\xB0"))
    {
        Trinity::TransformComponent l_Value = l_Transform;
        l_Value.Rotation = glm::quat(glm::radians(l_Degrees));
        m_EulerDegrees = l_Degrees;
        m_EulerRotation = l_Value.Rotation;
        l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::TransformComponent>>(entity.GetUUID(), std::move(l_Value), "Rotation"));
    }

    Label("Scale");
    EditField<Trinity::TransformComponent>(l_History, entity, "Scale", [](Trinity::TransformComponent& transform) { return ImGui::DragFloat3("##Scale", &transform.Scale.x, 0.01f, 0.0f, 0.0f, "%.3f"); });
}

void PropertiesPanel::DrawCamera(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::CameraComponent>() || !BeginComponent(entity, Trinity::CameraComponent::c_TypeName, Trinity::Icons::c_Monitor))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();

    Label("Projection");
    EditField<Trinity::CameraComponent>(l_History, entity, "Projection", [](Trinity::CameraComponent& camera)
    {
        int l_Projection = static_cast<int>(camera.Projection);
        const bool l_Changed = ImGui::Combo("##Projection", &l_Projection, "Orthographic\0Perspective\0");
        camera.Projection = static_cast<Trinity::CameraProjection>(l_Projection);

        return l_Changed;
    });

    if (entity.Get<Trinity::CameraComponent>().Projection == Trinity::CameraProjection::Perspective)
    {
        Label("Field of View");
        EditField<Trinity::CameraComponent>(l_History, entity, "FieldOfView", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##FieldOfView", &camera.FieldOfView, 0.25f, 1.0f, 179.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp); });

        Label("Near");
        EditField<Trinity::CameraComponent>(l_History, entity, "PerspectiveNear", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##PerspectiveNear", &camera.PerspectiveNear, 0.001f, 0.0001f, 1000.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });
    }
    else
    {
        Label("Size");
        EditField<Trinity::CameraComponent>(l_History, entity, "OrthographicSize", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##Size", &camera.OrthographicSize, 0.05f, 0.01f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

        Label("Near");
        EditField<Trinity::CameraComponent>(l_History, entity, "Near", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##Near", &camera.Near, 0.05f, 0.0f, 0.0f, "%.3f"); });

        Label("Far");
        EditField<Trinity::CameraComponent>(l_History, entity, "Far", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##Far", &camera.Far, 0.05f, 0.0f, 0.0f, "%.3f"); });
    }

    Label("Primary");
    EditField<Trinity::CameraComponent>(l_History, entity, "Primary", [](Trinity::CameraComponent& camera) { return ImGui::Checkbox("##Primary", &camera.Primary); });

    Label("Exposure");
    EditField<Trinity::CameraComponent>(l_History, entity, "ExposureEV100", [](Trinity::CameraComponent& camera) { return ImGui::DragFloat("##Exposure", &camera.ExposureEV100, 0.05f, -10.0f, 24.0f, "%.2f EV100", ImGuiSliderFlags_AlwaysClamp); });

    Label("Tonemapper");
    EditField<Trinity::CameraComponent>(l_History, entity, "Tonemap", [](Trinity::CameraComponent& camera)
    {
        constexpr std::array<std::pair<Trinity::Tonemapper, const char*>, 2> c_Tonemappers{ { { Trinity::Tonemapper::None, "None" },{ Trinity::Tonemapper::PBRNeutral, "PBR Neutral" } } };

        bool l_Changed = false;
        const auto a_Current = std::ranges::find(c_Tonemappers, camera.Tonemap, &std::pair<Trinity::Tonemapper, const char*>::first);
        if (ImGui::BeginCombo("##Tonemapper", a_Current != c_Tonemappers.end() ? a_Current->second : "?"))
        {
            for (const auto& [it_Tonemapper, it_Name] : c_Tonemappers)
            {
                if (ImGui::Selectable(it_Name, it_Tonemapper == camera.Tonemap) && it_Tonemapper != camera.Tonemap)
                {
                    camera.Tonemap = it_Tonemapper;
                    l_Changed = true;
                }
            }

            ImGui::EndCombo();
        }

        return l_Changed;
    });

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Exposure 1 is EV100 %.3f. PBR Neutral matches glTF Sample Viewer, and None only clamps, which keeps unlit 2D colours as they are", Trinity::c_NeutralEV100);
    }
}

// Directional, point or spot, in glTF's units: lux for a directional light, candela for the others. Directional and spot lights shine along the entity's -Z, so turning the entity aims them, and can cast shadows. Colour is edited as it looks, in sRGB, and kept linear. A range of 0 is worked out from the intensity, and the cone's inner angle stays inside its outer one
void PropertiesPanel::DrawLight(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::LightComponent>() || !BeginComponent(entity, Trinity::LightComponent::c_TypeName, Trinity::Icons::c_Globe))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();

    Label("Type");
    EditField<Trinity::LightComponent>(l_History, entity, "Type", [](Trinity::LightComponent& light)
    {
        int l_Type = static_cast<int>(light.Type);
        const bool l_Changed = ImGui::Combo("##Type", &l_Type, "Directional\0Point\0Spot\0");
        light.Type = static_cast<Trinity::LightType>(l_Type);

        return l_Changed;
    });

    Label("Color");
    EditField<Trinity::LightComponent>(l_History, entity, "Color", [](Trinity::LightComponent& light)
    {
        glm::vec3 l_Color(Trinity::LinearToSrgb(light.Color.r), Trinity::LinearToSrgb(light.Color.g), Trinity::LinearToSrgb(light.Color.b));
        const bool l_Changed = ImGui::ColorEdit3("##Color", &l_Color.x);
        light.Color = glm::vec3(Trinity::SrgbToLinear(l_Color.r), Trinity::SrgbToLinear(l_Color.g), Trinity::SrgbToLinear(l_Color.b));

        return l_Changed;
    });

    const Trinity::LightComponent& l_Light = entity.Get<Trinity::LightComponent>();
    const bool l_Directional = l_Light.Type == Trinity::LightType::Directional;
    Label("Intensity");
    EditField<Trinity::LightComponent>(l_History, entity, "Intensity", [l_Directional](Trinity::LightComponent& light) { return ImGui::DragFloat("##Intensity", &light.Intensity, 0.05f, 0.0f, 200000.0f, l_Directional ? "%.3f lux" : "%.3f cd", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });

    if (l_Light.Type != Trinity::LightType::Point)
    {
        Label("Cast Shadows");
        EditField<Trinity::LightComponent>(l_History, entity, "CastShadows", [](Trinity::LightComponent& light) { return ImGui::Checkbox("##CastShadows", &light.CastShadows); });
        if (ImGui::IsItemHovered() && l_Directional)
        {
            ImGui::SetTooltip("The first directional light that casts shadows is the sun, whose shadows reach %.0f m from the camera", static_cast<double>(Trinity::ShadowAtlas::c_Distance));
        }
        else if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("The first %u spot lights in view that cast shadows, in hierarchy order, each get a shadow map", Trinity::ShadowAtlas::c_MaxSpotShadows);
        }
    }

    if (l_Directional)
    {
        return;
    }

    Label("Range");
    EditField<Trinity::LightComponent>(l_History, entity, "Range", [](Trinity::LightComponent& light)
    {
        const std::string l_Format = light.Range > 0.0f ? std::string("%.3f m") : std::format("Automatic, {:.2f} m", light.GetRange());

        return ImGui::DragFloat("##Range", &light.Range, 0.05f, 0.0f, 100000.0f, l_Format.c_str(), ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    });

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Where the light has faded to nothing. At 0 it is where the light falls to %g lux on a surface facing it", Trinity::LightComponent::c_AutomaticRangeCutoff);
    }

    if (l_Light.Type != Trinity::LightType::Spot)
    {
        return;
    }

    Label("Inner Angle");
    EditField<Trinity::LightComponent>(l_History, entity, "InnerConeAngle", [](Trinity::LightComponent& light) { return ImGui::DragFloat("##InnerConeAngle", &light.InnerConeAngle, 0.25f, 0.0f, light.OuterConeAngle, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp); });

    Label("Outer Angle");
    EditField<Trinity::LightComponent>(l_History, entity, "OuterConeAngle", [](Trinity::LightComponent& light)
    {
        const bool l_Changed = ImGui::DragFloat("##OuterConeAngle", &light.OuterConeAngle, 0.25f, 0.1f, 90.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp);
        light.InnerConeAngle = std::min(light.InnerConeAngle, light.OuterConeAngle);

        return l_Changed;
    });
}

// Image-based lighting from an HDR environment in the project, which takes one dropped from the Content Browser. Rotation turns it about +Y, and 90 degrees lights a model as glTF Sample Viewer does by default
void PropertiesPanel::DrawEnvironment(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::EnvironmentComponent>() || !BeginComponent(entity, Trinity::EnvironmentComponent::c_TypeName, Trinity::Icons::c_Globe))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();
    const auto a_SetEnvironment = [&l_History, entity](Trinity::UUID environment)
    {
        Trinity::EnvironmentComponent l_Value = entity.Get<Trinity::EnvironmentComponent>();
        l_Value.Environment = environment;
        l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::EnvironmentComponent>>(entity.GetUUID(), std::move(l_Value), "Environment"));
        l_History.EndMerge();
    };

    DrawAssetSlot("Environment", c_EnvironmentPickerPopup, Trinity::EnvironmentAsset::c_AssetType, entity.Get<Trinity::EnvironmentComponent>().Environment, a_SetEnvironment, "An equirectangular .hdr in the project. Without one the scene keeps its ambient light");

    Label("Intensity");
    EditField<Trinity::EnvironmentComponent>(l_History, entity, "Intensity", [](Trinity::EnvironmentComponent& environment) { return ImGui::DragFloat("##Intensity", &environment.Intensity, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });

    Label("Rotation");
    EditField<Trinity::EnvironmentComponent>(l_History, entity, "Rotation", [](Trinity::EnvironmentComponent& environment) { return ImGui::DragFloat("##Rotation", &environment.Rotation, 0.5f, -360.0f, 360.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp); });
}

void PropertiesPanel::DrawSpriteRenderer(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::SpriteRendererComponent>() || !BeginComponent(entity, Trinity::SpriteRendererComponent::c_TypeName, Trinity::Icons::c_File))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();

    DrawAssetSlot("Texture", c_TexturePickerPopup, Trinity::TextureAsset::c_AssetType, entity.Get<Trinity::SpriteRendererComponent>().Texture, [this, entity](Trinity::UUID texture) { SetTexture(entity, texture); }, "A texture in the project. Without one the sprite draws white, in its tint");

    Label("Tint");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "Tint", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::ColorEdit4("##Tint", &sprite.Tint.x, ImGuiColorEditFlags_AlphaPreviewHalf); });

    Label("Flip");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "FlipX", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::Checkbox("X##FlipX", &sprite.FlipX); });
    ImGui::SameLine();
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "FlipY", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::Checkbox("Y##FlipY", &sprite.FlipY); });

    Label("UV Rect");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "UVRect", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::DragFloat4("##UVRect", &sprite.UVRect.x, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    Label("Sorting Layer");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "SortingLayer", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::DragInt("##SortingLayer", &sprite.SortingLayer, 0.1f); });

    Label("Order in Layer");
    EditField<Trinity::SpriteRendererComponent>(l_History, entity, "OrderInLayer", [](Trinity::SpriteRendererComponent& sprite) { return ImGui::DragInt("##OrderInLayer", &sprite.OrderInLayer, 0.1f); });
}

// The mesh, a material for each of its slots and whether it casts shadows, each change one command. The slots are those the mesh's submeshes use once it has loaded, and any set past them. An empty slot draws with the default material, and empty slots at the end are not kept
void PropertiesPanel::DrawMeshRenderer(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::MeshRendererComponent>() || !BeginComponent(entity, Trinity::MeshRendererComponent::c_TypeName, Trinity::Icons::c_Cube))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();
    const auto a_Set = [&l_History, entity](const std::function<void(Trinity::MeshRendererComponent&)>& change, const char* field)
    {
        Trinity::MeshRendererComponent l_Value = entity.Get<Trinity::MeshRendererComponent>();
        change(l_Value);
        while (!l_Value.Materials.empty() && !l_Value.Materials.back())
        {
            l_Value.Materials.pop_back();
        }

        l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::MeshRendererComponent>>(entity.GetUUID(), std::move(l_Value), field));
        l_History.EndMerge();
    };

    const Trinity::UUID l_MeshID = entity.Get<Trinity::MeshRendererComponent>().Mesh;
    DrawAssetSlot("Mesh", c_MeshPickerPopup, Trinity::MeshAsset::c_AssetType, l_MeshID, [&a_Set](Trinity::UUID mesh) { a_Set([mesh](Trinity::MeshRendererComponent& value) { value.Mesh = mesh; }, "Mesh"); }, "A mesh of a model in the project, or a built-in shape. Without one nothing is drawn");

    std::size_t l_Slots = entity.Get<Trinity::MeshRendererComponent>().Materials.size();
    const bool l_Loaded = l_MeshID && Trinity::AssetManager::GetState(l_MeshID) == Trinity::AssetState::Ready;
    const Trinity::Asset* l_Asset = l_Loaded ? Trinity::AssetManager::GetAsset(l_MeshID) : nullptr;
    if (l_Asset != nullptr && l_Asset->GetAssetType() == Trinity::MeshAsset::c_AssetType)
    {
        for (const Trinity::Submesh& it_Submesh : static_cast<const Trinity::MeshAsset*>(l_Asset)->GetSubmeshes())
        {
            l_Slots = std::max(l_Slots, static_cast<std::size_t>(it_Submesh.MaterialSlot) + 1);
        }
    }
    else if (l_MeshID && l_Slots == 0)
    {
        Label("Materials");
        ImGui::TextDisabled("Shown once the mesh has loaded");
    }

    for (std::size_t it_Slot = 0; it_Slot < l_Slots; ++it_Slot)
    {
        const auto& l_Materials = entity.Get<Trinity::MeshRendererComponent>().Materials;
        const Trinity::UUID l_Material = it_Slot < l_Materials.size() ? l_Materials[it_Slot] : Trinity::UUID();
        const std::string l_Label = std::format("Material {}", it_Slot);
        ImGui::PushID(static_cast<int>(it_Slot));
        DrawAssetSlot(l_Label.c_str(), c_MaterialPickerPopup, Trinity::MaterialAsset::c_AssetType, l_Material, [&a_Set, it_Slot](Trinity::UUID material)
        {
            a_Set([it_Slot, material](Trinity::MeshRendererComponent& value)
            {
                if (value.Materials.size() <= it_Slot)
                {
                    value.Materials.resize(it_Slot + 1);
                }

                value.Materials[it_Slot] = material;
            }, "Materials");
        }, "Empty, so the default material draws this slot");
        ImGui::PopID();
    }

    Label("Cast Shadows");
    EditField<Trinity::MeshRendererComponent>(l_History, entity, "CastShadows", [](Trinity::MeshRendererComponent& meshRenderer) { return ImGui::Checkbox("##CastShadows", &meshRenderer.CastShadows); });
}

// A slot for one asset of a type: a button naming it that opens a picker of the project's assets of that type, and the built-in meshes for a mesh, which also takes one dragged from the Content Browser, or the first of its type that one holds, and a cross that clears it. Only a drag that fits lights it up, as in Unity. As in Unity it reads None (Type) while empty, and in the warning colour Missing (Type) while the asset is gone, or Type Mismatch (Type) while it is another type
void PropertiesPanel::DrawAssetSlot(const char* label, const char* popup, std::string_view assetType, Trinity::UUID current, const std::function<void(Trinity::UUID)>& set, const char* hint)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const SlotText l_Slot = DescribeAssetSlot(l_Registry, assetType, current);

    Label(label);
    const float l_ClearWidth = ImGui::GetFrameHeight();
    const float l_Width = std::max(ImGui::GetContentRegionAvail().x - l_ClearWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);
    PushSlotColor(l_Slot);
    const bool l_Clicked = ImGui::Button(std::format("{}###{}", l_Slot.Text, label).c_str(), ImVec2(l_Width, 0.0f));
    PopSlotColor(l_Slot);
    if (l_Clicked)
    {
        m_AssetFilter.clear();
        ImGui::OpenPopup(popup);
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", l_Slot.Tooltip.empty() ? hint : l_Slot.Tooltip.c_str());
    }

    if (ImGui::BeginDragDropTarget())
    {
        const Trinity::UUID l_Fit = GetDraggedAsset(l_Registry, assetType);
        if (!l_Fit)
        {
            RefuseDrag();
        }
        else if (ImGui::AcceptDragDropPayload(c_AssetPayload))
        {
            set(l_Fit);
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!current);
    if (ImGui::Button(std::format("{}##Clear{}", Trinity::Icons::c_TimesCircle, label).c_str(), ImVec2(l_ClearWidth, 0.0f)))
    {
        set({});
    }

    ImGui::EndDisabled();

    DrawAssetPicker(popup, assetType, current, set);
}

// The project's assets of one type, or of every type when none is given, by path, after the built-in meshes for a mesh, narrowed by what is typed
void PropertiesPanel::DrawAssetPicker(const char* popup, std::string_view assetType, Trinity::UUID current, const std::function<void(Trinity::UUID)>& pick)
{
    if (!ImGui::BeginPopup(popup))
    {
        return;
    }

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
    ImGui::InputTextWithHint("##Filter", assetType.empty() ? "Search assets" : std::format("Search {} assets", assetType).c_str(), &m_AssetFilter);

    if (ImGui::Selectable("None"))
    {
        pick({});
    }

    const auto a_Matches = [this](std::string_view text) { return m_AssetFilter.empty() || std::ranges::search(text, m_AssetFilter, [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); }).begin() != text.end(); };
    if (assetType == Trinity::MeshAsset::c_AssetType)
    {
        bool l_Header = false;
        for (const Trinity::BuiltInMesh it_Mesh : Trinity::c_BuiltInMeshes)
        {
            const Trinity::UUID l_ID = Trinity::GetBuiltInMeshID(it_Mesh);
            if (!a_Matches(Trinity::ToString(it_Mesh)))
            {
                continue;
            }

            if (!std::exchange(l_Header, true))
            {
                ImGui::SeparatorText("Built-in");
            }

            if (ImGui::Selectable(std::format("{}###{}", Trinity::ToString(it_Mesh), l_ID).c_str(), l_ID == current))
            {
                pick(l_ID);
            }
        }

        if (l_Header)
        {
            ImGui::SeparatorText("Project");
        }
    }

    std::vector<const Trinity::AssetRecord*> l_Assets;
    if (const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry())
    {
        for (const Trinity::AssetRecord* it_Record : l_Registry->GetRecords())
        {
            if ((assetType.empty() || it_Record->Importer == assetType) && a_Matches(it_Record->Path))
            {
                l_Assets.push_back(it_Record);
            }
        }
    }

    std::ranges::sort(l_Assets, {}, &Trinity::AssetRecord::Path);
    for (const Trinity::AssetRecord* it_Record : l_Assets)
    {
        if (ImGui::Selectable(std::format("{}###{}", it_Record->Path, it_Record->ID).c_str(), it_Record->ID == current))
        {
            pick(it_Record->ID);
        }
    }

    if (l_Assets.empty())
    {
        ImGui::TextDisabled("%s", assetType.empty() ? "No assets in the project match" : std::format("No {} assets in the project match", assetType).c_str());
    }

    ImGui::EndPopup();
}

// A module's components, each field with the editor a script's field of its type has, then components registered without fields to edit, and those no loaded code knows, which are kept as they were read
void PropertiesPanel::DrawOtherComponents(Trinity::Entity entity)
{
    for (const std::string_view it_Name : Trinity::SceneSerializer::GetComponentNames())
    {
        if (std::ranges::find(c_EditedComponents, it_Name) != c_EditedComponents.end() || !Trinity::SceneSerializer::HasComponent(entity, it_Name))
        {
            continue;
        }

        if (!BeginComponent(entity, it_Name, Trinity::Icons::c_Gear))
        {
            continue;
        }

        const Trinity::NativeComponentDescription* l_Component = Trinity::NativeComponents::Find(it_Name);
        if (l_Component == nullptr)
        {
            ImGui::TextDisabled("No editor for this component yet");

            continue;
        }

        if (l_Component->Fields.empty())
        {
            ImGui::TextDisabled("No fields");
        }

        ImGui::PushID(it_Name.data(), it_Name.data() + it_Name.size());
        for (const Trinity::ScriptFieldDescription& it_Field : l_Component->Fields)
        {
            std::optional<Trinity::ScriptFieldValue> l_Value = Trinity::NativeComponents::GetField(entity, it_Name, it_Field.Name);
            ImGui::PushID(it_Field.Name.c_str());
            if (l_Value && DrawScriptValue(it_Field, *l_Value))
            {
                m_Session.GetHistory().Execute(Trinity::CreateScope<SetNativeFieldCommand>(entity.GetUUID(), std::string(it_Name), std::string(std::string_view(it_Field.Name)), std::move(*l_Value)));
            }

            ImGui::PopID();
        }

        ImGui::PopID();
    }

    if (!entity.Has<Trinity::UnknownComponentsComponent>())
    {
        return;
    }

    for (const Trinity::UnknownComponentsComponent::Entry& it_Unknown : entity.Get<Trinity::UnknownComponentsComponent>().Entries)
    {
        if (BeginSection(it_Unknown.Name, Trinity::Icons::c_Warning))
        {
            ImGui::TextDisabled("Kept as it was read, since no loaded code knows this component");
        }
    }
}

// Static bodies never move, kinematic ones go where their transform is moved, and dynamic ones fall and collide. Mass, damping, gravity, continuous collision, locks and sleep only matter to a dynamic body, so they show for one alone. Initial velocities are in metres per second, and in degrees per second about each world axis, kept as radians
void PropertiesPanel::DrawRigidBody(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::RigidBodyComponent>() || !BeginComponent(entity, Trinity::RigidBodyComponent::c_TypeName, Trinity::Icons::c_Arrows))
    {
        return;
    }

    CommandStack& l_History = m_Session.GetHistory();

    Label("Motion");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "Motion", [](Trinity::RigidBodyComponent& body)
    {
        int l_Motion = static_cast<int>(body.Motion);
        const bool l_Changed = ImGui::Combo("##Motion", &l_Motion, "Static\0Kinematic\0Dynamic\0");
        body.Motion = static_cast<Trinity::BodyMotion>(l_Motion);

        return l_Changed;
    });

    if (entity.Get<Trinity::RigidBodyComponent>().Motion != Trinity::BodyMotion::Dynamic)
    {
        return;
    }

    Label("Mass");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "Mass", [](Trinity::RigidBodyComponent& body) { return ImGui::DragFloat("##Mass", &body.Mass, 0.05f, 0.0f, 1.0e6f, body.Mass > 0.0f ? "%.3f kg" : "From colliders", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("0 takes the mass from the colliders, at the density of water");
    }

    Label("Linear Damping");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "LinearDamping", [](Trinity::RigidBodyComponent& body) { return ImGui::DragFloat("##LinearDamping", &body.LinearDamping, 0.005f, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    Label("Angular Damping");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "AngularDamping", [](Trinity::RigidBodyComponent& body) { return ImGui::DragFloat("##AngularDamping", &body.AngularDamping, 0.005f, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    Label("Gravity Factor");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "GravityFactor", [](Trinity::RigidBodyComponent& body) { return ImGui::DragFloat("##GravityFactor", &body.GravityFactor, 0.01f, -100.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    Label("Continuous");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "ContinuousCollision", [](Trinity::RigidBodyComponent& body) { return ImGui::Checkbox("##ContinuousCollision", &body.ContinuousCollision); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Sweeps the body along its motion each step, so a fast one cannot pass through a thin one");
    }

    Label("Lock Position");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "LockPosition", [](Trinity::RigidBodyComponent& body) { return AxisCheckboxes("##LockPosition", body.LockPosition); });

    Label("Lock Rotation");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "LockRotation", [](Trinity::RigidBodyComponent& body) { return AxisCheckboxes("##LockRotation", body.LockRotation); });

    Label("Velocity");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "InitialLinearVelocity", [](Trinity::RigidBodyComponent& body) { return ImGui::DragFloat3("##InitialLinearVelocity", &body.InitialLinearVelocity.x, 0.05f, 0.0f, 0.0f, "%.3f m/s"); });

    Label("Spin");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "InitialAngularVelocity", [](Trinity::RigidBodyComponent& body)
    {
        glm::vec3 l_Degrees = glm::degrees(body.InitialAngularVelocity);
        if (!ImGui::DragFloat3("##InitialAngularVelocity", &l_Degrees.x, 1.0f, 0.0f, 0.0f, "%.1f\xC2\xB0/s"))
        {
            return false;
        }

        body.InitialAngularVelocity = glm::radians(l_Degrees);

        return true;
    });

    Label("Start Asleep");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "StartAsleep", [](Trinity::RigidBodyComponent& body) { return ImGui::Checkbox("##StartAsleep", &body.StartAsleep); });

    Label("Can Sleep");
    EditField<Trinity::RigidBodyComponent>(l_History, entity, "CanSleep", [](Trinity::RigidBodyComponent& body) { return ImGui::Checkbox("##CanSleep", &body.CanSleep); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Whether it may fall asleep once it has stayed within a few centimetres for half a second. A slow, small swing does that at each end, so untick it for a swinging lamp or a bobbing buoy");
    }
}

// Each collider's settings, then its shape, in metres before its entity's scale
void PropertiesPanel::DrawColliders(Trinity::Entity entity)
{
    CommandStack& l_History = m_Session.GetHistory();
    const Trinity::CollisionLayers l_Layers = m_Session.GetProject() != nullptr ? m_Session.GetProject()->GetCollisionLayers() : Trinity::CollisionLayers();

    if (entity.Has<Trinity::BoxColliderComponent>() && BeginComponent(entity, Trinity::BoxColliderComponent::c_TypeName, Trinity::Icons::c_CubeOutline))
    {
        DrawColliderSettings<Trinity::BoxColliderComponent>(l_History, entity, l_Layers);
        Label("Half Extents");
        EditField<Trinity::BoxColliderComponent>(l_History, entity, "HalfExtents", [](Trinity::BoxColliderComponent& box) { return ImGui::DragFloat3("##HalfExtents", &box.HalfExtents.x, 0.01f, 0.001f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
    }

    if (entity.Has<Trinity::SphereColliderComponent>() && BeginComponent(entity, Trinity::SphereColliderComponent::c_TypeName, Trinity::Icons::c_Globe))
    {
        DrawColliderSettings<Trinity::SphereColliderComponent>(l_History, entity, l_Layers);
        Label("Radius");
        EditField<Trinity::SphereColliderComponent>(l_History, entity, "Radius", [](Trinity::SphereColliderComponent& sphere) { return ImGui::DragFloat("##Radius", &sphere.Radius, 0.01f, 0.001f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
    }

    if (entity.Has<Trinity::CapsuleColliderComponent>() && BeginComponent(entity, Trinity::CapsuleColliderComponent::c_TypeName, Trinity::Icons::c_CubeOutline))
    {
        DrawColliderSettings<Trinity::CapsuleColliderComponent>(l_History, entity, l_Layers);
        Label("Radius");
        EditField<Trinity::CapsuleColliderComponent>(l_History, entity, "Radius", [](Trinity::CapsuleColliderComponent& capsule) { return ImGui::DragFloat("##Radius", &capsule.Radius, 0.01f, 0.001f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });

        // Never less than the two end caps
        Label("Height");
        EditField<Trinity::CapsuleColliderComponent>(l_History, entity, "Height", [](Trinity::CapsuleColliderComponent& capsule) { return ImGui::DragFloat("##Height", &capsule.Height, 0.01f, capsule.Radius * 2.0f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
    }

    if (entity.Has<Trinity::CylinderColliderComponent>() && BeginComponent(entity, Trinity::CylinderColliderComponent::c_TypeName, Trinity::Icons::c_CubeOutline))
    {
        DrawColliderSettings<Trinity::CylinderColliderComponent>(l_History, entity, l_Layers);
        Label("Radius");
        EditField<Trinity::CylinderColliderComponent>(l_History, entity, "Radius", [](Trinity::CylinderColliderComponent& cylinder) { return ImGui::DragFloat("##Radius", &cylinder.Radius, 0.01f, 0.001f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });

        Label("Height");
        EditField<Trinity::CylinderColliderComponent>(l_History, entity, "Height", [](Trinity::CylinderColliderComponent& cylinder) { return ImGui::DragFloat("##Height", &cylinder.Height, 0.01f, 0.001f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
    }

    if (entity.Has<Trinity::ConvexHullColliderComponent>() && BeginComponent(entity, Trinity::ConvexHullColliderComponent::c_TypeName, Trinity::Icons::c_CubeOutline))
    {
        DrawColliderSettings<Trinity::ConvexHullColliderComponent>(l_History, entity, l_Layers);
        DrawShapeSlot<Trinity::ConvexHullColliderComponent>(entity, Trinity::CollisionShapeKind::ConvexHull, c_ConvexHullPickerPopup, "A convex hull a model cooked around one of its meshes. Without one it collides with nothing");
    }

    if (entity.Has<Trinity::MeshColliderComponent>() && BeginComponent(entity, Trinity::MeshColliderComponent::c_TypeName, Trinity::Icons::c_CubeOutline))
    {
        DrawColliderSettings<Trinity::MeshColliderComponent>(l_History, entity, l_Layers);
        DrawShapeSlot<Trinity::MeshColliderComponent>(entity, Trinity::CollisionShapeKind::Mesh, c_CollisionMeshPickerPopup, "A model's mesh as triangles, cooked at import. Without one it collides with nothing");
        if (entity.Has<Trinity::RigidBodyComponent>() && entity.Get<Trinity::RigidBodyComponent>().Motion == Trinity::BodyMotion::Dynamic)
        {
            ImGui::TextWrapped("%s", std::format("{} Triangle meshes are for static and kinematic bodies. A dynamic body takes a convex hull", Trinity::Icons::c_Warning).c_str());
        }
    }
}

// The type, the body it joins, picked from the entities with a body or a collider or dropped from the Hierarchy, its frame, and its type's own settings, with a hinge's angles in degrees, kept as radians. A joint naming an entity no longer in the scene says so, and is left out when the scene runs
void PropertiesPanel::DrawJoint(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::JointComponent>() || !BeginComponent(entity, Trinity::JointComponent::c_TypeName, Trinity::Icons::c_Link))
    {
        return;
    }

    using Joint = Trinity::JointComponent;
    CommandStack& l_History = m_Session.GetHistory();
    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Joint l_Joint = entity.Get<Joint>();
    const bool l_Hinge = l_Joint.Type == Trinity::JointType::Hinge;
    const bool l_Slider = l_Joint.Type == Trinity::JointType::Slider;
    const bool l_Distance = l_Joint.Type == Trinity::JointType::Distance;

    Label("Type");
    EditField<Joint>(l_History, entity, "Type", [](Joint& joint)
    {
        int l_Type = static_cast<int>(joint.Type);
        const bool l_Changed = ImGui::Combo("##Type", &l_Type, "Fixed\0Hinge\0Slider\0Distance\0Point\0");
        joint.Type = static_cast<Trinity::JointType>(l_Type);

        return l_Changed;
    });

    const SlotText l_Preview = DescribeEntitySlot(l_Scene, l_Joint.ConnectedBody, "World");
    Label("Connected Body");
    EditField<Joint>(l_History, entity, "ConnectedBody", [&](Joint& joint) { return ConnectedBodyCombo(l_Scene, entity, joint.ConnectedBody, l_Preview, &Is3DBody); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("The body it joins, through that body's entity or one of its collider entities, or the world. An entity can be dropped here from the Hierarchy");
    }

    if (l_Preview.Broken)
    {
        ImGui::TextWrapped("%s", std::format("{} The entity it joins is no longer in the scene, so the joint is left out when the scene runs", Trinity::Icons::c_Warning).c_str());
    }

    Label("Anchor");
    EditField<Joint>(l_History, entity, "Anchor", [](Joint& joint) { return ImGui::DragFloat3("##Anchor", &joint.Anchor.x, 0.01f, 0.0f, 0.0f, "%.3f m"); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Where the joint is, from the entity, in its own space");
    }

    if (l_Hinge || l_Slider)
    {
        Label("Axis");
        EditField<Joint>(l_History, entity, "Axis", [](Joint& joint) { return ImGui::DragFloat3("##Axis", &joint.Axis.x, 0.01f, -1.0f, 1.0f, "%.3f"); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", l_Hinge ? "What it turns about, in the entity's own space" : "What it slides along, in the entity's own space");
        }
    }

    if (l_Distance)
    {
        Label("Connected Anchor");
        EditField<Joint>(l_History, entity, "ConnectedAnchor", [](Joint& joint) { return ImGui::DragFloat3("##ConnectedAnchor", &joint.ConnectedAnchor.x, 0.01f, 0.0f, 0.0f, "%.3f m"); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("The other end, on the connected entity in its own space, or in the world for a joint to the world");
        }
    }

    Label("Collide Connected");
    EditField<Joint>(l_History, entity, "CollideConnected", [](Joint& joint) { return ImGui::Checkbox("##CollideConnected", &joint.CollideConnected); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Whether the two bodies still collide with each other");
    }

    if (l_Hinge || l_Slider)
    {
        Label("Limits");
        EditField<Joint>(l_History, entity, "UseLimits", [](Joint& joint) { return ImGui::Checkbox("##UseLimits", &joint.UseLimits); });
        if (l_Joint.UseLimits && l_Hinge)
        {
            Label("Lower");
            EditField<Joint>(l_History, entity, "LowerLimit", [](Joint& joint)
            {
                float l_Degrees = glm::degrees(joint.LowerLimit);
                const bool l_Changed = ImGui::DragFloat("##LowerLimit", &l_Degrees, 0.5f, -180.0f, 0.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp);
                joint.LowerLimit = glm::radians(l_Degrees);

                return l_Changed;
            });

            Label("Upper");
            EditField<Joint>(l_History, entity, "UpperLimit", [](Joint& joint)
            {
                float l_Degrees = glm::degrees(joint.UpperLimit);
                const bool l_Changed = ImGui::DragFloat("##UpperLimit", &l_Degrees, 0.5f, 0.0f, 180.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp);
                joint.UpperLimit = glm::radians(l_Degrees);

                return l_Changed;
            });
        }
        else if (l_Joint.UseLimits)
        {
            Label("Lower");
            EditField<Joint>(l_History, entity, "LowerLimit", [](Joint& joint) { return ImGui::DragFloat("##LowerLimit", &joint.LowerLimit, 0.01f, -1.0e4f, 0.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });

            Label("Upper");
            EditField<Joint>(l_History, entity, "UpperLimit", [](Joint& joint) { return ImGui::DragFloat("##UpperLimit", &joint.UpperLimit, 0.01f, 0.0f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
        }

        Label("Motor");
        EditField<Joint>(l_History, entity, "UseMotor", [](Joint& joint) { return ImGui::Checkbox("##UseMotor", &joint.UseMotor); });
        if (l_Joint.UseMotor)
        {
            Label("Speed");
            EditField<Joint>(l_History, entity, "MotorSpeed", [l_Hinge](Joint& joint)
            {
                if (!l_Hinge)
                {
                    return ImGui::DragFloat("##MotorSpeed", &joint.MotorSpeed, 0.01f, -1.0e4f, 1.0e4f, "%.3f m/s", ImGuiSliderFlags_AlwaysClamp);
                }

                float l_Degrees = glm::degrees(joint.MotorSpeed);
                const bool l_Changed = ImGui::DragFloat("##MotorSpeed", &l_Degrees, 1.0f, -1.0e5f, 1.0e5f, "%.1f\xC2\xB0/s", ImGuiSliderFlags_AlwaysClamp);
                joint.MotorSpeed = glm::radians(l_Degrees);

                return l_Changed;
            });

            Label(l_Hinge ? "Max Torque" : "Max Force");
            EditField<Joint>(l_History, entity, "MotorMaxForce", [l_Hinge](Joint& joint) { return ImGui::DragFloat("##MotorMaxForce", &joint.MotorMaxForce, 1.0f, 0.0f, 1.0e9f, l_Hinge ? "%.1f N m" : "%.1f N", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });
        }
    }

    if (l_Distance)
    {
        Label("Min Distance");
        EditField<Joint>(l_History, entity, "MinDistance", [](Joint& joint) { return ImGui::DragFloat("##MinDistance", &joint.MinDistance, 0.01f, -1.0f, 1.0e4f, joint.MinDistance >= 0.0f ? "%.3f m" : "From start", ImGuiSliderFlags_AlwaysClamp); });

        Label("Max Distance");
        EditField<Joint>(l_History, entity, "MaxDistance", [](Joint& joint) { return ImGui::DragFloat("##MaxDistance", &joint.MaxDistance, 0.01f, -1.0f, 1.0e4f, joint.MaxDistance >= 0.0f ? "%.3f m" : "From start", ImGuiSliderFlags_AlwaysClamp); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Below 0, the distance between the two ends as the scene starts");
        }

        Label("Spring");
        EditField<Joint>(l_History, entity, "SpringFrequency", [](Joint& joint) { return ImGui::DragFloat("##SpringFrequency", &joint.SpringFrequency, 0.01f, 0.0f, 1000.0f, joint.SpringFrequency > 0.0f ? "%.2f Hz" : "Rigid", ImGuiSliderFlags_AlwaysClamp); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("How many times a second a body on the spring bounces, 0 for a rigid range");
        }

        Label("Damping");
        EditField<Joint>(l_History, entity, "SpringDamping", [](Joint& joint) { return ImGui::DragFloat("##SpringDamping", &joint.SpringDamping, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("0 bounces for ever, 1 settles as fast as it can without overshooting");
        }
    }

    Label("Break Force");
    EditField<Joint>(l_History, entity, "BreakForce", [](Joint& joint) { return ImGui::DragFloat("##BreakForce", &joint.BreakForce, 1.0f, 0.0f, 1.0e9f, joint.BreakForce > 0.0f ? "%.1f N" : "Unbreakable", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });

    if (l_Joint.Type == Trinity::JointType::Fixed || l_Hinge || l_Slider)
    {
        Label("Break Torque");
        EditField<Joint>(l_History, entity, "BreakTorque", [](Joint& joint) { return ImGui::DragFloat("##BreakTorque", &joint.BreakTorque, 1.0f, 0.0f, 1.0e9f, joint.BreakTorque > 0.0f ? "%.1f N m" : "Unbreakable", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });
    }
}

// As a 3D body's, in the XY plane: the settings past the motion show for a dynamic body alone, and the initial spin is in degrees per second counter-clockwise, kept as radians. An entity with a RigidBody as well keeps its 3D body, which it says
void PropertiesPanel::DrawRigidBody2D(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::RigidBody2DComponent>() || !BeginComponent(entity, Trinity::RigidBody2DComponent::c_TypeName, Trinity::Icons::c_Arrows))
    {
        return;
    }

    using Body = Trinity::RigidBody2DComponent;
    CommandStack& l_History = m_Session.GetHistory();

    if (entity.Has<Trinity::RigidBodyComponent>())
    {
        ImGui::TextWrapped("%s", std::format("{} The entity has a RigidBody too, so it keeps its 3D body and this is left out when the scene runs", Trinity::Icons::c_Warning).c_str());
    }

    Label("Motion");
    EditField<Body>(l_History, entity, "Motion", [](Body& body)
    {
        int l_Motion = static_cast<int>(body.Motion);
        const bool l_Changed = ImGui::Combo("##Motion", &l_Motion, "Static\0Kinematic\0Dynamic\0");
        body.Motion = static_cast<Trinity::BodyMotion>(l_Motion);

        return l_Changed;
    });

    if (entity.Get<Body>().Motion != Trinity::BodyMotion::Dynamic)
    {
        return;
    }

    Label("Mass");
    EditField<Body>(l_History, entity, "Mass", [](Body& body) { return ImGui::DragFloat("##Mass", &body.Mass, 0.05f, 0.0f, 1.0e6f, body.Mass > 0.0f ? "%.3f kg" : "From colliders", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("0 takes the mass from the colliders, at 1 kg per square metre");
    }

    Label("Linear Damping");
    EditField<Body>(l_History, entity, "LinearDamping", [](Body& body) { return ImGui::DragFloat("##LinearDamping", &body.LinearDamping, 0.005f, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    Label("Angular Damping");
    EditField<Body>(l_History, entity, "AngularDamping", [](Body& body) { return ImGui::DragFloat("##AngularDamping", &body.AngularDamping, 0.005f, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    Label("Gravity Factor");
    EditField<Body>(l_History, entity, "GravityFactor", [](Body& body) { return ImGui::DragFloat("##GravityFactor", &body.GravityFactor, 0.01f, -100.0f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    Label("Continuous");
    EditField<Body>(l_History, entity, "ContinuousCollision", [](Body& body) { return ImGui::Checkbox("##ContinuousCollision", &body.ContinuousCollision); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Static bodies always stop a fast one. This sweeps it against other moving bodies too, at some cost");
    }

    Label("Lock Rotation");
    EditField<Body>(l_History, entity, "LockRotation", [](Body& body) { return ImGui::Checkbox("##LockRotation", &body.LockRotation); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Keeps it from turning. A 2D body cannot be locked along X or Y");
    }

    Label("Velocity");
    EditField<Body>(l_History, entity, "InitialLinearVelocity", [](Body& body) { return ImGui::DragFloat2("##InitialLinearVelocity", &body.InitialLinearVelocity.x, 0.05f, 0.0f, 0.0f, "%.3f m/s"); });

    Label("Spin");
    EditField<Body>(l_History, entity, "InitialAngularVelocity", [](Body& body)
    {
        float l_Degrees = glm::degrees(body.InitialAngularVelocity);
        if (!ImGui::DragFloat("##InitialAngularVelocity", &l_Degrees, 1.0f, 0.0f, 0.0f, "%.1f\xC2\xB0/s"))
        {
            return false;
        }

        body.InitialAngularVelocity = glm::radians(l_Degrees);

        return true;
    });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Counter-clockwise, looking down Z");
    }

    Label("Start Asleep");
    EditField<Body>(l_History, entity, "StartAsleep", [](Body& body) { return ImGui::Checkbox("##StartAsleep", &body.StartAsleep); });

    Label("Can Sleep");
    EditField<Body>(l_History, entity, "CanSleep", [](Body& body) { return ImGui::Checkbox("##CanSleep", &body.CanSleep); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Whether it may fall asleep once it has stayed nearly still for half a second. Untick it for something that must never stop, such as a slow swing");
    }
}

// Each 2D collider's settings, then its shape, in metres before its entity's scale. A polygon's points are listed one to a row, from 3 to 8, each removable while more than 3 are left
void PropertiesPanel::DrawColliders2D(Trinity::Entity entity)
{
    CommandStack& l_History = m_Session.GetHistory();
    const Trinity::CollisionLayers l_Layers = m_Session.GetProject() != nullptr ? m_Session.GetProject()->GetCollisionLayers() : Trinity::CollisionLayers();

    if (entity.Has<Trinity::BoxCollider2DComponent>() && BeginComponent(entity, Trinity::BoxCollider2DComponent::c_TypeName, Trinity::Icons::c_CubeOutline))
    {
        DrawCollider2DSettings<Trinity::BoxCollider2DComponent>(l_History, entity, l_Layers);
        Label("Half Extents");
        EditField<Trinity::BoxCollider2DComponent>(l_History, entity, "HalfExtents", [](Trinity::BoxCollider2DComponent& box) { return ImGui::DragFloat2("##HalfExtents", &box.HalfExtents.x, 0.01f, 0.001f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
    }

    if (entity.Has<Trinity::CircleCollider2DComponent>() && BeginComponent(entity, Trinity::CircleCollider2DComponent::c_TypeName, Trinity::Icons::c_Globe))
    {
        DrawCollider2DSettings<Trinity::CircleCollider2DComponent>(l_History, entity, l_Layers);
        Label("Radius");
        EditField<Trinity::CircleCollider2DComponent>(l_History, entity, "Radius", [](Trinity::CircleCollider2DComponent& circle) { return ImGui::DragFloat("##Radius", &circle.Radius, 0.01f, 0.001f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
    }

    if (entity.Has<Trinity::CapsuleCollider2DComponent>() && BeginComponent(entity, Trinity::CapsuleCollider2DComponent::c_TypeName, Trinity::Icons::c_CubeOutline))
    {
        DrawCollider2DSettings<Trinity::CapsuleCollider2DComponent>(l_History, entity, l_Layers);
        Label("Radius");
        EditField<Trinity::CapsuleCollider2DComponent>(l_History, entity, "Radius", [](Trinity::CapsuleCollider2DComponent& capsule) { return ImGui::DragFloat("##Radius", &capsule.Radius, 0.01f, 0.001f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });

        // Never less than the two end caps
        Label("Height");
        EditField<Trinity::CapsuleCollider2DComponent>(l_History, entity, "Height", [](Trinity::CapsuleCollider2DComponent& capsule) { return ImGui::DragFloat("##Height", &capsule.Height, 0.01f, capsule.Radius * 2.0f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
    }

    if (!entity.Has<Trinity::PolygonCollider2DComponent>() || !BeginComponent(entity, Trinity::PolygonCollider2DComponent::c_TypeName, Trinity::Icons::c_CubeOutline))
    {
        return;
    }

    using Polygon = Trinity::PolygonCollider2DComponent;
    DrawCollider2DSettings<Polygon>(l_History, entity, l_Layers);

    const std::size_t l_Count = entity.Get<Polygon>().Points.size();
    const float l_Button = ImGui::GetFrameHeight();
    bool l_Removed = false;
    for (std::size_t it_Point = 0; it_Point < l_Count && !l_Removed; ++it_Point)
    {
        // Each point's edits are named for it, so a drag of one never merges with another's or with a point added or removed
        const std::string l_Name = std::format("Point {}", it_Point + 1);
        ImGui::PushID(static_cast<int>(it_Point));
        Label(l_Name.c_str());
        ImGui::SetNextItemWidth(l_Count > 3 ? -(l_Button + ImGui::GetStyle().ItemSpacing.x) : -FLT_MIN);
        EditField<Polygon>(l_History, entity, l_Name, [it_Point](Polygon& polygon) { return ImGui::DragFloat2("##Point", &polygon.Points[it_Point].x, 0.01f, 0.0f, 0.0f, "%.3f m"); });

        if (l_Count > 3)
        {
            ImGui::SameLine();
            EditField<Polygon>(l_History, entity, "Points", [it_Point, l_Button, &l_Removed](Polygon& polygon)
            {
                if (!ImGui::Button(Trinity::Icons::c_Trash, ImVec2(l_Button, l_Button)))
                {
                    return false;
                }

                polygon.Points.erase(polygon.Points.begin() + static_cast<std::ptrdiff_t>(it_Point));
                l_Removed = true;

                return true;
            });
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("Remove this point");
            }
        }

        ImGui::PopID();
    }

    // A new point goes halfway between the last and the first, so the shape keeps its outline until it is dragged
    ImGui::BeginDisabled(l_Count >= Polygon::c_MaxPoints);
    EditField<Polygon>(l_History, entity, "Points", [](Polygon& polygon)
    {
        if (!ImGui::Button("Add Point", ImVec2(-FLT_MIN, 0.0f)) || polygon.Points.empty())
        {
            return false;
        }

        polygon.Points.push_back((polygon.Points.back() + polygon.Points.front()) * 0.5f);

        return true;
    });
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("The collider is the convex hull of its points, in its own space, so a point inside it or in line with two others is left out. 8 at most");
    }

    Label("Radius");
    EditField<Polygon>(l_History, entity, "Radius", [](Polygon& polygon) { return ImGui::DragFloat("##Radius", &polygon.Radius, 0.005f, 0.0f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Rounds the corners, and grows the shape outward by as much");
    }
}

// As a 3D joint's: the type, the 2D body it joins, picked from the entities with a 2D body or collider or dropped from the Hierarchy, its anchor and axis in its entity's own XY plane, and its type's own settings, with a revolute joint's angles and a revolute or wheel motor's speed in degrees, kept as radians. A joint naming an entity no longer in the scene says so, and is left out when the scene runs
void PropertiesPanel::DrawJoint2D(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::Joint2DComponent>() || !BeginComponent(entity, Trinity::Joint2DComponent::c_TypeName, Trinity::Icons::c_Link))
    {
        return;
    }

    using Joint = Trinity::Joint2DComponent;
    CommandStack& l_History = m_Session.GetHistory();
    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Joint l_Joint = entity.Get<Joint>();
    const bool l_Revolute = l_Joint.Type == Trinity::Joint2DType::Revolute;
    const bool l_Prismatic = l_Joint.Type == Trinity::Joint2DType::Prismatic;
    const bool l_Distance = l_Joint.Type == Trinity::Joint2DType::Distance;
    const bool l_Wheel = l_Joint.Type == Trinity::Joint2DType::Wheel;
    // Turning about the anchor, measured in degrees
    const bool l_Turns = l_Revolute || l_Wheel;

    Label("Type");
    EditField<Joint>(l_History, entity, "Type", [](Joint& joint)
    {
        int l_Type = static_cast<int>(joint.Type);
        const bool l_Changed = ImGui::Combo("##Type", &l_Type, "Revolute\0Prismatic\0Distance\0Weld\0Wheel\0");
        joint.Type = static_cast<Trinity::Joint2DType>(l_Type);

        return l_Changed;
    });

    const SlotText l_Preview = DescribeEntitySlot(l_Scene, l_Joint.ConnectedBody, "World");
    Label("Connected Body");
    EditField<Joint>(l_History, entity, "ConnectedBody", [&](Joint& joint) { return ConnectedBodyCombo(l_Scene, entity, joint.ConnectedBody, l_Preview, &Is2DBody); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("The 2D body it joins, through that body's entity or one of its 2D collider entities, or the world. An entity can be dropped here from the Hierarchy");
    }

    if (l_Preview.Broken)
    {
        ImGui::TextWrapped("%s", std::format("{} The entity it joins is no longer in the scene, so the joint is left out when the scene runs", Trinity::Icons::c_Warning).c_str());
    }

    Label("Anchor");
    EditField<Joint>(l_History, entity, "Anchor", [](Joint& joint) { return ImGui::DragFloat2("##Anchor", &joint.Anchor.x, 0.01f, 0.0f, 0.0f, "%.3f m"); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Where the joint is, from the entity, in its own space");
    }

    if (l_Prismatic || l_Wheel)
    {
        Label("Axis");
        EditField<Joint>(l_History, entity, "Axis", [](Joint& joint) { return ImGui::DragFloat2("##Axis", &joint.Axis.x, 0.01f, -1.0f, 1.0f, "%.3f"); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", l_Prismatic ? "What it slides along, in the entity's own space" : "What the wheel's suspension travels along, in the entity's own space");
        }
    }

    if (l_Distance)
    {
        Label("Connected Anchor");
        EditField<Joint>(l_History, entity, "ConnectedAnchor", [](Joint& joint) { return ImGui::DragFloat2("##ConnectedAnchor", &joint.ConnectedAnchor.x, 0.01f, 0.0f, 0.0f, "%.3f m"); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("The other end, on the connected entity in its own space, or in the world for a joint to the world");
        }
    }

    Label("Collide Connected");
    EditField<Joint>(l_History, entity, "CollideConnected", [](Joint& joint) { return ImGui::Checkbox("##CollideConnected", &joint.CollideConnected); });
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Whether the two bodies still collide with each other");
    }

    if (l_Revolute || l_Prismatic || l_Wheel)
    {
        Label("Limits");
        EditField<Joint>(l_History, entity, "UseLimits", [](Joint& joint) { return ImGui::Checkbox("##UseLimits", &joint.UseLimits); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", l_Revolute ? "How far it turns either way from where it started" : (l_Prismatic ? "How far it slides either way from where it started" : "How far the wheel's suspension travels either way from where it started"));
        }

        if (l_Joint.UseLimits && l_Revolute)
        {
            Label("Lower");
            EditField<Joint>(l_History, entity, "LowerLimit", [](Joint& joint)
            {
                float l_Degrees = glm::degrees(joint.LowerLimit);
                const bool l_Changed = ImGui::DragFloat("##LowerLimit", &l_Degrees, 0.5f, -178.2f, 0.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp);
                joint.LowerLimit = glm::radians(l_Degrees);

                return l_Changed;
            });

            Label("Upper");
            EditField<Joint>(l_History, entity, "UpperLimit", [](Joint& joint)
            {
                float l_Degrees = glm::degrees(joint.UpperLimit);
                const bool l_Changed = ImGui::DragFloat("##UpperLimit", &l_Degrees, 0.5f, 0.0f, 178.2f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp);
                joint.UpperLimit = glm::radians(l_Degrees);

                return l_Changed;
            });
        }
        else if (l_Joint.UseLimits)
        {
            Label("Lower");
            EditField<Joint>(l_History, entity, "LowerLimit", [](Joint& joint) { return ImGui::DragFloat("##LowerLimit", &joint.LowerLimit, 0.01f, -1.0e4f, 0.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });

            Label("Upper");
            EditField<Joint>(l_History, entity, "UpperLimit", [](Joint& joint) { return ImGui::DragFloat("##UpperLimit", &joint.UpperLimit, 0.01f, 0.0f, 1.0e4f, "%.3f m", ImGuiSliderFlags_AlwaysClamp); });
        }

        Label("Motor");
        EditField<Joint>(l_History, entity, "UseMotor", [](Joint& joint) { return ImGui::Checkbox("##UseMotor", &joint.UseMotor); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", l_Prismatic ? "Drives it along its axis" : "Turns it about its anchor, counter-clockwise for a positive speed");
        }

        if (l_Joint.UseMotor)
        {
            Label("Speed");
            EditField<Joint>(l_History, entity, "MotorSpeed", [l_Turns](Joint& joint)
            {
                if (!l_Turns)
                {
                    return ImGui::DragFloat("##MotorSpeed", &joint.MotorSpeed, 0.01f, -1.0e4f, 1.0e4f, "%.3f m/s", ImGuiSliderFlags_AlwaysClamp);
                }

                float l_Degrees = glm::degrees(joint.MotorSpeed);
                const bool l_Changed = ImGui::DragFloat("##MotorSpeed", &l_Degrees, 1.0f, -1.0e5f, 1.0e5f, "%.1f\xC2\xB0/s", ImGuiSliderFlags_AlwaysClamp);
                joint.MotorSpeed = glm::radians(l_Degrees);

                return l_Changed;
            });

            Label(l_Turns ? "Max Torque" : "Max Force");
            EditField<Joint>(l_History, entity, "MotorMaxForce", [l_Turns](Joint& joint) { return ImGui::DragFloat("##MotorMaxForce", &joint.MotorMaxForce, 1.0f, 0.0f, 1.0e9f, l_Turns ? "%.1f N m" : "%.1f N", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });
        }
    }

    if (l_Distance)
    {
        Label("Min Distance");
        EditField<Joint>(l_History, entity, "MinDistance", [](Joint& joint) { return ImGui::DragFloat("##MinDistance", &joint.MinDistance, 0.01f, -1.0f, 1.0e4f, joint.MinDistance >= 0.0f ? "%.3f m" : "From start", ImGuiSliderFlags_AlwaysClamp); });

        Label("Max Distance");
        EditField<Joint>(l_History, entity, "MaxDistance", [](Joint& joint) { return ImGui::DragFloat("##MaxDistance", &joint.MaxDistance, 0.01f, -1.0f, 1.0e4f, joint.MaxDistance >= 0.0f ? "%.3f m" : "From start", ImGuiSliderFlags_AlwaysClamp); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Below 0, the distance between the two ends as the scene starts. Without a spring the ends move freely between the two, and are held rigid when they match");
        }
    }

    if (l_Distance || l_Wheel)
    {
        Label("Spring");
        EditField<Joint>(l_History, entity, "SpringFrequency", [](Joint& joint) { return ImGui::DragFloat("##SpringFrequency", &joint.SpringFrequency, 0.01f, 0.0f, 1000.0f, joint.SpringFrequency > 0.0f ? "%.2f Hz" : "None", ImGuiSliderFlags_AlwaysClamp); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("%s", l_Distance ? "How many times a second a body on the spring bounces. It pulls towards the middle of the range, which still stops it at either end" : "How many times a second the wheel bounces on its suspension. Without one the wheel travels freely along its axis, within any limits");
        }

        Label("Damping");
        EditField<Joint>(l_History, entity, "SpringDamping", [](Joint& joint) { return ImGui::DragFloat("##SpringDamping", &joint.SpringDamping, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("0 bounces for ever, 1 settles as fast as it can without overshooting");
        }
    }

    Label("Break Force");
    EditField<Joint>(l_History, entity, "BreakForce", [](Joint& joint) { return ImGui::DragFloat("##BreakForce", &joint.BreakForce, 1.0f, 0.0f, 1.0e9f, joint.BreakForce > 0.0f ? "%.1f N" : "Unbreakable", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });

    // A revolute joint resists turning only at its limits, and a wheel never, since motors are left out
    if (l_Joint.Type == Trinity::Joint2DType::Weld || l_Prismatic || (l_Revolute && l_Joint.UseLimits))
    {
        Label("Break Torque");
        EditField<Joint>(l_History, entity, "BreakTorque", [](Joint& joint) { return ImGui::DragFloat("##BreakTorque", &joint.BreakTorque, 1.0f, 0.0f, 1.0e9f, joint.BreakTorque > 0.0f ? "%.1f N m" : "Unbreakable", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic); });
    }
}

// A slot for the cooked shape a hull or mesh collider takes, and once the shape is loaded, as the Scene panel's drawing of the selection loads it, why it failed if it did. When the model of the entity's Mesh Renderer cooked a shape of this kind for its mesh, and the collider has another, a button takes that one
template<typename T>
void PropertiesPanel::DrawShapeSlot(Trinity::Entity entity, Trinity::CollisionShapeKind kind, const char* popup, const char* hint)
{
    CommandStack& l_History = m_Session.GetHistory();
    const auto a_Set = [&l_History, entity](Trinity::UUID shape)
    {
        T l_Value = entity.Get<T>();
        l_Value.Shape = shape;
        l_History.Execute(Trinity::CreateScope<SetComponentCommand<T>>(entity.GetUUID(), std::move(l_Value), "Shape"));
        l_History.EndMerge();
    };

    const Trinity::UUID l_Shape = entity.Get<T>().Shape;
    DrawAssetSlot("Shape", popup, Trinity::GetCollisionShapeAssetType(kind), l_Shape, a_Set, hint);
    if (l_Shape && Trinity::AssetManager::GetState(l_Shape) == Trinity::AssetState::Failed)
    {
        Label("");
        ImGui::TextDisabled("%s", std::format("{} Could not be loaded, as the Console says", Trinity::Icons::c_Warning).c_str());
    }

    const Trinity::UUID l_Matching = entity.Has<Trinity::MeshRendererComponent>() ? FindCollisionShape(m_Session.GetRegistry(), entity.Get<Trinity::MeshRendererComponent>().Mesh, kind) : Trinity::UUID();
    if (l_Matching && l_Matching != l_Shape)
    {
        Label("");
        if (ImGui::Button(std::format("{} Match Mesh Renderer", Trinity::Icons::c_Cube).c_str(), ImVec2(-FLT_MIN, 0.0f)))
        {
            a_Set(l_Matching);
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Takes the %s its model cooked for the Mesh Renderer's mesh", kind == Trinity::CollisionShapeKind::ConvexHull ? "convex hull" : "triangle mesh");
        }
    }
}

// Unity's Add Component: every registered component the entity is without, except Transform, which it always has, under its category, and every script class loaded code has under Scripts, which adds it last with its fields at their defaults and the Script component too when the entity is without it. The search is focused as it opens, and what is typed narrows them to one list across all of them. Up and Down move the highlight, which the mouse also moves, and Enter adds it. As Unity's New Script does, a name typed that no loaded class has makes a C# or C++ script by that name from its template, adds it at once and opens it in the script editor, and it runs once its build loads the class
void PropertiesPanel::DrawAddComponent(Trinity::Entity entity)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float l_Width = ImGui::GetFontSize() * 12.0f;
    ImGui::SetCursorPosX(std::max((ImGui::GetContentRegionAvail().x - l_Width) * 0.5f, 0.0f) + ImGui::GetCursorPosX());
    if (ImGui::Button("Add Component", ImVec2(l_Width, 0.0f)))
    {
        m_ComponentFilter.clear();
        m_ComponentHighlight = 0;
        ImGui::OpenPopup(c_AddComponentPopup);
    }

    if (!ImGui::BeginPopup(c_AddComponentPopup))
    {
        return;
    }

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    const ImGuiStyle& l_Style = ImGui::GetStyle();
    const float l_PopupWidth = ImGui::GetFontSize() * 20.0f;
    const ImGuiInputTextFlags l_Flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackEdit;
    ImGui::SetNextItemWidth(l_PopupWidth);
    const bool l_Enter = ImGui::InputTextWithHint("##Filter", std::format("{} Search components and scripts", Trinity::Icons::c_Search).c_str(), &m_ComponentFilter, l_Flags, &PropertiesPanel::OnComponentFilterEdit, this);

    const std::vector<ComponentChoice> l_Choices = GetComponentChoices(entity, m_ComponentFilter);
    const bool l_Searching = !m_ComponentFilter.empty();
    m_ComponentHighlight = l_Choices.empty() ? 0 : std::clamp(m_ComponentHighlight, 0, static_cast<int>(l_Choices.size()) - 1);
    std::optional<std::size_t> l_Picked = l_Enter && !l_Choices.empty() ? std::optional(static_cast<std::size_t>(m_ComponentHighlight)) : std::nullopt;

    if (l_Choices.empty())
    {
        ImGui::TextDisabled("%s", l_Searching ? "No component or script matches" : "The entity has every component there is, and no script classes are loaded");
    }
    else
    {
        // Headers too while not searching, each a separator's height
        float l_Height = static_cast<float>(l_Choices.size()) * ImGui::GetTextLineHeightWithSpacing();
        if (!l_Searching)
        {
            std::size_t l_Categories = 0;
            for (std::size_t it_Index = 0; it_Index < l_Choices.size(); ++it_Index)
            {
                l_Categories += it_Index == 0 || l_Choices[it_Index].Category != l_Choices[it_Index - 1].Category ? 1 : 0;
            }

            l_Height += static_cast<float>(l_Categories) * (ImGui::GetTextLineHeight() + l_Style.SeparatorTextPadding.y * 2.0f + l_Style.ItemSpacing.y);
        }

        if (ImGui::BeginChild("##Choices", ImVec2(l_PopupWidth, std::min(l_Height, c_ComponentChoiceLines * ImGui::GetTextLineHeightWithSpacing()))))
        {
            const ImGuiIO& l_IO = ImGui::GetIO();
            for (std::size_t it_Index = 0; it_Index < l_Choices.size(); ++it_Index)
            {
                const ComponentChoice& l_Choice = l_Choices[it_Index];
                if (!l_Searching && (it_Index == 0 || l_Choice.Category != l_Choices[it_Index - 1].Category))
                {
                    ImGui::SeparatorText(std::string(l_Choice.Category).c_str());
                }

                const bool l_Highlighted = static_cast<int>(it_Index) == m_ComponentHighlight;
                if (ImGui::Selectable(std::format("{}###{}{}", l_Choice.Label, l_Choice.Category, l_Choice.Script != nullptr ? std::string_view(l_Choice.Script->Name) : l_Choice.Component).c_str(), l_Highlighted))
                {
                    l_Picked = it_Index;
                }

                // The mouse moves the highlight only as it moves, so it never takes it back from Up and Down
                if (ImGui::IsItemHovered() && (l_IO.MouseDelta.x != 0.0f || l_IO.MouseDelta.y != 0.0f))
                {
                    m_ComponentHighlight = static_cast<int>(it_Index);
                }

                if (l_Choice.Script != nullptr)
                {
                    ImGui::SetItemTooltip("%s", std::string(std::string_view(l_Choice.Script->Name)).c_str());
                }

                if (l_Highlighted && std::exchange(m_ScrollToHighlight, false))
                {
                    ImGui::SetScrollHereY();
                }
            }
        }

        ImGui::EndChild();
    }

    // A new script by the name typed, in each language the project has scripts in, unless a loaded class already has that name
    std::optional<Trinity::ScriptLanguage> l_NewScript;
    const Trinity::Project* l_Project = m_Session.GetProject();
    if (l_Searching && l_Project != nullptr && EditorSession::IsClassName(m_ComponentFilter))
    {
        bool l_Separated = false;
        for (const Trinity::ScriptLanguage it_Language : { Trinity::ScriptLanguage::CSharp, Trinity::ScriptLanguage::Cpp })
        {
            const bool l_CSharp = it_Language == Trinity::ScriptLanguage::CSharp;
            const bool l_Taken = Trinity::ScriptClassRegistry::Find(it_Language, m_Session.GetScriptClassName(it_Language, m_ComponentFilter)) != nullptr;
            if ((l_CSharp ? !l_Project->HasAssembly() : !l_Project->HasModule()) || l_Taken)
            {
                continue;
            }

            if (!std::exchange(l_Separated, true))
            {
                ImGui::Separator();
            }

            if (ImGui::Selectable(std::format("{} New {} Script \"{}\"", Trinity::Icons::c_FileCode, GetScriptLanguageLabel(it_Language), m_ComponentFilter).c_str()))
            {
                l_NewScript = it_Language;
            }

            ImGui::SetItemTooltip("%s", std::format("Writes {}/{}{} from its template, adds the script and opens it in the script editor. It runs once its build loads the class", l_CSharp ? EditorSession::c_ScriptsFolder : EditorSession::c_SourceFolder, m_ComponentFilter, l_CSharp ? ".cs" : ".cpp").c_str());
        }
    }

    if (l_Picked)
    {
        const ComponentChoice& l_Choice = l_Choices[*l_Picked];
        if (l_Choice.Script != nullptr)
        {
            m_Session.GetHistory().Execute(Trinity::CreateScope<AddScriptCommand>(entity.GetUUID(), Trinity::ScriptClassRegistry::CreateEntry(*l_Choice.Script)));
        }
        else
        {
            m_Session.GetHistory().Execute(Trinity::CreateScope<AddComponentCommand>(entity.GetUUID(), std::string(l_Choice.Component)));
        }

        ImGui::CloseCurrentPopup();
    }
    else if (l_NewScript)
    {
        const std::string_view l_Folder = *l_NewScript == Trinity::ScriptLanguage::CSharp ? EditorSession::c_ScriptsFolder : EditorSession::c_SourceFolder;
        if (const std::optional<std::string> l_Path = m_Session.CreateScript(l_Folder, *l_NewScript, m_ComponentFilter))
        {
            Trinity::ScriptEntry l_Entry;
            l_Entry.ClassName.assign(m_Session.GetScriptClassName(*l_NewScript, m_ComponentFilter));
            l_Entry.Language = *l_NewScript;
            m_Session.GetHistory().Execute(Trinity::CreateScope<AddScriptCommand>(entity.GetUUID(), std::move(l_Entry)));
            static_cast<void>(m_Session.OpenScript(*l_Path));
        }

        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Up and Down move the highlight through the choices, and typing puts it back on the first, as in the search of Unity's Add Component
int PropertiesPanel::OnComponentFilterEdit(ImGuiInputTextCallbackData* data)
{
    PropertiesPanel& l_Panel = *static_cast<PropertiesPanel*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        l_Panel.m_ComponentHighlight += data->EventKey == ImGuiKey_UpArrow ? -1 : 1;
        l_Panel.m_ScrollToHighlight = true;
    }
    else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        l_Panel.m_ComponentHighlight = 0;
        l_Panel.m_ScrollToHighlight = true;
    }

    return 0;
}

// Each script by its class, with a box that turns it on and off and its fields below. Right-clicking a script resets its fields to its class's defaults, moves or removes it. A class no loaded code has is marked, and its values are shown as they were read. Every change goes to a copy of the component and becomes one command once the section is drawn, since the section reads the component
void PropertiesPanel::DrawScripts(Trinity::Entity entity)
{
    if (!entity.Has<Trinity::ScriptComponent>() || !BeginComponent(entity, Trinity::ScriptComponent::c_TypeName, Trinity::Icons::c_Terminal))
    {
        return;
    }

    enum class Action : std::uint8_t
    {
        None,
        Reset,
        MoveUp,
        MoveDown,
        Remove
    };

    Trinity::ScriptComponent l_Value = entity.Get<Trinity::ScriptComponent>();
    std::string l_Change;
    Action l_Action = Action::None;
    std::size_t l_ActionIndex = 0;
    // The script to open in the script editor, by its language and class, once the component is drawn
    std::optional<std::pair<Trinity::ScriptLanguage, std::string>> l_Edit;
    Trinity::SceneRuntime* l_Runtime = m_Session.GetRuntime();

    if (l_Value.Scripts.empty())
    {
        ImGui::TextDisabled("No scripts yet. Add Script below adds one");
    }

    for (std::size_t it_Index = 0; it_Index < l_Value.Scripts.size(); ++it_Index)
    {
        Trinity::ScriptEntry& l_Entry = l_Value.Scripts[it_Index];
        const std::string l_Label = GetScriptLabel(l_Entry);
        const bool l_Known = Trinity::ScriptClassRegistry::Find(l_Entry.Language, l_Entry.ClassName) != nullptr;
        const bool l_Stopped = l_Runtime != nullptr && l_Runtime->GetScripts().IsFaulted(entity, it_Index);
        const float l_RowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        ImGui::PushID(static_cast<int>(it_Index));
        if (ImGui::Checkbox("##Enabled", &l_Entry.Enabled))
        {
            l_Change = std::format("{} Enabled", l_Label);
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            ImGui::SetTooltip("Whether it runs while the scene plays");
        }

        ImGui::SameLine();
        // The badge for a stopped script sits over the header's right end
        const bool l_Open = ImGui::TreeNodeEx("##Script", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap, "%s", (l_Known ? l_Label : std::format("{} {}", Trinity::Icons::c_Warning, l_Label)).c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            const std::string_view l_Name(l_Entry.ClassName);
            ImGui::SetTooltip("%s", (l_Known ? std::format("{}, in {}. Double-click to edit it", l_Name, GetScriptLanguageLabel(l_Entry.Language)) : std::format("{}, in {}. No loaded code has this class, so its values are kept as they were read. Double-click to edit it", l_Name, GetScriptLanguageLabel(l_Entry.Language))).c_str());
        }

        // As Unity opens a script from its component, the two clicks leaving the header open or closed as it was
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            l_Edit = std::pair(l_Entry.Language, std::string(std::string_view(l_Entry.ClassName)));
        }

        if (ImGui::BeginPopupContextItem())
        {
            // First, as in Unity's component menu, for a class whose defaults are known
            if (ImGui::MenuItem(std::format("{} Reset", Trinity::Icons::c_Undo).c_str(), nullptr, false, l_Known))
            {
                l_Action = Action::Reset;
                l_ActionIndex = it_Index;
            }

            ImGui::Separator();
            if (ImGui::MenuItem(std::format("{} Move Up", Trinity::Icons::c_ArrowUp).c_str(), nullptr, false, it_Index > 0))
            {
                l_Action = Action::MoveUp;
                l_ActionIndex = it_Index;
            }

            if (ImGui::MenuItem("Move Down", nullptr, false, it_Index + 1 < l_Value.Scripts.size()))
            {
                l_Action = Action::MoveDown;
                l_ActionIndex = it_Index;
            }

            ImGui::Separator();
            if (ImGui::MenuItem(std::format("{} Remove Script", Trinity::Icons::c_Trash).c_str()))
            {
                l_Action = Action::Remove;
                l_ActionIndex = it_Index;
            }

            // Last, as in Unity's component menu
            ImGui::Separator();
            if (ImGui::MenuItem(std::format("{} Edit Script", Trinity::Icons::c_FileCode).c_str()))
            {
                l_Edit = std::pair(l_Entry.Language, std::string(std::string_view(l_Entry.ClassName)));
            }

            ImGui::EndPopup();
        }

        if (l_Stopped)
        {
            const std::string l_Badge = std::format("{} Stopped###Stopped", Trinity::Icons::c_TimesCircle);
            const float l_Width = ImGui::CalcTextSize(l_Badge.c_str(), nullptr, true).x + 2.0f * ImGui::GetStyle().FramePadding.x;
            const ImVec4 l_Color = ConsolePanel::GetLevelColor(Trinity::LogLevel::Error);
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(l_RowRight - l_Width, ImGui::GetCursorPosX()));
            ImGui::PushStyleColor(ImGuiCol_Text, l_Color);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(l_Color.x, l_Color.y, l_Color.z, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(l_Color.x, l_Color.y, l_Color.z, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(l_Color.x, l_Color.y, l_Color.z, 0.5f));
            if (ImGui::SmallButton(l_Badge.c_str()) && m_ShowConsole)
            {
                m_ShowConsole();
            }

            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("A call into it failed, as when it throws, so it is called no more while the scene plays. It runs again once its code reloads, or on the next Play. Click to show the Console, which has why");
            }
        }

        if (l_Open)
        {
            DrawScriptFields(l_Entry, l_Change);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    std::string l_CommandLabel;
    if (l_Action != Action::None)
    {
        const std::string l_Label = GetScriptLabel(l_Value.Scripts[l_ActionIndex]);
        const std::string l_EntityLabel = GetEntityLabel(entity);
        l_Change = "Order";
        switch (l_Action)
        {
            case Action::Reset:
            {
                // The menu offers it for a known class alone, and nothing loads code until the section is drawn
                Trinity::ScriptEntry& l_Entry = l_Value.Scripts[l_ActionIndex];
                l_Change = std::format("{} Reset", l_Label);
                l_CommandLabel = std::format("Reset Script {} on {}", l_Label, l_EntityLabel);
                l_Entry.Fields = Trinity::ScriptClassRegistry::CreateEntry(*Trinity::ScriptClassRegistry::Find(l_Entry.Language, l_Entry.ClassName)).Fields;
                break;
            }
            case Action::MoveUp:
                l_CommandLabel = std::format("Move Script {} up on {}", l_Label, l_EntityLabel);
                std::swap(l_Value.Scripts[l_ActionIndex], l_Value.Scripts[l_ActionIndex - 1]);
                break;
            case Action::MoveDown:
                l_CommandLabel = std::format("Move Script {} down on {}", l_Label, l_EntityLabel);
                std::swap(l_Value.Scripts[l_ActionIndex], l_Value.Scripts[l_ActionIndex + 1]);
                break;
            case Action::Remove:
                l_CommandLabel = std::format("Remove Script {} from {}", l_Label, l_EntityLabel);
                l_Value.Scripts.erase(l_Value.Scripts.begin() + static_cast<std::ptrdiff_t>(l_ActionIndex));
                break;
            case Action::None:
                break;
        }
    }

    if (l_Edit)
    {
        static_cast<void>(m_Session.EditScript(l_Edit->first, l_Edit->second));
    }

    if (!l_Change.empty())
    {
        m_Session.GetHistory().Execute(Trinity::CreateScope<SetComponentCommand<Trinity::ScriptComponent>>(entity.GetUUID(), std::move(l_Value), std::move(l_Change), std::move(l_CommandLabel)));
    }
}

// A known class's fields in its order, each with its type's editor, then those the entry keeps that the class lacks or now types otherwise, greyed, with a button that drops them. An unknown class's fields are greyed as they were read
void PropertiesPanel::DrawScriptFields(Trinity::ScriptEntry& entry, std::string& change)
{
    Trinity::Scene& l_Scene = m_Session.GetScene();
    const Trinity::ScriptClassDescription* l_Class = Trinity::ScriptClassRegistry::Find(entry.Language, entry.ClassName);
    if (l_Class == nullptr)
    {
        ImGui::TextDisabled("Not loaded, so its values are kept as they were read");
        for (const Trinity::ScriptEntry::Field& it_Field : entry.Fields)
        {
            DrawReadOnlyField(l_Scene, m_Session.GetRegistry(), it_Field.Name.c_str(), it_Field.Value);
        }

        return;
    }

    if (l_Class->Fields.empty() && entry.Fields.empty())
    {
        ImGui::TextDisabled("No fields");
    }

    for (const Trinity::ScriptFieldDescription& it_Field : l_Class->Fields)
    {
        Trinity::ScriptFieldValue l_Value = Trinity::ScriptClassRegistry::GetFieldValue(entry, it_Field);
        ImGui::PushID(it_Field.Name.c_str());
        if (DrawScriptValue(it_Field, l_Value))
        {
            change = std::format("{}.{}", GetScriptLabel(entry), std::string_view(it_Field.Name));
            entry.SetField(it_Field.Name, std::move(l_Value));
        }

        ImGui::PopID();
    }

    const auto a_IsStale = [l_Class](const Trinity::ScriptEntry::Field& field)
    {
        const Trinity::ScriptFieldDescription* l_Field = l_Class->FindField(field.Name);

        return l_Field == nullptr || l_Field->GetType() != field.Value.GetType();
    };

    if (std::ranges::none_of(entry.Fields, a_IsStale))
    {
        return;
    }

    ImGui::SeparatorText("Not in class");
    for (const Trinity::ScriptEntry::Field& it_Field : entry.Fields)
    {
        if (a_IsStale(it_Field))
        {
            DrawReadOnlyField(l_Scene, m_Session.GetRegistry(), it_Field.Name.c_str(), it_Field.Value);
        }
    }

    if (ImGui::Button("Remove Fields Not in Class"))
    {
        std::erase_if(entry.Fields, a_IsStale);
        change = std::format("{} Fields Not in Class", GetScriptLabel(entry));
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("Values the class no longer has, or now holds as another type. They are kept until removed here, so a field put back gets its value again");
    }
}

// The field's editor by its type, its name on the left. A colour is edited in sRGB and kept linear, a rotation in degrees about X, Y and Z worked out afresh each frame, an enum by its values' names, an asset reference takes the asset type the field asks for, or any, and an input action one of the project's default input actions
bool PropertiesPanel::DrawScriptValue(const Trinity::ScriptFieldDescription& field, Trinity::ScriptFieldValue& value)
{
    if (value.GetType() == Trinity::ScriptFieldType::Asset)
    {
        bool l_Changed = false;
        const Trinity::UUID l_Current = value.Get<Trinity::UUID>();
        DrawAssetSlot(field.Name.c_str(), c_ScriptAssetPickerPopup, field.AssetType, l_Current, [&value, &l_Changed, l_Current](Trinity::UUID asset)
        {
            if (asset != l_Current)
            {
                value = Trinity::ScriptFieldValue::Asset(asset);
                l_Changed = true;
            }
        }, "Drop an asset here, or pick one");

        return l_Changed;
    }

    Label(field.Name.c_str());
    switch (value.GetType())
    {
        case Trinity::ScriptFieldType::Bool:
        {
            return ImGui::Checkbox("##Value", &value.Get<bool>());
        }
        case Trinity::ScriptFieldType::Int32:
        {
            return ImGui::DragScalar("##Value", ImGuiDataType_S32, &value.Get<std::int32_t>(), 0.25f);
        }
        case Trinity::ScriptFieldType::Int64:
        {
            return ImGui::DragScalar("##Value", ImGuiDataType_S64, &value.Get<std::int64_t>(), 0.25f);
        }
        case Trinity::ScriptFieldType::UInt32:
        {
            return ImGui::DragScalar("##Value", ImGuiDataType_U32, &value.Get<std::uint32_t>(), 0.25f);
        }
        case Trinity::ScriptFieldType::UInt64:
        {
            return ImGui::DragScalar("##Value", ImGuiDataType_U64, &value.Get<std::uint64_t>(), 0.25f);
        }
        case Trinity::ScriptFieldType::Float:
        {
            return ImGui::DragFloat("##Value", &value.Get<float>(), 0.01f, 0.0f, 0.0f, "%.3f");
        }
        case Trinity::ScriptFieldType::Double:
        {
            return ImGui::DragScalar("##Value", ImGuiDataType_Double, &value.Get<double>(), 0.01f, nullptr, nullptr, "%.6f");
        }
        case Trinity::ScriptFieldType::Vector2:
        {
            return ImGui::DragFloat2("##Value", &value.Get<glm::vec2>().x, 0.01f, 0.0f, 0.0f, "%.3f");
        }
        case Trinity::ScriptFieldType::Vector3:
        {
            return ImGui::DragFloat3("##Value", &value.Get<glm::vec3>().x, 0.01f, 0.0f, 0.0f, "%.3f");
        }
        case Trinity::ScriptFieldType::Vector4:
        {
            return ImGui::DragFloat4("##Value", &value.Get<glm::vec4>().x, 0.01f, 0.0f, 0.0f, "%.3f");
        }
        case Trinity::ScriptFieldType::Quaternion:
        {
            glm::quat& l_Rotation = value.Get<glm::quat>();
            glm::vec3 l_Degrees = glm::degrees(glm::eulerAngles(l_Rotation));
            if (!ImGui::DragFloat3("##Value", &l_Degrees.x, 0.5f, 0.0f, 0.0f, "%.2f\xC2\xB0"))
            {
                return false;
            }

            l_Rotation = glm::quat(glm::radians(l_Degrees));

            return true;
        }
        case Trinity::ScriptFieldType::Color:
        {
            glm::vec4& l_Color = value.Get<glm::vec4>();
            glm::vec4 l_Srgb(Trinity::LinearToSrgb(l_Color.r), Trinity::LinearToSrgb(l_Color.g), Trinity::LinearToSrgb(l_Color.b), l_Color.a);
            if (!ImGui::ColorEdit4("##Value", &l_Srgb.x, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar))
            {
                return false;
            }

            l_Color = glm::vec4(Trinity::SrgbToLinear(l_Srgb.r), Trinity::SrgbToLinear(l_Srgb.g), Trinity::SrgbToLinear(l_Srgb.b), l_Srgb.a);

            return true;
        }
        case Trinity::ScriptFieldType::String:
        {
            std::string l_Text(std::string_view(value.Get<Trinity::ScriptString>()));
            if (!ImGui::InputText("##Value", &l_Text))
            {
                return false;
            }

            value.Get<Trinity::ScriptString>() = l_Text;

            return true;
        }
        case Trinity::ScriptFieldType::Enum:
        {
            std::int64_t& l_Current = value.Get<std::int64_t>();
            const auto a_Found = std::ranges::find(field.EnumValues, l_Current, &Trinity::ScriptEnumValue::Value);
            const std::string l_Preview = a_Found != field.EnumValues.end() ? std::string(std::string_view(a_Found->Name)) : std::format("{}", l_Current);

            bool l_Changed = false;
            if (ImGui::BeginCombo("##Value", l_Preview.c_str()))
            {
                for (std::size_t it_Index = 0; it_Index < field.EnumValues.size(); ++it_Index)
                {
                    const Trinity::ScriptEnumValue& l_Option = field.EnumValues[it_Index];
                    const bool l_Selected = l_Option.Value == l_Current;
                    ImGui::PushID(static_cast<int>(it_Index));
                    if (ImGui::Selectable(l_Option.Name.c_str(), l_Selected) && !l_Selected)
                    {
                        l_Current = l_Option.Value;
                        l_Changed = true;
                    }

                    if (l_Selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }

                    ImGui::PopID();
                }

                ImGui::EndCombo();
            }

            return l_Changed;
        }
        case Trinity::ScriptFieldType::Entity:
        {
            return EntityCombo(m_Session.GetScene(), value.Get<Trinity::UUID>());
        }
        case Trinity::ScriptFieldType::Asset:
        {
            break;
        }
        case Trinity::ScriptFieldType::InputAction:
        {
            return DrawInputActionSlot(value.Get<Trinity::ScriptString>());
        }
    }

    return false;
}

// Picked from the project's default input actions, each map's under its name, by a search of their paths, or dropped from the Input Actions panel showing that asset. A path the actions lack is kept, shown as missing, until another is picked
bool PropertiesPanel::DrawInputActionSlot(Trinity::ScriptString& path)
{
    const Trinity::Project* l_Project = m_Session.GetProject();
    const Trinity::UUID l_Asset = l_Project != nullptr ? l_Project->GetInputActions() : Trinity::UUID();
    const EditorSession::InputActionsFile* l_File = l_Asset ? m_Session.ReadInputActions(l_Asset) : nullptr;
    const Trinity::InputActionsData* l_Data = l_File != nullptr && l_File->Data ? &*l_File->Data : nullptr;
    const Trinity::AssetRecord* l_Record = l_Asset && m_Session.GetRegistry() != nullptr ? m_Session.GetRegistry()->Find(l_Asset) : nullptr;
    const std::string l_Unavailable = GetActionsUnavailable(l_Asset, l_File);
    const SlotText l_Slot = DescribeActionSlot(l_Data, l_Unavailable, l_Record != nullptr ? std::string_view(l_Record->Path) : std::string_view(), std::string_view(path));

    std::optional<std::string> l_Picked;
    PushSlotColor(l_Slot);
    const bool l_Open = ImGui::BeginCombo("##Value", l_Slot.Text.c_str(), ImGuiComboFlags_HeightLarge);
    PopSlotColor(l_Slot);
    if (!l_Open && !l_Slot.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("%s", l_Slot.Tooltip.c_str());
    }

    if (!l_Open && ImGui::BeginDragDropTarget())
    {
        const ImGuiPayload* l_Payload = ImGui::GetDragDropPayload();
        const std::optional<std::pair<std::uint64_t, std::string_view>> l_Dragged = l_Payload != nullptr && l_Payload->IsDataType(c_InputActionPayload) ? ReadInputActionPayload(l_Payload->Data, l_Payload->DataSize) : std::nullopt;
        if (!l_Dragged || Trinity::UUID(l_Dragged->first) != l_Asset || l_Data == nullptr || !FindInputAction(*l_Data, l_Dragged->second))
        {
            RefuseDrag();
        }
        else if (ImGui::AcceptDragDropPayload(c_InputActionPayload))
        {
            l_Picked = std::string(l_Dragged->second);
        }

        ImGui::EndDragDropTarget();
    }

    if (l_Open)
    {
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##Filter", std::format("{} Search actions", Trinity::Icons::c_Search).c_str(), &m_ActionFilter);
        if (ImGui::Selectable("None", path.empty()) && !path.empty())
        {
            l_Picked = std::string();
        }

        if (l_Data == nullptr)
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextDisabled("%s", l_Unavailable.c_str());
            ImGui::PopTextWrapPos();
        }
        else
        {
            const std::optional<std::pair<std::size_t, std::size_t>> l_Current = FindInputAction(*l_Data, std::string_view(path));
            bool l_Shown = false;
            for (std::size_t it_Map = 0; it_Map < l_Data->Maps.size(); ++it_Map)
            {
                const Trinity::InputActionMap& l_Map = l_Data->Maps[it_Map];
                bool l_Header = false;
                for (std::size_t it_Action = 0; it_Action < l_Map.Actions.size(); ++it_Action)
                {
                    const Trinity::InputActionDefinition& l_Action = l_Map.Actions[it_Action];
                    const std::string l_Path = std::format("{}{}{}", std::string_view(l_Map.Name), Trinity::InputActionsData::c_PathSeparator, std::string_view(l_Action.Name));
                    if (!ContainsIgnoringCase(l_Path, m_ActionFilter))
                    {
                        continue;
                    }

                    if (!std::exchange(l_Header, true))
                    {
                        ImGui::SeparatorText(l_Map.Name.c_str());
                    }

                    l_Shown = true;
                    const bool l_Selected = l_Current == std::pair(it_Map, it_Action);
                    ImGui::PushID(l_Path.c_str());
                    if (ImGui::Selectable(l_Action.Name.c_str(), l_Selected) && !l_Selected)
                    {
                        l_Picked = l_Path;
                    }

                    if (l_Selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }

                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", std::string(Trinity::ToString(l_Action.Type)).c_str());
                    ImGui::PopID();
                }
            }

            if (!l_Shown)
            {
                const bool l_Empty = std::ranges::all_of(l_Data->Maps, [](const Trinity::InputActionMap& map) { return map.Actions.empty(); });
                ImGui::TextDisabled("%s", l_Empty ? "The project's default input actions have no actions yet. Add them in the Input Actions panel" : std::format("No action has \"{}\" in its path", m_ActionFilter).c_str());
            }
        }

        ImGui::EndCombo();
    }

    if (!l_Picked)
    {
        return false;
    }

    path = *l_Picked;

    return true;
}

// An asset picked in the Content Browser: where it is, for a texture a preview and its import settings, for a model its import settings and materials, and for a material its editor
void PropertiesPanel::DrawAsset(Trinity::UUID id)
{
    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Record = l_Registry != nullptr ? l_Registry->Find(id) : nullptr;
    if (l_Record == nullptr)
    {
        m_Session.SetInspectedAsset({});

        return;
    }

    const bool l_Texture = l_Record->Importer == Trinity::TextureAsset::c_AssetType;
    const bool l_Model = l_Record->Importer == ModelImporter::c_Importer;
    const bool l_Material = l_Record->Importer == Trinity::MaterialAsset::c_AssetType;
    const char* l_Icon = l_Texture ? Trinity::Icons::c_FileImage : (l_Model ? Trinity::Icons::c_Cube : (l_Material ? Trinity::Icons::c_PaintBrush : Trinity::Icons::c_File));
    ImGui::TextUnformatted(std::format("{} {}", l_Icon, GetAssetName(*l_Record)).c_str());
    ImGui::TextDisabled("%s", l_Record->Path.c_str());
    ImGui::TextDisabled("UUID %s, %s", id.ToString().c_str(), l_Record->Importer.c_str());
    ImGui::Spacing();

    if (!l_Material)
    {
        m_Material = {};
    }

    if (l_Model)
    {
        m_Preview = {};
        DrawModelSettings(*l_Record);
        DrawModelMaterials(*l_Record);

        return;
    }

    if (l_Material)
    {
        m_Preview = {};
        DrawMaterial(*l_Record);

        return;
    }

    if (!l_Texture)
    {
        m_Preview = {};
        ImGui::TextDisabled("This kind of asset has no import settings");

        return;
    }

    if (m_Preview.GetID() != id)
    {
        m_Preview = Trinity::AssetRef<Trinity::TextureAsset>(id);
    }

    DrawTexturePreview();
    if (l_Record->Parent.IsValid())
    {
        ImGui::TextDisabled("Imported with its model, which decides how");

        return;
    }

    DrawTextureSettings(*l_Record);
}

// Fitted to the panel's width, and to a height that leaves the settings in view
void PropertiesPanel::DrawTexturePreview()
{
    const Trinity::TextureAsset* l_Texture = m_Preview.IsReady() ? m_Preview.Get() : nullptr;
    if (l_Texture == nullptr || !l_Texture->GetTexture() || l_Texture->GetShaderResourceIndex() == Trinity::RHI::c_NoBindlessIndex)
    {
        ImGui::TextDisabled("%s", m_Preview.GetState() == Trinity::AssetState::Failed ? "The texture could not be loaded" : "Loading...");
        ImGui::Spacing();

        return;
    }

    const float l_Width = static_cast<float>(l_Texture->GetWidth());
    const float l_Height = static_cast<float>(std::max(l_Texture->GetHeight(), 1u));
    const float l_Scale = std::min({ ImGui::GetContentRegionAvail().x / l_Width, ImGui::GetFontSize() * 16.0f / l_Height, 1.0f });
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(Trinity::GetImGuiTextureID(*l_Texture))), ImVec2(l_Width * l_Scale, l_Height * l_Scale));
    ImGui::TextDisabled("%ux%u, %u mip(s), %s", l_Texture->GetWidth(), l_Texture->GetHeight(), l_Texture->GetMipLevels(), std::string(Trinity::RHI::ToString(l_Texture->GetFormat())).c_str());
    ImGui::Spacing();
}

// Edited here first, and written to the .meta and reimported only on Apply, which the texture's sprites pick up once the new one has loaded
void PropertiesPanel::DrawTextureSettings(const Trinity::AssetRecord& record)
{
    const TextureImportSettings l_Saved = TextureImporter::ReadSettings(record);
    if (m_SettingsAsset != record.ID)
    {
        m_SettingsAsset = record.ID;
        m_TextureSettings = l_Saved;
    }

    if (!BeginSection("Import Settings", Trinity::Icons::c_Gear))
    {
        return;
    }

    Label("sRGB");
    ImGui::BeginDisabled(m_TextureSettings.NormalMap);
    ImGui::Checkbox("##Srgb", &m_TextureSettings.Srgb);
    ImGui::EndDisabled();

    Label("Normal Map");
    ImGui::Checkbox("##NormalMap", &m_TextureSettings.NormalMap);

    Label("Generate Mips");
    ImGui::Checkbox("##GenerateMips", &m_TextureSettings.GenerateMips);

    Label("UASTC Level");
    int l_Level = static_cast<int>(m_TextureSettings.UastcLevel);
    if (ImGui::SliderInt("##UastcLevel", &l_Level, 0, static_cast<int>(TextureImporter::c_MaxUastcLevel), "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        m_TextureSettings.UastcLevel = static_cast<std::uint32_t>(l_Level);
    }

    Label("Filter");
    int l_Filter = m_TextureSettings.Filter == Trinity::RHI::Filter::Nearest ? 1 : 0;
    if (ImGui::Combo("##Filter", &l_Filter, "Linear\0Nearest\0"))
    {
        m_TextureSettings.Filter = l_Filter == 1 ? Trinity::RHI::Filter::Nearest : Trinity::RHI::Filter::Linear;
    }

    ImGui::Spacing();
    const bool l_Changed = m_TextureSettings != l_Saved;
    ImGui::BeginDisabled(!l_Changed);
    if (ImGui::Button("Apply"))
    {
        static_cast<void>(m_Session.ApplyImportSettings(record.ID, TextureImporter::MakeSettings(m_TextureSettings)));
    }

    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        m_TextureSettings = l_Saved;
    }

    ImGui::EndDisabled();

    if (m_Session.IsImporting(record.ID))
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Importing...");
    }
}

// Units and axes as the file gives them unless set here, and the collision shapes cooked for its meshes. Apply writes them to the .meta and imports the model again, which entities already created from it do not follow
void PropertiesPanel::DrawModelSettings(const Trinity::AssetRecord& record)
{
    const ModelImportSettings l_Saved = ModelImporter::ReadSettings(record);
    if (m_SettingsAsset != record.ID)
    {
        m_SettingsAsset = record.ID;
        m_ModelSettings = l_Saved;
    }

    if (!BeginSection("Import Settings", Trinity::Icons::c_Gear))
    {
        return;
    }

    Label("Auto Unit Scale");
    bool l_AutoScale = !m_ModelSettings.UnitScale.has_value();
    if (ImGui::Checkbox("##AutoUnitScale", &l_AutoScale))
    {
        m_ModelSettings.UnitScale = l_AutoScale ? std::nullopt : std::optional<float>(1.0f);
    }

    if (m_ModelSettings.UnitScale)
    {
        Label("Unit Scale");
        ImGui::DragFloat("##UnitScale", &*m_ModelSettings.UnitScale, 0.001f, 0.0001f, 10000.0f, "%.4f m", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    }

    Label("Up Axis");
    int l_Axis = static_cast<int>(m_ModelSettings.UpAxis);
    if (ImGui::Combo("##UpAxis", &l_Axis, "Auto\0Y\0Z\0"))
    {
        m_ModelSettings.UpAxis = static_cast<ModelUpAxis>(l_Axis);
    }

    Label("Collision");
    int l_Collision = static_cast<int>(m_ModelSettings.Collision);
    if (ImGui::Combo("##Collision", &l_Collision, "None\0Convex Hulls\0Triangle Meshes\0Both\0"))
    {
        m_ModelSettings.Collision = static_cast<ModelCollision>(l_Collision);
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::SetTooltip("The shapes cooked beside each mesh for colliders: a convex hull, which any body can have, and its triangles, for static and kinematic bodies");
    }

    ImGui::Spacing();
    const bool l_Changed = m_ModelSettings != l_Saved;
    ImGui::BeginDisabled(!l_Changed);
    if (ImGui::Button("Apply"))
    {
        static_cast<void>(m_Session.ApplyImportSettings(record.ID, ModelImporter::MakeSettings(m_ModelSettings, record)));
    }

    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        m_ModelSettings = l_Saved;
    }

    ImGui::EndDisabled();

    if (m_Session.IsImportingModels())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Importing...");
    }
}

// Each of the model's materials, which opens its editor, with how many fields its .meta overrides
void PropertiesPanel::DrawModelMaterials(const Trinity::AssetRecord& record)
{
    if (!BeginSection("Materials", Trinity::Icons::c_PaintBrush))
    {
        return;
    }

    bool l_Any = false;
    for (const Trinity::SubAsset& it_SubAsset : record.SubAssets)
    {
        if (it_SubAsset.Importer != ModelImporter::c_MaterialImporter)
        {
            continue;
        }

        l_Any = true;
        const std::size_t l_Overrides = ModelImporter::ReadMaterialOverrides(record, it_SubAsset.Key).size();
        const std::string l_Label = l_Overrides == 0 ? it_SubAsset.Key : std::format("{} ({} overridden)", it_SubAsset.Key, l_Overrides);
        if (ImGui::Selectable(std::format("{} {}###{}", Trinity::Icons::c_PaintBrush, l_Label, it_SubAsset.ID).c_str()))
        {
            m_Session.SetInspectedAsset(it_SubAsset.ID);
        }
    }

    if (!l_Any)
    {
        ImGui::TextDisabled("%s", m_Session.IsImportingModels() ? "Importing..." : "The model has no materials");
    }
}

// Every factor and texture, each edit written at once and shown the same frame, and undone in one step. A model's material marks in colour the fields its .meta overrides, and right-clicking one puts back what was imported. Colours are edited as they look, in sRGB, and kept linear
void PropertiesPanel::DrawMaterial(const Trinity::AssetRecord& record)
{
    if (m_Material.GetID() != record.ID)
    {
        m_Material = Trinity::AssetRef<Trinity::MaterialAsset>(record.ID);
        m_ImportedMaterialImports = UINT64_MAX;
    }

    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    const Trinity::AssetRecord* l_Model = record.Parent.IsValid() && l_Registry != nullptr ? l_Registry->Find(record.Parent) : nullptr;
    if (l_Model != nullptr)
    {
        if (m_ImportedMaterialImports != m_Session.GetModelImportCount())
        {
            m_ImportedMaterial = m_Session.ReadImportedMaterial(record.ID);
            m_ImportedMaterialImports = m_Session.GetModelImportCount();
        }

        ImGui::TextDisabled("Imported with %s. Edits are kept in its .meta", GetAssetName(*l_Model).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(std::format("{} Show Model", Trinity::Icons::c_Cube).c_str()))
        {
            m_Session.SetInspectedAsset(l_Model->ID);

            return;
        }

        ImGui::Spacing();
    }
    else
    {
        m_ImportedMaterial.reset();
    }

    const Trinity::MaterialAsset* l_Asset = m_Material.IsReady() ? m_Material.Get() : nullptr;
    if (l_Asset == nullptr)
    {
        ImGui::TextDisabled("%s", m_Material.GetState() == Trinity::AssetState::Failed ? "The material could not be loaded" : "Loading...");

        return;
    }

    if (!BeginSection("Material", Trinity::Icons::c_PaintBrush))
    {
        return;
    }

    const Trinity::MaterialData l_Current = l_Asset->GetData();
    std::vector<Trinity::MaterialField> l_ImportedFields;
    std::vector<std::string> l_Overridden;
    if (m_ImportedMaterial)
    {
        l_ImportedFields = Trinity::GetMaterialFields(*m_ImportedMaterial);
        const std::vector<Trinity::MaterialField> l_Fields = Trinity::GetMaterialFields(l_Current);
        for (std::size_t it_Field = 0; it_Field < l_Fields.size(); ++it_Field)
        {
            if (l_Fields[it_Field] != l_ImportedFields[it_Field])
            {
                l_Overridden.push_back(l_Fields[it_Field].Name);
            }
        }
    }

    // The label, in the accent colour when any of its fields is overridden, with a menu that puts them back as imported
    const auto a_Label = [&](const char* label, std::initializer_list<std::string_view> fields)
    {
        const bool l_IsOverridden = std::ranges::any_of(fields, [&l_Overridden](std::string_view field) { return std::ranges::find(l_Overridden, field) != l_Overridden.end(); });
        ImGui::AlignTextToFramePadding();
        if (l_IsOverridden)
        {
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", label);
        }
        else
        {
            ImGui::TextUnformatted(label);
        }

        const std::string l_Popup = std::format("##Revert{}", label);
        if (l_IsOverridden)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                ImGui::SetTooltip("Overridden in the model's .meta. Right-click to put back what was imported");
            }

            ImGui::OpenPopupOnItemClick(l_Popup.c_str(), ImGuiPopupFlags_MouseButtonRight);
        }

        if (ImGui::BeginPopup(l_Popup.c_str()))
        {
            if (ImGui::MenuItem(std::format("{} Revert to Imported", Trinity::Icons::c_Undo).c_str()))
            {
                std::vector<Trinity::MaterialField> l_Reverted;
                std::ranges::copy_if(l_ImportedFields, std::back_inserter(l_Reverted), [fields](const Trinity::MaterialField& field) { return std::ranges::find(fields, std::string_view(field.Name)) != fields.end(); });
                if (const Trinity::Expected<Trinity::MaterialData, std::string> l_Edited = Trinity::ApplyMaterialFields(l_Current, l_Reverted))
                {
                    static_cast<void>(m_Session.EditMaterial(record.ID, l_Current, *l_Edited, std::format("Revert {}", label)));
                }
            }

            ImGui::EndPopup();
        }

        ImGui::SameLine(ImGui::GetFontSize() * c_LabelWidthInFonts);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    // The widget edits a copy, and a change becomes an edit, which merges with the next while the widget stays active
    const auto a_Edit = [&](std::string_view field, const auto& widget)
    {
        Trinity::MaterialData l_Edited = l_Current;
        if (widget(l_Edited))
        {
            static_cast<void>(m_Session.EditMaterial(record.ID, l_Current, l_Edited, std::string(field)));
        }
    };

    const auto a_ToSrgb = [](glm::vec3 color) { return glm::vec3(Trinity::LinearToSrgb(color.r), Trinity::LinearToSrgb(color.g), Trinity::LinearToSrgb(color.b)); };
    const auto a_ToLinear = [](glm::vec3 color) { return glm::vec3(Trinity::SrgbToLinear(color.r), Trinity::SrgbToLinear(color.g), Trinity::SrgbToLinear(color.b)); };

    a_Label("Base Color", { "BaseColorFactor" });
    a_Edit("Base Color", [&](Trinity::MaterialData& material)
    {
        glm::vec4 l_Color(a_ToSrgb(glm::vec3(material.BaseColorFactor)), material.BaseColorFactor.a);
        const bool l_Changed = ImGui::ColorEdit4("##BaseColor", &l_Color.x, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar);
        material.BaseColorFactor = glm::vec4(a_ToLinear(glm::vec3(l_Color)), l_Color.a);

        return l_Changed;
    });

    a_Label("Metallic", { "MetallicFactor" });
    a_Edit("Metallic", [](Trinity::MaterialData& material) { return ImGui::SliderFloat("##Metallic", &material.MetallicFactor, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Roughness", { "RoughnessFactor" });
    a_Edit("Roughness", [](Trinity::MaterialData& material) { return ImGui::SliderFloat("##Roughness", &material.RoughnessFactor, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Emissive", { "EmissiveFactor" });
    a_Edit("Emissive", [&](Trinity::MaterialData& material)
    {
        glm::vec3 l_Color = a_ToSrgb(material.EmissiveFactor);
        const bool l_Changed = ImGui::ColorEdit3("##Emissive", &l_Color.x);
        material.EmissiveFactor = a_ToLinear(l_Color);

        return l_Changed;
    });

    a_Label("Emissive Strength", { "EmissiveStrength" });
    a_Edit("Emissive Strength", [](Trinity::MaterialData& material) { return ImGui::DragFloat("##EmissiveStrength", &material.EmissiveStrength, 0.05f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Normal Scale", { "NormalScale" });
    a_Edit("Normal Scale", [](Trinity::MaterialData& material) { return ImGui::DragFloat("##NormalScale", &material.NormalScale, 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Occlusion Strength", { "OcclusionStrength" });
    a_Edit("Occlusion Strength", [](Trinity::MaterialData& material) { return ImGui::SliderFloat("##OcclusionStrength", &material.OcclusionStrength, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });

    a_Label("Alpha Mode", { "AlphaMode" });
    a_Edit("Alpha Mode", [](Trinity::MaterialData& material)
    {
        int l_Mode = static_cast<int>(material.AlphaMode);
        const bool l_Changed = ImGui::Combo("##AlphaMode", &l_Mode, "Opaque\0Mask\0Blend\0");
        material.AlphaMode = static_cast<Trinity::MaterialAlphaMode>(l_Mode);

        return l_Changed;
    });

    a_Label("Alpha Cutoff", { "AlphaCutoff" });
    ImGui::BeginDisabled(l_Current.AlphaMode != Trinity::MaterialAlphaMode::Mask);
    a_Edit("Alpha Cutoff", [](Trinity::MaterialData& material) { return ImGui::SliderFloat("##AlphaCutoff", &material.AlphaCutoff, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp); });
    ImGui::EndDisabled();

    a_Label("Double Sided", { "DoubleSided" });
    a_Edit("Double Sided", [](Trinity::MaterialData& material) { return ImGui::Checkbox("##DoubleSided", &material.DoubleSided); });

    DrawMaterialTextures(record, l_Current, a_Label);

    // Nothing is being dragged or typed into, so the next change is an edit of its own
    if (!ImGui::IsAnyItemActive())
    {
        m_Session.GetHistory().EndMerge();
    }
}

// Each slot shows its texture once loaded, takes a texture dropped from elsewhere in Forge or picked from the project's, and names the UV set it is sampled with
void PropertiesPanel::DrawMaterialTextures(const Trinity::AssetRecord& record, const Trinity::MaterialData& current, const std::function<void(const char*, std::initializer_list<std::string_view>)>& label)
{
    struct Slot
    {
        const char* Label;
        std::string_view Texture;
        std::string_view TexCoord;
        Trinity::MaterialTexture Trinity::MaterialData::* Member;
    };

    constexpr std::array<Slot, 5> c_Slots{ {
        { "Base Color Map", "BaseColorTexture", "BaseColorTexCoord", &Trinity::MaterialData::BaseColorTexture },
        { "Metal/Rough Map", "MetallicRoughnessTexture", "MetallicRoughnessTexCoord", &Trinity::MaterialData::MetallicRoughnessTexture },
        { "Normal Map", "NormalTexture", "NormalTexCoord", &Trinity::MaterialData::NormalTexture },
        { "Occlusion Map", "OcclusionTexture", "OcclusionTexCoord", &Trinity::MaterialData::OcclusionTexture },
        { "Emissive Map", "EmissiveTexture", "EmissiveTexCoord", &Trinity::MaterialData::EmissiveTexture }
    } };

    const Trinity::AssetRegistry* l_Registry = m_Session.GetRegistry();
    for (const Slot& it_Slot : c_Slots)
    {
        ImGui::PushID(it_Slot.Label);
        const Trinity::MaterialTexture& l_Slot = current.*it_Slot.Member;
        const auto a_Set = [&](Trinity::MaterialTexture texture)
        {
            Trinity::MaterialData l_Edited = current;
            l_Edited.*it_Slot.Member = texture;
            static_cast<void>(m_Session.EditMaterial(record.ID, current, l_Edited, it_Slot.Label));
            m_Session.GetHistory().EndMerge();
        };

        label(it_Slot.Label, { it_Slot.Texture, it_Slot.TexCoord });

        // The thumbnail only for a texture already loaded, which the material keeps loaded
        const float l_Size = ImGui::GetFrameHeight();
        const Trinity::Asset* l_Loaded = l_Slot.Texture.IsValid() && Trinity::AssetManager::GetState(l_Slot.Texture) == Trinity::AssetState::Ready ? Trinity::AssetManager::GetAsset(l_Slot.Texture) : nullptr;
        const Trinity::TextureAsset* l_Texture = l_Loaded != nullptr && l_Loaded->GetAssetType() == Trinity::TextureAsset::c_AssetType ? static_cast<const Trinity::TextureAsset*>(l_Loaded) : nullptr;
        if (l_Texture != nullptr && l_Texture->GetTexture() && l_Texture->GetShaderResourceIndex() != Trinity::RHI::c_NoBindlessIndex)
        {
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(Trinity::GetImGuiTextureID(*l_Texture))), ImVec2(l_Size, l_Size));
        }
        else
        {
            ImGui::Dummy(ImVec2(l_Size, l_Size));
        }

        ImGui::SameLine();
        const SlotText l_Text = DescribeAssetSlot(l_Registry, Trinity::TextureAsset::c_AssetType, l_Slot.Texture);
        const float l_UVWidth = ImGui::GetFontSize() * 3.5f;
        const float l_Width = std::max(ImGui::GetContentRegionAvail().x - l_Size - l_UVWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f, 1.0f);
        const std::string l_Popup = std::format("##Pick{}", it_Slot.Texture);
        PushSlotColor(l_Text);
        const bool l_Clicked = ImGui::Button(std::format("{}###Texture", l_Text.Text).c_str(), ImVec2(l_Width, 0.0f));
        PopSlotColor(l_Text);
        if (l_Clicked)
        {
            m_AssetFilter.clear();
            ImGui::OpenPopup(l_Popup.c_str());
        }

        if (!l_Text.Tooltip.empty() && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", l_Text.Tooltip.c_str());
        }

        if (ImGui::BeginDragDropTarget())
        {
            const Trinity::UUID l_Dragged = GetDraggedAsset(l_Registry, Trinity::TextureAsset::c_AssetType);
            if (!l_Dragged)
            {
                RefuseDrag();
            }
            else if (ImGui::AcceptDragDropPayload(c_AssetPayload))
            {
                a_Set({ l_Dragged, l_Slot.TexCoord });
            }

            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!l_Slot.Texture);
        if (ImGui::Button(std::format("{}##Clear", Trinity::Icons::c_TimesCircle).c_str(), ImVec2(l_Size, 0.0f)))
        {
            a_Set({});
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(l_UVWidth);
        int l_TexCoord = static_cast<int>(l_Slot.TexCoord);
        if (ImGui::Combo("##UV", &l_TexCoord, "UV 0\0UV 1\0"))
        {
            a_Set({ l_Slot.Texture, static_cast<std::uint32_t>(l_TexCoord) });
        }

        DrawAssetPicker(l_Popup.c_str(), Trinity::TextureAsset::c_AssetType, l_Slot.Texture, [&](Trinity::UUID texture) { a_Set({ texture, l_Slot.TexCoord }); });
        ImGui::PopID();
    }
}

// The component's section, with Unity's component menu on a right-click: Reset to a new one's values, Remove Component, and Copy Component, whose values go into one of its type with Paste Component Values, or onto an entity without one with Paste Component As New, each change one command. A greyed section, as while the scene plays, has the menu too with Copy Component alone, so values found while playing can be pasted once it stops. A Transform stays, and a Script component's scripts reset each from its own menu
bool PropertiesPanel::BeginComponent(Trinity::Entity entity, std::string_view name, const char* icon)
{
    const bool l_Open = BeginSection(name, icon);
    const bool l_Editable = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0;
    const std::string l_Menu = std::format("##{}Menu", name);
    // BeginPopupContextItem passes over a greyed header, as over any greyed item
    if (!l_Editable && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::OpenPopup(l_Menu.c_str());
    }

    // The menu is not greyed with its section, as a tooltip over it is not
    if (!l_Editable)
    {
        ImGui::BeginDisabledOverrideReenable();
    }

    if (ImGui::BeginPopupContextItem(l_Menu.c_str()))
    {
        const std::string_view l_Label = GetComponentLabel(name);
        if (name != Trinity::ScriptComponent::c_TypeName)
        {
            if (ImGui::MenuItem(std::format("{} Reset", Trinity::Icons::c_Undo).c_str(), nullptr, false, l_Editable))
            {
                m_PendingCommand = Trinity::CreateScope<SetComponentTextCommand>(entity.GetUUID(), std::string(name), GetNewComponentText(name), std::format("Reset {} of {}", l_Label, GetEntityLabel(entity)));
            }

            ImGui::Separator();
        }

        if (ImGui::MenuItem(std::format("{} Remove Component", Trinity::Icons::c_Trash).c_str(), nullptr, false, l_Editable && name != Trinity::TransformComponent::c_TypeName))
        {
            m_PendingCommand = Trinity::CreateScope<RemoveComponentCommand>(entity.GetUUID(), std::string(name));
        }

        ImGui::Separator();
        if (ImGui::MenuItem(std::format("{} Copy Component", Trinity::Icons::c_Copy).c_str()))
        {
            m_CopiedComponent = CopiedComponent{ .Name = std::string(name), .Text = Trinity::SceneSerializer::SaveComponentToText(entity, name) };
        }

        // As new onto an entity without one of the copied type, once code that knows the type is loaded
        const std::vector<std::string_view> l_Names = Trinity::SceneSerializer::GetComponentNames();
        const bool l_CanPasteNew = m_CopiedComponent && std::ranges::find(l_Names, m_CopiedComponent->Name) != l_Names.end() && !Trinity::SceneSerializer::HasComponent(entity, m_CopiedComponent->Name);
        if (ImGui::MenuItem("Paste Component As New", nullptr, false, l_Editable && l_CanPasteNew))
        {
            m_PendingCommand = Trinity::CreateScope<SetComponentTextCommand>(entity.GetUUID(), m_CopiedComponent->Name, m_CopiedComponent->Text, std::format("Paste {} As New to {}", GetComponentLabel(m_CopiedComponent->Name), GetEntityLabel(entity)));
        }

        if (ImGui::MenuItem("Paste Component Values", nullptr, false, l_Editable && m_CopiedComponent && m_CopiedComponent->Name == name))
        {
            m_PendingCommand = Trinity::CreateScope<SetComponentTextCommand>(entity.GetUUID(), std::string(name), m_CopiedComponent->Text, std::format("Paste {} Values to {}", l_Label, GetEntityLabel(entity)));
        }

        ImGui::EndPopup();
    }

    if (!l_Editable)
    {
        ImGui::EndDisabledOverrideReenable();
    }

    return l_Open;
}

void PropertiesPanel::SetTexture(Trinity::Entity entity, Trinity::UUID texture)
{
    Trinity::SpriteRendererComponent l_Value = entity.Get<Trinity::SpriteRendererComponent>();
    l_Value.Texture = texture;

    CommandStack& l_History = m_Session.GetHistory();
    l_History.Execute(Trinity::CreateScope<SetComponentCommand<Trinity::SpriteRendererComponent>>(entity.GetUUID(), std::move(l_Value), "Texture"));
    l_History.EndMerge();
}