using System;
using Trinity.Interop;

namespace Trinity
{
    // As RmlUi raises it. Numbered as the engine's UIEventType
    public enum UIEventType
    {
        Click,
        Change,
        Submit,
        PointerEnter,
        PointerLeave,
        Focus,
        Blur
    }

    // An element's text and flags as they cross, numbered as the engine's InteropUI numbers them
    internal enum UIElementText
    {
        ID,
        Text,
        InnerRml,
        Value
    }

    internal enum UIElementFlag
    {
        Visible,
        Focused,
        Checked
    }

    // Safe to keep after the element has gone: calls then do nothing, logged once. Main thread only
    public readonly unsafe struct UIElement : IEquatable<UIElement>
    {
        internal UIElement(ulong handle)
        {
            Handle = handle;
        }

        internal ulong Handle { get; }

        public static UIElement None => default;

        public bool IsValid => Handle != 0 && Native.UIElement_IsValid(Handle);

        public string ID => GetText(UIElementText.ID);

        // Every text inside it, joined. Setting it replaces everything it holds, shown as written, not RML
        public string Text
        {
            get => GetText(UIElementText.Text);
            set => SetText(UIElementText.Text, value);
        }

        public string InnerRml
        {
            get => GetText(UIElementText.InnerRml);
            set => SetText(UIElementText.InnerRml, value);
        }

        // A form control's value, such as a text field's text. Empty, and unsettable, for other elements
        public string Value
        {
            get => GetText(UIElementText.Value);
            set => SetText(UIElementText.Value, value);
        }

        // A checkbox's or a radio button's, which its checked attribute is
        public bool Checked
        {
            get => Native.UIElement_GetFlag(Handle, (int)UIElementFlag.Checked);
            set => Native.UIElement_SetFlag(Handle, (int)UIElementFlag.Checked, value);
        }

        // Hidden sets display: none on the element. Visible reads whether it and all around it are shown
        public bool Visible
        {
            get => Native.UIElement_GetFlag(Handle, (int)UIElementFlag.Visible);
            set => Native.UIElement_SetFlag(Handle, (int)UIElementFlag.Visible, value);
        }

        public bool IsFocused => Native.UIElement_GetFlag(Handle, (int)UIElementFlag.Focused);

        // False when it cannot take the focus, such as when it is hidden
        public bool Focus() => Native.UIElement_SetFlag(Handle, (int)UIElementFlag.Focused, true);

        // The focus back on its document, when the element has it
        public void Blur() => Native.UIElement_SetFlag(Handle, (int)UIElementFlag.Focused, false);

        public bool HasClass(string name)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return Native.UIElement_HasClass(Handle, new Utf8(l_Data, l_Name.Bytes.Length));
            }
        }

        public void SetClass(string name, bool set = true)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                Native.UIElement_SetClass(Handle, new Utf8(l_Data, l_Name.Bytes.Length), set);
            }
        }

        // Null for one it does not have
        public string? GetAttribute(string name)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                bool l_Found;
                Utf8 l_Value = Native.UIElement_GetAttribute(Handle, new Utf8(l_Data, l_Name.Bytes.Length), &l_Found);

                return l_Found ? l_Value.ToString() : null;
            }
        }

        public void SetAttribute(string name, string value)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            using Utf8String l_Value = new(value, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_NameData = l_Name.Bytes)
            fixed (byte* l_ValueData = l_Value.Bytes)
            {
                Native.UIElement_SetAttribute(Handle, new Utf8(l_NameData, l_Name.Bytes.Length), new Utf8(l_ValueData, l_Value.Bytes.Length));
            }
        }

        public void RemoveAttribute(string name)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                Native.UIElement_RemoveAttribute(Handle, new Utf8(l_Data, l_Name.Bytes.Length));
            }
        }

        // Set on the element itself, over its stylesheets'. False for a property or value RmlUi cannot read
        public bool SetStyle(string property, string value)
        {
            using Utf8String l_Property = new(property, stackalloc byte[Utf8String.c_StackBytes]);
            using Utf8String l_Value = new(value, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_PropertyData = l_Property.Bytes)
            fixed (byte* l_ValueData = l_Value.Bytes)
            {
                return Native.UIElement_SetStyle(Handle, new Utf8(l_PropertyData, l_Property.Bytes.Length), new Utf8(l_ValueData, l_Value.Bytes.Length));
            }
        }

        public void RemoveStyle(string property)
        {
            using Utf8String l_Property = new(property, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Property.Bytes)
            {
                Native.UIElement_RemoveStyle(Handle, new Utf8(l_Data, l_Property.Bytes.Length));
            }
        }

        // As the element has it, from its own style or its stylesheets. Empty for one RmlUi does not know
        public string GetStyle(string property)
        {
            using Utf8String l_Property = new(property, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Property.Bytes)
            {
                return Native.UIElement_GetStyle(Handle, new Utf8(l_Data, l_Property.Bytes.Length)).ToString();
            }
        }

        public bool Equals(UIElement other) => Handle == other.Handle;

        public override bool Equals(object? other) => other is UIElement l_Element && Equals(l_Element);

        public override int GetHashCode() => Handle.GetHashCode();

        public static bool operator ==(UIElement left, UIElement right) => left.Equals(right);

        public static bool operator !=(UIElement left, UIElement right) => !left.Equals(right);

        public override string ToString() => IsValid ? $"UIElement #{ID}" : "UIElement (none)";

        private string GetText(UIElementText text) => Native.UIElement_GetText(Handle, (int)text).ToString();

        private void SetText(UIElementText text, string value)
        {
            using Utf8String l_Value = new(value, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Value.Bytes)
            {
                Native.UIElement_SetText(Handle, (int)text, new Utf8(l_Data, l_Value.Bytes.Length));
            }
        }
    }

    // The element it was added for, the target, on or inside it, and for Change and Submit the value
    public readonly struct UIEvent
    {
        internal UIEvent(UIEventType type, UIDocument document, UIElement element, UIElement target, string value)
        {
            Type = type;
            Document = document;
            Element = element;
            Target = target;
            Value = value;
        }

        public UIEventType Type { get; }
        public UIDocument Document { get; }
        public UIElement Element { get; }
        public UIElement Target { get; }
        public string Value { get; }
    }

    // A handler a UIDocument added, which stays valid to remove after it has gone
    public readonly struct UIEventHandle : IEquatable<UIEventHandle>
    {
        internal UIEventHandle(ulong id)
        {
            ID = id;
        }

        internal ulong ID { get; }

        public bool Equals(UIEventHandle other) => ID == other.ID;

        public override bool Equals(object? other) => other is UIEventHandle l_Handle && Equals(l_Handle);

        public override int GetHashCode() => ID.GetHashCode();
    }
}

namespace Trinity.Scripting
{
    // A C# handler as the engine holds it, by a GC handle, with its document and element ID
    internal sealed class UIHandler
    {
        public UIHandler(UIDocument document, string element, Action<UIEvent> action)
        {
            Document = document;
            Element = element;
            Action = action;
        }

        public UIDocument Document { get; }
        public string Element { get; }
        public Action<UIEvent> Action { get; }

        public void Invoke(UIEventType type, ulong element, ulong target, string value) => Action(new UIEvent(type, Document, new UIElement(element), new UIElement(target), value));
    }
}