#include "core/web_result_cache.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace tmw::core {
namespace {

constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

std::uint64_t fnv(std::uint64_t hash, std::string_view bytes) {
    for (const char c : bytes) {
        hash = (hash ^ static_cast<unsigned char>(c)) * kFnvPrime;
    }
    return hash;
}

std::string hex(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

bool isKey(const std::string& name) {
    return name.size() == 16 && std::all_of(name.begin(), name.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

}  // namespace

std::uint64_t hashPixels(const ImageBgra& image) {
    const auto* data = reinterpret_cast<const char*>(image.pixels.data());
    return fnv(kFnvOffset, std::string_view(data, image.pixels.size()));
}

WebResultCache::WebResultCache(std::filesystem::path directory, std::uint64_t maxBytes)
    : directory_(std::move(directory)), maxBytes_(maxBytes) {}

std::string WebResultCache::makeKey(std::uint64_t pixelHash, int width, int height,
                                    std::string_view language, bool soundEffects,
                                    std::string_view tag) {
    // 各欄位之間用 0 隔開，免得「ab」+「c」和「a」+「bc」一樣
    std::uint64_t hash = fnv(kFnvOffset, hex(pixelHash));
    for (const std::string& part :
         {std::to_string(width), std::to_string(height), std::string(language),
          std::string(soundEffects ? "1" : "0"), std::string(tag)}) {
        hash = fnv(hash, std::string_view("\0", 1));
        hash = fnv(hash, part);
    }
    return hex(hash);
}

std::filesystem::path WebResultCache::pathOf(const std::string& key) const {
    return directory_ / (key + ".json");
}

void WebResultCache::loadIndex() {
    if (loaded_) {
        return;
    }
    loaded_ = true;
    std::error_code error;
    for (const auto& file : std::filesystem::directory_iterator(directory_, error)) {
        const std::string name = file.path().stem().string();
        if (file.path().extension() != ".json" || !isKey(name)) {
            continue;  // 寫到一半的暫存檔之類的
        }
        std::error_code ignored;
        const std::uint64_t size = file.file_size(ignored);
        entries_[name] = Entry{size, file.last_write_time(ignored)};
        totalBytes_ += size;
    }
}

std::optional<std::string> WebResultCache::find(const std::string& key) {
    loadIndex();
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        return std::nullopt;
    }
    std::ifstream in(pathOf(key), std::ios::binary);
    std::string reply((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (reply.empty()) {
        totalBytes_ -= found->second.size;  // 檔案被刪了或壞了
        entries_.erase(found);
        return std::nullopt;
    }
    // 記下「剛用過」：容量不夠時最後才刪
    std::error_code ignored;
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(pathOf(key), now, ignored);
    found->second.used = now;
    return reply;
}

void WebResultCache::store(const std::string& key, const std::string& reply) {
    loadIndex();
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    // 先寫暫存檔再改名：寫到一半當掉不會留下壞掉的結果
    const std::filesystem::path temporary = directory_ / (key + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reply.data(), static_cast<std::streamsize>(reply.size()));
        if (!out.good()) {
            out.close();
            std::filesystem::remove(temporary, error);
            return;
        }
    }
    std::filesystem::rename(temporary, pathOf(key), error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return;
    }
    if (const auto old = entries_.find(key); old != entries_.end()) {
        totalBytes_ -= old->second.size;
    }
    entries_[key] = Entry{reply.size(), std::filesystem::file_time_type::clock::now()};
    totalBytes_ += reply.size();
    evict();
}

void WebResultCache::evict() {
    if (totalBytes_ <= maxBytes_) {
        return;
    }
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> byAge;
    byAge.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        byAge.emplace_back(entry.used, key);
    }
    std::sort(byAge.begin(), byAge.end());
    for (const auto& [used, key] : byAge) {
        if (totalBytes_ <= maxBytes_) {
            break;
        }
        std::error_code ignored;
        std::filesystem::remove(pathOf(key), ignored);
        totalBytes_ -= entries_[key].size;
        entries_.erase(key);
    }
}

}  // namespace tmw::core
