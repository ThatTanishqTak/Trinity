// Trinity-InteropGen: writes the C# side of NativeFunctions.def, the list of engine functions C# can call, as Trinity-ScriptCore builds. The engine's side comes from the same lines through its X-macros, so the two cannot drift
//
// Usage: Trinity-InteropGen <NativeFunctions.def> <NativeFunctions.g.cs>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // An interop type's C# spelling, and whether it may cross by value. The vectors and matrices cross only through pointers, since MSVC passes and returns a glm type with constructors differently from the C# struct of the same layout
    struct InteropType
    {
        std::string_view Name;
        std::string_view CSharp;
        bool ByValue = true;
    };

    constexpr InteropType c_Types[] = {
        { "Void", "void", true },
        { "Bool", "bool", true },
        { "Int32", "int", true },
        { "UInt32", "uint", true },
        { "Int64", "long", true },
        { "UInt64", "ulong", true },
        { "Float", "float", true },
        { "Double", "double", true },
        { "EntityId", "ulong", true },
        { "LogLevel", "LogLevel", true },
        { "Utf8", "Utf8", true },
        { "Vector2", "Vector2", false },
        { "Vector3", "Vector3", false },
        { "Vector4", "Vector4", false },
        { "Quaternion", "Quaternion", false },
        { "Color", "Color", false },
        { "Matrix4x4", "Matrix4x4", false }
    };

    struct Parameter
    {
        std::string Type;
        std::string Name;
    };

    struct Function
    {
        std::string Name;
        std::string Return;
        std::vector<Parameter> Parameters;
    };

    struct Error
    {
        std::size_t Line = 0;
        std::string Message;
    };

    const InteropType* FindType(std::string_view name)
    {
        const auto a_Found = std::ranges::find(c_Types, name, &InteropType::Name);

        return a_Found != std::end(c_Types) ? &*a_Found : nullptr;
    }

    std::string Trim(std::string_view text)
    {
        const std::size_t l_First = text.find_first_not_of(" \t\r");
        if (l_First == std::string_view::npos)
        {
            return {};
        }

        return std::string(text.substr(l_First, text.find_last_not_of(" \t\r") - l_First + 1));
    }

    // A type as the .def spells it, such as Int32, Vector3* or const Utf8*, in C#. Empty, with the reason in error, for one that is not an interop type or may not cross that way
    std::string ToCSharp(const std::string& spelling, bool isReturn, std::string& error)
    {
        static const std::regex c_Type(R"(^(const\s+)?(\w+)\s*(\*)?$)");
        std::smatch l_Match;
        if (!std::regex_match(spelling, l_Match, c_Type))
        {
            error = std::format("'{}' is not a type the list can name", spelling);

            return {};
        }

        const InteropType* l_Type = FindType(l_Match[2].str());
        const bool l_Pointer = l_Match[3].matched;
        if (l_Type == nullptr)
        {
            error = std::format("'{}' is not one of the interop types of Interop.hpp", l_Match[2].str());

            return {};
        }

        if (!l_Pointer && !l_Type->ByValue)
        {
            error = std::format("{} crosses only through a pointer, as {}* or const {}*", l_Type->Name, l_Type->Name, l_Type->Name);

            return {};
        }

        if (!l_Pointer && l_Type->Name == "Void" && !isReturn)
        {
            error = "Void is only a return type, and an empty list takes nothing";

            return {};
        }

        return std::format("{}{}", l_Type->CSharp, l_Pointer ? "*" : "");
    }

    std::vector<Function> Parse(const std::string& text, std::vector<Error>& errors)
    {
        static const std::regex c_Native(R"(^TR_NATIVE\(\s*(\w+)\s*,\s*([^,]+?)\s*,\s*\((.*)\)\s*\)$)");
        static const std::regex c_Parameter(R"(^(.+?)\s*(\b\w+)$)");

        std::vector<Function> l_Functions;
        std::istringstream l_Stream(text);
        std::string l_Line;
        for (std::size_t it_Line = 1; std::getline(l_Stream, l_Line); ++it_Line)
        {
            const std::string l_Code = Trim(std::string_view(l_Line).substr(0, l_Line.find("//")));
            if (l_Code.empty())
            {
                continue;
            }

            std::smatch l_Match;
            if (!std::regex_match(l_Code, l_Match, c_Native))
            {
                errors.push_back({ it_Line, "expected TR_NATIVE(Name, Return, (Parameters)) on one line" });

                continue;
            }

            Function l_Function{ l_Match[1].str(), l_Match[2].str(),{} };
            if (std::ranges::any_of(l_Functions, [&l_Function](const Function& function) { return function.Name == l_Function.Name; }))
            {
                errors.push_back({ it_Line, std::format("{} is in the list already", l_Function.Name) });
            }

            std::string l_Error;
            if (ToCSharp(l_Function.Return, true, l_Error).empty())
            {
                errors.push_back({ it_Line, std::format("{} returns {}: {}", l_Function.Name, l_Function.Return, l_Error) });
            }

            const std::string l_Parameters = Trim(l_Match[3].str());
            std::size_t l_Start = 0;
            while (!l_Parameters.empty() && l_Start <= l_Parameters.size())
            {
                const std::size_t l_End = std::min(l_Parameters.find(',', l_Start), l_Parameters.size());
                const std::string l_Text = Trim(std::string_view(l_Parameters).substr(l_Start, l_End - l_Start));
                l_Start = l_End + 1;

                std::smatch l_ParameterMatch;
                if (!std::regex_match(l_Text, l_ParameterMatch, c_Parameter))
                {
                    errors.push_back({ it_Line, std::format("parameter '{}' of {} has no type and name", l_Text, l_Function.Name) });

                    continue;
                }

                Parameter l_Parameter{ Trim(l_ParameterMatch[1].str()), l_ParameterMatch[2].str() };
                if (ToCSharp(l_Parameter.Type, false, l_Error).empty())
                {
                    errors.push_back({ it_Line, std::format("parameter {} of {}: {}", l_Parameter.Name, l_Function.Name, l_Error) });
                }

                l_Function.Parameters.push_back(std::move(l_Parameter));
            }

            l_Functions.push_back(std::move(l_Function));
        }

        return l_Functions;
    }

    std::string Write(const std::vector<Function>& functions)
    {
        std::string l_Ignored;
        const auto a_Signature = [&l_Ignored](const Function& function)
        {
            std::string l_Signature = "delegate* unmanaged<";
            for (const Parameter& it_Parameter : function.Parameters)
            {
                l_Signature += ToCSharp(it_Parameter.Type, false, l_Ignored) + ", ";
            }

            return l_Signature + ToCSharp(function.Return, true, l_Ignored) + ">";
        };

        std::string l_Text = "// Generated by Trinity-InteropGen from NativeFunctions.def as Trinity-ScriptCore builds. Edits here are lost on the next build\n";
        l_Text += "using System.Runtime.CompilerServices;\nusing System.Runtime.InteropServices;\n\nnamespace Trinity.Interop\n{\n";
        l_Text += "    // The engine's functions, as it hands them to Host.Initialize, in the order of NativeFunctions.def\n";
        l_Text += "    [StructLayout(LayoutKind.Sequential)]\n    public unsafe struct NativeFunctions\n    {\n";
        for (const Function& it_Function : functions)
        {
            l_Text += std::format("        public {} {};\n", a_Signature(it_Function), it_Function.Name);
        }

        l_Text += "    }\n\n";
        l_Text += "    // The engine's functions to call, once Host.Initialize has bound them\n";
        l_Text += "    internal static unsafe class Native\n    {\n";
        for (const Function& it_Function : functions)
        {
            l_Text += std::format("        private static {} s_{};\n", a_Signature(it_Function), it_Function.Name);
        }

        l_Text += "\n        internal static void Bind(in NativeFunctions functions)\n        {\n";
        for (const Function& it_Function : functions)
        {
            l_Text += std::format("            s_{0} = functions.{0};\n", it_Function.Name);
        }

        l_Text += "        }\n";
        for (const Function& it_Function : functions)
        {
            std::string l_Parameters;
            std::string l_Arguments;
            for (const Parameter& it_Parameter : it_Function.Parameters)
            {
                l_Parameters += std::format("{}{} {}", l_Parameters.empty() ? "" : ", ", ToCSharp(it_Parameter.Type, false, l_Ignored), it_Parameter.Name);
                l_Arguments += std::format("{}{}", l_Arguments.empty() ? "" : ", ", it_Parameter.Name);
            }

            const std::string l_Return = ToCSharp(it_Function.Return, true, l_Ignored);
            l_Text += std::format("\n        [MethodImpl(MethodImplOptions.AggressiveInlining)]\n        internal static {} {}({}) => s_{}({});\n", l_Return, it_Function.Name, l_Parameters, it_Function.Name, l_Arguments);
        }

        l_Text += "    }\n}\n";

        return l_Text;
    }

    std::optional<std::string> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream l_File(path, std::ios::binary);
        if (!l_File)
        {
            return std::nullopt;
        }

        return std::string(std::istreambuf_iterator<char>(l_File), std::istreambuf_iterator<char>());
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "Usage: Trinity-InteropGen <NativeFunctions.def> <NativeFunctions.g.cs>\n");

        return 2;
    }

    const std::filesystem::path l_Input = argv[1];
    const std::filesystem::path l_Output = argv[2];
    const std::optional<std::string> l_List = ReadFile(l_Input);
    if (!l_List)
    {
        std::fprintf(stderr, "%s: error: could not be read\n", l_Input.string().c_str());

        return 1;
    }

    std::vector<Error> l_Errors;
    const std::vector<Function> l_Functions = Parse(*l_List, l_Errors);
    for (const Error& it_Error : l_Errors)
    {
        std::fprintf(stderr, "%s(%zu): error: %s\n", l_Input.string().c_str(), it_Error.Line, it_Error.Message.c_str());
    }

    if (!l_Errors.empty())
    {
        return 1;
    }

    // Written only when it changes, so ScriptCore builds again only then
    const std::string l_Text = Write(l_Functions);
    if (ReadFile(l_Output) == l_Text)
    {
        return 0;
    }

    std::error_code l_Error;
    std::filesystem::create_directories(l_Output.parent_path(), l_Error);
    std::ofstream l_File(l_Output, std::ios::binary | std::ios::trunc);
    l_File << l_Text;
    l_File.close();
    if (!l_File)
    {
        std::fprintf(stderr, "%s: error: could not be written\n", l_Output.string().c_str());

        return 1;
    }

    return 0;
}