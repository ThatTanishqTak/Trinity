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

// Exports the project as a game that runs with nothing installed. A failed export leaves the last
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

    // Into the folder. closeWhenDone is --export's, run is Build And Run's. Nothing while one runs
    void Start(const std::filesystem::path& folder, bool closeWhenDone = false, bool run = false);
    // Once a frame. Waits while the project imports or builds, then runs each stage in turn
    void Update();
    // Ends what is running, waits for it, and removes the staging folder, leaving the last export
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

    // What stops the settings from making a game, each a sentence for the Build panel. Empty if none
    [[nodiscard]] static std::vector<std::string> CheckSettings(const Trinity::Project& project, const Trinity::AssetRegistry& registry);
    // Trinity's build of a configuration in Forge's version, two folders above Forge's own, or why none
    [[nodiscard]] static Trinity::Expected<std::filesystem::path, std::string> GetTrinityDirectory(std::string_view configuration);
    // The platforms this version builds games for, as its manifest lists them, or its own alone
    [[nodiscard]] static std::vector<std::string> GetPlatforms();
    // With .exe on Windows, without the characters Windows refuses, and never a device name such as CON
    [[nodiscard]] static std::string GetExecutableName(std::string_view product);

private:
    // The pack's write, on a job. Both threads read the flags and the count
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
    // Once the pack is written: the player and what goes beside it, then the swap. False once it failed
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
    // Trinity's build of the configuration the player comes from, found as the player's build starts
    std::filesystem::path m_Trinity;
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