#include "Trinity/Core/PlayerPrefs.hpp"

#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/Memory.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"

#include <yaml-cpp/yaml.h>

#include <functional>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace Trinity
{
    namespace
    {
        using Value = std::variant<std::int32_t, float, std::string>;

        // A path other than the default is kept only while it is set, so nothing is held once the values are closed
        struct Store
        {
            std::string Path;
            std::map<std::string, Value, std::less<>> Values;
            bool Loaded = false;
            bool Dirty = false;

            [[nodiscard]] std::string_view GetPath() const
            {
                return Path.empty() ? PlayerPrefs::c_DefaultPath : std::string_view(Path);
            }
        };

        Store& GetStore()
        {
            static Store s_Store;

            return s_Store;
        }

        template<typename T>
        void ReadSection(const YAML::Node& root, const char* name, std::map<std::string, Value, std::less<>>& values)
        {
            const YAML::Node l_Section = root[name];
            if (!l_Section || !l_Section.IsMap())
            {
                return;
            }

            for (const auto& it_Entry : l_Section)
            {
                values.insert_or_assign(it_Entry.first.as<std::string>(), Value(std::in_place_type<T>, it_Entry.second.as<T>()));
            }
        }

        // The file, the first time the values are used since they were last closed. One that is not there yet is no error
        Store& Load()
        {
            Store& l_Store = GetStore();
            if (l_Store.Loaded)
            {
                return l_Store;
            }

            l_Store.Loaded = true;
            const Expected<std::string, FileError> l_Text = FileSystem::ReadText(l_Store.GetPath());
            if (!l_Text)
            {
                if (l_Text.GetError() != FileError::NotFound)
                {
                    TR_CORE_ERROR("PlayerPrefs: {} could not be read, so none are kept: {}", l_Store.GetPath(), ToString(l_Text.GetError()));
                }

                return l_Store;
            }

            const MemoryTagScope l_Scope(MemoryTag::Engine);
            try
            {
                const YAML::Node l_Root = YAML::Load(*l_Text);
                ReadSection<std::int32_t>(l_Root, "Int", l_Store.Values);
                ReadSection<float>(l_Root, "Float", l_Store.Values);
                ReadSection<std::string>(l_Root, "String", l_Store.Values);
            }
            catch (const YAML::Exception& exception)
            {
                l_Store.Values.clear();
                TR_CORE_ERROR("PlayerPrefs: {} could not be read, so none are kept: {}", l_Store.GetPath(), exception.what());
            }

            return l_Store;
        }

        template<typename T>
        void Set(std::string_view key, T value)
        {
            Store& l_Store = Load();
            const MemoryTagScope l_Scope(MemoryTag::Engine);
            const auto a_Found = l_Store.Values.find(key);
            if (a_Found == l_Store.Values.end())
            {
                l_Store.Values.emplace(std::string(key), Value(std::in_place_type<T>, std::move(value)));
            }
            else if (const T* l_Held = std::get_if<T>(&a_Found->second); l_Held == nullptr || *l_Held != value)
            {
                a_Found->second = Value(std::in_place_type<T>, std::move(value));
            }
            else
            {
                return;
            }

            l_Store.Dirty = true;
        }

        template<typename T>
        [[nodiscard]] const T* Find(std::string_view key)
        {
            const Store& l_Store = Load();
            const auto a_Found = l_Store.Values.find(key);

            return a_Found != l_Store.Values.end() ? std::get_if<T>(&a_Found->second) : nullptr;
        }

        // Each kind in a map of its own, the keys sorted, and floats with the digits that read back as the same float
        template<typename T>
        void WriteSection(YAML::Emitter& emitter, const char* name, const std::map<std::string, Value, std::less<>>& values)
        {
            emitter << YAML::Key << name << YAML::Value << YAML::BeginMap;
            for (const auto& [it_Key, it_Value] : values)
            {
                if (const T* l_Value = std::get_if<T>(&it_Value))
                {
                    emitter << YAML::Key << YAML::DoubleQuoted << it_Key << YAML::Value;
                    if constexpr (std::is_same_v<T, std::string>)
                    {
                        emitter << YAML::DoubleQuoted << *l_Value;
                    }
                    else
                    {
                        emitter << *l_Value;
                    }
                }
            }

            emitter << YAML::EndMap;
        }
    }

    void PlayerPrefs::SetInt(std::string_view key, std::int32_t value)
    {
        Set<std::int32_t>(key, value);
    }

    std::int32_t PlayerPrefs::GetInt(std::string_view key, std::int32_t defaultValue)
    {
        const std::int32_t* l_Value = Find<std::int32_t>(key);

        return l_Value != nullptr ? *l_Value : defaultValue;
    }

    void PlayerPrefs::SetFloat(std::string_view key, float value)
    {
        Set<float>(key, value);
    }

    float PlayerPrefs::GetFloat(std::string_view key, float defaultValue)
    {
        const float* l_Value = Find<float>(key);

        return l_Value != nullptr ? *l_Value : defaultValue;
    }

    void PlayerPrefs::SetString(std::string_view key, std::string_view value)
    {
        Set<std::string>(key, std::string(value));
    }

    std::string PlayerPrefs::GetString(std::string_view key, std::string_view defaultValue)
    {
        return std::string(GetStringView(key, defaultValue));
    }

    std::string_view PlayerPrefs::GetStringView(std::string_view key, std::string_view defaultValue)
    {
        const std::string* l_Value = Find<std::string>(key);

        return l_Value != nullptr ? std::string_view(*l_Value) : defaultValue;
    }

    bool PlayerPrefs::HasKey(std::string_view key)
    {
        return Load().Values.contains(key);
    }

    void PlayerPrefs::DeleteKey(std::string_view key)
    {
        Store& l_Store = Load();
        if (const auto a_Found = l_Store.Values.find(key); a_Found != l_Store.Values.end())
        {
            l_Store.Values.erase(a_Found);
            l_Store.Dirty = true;
        }
    }

    void PlayerPrefs::DeleteAll()
    {
        Store& l_Store = Load();
        l_Store.Dirty = l_Store.Dirty || !l_Store.Values.empty();
        l_Store.Values.clear();
    }

    bool PlayerPrefs::Save()
    {
        Store& l_Store = GetStore();
        if (!l_Store.Dirty)
        {
            return true;
        }

        YAML::Emitter l_Emitter;
        l_Emitter.SetFloatPrecision(std::numeric_limits<float>::max_digits10);
        l_Emitter << YAML::BeginMap;
        WriteSection<std::int32_t>(l_Emitter, "Int", l_Store.Values);
        WriteSection<float>(l_Emitter, "Float", l_Store.Values);
        WriteSection<std::string>(l_Emitter, "String", l_Store.Values);
        l_Emitter << YAML::EndMap;

        if (const Expected<void, FileError> l_Written = FileSystem::WriteText(l_Store.GetPath(), std::string_view(l_Emitter.c_str(), l_Emitter.size())); !l_Written)
        {
            TR_CORE_ERROR("PlayerPrefs: {} could not be written, so its values are not saved yet: {}", l_Store.GetPath(), ToString(l_Written.GetError()));

            return false;
        }

        l_Store.Dirty = false;

        return true;
    }

    void PlayerPrefs::SetPath(std::string_view path)
    {
        Close();
        if (path != c_DefaultPath)
        {
            const MemoryTagScope l_Scope(MemoryTag::Engine);
            GetStore().Path = path;
        }
    }

    // The path goes back to the default too, so a store set for Forge's Play holds nothing once it stops
    void PlayerPrefs::Close()
    {
        Store& l_Store = GetStore();
        static_cast<void>(Save());
        l_Store.Values = {};
        l_Store.Path = std::string();
        l_Store.Loaded = false;
        l_Store.Dirty = false;
    }
}