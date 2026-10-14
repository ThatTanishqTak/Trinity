using System;
using System.Collections;
using System.Collections.Generic;

namespace Trinity.ScriptGenerator
{
    // An array compared by its items, so the generator's models compare equal when nothing changed and the output is not written again
    internal readonly struct EquatableArray<T> : IEquatable<EquatableArray<T>>, IEnumerable<T> where T : IEquatable<T>
    {
        private readonly T[]? m_Items;

        public EquatableArray(T[] items)
        {
            m_Items = items;
        }

        public int Count => m_Items?.Length ?? 0;

        public T this[int index] => m_Items![index];

        public bool Equals(EquatableArray<T> other)
        {
            if (Count != other.Count)
            {
                return false;
            }

            for (int it_Index = 0; it_Index < Count; ++it_Index)
            {
                if (!m_Items![it_Index].Equals(other.m_Items![it_Index]))
                {
                    return false;
                }
            }

            return true;
        }

        public override bool Equals(object? obj) => obj is EquatableArray<T> other && Equals(other);

        public override int GetHashCode()
        {
            int l_Hash = 17;
            for (int it_Index = 0; it_Index < Count; ++it_Index)
            {
                l_Hash = unchecked(l_Hash * 31 + m_Items![it_Index].GetHashCode());
            }

            return l_Hash;
        }

        public IEnumerator<T> GetEnumerator() => ((IEnumerable<T>)(m_Items ?? Array.Empty<T>())).GetEnumerator();

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}