#include "Trinity/Asset/AssetManager.hpp"

#include "Trinity/Asset/AssetRegistry.hpp"
#include "Trinity/Core/JobSystem.hpp"
#include "Trinity/Core/Log.hpp"
#include "Trinity/Core/MainThread.hpp"
#include "Trinity/Core/Profiler.hpp"
#include "Trinity/FileSystem/FileSystem.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Trinity
{
    namespace
    {
        class BinaryLoader final : public AssetLoader
        {
        public:
            [[nodiscard]] std::string_view GetAssetType() const override
            {
                return BinaryAsset::c_AssetType;
            }

            [[nodiscard]] Expected<Asset*, std::string> Load(std::span<const std::byte> data) const override
            {
                BinaryAsset* l_Asset = Memory::New<BinaryAsset>(MemoryTag::Assets);
                l_Asset->Data.assign(data.begin(), data.end());

                return l_Asset;
            }
        };

        // Lives from the read finishing until Update takes its result, and owns the file's bytes until the loader is done with them
        struct LoadTask
        {
            UUID ID;
            std::uint64_t Generation = 0;
            const AssetLoader* Loader = nullptr;
            FileBuffer Data;
            Asset* Result = nullptr;
            std::string Error;
        };

        struct Entry
        {
            std::uint32_t References = 0;
            AssetState State = AssetState::Loading;
            std::uint64_t Generation = 0;
            const AssetLoader* Loader = nullptr;
            Asset* Loaded = nullptr;
            FileRequest Request;
        };

        using EntryMap = std::unordered_map<UUID, Entry, std::hash<UUID>, std::equal_to<UUID>, TaggedAllocator<std::pair<const UUID, Entry>, MemoryTag::Assets>>;

        struct State
        {
            EntryMap Entries;
            std::vector<const AssetLoader*, TaggedAllocator<const AssetLoader*, MemoryTag::Engine>> Loaders;
            const AssetRegistry* Registry = nullptr;
            std::uint64_t NextGeneration = 1;
            JobCounter Jobs;
            std::mutex CompletedMutex;
            std::vector<LoadTask*, TaggedAllocator<LoadTask*, MemoryTag::Engine>> Completed;
            BinaryLoader Binary;
        };

        State* s_State = nullptr;

        const AssetLoader* FindLoader(std::string_view assetType)
        {
            const auto a_Found = std::ranges::find_if(s_State->Loaders, [assetType](const AssetLoader* loader) { return loader->GetAssetType() == assetType; });

            return a_Found != s_State->Loaders.end() ? *a_Found : nullptr;
        }

        Entry* FindEntry(UUID id, std::uint64_t generation)
        {
            const auto a_Found = s_State->Entries.find(id);

            return a_Found != s_State->Entries.end() && a_Found->second.Generation == generation ? &a_Found->second : nullptr;
        }

        // On a worker. Loaders must not throw, as no job may
        void Decode(LoadTask* task)
        {
            Expected<Asset*, std::string> l_Result = task->Loader->Load(task->Data);
            if (l_Result)
            {
                task->Result = *l_Result;
            }
            else
            {
                task->Error = l_Result.GetError();
            }

            task->Data = FileBuffer();

            std::scoped_lock l_Lock(s_State->CompletedMutex);
            s_State->Completed.push_back(task);
        }

        // Read requests are cancelled when the last reference goes, so this only runs for an entry still wanted
        void OnRead(UUID id, std::uint64_t generation, Expected<FileBuffer, FileError> result)
        {
            Entry* l_Entry = FindEntry(id, generation);
            if (l_Entry == nullptr)
            {
                return;
            }

            l_Entry->Request = FileRequest();
            if (!result)
            {
                l_Entry->State = l_Entry->Loaded != nullptr ? AssetState::Ready : AssetState::Failed;
                TR_CORE_ERROR("Assets: {} could not be read: {}{}", id, ToString(result.GetError()), l_Entry->Loaded != nullptr ? ", so it keeps the version it had" : "");

                return;
            }

            LoadTask* l_Task = Memory::New<LoadTask>(MemoryTag::Assets);
            l_Task->ID = id;
            l_Task->Generation = generation;
            l_Task->Loader = l_Entry->Loader;
            l_Task->Data = std::move(*result);

            JobSystem::Submit([l_Task] { Decode(l_Task); }, &s_State->Jobs);
        }

        void DestroyEntries()
        {
            for (auto& [it_ID, it_Entry] : s_State->Entries)
            {
                it_Entry.Request.Cancel();
                Memory::Delete(it_Entry.Loaded);
            }

            EntryMap().swap(s_State->Entries);
        }
    }

    std::string_view ToString(AssetState state)
    {
        switch (state)
        {
            case AssetState::None:
            {
                return "none";
            }
            case AssetState::Loading:
            {
                return "loading";
            }
            case AssetState::Ready:
            {
                return "ready";
            }
            case AssetState::Failed:
            {
                return "failed";
            }
        }

        return "unknown";
    }

    std::string AssetLoader::GetLoadPath(const AssetRecord& record) const
    {
        return record.Path;
    }

    namespace AssetManager
    {
        // Bookkeeping is under Engine, so Assets holds only entries and loaded assets, and is 0 B whenever nothing is loaded
        void Initialize()
        {
            TR_CORE_ASSERT(s_State == nullptr, "The asset manager is already initialized.");

            s_State = Memory::New<State>(MemoryTag::Engine);
            RegisterLoader(s_State->Binary);
        }

        void Shutdown()
        {
            TR_CORE_ASSERT(s_State != nullptr, "The asset manager is not initialized.");

            JobSystem::Wait(s_State->Jobs);
            Update();

            if (!s_State->Entries.empty())
            {
                TR_CORE_WARN("Assets: {} asset(s) were still referenced when the asset manager shut down", s_State->Entries.size());
            }

            DestroyEntries();
            Memory::Delete(s_State);
            s_State = nullptr;
        }

        // Takes finished loads on the main thread, once a frame. A result nobody wants any more is thrown away
        void Update()
        {
            TR_PROFILE_FUNCTION();

            decltype(s_State->Completed) l_Completed;
            {
                std::scoped_lock l_Lock(s_State->CompletedMutex);
                l_Completed.swap(s_State->Completed);
            }

            for (LoadTask* it_Task : l_Completed)
            {
                Entry* l_Entry = FindEntry(it_Task->ID, it_Task->Generation);
                if (l_Entry == nullptr)
                {
                    Memory::Delete(it_Task->Result);
                    Memory::Delete(it_Task);

                    continue;
                }

                // What a worker cannot do, such as creating GPU resources, the loader finishes here
                if (it_Task->Result != nullptr)
                {
                    const Expected<void, std::string> l_Finished = it_Task->Loader->Finish(*it_Task->Result);
                    if (!l_Finished)
                    {
                        it_Task->Error = l_Finished.GetError();
                        Memory::Delete(it_Task->Result);
                        it_Task->Result = nullptr;
                    }
                }

                // A reload replaces the version the asset had, which was in use until now. A failed one keeps it
                if (it_Task->Result != nullptr)
                {
                    Memory::Delete(l_Entry->Loaded);
                    l_Entry->Loaded = it_Task->Result;
                    l_Entry->State = AssetState::Ready;
                }
                else
                {
                    l_Entry->State = l_Entry->Loaded != nullptr ? AssetState::Ready : AssetState::Failed;
                    TR_CORE_ERROR("Assets: {} could not be loaded: {}{}", it_Task->ID, it_Task->Error, l_Entry->Loaded != nullptr ? ", so it keeps the version it had" : "");
                }

                Memory::Delete(it_Task);
            }
        }

        // Entries already loading keep their path. A registry swapped for another project's needs no assets left from the old one
        void SetRegistry(const AssetRegistry* registry)
        {
            TR_CORE_ASSERT(registry == nullptr || s_State->Entries.empty() || registry == s_State->Registry, "Assets from one registry are still loaded while another is set");

            s_State->Registry = registry;
        }

        void RegisterLoader(const AssetLoader& loader)
        {
            if (FindLoader(loader.GetAssetType()) != nullptr)
            {
                TR_CORE_ERROR("Assets: a loader for {} assets is already registered", loader.GetAssetType());

                return;
            }

            s_State->Loaders.push_back(&loader);
        }

        // Its decodes may still be running, and its assets may need it to be destroyed, so both end before it goes. Assets still referenced read as failed from then on
        void UnregisterLoader(std::string_view assetType)
        {
            const AssetLoader* l_Loader = FindLoader(assetType);
            if (l_Loader == nullptr)
            {
                return;
            }

            JobSystem::Wait(s_State->Jobs);

            std::size_t l_Dropped = 0;
            {
                std::scoped_lock l_Lock(s_State->CompletedMutex);
                std::erase_if(s_State->Completed, [l_Loader](LoadTask* task)
                {
                    if (task->Loader != l_Loader)
                    {
                        return false;
                    }

                    Memory::Delete(task->Result);
                    Memory::Delete(task);

                    return true;
                });
            }

            for (auto& [it_ID, it_Entry] : s_State->Entries)
            {
                if (it_Entry.Loader != l_Loader)
                {
                    continue;
                }

                it_Entry.Request.Cancel();
                Memory::Delete(it_Entry.Loaded);
                it_Entry.Loaded = nullptr;
                it_Entry.Loader = nullptr;
                it_Entry.State = AssetState::Failed;
                ++l_Dropped;
            }

            if (l_Dropped != 0)
            {
                TR_CORE_WARN("Assets: {} {} asset(s) were still referenced when their loader went, so they now read as failed", l_Dropped, assetType);
            }

            std::erase(s_State->Loaders, l_Loader);
        }

        void Acquire(UUID id)
        {
            if (!id)
            {
                return;
            }

            TR_CORE_ASSERT(MainThread::IsMainThread(), "Assets are acquired and released on the main thread.");

            const auto [a_Found, a_Inserted] = s_State->Entries.try_emplace(id);
            Entry& l_Entry = a_Found->second;
            ++l_Entry.References;
            if (!a_Inserted)
            {
                return;
            }

            l_Entry.Generation = s_State->NextGeneration++;

            const AssetRecord* l_Record = s_State->Registry != nullptr ? s_State->Registry->Find(id) : nullptr;
            if (l_Record == nullptr)
            {
                l_Entry.State = AssetState::Failed;
                TR_CORE_ERROR("Assets: no asset has the UUID {}", id);

                return;
            }

            l_Entry.Loader = FindLoader(l_Record->Importer);
            if (l_Entry.Loader == nullptr)
            {
                l_Entry.State = AssetState::Failed;
                TR_CORE_ERROR("Assets: {} cannot load, since no loader takes {} assets", l_Record->Path, l_Record->Importer);

                return;
            }

            l_Entry.Request = FileSystem::ReadFileAsync(l_Entry.Loader->GetLoadPath(*l_Record), [id, l_Generation = l_Entry.Generation](Expected<FileBuffer, FileError> result) { OnRead(id, l_Generation, std::move(result)); });
        }

        void Release(UUID id)
        {
            if (!id)
            {
                return;
            }

            TR_CORE_ASSERT(MainThread::IsMainThread(), "Assets are acquired and released on the main thread.");

            const auto a_Found = s_State->Entries.find(id);
            TR_CORE_ASSERT(a_Found != s_State->Entries.end(), "Asset {} was released more often than it was acquired", id);
            if (a_Found == s_State->Entries.end() || --a_Found->second.References != 0)
            {
                return;
            }

            a_Found->second.Request.Cancel();
            Memory::Delete(a_Found->second.Loaded);
            s_State->Entries.erase(a_Found);

            // An emptied map keeps its buckets, so it is replaced, and Assets reads 0 B
            if (s_State->Entries.empty())
            {
                EntryMap().swap(s_State->Entries);
            }
        }

        // As after a reimport. The version already loaded stays in use until the new one is ready, and a load still running is dropped. An asset gone from the registry fails, so it reads as its loader's placeholder
        void Reload(UUID id)
        {
            TR_CORE_ASSERT(MainThread::IsMainThread(), "Assets are reloaded on the main thread.");

            const auto a_Found = s_State->Entries.find(id);
            if (a_Found == s_State->Entries.end())
            {
                return;
            }

            Entry& l_Entry = a_Found->second;
            l_Entry.Request.Cancel();
            l_Entry.Generation = s_State->NextGeneration++;

            const AssetRecord* l_Record = s_State->Registry != nullptr ? s_State->Registry->Find(id) : nullptr;
            const AssetLoader* l_Loader = l_Record != nullptr ? FindLoader(l_Record->Importer) : nullptr;
            if (l_Loader == nullptr)
            {
                Memory::Delete(l_Entry.Loaded);
                l_Entry.Loaded = nullptr;
                l_Entry.State = AssetState::Failed;
                TR_CORE_WARN("Assets: {} {}, so it reads as failed", id, l_Record == nullptr ? "is no longer in the project" : "has no loader any more");

                return;
            }

            l_Entry.Loader = l_Loader;
            l_Entry.State = l_Entry.Loaded != nullptr ? AssetState::Ready : AssetState::Loading;
            l_Entry.Request = FileSystem::ReadFileAsync(l_Loader->GetLoadPath(*l_Record), [id, l_Generation = l_Entry.Generation](Expected<FileBuffer, FileError> result) { OnRead(id, l_Generation, std::move(result)); });
        }

        AssetState GetState(UUID id)
        {
            const auto a_Found = s_State->Entries.find(id);

            return a_Found != s_State->Entries.end() ? a_Found->second.State : AssetState::None;
        }

        // The asset once ready, and until then, or after a failure, its loader's placeholder
        const Asset* GetAsset(UUID id)
        {
            const auto a_Found = s_State->Entries.find(id);
            if (a_Found == s_State->Entries.end())
            {
                return nullptr;
            }

            const Entry& l_Entry = a_Found->second;
            if (l_Entry.State == AssetState::Ready)
            {
                return l_Entry.Loaded;
            }

            return l_Entry.Loader != nullptr ? l_Entry.Loader->GetPlaceholder() : nullptr;
        }

        std::size_t GetEntryCount()
        {
            return s_State->Entries.size();
        }
    }
}