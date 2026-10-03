#include "platform/web_pipe.h"

#include <sddl.h>

#include <array>
#include <chrono>
#include <utility>
#include <vector>

#include "core/web_protocol.h"

namespace tmw::platform {
namespace {

// 自動關閉的 HANDLE
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

// 程序的使用者 SID（TOKEN_USER 的內容）。失敗時是空的
std::vector<std::uint8_t> tokenUser(HANDLE process) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) {
        return {};
    }
    const Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    std::vector<std::uint8_t> buffer(size);
    if (size == 0 || !GetTokenInformation(token.value, TokenUser, buffer.data(), size, &size)) {
        return {};
    }
    return buffer;
}

PSID sidOf(std::vector<std::uint8_t>& user) {
    return reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
}

std::wstring currentUserSid() {
    std::vector<std::uint8_t> user = tokenUser(GetCurrentProcess());
    if (user.empty()) {
        return {};
    }
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sidOf(user), &text)) {
        return {};
    }
    std::wstring out(text);
    LocalFree(text);
    return out;
}

// 讀滿 size 個位元組。stop 可以是 nullptr（不能從外面中斷）
bool readExact(HANDLE pipe, HANDLE stop, char* data, std::size_t size) {
    const Handle done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (done.value == nullptr) {
        return false;
    }
    std::size_t total = 0;
    while (total < size) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = done.value;
        ResetEvent(done.value);
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(size - total, 1u << 20));
        DWORD got = 0;
        if (!ReadFile(pipe, data + total, chunk, nullptr, &overlapped) &&
            GetLastError() != ERROR_IO_PENDING) {
            return false;  // 對方關閉（ERROR_BROKEN_PIPE）或出錯
        }
        if (stop != nullptr) {
            const std::array<HANDLE, 2> waits{done.value, stop};
            if (WaitForMultipleObjects(2, waits.data(), FALSE, INFINITE) != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &overlapped);
                GetOverlappedResult(pipe, &overlapped, &got, TRUE);
                return false;
            }
        }
        if (!GetOverlappedResult(pipe, &overlapped, &got, TRUE) || got == 0) {
            return false;
        }
        total += got;
    }
    return true;
}

}  // namespace

std::wstring webPipeName() {
    return L"\\\\.\\pipe\\TranslationMagicWindow.web." + currentUserSid();
}

std::optional<std::string> readWebFrame(HANDLE pipe, HANDLE stop, std::size_t maxBytes) {
    std::array<std::uint8_t, 4> header{};
    if (!readExact(pipe, stop, reinterpret_cast<char*>(header.data()), header.size())) {
        return std::nullopt;
    }
    const std::uint32_t length = core::frameLength(header);
    if (length > maxBytes) {
        return std::nullopt;
    }
    std::string message(length, '\0');
    if (length > 0 && !readExact(pipe, stop, message.data(), length)) {
        return std::nullopt;
    }
    return message;
}

bool writeWebFrame(HANDLE pipe, std::string_view message) {
    const std::string framed = core::frameMessage(message);
    const Handle done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (done.value == nullptr) {
        return false;
    }
    std::size_t total = 0;
    while (total < framed.size()) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = done.value;
        ResetEvent(done.value);
        const auto chunk =
            static_cast<DWORD>(std::min<std::size_t>(framed.size() - total, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(pipe, framed.data() + total, chunk, nullptr, &overlapped) &&
            GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        if (!GetOverlappedResult(pipe, &overlapped, &written, TRUE) || written == 0) {
            return false;
        }
        total += written;
    }
    return true;
}

struct WebPipeServer::Connection {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::mutex writing;
    std::thread thread;
    std::atomic<bool> open{true};
    // serve 完全結束了（包括 onDisconnect）：這時候 join 不會等到正在送訊息的回呼
    std::atomic<bool> finished{false};

    ~Connection() {
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
        }
    }
};

WebPipeServer::WebPipeServer(std::wstring name, OnMessage onMessage, OnDisconnect onDisconnect)
    : name_(std::move(name)),
      onMessage_(std::move(onMessage)),
      onDisconnect_(std::move(onDisconnect)) {}

WebPipeServer::~WebPipeServer() {
    stop();
}

HANDLE WebPipeServer::createInstance(bool first) {
    // 只有目前的使用者能開（P：不繼承上層的權限）
    const std::wstring sid = currentUserSid();
    if (sid.empty()) {
        return INVALID_HANDLE_VALUE;
    }
    const std::wstring sddl = L"D:P(A;;GA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr)) {
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const HANDLE pipe = CreateNamedPipeW(
        name_.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 1u << 20, 1u << 20, 0, &attributes);
    LocalFree(descriptor);
    return pipe;
}

