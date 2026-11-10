#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "Common/Logger.h"
#include "Common/Thread.h"
#include "Test.h"

using namespace Common;

namespace {

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream file(path);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

}

TEST(threadCanBePinned) {
    const int cores = static_cast<int>(std::thread::hardware_concurrency());
    bool pinned = false;
    std::jthread worker = createAndStartThread(cores - 1, [&pinned] { pinned = true; });
    worker.join();
    CHECK(pinned);

    CHECK(!setThreadCore(cores + 64));
    CHECK(!setThreadCore(-1));
}

TEST(logRecordFormatting) {
    LogRecord record;
    record.wallTime = 0;
    record.format = "order {} {} {} @ {} ({})";
    record.args = {toLogArg(42), toLogArg(Side::BUY), toLogArg(100u), toLogArg(1.5)};
    record.argCount = 4;
    record.level = LogLevel::WARN;

    std::string text;
    appendLogRecord(record, text);
    CHECK(text == "1970-01-01 00:00:00.000000000 WARN order 42 BUY 100 @ 1.5 ({?})\n");
}

TEST(toLogArgPicksTheRightAlternative) {
    CHECK(std::holds_alternative<char>(toLogArg('x')));
    CHECK(std::holds_alternative<std::int64_t>(toLogArg(-3)));
    CHECK(std::holds_alternative<std::uint64_t>(toLogArg(Qty{3})));
    CHECK(std::holds_alternative<double>(toLogArg(2.5f)));
    CHECK(std::holds_alternative<Side>(toLogArg(Side::SELL)));
}

TEST(loggerWritesEveryRecordToItsFile) {
    const auto path = std::filesystem::temp_directory_path() / "lowlatency_logger_test.txt";
    {
        Logger logger(path.string());
        for (int i = 0; i < 1000; ++i) {
            logger.log(LogLevel::INFO, "order {} {} qty {}", i, Side::SELL, Qty{10});
        }
        CHECK(logger.droppedRecords() == 0);
    }

    const std::string text = readFile(path);
    std::size_t lines = 0;
    for (char c : text) {
        lines += (c == '\n') ? 1 : 0;
    }
    CHECK(lines == 1000);
    CHECK(text.find("INFO order 0 SELL qty 10\n") != std::string::npos);
    CHECK(text.find("INFO order 999 SELL qty 10\n") != std::string::npos);
    std::filesystem::remove(path);
}
