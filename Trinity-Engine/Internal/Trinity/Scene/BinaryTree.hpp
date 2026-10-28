#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Trinity
{
    // YAML's tree without its text, as binary scenes hold a component: maps, sequences and scalars, each scalar the text YAML would read, so a value reads back exactly as it does from a scene file. A sequence or map keeps whether YAML writes it on one line, and a scalar whether YAML would quote it, so the tree gives back the same text
    enum class BinaryNodeKind : std::uint8_t
    {
        Null,
        // A number, a boolean or a UUID, written as it is
        Plain,
        // Text, which YAML quotes where it could be taken for anything else
        String,
        BlockSequence,
        FlowSequence,
        BlockMap,
        FlowMap
    };

    struct BinaryTreeNode
    {
        BinaryNodeKind Kind = BinaryNodeKind::Null;
        std::array<std::uint8_t, 3> Padding{};
        // A scalar's string, or a sequence's or map's first entry
        std::uint32_t First = 0;
        std::uint32_t Count = 0;
    };

    static_assert(sizeof(BinaryTreeNode) == 12);

    // A sequence's item, which has no key, or a map's key, with the node it holds
    struct BinaryTreeEntry
    {
        static constexpr std::uint32_t c_NoKey = 0xFFFFFFFF;

        std::uint32_t Key = c_NoKey;
        std::uint32_t Value = 0;
    };

    static_assert(sizeof(BinaryTreeEntry) == 8);

    // A tree's tables as read, its strings views into the bytes they were read from, which must outlive it. Every node's entries come before it, so a tree read whole has no loop
    struct BinaryTree
    {
        std::vector<std::string_view> Strings;
        std::vector<BinaryTreeNode> Nodes;
        std::vector<BinaryTreeEntry> Entries;

        // Each string, node and entry in range, and each node's entries before it
        [[nodiscard]] bool IsValid() const;
    };

    class BinaryNodeIterator;

    // A map's key, read as a YAML::Node's key is
    struct BinaryNodeKey
    {
        std::string_view Text;

        [[nodiscard]] bool IsScalar() const { return true; }
        [[nodiscard]] std::string_view Scalar() const { return Text; }
    };

    // One node of a tree, read as a YAML::Node is, so the component reader's helpers read either. A node a lookup does not find reads as false
    class BinaryNode
    {
    public:
        BinaryNode() = default;
        BinaryNode(const BinaryTree& tree, std::uint32_t index) : m_Tree(&tree), m_Index(index)
        {

        }

        explicit operator bool() const { return m_Tree != nullptr; }

        [[nodiscard]] BinaryNodeKind GetKind() const { return m_Tree != nullptr ? m_Tree->Nodes[m_Index].Kind : BinaryNodeKind::Null; }
        [[nodiscard]] bool IsNull() const { return GetKind() == BinaryNodeKind::Null; }
        [[nodiscard]] bool IsScalar() const { return GetKind() == BinaryNodeKind::Plain || GetKind() == BinaryNodeKind::String; }
        [[nodiscard]] bool IsSequence() const { return GetKind() == BinaryNodeKind::BlockSequence || GetKind() == BinaryNodeKind::FlowSequence; }
        [[nodiscard]] bool IsMap() const { return GetKind() == BinaryNodeKind::BlockMap || GetKind() == BinaryNodeKind::FlowMap; }

        // Empty for a node that is not a scalar
        [[nodiscard]] std::string_view Scalar() const { return IsScalar() ? m_Tree->Strings[m_Tree->Nodes[m_Index].First] : std::string_view(); }
        // A sequence's items or a map's keys, and none for anything else
        [[nodiscard]] std::size_t size() const { return IsSequence() || IsMap() ? m_Tree->Nodes[m_Index].Count : 0; }

        [[nodiscard]] BinaryNode operator[](std::size_t index) const;
        // The value under the key of a map
        [[nodiscard]] BinaryNode Find(std::string_view key) const;

        [[nodiscard]] BinaryNodeIterator begin() const;
        [[nodiscard]] BinaryNodeIterator end() const;

    private:
        const BinaryTree* m_Tree = nullptr;
        std::uint32_t m_Index = 0;
    };

    // What iterating a node gives, as YAML's iterator gives it: a map's key and value, or a sequence's item as the node itself
    struct BinaryNodeItem
    {
        BinaryNodeKey first;
        BinaryNode second;

        operator BinaryNode() const { return second; }
    };

    class BinaryNodeIterator
    {
    public:
        BinaryNodeIterator(const BinaryTree* tree, std::uint32_t entry) : m_Tree(tree), m_Entry(entry)
        {

        }

        [[nodiscard]] BinaryNodeItem operator*() const
        {
            const BinaryTreeEntry& l_Entry = m_Tree->Entries[m_Entry];

            return { { l_Entry.Key != BinaryTreeEntry::c_NoKey ? m_Tree->Strings[l_Entry.Key] : std::string_view() }, BinaryNode(*m_Tree, l_Entry.Value) };
        }

        BinaryNodeIterator& operator++()
        {
            ++m_Entry;

            return *this;
        }

        [[nodiscard]] bool operator==(const BinaryNodeIterator&) const = default;

    private:
        const BinaryTree* m_Tree = nullptr;
        std::uint32_t m_Entry = 0;
    };

    inline BinaryNode BinaryNode::operator[](std::size_t index) const
    {
        return index < size() ? BinaryNode(*m_Tree, m_Tree->Entries[m_Tree->Nodes[m_Index].First + index].Value) : BinaryNode();
    }

    inline BinaryNode BinaryNode::Find(std::string_view key) const
    {
        if (!IsMap())
        {
            return {};
        }

        const BinaryTreeNode& l_Node = m_Tree->Nodes[m_Index];
        for (std::uint32_t it_Entry = l_Node.First; it_Entry < l_Node.First + l_Node.Count; ++it_Entry)
        {
            if (m_Tree->Strings[m_Tree->Entries[it_Entry].Key] == key)
            {
                return BinaryNode(*m_Tree, m_Tree->Entries[it_Entry].Value);
            }
        }

        return {};
    }

    inline BinaryNodeIterator BinaryNode::begin() const
    {
        return BinaryNodeIterator(m_Tree, size() != 0 ? m_Tree->Nodes[m_Index].First : 0);
    }

    inline BinaryNodeIterator BinaryNode::end() const
    {
        return BinaryNodeIterator(m_Tree, size() != 0 ? m_Tree->Nodes[m_Index].First + m_Tree->Nodes[m_Index].Count : 0);
    }

    // Builds trees as ComponentWriter writes components, each string kept once however many nodes hold it
    class BinaryTreeBuilder
    {
    public:
        // A component's node: a map of the keys written into it, the one value written as it, or null when nothing is
        void BeginComponent();
        [[nodiscard]] std::uint32_t EndComponent();

        // A scalar under the key, or a sequence's next item, whose key is ignored
        void AddScalar(std::string_view key, BinaryNodeKind kind, std::string_view text);
        // The component's own value, in place of keys
        void SetValue(BinaryNodeKind kind, std::string_view text);
        // A sequence or map under the key, or as a sequence's next item, until End
        void Begin(std::string_view key, BinaryNodeKind kind);
        // A block sequence's next item as a map of the keys that follow, ended by the next item or the sequence's End
        void BeginItem();
        void End();

        [[nodiscard]] std::uint32_t AddString(std::string_view text);

        [[nodiscard]] std::span<const std::string_view> GetStrings() const { return m_Strings; }
        [[nodiscard]] std::span<const BinaryTreeNode> GetNodes() const { return m_Nodes; }
        [[nodiscard]] std::span<const BinaryTreeEntry> GetEntries() const { return m_Entries; }

    private:
        struct Frame
        {
            BinaryNodeKind Kind = BinaryNodeKind::BlockMap;
            std::uint32_t Key = BinaryTreeEntry::c_NoKey;
            // Where its entries start among those not yet placed
            std::size_t Pending = 0;
            bool Item = false;
            bool Component = false;
            // A component's own value
            BinaryNodeKind ValueKind = BinaryNodeKind::Null;
            std::uint32_t Value = 0;
        };

        struct StringHash
        {
            using is_transparent = void;

            [[nodiscard]] std::size_t operator()(std::string_view text) const { return std::hash<std::string_view>()(text); }
        };

        [[nodiscard]] std::uint32_t AddNode(BinaryNodeKind kind, std::uint32_t first, std::uint32_t count);
        // The key's string under a map, and none for a sequence's item
        [[nodiscard]] std::uint32_t GetKey(std::string_view key);
        [[nodiscard]] std::uint32_t Place(const Frame& frame);
        void Close();

        std::unordered_map<std::string, std::uint32_t, StringHash, std::equal_to<>> m_StringIndices;
        // Views into the map's keys, which stay where they are
        std::vector<std::string_view> m_Strings;
        std::vector<BinaryTreeNode> m_Nodes;
        std::vector<BinaryTreeEntry> m_Entries;
        std::vector<Frame> m_Frames;
        std::vector<BinaryTreeEntry> m_Pending;
    };
}