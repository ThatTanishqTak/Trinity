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
            Console.WriteLine("Hello World from C# function");
        }

        public void PrintInt(int num)
        {
            Console.WriteLine($"C# says(int parameter): {num}");
        }

        public void PrintCustomMessage(string message)
        {
            Console.WriteLine($"C# says(string parameter): {message}");
        }
    }
}