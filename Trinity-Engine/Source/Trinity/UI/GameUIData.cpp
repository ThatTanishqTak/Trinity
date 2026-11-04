#include "Trinity/UI/GameUIData.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/UI/GameUIAccess.hpp"
#include "Trinity/UI/GameUIRenderInterface.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataVariable.h>
#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Trinity
{
    namespace
    {
        // What a value or a field of a row holds, of the type its variable or field has
        struct Cell
        {
            std::int32_t Int = 0;
            float Float = 0.0f;
            bool Bool = false;
            Rml::String Text;
        };

        struct Model;
        struct Link;

        // Without asking for it by name, which RmlUi logs as an error when the context has none
        [[nodiscard]] bool HasModel(const Rml::Context& context, const Rml::String& name)
        {
            return context.GetDataModels().contains(name);
        }

        // A name is RmlUi's, which an expression can reach
        [[nodiscard]] bool IsLegalName(std::string_view name)
        {
            const auto a_Letter = [](char character) { return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_'; };

            return !name.empty() && a_Letter(name.front()) && std::ranges::all_of(name, [&](char character) { return a_Letter(character) || (character >= '0' && character <= '9'); });
        }

        [[nodiscard]] UIDataScalar Read(const Cell& cell, UIDataType type)
        {
            return { type, cell.Int, cell.Float, cell.Bool, cell.Text };
        }

        // Whether it changed
        bool Write(Cell& cell, UIDataType type, const UIDataScalar& value)
        {
            switch (type)
            {
                case UIDataType::Int:
                {
                    return std::exchange(cell.Int, value.Int) != value.Int;
                }
                case UIDataType::Float:
                {
                    return std::exchange(cell.Float, value.Float) != value.Float;
                }
                case UIDataType::Bool:
                {
                    return std::exchange(cell.Bool, value.Bool) != value.Bool;
                }
                case UIDataType::String:
                {
                    if (cell.Text == value.Text)
                    {
                        return false;
                    }

                    cell.Text.assign(value.Text);

                    return true;
                }
            }

            return false;
        }

        void ToVariant(const Cell& cell, UIDataType type, Rml::Variant& variant)
        {
            switch (type)
            {
                case UIDataType::Int:
                {
                    variant = cell.Int;

                    break;
                }
                case UIDataType::Float:
                {
                    variant = cell.Float;

                    break;
                }
                case UIDataType::Bool:
                {
                    variant = cell.Bool;

                    break;
                }
                case UIDataType::String:
                {
                    variant = cell.Text;

                    break;
                }
            }
        }

        // What a control writes, converted to the cell's type as RmlUi converts it. Whether it changed
        bool FromVariant(Cell& cell, UIDataType type, const Rml::Variant& variant)
        {
            UIDataScalar l_Value;
            l_Value.Type = type;
            Rml::String l_Text;
            switch (type)
            {
                case UIDataType::Int:
                {
                    l_Value.Int = variant.Get<int>();

                    break;
                }
                case UIDataType::Float:
                {
                    l_Value.Float = variant.Get<float>();

                    break;
                }
                case UIDataType::Bool:
                {
                    l_Value.Bool = variant.Get<bool>();

                    break;
                }
                case UIDataType::String:
                {
                    l_Text = variant.Get<Rml::String>();
                    l_Value.Text = l_Text;

                    break;
                }
            }

            return Write(cell, type, l_Value);
        }

        // How RmlUi reaches a cell of each type, through a pointer to it
        class CellDefinition final : public Rml::VariableDefinition
        {
        public:
            explicit CellDefinition(UIDataType type) : Rml::VariableDefinition(Rml::DataVariableType::Scalar), m_Type(type)
            {
            }

            bool Get(void* ptr, Rml::Variant& variant) override
            {
                ToVariant(*static_cast<const Cell*>(ptr), m_Type, variant);

                return true;
            }

            bool Set(void* ptr, const Rml::Variant& variant) override
            {
                static_cast<void>(FromVariant(*static_cast<Cell*>(ptr), m_Type, variant));

                return true;
            }

        private:
            UIDataType m_Type;
        };

        [[nodiscard]] CellDefinition& GetCellDefinition(UIDataType type)
        {
            static CellDefinition s_Int(UIDataType::Int);
            static CellDefinition s_Float(UIDataType::Float);
            static CellDefinition s_Bool(UIDataType::Bool);
            static CellDefinition s_String(UIDataType::String);
            switch (type)
            {
                case UIDataType::Int:
                {
                    return s_Int;
                }
                case UIDataType::Float:
                {
                    return s_Float;
                }
                case UIDataType::Bool:
                {
                    return s_Bool;
                }
                case UIDataType::String:
                {
                    return s_String;
                }
            }

            return s_Int;
        }

        // A list's rows, through a pointer to a row's first cell, whose fields are found by name
        class RowDefinition final : public Rml::VariableDefinition
        {
        public:
            explicit RowDefinition(const GameUIVector<std::pair<Rml::String, UIDataType>>& fields) : Rml::VariableDefinition(Rml::DataVariableType::Struct), m_Fields(fields)
            {
            }

            Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override
            {
                const auto a_Field = std::ranges::find(m_Fields, address.name, &std::pair<Rml::String, UIDataType>::first);
                if (a_Field == m_Fields.end())
                {
                    return {};
                }

                return Rml::DataVariable(&GetCellDefinition(a_Field->second), static_cast<Cell*>(ptr) + (a_Field - m_Fields.begin()));
            }

            Rml::StringList ReflectMemberNames() override
            {
                Rml::StringList l_Names;
                for (const auto& [it_Name, it_Type] : m_Fields)
                {
                    l_Names.push_back(it_Name);
                }

                return l_Names;
            }

        private:
            const GameUIVector<std::pair<Rml::String, UIDataType>>& m_Fields;
        };

        // A value or a list bound into a model, and the links that reach it from the documents showing its model. A list's cells are its rows', one after another
        struct Variable
        {
            Model* Owner = nullptr;
            Rml::String Name;
            bool List = false;
            UIDataType Type = UIDataType::Int;
            Cell Value;
            GameUIVector<std::pair<Rml::String, UIDataType>> Fields;
            GameUIVector<Cell> Cells;
            std::uint32_t Rows = 0;
            Scope<RowDefinition> Row;
            GameUIVector<Link*> Links;
            std::uint32_t Slot = 0;
            bool Reported = false;
        };

        struct Model
        {
            const Scene* Owner = nullptr;
            UUID Entity;
            Rml::String Name;
            GameUIVector<Scope<Variable>> Variables;
            std::uint32_t Slot = 0;
        };

        // A name a document's context bound into one of its models, which reaches a variable while one is bound to it
        struct Link
        {
            Rml::String Model;
            Rml::String Name;
            Rml::DataModelHandle Handle;
            bool List = false;
            Variable* Target = nullptr;
        };

        // A value through its link, which reads as nothing while none is bound to it. What a control writes there marks it changed in the other documents showing it, as RmlUi marks it in this one
        class ValueLinkDefinition final : public Rml::VariableDefinition
        {
        public:
            ValueLinkDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Scalar)
            {
            }

            bool Get(void* ptr, Rml::Variant& variant) override
            {
                const Variable* l_Target = static_cast<const Link*>(ptr)->Target;
                if (l_Target == nullptr || l_Target->List)
                {
                    return false;
                }

                ToVariant(l_Target->Value, l_Target->Type, variant);

                return true;
            }

            bool Set(void* ptr, const Rml::Variant& variant) override
            {
                const Link* l_Link = static_cast<const Link*>(ptr);
                Variable* l_Target = l_Link->Target;
                if (l_Target == nullptr || l_Target->List)
                {
                    return false;
                }

                const MemoryTagScope l_Scope(MemoryTag::GameUI);
                if (FromVariant(l_Target->Value, l_Target->Type, variant))
                {
                    for (Link* it_Link : l_Target->Links)
                    {
                        if (it_Link != l_Link)
                        {
                            it_Link->Handle.DirtyVariable(l_Target->Name);
                        }
                    }
                }

                return true;
            }
        };

        // A list through its link, empty while none is bound to it, with its size as list.size
        class ListLinkDefinition final : public Rml::VariableDefinition
        {
        public:
            ListLinkDefinition() : Rml::VariableDefinition(Rml::DataVariableType::Array)
            {
            }

            int Size(void* ptr) override
            {
                const Variable* l_Target = static_cast<const Link*>(ptr)->Target;

                return l_Target != nullptr && l_Target->List ? static_cast<int>(l_Target->Rows) : 0;
            }

            Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override
            {
                Variable* l_Target = static_cast<const Link*>(ptr)->Target;
                const int l_Rows = Size(ptr);
                if (l_Target == nullptr || address.index < 0 || address.index >= l_Rows)
                {
                    return address.name == "size" ? Rml::MakeLiteralIntVariable(l_Rows) : Rml::DataVariable();
                }

                return Rml::DataVariable(l_Target->Row.get(), l_Target->Cells.data() + static_cast<std::size_t>(address.index) * l_Target->Fields.size());
            }
        };

        ValueLinkDefinition s_ValueLink;
        ListLinkDefinition s_ListLink;

        // A document's context, the UIDocument it shows and the names it bound
        struct Shown
        {
            UIDocumentID Document = GameUI::c_NoDocument;
            Rml::Context* Context = nullptr;
            const Scene* Owner = nullptr;
            UUID Entity;
            GameUIVector<Scope<Link>> Links;
        };

        // A handle names its slot in its low half and the slot's generation in its high half, which starts at 1, so none is 0
        template<typename T>
        struct Slot
        {
            T* Target = nullptr;
            std::uint32_t Generation = 0;
        };

        template<typename T>
        std::uint64_t Take(GameUIVector<Slot<T>>& slots, T& target, std::uint32_t& index)
        {
            const auto a_Free = std::ranges::find(slots, nullptr, &Slot<T>::Target);
            index = static_cast<std::uint32_t>(a_Free - slots.begin());
            if (a_Free == slots.end())
            {
                slots.emplace_back();
            }

            Slot<T>& l_Slot = slots[index];
            l_Slot.Target = &target;
            ++l_Slot.Generation;

            return static_cast<std::uint64_t>(l_Slot.Generation) << 32 | index;
        }

        template<typename T>
        [[nodiscard]] T* Resolve(const GameUIVector<Slot<T>>& slots, std::uint64_t handle)
        {
            const std::uint32_t l_Index = static_cast<std::uint32_t>(handle);
            if (handle == 0 || l_Index >= slots.size() || slots[l_Index].Generation != static_cast<std::uint32_t>(handle >> 32))
            {
                return nullptr;
            }

            return slots[l_Index].Target;
        }

        [[nodiscard]] std::uint64_t GetHandle(const auto& slots, std::uint32_t index)
        {
            return static_cast<std::uint64_t>(slots[index].Generation) << 32 | index;
        }
    }

    struct GameUIData::State
    {
        GameUIVector<Scope<Model>> Models;
        GameUIVector<Slot<Model>> ModelSlots;
        GameUIVector<Slot<Variable>> VariableSlots;
        GameUIVector<Scope<Shown>> Documents;

        // The link the document's context has for the name, bound into the model there, made by the first value of that name, and kept for the context's life
        Link* FindLink(Shown& document, const Rml::String& model, const Variable& variable)
        {
            for (const Scope<Link>& it_Link : document.Links)
            {
                if (it_Link->Model == model && it_Link->Name == variable.Name)
                {
                    return it_Link.get();
                }
            }

            Rml::DataModelConstructor l_Constructor = HasModel(*document.Context, model) ? document.Context->GetDataModel(model) : document.Context->CreateDataModel(model, nullptr, true);

            Scope<Link>& l_Link = document.Links.emplace_back(CreateScope<Link>());
            l_Link->Model = model;
            l_Link->Name = variable.Name;
            l_Link->Handle = l_Constructor.GetModelHandle();
            l_Link->List = variable.List;
            if (!l_Constructor || !l_Constructor.BindCustomDataVariable(variable.Name, Rml::DataVariable(variable.List ? static_cast<Rml::VariableDefinition*>(&s_ListLink) : &s_ValueLink, l_Link.get())))
            {
                TR_CORE_WARN("GameUI: {} of data model '{}' could not be bound into document {}", variable.Name, model, document.Document);
            }

            return l_Link.get();
        }

        static void Unlink(Link& link)
        {
            if (link.Target != nullptr)
            {
                std::erase(link.Target->Links, &link);
                link.Target = nullptr;
            }
        }

        // The variable reached from the document, which shows it at the next Update. A name a context bound as a value cannot reach a list there, nor the other way, until the document's context is made again
        void Connect(Shown& document, Variable& variable)
        {
            Link* l_Link = FindLink(document, variable.Owner->Name, variable);
            if (l_Link->List != variable.List)
            {
                TR_CORE_WARN("GameUI: data model '{}' of document {} had {} bound as a {}, so it does not show the {} bound now", variable.Owner->Name, document.Document, variable.Name, l_Link->List ? "list" : "value", variable.List ? "list" : "value");

                return;
            }

            if (l_Link->Target == &variable)
            {
                return;
            }

            Unlink(*l_Link);
            l_Link->Target = &variable;
            variable.Links.push_back(l_Link);
            l_Link->Handle.DirtyVariable(variable.Name);
        }

        void ConnectAll(Variable& variable)
        {
            for (const Scope<Shown>& it_Document : Documents)
            {
                if (it_Document->Owner == variable.Owner->Owner && it_Document->Entity == variable.Owner->Entity)
                {
                    Connect(*it_Document, variable);
                }
            }
        }

        static void Dirty(const Variable& variable)
        {
            for (Link* it_Link : variable.Links)
            {
                it_Link->Handle.DirtyVariable(variable.Name);
            }
        }

        void FreeModel(Model& model)
        {
            for (const Scope<Variable>& it_Variable : model.Variables)
            {
                for (Link* it_Link : it_Variable->Links)
                {
                    it_Link->Target = nullptr;
                }

                VariableSlots[it_Variable->Slot].Target = nullptr;
            }

            ModelSlots[model.Slot].Target = nullptr;
        }

        // The model's variable of that name, made with the kind and type asked for, and whether it was made now. Null, logged, for a name RmlUi cannot reach, or one bound before as another kind or type
        std::pair<Variable*, bool> Bind(std::uint64_t model, std::string_view name, bool list, UIDataType type, std::span<const UIDataField> fields)
        {
            Model* l_Model = Resolve(ModelSlots, model);
            if (l_Model == nullptr)
            {
                return {};
            }

            if (!IsLegalName(name))
            {
                TR_CORE_WARN("GameUI: '{}' was not bound into data model '{}', as a name is letters, digits and underscores, not starting with a digit", name, l_Model->Name);

                return {};
            }

            const auto a_Bound = std::ranges::find_if(l_Model->Variables, [name](const Scope<Variable>& variable) { return variable->Name == name; });
            if (a_Bound != l_Model->Variables.end())
            {
                Variable& l_Bound = **a_Bound;
                const bool l_Same = l_Bound.List == list && (list ? std::ranges::equal(l_Bound.Fields, fields, [](const auto& bound, const UIDataField& field) { return bound.first == field.Name && bound.second == field.Type; }) : l_Bound.Type == type);
                if (!l_Same)
                {
                    TR_CORE_WARN("GameUI: {} of data model '{}' was bound before as another {}, so it is not bound again", name, l_Model->Name, l_Bound.List == list ? (list ? "list" : "type") : "kind");

                    return {};
                }

                return { &l_Bound, false };
            }

            for (const UIDataField& it_Field : fields)
            {
                if (!IsLegalName(it_Field.Name) || std::ranges::count(fields, it_Field.Name, &UIDataField::Name) != 1)
                {
                    TR_CORE_WARN("GameUI: list {} of data model '{}' was not bound, as its field '{}' is no name an expression can reach, or is named twice", name, l_Model->Name, it_Field.Name);

                    return {};
                }
            }

            Variable& l_Variable = *l_Model->Variables.emplace_back(CreateScope<Variable>());
            l_Variable.Owner = l_Model;
            l_Variable.Name = Rml::String(name);
            l_Variable.List = list;
            l_Variable.Type = type;
            for (const UIDataField& it_Field : fields)
            {
                l_Variable.Fields.emplace_back(Rml::String(it_Field.Name), it_Field.Type);
            }

            if (list)
            {
                l_Variable.Row = CreateScope<RowDefinition>(l_Variable.Fields);
            }

            static_cast<void>(Take(VariableSlots, l_Variable, l_Variable.Slot));

            return { &l_Variable, true };
        }

        // The list's cell of that row and field, which logs once a list for a row past its end or a field it has not
        Cell* FindCell(Variable& list, std::uint32_t row, std::string_view field, std::optional<UIDataType> type)
        {
            const auto a_Field = std::ranges::find(list.Fields, field, &std::pair<Rml::String, UIDataType>::first);
            if (a_Field == list.Fields.end() || row >= list.Rows || (type && a_Field->second != *type))
            {
                if (!std::exchange(list.Reported, true))
                {
                    const std::string l_Why = a_Field == list.Fields.end() ? std::format("it has no field '{}'", field) : row >= list.Rows ? std::format("it has {} row(s), so no row {}", list.Rows, row) : std::format("its field '{}' holds a {}, not a {}", field, ToString(a_Field->second), ToString(*type));
                    TR_CORE_WARN("GameUI: list {} of data model '{}' was asked for what is not there, as {}", list.Name, list.Owner->Name, l_Why);
                }

                return nullptr;
            }

            return &list.Cells[static_cast<std::size_t>(row) * list.Fields.size() + static_cast<std::size_t>(a_Field - list.Fields.begin())];
        }
    };

    GameUIData::GameUIData() : m_State(CreateScope<State>())
    {

    }

    GameUIData::~GameUIData() = default;

    // A data-model attribute in double or single quotes, which is how RmlUi reads one
    void GameUIData::DeclareModels(Rml::Context& context, std::string_view text)
    {
        constexpr std::string_view c_Attribute = "data-model";
        for (std::size_t l_At = text.find(c_Attribute); l_At != std::string_view::npos; l_At = text.find(c_Attribute, l_At + c_Attribute.size()))
        {
            std::size_t l_Quote = text.find_first_not_of(" \t\r\n", l_At + c_Attribute.size());
            if (l_Quote == std::string_view::npos || text[l_Quote] != '=')
            {
                continue;
            }

            l_Quote = text.find_first_not_of(" \t\r\n", l_Quote + 1);
            if (l_Quote == std::string_view::npos || (text[l_Quote] != '"' && text[l_Quote] != '\''))
            {
                continue;
            }

            const std::size_t l_End = text.find(text[l_Quote], l_Quote + 1);
            const std::string_view l_Name = l_End != std::string_view::npos ? text.substr(l_Quote + 1, l_End - l_Quote - 1) : std::string_view();
            if (IsLegalName(l_Name) && !HasModel(context, Rml::String(l_Name)))
            {
                static_cast<void>(context.CreateDataModel(Rml::String(l_Name), nullptr, true));
            }
        }
    }

    void GameUIData::Attach(UIDocumentID document, Rml::Context& context, const Scene* owner, UUID entity)
    {
        auto a_Shown = std::ranges::find_if(m_State->Documents, [document](const Scope<Shown>& shown) { return shown->Document == document; });
        if (a_Shown != m_State->Documents.end() && (*a_Shown)->Owner == owner && (*a_Shown)->Entity == entity)
        {
            return;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        if (a_Shown == m_State->Documents.end())
        {
            Scope<Shown>& l_New = m_State->Documents.emplace_back(CreateScope<Shown>());
            l_New->Document = document;
            l_New->Context = &context;
            a_Shown = m_State->Documents.end() - 1;
        }

        Shown& l_Shown = **a_Shown;
        l_Shown.Owner = owner;
        l_Shown.Entity = entity;
        for (const Scope<Model>& it_Model : m_State->Models)
        {
            if (it_Model->Owner != owner || it_Model->Entity != entity)
            {
                continue;
            }

            for (const Scope<Variable>& it_Variable : it_Model->Variables)
            {
                m_State->Connect(l_Shown, *it_Variable);
            }
        }
    }

    void GameUIData::Detach(UIDocumentID document)
    {
        const auto a_Shown = std::ranges::find_if(m_State->Documents, [document](const Scope<Shown>& shown) { return shown->Document == document; });
        if (a_Shown == m_State->Documents.end())
        {
            return;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        for (const Scope<Link>& it_Link : (*a_Shown)->Links)
        {
            State::Unlink(*it_Link);
        }

        m_State->Documents.erase(a_Shown);
    }

    void GameUIData::ReleaseScene(const Scene* scene)
    {
        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        std::erase_if(m_State->Models, [this, scene](Scope<Model>& model)
        {
            if (model->Owner != scene)
            {
                return false;
            }

            m_State->FreeModel(*model);

            return true;
        });
    }

    std::uint64_t GameUIData::GetModel(const Scene& owner, UUID entity, std::string_view name)
    {
        const auto a_Model = std::ranges::find_if(m_State->Models, [&](const Scope<Model>& model) { return model->Owner == &owner && model->Entity == entity && model->Name == name; });
        if (a_Model != m_State->Models.end())
        {
            return GetHandle(m_State->ModelSlots, (*a_Model)->Slot);
        }

        if (!IsLegalName(name))
        {
            TR_CORE_WARN("GameUI: '{}' is no data model's name, which is letters, digits and underscores, not starting with a digit", name);

            return 0;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        Model& l_Model = *m_State->Models.emplace_back(CreateScope<Model>());
        l_Model.Owner = &owner;
        l_Model.Entity = entity;
        l_Model.Name = Rml::String(name);

        return Take(m_State->ModelSlots, l_Model, l_Model.Slot);
    }

    bool GameUIData::IsModel(std::uint64_t model) const
    {
        return Resolve(m_State->ModelSlots, model) != nullptr;
    }

    std::uint64_t GameUIData::BindValue(std::uint64_t model, std::string_view name, const UIDataScalar& value)
    {
        // A new one takes the value given, and one bound before keeps what it holds. Either is shown by the documents showing its model
        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        const auto [l_Variable, l_Made] = m_State->Bind(model, name, false, value.Type, {});
        if (l_Variable == nullptr)
        {
            return 0;
        }

        if (l_Made)
        {
            static_cast<void>(Write(l_Variable->Value, value.Type, value));
        }

        m_State->ConnectAll(*l_Variable);

        return GetHandle(m_State->VariableSlots, l_Variable->Slot);
    }

    std::uint64_t GameUIData::BindList(std::uint64_t model, std::string_view name, std::span<const UIDataField> fields)
    {
        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        Variable* l_Variable = m_State->Bind(model, name, true, UIDataType::Int, fields).first;
        if (l_Variable == nullptr)
        {
            return 0;
        }

        m_State->ConnectAll(*l_Variable);

        return GetHandle(m_State->VariableSlots, l_Variable->Slot);
    }

    bool GameUIData::IsValue(std::uint64_t value) const
    {
        const Variable* l_Variable = Resolve(m_State->VariableSlots, value);

        return l_Variable != nullptr && !l_Variable->List;
    }

    std::optional<UIDataType> GameUIData::GetType(std::uint64_t value) const
    {
        const Variable* l_Variable = Resolve(m_State->VariableSlots, value);

        return l_Variable != nullptr && !l_Variable->List ? std::optional(l_Variable->Type) : std::nullopt;
    }

    void GameUIData::Set(std::uint64_t value, const UIDataScalar& scalar)
    {
        Variable* l_Variable = Resolve(m_State->VariableSlots, value);
        if (l_Variable == nullptr || l_Variable->List)
        {
            return;
        }

        if (l_Variable->Type != scalar.Type)
        {
            if (!std::exchange(l_Variable->Reported, true))
            {
                TR_CORE_WARN("GameUI: {} of data model '{}' holds a {}, so a {} set into it is refused", l_Variable->Name, l_Variable->Owner->Name, ToString(l_Variable->Type), ToString(scalar.Type));
            }

            return;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        if (Write(l_Variable->Value, l_Variable->Type, scalar))
        {
            State::Dirty(*l_Variable);
        }
    }

    std::optional<UIDataScalar> GameUIData::Get(std::uint64_t value) const
    {
        const Variable* l_Variable = Resolve(m_State->VariableSlots, value);

        return l_Variable != nullptr && !l_Variable->List ? std::optional(Read(l_Variable->Value, l_Variable->Type)) : std::nullopt;
    }

    bool GameUIData::IsList(std::uint64_t list) const
    {
        const Variable* l_Variable = Resolve(m_State->VariableSlots, list);

        return l_Variable != nullptr && l_Variable->List;
    }

    std::uint32_t GameUIData::GetSize(std::uint64_t list) const
    {
        const Variable* l_Variable = Resolve(m_State->VariableSlots, list);

        return l_Variable != nullptr && l_Variable->List ? l_Variable->Rows : 0;
    }

    // Cells are kept as rows go, so a list that shrinks and grows again allocates nothing more
    void GameUIData::Resize(std::uint64_t list, std::uint32_t rows)
    {
        Variable* l_Variable = Resolve(m_State->VariableSlots, list);
        if (l_Variable == nullptr || !l_Variable->List || l_Variable->Rows == rows)
        {
            return;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        const std::size_t l_Fields = l_Variable->Fields.size();
        const std::size_t l_Cells = static_cast<std::size_t>(rows) * l_Fields;
        if (l_Variable->Cells.size() < l_Cells)
        {
            l_Variable->Cells.resize(l_Cells);
        }

        for (std::size_t it_Cell = static_cast<std::size_t>(std::min(rows, l_Variable->Rows)) * l_Fields; it_Cell < l_Cells; ++it_Cell)
        {
            Cell& l_Cell = l_Variable->Cells[it_Cell];
            l_Cell.Int = 0;
            l_Cell.Float = 0.0f;
            l_Cell.Bool = false;
            l_Cell.Text.clear();
        }

        l_Variable->Rows = rows;
        State::Dirty(*l_Variable);
    }

    void GameUIData::Set(std::uint64_t list, std::uint32_t row, std::string_view field, const UIDataScalar& scalar)
    {
        Variable* l_Variable = Resolve(m_State->VariableSlots, list);
        Cell* l_Cell = l_Variable != nullptr && l_Variable->List ? m_State->FindCell(*l_Variable, row, field, scalar.Type) : nullptr;
        if (l_Cell == nullptr)
        {
            return;
        }

        const MemoryTagScope l_Scope(MemoryTag::GameUI);
        if (Write(*l_Cell, scalar.Type, scalar))
        {
            State::Dirty(*l_Variable);
        }
    }

    std::optional<UIDataScalar> GameUIData::Get(std::uint64_t list, std::uint32_t row, std::string_view field) const
    {
        Variable* l_Variable = Resolve(m_State->VariableSlots, list);
        const Cell* l_Cell = l_Variable != nullptr && l_Variable->List ? m_State->FindCell(*l_Variable, row, field, std::nullopt) : nullptr;
        if (l_Cell == nullptr)
        {
            return std::nullopt;
        }

        const auto a_Field = std::ranges::find(l_Variable->Fields, field, &std::pair<Rml::String, UIDataType>::first);

        return Read(*l_Cell, a_Field->second);
    }

    std::uint32_t GameUIData::GetModelCount() const
    {
        return static_cast<std::uint32_t>(m_State->Models.size());
    }

    std::uint32_t GameUIData::GetVariableCount() const
    {
        return static_cast<std::uint32_t>(std::ranges::count_if(m_State->VariableSlots, [](const Slot<Variable>& slot) { return slot.Target != nullptr; }));
    }

    std::string_view ToString(UIDataType type)
    {
        switch (type)
        {
            case UIDataType::Int:
            {
                return "Int";
            }
            case UIDataType::Float:
            {
                return "Float";
            }
            case UIDataType::Bool:
            {
                return "Bool";
            }
            case UIDataType::String:
            {
                return "String";
            }
        }

        return "Unknown";
    }

    namespace
    {
        [[nodiscard]] GameUIData* GetData()
        {
            GameUI* l_UI = GameUIAccess::GetUI();

            return l_UI != nullptr ? &GameUIAccess::GetData(*l_UI) : nullptr;
        }

        [[nodiscard]] UIDataScalar Scalar(std::int32_t value)
        {
            UIDataScalar l_Scalar;
            l_Scalar.Type = UIDataType::Int;
            l_Scalar.Int = value;

            return l_Scalar;
        }

        [[nodiscard]] UIDataScalar Scalar(float value)
        {
            UIDataScalar l_Scalar;
            l_Scalar.Type = UIDataType::Float;
            l_Scalar.Float = value;

            return l_Scalar;
        }

        [[nodiscard]] UIDataScalar Scalar(bool value)
        {
            UIDataScalar l_Scalar;
            l_Scalar.Type = UIDataType::Bool;
            l_Scalar.Bool = value;

            return l_Scalar;
        }

        [[nodiscard]] UIDataScalar Scalar(std::string_view value)
        {
            UIDataScalar l_Scalar;
            l_Scalar.Type = UIDataType::String;
            l_Scalar.Text = value;

            return l_Scalar;
        }

        // A value read as the type asked for, or the type's default for none or another type
        [[nodiscard]] UIDataScalar Expect(const std::optional<UIDataScalar>& value, UIDataType type)
        {
            if (value && value->Type == type)
            {
                return *value;
            }

            UIDataScalar l_Default;
            l_Default.Type = type;

            return l_Default;
        }
    }

    bool UIDataValue::IsValid() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr && l_Data->IsValue(m_Handle);
    }

    UIDataType UIDataValue::GetType() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr ? l_Data->GetType(m_Handle).value_or(UIDataType::Int) : UIDataType::Int;
    }

    void UIDataValue::Set(std::int32_t value) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Set(m_Handle, Scalar(value));
        }
    }

    void UIDataValue::Set(float value) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Set(m_Handle, Scalar(value));
        }
    }

    void UIDataValue::Set(bool value) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Set(m_Handle, Scalar(value));
        }
    }

    void UIDataValue::Set(std::string_view value) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Set(m_Handle, Scalar(value));
        }
    }

    std::int32_t UIDataValue::GetInt() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr ? Expect(l_Data->Get(m_Handle), UIDataType::Int).Int : 0;
    }

    float UIDataValue::GetFloat() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr ? Expect(l_Data->Get(m_Handle), UIDataType::Float).Float : 0.0f;
    }

    bool UIDataValue::GetBool() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr && Expect(l_Data->Get(m_Handle), UIDataType::Bool).Bool;
    }

    std::string UIDataValue::GetString() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr ? std::string(Expect(l_Data->Get(m_Handle), UIDataType::String).Text) : std::string();
    }

    bool UIDataList::IsValid() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr && l_Data->IsList(m_Handle);
    }

    std::uint32_t UIDataList::GetSize() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr ? l_Data->GetSize(m_Handle) : 0;
    }

    void UIDataList::Resize(std::uint32_t rows) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Resize(m_Handle, rows);
        }
    }

    void UIDataList::Set(std::uint32_t row, std::string_view field, std::int32_t value) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Set(m_Handle, row, field, Scalar(value));
        }
    }

    void UIDataList::Set(std::uint32_t row, std::string_view field, float value) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Set(m_Handle, row, field, Scalar(value));
        }
    }

    void UIDataList::Set(std::uint32_t row, std::string_view field, bool value) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Set(m_Handle, row, field, Scalar(value));
        }
    }

    void UIDataList::Set(std::uint32_t row, std::string_view field, std::string_view value) const
    {
        if (GameUIData* l_Data = GetData())
        {
            l_Data->Set(m_Handle, row, field, Scalar(value));
        }
    }

    std::int32_t UIDataList::GetInt(std::uint32_t row, std::string_view field) const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr ? Expect(l_Data->Get(m_Handle, row, field), UIDataType::Int).Int : 0;
    }

    float UIDataList::GetFloat(std::uint32_t row, std::string_view field) const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr ? Expect(l_Data->Get(m_Handle, row, field), UIDataType::Float).Float : 0.0f;
    }

    bool UIDataList::GetBool(std::uint32_t row, std::string_view field) const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr && Expect(l_Data->Get(m_Handle, row, field), UIDataType::Bool).Bool;
    }

    std::string UIDataList::GetString(std::uint32_t row, std::string_view field) const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr ? std::string(Expect(l_Data->Get(m_Handle, row, field), UIDataType::String).Text) : std::string();
    }

    bool UIDataModel::IsValid() const
    {
        const GameUIData* l_Data = GetData();

        return l_Data != nullptr && l_Data->IsModel(m_Handle);
    }

    UIDataValue UIDataModel::BindInt(std::string_view name, std::int32_t value) const
    {
        GameUIData* l_Data = GetData();

        return UIDataValue::FromHandle(l_Data != nullptr ? l_Data->BindValue(m_Handle, name, Scalar(value)) : 0);
    }

    UIDataValue UIDataModel::BindFloat(std::string_view name, float value) const
    {
        GameUIData* l_Data = GetData();

        return UIDataValue::FromHandle(l_Data != nullptr ? l_Data->BindValue(m_Handle, name, Scalar(value)) : 0);
    }

    UIDataValue UIDataModel::BindBool(std::string_view name, bool value) const
    {
        GameUIData* l_Data = GetData();

        return UIDataValue::FromHandle(l_Data != nullptr ? l_Data->BindValue(m_Handle, name, Scalar(value)) : 0);
    }

    UIDataValue UIDataModel::BindString(std::string_view name, std::string_view value) const
    {
        GameUIData* l_Data = GetData();

        return UIDataValue::FromHandle(l_Data != nullptr ? l_Data->BindValue(m_Handle, name, Scalar(value)) : 0);
    }

    UIDataList UIDataModel::BindList(std::string_view name, std::span<const UIDataField> fields) const
    {
        GameUIData* l_Data = GetData();

        return UIDataList::FromHandle(l_Data != nullptr ? l_Data->BindList(m_Handle, name, fields) : 0);
    }
}