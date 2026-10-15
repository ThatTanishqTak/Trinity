using System;
using System.Collections;
using System.Collections.Generic;
using Trinity.Interop;

namespace Trinity.Scripting
{
    // What a task waits for, numbered as the engine's ScriptWaitType and ScriptTaskKind number them
    internal enum WaitType
    {
        Done,
        Seconds,
        Frames,
        FixedStep,
        Condition
    }

    internal enum TaskKind
    {
        Timer,
        Coroutine
    }

    internal readonly struct TaskWait
    {
        public WaitType Type { get; init; }
        public float Seconds { get; init; }
        public int Frames { get; init; }

        public static TaskWait ForSeconds(float seconds) => new() { Type = WaitType.Seconds, Seconds = seconds };
        public static TaskWait ForFrames(int frames) => new() { Type = WaitType.Frames, Frames = Math.Max(frames, 1) };
    }

    // A script's timer, which runs its action, or its coroutine, which runs its IEnumerator and those it yields in turn until it next waits. The engine keeps it by a GC handle and says when to run it
    internal sealed class ScriptTask
    {
        private readonly Action? m_Action;
        private readonly Stack<IEnumerator>? m_Routines;
        private Func<bool>? m_Condition;
        private bool m_Holds;
        private ulong m_Awaited;

        public Script Owner { get; }
        public bool IsTimer => m_Action != null;

        public ScriptTask(Script owner, Action action)
        {
            Owner = owner;
            m_Action = action;
        }

        public ScriptTask(Script owner, IEnumerator routine)
        {
            Owner = owner;
            m_Routines = new Stack<IEnumerator>();
            m_Routines.Push(routine);
        }

        // Anything else it yields waits for the next frame, as null does
        public TaskWait Resume()
        {
            if (m_Action != null)
            {
                m_Action();

                return default;
            }

            m_Condition = null;
            m_Awaited = 0;
            while (m_Routines!.Count != 0)
            {
                IEnumerator l_Top = m_Routines.Peek();
                if (!l_Top.MoveNext())
                {
                    m_Routines.Pop();

                    continue;
                }

                switch (l_Top.Current)
                {
                    case WaitForSeconds it_Wait:
                        {
                            return TaskWait.ForSeconds(it_Wait.Seconds);
                        }
                    case WaitForFrames it_Wait:
                        {
                            return TaskWait.ForFrames(it_Wait.Frames);
                        }
                    case WaitForFixedUpdate:
                        {
                            return new TaskWait { Type = WaitType.FixedStep };
                        }
                    case WaitUntil it_Wait:
                        {
                            m_Condition = it_Wait.Condition;
                            m_Holds = true;

                            return new TaskWait { Type = WaitType.Condition };
                        }
                    case WaitWhile it_Wait:
                        {
                            m_Condition = it_Wait.Condition;
                            m_Holds = false;

                            return new TaskWait { Type = WaitType.Condition };
                        }
                    case Coroutine it_Coroutine:
                        {
                            if (it_Coroutine.ID == 0 || !Native.Scripting_IsTaskRunning(it_Coroutine.ID))
                            {
                                continue;
                            }

                            m_Awaited = it_Coroutine.ID;

                            return new TaskWait { Type = WaitType.Condition };
                        }
                    case IEnumerator it_Nested:
                        {
                            m_Routines.Push(it_Nested);

                            continue;
                        }
                    default:
                        {
                            return TaskWait.ForFrames(1);
                        }
                }
            }

            return default;
        }

        public bool IsMet()
        {
            if (m_Awaited != 0)
            {
                return !Native.Scripting_IsTaskRunning(m_Awaited);
            }

            return m_Condition == null || m_Condition() == m_Holds;
        }
    }
}