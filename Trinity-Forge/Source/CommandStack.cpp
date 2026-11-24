#include "CommandStack.hpp"

#include <algorithm>
#include <utility>

CommandStack::CommandStack(Trinity::Scene& scene, EditorSelection& selection, std::size_t capacity) : m_Scene(scene), m_Selection(selection), m_Capacity(capacity)
{

}

// One that changes nothing is not kept. During a drag it merges into the one before, unless saved
bool CommandStack::Execute(Trinity::Scope<EditorCommand> command)
{
    if (!command || RefuseLocked(command->ChangesScene()) || !command->Execute(m_Scene))
    {
        return false;
    }

    ++m_Revision;
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
    if (m_Position == 0 || RefuseLocked(m_Commands[m_Position - 1]->ChangesScene()))
    {
        return false;
    }

    EditorCommand& l_Command = *m_Commands[--m_Position];
    l_Command.Undo(m_Scene);
    ++m_Revision;
    Select(l_Command);

    return true;
}

// Fails only if the history no longer matches the scene, and then nothing after it can be trusted
bool CommandStack::Redo()
{
    m_MergeOpen = false;
    if (m_Position == m_Commands.size() || RefuseLocked(m_Commands[m_Position]->ChangesScene()))
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
    ++m_Revision;
    Select(l_Command);

    return true;
}

// Undoes or redoes one command at a time until the history stands at the position
void CommandStack::JumpTo(std::size_t position)
{
    if (RefuseLocked())
    {
        return;
    }

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
    ++m_Revision;
}

void CommandStack::MarkSaved()
{
    m_SavedPosition = m_Position;
    m_MergeOpen = false;
}

// When only asset edits lie between the saved state and now, since they are already in their files
bool CommandStack::IsSaved() const
{
    if (!m_SavedPosition)
    {
        return false;
    }

    const std::size_t l_First = std::min(*m_SavedPosition, m_Position);
    const std::size_t l_End = std::max(*m_SavedPosition, m_Position);
    for (std::size_t it_Index = l_First; it_Index < l_End; ++it_Index)
    {
        if (m_Commands[it_Index]->ChangesScene())
        {
            return false;
        }
    }

    return true;
}

// A jump through the history may pass edits of the scene, so it is refused whatever lies between
bool CommandStack::RefuseLocked(bool changesScene)
{
    if (!m_Locked || !changesScene)
    {
        return false;
    }

    if (!std::exchange(m_WarnedLocked, true))
    {
        TR_WARN("Forge: the scene cannot be edited while it plays, though assets can. Stop first");
    }

    return true;
}

// What the command touched, alone, of which what is still in the scene. A deleted entity leaves nothing selected
void CommandStack::Select(const EditorCommand& command)
{
    if (!command.SelectsSubject())
    {
        return;
    }

    std::vector<Trinity::UUID> l_Subjects = command.GetSubjects();
    std::erase_if(l_Subjects, [this](Trinity::UUID subject) { return !m_Scene.FindEntityByUUID(subject); });
    m_Selection.Set(std::move(l_Subjects));
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

// The oldest commands go first. Only called with the newest applied, so the position follows
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