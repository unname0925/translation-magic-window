#include "core/web_protocol.h"

#include <gtest/gtest.h>

#include <array>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace tmw::core {
namespace {

std::vector<std::uint8_t> bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

TEST(WebFrameTest, LengthIsFourLittleEndianBytes) {
    const std::string framed = frameMessage("{}");
    ASSERT_EQ(framed.size(), 6u);
    EXPECT_EQ(framed.substr(0, 4), std::string("\x02\x00\x00\x00", 4));
    EXPECT_EQ(framed.substr(4), "{}");
}

TEST(WebFrameTest, ReadsTheLengthBack) {
    const std::string framed = frameMessage(std::string(70000, 'x'));
    std::array<std::uint8_t, 4> header{};
    for (int i = 0; i < 4; ++i) {
        header[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(framed[static_cast<std::size_t>(i)]);
    }
    EXPECT_EQ(frameLength(header), 70000u);
}

TEST(Base64Test, MatchesTheStandardExamples) {
    // RFC 4648 第 10 節
    EXPECT_EQ(base64Encode(bytes("")), "");
    EXPECT_EQ(base64Encode(bytes("f")), "Zg==");
    EXPECT_EQ(base64Encode(bytes("fo")), "Zm8=");
    EXPECT_EQ(base64Encode(bytes("foo")), "Zm9v");
    EXPECT_EQ(base64Encode(bytes("foobar")), "Zm9vYmFy");
}

TEST(Base64Test, DecodesWhatItEncodes) {
    std::vector<std::uint8_t> all(256);
    for (int i = 0; i < 256; ++i) {
        all[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    }
    for (std::size_t length : {0u, 1u, 2u, 3u, 255u, 256u}) {
        const std::vector<std::uint8_t> part(all.begin(),
                                             all.begin() + static_cast<std::ptrdiff_t>(length));
        EXPECT_EQ(base64Decode(base64Encode(part)), part) << length;
    }
}

TEST(Base64Test, RejectsBrokenText) {
    EXPECT_FALSE(base64Decode("Zg="));   // 長度不是 4 的倍數
    EXPECT_FALSE(base64Decode("Z!=="));  // 不是 base64 的字元
    EXPECT_FALSE(base64Decode("Zg==Zg==")) << "「=」只能在最後";
    EXPECT_FALSE(base64Decode("Z=g="));
}

TEST(ParseWebRequestTest, Hello) {
    EXPECT_EQ(parseWebRequest(R"({"type":"hello","protocol":1})").type, WebRequest::Type::Hello);
}

TEST(ParseWebRequestTest, TranslateTurnsRgbaIntoBgra) {
    // 2×1：紅、藍
    const std::vector<std::uint8_t> rgba{255, 0, 0, 255, 0, 0, 255, 255};
    const std::string json =
        nlohmann::json{
            {"type", "translate"},          {"id", "abc"},     {"width", 2}, {"height", 1},
            {"pixels", base64Encode(rgba)}, {"language", "ja"}}
            .dump();
    const WebRequest request = parseWebRequest(json);
    ASSERT_EQ(request.type, WebRequest::Type::Translate) << request.error;
    EXPECT_EQ(request.id, "abc");
    EXPECT_EQ(request.language, "ja");
    ASSERT_EQ(request.image.width, 2);
    ASSERT_EQ(request.image.height, 1);
    EXPECT_EQ(request.image.pixels, (std::vector<std::uint8_t>{0, 0, 255, 255, 255, 0, 0, 255}));
}

TEST(ParseWebRequestTest, PixelsMustMatchTheSize) {
    const std::string json = nlohmann::json{
        {"type", "translate"},
        {"id", "abc"},
        {"width", 2},
        {"height", 2},
        {"pixels", base64Encode(std::vector<std::uint8_t>(
                       4, 0))}}.dump();
    const WebRequest request = parseWebRequest(json);
    EXPECT_EQ(request.type, WebRequest::Type::Invalid);
    EXPECT_EQ(request.id, "abc") << "錯誤要能對回是哪一張圖";
}

TEST(ParseWebRequestTest, RejectsHugeImages) {
    const std::string json = nlohmann::json{{"type", "translate"},
                                            {"id", "x"},
                                            {"width", 100000},
                                            {"height", 100000},
                                            {"pixels", ""}}
                                 .dump();
    EXPECT_EQ(parseWebRequest(json).type, WebRequest::Type::Invalid);
}

TEST(ParseWebRequestTest, EnginesAndSetEngine) {
    EXPECT_EQ(parseWebRequest(R"({"type":"engines"})").type, WebRequest::Type::Engines);
    const WebRequest set = parseWebRequest(R"({"type":"set-engine","index":2})");
    EXPECT_EQ(set.type, WebRequest::Type::SetEngine);
    EXPECT_EQ(set.index, 2);
    EXPECT_EQ(parseWebRequest(R"({"type":"set-engine"})").type, WebRequest::Type::Invalid);
    EXPECT_EQ(parseWebRequest(R"({"type":"set-engine","index":-1})").type,
              WebRequest::Type::Invalid);
}

TEST(WebEnginesReplyTest, ListsLabelsWithTheirIndex) {
    const std::vector<std::string> labels{"hy-mt2（Ollama）", "Google 翻譯（免費）"};
    const nlohmann::json reply = nlohmann::json::parse(webEnginesReply(labels, "hy-mt2（Ollama）"));
    EXPECT_EQ(reply["type"], "engines");
    ASSERT_EQ(reply["engines"].size(), 2u);
    EXPECT_EQ(reply["engines"][1]["index"], 1);
    EXPECT_EQ(reply["engines"][1]["label"], "Google 翻譯（免費）");
    EXPECT_EQ(reply["current"], "hy-mt2（Ollama）");
}

TEST(ParseWebRequestTest, RejectsGarbage) {
    EXPECT_EQ(parseWebRequest("not json").type, WebRequest::Type::Invalid);
    EXPECT_EQ(parseWebRequest(R"({"type":"explode"})").type, WebRequest::Type::Invalid);
    EXPECT_EQ(parseWebRequest(R"({"type":"cancel"})").type, WebRequest::Type::Invalid);
    EXPECT_EQ(parseWebRequest(R"({"type":"cancel","id":"a"})").type, WebRequest::Type::Cancel);
}

OverlayItem item(int left, std::string text) {
    OverlayItem out;
    out.rect = RectI{left, 10, left + 40, 110};
    out.text = std::move(text);
    out.vertical = true;
    out.background = Rgba{255, 255, 255};
    out.foreground = Rgba{0, 0, 0};
    out.outline = Rgba{0x12, 0xab, 0xff};
    out.ruby = {OverlayRuby{0, 2, "ふり"}};
    out.size = TextSize::Large;
    out.lineThickness = 24;
    return out;
}

TEST(WebResultReplyTest, DescribesEachItem) {
    const std::array<OverlayItem, 1> items{item(5, "你好")};
    const nlohmann::json reply = nlohmann::json::parse(webResultReply("p1", items, "", nullptr));
    EXPECT_EQ(reply["type"], "result");
    EXPECT_EQ(reply["id"], "p1");
    ASSERT_EQ(reply["items"].size(), 1u);
    const nlohmann::json& first = reply["items"][0];
    EXPECT_EQ(first["rect"], (nlohmann::json{5, 10, 45, 110}));
    EXPECT_EQ(first["text"], "你好");
    EXPECT_EQ(first["vertical"], true);
    EXPECT_EQ(first["background"], "#ffffff");
    EXPECT_EQ(first["foreground"], "#000000");
    EXPECT_EQ(first["outline"], "#12abff");
    EXPECT_EQ(first["size"], "large");
    EXPECT_EQ(first["lineThickness"], 24);
    EXPECT_EQ(first["ruby"][0]["text"], "ふり");
    EXPECT_FALSE(first.contains("patch")) << "沒有背景修補時是純色";
}

TEST(WebResultReplyTest, DropsTheBiggestPatchesWhenTooLarge) {
    std::array<OverlayItem, 3> items{item(0, "一"), item(50, "二"), item(100, "三")};
    for (OverlayItem& entry : items) {
        entry.patch = ImageBgra(4, 4);
    }
    // 假的 PNG：大小依位置不同，第二張最大
    int calls = 0;
    const PngEncoder sized = [&calls](const ImageBgra&) {
        const std::array<std::size_t, 3> sizes{3000, 9000, 3000};
        return std::vector<std::uint8_t>(sizes[static_cast<std::size_t>(calls++)], 7);
    };
    const std::string reply = webResultReply("p", items, "", sized, 12000);
    EXPECT_LE(reply.size(), 12000u);
    const nlohmann::json parsed = nlohmann::json::parse(reply);
    EXPECT_EQ(parsed["patchesDropped"], 1);
    EXPECT_TRUE(parsed["items"][0].contains("patch"));
    EXPECT_FALSE(parsed["items"][1].contains("patch")) << "先丟最大的";
    EXPECT_TRUE(parsed["items"][2].contains("patch"));
}

TEST(WebResultReplyTest, InvalidUtf8DoesNotBreakTheReply) {
    const std::array<OverlayItem, 1> items{item(0, std::string("ok\xff", 3))};
    const std::string reply = webResultReply("p", items, "", nullptr);
    EXPECT_TRUE(nlohmann::json::accept(reply));
}

}  // namespace
}  // namespace tmw::core
