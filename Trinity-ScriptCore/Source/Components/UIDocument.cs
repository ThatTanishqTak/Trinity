using System;
using Trinity.Interop;
using Trinity.Scripting;

namespace Trinity
{
    // What a script reaches of the UIDocument as the scene runs: the elements of its document and handlers of their events. Its document is loaded as it is first reached, when no view shows the scene yet, so OnCreate finds its elements. Everything does nothing, or finds nothing, for an entity without a UIDocument or one whose document did not load, and while no scene runs
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

        // The data model of that name, as data-model="name" names it in the document, made the first time it is asked for, and kept while the scene's scripts run, its document loaded or not. Not valid for a name RmlUi cannot reach, or while no scene runs
        public UIDataModel GetModel(string name)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Name.Bytes)
            {
                return new UIDataModel(Native.UIDocument_GetModel(m_Entity.ID, new Utf8(l_Data, l_Name.Bytes.Length)));
            }
        }

        // The handler, for each event of that type on the element with that ID, and for Click, Change and Submit on any element inside it too, as they bubble out, the innermost element's handlers first. It is found by its ID as each event comes, so it holds for an element made again. Events are queued as they happen and given to handlers once a frame, before the fixed steps and OnUpdate, after the input action handlers. The handler is the script's that is running as it is added, and goes with it, and every handler goes with the running scene. One that throws is logged and removed. Throws while no scene runs, or for an empty ID
        public UIEventHandle On(string element, UIEventType type, Action<UIEvent> handler)
        {
            ArgumentException.ThrowIfNullOrEmpty(element);
            ArgumentNullException.ThrowIfNull(handler);

            return new UIEventHandle(ScriptHost.AddUIHandler(new UIHandler(this, element, handler), type));
        }

        // False when it had already gone
        public bool Off(UIEventHandle handler) => handler.ID != 0 && Native.UIDocument_RemoveHandler(handler.ID);
    }
}