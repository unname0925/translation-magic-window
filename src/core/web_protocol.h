// 網頁漫畫整頁翻譯：瀏覽器擴充功能和主程式之間的訊息（docs/proposal-speed-and-web-manga.md 第二部分）。
//
// 擴充功能 ⇄ tmw_web_host.exe（Chrome／Edge 的 Native Messaging）⇄ 具名管道 ⇄ 主程式。
// 兩段用同一種封包：4 位元組的長度（little-endian，Windows 上就是 Native Messaging 的「原生位元組順序」）
// 加上 UTF-8 的 JSON。轉送程式不必看懂內容，原封不動搬過去。
//
// 擴充功能 → 主程式：
//   {"type":"hello","protocol":1}
//   {"type":"translate","id":"…","width":W,"height":H,"pixels":"<base64 RGBA>","language":"auto"}
//     pixels 是瀏覽器 getImageData 的 RGBA（每列緊密排列）。id 由擴充功能決定（圖片內容的雜湊），原樣帶回
//   {"type":"cancel","id":"…"}
// 主程式 → 擴充功能：
//   {"type":"hello","protocol":1,"version":"0.1.0"}
//   {"type":"result","id":"…","items":[…],"error":"","patchesDropped":0}
//     items 的每一個：rect [left, top, right, bottom]（圖片的像素座標）、text、vertical、
//     foreground／background／outline（"#rrggbb"，沒有描邊時沒有 outline）、size（"small"|"normal"|"large"）、
//     lineThickness（原文一行的粗細，譯文的字不比它大；0 = 不知道）、ruby [{start, length, text}]、
//     patch（背景修補的小圖，base64 PNG；沒有時是純色背景）
//   {"type":"error","id":"…","message":"…"}
//
// Native Messaging 規定主程式送給擴充功能的一則訊息最多 1 MB（反方向 64 MiB），所以結果太大時
// 先丟掉最大的背景修補圖（那幾段改用純色背景），patchesDropped 記丟了幾張。
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/image.h"
#include "core/overlay_item.h"

namespace tmw::core {

inline constexpr int kWebProtocolVersion = 1;
// 主程式 → 擴充功能一則訊息的上限（Native Messaging 是 1 MB；留一點餘裕）
inline constexpr std::size_t kWebMaxReplyBytes = 1000 * 1000;
// 擴充功能 → 主程式一則訊息的上限（Native Messaging 是 64 MiB）
inline constexpr std::size_t kWebMaxRequestBytes = 64u * 1024 * 1024;
// 一張圖最多幾個像素（24 MP：例如 2000×12000 的條漫）。再大的要擴充功能切開再送
inline constexpr std::int64_t kWebMaxPixels = 24'000'000;

// 封包：4 位元組長度（little-endian）＋內容
std::string frameMessage(std::string_view json);
// 從 4 個位元組讀出長度
std::uint32_t frameLength(std::span<const std::uint8_t, 4> header);

std::string base64Encode(std::span<const std::uint8_t> bytes);
// 不是合法的 base64 時回傳 nullopt（忽略不了的字元、長度不對）
std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text);

struct WebRequest {
    enum class Type { Hello, Translate, Cancel, Invalid };
    Type type = Type::Invalid;
    std::string id;
    ImageBgra image;        // Translate：RGBA 已經轉成 BGRA
    std::string language;   // Translate："auto"、"ja"…；沒給時是空字串
    std::string error;      // Invalid：哪裡不對（回給擴充功能看）
};

// 解析擴充功能送來的一則訊息（JSON 本體，不含長度）。格式不對時 type 是 Invalid
WebRequest parseWebRequest(std::string_view json);

std::string webHelloReply(std::string_view version);
std::string webErrorReply(std::string_view id, std::string_view message);

// 背景修補的小圖編成 PNG（core 不認得影像格式，由呼叫端提供）
using PngEncoder = std::function<std::vector<std::uint8_t>(const ImageBgra&)>;

// 翻譯結果。編好之後超過 maxBytes 時，從最大的背景修補圖開始丟，直到放得下
std::string webResultReply(std::string_view id, std::span<const OverlayItem> items,
                           std::string_view error, const PngEncoder& encodePng,
                           std::size_t maxBytes = kWebMaxReplyBytes);

}  // namespace tmw::core
