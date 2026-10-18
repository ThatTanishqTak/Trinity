using System;

namespace Trinity.Interop
{
    // An action's events, numbered as their bytes in InputAction_GetEdges are, in the order a player's are raised in one frame
    internal enum InputEdge
    {
        Pressed,
        Released,
        Performed,
        Canceled
    }

    // The handlers scripts add to input actions' events, raised once a frame as the engine asks through ScriptHost. Each action's handlers are in arrays made again only as one is added or removed, so raising them allocates nothing, and a handler may add or remove handlers while they are raised. An action the running scene did not find has its handlers dropped, so those of a scene that stopped go as the next one runs. Main thread only
    internal static class InputEvents
    {
        private const int c_EdgeCount = 4;

        private sealed class Subscription
        {
            public Subscription(InputAction action)
            {
                Action = action;
                for (int it_Edge = 0; it_Edge < c_EdgeCount; ++it_Edge)
                {
                    Handlers[it_Edge] = Array.Empty<Action<InputActionEvent>>();
                }
            }

            public InputAction Action { get; }
            // By InputEdge
            public readonly Action<InputActionEvent>[][] Handlers = new Action<InputActionEvent>[c_EdgeCount][];

            public bool IsEmpty
            {
                get
                {
                    foreach (Action<InputActionEvent>[] it_Handlers in Handlers)
                    {
                        if (it_Handlers.Length != 0)
                        {
                            return false;
                        }
                    }

                    return true;
                }
            }
        }

        private static Subscription[] s_Subscriptions = Array.Empty<Subscription>();

        public static void Add(InputAction action, InputEdge edge, Action<InputActionEvent>? handler)
        {
            if (action.Handle == 0)
            {
                throw new InvalidOperationException($"An input action handler was not added, since {(action.Path.Length != 0 ? $"'{action.Path}' was found while no scene ran" : "the action is None")}");
            }

            if (handler == null)
            {
                return;
            }

            Subscription? l_Subscription = Find(action);
            if (l_Subscription == null)
            {
                l_Subscription = new Subscription(action);
                s_Subscriptions = Append(s_Subscriptions, l_Subscription);
            }

            ref Action<InputActionEvent>[] l_Handlers = ref l_Subscription.Handlers[(int)edge];
            l_Handlers = Append(l_Handlers, handler);
        }

        // The last added that is equal to it, as removing from an event does
        public static void Remove(InputAction action, InputEdge edge, Action<InputActionEvent>? handler)
        {
            Subscription? l_Subscription = handler != null ? Find(action) : null;
            if (l_Subscription == null)
            {
                return;
            }

            ref Action<InputActionEvent>[] l_Handlers = ref l_Subscription.Handlers[(int)edge];
            int l_Index = Array.LastIndexOf(l_Handlers, handler);
            if (l_Index < 0)
            {
                return;
            }

            l_Handlers = RemoveAt(l_Handlers, l_Index);
            if (l_Subscription.IsEmpty)
            {
                Drop(l_Subscription);
            }
        }

        // Every handler that is a method of the object, as the engine frees a script
        public static void RemoveTarget(object target)
        {
            foreach (Subscription it_Subscription in s_Subscriptions)
            {
                for (int it_Edge = 0; it_Edge < c_EdgeCount; ++it_Edge)
                {
                    it_Subscription.Handlers[it_Edge] = RemoveTarget(it_Subscription.Handlers[it_Edge], target);
                }

                if (it_Subscription.IsEmpty)
                {
                    Drop(it_Subscription);
                }
            }
        }

        // As a game assembly unloads, so no handler keeps its load context alive
        public static void Clear()
        {
            s_Subscriptions = Array.Empty<Subscription>();
        }

