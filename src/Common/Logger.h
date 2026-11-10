#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

#include "Assert.h"
#include "LFQueue.h"
#include "Thread.h"
#include "Time.h"
#include "Types.h"

namespace Common {

    // How important a log record is. Ordered from least to most important, so levels can be
    // compared: e.g. "only write records with level >= WARN".
    // (ERR, not ERROR: <windows.h> defines a macro named ERROR, which would break this enum.)
    enum class LogLevel : std::uint8_t {
        DEBUG,   // detailed information for finding bugs
        INFO,    // normal events: order received, order filled, ...
        WARN,    // something unusual that the program can handle
        ERR      // something went wrong
    };

    // Text form of a log level, for the log file. Returns a view of a string literal (no allocation).
    [[nodiscard]] constexpr std::string_view logLevelToString(LogLevel level) noexcept {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERR:   return "ERROR";
        }
        return "UNKNOWN";   // a value that is none of the enumerators
    }

    // One value to be inserted into a log message. Only small types that can be copied as raw bytes:
    // signed numbers (prices, ...), unsigned numbers (ids, quantities), doubles, characters and sides.
    using LogArg = std::variant<std::int64_t, std::uint64_t, double, char, Side>;

    // Maximum number of values one log record can carry.
    inline constexpr std::size_t LOG_MAX_ARGS = 4;

    // Everything the hot path hands to the logger thread for one log line. Fixed size, no pointers to
    // temporary data, cheap to copy: the logger thread turns it into text later.
    struct LogRecord {
        Nanos wallTime = 0;                        // when it happened (getWallClockNanos())
        const char* format = nullptr;              // a STRING LITERAL with {} placeholders, e.g. "order {} filled"
        std::array<LogArg, LOG_MAX_ARGS> args{};   // the values for the placeholders
        std::uint8_t argCount = 0;                 // how many entries of args are used
        LogLevel level = LogLevel::INFO;
    };

    // The types a log() call accepts as values: integers, floating-point numbers, and Side.
    template <typename T>
    concept Loggable = std::integral<T> || std::floating_point<T> || std::same_as<T, Side>;

    // Converts one value into the matching LogArg alternative. Checked in this order:
    // Side and char are kept as they are (printed as text / a character), floating -> double,
    // signed integers -> std::int64_t, unsigned integers -> std::uint64_t.
    template <Loggable T>
    [[nodiscard]] constexpr LogArg toLogArg(const T& value) noexcept {
        if constexpr (std::same_as<T, Side> || std::same_as<T, char>) {
            return value;
        } else if constexpr (std::floating_point<T>) {
            return static_cast<double>(value);
        } else if constexpr (std::signed_integral<T>) {
            return static_cast<std::int64_t>(value);
        } else {
            return static_cast<std::uint64_t>(value);
        }
    }

    // Appends the text form of record to out: "<UTC time> <LEVEL> <message, each {} replaced by the next arg>\n".
    // Missing args are shown as {?}; extra args are ignored.
    // Slow (formatting): meant for the logger thread, never for the hot path. Appending to a string the caller
    // reuses means no new memory is allocated once that string has grown big enough.
    inline void appendLogRecord(const LogRecord& record, std::string& out) {
        out += wallClockToString(record.wallTime);
        out += ' ';
        out += logLevelToString(record.level);
        out += ' ';

        std::size_t argIndex = 0;
        for (const char* p = (record.format != nullptr) ? record.format : ""; *p != '\0'; ++p) {
            if (p[0] != '{' || p[1] != '}') {
                out += *p;   // ordinary character: copy it
                continue;
            }
            if (argIndex < record.argCount) {
                std::visit([&out](const auto& value) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, Side>) {
                        out += sideToString(value);
                    } else {
                        std::format_to(std::back_inserter(out), "{}", value);
                    }
                }, record.args[argIndex]);
            } else {
                out += "{?}";
            }
            ++argIndex;
            ++p;   // skip the '}' as well
        }
        out += '\n';
    }

    // Asynchronous logger. The hot-path thread hands over LogRecords through a lock-free queue; a background
    // "logger thread" turns them into text and writes them to a file.
    // Exactly ONE thread may log through a given Logger (the queue is single-producer).
    class Logger {
    public:
        // Opens (and empties) the log file and starts the logger thread.
        // loggerCore: the core to pin the logger thread to, or -1 for "any core" (it is not latency-critical).
        explicit Logger(const std::string& fileName, int loggerCore = -1, std::size_t queueCapacity = 64 * 1024)
            : queue_(queueCapacity),
              file_(fileName),
              thread_(createAndStartThread(loggerCore, [this] { run(); })) {
            if (!file_) {
                fatal("Logger: could not open the log file");
            }
        }

        // Stops the logger thread after it has written every record that was handed over before.
        ~Logger() {
            running_.store(false, std::memory_order_release);
            thread_.join();
        }

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        // Hands a finished record to the logger thread. Cheap: one queue push.
        // If the queue is full (the logger thread cannot keep up), the record is dropped.
        void push(const LogRecord& record) noexcept {
            if (!queue_.tryPush(record)) [[unlikely]] {
                droppedRecords_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // The function the hot path calls, e.g.
        //     logger.log(LogLevel::INFO, "order {} filled: {} @ {}", orderId, qty, price);
        // Costs a clock read and a queue push; formatting and writing happen later on the logger thread.
        // format must be a string literal: the parameter only accepts a char array, not a pointer or std::string.
        template <std::size_t N, Loggable... Args>
        void log(LogLevel level, const char (&format)[N], const Args&... args) noexcept {
            static_assert(sizeof...(Args) <= LOG_MAX_ARGS, "Logger::log: too many values for one log record");
            LogRecord record;
            record.wallTime = getWallClockNanos();
            record.level = level;
            record.format = format;
            record.argCount = static_cast<std::uint8_t>(sizeof...(Args));
            std::size_t index = 0;
            ((record.args[index++] = toLogArg(args)), ...);   // one assignment per value, in order
            push(record);
        }

        // How many records were dropped because the queue was full.
        [[nodiscard]] std::uint64_t droppedRecords() const noexcept {
            return droppedRecords_.load(std::memory_order_relaxed);
        }

    private:
        // The logger thread's whole life: take records out of the queue, format them, write them.
        void run() {
            std::string text;   // reused for every record (no allocations once it is big enough)
            LogRecord record;
            while (true) {
                // Read the flag BEFORE draining: if we were told to stop, everything pushed before
                // that is already in the queue, so one more drain writes all of it.
                const bool stopping = !running_.load(std::memory_order_acquire);

                bool wroteSomething = false;
                while (queue_.tryPop(record)) {
                    text.clear();
                    appendLogRecord(record, text);
                    file_ << text;   // goes into the stream's buffer; no system call yet
                    wroteSomething = true;
                }
                if (wroteSomething) {
                    file_.flush();   // one system call per batch, not per line
                }
                if (stopping) {
                    break;
                }
                if (!wroteSomething) {
                    // Nothing to do. This thread is not latency-critical, so instead of burning a core
                    // with busy-spinning it sleeps a little (about 1-15 ms on Windows).
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }

        LFQueue<LogRecord> queue_;
        std::ofstream file_;
        std::atomic<bool> running_{true};
        std::atomic<std::uint64_t> droppedRecords_{0};
        std::jthread thread_;   // LAST member: started only after everything it uses has been constructed
    };

}
