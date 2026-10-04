#include "app/web_service.h"

#include <algorithm>
#include <utility>

#include "platform/png_file.h"

namespace tmw::app {
namespace {

// 管道的執行緒交回 UI 執行緒的工作：WebService 已經不在時什麼都不做
std::function<void()> guarded(std::weak_ptr<bool> alive, std::function<void()> work) {
    return [alive = std::move(alive), work = std::move(work)] {
        if (alive.lock()) {
            work();
        }
    };
}

}  // namespace

WebService::WebService(std::wstring pipeName, Callbacks callbacks)
    : callbacks_(std::move(callbacks)),
      server_(
          std::move(pipeName),
          [this](int connection, std::string message) {
              onMessage(connection, std::move(message));
          },
          [this](int connection) { onDisconnect(connection); }),
      alive_(std::make_shared<bool>(true)) {}

WebService::~WebService() {
    alive_.reset();  // 還沒執行的 post 不再碰這個物件
    server_.stop();
}

bool WebService::start() {
    return server_.start();
}

void WebService::onMessage(int connection, std::string message) {
    // 管道的執行緒：解析（包括把整張圖 base64 解碼）在這裡做
    auto request = std::make_shared<core::WebRequest>(core::parseWebRequest(message));
    message.clear();
    switch (request->type) {
        case core::WebRequest::Type::Hello:
            server_.send(connection, core::webHelloReply(TMW_VERSION));
            return;
        case core::WebRequest::Type::Invalid:
            server_.send(connection, core::webErrorReply(request->id, request->error));
            return;
        case core::WebRequest::Type::Engines:
            callbacks_.post(guarded(alive_, [this, connection] { replyEngines(connection); }));
            return;
        case core::WebRequest::Type::SetEngine:
            callbacks_.post(guarded(alive_, [this, connection, index = request->index] {
                if (callbacks_.setEngine && !callbacks_.setEngine(index)) {
                    server_.send(connection, core::webErrorReply("", "no-such-engine"));
                    return;
                }
                replyEngines(connection);
            }));
            return;
        case core::WebRequest::Type::Cancel:
            callbacks_.post(
                guarded(alive_, [this, connection, id = request->id] { cancel(connection, id); }));
            return;
        case core::WebRequest::Type::Translate:
            callbacks_.post(
                guarded(alive_, [this, connection, request] { enqueue(connection, request); }));
            return;
    }
}

void WebService::replyEngines(int connection) {
    if (!callbacks_.engines) {
        server_.send(connection, core::webEnginesReply({}, ""));
        return;
    }
    const auto [labels, current] = callbacks_.engines();
    server_.send(connection, core::webEnginesReply(labels, current));
}

void WebService::onDisconnect(int connection) {
    callbacks_.post(guarded(alive_, [this, connection] { dropConnection(connection); }));
}

void WebService::enqueue(int connection, std::shared_ptr<core::WebRequest> request) {
    // 同一張圖重複送來（例如擴充功能重新整理）：只留最新的一次
    std::erase_if(queue_, [&](const Pending& pending) {
        return pending.connection == connection && pending.id == request->id;
    });
    std::string id = request->id;
    queue_.push_back(Pending{connection, std::move(id), std::move(request)});
    pump();
}

void WebService::cancel(int connection, const std::string& id) {
    std::erase_if(queue_, [&](const Pending& pending) {
        return pending.connection == connection && pending.id == id;
    });
    for (auto it = running_.begin(); it != running_.end();) {
        if (it->second.connection == connection && it->second.id == id) {
            callbacks_.cancel(it->first);
            it = running_.erase(it);
        } else {
            ++it;
        }
    }
    pump();
}

void WebService::dropConnection(int connection) {
    std::erase_if(
        queue_, [connection](const Pending& pending) { return pending.connection == connection; });
    for (auto it = running_.begin(); it != running_.end();) {
        if (it->second.connection == connection) {
            callbacks_.cancel(it->first);
            it = running_.erase(it);
        } else {
            ++it;
        }
    }
    pump();
}

void WebService::pump() {
    while (running_.size() < kInFlight && !queue_.empty()) {
        submitNext();
    }
}

void WebService::submitNext() {
    Pending next = std::move(queue_.front());
    queue_.pop_front();

    core::PipelineJob job;
    job.generation = ++nextGeneration_;
    job.lens = kLensId;
    job.region = core::RectI{0, 0, next.request->image.width, next.request->image.height};
    job.frame = std::move(next.request->image);
    job.manga = true;
    if (callbacks_.configure) {
        callbacks_.configure(job);
    }
    if (!next.request->language.empty() && next.request->language != "auto") {
        job.language = next.request->language;  // 擴充功能指定的語言優先
    }
    job.soundEffects = next.request->soundEffects;  // 控制面板的開關
    running_[job.generation] = Running{next.connection, std::move(next.id)};
    callbacks_.submit(std::move(job));
}

void WebService::abandonRunning(const std::string& reason) {
    for (const auto& [generation, running] : running_) {
        server_.send(running.connection, core::webErrorReply(running.id, reason));
    }
    running_.clear();
}

void WebService::onResult(const core::PipelineResult& result) {
    const auto found = running_.find(result.generation);
    if (found == running_.end()) {
        return;  // 已經取消了
    }
    const Running done = std::move(found->second);
    running_.erase(found);
    if (callbacks_.log) {
        const core::PipelineTimings& t = result.timings;
        callbacks_.log("網頁：" + std::to_string(result.groups.size()) + " 段，OCR " +
                       std::to_string(static_cast<int>(t.ocrMs)) + " ms（manga-ocr " +
                       std::to_string(static_cast<int>(t.rereadMs)) + "）、翻譯 " +
                       std::to_string(static_cast<int>(t.translationMs)) + " ms、覆蓋層 " +
                       std::to_string(static_cast<int>(t.overlayMs)) + " ms" +
                       (result.error.empty() ? "" : "，翻譯失敗：" + result.error));
    }
    const core::PngEncoder encode = [](const core::ImageBgra& image) {
        return platform::encodePng(image);
    };
    std::string reply;
    try {
        reply = core::webResultReply(done.id, result.overlay, result.error, encode,
                                     core::kWebMaxReplyBytes, result.notice);
    } catch (const std::exception& error) {
        reply = core::webErrorReply(done.id, error.what());
    }
    if (!server_.send(done.connection, reply) && callbacks_.log) {
        callbacks_.log("網頁：擴充功能已經斷線，結果丟棄");
    }
    pump();
}

}  // namespace tmw::app