bool WebPipeServer::start() {
    if (running_) {
        return true;
    }
    // 第一個實例：同名管道已經存在（另一個主程式、或別人搶先開的）就失敗
    const HANDLE first = createInstance(true);
    if (first == INVALID_HANDLE_VALUE) {
        lastError_ = GetLastError();
        return false;
    }
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_ == nullptr) {
        CloseHandle(first);
        return false;
    }
    running_ = true;
    acceptThread_ = std::thread([this, first] { acceptLoop(first); });
    return true;
}

void WebPipeServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    SetEvent(stop_);
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    std::map<int, std::shared_ptr<Connection>> connections;
    {
        const std::lock_guard lock(mutex_);
        connections.swap(connections_);
    }
    for (auto& [id, connection] : connections) {
        if (connection->thread.joinable()) {
            connection->thread.join();
        }
    }
    CloseHandle(stop_);
    stop_ = nullptr;
}

void WebPipeServer::acceptLoop(HANDLE pipe) {
    const Handle connected(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    while (pipe != INVALID_HANDLE_VALUE) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = connected.value;
        ResetEvent(connected.value);
        bool ready = false;
        if (ConnectNamedPipe(pipe, &overlapped)) {
            ready = true;
        } else if (GetLastError() == ERROR_PIPE_CONNECTED) {
            ready = true;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            const std::array<HANDLE, 2> waits{connected.value, stop_};
            if (WaitForMultipleObjects(2, waits.data(), FALSE, INFINITE) == WAIT_OBJECT_0) {
                DWORD ignored = 0;
                ready = GetOverlappedResult(pipe, &overlapped, &ignored, FALSE) != FALSE;
            } else {
                CancelIoEx(pipe, &overlapped);
                DWORD ignored = 0;
                GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
                CloseHandle(pipe);
                return;  // 要停了
            }
        }
        if (!ready) {
            CloseHandle(pipe);
        } else {
            auto connection = std::make_shared<Connection>();
            connection->pipe = pipe;
            const std::lock_guard lock(mutex_);
            // 順便清掉已經結束的連線
            for (auto it = connections_.begin(); it != connections_.end();) {
                if (it->second->finished && it->second->thread.joinable()) {
                    it->second->thread.join();
                    it = connections_.erase(it);
                } else {
                    ++it;
                }
            }
            const int id = nextId_++;
            connections_[id] = connection;
            connection->thread = std::thread([this, id, connection] { serve(id, connection); });
        }
        if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) {
            return;
        }
        pipe = createInstance(false);  // 下一條連線
    }
}

void WebPipeServer::serve(int id, std::shared_ptr<Connection> connection) {
    while (true) {
        std::optional<std::string> message =
            readWebFrame(connection->pipe, stop_, core::kWebMaxRequestBytes);
        if (!message) {
            break;
        }
        if (onMessage_) {
            onMessage_(id, std::move(*message));
        }
    }
    connection->open = false;
    if (onDisconnect_ && running_) {
        onDisconnect_(id);
    }
    connection->finished = true;
}

bool WebPipeServer::send(int connection, std::string_view message) {
    std::shared_ptr<Connection> target;
    {
        const std::lock_guard lock(mutex_);
        const auto found = connections_.find(connection);
        if (found == connections_.end()) {
            return false;
        }
        target = found->second;
    }
    if (!target->open) {
        return false;
    }
    const std::lock_guard writing(target->writing);
    return writeWebFrame(target->pipe, message);
}

HANDLE connectWebPipe(const std::wstring& name, DWORD timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        // SECURITY_IDENTIFICATION：伺服器頂多知道是誰連進來，不能拿我們的身分去做事
        const HANDLE pipe = CreateFileW(
            name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            // 開管道的程序要是同一個使用者
            ULONG pid = 0;
            bool same = false;
            if (GetNamedPipeServerProcessId(pipe, &pid)) {
                const Handle server(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
                if (server.value != nullptr) {
                    std::vector<std::uint8_t> theirs = tokenUser(server.value);
                    std::vector<std::uint8_t> ours = tokenUser(GetCurrentProcess());
                    same = !theirs.empty() && !ours.empty() &&
                           EqualSid(sidOf(theirs), sidOf(ours)) != FALSE;
                }
            }
            if (same) {
                return pipe;
            }
            CloseHandle(pipe);
            return INVALID_HANDLE_VALUE;
        }
        const DWORD error = GetLastError();
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0) {
            return INVALID_HANDLE_VALUE;
        }
        if (error == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(name.c_str(), static_cast<DWORD>(left));
        } else if (error == ERROR_FILE_NOT_FOUND) {
            Sleep(static_cast<DWORD>(std::min<long long>(left, 100)));  // 主程式還沒開管道
        } else {
            return INVALID_HANDLE_VALUE;
        }
    }
}

}  // namespace tmw::platform
