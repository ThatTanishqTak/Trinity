#pragma once

#include "Trinity/Core/Base.hpp"
#include "Trinity/Core/UUID.hpp"
#include "Trinity/UI/GameUI.hpp"
#include "Trinity/UI/UIDataModel.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Rml
{
    class Context;
}

namespace Trinity
{
    class Scene;

    // A scalar as it crosses into and out of the store, of its type alone. Text is valid until the value changes
    struct UIDataScalar
    {
        UIDataType Type = UIDataType::Int;
        std::int32_t Int = 0;
        float Float = 0.0f;
        bool Bool = false;
        std::string_view Text;
    };

    // The data models scripts bind into UIDocuments, held here, by the scene and the entity whose UIDocument they are and by name, and reached by RmlUi through links each document's context keeps, by the model's and the value's names. A link reaches what it was bound to while that lives, and nothing once it has gone, so a document outlives the values it showed, and binding the name again, as the next run of a scene does, takes the link over. Everything is counted under GameUI. Main thread only
    class GameUIData
    {
    public:
        GameUIData();
        ~GameUIData();

        GameUIData(const GameUIData&) = delete;
        GameUIData& operator=(const GameUIData&) = delete;

        // Before a document is read into the context: an empty model for each data-model its text names, and the context lacks, which takes values bound once the document has been read
        static void DeclareModels(Rml::Context& context, std::string_view text);

        // The document's context shows the entity's UIDocument of the scene, so the models bound there are linked into it. Cheap when it already showed them
        void Attach(UIDocumentID document, Rml::Context& context, const Scene* owner, UUID entity);
        // As its context goes, with what it linked
        void Detach(UIDocumentID document);
        // As the scene's scripts end: its models go, and what linked to them reaches nothing
        void ReleaseScene(const Scene* scene);

        [[nodiscard]] std::uint64_t GetModel(const Scene& owner, UUID entity, std::string_view name);
        [[nodiscard]] bool IsModel(std::uint64_t model) const;
        [[nodiscard]] std::uint64_t BindValue(std::uint64_t model, std::string_view name, const UIDataScalar& value);
        [[nodiscard]] std::uint64_t BindList(std::uint64_t model, std::string_view name, std::span<const UIDataField> fields);

        [[nodiscard]] bool IsValue(std::uint64_t value) const;
        [[nodiscard]] std::optional<UIDataType> GetType(std::uint64_t value) const;
        void Set(std::uint64_t value, const UIDataScalar& scalar);
        [[nodiscard]] std::optional<UIDataScalar> Get(std::uint64_t value) const;

        [[nodiscard]] bool IsList(std::uint64_t list) const;
        [[nodiscard]] std::uint32_t GetSize(std::uint64_t list) const;
        void Resize(std::uint64_t list, std::uint32_t rows);
        void Set(std::uint64_t list, std::uint32_t row, std::string_view field, const UIDataScalar& scalar);
        [[nodiscard]] std::optional<UIDataScalar> Get(std::uint64_t list, std::uint32_t row, std::string_view field) const;

        [[nodiscard]] std::uint32_t GetModelCount() const;
        [[nodiscard]] std::uint32_t GetVariableCount() const;

    private:
        struct State;

        Scope<State> m_State;
    };
}