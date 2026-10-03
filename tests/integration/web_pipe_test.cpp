// 網頁漫畫：主程式和 tmw_web_host.exe 之間的具名管道（platform/web_pipe）。
#include "platform/web_pipe.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tmw::platform {
namespace {

using namespace std::chrono_literals;

// 每個測試用自己的名稱，不會撞到正在執行的主程式
std::wstring testPipeName() {
    return webPipeName() + L".test." + std::to_wstring(GetCurrentProcessId()) + L"." +
           std::to_wstring(GetTickCount64());
}

struct Inbox {
    std::mutex mutex;
    std::condition_variable arrived;
    std::vector<std::pair<int, std::string>> messages;
    std::vector<int> disconnected;

    bool waitFor(std::size_t count) {
        std::unique_lock lock(mutex);
        return arrived.wait_for(lock, 5s, [&] { return messages.size() >= count; });
    }
    bool waitForDisconnect() {
        std::unique_lock lock(mutex);
        return arrived.wait_for(lock, 5s, [&] { return !disconnected.empty(); });
    }
};

class WebPipeTest : public ::testing::Test {
protected:
    WebPipeTest()
        : name_(testPipeName()),
          server_(
              name_,
              [this](int connection, std::string message) {
                  {
                      const std::lock_guard lock(inbox_.mutex);
                      inbox_.messages.emplace_back(connection, std::move(message));
                  }
                  inbox_.arrived.notify_all();
              },
              [this](int connection) {
                  {
                      const std::lock_guard lock(inbox_.mutex);
                      inbox_.disconnected.push_back(connection);
                  }
                  inbox_.arrived.notify_all();
              }) {}

    std::wstring name_;
    Inbox inbox_;
    WebPipeServer server_;
};

TEST_F(WebPipeTest, MessagesGoBothWays) {
    ASSERT_TRUE(server_.start());
    const HANDLE client = connectWebPipe(name_, 2000);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);

    ASSERT_TRUE(writeWebFrame(client, R"({"type":"hello"})"));
    ASSERT_TRUE(inbox_.waitFor(1));
    const int connection = inbox_.messages[0].first;
    EXPECT_EQ(inbox_.messages[0].second, R"({"type":"hello"})");

    ASSERT_TRUE(server_.send(connection, "回覆"));
    const std::optional<std::string> reply = readWebFrame(client, nullptr, 1 << 20);
    ASSERT_TRUE(reply);
    EXPECT_EQ(*reply, "回覆");
    CloseHandle(client);
}

TEST_F(WebPipeTest, LargeMessagesArriveWhole) {
    // 一張 1600×2400 的圖編成 base64 大約 20 MB
    ASSERT_TRUE(server_.start());
    const HANDLE client = connectWebPipe(name_, 2000);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    std::string big(20 * 1024 * 1024, 'x');
    big[12345] = 'y';
    ASSERT_TRUE(writeWebFrame(client, big));
    ASSERT_TRUE(inbox_.waitFor(1));
    EXPECT_EQ(inbox_.messages[0].second.size(), big.size());
    EXPECT_EQ(inbox_.messages[0].second, big);
    CloseHandle(client);
}

TEST_F(WebPipeTest, ClosingTheClientIsReported) {
    ASSERT_TRUE(server_.start());
    const HANDLE client = connectWebPipe(name_, 2000);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    ASSERT_TRUE(writeWebFrame(client, "x"));
    ASSERT_TRUE(inbox_.waitFor(1));
    CloseHandle(client);
    ASSERT_TRUE(inbox_.waitForDisconnect());
    EXPECT_FALSE(server_.send(inbox_.messages[0].first, "too late"));
}

TEST_F(WebPipeTest, SeveralClientsAtOnce) {
    ASSERT_TRUE(server_.start());
    const HANDLE first = connectWebPipe(name_, 2000);
    const HANDLE second = connectWebPipe(name_, 2000);
    ASSERT_NE(first, INVALID_HANDLE_VALUE);
    ASSERT_NE(second, INVALID_HANDLE_VALUE);
    ASSERT_TRUE(writeWebFrame(first, "1"));
    ASSERT_TRUE(writeWebFrame(second, "2"));
    ASSERT_TRUE(inbox_.waitFor(2));
    EXPECT_NE(inbox_.messages[0].first, inbox_.messages[1].first) << "兩條連線分得開";
    CloseHandle(first);
    CloseHandle(second);
}

TEST_F(WebPipeTest, OnlyOneServerPerName) {
    ASSERT_TRUE(server_.start());
    WebPipeServer other(name_, nullptr, nullptr);
    EXPECT_FALSE(other.start()) << "另一個主程式（或別人搶先）開了同名管道";
}

TEST_F(WebPipeTest, ConnectingWithoutAServerTimesOut) {
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(connectWebPipe(name_, 300), INVALID_HANDLE_VALUE);
    EXPECT_GE(std::chrono::steady_clock::now() - start, 250ms);
}

TEST_F(WebPipeTest, StoppingWithAClientConnectedDoesNotHang) {
    ASSERT_TRUE(server_.start());
    const HANDLE client = connectWebPipe(name_, 2000);
    ASSERT_NE(client, INVALID_HANDLE_VALUE);
    ASSERT_TRUE(writeWebFrame(client, "x"));
    ASSERT_TRUE(inbox_.waitFor(1));
    server_.stop();  // 解構時也會呼叫；這裡不能卡住
    CloseHandle(client);
}

}  // namespace
}  // namespace tmw::platform
