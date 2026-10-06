#pragma once

#include <Trinity.hpp>

#include <cstddef>
#include <deque>
#include <optional>
#include <string>

class EditorCommand
{
public:
    virtual ~EditorCommand() = default;

    [[nodiscard]] virtual bool Execute(Trinity::Scene& scene) = 0;
    virtual void Undo(Trinity::Scene& scene) = 0;
    [[nodiscard]] virtual bool MergeWith([[maybe_unused]] const EditorCommand& next) { return false; }

    [[nodiscard]] virtual std::string GetName() const = 0;
    [[nodiscard]] virtual Trinity::UUID GetSubject() const = 0;
    [[nodiscard]] virtual std::size_t GetMemorySize() const = 0;
};

class CommandStack
{
public:
    static constexpr std::size_t c_DefaultCapacity = std::size_t{ 64 } * 1024 * 1024;

    CommandStack(Trinity::Scene& scene, Trinity::UUID& selection, std::size_t capacity = c_DefaultCapacity);

    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;

    bool Execute(Trinity::Scope<EditorCommand> command);
    void EndMerge() { m_MergeOpen = false; }

    bool Undo();
    bool Redo();
    void JumpTo(std::size_t position);

    void Clear();
    void MarkSaved();

    [[nodiscard]] bool CanUndo() const { return m_Position > 0; }
    [[nodiscard]] bool CanRedo() const { return m_Position < m_Commands.size(); }
    [[nodiscard]] bool IsSaved() const { return m_SavedPosition == m_Position; }

    [[nodiscard]] std::size_t GetCount() const { return m_Commands.size(); }
    [[nodiscard]] std::size_t GetPosition() const { return m_Position; }
    [[nodiscard]] const EditorCommand& GetCommand(std::size_t index) const { return *m_Commands[index]; }
    [[nodiscard]] std::size_t GetDroppedCount() const { return m_DroppedCount; }
    [[nodiscard]] std::size_t GetMemorySize() const { return m_MemorySize; }
    [[nodiscard]] std::size_t GetCapacity() const { return m_Capacity; }

private:
    void Select(const EditorCommand& command);
    void DropRedo();
    void Trim();

    Trinity::Scene& m_Scene;
    Trinity::UUID& m_Selection;
    std::deque<Trinity::Scope<EditorCommand>> m_Commands;
    std::size_t m_Position = 0;
    std::optional<std::size_t> m_SavedPosition = 0;
    std::size_t m_DroppedCount = 0;
    std::size_t m_MemorySize = 0;
    std::size_t m_Capacity = 0;
    bool m_MergeOpen = false;
};