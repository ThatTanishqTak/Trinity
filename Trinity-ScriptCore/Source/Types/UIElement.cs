using System;
using Trinity.Interop;

namespace Trinity
{
    // What happened to an element, as RmlUi raises it: a click, a control's value changing, a form submitted, the pointer coming onto and leaving it, and the focus coming to and leaving it. Numbered as the engine's UIEventType
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

    // An element of a UI document, safe to keep after the element has gone, such as when InnerRml replaced what held it or its document was read again: everything on it then does nothing, or reads as nothing, logged once. Main thread only
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

        // Every text inside it, joined, and text put in place of everything it holds, shown as written rather than read as RML
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

        // A form control's value, such as a text field's text or a range's number, and empty for any other element, on which setting it does nothing
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

        // Hidden is display: none set on the element, so it takes no space and neither the pointer nor the focus reaches it. Shown again, that goes, and its stylesheets' display holds. Visible reads whether it and every element around it are displayed
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

        // A property set on the element itself, as its style attribute sets one, over its stylesheets'. False for a property or a value RmlUi cannot read
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

        // The property as the element has it, from its own style or its stylesheets, empty for one RmlUi does not know
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

    // What a handler is told: its document, the element it was added for, where the event happened, which is that element or one inside it, and for Change and Submit the value it changed to or the form sent
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
    // A C# handler as the engine holds it, by a GC handle, and the document and element ID it was added for
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