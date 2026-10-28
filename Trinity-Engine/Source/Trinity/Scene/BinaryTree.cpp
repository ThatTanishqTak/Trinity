#include "Trinity/Scene/BinaryTree.hpp"

#include "Trinity/Core/Assert.hpp"

namespace Trinity
{
    bool BinaryTree::IsValid() const
    {
        for (std::size_t it_Index = 0; it_Index < Nodes.size(); ++it_Index)
        {
            const BinaryTreeNode& l_Node = Nodes[it_Index];
            switch (l_Node.Kind)
            {
                case BinaryNodeKind::Null:
                {
                    break;
                }
                case BinaryNodeKind::Plain:
                case BinaryNodeKind::String:
                {
                    if (l_Node.First >= Strings.size())
                    {
                        return false;
                    }

                    break;
                }
                case BinaryNodeKind::BlockSequence:
                case BinaryNodeKind::FlowSequence:
                case BinaryNodeKind::BlockMap:
                case BinaryNodeKind::FlowMap:
                {
                    if (l_Node.First > Entries.size() || l_Node.Count > Entries.size() - l_Node.First)
                    {
                        return false;
                    }

                    const bool l_Map = l_Node.Kind == BinaryNodeKind::BlockMap || l_Node.Kind == BinaryNodeKind::FlowMap;
                    for (std::uint32_t it_Entry = l_Node.First; it_Entry < l_Node.First + l_Node.Count; ++it_Entry)
                    {
                        const BinaryTreeEntry& l_Entry = Entries[it_Entry];
                        if (l_Entry.Value >= it_Index || (l_Map ? l_Entry.Key >= Strings.size() : l_Entry.Key != BinaryTreeEntry::c_NoKey))
                        {
                            return false;
                        }
                    }

                    break;
                }
                default:
                {
                    return false;
                }
            }
        }

        return true;
    }

    std::uint32_t BinaryTreeBuilder::AddString(std::string_view text)
    {
        if (const auto a_Found = m_StringIndices.find(text); a_Found != m_StringIndices.end())
        {
            return a_Found->second;
        }

        const std::uint32_t l_Index = static_cast<std::uint32_t>(m_Strings.size());
        const auto a_Inserted = m_StringIndices.emplace(std::string(text), l_Index).first;
        m_Strings.push_back(a_Inserted->first);

        return l_Index;
    }

    std::uint32_t BinaryTreeBuilder::AddNode(BinaryNodeKind kind, std::uint32_t first, std::uint32_t count)
    {
        BinaryTreeNode l_Node;
        l_Node.Kind = kind;
        l_Node.First = first;
        l_Node.Count = count;
        m_Nodes.push_back(l_Node);

        return static_cast<std::uint32_t>(m_Nodes.size() - 1);
    }

    void BinaryTreeBuilder::BeginComponent()
    {
        TR_CORE_ASSERT(m_Frames.empty(), "BinaryTreeBuilder: a component begun inside another");

        Frame l_Frame;
        l_Frame.Pending = m_Pending.size();
        l_Frame.Component = true;
        m_Frames.push_back(l_Frame);
    }

    std::uint32_t BinaryTreeBuilder::EndComponent()
    {
        TR_CORE_ASSERT(m_Frames.size() == 1 && m_Frames.back().Component, "BinaryTreeBuilder: a component ended with a block still open");

        const Frame l_Frame = m_Frames.back();
        m_Frames.pop_back();
        if (l_Frame.ValueKind != BinaryNodeKind::Null)
        {
            return AddNode(l_Frame.ValueKind, l_Frame.Value, 0);
        }

        if (m_Pending.size() == l_Frame.Pending)
        {
            return AddNode(BinaryNodeKind::Null, 0, 0);
        }

        return Place(l_Frame);
    }

    void BinaryTreeBuilder::AddScalar(std::string_view key, BinaryNodeKind kind, std::string_view text)
    {
        const std::uint32_t l_Node = AddNode(kind, AddString(text), 0);
        m_Pending.push_back({ GetKey(key), l_Node });
    }

    void BinaryTreeBuilder::SetValue(BinaryNodeKind kind, std::string_view text)
    {
        TR_CORE_ASSERT(m_Frames.size() == 1 && m_Frames.back().Component, "BinaryTreeBuilder: a value given to a block inside a component");

        m_Frames.back().ValueKind = kind;
        m_Frames.back().Value = AddString(text);
    }

    void BinaryTreeBuilder::Begin(std::string_view key, BinaryNodeKind kind)
    {
        Frame l_Frame;
        l_Frame.Kind = kind;
        l_Frame.Key = GetKey(key);
        l_Frame.Pending = m_Pending.size();
        m_Frames.push_back(l_Frame);
    }

    void BinaryTreeBuilder::BeginItem()
    {
        if (m_Frames.back().Item)
        {
            Close();
        }

        TR_CORE_ASSERT(m_Frames.back().Kind == BinaryNodeKind::BlockSequence, "BinaryTreeBuilder: an item outside a block sequence");

        Frame l_Frame;
        l_Frame.Pending = m_Pending.size();
        l_Frame.Item = true;
        m_Frames.push_back(l_Frame);
    }

    void BinaryTreeBuilder::End()
    {
        if (m_Frames.back().Item)
        {
            Close();
        }

        TR_CORE_ASSERT(!m_Frames.back().Component, "BinaryTreeBuilder: End without a block to end");

        Close();
    }

    std::uint32_t BinaryTreeBuilder::GetKey(std::string_view key)
    {
        const BinaryNodeKind l_Parent = m_Frames.back().Kind;

        return l_Parent == BinaryNodeKind::BlockSequence || l_Parent == BinaryNodeKind::FlowSequence ? BinaryTreeEntry::c_NoKey : AddString(key);
    }

    // The frame's entries put together, after those of every node inside it, and its node after theirs
    std::uint32_t BinaryTreeBuilder::Place(const Frame& frame)
    {
        const std::uint32_t l_First = static_cast<std::uint32_t>(m_Entries.size());
        const std::uint32_t l_Count = static_cast<std::uint32_t>(m_Pending.size() - frame.Pending);
        m_Entries.insert(m_Entries.end(), m_Pending.begin() + static_cast<std::ptrdiff_t>(frame.Pending), m_Pending.end());
        m_Pending.resize(frame.Pending);

        return AddNode(frame.Kind, l_First, l_Count);
    }

    void BinaryTreeBuilder::Close()
    {
        const Frame l_Frame = m_Frames.back();
        m_Frames.pop_back();
        const std::uint32_t l_Node = Place(l_Frame);
        m_Pending.push_back({ l_Frame.Key, l_Node });
    }
}