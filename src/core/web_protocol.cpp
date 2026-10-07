#include "core/web_protocol.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <numeric>
#include <utility>

namespace tmw::core {
namespace {

constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

std::string hexColor(Rgba color) {
    std::array<char, 8> text{};
    std::snprintf(text.data(), text.size(), "#%02x%02x%02x", color.r, color.g, color.b);
    return text.data();
}

std::string_view sizeName(TextSize size) {
    switch (size) {
        case TextSize::Small:
            return "small";
        case TextSize::Large:
            return "large";
        case TextSize::Normal:
            break;
    }
    return "normal";
}

WebRequest invalid(std::string id, std::string error) {
    WebRequest out;
    out.type = WebRequest::Type::Invalid;
    out.id = std::move(id);
    out.error = std::move(error);
    return out;
}

}  // namespace

std::string frameMessage(std::string_view json) {
    const auto length = static_cast<std::uint32_t>(json.size());
    std::string out(4, '\0');
    for (int i = 0; i < 4; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<char>((length >> (8 * i)) & 0xFF);
    }
    out.append(json);
    return out;
}

std::uint32_t frameLength(std::span<const std::uint8_t, 4> header) {
    return static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8) |
           (static_cast<std::uint32_t>(header[2]) << 16) |
           (static_cast<std::uint32_t>(header[3]) << 24);
}

std::string base64Encode(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += kBase64Alphabet[(n >> 18) & 63];
        out += kBase64Alphabet[(n >> 12) & 63];
        out += kBase64Alphabet[(n >> 6) & 63];
        out += kBase64Alphabet[n & 63];
    }
    if (const std::size_t rest = bytes.size() - i; rest > 0) {
        const std::uint32_t n = (bytes[i] << 16) | (rest == 2 ? bytes[i + 1] << 8 : 0);
        out += kBase64Alphabet[(n >> 18) & 63];
        out += kBase64Alphabet[(n >> 12) & 63];
        out += rest == 2 ? kBase64Alphabet[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text) {
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        int padding = 0;
        std::uint32_t n = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const char c = text[i + k];
            int value = 0;
            if (c == '=' && last && k >= 2) {
                ++padding;
            } else if (padding > 0 || (value = base64Value(c)) < 0) {
                return std::nullopt;  // 「=」後面又有字，或不是 base64 的字元
            }
            n = (n << 6) | static_cast<std::uint32_t>(value);
        }
        out.push_back(static_cast<std::uint8_t>(n >> 16));
        if (padding < 2) {
            out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
        }
        if (padding < 1) {
            out.push_back(static_cast<std::uint8_t>(n & 0xFF));
        }
    }
    return out;
}

WebRequest parseWebRequest(std::string_view json) {
    const nlohmann::json message = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) {
        return invalid({}, "not a message");
    }
    const std::string type = message["type"].get<std::string>();
    std::string id;
    if (message.contains("id") && message["id"].is_string()) {
        id = message["id"].get<std::string>();
    }
    if (type == "hello") {
        WebRequest out;
        out.type = WebRequest::Type::Hello;
        return out;
    }
    if (type == "engines") {
        WebRequest out;
        out.type = WebRequest::Type::Engines;
        return out;
    }
    if (type == "set-engine") {
        if (!message.contains("index") || !message["index"].is_number_integer() ||
            message["index"].get<int>() < 0) {
            return invalid({}, "set-engine without index");
        }
        WebRequest out;
        out.type = WebRequest::Type::SetEngine;
        out.index = message["index"].get<int>();
        return out;
    }
    if (type == "cancel") {
        if (id.empty()) {
            return invalid({}, "cancel without id");
        }
        WebRequest out;
        out.type = WebRequest::Type::Cancel;
        out.id = std::move(id);
        return out;
    }
    if (type != "translate") {
        return invalid(std::move(id), "unknown type: " + type);
    }
    if (id.empty()) {
        return invalid({}, "translate without id");
    }
    const auto dimension = [&message](const char* key) -> std::int64_t {
        return message.contains(key) && message[key].is_number_integer()
                   ? message[key].get<std::int64_t>()
                   : 0;
    };
    const std::int64_t width = dimension("width");
    const std::int64_t height = dimension("height");
    if (width <= 0 || height <= 0 || width * height > kWebMaxPixels) {
        return invalid(std::move(id), "bad image size");
    }
    if (!message.contains("pixels") || !message["pixels"].is_string()) {
        return invalid(std::move(id), "no pixels");
    }
    std::optional<std::vector<std::uint8_t>> pixels =
        base64Decode(message["pixels"].get_ref<const std::string&>());
    if (!pixels || pixels->size() != static_cast<std::size_t>(width * height * 4)) {
        return invalid(std::move(id), "pixels do not match the size");
    }
    WebRequest out;
    out.type = WebRequest::Type::Translate;
    out.id = std::move(id);
    out.image.width = static_cast<int>(width);
    out.image.height = static_cast<int>(height);
    out.image.pixels = std::move(*pixels);
    // RGBA → BGRA：交換 R 和 B
    for (std::size_t i = 0; i + 3 < out.image.pixels.size(); i += 4) {
        std::swap(out.image.pixels[i], out.image.pixels[i + 2]);
    }
    if (message.contains("language") && message["language"].is_string()) {
        out.language = message["language"].get<std::string>();
    }
    if (message.contains("site") && message["site"].is_string()) {
        out.site = message["site"].get<std::string>();
    }
    if (message.contains("soundEffects") && message["soundEffects"].is_boolean()) {
        out.soundEffects = message["soundEffects"].get<bool>();
    }
    return out;
}

