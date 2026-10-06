#pragma once

#include "CommandStack.hpp"

#include <Trinity.hpp>

#include <concepts>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

[[nodiscard]] std::string GetEntityLabel(Trinity::Entity entity);
[[nodiscard]] std::string_view GetComponentLabel(std::string_view name);

class CreateEntityCommand final : public EditorCommand
{
public:
    CreateEntityCommand(std::string name, Trinity::UUID parent, Trinity::UUID before);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    std::string m_Name;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    Trinity::UUID m_Entity;
    std::string m_Text;
    std::string m_Label;
};

class DeleteEntityCommand final : public EditorCommand
{
public:
    explicit DeleteEntityCommand(Trinity::UUID entity);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    std::string m_Text;
    std::string m_Label;
};

class DuplicateEntityCommand final : public EditorCommand
{
public:
    explicit DuplicateEntityCommand(Trinity::UUID source);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Copy; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Source;
    Trinity::UUID m_Copy;
    Trinity::UUID m_Parent;
    Trinity::UUID m_Before;
    std::string m_Text;
    std::string m_Label;
};

class MoveEntityCommand final : public EditorCommand
{
public:
    MoveEntityCommand(Trinity::UUID entity, Trinity::UUID parent, Trinity::UUID before, bool keepWorldTransform);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    Trinity::UUID m_NewParent;
    Trinity::UUID m_NewBefore;
    Trinity::UUID m_OldParent;
    Trinity::UUID m_OldBefore;
    Trinity::TransformComponent m_OldTransform;
    Trinity::TransformComponent m_NewTransform;
    std::string m_Label;
    bool m_KeepWorldTransform = true;
    bool m_Executed = false;
};

class AddComponentCommand final : public EditorCommand
{
public:
    AddComponentCommand(Trinity::UUID entity, std::string component);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    std::string m_Component;
    std::string m_Label;
};

class RemoveComponentCommand final : public EditorCommand
{
public:
    RemoveComponentCommand(Trinity::UUID entity, std::string component);

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override;
    void Undo(Trinity::Scene& scene) override;

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }
    [[nodiscard]] std::size_t GetMemorySize() const override;

private:
    Trinity::UUID m_Entity;
    std::string m_Component;
    std::string m_Text;
    std::string m_Label;
};

template<Trinity::Component T>
class SetComponentCommand final : public EditorCommand
{
public:
    SetComponentCommand(Trinity::UUID entity, T value, std::string field) : m_Entity(entity), m_New(std::move(value)), m_Field(std::move(field))
    {

    }

    [[nodiscard]] bool Execute(Trinity::Scene& scene) override
    {
        Trinity::Entity l_Entity = scene.FindEntityByUUID(m_Entity);
        if (!l_Entity || !l_Entity.Has<T>())
        {
            return false;
        }

        T& l_Component = l_Entity.Get<T>();
        if (!m_Executed)
        {
            if constexpr (std::equality_comparable<T>)
            {
                if (l_Component == m_New)
                {
                    return false;
                }
            }

            m_Old = l_Component;
            m_Label = m_Field.empty() ? std::format("Edit {} of {}", GetComponentLabel(T::c_TypeName), GetEntityLabel(l_Entity)) : std::format("Edit {} {} of {}", GetComponentLabel(T::c_TypeName), m_Field, GetEntityLabel(l_Entity));
            m_Executed = true;
        }

        l_Component = m_New;

        return true;
    }

    void Undo(Trinity::Scene& scene) override
    {
        scene.FindEntityByUUID(m_Entity).Get<T>() = m_Old;
    }

    [[nodiscard]] bool MergeWith(const EditorCommand& next) override
    {
        const SetComponentCommand* l_Next = dynamic_cast<const SetComponentCommand*>(&next);
        if (l_Next == nullptr || l_Next->m_Entity != m_Entity || l_Next->m_Field != m_Field)
        {
            return false;
        }

        m_New = l_Next->m_New;

        return true;
    }

    [[nodiscard]] std::string GetName() const override { return m_Label; }
    [[nodiscard]] Trinity::UUID GetSubject() const override { return m_Entity; }

    [[nodiscard]] std::size_t GetMemorySize() const override
    {
        std::size_t l_Size = sizeof(*this) + m_Field.capacity() + m_Label.capacity();
        if constexpr (std::is_same_v<T, Trinity::TagComponent>)
        {
            l_Size += m_Old.Tag.capacity() + m_New.Tag.capacity();
        }

        return l_Size;
    }

private:
    Trinity::UUID m_Entity;
    T m_Old{};
    T m_New;
    std::string m_Field;
    std::string m_Label;
    bool m_Executed = false;
};