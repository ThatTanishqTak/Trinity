using System;
using Trinity.Interop;
using Trinity.Scripting;

namespace Trinity
{
    // Its elements, event handlers, data models and sound group. Loaded as first reached, for OnCreate
    public readonly unsafe partial struct UIDocument
    {
        // Whether its document loaded
        public bool IsLoaded => Native.UIDocument_IsValid(m_Entity.ID);

        // None when it has no element with that ID
        public UIElement GetElement(string id)
        {
            using Utf8String l_ID = new(id, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_ID.Bytes)
            {
                return new UIElement(Native.UIDocument_GetElement(m_Entity.ID, new Utf8(l_Data, l_ID.Bytes.Length)));
            }
        }

        // The first element the selector matches, such as "#menu button.primary", or none
        public UIElement Query(string selector)
        {
            using Utf8String l_Selector = new(selector, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Selector.Bytes)
            {
                return new UIElement(Native.UIDocument_Query(m_Entity.ID, new Utf8(l_Data, l_Selector.Bytes.Length)));
            }
        }

        // The element with the focus, or none
        public UIElement Focus => new(Native.UIDocument_GetFocus(m_Entity.ID));

        // The mixer group its elements' sounds play into. None is each event's own Output, or Master
        public AudioMixerGroup SoundOutput
        {
            get
            {
                ulong l_Group;
                Check(Native.UIDocument_GetSoundOutput(m_Entity.ID, &l_Group));

                return new AudioMixerGroup(l_Group);
            }
            set => Check(Native.UIDocument_SetSoundOutput(m_Entity.ID, value.ID));
        }

        // As data-model names it, made on first ask. Not valid for a name RmlUi cannot reach
        public UIDataModel GetModel(string name)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return new UIDataModel(Native.UIDocument_GetModel(m_Entity.ID, new Utf8(l_Data, l_Name.Bytes.Length)));
            }
        }

        // By element ID. Click, Change and Submit bubble out to it. Raised once a frame
        public UIEventHandle On(string element, UIEventType type, Action<UIEvent> handler)
        {
            ArgumentException.ThrowIfNullOrEmpty(element);
            ArgumentNullException.ThrowIfNull(handler);

            return new UIEventHandle(ScriptHost.AddUIHandler(new UIHandler(this, element, handler), type));
        }

        // False when it had already gone
        public bool Off(UIEventHandle handler) => handler.ID != 0 && Native.UIDocument_RemoveHandler(handler.ID);

        // The message made only for a failure, so a call that works allocates nothing
        private void Check(int result)
        {
            if ((AccessResult)result != AccessResult.Done)
            {
                ComponentAccess.Check(m_Entity, (AccessResult)result, "has no UIDocument");
            }
        }
    }
}