std::string webHelloReply(std::string_view version) {
    return nlohmann::json{
        {"type", "hello"}, {"protocol", kWebProtocolVersion}, {"version", std::string(version)}}
        .dump();
}

std::string webEnginesReply(std::span<const std::string> labels, std::string_view current) {
    nlohmann::json list = nlohmann::json::array();
    for (std::size_t i = 0; i < labels.size(); ++i) {
        list.push_back({{"index", i}, {"label", labels[i]}});
    }
    return nlohmann::json{
        {"type", "engines"}, {"engines", std::move(list)}, {"current", std::string(current)}}
        .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string webErrorReply(std::string_view id, std::string_view message) {
    return nlohmann::json{
        {"type", "error"}, {"id", std::string(id)}, {"message", std::string(message)}}
        .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string webReplyWithId(std::string_view reply, std::string_view id) {
    nlohmann::json parsed = nlohmann::json::parse(reply, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || parsed.value("type", "") != "result") {
        return {};
    }
    parsed["id"] = std::string(id);
    return parsed.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string webResultReply(std::string_view id, std::span<const OverlayItem> items,
                           std::string_view error, const PngEncoder& encodePng,
                           std::size_t maxBytes, std::string_view notice) {
    nlohmann::json list = nlohmann::json::array();
    std::vector<std::string> patches(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        const OverlayItem& item = items[i];
        nlohmann::json entry{
            {"rect", {item.rect.left, item.rect.top, item.rect.right, item.rect.bottom}},
            {"text", item.text},
            {"vertical", item.vertical},
            {"foreground", hexColor(item.foreground)},
            {"background", hexColor(item.background)},
            {"size", sizeName(item.size)},
            {"lineThickness", item.lineThickness},
        };
        if (item.outline) {
            entry["outline"] = hexColor(*item.outline);
        }
        if (item.soundEffect) {
            entry["soundEffect"] = true;
        }
        nlohmann::json ruby = nlohmann::json::array();
        for (const OverlayRuby& annotation : item.ruby) {
            ruby.push_back({{"start", annotation.start},
                            {"length", annotation.length},
                            {"text", annotation.text}});
        }
        entry["ruby"] = std::move(ruby);
        if (!item.patch.empty() && encodePng) {
            patches[i] = base64Encode(encodePng(item.patch));
        }
        list.push_back(std::move(entry));
    }

    const auto build = [&](int dropped) {
        nlohmann::json reply{{"type", "result"},
                             {"id", std::string(id)},
                             {"items", list},
                             {"error", std::string(error)},
                             {"notice", std::string(notice)},
                             {"patchesDropped", dropped}};
        for (std::size_t i = 0; i < patches.size(); ++i) {
            if (!patches[i].empty()) {
                reply["items"][i]["patch"] = patches[i];
            }
        }
        // OCR 讀到的字可能不是合法的 UTF-8：換成替代字元，不要讓整則訊息失敗
        return reply.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    };

    // 太大時從最大的背景修補圖開始丟
    std::vector<std::size_t> bySize(patches.size());
    std::iota(bySize.begin(), bySize.end(), std::size_t{0});
    std::stable_sort(bySize.begin(), bySize.end(), [&patches](std::size_t a, std::size_t b) {
        return patches[a].size() > patches[b].size();
    });
    int dropped = 0;
    std::string reply = build(dropped);
    for (const std::size_t index : bySize) {
        if (reply.size() <= maxBytes || patches[index].empty()) {
            break;
        }
        patches[index].clear();
        ++dropped;
        reply = build(dropped);
    }
    return reply;
}

}  // namespace tmw::core
