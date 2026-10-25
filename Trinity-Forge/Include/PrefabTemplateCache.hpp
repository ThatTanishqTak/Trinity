#pragma once

#include <Trinity.hpp>

#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// The prefabs the open scene's instances are made from, each read from its file the first time the serializer asks for it and kept from then on, with the text it was made from, so the instances save against the prefab they were made from, even once its file is deleted. A model's is the prefab its cooked hierarchy makes. Cleared as another scene opens, which reads them again
class PrefabTemplateCache
{
public:
    // Null for a prefab the registry does not have, or a model not imported yet, which are asked of it again each time, and for one whose file cannot be read, which is logged once and kept as null until its file changes
    [[nodiscard]] Trinity::Scene* Find(Trinity::UUID prefab, const Trinity::AssetRegistry* registry);
    // The text the prefab's template was made from, or null for a prefab without one kept
    [[nodiscard]] const std::string* FindText(Trinity::UUID prefab) const;
    // The prefabs' templates made again from the texts, in place of those kept, which go at once, and each prefab kept that holds one of them, at any depth, made again from the text it was made from, inner ones first, so it holds them as they now are. The texts are what the files hold from then on, so they are not found changed. Each prefab made again, in the order made. Refused, keeping every template as it was, for a text that is not a prefab, which is not found changed either until the file changes again. One holding them that cannot be read again is logged and kept as null, as one whose file cannot be read is
    [[nodiscard]] Trinity::Expected<std::vector<Trinity::UUID>, std::string> Replace(std::span<const Trinity::SceneSerializer::PrefabText> texts);
    // Each prefab kept, or kept as null, whose file holds other text now, with that text, a model's being the prefab its cooked hierarchy makes now. One whose file is gone is left as it is until another scene opens
    [[nodiscard]] std::vector<std::pair<Trinity::UUID, std::string>> FindChanged(const Trinity::AssetRegistry* registry) const;
    // Each template kept and a prefab an instance nested in it stands in for, missing as it was read, or held back as the template would hold itself through it
    [[nodiscard]] std::vector<std::pair<Trinity::UUID, Trinity::UUID>> FindMissing() const;
    void Clear()
    {
        m_Templates.clear();
        m_Models.clear();
    }

private:
    // The template and the text it was made from, and the last text that would not read, so it is logged once
    struct Entry
    {
        Trinity::Scope<Trinity::Scene> Template;
        std::string Text;
        std::string Refused;
    };

    // A model's prefab text and the cooked hierarchy it was made from, or why that could not be read, so a hierarchy that has not changed is not made into it again
    struct ModelText
    {
        std::string Cooked;
        std::string Text;
        std::string Error;
    };

    // The text the record's prefab is read from: a prefab's file, or the prefab a model's cooked hierarchy makes. Nothing, with why, for a file or a hierarchy that cannot be read
    [[nodiscard]] Trinity::Expected<std::string, std::string> ReadText(const Trinity::AssetRecord& record) const;

    std::unordered_map<Trinity::UUID, Entry> m_Templates;
    mutable std::unordered_map<Trinity::UUID, ModelText> m_Models;
};