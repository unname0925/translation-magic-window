#include "core/translator_chain.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace tmw::core {

TranslatorChain::TranslatorChain(std::vector<std::shared_ptr<ITranslator>> engines,
                                 const IClock& clock, ChainOptions options)
    : clock_(clock), options_(options) {
    states_.reserve(engines.size());
    for (std::shared_ptr<ITranslator>& engine : engines) {
        states_.push_back(State{std::move(engine), 0, TimePoint{}});
    }
}

std::string TranslatorChain::describePaused() const {
    const TimePoint now = clock_.now();
    std::string out;
    for (const State& state : states_) {
        if (now >= state.pausedUntil) {
            continue;
        }
        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(
                                 state.pausedUntil - now + std::chrono::minutes(1))
                                 .count();
        out += out.empty() ? "" : "；";
        out += state.engine->id() + "：" + describeTranslateError(state.lastError) + "，約 " +
               std::to_string(minutes) + " 分鐘後再試";
    }
    return out;
}

void TranslatorChain::recordFailure(State& state) {
    ++state.failures;
    if (state.failures >= options_.failuresBeforePause) {
        state.pausedUntil = clock_.now() + options_.pause;
        // 暫停結束後重新給它三次機會，而不是一失敗就又被停掉
        state.failures = 0;
    }
}

ChainResult TranslatorChain::translate(std::span<const std::string> segments,
                                       const TranslateRequest& request, std::stop_token cancel) {
    if (cancel.stop_requested()) {
        throw TranslatorError(TranslateError::Cancelled, "翻譯前就被取消");
    }
    if (segments.empty()) {
        return {};
    }

    std::optional<TranslatorError> lastError;
    bool tried = false;
    for (std::size_t i = 0; i < states_.size(); ++i) {
        std::shared_ptr<ITranslator> engine;
        {
            const std::lock_guard lock(mutex_);
            if (clock_.now() < states_[i].pausedUntil) {
                continue;
            }
            engine = states_[i].engine;
        }
        if (cancel.stop_requested()) {
            throw TranslatorError(TranslateError::Cancelled, "翻譯中被取消");
        }
        tried = true;
        try {
            std::vector<std::string> out = engine->translate(segments, request, cancel);
            if (out.size() != segments.size()) {
                throw TranslatorError(TranslateError::BadResponse, "譯文數量和原文不同");
            }
            {
                const std::lock_guard lock(mutex_);
                states_[i].failures = 0;
            }
            return ChainResult{engine->id(), std::move(out)};
        } catch (const TranslatorError& error) {
            if (error.kind() == TranslateError::Cancelled) {
                throw;
            }
            lastError = error;
            {
                const std::lock_guard lock(mutex_);
                states_[i].lastError = error.kind();
            }
        } catch (const std::exception& error) {
            // 引擎應該丟 TranslatorError，但第三方程式庫（JSON、HTTP）可能丟別的
            lastError = TranslatorError(TranslateError::BadResponse, error.what());
        }
        const std::lock_guard lock(mutex_);
        recordFailure(states_[i]);
    }

    if (!tried) {
        if (states_.empty()) {
            throw TranslatorError(TranslateError::Unavailable, "沒有設定任何翻譯引擎");
        }
        const std::lock_guard lock(mutex_);
        throw TranslatorError(TranslateError::Unavailable,
                              "所有翻譯引擎都在暫停中（" + describePaused() + "）");
    }
    throw lastError.value();
}

std::vector<std::string> TranslatorChain::engineIds() const {
    const std::lock_guard lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(states_.size());
    for (const State& state : states_) {
        ids.push_back(state.engine->id());
    }
    return ids;
}

bool TranslatorChain::paused(std::string_view id) const {
    const std::lock_guard lock(mutex_);
    for (const State& state : states_) {
        if (state.engine->id() == id) {
            return clock_.now() < state.pausedUntil;
        }
    }
    return false;
}

}  // namespace tmw::core
