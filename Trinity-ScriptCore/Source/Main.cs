using System;

namespace Trinity
{
    public class Main
    {
        public Main()
        {
            Console.WriteLine("Hi from constructor");        
        }

        public void PrintMessage()
        {
            Console.WriteLine("Hello World from C#");
        }

        public void PrintCustomMessage(string message)
        {
            Console.WriteLine($"C# says: {message}");
        }
    }
}