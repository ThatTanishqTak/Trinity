#pragma once

#include <Trinity.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Each read from its file as the serializer first asks, then kept. Cleared as another scene opens
class PrefabTemplateCache
{
public:
    // Null for a prefab the registry lacks or a model not imported yet. A bad file is logged once
    [[nodiscard]] Trinity::Scene* Find(Trinity::UUID prefab, const Trinity::AssetRegistry* registry);
    // The text the prefab's template was made from, or null for a prefab without one kept
    [[nodiscard]] const std::string* FindText(Trinity::UUID prefab) const;
    // A number no other making has had since Forge started. 0 for a prefab without one kept
    [[nodiscard]] std::uint64_t GetVersion(Trinity::UUID prefab) const;
    // Remakes the templates from the texts, then each kept prefab holding them. Refused for a bad text
    [[nodiscard]] Trinity::Expected<std::vector<Trinity::UUID>, std::string> Replace(std::span<const Trinity::SceneSerializer::PrefabText> texts);
    // Each prefab kept whose file holds other text now, with that text. A missing file is left alone
    [[nodiscard]] std::vector<std::pair<Trinity::UUID, std::string>> FindChanged(const Trinity::AssetRegistry* registry) const;
    // Each template kept and a prefab that an instance nested in it stands in for, missing or held back
    [[nodiscard]] std::vector<std::pair<Trinity::UUID, Trinity::UUID>> FindMissing() const;
    void Clear()
    {
        m_Templates.clear();
        m_Models.clear();
    }

private:
    // The template, its text, the last text that would not read, so it is logged once, and its version
    struct Entry
    {
        Trinity::Scope<Trinity::Scene> Template;
        std::string Text;
        std::string Refused;
        std::uint64_t Version = 0;
    };

    // A model's prefab text and the cooked hierarchy it was made from, or why that could not be read
    struct ModelText
    {
        std::string Cooked;
        std::string Text;
        std::string Error;
    };

    // A prefab's file, or the prefab a model's cooked hierarchy makes. An error if neither can be read
    [[nodiscard]] Trinity::Expected<std::string, std::string> ReadText(const Trinity::AssetRecord& record) const;

    std::unordered_map<Trinity::UUID, Entry> m_Templates;
    mutable std::unordered_map<Trinity::UUID, ModelText> m_Models;
    // Clear does not restart it, so a template read again is never taken for the one before
    std::uint64_t m_LastVersion = 0;
};