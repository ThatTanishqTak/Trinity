#include "CommandStack.hpp"

#include <algorithm>
#include <utility>

CommandStack::CommandStack(Trinity::Scene& scene, Trinity::UUID& selection, std::size_t capacity) : m_Scene(scene), m_Selection(selection), m_Capacity(capacity)
{

}

// The command runs before it is kept, and one that changes nothing is not kept. Anything that could have been redone is dropped. While a drag goes on, a command merges into the one before it, unless the scene was saved in between, since the saved state would be lost
bool CommandStack::Execute(Trinity::Scope<EditorCommand> command)
{
    if (!command || !command->Execute(m_Scene))
    {
        return false;
    }

    DropRedo();

    if (m_MergeOpen && m_Position > 0 && m_SavedPosition != m_Position)
    {
        EditorCommand& l_Last = *m_Commands.back();
        const std::size_t l_Size = l_Last.GetMemorySize();
        if (l_Last.MergeWith(*command))
        {
            m_MemorySize = m_MemorySize - l_Size + l_Last.GetMemorySize();
            Select(l_Last);
            Trim();

            return true;
        }
    }

    m_MemorySize += command->GetMemorySize();
    m_Commands.push_back(std::move(command));
    ++m_Position;
    m_MergeOpen = true;
    Select(*m_Commands.back());
    Trim();

    return true;
}

bool CommandStack::Undo()
{
    m_MergeOpen = false;
    if (m_Position == 0)
    {
        return false;
    }

    EditorCommand& l_Command = *m_Commands[--m_Position];
    l_Command.Undo(m_Scene);
    Select(l_Command);

    return true;
}

// The scene is as it was when the command first ran, so it can only fail if the history no longer matches the scene, and then nothing after it can be trusted
bool CommandStack::Redo()
{
    m_MergeOpen = false;
    if (m_Position == m_Commands.size())
    {
        return false;
    }

    EditorCommand& l_Command = *m_Commands[m_Position];
    if (!l_Command.Execute(m_Scene))
    {
        TR_ERROR("Forge: {} could not be redone, so the history after it is dropped", l_Command.GetName());
        DropRedo();

        return false;
    }

    ++m_Position;
    Select(l_Command);

    return true;
}

// Undoes or redoes one command at a time until the history stands at the position, which counts the commands applied
void CommandStack::JumpTo(std::size_t position)
{
    position = std::min(position, m_Commands.size());
    while (m_Position > position)
    {
        Undo();
    }

    while (m_Position < position)
    {
        if (!Redo())
        {
            break;
        }
    }
}

// For a scene just opened or created, which is its own saved state
void CommandStack::Clear()
{
    m_Commands.clear();
    m_Position = 0;
    m_SavedPosition = 0;
    m_DroppedCount = 0;
    m_MemorySize = 0;
    m_MergeOpen = false;
}

void CommandStack::MarkSaved()
{
    m_SavedPosition = m_Position;
    m_MergeOpen = false;
}

// What the command touched, while it is in the scene. One that is gone, such as a deleted entity, leaves nothing selected
void CommandStack::Select(const EditorCommand& command)
{
    const Trinity::UUID l_Subject = command.GetSubject();
    m_Selection = m_Scene.FindEntityByUUID(l_Subject) ? l_Subject : Trinity::UUID();
}

// A saved state among the dropped commands can no longer be reached
void CommandStack::DropRedo()
{
    while (m_Commands.size() > m_Position)
    {
        m_MemorySize -= m_Commands.back()->GetMemorySize();
        m_Commands.pop_back();
    }

    if (m_SavedPosition && *m_SavedPosition > m_Position)
    {
        m_SavedPosition.reset();
    }
}

// The oldest commands go first, and with them the way back to the scene before them. Only called with the newest command applied, so the position follows the count
void CommandStack::Trim()
{
    while (m_MemorySize > m_Capacity && !m_Commands.empty())
    {
        m_MemorySize -= m_Commands.front()->GetMemorySize();
        m_Commands.pop_front();
        --m_Position;
        ++m_DroppedCount;

        if (m_SavedPosition)
        {
            m_SavedPosition = *m_SavedPosition > 0 ? std::optional<std::size_t>(*m_SavedPosition - 1) : std::nullopt;
        }
    }

    m_MergeOpen = m_MergeOpen && !m_Commands.empty();
}