        // For each action, each player it had an edge for, its events in InputEdge's order
        public static void Raise()
        {
            foreach (Subscription it_Subscription in s_Subscriptions)
            {
                long l_Edges = Native.InputAction_GetEdges(it_Subscription.Action.Handle);
                if (l_Edges < 0)
                {
                    Drop(it_Subscription);

                    continue;
                }

                uint l_Bits = (uint)l_Edges;
                uint l_Players = (l_Bits | (l_Bits >> 8) | (l_Bits >> 16) | (l_Bits >> 24)) & 0xFF;
                for (int it_Player = 0; (l_Players >> it_Player) != 0; ++it_Player)
                {
                    if ((l_Players & (1u << it_Player)) == 0)
                    {
                        continue;
                    }

                    InputActionEvent l_Event = new(it_Subscription.Action, it_Player, it_Subscription.Action.ReadValue(it_Player));
                    for (int it_Edge = 0; it_Edge < c_EdgeCount; ++it_Edge)
                    {
                        if ((l_Bits & (1u << (it_Edge * 8 + it_Player))) != 0)
                        {
                            Invoke(it_Subscription, (InputEdge)it_Edge, l_Event);
                        }
                    }
                }
            }
        }

        private static void Invoke(Subscription subscription, InputEdge edge, InputActionEvent inputEvent)
        {
            foreach (Action<InputActionEvent> it_Handler in subscription.Handlers[(int)edge])
            {
                try
                {
                    it_Handler(inputEvent);
                }
                catch (Exception exception)
                {
                    // A script a game made itself has no class the engine knows, so its type names it
                    string l_Handler = $"{edge} handler {it_Handler.Method.Name} of input action '{subscription.Action.Path}'";
                    NativeLog.Write(LogLevel.Error, it_Handler.Target is Script l_Script
                        ? $"Scripting: {l_Script.Class?.Name ?? l_Script.GetType().Name} on entity {l_Script.Entity} threw in its {l_Handler}, which is removed while play goes on: {exception}"
                        : $"Scripting: the {l_Handler} threw, and is removed while play goes on: {exception}");
                    Remove(subscription.Action, edge, it_Handler);
                }
            }
        }

        private static Subscription? Find(InputAction action)
        {
            foreach (Subscription it_Subscription in s_Subscriptions)
            {
                if (it_Subscription.Action == action)
                {
                    return it_Subscription;
                }
            }

            return null;
        }

        private static void Drop(Subscription subscription)
        {
            int l_Index = Array.IndexOf(s_Subscriptions, subscription);
            if (l_Index >= 0)
            {
                s_Subscriptions = RemoveAt(s_Subscriptions, l_Index);
            }
        }

        private static T[] Append<T>(T[] items, T item)
        {
            T[] l_Items = new T[items.Length + 1];
            items.CopyTo(l_Items, 0);
            l_Items[items.Length] = item;

            return l_Items;
        }

        private static T[] RemoveAt<T>(T[] items, int index)
        {
            if (items.Length == 1)
            {
                return Array.Empty<T>();
            }

            T[] l_Items = new T[items.Length - 1];
            Array.Copy(items, 0, l_Items, 0, index);
            Array.Copy(items, index + 1, l_Items, index, items.Length - index - 1);

            return l_Items;
        }

        // The same array when none is the target's, so freeing a script with no handlers allocates nothing
        private static Action<InputActionEvent>[] RemoveTarget(Action<InputActionEvent>[] handlers, object target)
        {
            int l_Kept = 0;
            foreach (Action<InputActionEvent> it_Handler in handlers)
            {
                l_Kept += ReferenceEquals(it_Handler.Target, target) ? 0 : 1;
            }

            if (l_Kept == handlers.Length)
            {
                return handlers;
            }

            Action<InputActionEvent>[] l_Handlers = l_Kept != 0 ? new Action<InputActionEvent>[l_Kept] : Array.Empty<Action<InputActionEvent>>();
            int l_Next = 0;
            foreach (Action<InputActionEvent> it_Handler in handlers)
            {
                if (!ReferenceEquals(it_Handler.Target, target))
                {
                    l_Handlers[l_Next++] = it_Handler;
                }
            }

            return l_Handlers;
        }
    }
}