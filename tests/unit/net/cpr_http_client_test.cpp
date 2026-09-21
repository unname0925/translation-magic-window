// 連本機服務要用 127.0.0.1 而不是 localhost（M0-12：每個請求差約 2 秒）。
#include "net/cpr_http_client.h"

#include <gtest/gtest.h>

#include <string>

namespace tmw::net {
namespace {

TEST(PreferIpv4LoopbackTest, ReplacesTheHostName) {
    EXPECT_EQ(preferIpv4Loopback("http://localhost:11434/v1/chat/completions"),
              "http://127.0.0.1:11434/v1/chat/completions");
    EXPECT_EQ(preferIpv4Loopback("http://localhost/v1"), "http://127.0.0.1/v1");
    EXPECT_EQ(preferIpv4Loopback("http://localhost"), "http://127.0.0.1");
}

TEST(PreferIpv4LoopbackTest, LeavesEverythingElseAlone) {
    EXPECT_EQ(preferIpv4Loopback("http://127.0.0.1:11434/v1"), "http://127.0.0.1:11434/v1");
    EXPECT_EQ(preferIpv4Loopback("https://api.openai.com/v1"), "https://api.openai.com/v1");
    EXPECT_EQ(preferIpv4Loopback("http://localhost.example.com/v1"),
              "http://localhost.example.com/v1")
        << "只有主機名稱剛好是 localhost 才換";
    EXPECT_EQ(preferIpv4Loopback("http://mylocalhost/v1"), "http://mylocalhost/v1");
    EXPECT_EQ(preferIpv4Loopback(""), "");
    EXPECT_EQ(preferIpv4Loopback("localhost:11434"), "localhost:11434") << "沒有通訊協定就不動它";
}

}  // namespace
}  // namespace tmw::net
