#include "Trinity/Core/Log.hpp"

#include "Trinity/Core/Memory.hpp"

#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace Trinity
{
    namespace
    {
        // Lines below warnings reach the file within this long too, so a crash loses little of what led up to it
        constexpr std::chrono::seconds c_FlushInterval{ 1 };

        // What a run logs before its file opens, past which lines wait no more and are counted instead
        constexpr std::size_t c_PendingCapacity = 1024 * 1024;

        // The log file, opened once the application knows its folder. Until then each line waits, formatted as the file has it, so the file starts with the run's first line. Both loggers share it, under its own lock
        class DeferredFileSink final : public spdlog::sinks::base_sink<std::mutex>
        {
        public:
            // The file opened, after what waited, or the reason it was not
            [[nodiscard]] std::string Open(const std::filesystem::path& path)
            {
                std::scoped_lock l_Lock(mutex_);
                try
                {
                    m_File.open(path.string(), true);
                }
                catch (const std::exception& exception)
                {
                    return exception.what();
                }

                m_Open = true;
                spdlog::memory_buf_t l_Pending;
                l_Pending.append(m_Pending.data(), m_Pending.data() + m_Pending.size());
                m_File.write(l_Pending);
                std::string().swap(m_Pending);
                if (m_Dropped != 0)
                {
                    const std::string l_Note = std::format("[{} lines logged before the file opened were dropped, as too many waited]\n", m_Dropped);
                    spdlog::memory_buf_t l_Buffer;
                    l_Buffer.append(l_Note.data(), l_Note.data() + l_Note.size());
                    m_File.write(l_Buffer);
                }

                m_File.flush();

                return {};
            }

        protected:
            void sink_it_(const spdlog::details::log_msg& message) override
            {
                spdlog::memory_buf_t l_Formatted;
                formatter_->format(message, l_Formatted);
                if (m_Open)
                {
                    m_File.write(l_Formatted);
                }
                else if (m_Pending.size() + l_Formatted.size() <= c_PendingCapacity)
                {
                    m_Pending.append(l_Formatted.data(), l_Formatted.size());
                }
                else
                {
                    ++m_Dropped;
                }
            }

            void flush_() override
            {
                if (m_Open)
                {
                    m_File.flush();
                }
            }

        private:
            spdlog::details::file_helper m_File;
            bool m_Open = false;
            std::string m_Pending;
            std::size_t m_Dropped = 0;
        };

        std::array<std::shared_ptr<spdlog::logger>, 2> s_Loggers;
        std::shared_ptr<DeferredFileSink> s_FileSink;
        std::filesystem::path s_FilePath;
        std::filesystem::path s_PreviousFilePath;

        spdlog::logger* GetLogger(LogChannel channel)
        {
            return s_Loggers[static_cast<std::size_t>(channel)].get();
        }

        spdlog::level::level_enum ToSpdlogLevel(LogLevel level)
        {
            switch (level)
            {
                case LogLevel::Trace:
                {
                    return spdlog::level::trace;
                }
                case LogLevel::Info:
                {
                    return spdlog::level::info;
                }
                case LogLevel::Warn:
                {
                    return spdlog::level::warn;
                }
                case LogLevel::Error:
                {
                    return spdlog::level::err;
                }
                case LogLevel::Critical:
                {
                    return spdlog::level::critical;
                }
            }

            return spdlog::level::critical;
        }

        LogLevel ToLogLevel(spdlog::level::level_enum level)
        {
            switch (level)
            {
                case spdlog::level::trace:
                case spdlog::level::debug:
                {
                    return LogLevel::Trace;
                }
                case spdlog::level::info:
                {
                    return LogLevel::Info;
                }
                case spdlog::level::warn:
                {
                    return LogLevel::Warn;
                }
                case spdlog::level::err:
                {
                    return LogLevel::Error;
                }
                default:
                {
                    return LogLevel::Critical;
                }
            }
        }

        // The history takes its own lock, so the sink needs none
        class HistorySink final : public spdlog::sinks::base_sink<spdlog::details::null_mutex>
        {
        public:
            explicit HistorySink(LogChannel channel) : m_Channel(channel)
            {

            }

        protected:
            void sink_it_(const spdlog::details::log_msg& message) override
            {
                LogHistory::Add(m_Channel, ToLogLevel(message.level), message.time, std::string_view(message.payload.data(), message.payload.size()));
            }

            void flush_() override
            {

            }

        private:
            LogChannel m_Channel;
        };

        struct History
        {
            std::mutex Mutex;
            LogEntry* Entries = nullptr;
            char* Text = nullptr;
            std::size_t First = 0;
            std::size_t Count = 0;
            std::size_t TextHead = 0;
            std::atomic<std::uint64_t> NextSequence{ 0 };
        };

        History s_History;

        LogEntry& GetEntry(std::size_t index)
        {
            return s_History.Entries[(s_History.First + index) % LogHistory::c_LineCapacity];
        }

        std::size_t GetTextOffset(const LogEntry& entry)
        {
            return static_cast<std::size_t>(entry.Text.data() - s_History.Text);
        }

        void DropOldest()
        {
            s_History.First = (s_History.First + 1) % LogHistory::c_LineCapacity;
            if (--s_History.Count == 0)
            {
                s_History.First = 0;
                s_History.TextHead = 0;
            }
        }
    }

    std::string_view ToString(LogChannel channel)
    {
        return channel == LogChannel::Core ? "TRINITY" : "APP";
    }

    std::string_view ToString(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace:
            {
                return "trace";
            }
            case LogLevel::Info:
            {
                return "info";
            }
            case LogLevel::Warn:
            {
                return "warning";
            }
            case LogLevel::Error:
            {
                return "error";
            }
            case LogLevel::Critical:
            {
                return "critical";
            }
        }

        return "critical";
    }

    void Log::Initialize()
    {
        std::vector<spdlog::sink_ptr> l_Sinks;

        auto a_ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        a_ConsoleSink->set_pattern("%^[%T.%e] %-7n %-8l%$ %v");
        l_Sinks.push_back(std::move(a_ConsoleSink));

        s_FileSink = std::make_shared<DeferredFileSink>();
        s_FileSink->set_pattern("[%Y-%m-%d %T.%e] %-7n %-8l %v");
        l_Sinks.push_back(s_FileSink);

        std::vector<spdlog::sink_ptr> l_CoreSinks = l_Sinks;
        l_CoreSinks.push_back(std::make_shared<HistorySink>(LogChannel::Core));
        std::vector<spdlog::sink_ptr> l_ClientSinks = l_Sinks;
        l_ClientSinks.push_back(std::make_shared<HistorySink>(LogChannel::Client));

        s_Loggers[static_cast<std::size_t>(LogChannel::Core)] = std::make_shared<spdlog::logger>("TRINITY", l_CoreSinks.begin(), l_CoreSinks.end());
        s_Loggers[static_cast<std::size_t>(LogChannel::Client)] = std::make_shared<spdlog::logger>("APP", l_ClientSinks.begin(), l_ClientSinks.end());

        for (const std::shared_ptr<spdlog::logger>& it_Logger : s_Loggers)
        {
            it_Logger->set_level(spdlog::level::trace);
            it_Logger->flush_on(spdlog::level::warn);
            spdlog::register_logger(it_Logger);
        }

        spdlog::flush_every(c_FlushInterval);
    }

    // The last run's log is kept beside the new one, as Unity keeps Editor-prev.log, so what led up to a crash is still there after the restart
    void Log::OpenFile(const std::filesystem::path& logFile)
    {
        if (s_FileSink == nullptr || !s_FilePath.empty())
        {
            return;
        }

        std::filesystem::path l_Previous = logFile;
        l_Previous.replace_filename(std::filesystem::path(logFile.stem()) += "-prev");
        l_Previous += logFile.extension();

        std::error_code l_Ignored;
        std::error_code l_KeepError;
        std::string l_Error;
        try
        {
            if (logFile.has_parent_path())
            {
                std::filesystem::create_directories(logFile.parent_path(), l_Ignored);
            }

            if (std::filesystem::exists(logFile, l_Ignored))
            {
                std::filesystem::rename(logFile, l_Previous, l_KeepError);
            }

            l_Error = s_FileSink->Open(logFile);
        }
        catch (const std::exception& exception)
        {
            l_Error = exception.what();
        }

        if (!l_Error.empty())
        {
            TR_CORE_WARN("File logging is disabled: {}", l_Error);

            return;
        }

        s_FilePath = logFile;
        s_PreviousFilePath = std::filesystem::exists(l_Previous, l_Ignored) ? l_Previous : std::filesystem::path();
        TR_CORE_INFO("Logging to {}", s_FilePath.string());
        if (l_KeepError)
        {
            TR_CORE_WARN("The last run's log could not be kept as {}, so it was written over: {}", l_Previous.string(), l_KeepError.message());
        }
    }

    const std::filesystem::path& Log::GetFilePath()
    {
        return s_FilePath;
    }

    const std::filesystem::path& Log::GetPreviousFilePath()
    {
        return s_PreviousFilePath;
    }

    void Log::Shutdown()
    {
        for (std::shared_ptr<spdlog::logger>& it_Logger : s_Loggers)
        {
            it_Logger.reset();
        }

        s_FileSink.reset();
        spdlog::shutdown();
    }

    bool Log::ShouldLog(LogChannel channel, LogLevel level)
    {
        const spdlog::logger* l_Logger = GetLogger(channel);

        return l_Logger != nullptr && l_Logger->should_log(ToSpdlogLevel(level));
    }

    void Log::Write(LogChannel channel, LogLevel level, std::string_view message)
    {
        if (spdlog::logger* l_Logger = GetLogger(channel))
        {
            l_Logger->log(ToSpdlogLevel(level), message);
        }
    }

    std::string Log::FormatTime(std::chrono::system_clock::time_point time)
    {
        const std::tm l_Local = spdlog::details::os::localtime(std::chrono::system_clock::to_time_t(time));
        const auto a_Milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;

        return std::format("{:02}:{:02}:{:02}.{:03}", l_Local.tm_hour, l_Local.tm_min, l_Local.tm_sec, a_Milliseconds);
    }

    void Log::Flush()
    {
        for (const std::shared_ptr<spdlog::logger>& it_Logger : s_Loggers)
        {
            if (it_Logger != nullptr)
            {
                it_Logger->flush();
            }
        }
    }

    LogHistory::Reader::Reader(std::unique_lock<std::mutex> lock) : m_Lock(std::move(lock))
    {

    }

    std::size_t LogHistory::Reader::GetCount() const
    {
        return s_History.Count;
    }

    // Oldest first
    const LogEntry& LogHistory::Reader::operator[](std::size_t index) const
    {
        return GetEntry(index);
    }

    // After Memory starts, so the history counts under its tag, and lines written before then are not kept
    void LogHistory::Initialize()
    {
        std::scoped_lock l_Lock(s_History.Mutex);
        s_History.Entries = static_cast<LogEntry*>(Memory::Allocate(sizeof(LogEntry) * c_LineCapacity, MemoryTag::Log, alignof(LogEntry)));
        std::uninitialized_value_construct_n(s_History.Entries, c_LineCapacity);
        s_History.Text = static_cast<char*>(Memory::Allocate(c_TextCapacity, MemoryTag::Log));
        s_History.First = 0;
        s_History.Count = 0;
        s_History.TextHead = 0;
    }

    // Before Memory's leak report
    void LogHistory::Shutdown()
    {
        std::scoped_lock l_Lock(s_History.Mutex);
        std::destroy_n(s_History.Entries, c_LineCapacity);
        Memory::Free(s_History.Entries);
        Memory::Free(s_History.Text);
        s_History.Entries = nullptr;
        s_History.Text = nullptr;
        s_History.Count = 0;
    }

    LogHistory::Reader LogHistory::Read()
    {
        return Reader(std::unique_lock(s_History.Mutex));
    }

    // Moves on with every line added and every clear, so a reader can tell the history changed without taking the lock
    std::uint64_t LogHistory::GetNextSequence()
    {
        return s_History.NextSequence.load(std::memory_order_acquire);
    }

    void LogHistory::Clear()
    {
        std::scoped_lock l_Lock(s_History.Mutex);
        s_History.First = 0;
        s_History.Count = 0;
        s_History.TextHead = 0;
        s_History.NextSequence.fetch_add(1, std::memory_order_release);
    }

    // The sequence is taken under the lock, so lines from different threads keep the order they were written in
    void LogHistory::Add(LogChannel channel, LogLevel level, std::chrono::system_clock::time_point time, std::string_view text)
    {
        std::scoped_lock l_Lock(s_History.Mutex);
        if (s_History.Entries == nullptr)
        {
            return;
        }

        const std::size_t l_Size = std::min(text.size(), c_MaximumLineSize);
        const std::size_t l_Footprint = l_Size + 1;
        std::size_t l_Start = s_History.TextHead;
        if (l_Start + l_Footprint > c_TextCapacity)
        {
            // Lines in the unused tail are older than any at the front, so they go first
            while (s_History.Count > 0 && GetTextOffset(GetEntry(0)) >= s_History.TextHead)
            {
                DropOldest();
            }

            l_Start = 0;
        }

        while (s_History.Count == c_LineCapacity)
        {
            DropOldest();
        }

        while (s_History.Count > 0)
        {
            const LogEntry& l_Oldest = GetEntry(0);
            const std::size_t l_Offset = GetTextOffset(l_Oldest);
            if (l_Offset >= l_Start + l_Footprint || l_Offset + l_Oldest.Text.size() + 1 <= l_Start)
            {
                break;
            }

            DropOldest();
        }

        std::memcpy(s_History.Text + l_Start, text.data(), l_Size);
        s_History.Text[l_Start + l_Size] = '\0';
        s_History.TextHead = l_Start + l_Footprint;

        LogEntry& l_Entry = GetEntry(s_History.Count);
        l_Entry.Sequence = s_History.NextSequence.fetch_add(1, std::memory_order_release);
        l_Entry.Time = time;
        l_Entry.Channel = channel;
        l_Entry.Level = level;
        l_Entry.Text = std::string_view(s_History.Text + l_Start, l_Size);
        ++s_History.Count;
    }
}