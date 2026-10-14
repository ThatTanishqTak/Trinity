// Trinity-InteropGen: writes the C# side of what the engine shares with Trinity-ScriptCore as it builds: NativeFunctions.def, the list of engine functions C# can call, ScriptComponents.def, the components and enums C# reaches, and the engine's key and mouse button codes. The engine's side comes from the same lines through its X-macros and enums, so the two cannot drift
// Usage: Trinity-InteropGen <NativeFunctions.def> <ScriptComponents.def> <KeyCodes.hpp> <MouseCodes.hpp> <output directory>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // An interop type's C# spelling, and whether it may cross by value. The vectors and matrices cross only through pointers, since MSVC passes and returns a glm type with constructors differently from the C# struct of the same layout, as do the larger structs that hold them
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
        { "Matrix4x4", "Matrix4x4", false },
        { "FieldValue", "FieldValue", false },
        { "Collision", "Collision", false }
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

    struct EnumValue
    {
        std::string Name;
        std::int64_t Number = 0;
    };

    struct Enum
    {
        std::string Name;
        std::vector<EnumValue> Values;
    };

    struct Property
    {
        std::string Name;
        std::string Kind;
        std::string Argument;
        std::size_t Index = 0;
    };

    struct Component
    {
        std::string Name;
        std::vector<Property> Properties;
    };

    struct ComponentList
    {
        std::vector<Enum> Enums;
        std::vector<Component> Components;
    };

    // A property's kind, and the C# type it reads as, with how it is read from and made into a FieldValue
    struct PropertyKind
    {
        std::string_view Name;
        std::string_view CSharp;
        std::string_view Read;
        std::string_view Write;
    };

    constexpr PropertyKind c_Kinds[] = {
        { "Bool", "bool", "ComponentAccess.Get(m_Entity, {0}).ToBool()", "FieldValue.Bool(value)" },
        { "Int32", "int", "ComponentAccess.Get(m_Entity, {0}).ToInt32()", "FieldValue.Int32(value)" },
        { "UInt32", "uint", "ComponentAccess.Get(m_Entity, {0}).ToUInt32()", "FieldValue.UInt32(value)" },
        { "Float", "float", "ComponentAccess.Get(m_Entity, {0}).ToFloat()", "FieldValue.Float(value)" },
        { "Vector2", "Vector2", "ComponentAccess.Get(m_Entity, {0}).ToVector2()", "FieldValue.Vector2(value)" },
        { "Vector3", "Vector3", "ComponentAccess.Get(m_Entity, {0}).ToVector3()", "FieldValue.Vector3(value)" },
        { "Vector4", "Vector4", "ComponentAccess.Get(m_Entity, {0}).ToVector4()", "FieldValue.Vector4(value)" },
        { "Quaternion", "Quaternion", "ComponentAccess.Get(m_Entity, {0}).ToQuaternion()", "FieldValue.Quaternion(value)" },
        { "Color", "Color", "ComponentAccess.Get(m_Entity, {0}).ToColor()", "FieldValue.Color(value)" },
        { "Axes", "Axes", "(Axes)ComponentAccess.Get(m_Entity, {0}).ToInt32()", "FieldValue.Int32((int)value)" },
        { "Entity", "Entity", "ComponentAccess.Get(m_Entity, {0}).ToEntity()", "FieldValue.Entity(value)" },
        { "Enum", "{1}", "({1})ComponentAccess.Get(m_Entity, {0}).ToEnum()", "FieldValue.Enum((long)value)" },
        { "Asset", "AssetRef<{1}>", "new AssetRef<{1}>(ComponentAccess.Get(m_Entity, {0}).ToAsset())", "FieldValue.Asset(value.ID)" }
    };

    const PropertyKind* FindKind(std::string_view name)
    {
        const auto a_Found = std::ranges::find(c_Kinds, name, &PropertyKind::Name);

        return a_Found != std::end(c_Kinds) ? &*a_Found : nullptr;
    }

    std::string Format(std::string_view pattern, std::string_view first, std::string_view second)
    {
        return std::vformat(pattern, std::make_format_args(first, second));
    }

    ComponentList ParseComponents(const std::string& text, std::vector<Error>& errors)
    {
        static const std::regex c_Enum(R"(^TR_ENUM\(\s*(\w+)\s*,\s*(\w+)\s*\)$)");
        static const std::regex c_EnumValue(R"(^TR_ENUM_VALUE\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(-?\d+)\s*\)$)");
        static const std::regex c_Component(R"(^TR_COMPONENT\(\s*(\w+)\s*,\s*(\w+)\s*\)$)");
        static const std::regex c_Property(R"(^TR_PROPERTY\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)(?:\(\s*(\w+)\s*\))?\s*,\s*(\w+)\s*\)$)");

        ComponentList l_List;
        std::size_t l_Properties = 0;
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
            if (std::regex_match(l_Code, l_Match, c_Enum))
            {
                if (std::ranges::any_of(l_List.Enums, [&l_Match](const Enum& value) { return value.Name == l_Match[1].str(); }))
                {
                    errors.push_back({ it_Line, std::format("enum {} is in the list already", l_Match[1].str()) });
                }

                l_List.Enums.push_back({ l_Match[1].str(),{} });
            }
            else if (std::regex_match(l_Code, l_Match, c_EnumValue))
            {
                if (l_List.Enums.empty() || l_List.Enums.back().Name != l_Match[1].str())
                {
                    errors.push_back({ it_Line, std::format("{}::{} follows no TR_ENUM({}, ...)", l_Match[1].str(), l_Match[2].str(), l_Match[1].str()) });

                    continue;
                }

                l_List.Enums.back().Values.push_back({ l_Match[2].str(), std::stoll(l_Match[3].str()) });
            }
            else if (std::regex_match(l_Code, l_Match, c_Component))
            {
                if (std::ranges::any_of(l_List.Components, [&l_Match](const Component& component) { return component.Name == l_Match[1].str(); }))
                {
                    errors.push_back({ it_Line, std::format("component {} is in the list already", l_Match[1].str()) });
                }

                l_List.Components.push_back({ l_Match[1].str(),{} });
            }
            else if (std::regex_match(l_Code, l_Match, c_Property))
            {
                Property l_Property{ l_Match[2].str(), l_Match[3].str(), l_Match[4].str(), l_Properties++ };
                if (l_List.Components.empty() || l_List.Components.back().Name != l_Match[1].str())
                {
                    errors.push_back({ it_Line, std::format("{}.{} follows no TR_COMPONENT({}, ...)", l_Match[1].str(), l_Property.Name, l_Match[1].str()) });

                    continue;
                }

                const PropertyKind* l_Kind = FindKind(l_Property.Kind);
                const bool l_TakesArgument = l_Property.Kind == "Enum" || l_Property.Kind == "Asset";
                if (l_Kind == nullptr || l_TakesArgument == l_Property.Argument.empty())
                {
                    errors.push_back({ it_Line, std::format("{}.{} has kind '{}', which is not Bool, Int32, UInt32, Float, Vector2, Vector3, Vector4, Quaternion, Color, Axes, Entity, Enum(Name) or Asset(Type)", l_Match[1].str(), l_Property.Name, l_Match[3].str()) });
                }
                else if (l_Property.Kind == "Enum" && std::ranges::none_of(l_List.Enums, [&l_Property](const Enum& value) { return value.Name == l_Property.Argument; }))
                {
                    errors.push_back({ it_Line, std::format("{}.{} is an enum {} the list has not named", l_Match[1].str(), l_Property.Name, l_Property.Argument) });
                }

                if (l_Property.Name == l_Match[1].str() || std::ranges::any_of(l_List.Components.back().Properties, [&l_Property](const Property& property) { return property.Name == l_Property.Name; }))
                {
                    errors.push_back({ it_Line, std::format("{}.{} repeats a name C# has for it already", l_Match[1].str(), l_Property.Name) });
                }

                l_List.Components.back().Properties.push_back(std::move(l_Property));
            }
            else
            {
                errors.push_back({ it_Line, "expected TR_ENUM, TR_ENUM_VALUE, TR_COMPONENT or TR_PROPERTY on one line" });
            }
        }

        return l_List;
    }

    std::string WriteComponents(const ComponentList& list)
    {
        std::string l_Text = "// Generated by Trinity-InteropGen from ScriptComponents.def as Trinity-ScriptCore builds. Edits here are lost on the next build\n";
        l_Text += "using Trinity.Interop;\n\nnamespace Trinity\n{\n";
        for (const Enum& it_Enum : list.Enums)
        {
            l_Text += std::format("    public enum {}\n    {{\n", it_Enum.Name);
            for (std::size_t it_Value = 0; it_Value < it_Enum.Values.size(); ++it_Value)
            {
                l_Text += std::format("        {} = {}{}\n", it_Enum.Values[it_Value].Name, it_Enum.Values[it_Value].Number, it_Value + 1 < it_Enum.Values.size() ? "," : "");
            }

            l_Text += "    }\n\n";
        }

        for (std::size_t it_Component = 0; it_Component < list.Components.size(); ++it_Component)
        {
            const Component& l_Component = list.Components[it_Component];
            l_Text += std::format("    // The {0} of an entity, which reads and writes the engine's own each time, so it is never out of date. Get it from Entity.Get<{0}>()\n", l_Component.Name);
            l_Text += std::format("    public readonly partial struct {0} : IComponent<{0}>\n    {{\n", l_Component.Name);
            l_Text += "        private readonly Entity m_Entity;\n\n";
            l_Text += std::format("        internal {0}(Entity entity)\n        {{\n            m_Entity = entity;\n        }}\n\n", l_Component.Name);
            l_Text += std::format("        static int IComponent<{0}>.TypeId => {1};\n", l_Component.Name, it_Component);
            l_Text += std::format("        static string IComponent<{0}>.TypeName => \"{0}\";\n", l_Component.Name);
            l_Text += std::format("        static {0} IComponent<{0}>.Bind(Entity entity) => new(entity);\n\n", l_Component.Name);
            l_Text += "        public Entity Entity => m_Entity;\n";
            for (const Property& it_Property : l_Component.Properties)
            {
                const PropertyKind& l_Kind = *FindKind(it_Property.Kind);
                const std::string l_Index = std::to_string(it_Property.Index);
                l_Text += std::format("\n        public {} {}\n        {{\n", Format(l_Kind.CSharp, l_Index, it_Property.Argument), it_Property.Name);
                l_Text += std::format("            get => {};\n", Format(l_Kind.Read, l_Index, it_Property.Argument));
                l_Text += std::format("            set => ComponentAccess.Set(m_Entity, {}, {});\n", l_Index, Format(l_Kind.Write, l_Index, it_Property.Argument));
                l_Text += "        }\n";
            }

            l_Text += "    }\n\n";
        }

        l_Text += "}\n\nnamespace Trinity.Interop\n{\n    internal static partial class ComponentAccess\n    {\n";
        l_Text += "        // Each property's component and name, by its number, for what an exception says\n";
        l_Text += "        private static readonly string[] s_PropertyNames =\n        {\n";
        std::vector<std::string> l_Names;
        for (const Component& it_Component : list.Components)
        {
            for (const Property& it_Property : it_Component.Properties)
            {
                l_Names.push_back(std::format("{}.{}", it_Component.Name, it_Property.Name));
            }
        }

        for (std::size_t it_Name = 0; it_Name < l_Names.size(); ++it_Name)
        {
            l_Text += std::format("            \"{}\"{}\n", l_Names[it_Name], it_Name + 1 < l_Names.size() ? "," : "");
        }

        l_Text += "        };\n    }\n}\n";

        return l_Text;
    }

    // An enum class of a header, each value by name, as the header numbers it: given, the one before it plus one, or another value's
    std::optional<std::vector<EnumValue>> ParseEnum(const std::string& text, std::string_view name, std::string& error)
    {
        const std::regex c_Start(std::format(R"(enum\s+class\s+{}\s*:\s*[\w:]+\s*\{{)", name));
        std::smatch l_Match;
        if (!std::regex_search(text, l_Match, c_Start))
        {
            error = std::format("has no enum class {}", name);

            return std::nullopt;
        }

        const std::size_t l_Begin = static_cast<std::size_t>(l_Match.position(0) + l_Match.length(0));
        const std::size_t l_End = text.find('}', l_Begin);
        if (l_End == std::string::npos)
        {
            error = std::format("enum class {} does not end", name);

            return std::nullopt;
        }

        std::string l_Body;
        std::istringstream l_Lines(text.substr(l_Begin, l_End - l_Begin));
        for (std::string l_Line; std::getline(l_Lines, l_Line);)
        {
            l_Body += l_Line.substr(0, l_Line.find("//")) + ' ';
        }

        static const std::regex c_Item(R"(^(\w+)(?:\s*=\s*(\w+))?$)");
        std::vector<EnumValue> l_Values;
        std::int64_t l_Next = 0;
        std::size_t l_Start = 0;
        while (l_Start < l_Body.size())
        {
            const std::size_t l_Comma = std::min(l_Body.find(',', l_Start), l_Body.size());
            const std::string l_Item = Trim(std::string_view(l_Body).substr(l_Start, l_Comma - l_Start));
            l_Start = l_Comma + 1;
            if (l_Item.empty())
            {
                continue;
            }

            std::smatch l_ItemMatch;
            if (!std::regex_match(l_Item, l_ItemMatch, c_Item))
            {
                error = std::format("enum class {} has '{}', which is not a name or a name = value", name, l_Item);

                return std::nullopt;
            }

            std::int64_t l_Number = l_Next;
            if (l_ItemMatch[2].matched)
            {
                const std::string l_Given = l_ItemMatch[2].str();
                const auto a_Named = std::ranges::find(l_Values, l_Given, &EnumValue::Name);
                if (a_Named != l_Values.end())
                {
                    l_Number = a_Named->Number;
                }
                else if (std::isdigit(static_cast<unsigned char>(l_Given[0])))
                {
                    l_Number = std::stoll(l_Given);
                }
                else
                {
                    error = std::format("enum class {} gives {} the value {}, which is neither a number nor a name before it", name, l_ItemMatch[1].str(), l_Given);

                    return std::nullopt;
                }
            }

            l_Values.push_back({ l_ItemMatch[1].str(), l_Number });
            l_Next = l_Number + 1;
        }

        return l_Values;
    }

    // TR_LEFT_SHIFT as LeftShift and TR_KP_ADD as KeypadAdd, as C# spells its names. Count is how many there are, not a key
    std::string ToCSharpName(std::string_view name)
    {
        if (name.starts_with("TR_"))
        {
            name.remove_prefix(3);
        }

        std::string l_Name;
        std::size_t l_Start = 0;
        while (l_Start <= name.size())
        {
            const std::size_t l_End = std::min(name.find('_', l_Start), name.size());
            std::string l_Word(name.substr(l_Start, l_End - l_Start));
            l_Start = l_End + 1;
            if (l_Word.empty())
            {
                continue;
            }

            if (l_Word.starts_with("KP"))
            {
                l_Word = "Keypad" + l_Word.substr(2);
            }
            else
            {
                std::ranges::transform(l_Word.begin() + 1, l_Word.end(), l_Word.begin() + 1, [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
            }

            l_Name += l_Word;
        }

        return l_Name;
    }

    std::string WriteEnum(std::string_view comment, std::string_view name, std::string_view underlying, const std::vector<EnumValue>& values)
    {
        std::string l_Text = std::format("    // {}\n    public enum {} : {}\n    {{\n", comment, name, underlying);
        std::vector<EnumValue> l_Kept;
        std::ranges::copy_if(values, std::back_inserter(l_Kept), [](const EnumValue& value) { return value.Name != "Count"; });
        for (std::size_t it_Value = 0; it_Value < l_Kept.size(); ++it_Value)
        {
            l_Text += std::format("        {} = {}{}\n", ToCSharpName(l_Kept[it_Value].Name), l_Kept[it_Value].Number, it_Value + 1 < l_Kept.size() ? "," : "");
        }

        return l_Text + "    }\n";
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

namespace
{
    // Written only when it changes, so ScriptCore builds again only then
    bool WriteIfChanged(const std::filesystem::path& path, const std::string& text)
    {
        if (ReadFile(path) == text)
        {
            return true;
        }

        std::error_code l_Error;
        std::filesystem::create_directories(path.parent_path(), l_Error);
        std::ofstream l_File(path, std::ios::binary | std::ios::trunc);
        l_File << text;
        l_File.close();
        if (!l_File)
        {
            std::fprintf(stderr, "%s: error: could not be written\n", path.string().c_str());

            return false;
        }

        return true;
    }

    bool Report(const std::filesystem::path& path, const std::vector<Error>& errors)
    {
        for (const Error& it_Error : errors)
        {
            std::fprintf(stderr, "%s(%zu): error: %s\n", path.string().c_str(), it_Error.Line, it_Error.Message.c_str());
        }

        return errors.empty();
    }
}

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr, "Usage: Trinity-InteropGen <NativeFunctions.def> <ScriptComponents.def> <KeyCodes.hpp> <MouseCodes.hpp> <output directory>\n");

        return 2;
    }

    const std::filesystem::path l_FunctionsPath = argv[1];
    const std::filesystem::path l_ComponentsPath = argv[2];
    const std::filesystem::path l_KeysPath = argv[3];
    const std::filesystem::path l_MousePath = argv[4];
    const std::filesystem::path l_Output = argv[5];
    for (const std::filesystem::path& it_Path : { l_FunctionsPath, l_ComponentsPath, l_KeysPath, l_MousePath })
    {
        if (!ReadFile(it_Path))
        {
            std::fprintf(stderr, "%s: error: could not be read\n", it_Path.string().c_str());

            return 1;
        }
    }

    std::vector<Error> l_FunctionErrors;
    const std::vector<Function> l_Functions = Parse(*ReadFile(l_FunctionsPath), l_FunctionErrors);
    std::vector<Error> l_ComponentErrors;
    const ComponentList l_Components = ParseComponents(*ReadFile(l_ComponentsPath), l_ComponentErrors);
    std::string l_KeyError;
    const std::optional<std::vector<EnumValue>> l_Keys = ParseEnum(*ReadFile(l_KeysPath), "KeyCode", l_KeyError);
    std::string l_MouseError;
    const std::optional<std::vector<EnumValue>> l_Buttons = ParseEnum(*ReadFile(l_MousePath), "MouseCode", l_MouseError);
    bool l_Good = Report(l_FunctionsPath, l_FunctionErrors);
    l_Good = Report(l_ComponentsPath, l_ComponentErrors) && l_Good;
    if (!l_Keys)
    {
        std::fprintf(stderr, "%s: error: %s\n", l_KeysPath.string().c_str(), l_KeyError.c_str());
        l_Good = false;
    }

    if (!l_Buttons)
    {
        std::fprintf(stderr, "%s: error: %s\n", l_MousePath.string().c_str(), l_MouseError.c_str());
        l_Good = false;
    }

    if (!l_Good)
    {
        return 1;
    }

    std::string l_Input = "// Generated by Trinity-InteropGen from KeyCodes.hpp and MouseCodes.hpp as Trinity-ScriptCore builds. Edits here are lost on the next build\nnamespace Trinity\n{\n";
    l_Input += WriteEnum("A key, numbered as the engine's KeyCode numbers it", "Key", "ushort", *l_Keys) + "\n";
    l_Input += WriteEnum("A mouse button, numbered as the engine's MouseCode numbers it", "MouseButton", "byte", *l_Buttons) + "}\n";

    const bool l_Written = WriteIfChanged(l_Output / "NativeFunctions.g.cs", Write(l_Functions));
    const bool l_ComponentsWritten = WriteIfChanged(l_Output / "Components.g.cs", WriteComponents(l_Components));
    const bool l_InputWritten = WriteIfChanged(l_Output / "Input.g.cs", l_Input);

    return l_Written && l_ComponentsWritten && l_InputWritten ? 0 : 1;
}