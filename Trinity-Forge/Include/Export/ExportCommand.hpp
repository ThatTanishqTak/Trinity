#pragma once

#include "Export/GameRunner.hpp"
#include "Export/PackBuilder.hpp"
#include "Modules/AssemblyBuild.hpp"
#include "Modules/ModuleBuild.hpp"

#include <Trinity.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class EditorSession;

// Exports the open project as a game a player runs with nothing installed, as its export settings say, from the Build panel or from Forge's --export=<folder>, which then closes Forge with exit code 0, or 1 when it failed. Once every import and Forge's own builds of the module and the scripts have ended, the player is made: a project with a C++ module has it linked into a Distribution player by CMake, against the package of Trinity's Distribution build, and one without takes that build's prebuilt Trinity-Runtime, which needs no compiler. A Development Build instead takes the prebuilt Release player, with its logs, asserts, profiler and overlay, beside the shared Release engine, and the module built for Release, which the player loads as Forge does. Then its C# scripts are compiled ahead of time with NativeAOT, and BuildWalk gathers what the ticked scenes reach, cooking the scenes and prefabs, into Game.trpak with its Game.trgame, written on a job, each file's compressed bytes kept in the project's Build/Export cache by its content, so the next export compresses only what changed. The player goes in named for the product, a Windows one with its icon and version written in, with the scripts' library, the D3D12 Agility SDK runtime, the GameInput redistributable and the engine's shaders for the export's graphics APIs. Everything is written into a staging folder beside the output folder, which then replaces the output folder whole, so an export that fails or is cancelled leaves the last one as it was. Build And Run then starts the game, whose log the Console shows, and an export into the folder a game it started runs from closes that game first, as Windows will not replace the files of a program that runs
class ExportCommand
{
public:
    // Where an export is, for its progress popup
    enum class Stage : std::uint8_t
    {
        Waiting,
        BuildingPlayer,
        CompilingScripts,
        Packing
    };

    explicit ExportCommand(EditorSession& session) : m_Session(session)
    {

    }

    // Its builds end with it, and the pack's job is waited for, so none outlives what it writes into
    ~ExportCommand();

    ExportCommand(const ExportCommand&) = delete;
    ExportCommand& operator=(const ExportCommand&) = delete;

    // Into the folder, closing Forge once it is exported or has failed when asked to, as --export does, or starting the game once it is exported, as Build And Run does. Nothing while one is under way
    void Start(const std::filesystem::path& folder, bool closeWhenDone = false, bool run = false);
    // Once a frame. Waits while the project imports or builds, then runs each stage in turn
    void Update();
    // Ends what is running, waits for it, and removes the staging folder, so the output folder is as the last export left it
    void Cancel();

    [[nodiscard]] bool IsRunning() const { return m_Folder.has_value(); }
    // The game Build And Run started, and ending it
    [[nodiscard]] const GameRunner& GetGame() const { return m_Game; }
    void StopGame() { m_Game.Stop(); }
    [[nodiscard]] Stage GetStage() const { return m_Stage; }
    // What it is doing, as a line for the popup, and how far along from 0 to 1 where that can be told
    [[nodiscard]] std::string GetStatus() const;
    [[nodiscard]] std::optional<float> GetProgress() const;
    [[nodiscard]] double GetSeconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Started).count(); }

    // What stops the project's settings from making a game, each a sentence for the Build panel and the Console: a scene of the build that is missing or is not a scene, no scene ticked, no product or company, which its saves and log go under, an icon that is missing or is not a texture, no graphics API, or an output folder that is missing or would replace the project's own files. Empty when nothing does
    [[nodiscard]] static std::vector<std::string> CheckSettings(const Trinity::Project& project, const Trinity::AssetRegistry& registry);
    // Trinity's build of a configuration, beside Forge's own build in bin as <configuration>-<platform>/Trinity. Distribution's holds the prebuilt player, the package a game's player is built against and the runtimes copied beside a game, and Release's the same for a Development Build, with the shared engine its player loads
    [[nodiscard]] static std::filesystem::path GetTrinityDirectory(std::string_view configuration);
    // The product's name as the executable's, with .exe on Windows: without the characters Windows refuses in a file name, nor the dots and spaces it drops from the end, and never a device name such as CON. Game when nothing is left
    [[nodiscard]] static std::string GetExecutableName(std::string_view product);

private:
    // The pack's write, on a job. The flags and the count are read by both threads, and the result written by the job before Done
    struct Packing
    {
        PackBuilder Builder;
        PackWriteOptions Options;
        std::filesystem::path Output;
        std::atomic<bool> Cancel = false;
        std::atomic<std::size_t> Written = 0;
        std::atomic<bool> Done = false;
        std::optional<Trinity::Expected<PackSummary, std::string>> Result;
    };

    // A file the export wrote, by where it lies in the folder, and the kind the Console groups it under
    struct WrittenFile
    {
        std::filesystem::path Path;
        std::string Kind;
    };

    void StartPlayer();
    void StartScripts();
    void StartPacking();
    // Once the pack is written: the player and what goes beside it, then the swap. False once it has failed, which it has said why
    [[nodiscard]] bool CopyFiles();
    [[nodiscard]] bool CopyPlayer(const std::filesystem::path& to);
    [[nodiscard]] bool CopyFile(const std::filesystem::path& from, const std::filesystem::path& to, std::string kind);
    [[nodiscard]] bool ReplaceOutput();
    void Report(const PackSummary& pack) const;
    void Fail(std::string_view reason);
    void Finish(bool succeeded);
    // The builds and the pack's job stopped and waited for, and the staging folder removed
    void Stop();

    EditorSession& m_Session;
    std::optional<std::filesystem::path> m_Folder;
    std::filesystem::path m_Staging;
    bool m_CloseWhenDone = false;
    // Read from the settings as the export begins
    bool m_Development = false;
    bool m_RunWhenDone = false;
    Stage m_Stage = Stage::Waiting;
    std::chrono::steady_clock::time_point m_Started;
    ModuleBuild m_PlayerBuild;
    std::filesystem::path m_Player;
    // A Development Build's module, built for Release, which its player loads from beside the pack
    std::filesystem::path m_Module;
    AssemblyBuild m_Compiler;
    std::filesystem::path m_Compiled;
    std::unique_ptr<Packing> m_Packing;
    Trinity::JobCounter m_PackJobs;
    std::size_t m_PackFiles = 0;
    std::size_t m_Assets = 0;
    std::size_t m_Scenes = 0;
    // How many of the pack's files lie in each of its folders, and their sizes, for the Console
    std::map<std::string, std::pair<std::size_t, std::uint64_t>> m_PackKinds;
    std::vector<WrittenFile> m_Written;
    GameRunner m_Game;
};