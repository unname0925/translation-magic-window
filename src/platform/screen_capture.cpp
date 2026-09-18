#include "platform/screen_capture.h"

#include <windows.h>

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace tmw::platform {
namespace {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
using WinrtDevice = wgd::Direct3D11::IDirect3DDevice;

constexpr auto kPixelFormat = wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized;
constexpr int kFramePoolBuffers = 2;

struct D3D {
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    WinrtDevice winrtDevice{nullptr};
};

HRESULT tryCreateDevice(D3D_DRIVER_TYPE type, UINT flags, D3D& d3d) {
    d3d = {};
    return D3D11CreateDevice(nullptr, type, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                             d3d.device.put(), nullptr, d3d.context.put());
}

D3D createD3D() {
    D3D d3d;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = E_FAIL;
    // 沒有 GPU（或遠端桌面）時退回 WARP 軟體繪製
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
#ifndef NDEBUG
        // 除錯版開啟 D3D 除錯層；沒有安裝「圖形工具」選用功能時會失敗，改用一般模式
        hr = tryCreateDevice(type, flags | D3D11_CREATE_DEVICE_DEBUG, d3d);
        if (SUCCEEDED(hr)) {
            break;
        }
#endif
        hr = tryCreateDevice(type, flags, d3d);
        if (SUCCEEDED(hr)) {
            break;
        }
    }
    winrt::check_hresult(hr);

    // 畫面回呼在背景執行緒、讀取在呼叫端執行緒。我們自己用 mutex 保護 context，
    // 這裡再開啟 D3D 內建的多執行緒保護作為第二道防線。
    if (const auto multithread = d3d.context.try_as<ID3D11Multithread>()) {
        multithread->SetMultithreadProtected(TRUE);
    }

    const auto dxgiDevice = d3d.device.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    d3d.winrtDevice = inspectable.as<WinrtDevice>();
    return d3d;
}

bool sessionHasProperty(const wchar_t* name) {
    return winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
        L"Windows.Graphics.Capture.GraphicsCaptureSession", name);
}

// 畫面回呼和呼叫端共用的狀態。回呼持有它的 shared_ptr，
// 所以即使工作階段已經關閉，晚到的回呼也不會存取到已釋放的記憶體。
struct Shared {
    std::mutex mutex;  // 保護 D3D context 和以下所有欄位
    std::condition_variable frameReady;

    D3D d3d;
    // 每次開始或結束工作階段都加一。回呼只處理自己那一代的畫面。
    std::uint64_t generation = 0;
    bool sessionClosed = false;  // 例如螢幕被拔掉
    bool deviceLost = false;

    // 最新一張畫面的複本（整個螢幕），以及其中有效內容的大小
    winrt::com_ptr<ID3D11Texture2D> latest;
    core::SizeI latestSize;
    // 讀回 CPU 用的暫存材質，依裁切大小重複使用
    winrt::com_ptr<ID3D11Texture2D> staging;
    core::SizeI stagingSize;

    CaptureStats stats;
};

bool isDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
}

void onFrameArrived(Shared& shared, std::uint64_t generation,
                    const wgc::Direct3D11CaptureFramePool& pool) {
    const wgc::Direct3D11CaptureFrame frame = pool.TryGetNextFrame();
    if (!frame) {
        return;
    }
    std::lock_guard lock(shared.mutex);
    if (generation != shared.generation) {
        frame.Close();
        return;
    }
    ++shared.stats.framesArrived;

    try {
        const auto access =
            frame.Surface()
                .as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::check_hresult(
            access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), texture.put_void()));

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (shared.latest) {
            D3D11_TEXTURE2D_DESC latestDesc{};
            shared.latest->GetDesc(&latestDesc);
            if (latestDesc.Width != desc.Width || latestDesc.Height != desc.Height) {
                shared.latest = nullptr;
            }
        }
        if (!shared.latest) {
            D3D11_TEXTURE2D_DESC copyDesc = desc;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.BindFlags = 0;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.MiscFlags = 0;
            winrt::check_hresult(
                shared.d3d.device->CreateTexture2D(&copyDesc, nullptr, shared.latest.put()));
        }
        // 每張畫面都保留（GPU 內部複製，成本很低）。
        // 如果在這裡丟掉畫面來節流，畫面之後靜止時系統就不會再送新畫面，
        // 我們會一直停在過時的內容。節流交給系統的 MinUpdateInterval。
        shared.d3d.context->CopyResource(shared.latest.get(), texture.get());

        const auto contentSize = frame.ContentSize();
        shared.latestSize = {std::min(contentSize.Width, static_cast<int>(desc.Width)),
                             std::min(contentSize.Height, static_cast<int>(desc.Height))};
        frame.Close();
        shared.frameReady.notify_all();

        // 螢幕解析度改變時，畫面緩衝區也要跟著改變大小
        if (contentSize.Width != static_cast<int>(desc.Width) ||
            contentSize.Height != static_cast<int>(desc.Height)) {
            pool.Recreate(shared.d3d.winrtDevice, kPixelFormat, kFramePoolBuffers, contentSize);
        }
    } catch (const winrt::hresult_error& error) {
        if (isDeviceLost(error.code())) {
            shared.deviceLost = true;
        }
        OutputDebugStringW(error.message().c_str());
    }
}

}  // namespace

struct ScreenCapture::Impl {
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::chrono::milliseconds minFrameInterval;

    HMONITOR monitor = nullptr;
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameArrived;
    wgc::GraphicsCaptureItem::Closed_revoker itemClosed;

    explicit Impl(std::chrono::milliseconds interval) : minFrameInterval(interval) {
        shared->d3d = createD3D();
    }

    ~Impl() { stopSession(); }

    void stopSession() {
        {
            // 先讓進行中和晚到的回呼失效，再關閉工作階段（關閉時不能持有 mutex，
            // 否則正在等 mutex 的回呼會和 Close() 互相等待）
            std::lock_guard lock(shared->mutex);
            ++shared->generation;
            shared->latest = nullptr;
            shared->latestSize = {};
        }
        frameArrived.revoke();
        itemClosed.revoke();
        if (session) {
            session.Close();
        }
        if (pool) {
            pool.Close();
        }
        session = nullptr;
        pool = nullptr;
        item = nullptr;
        monitor = nullptr;
    }

    void startSession(HMONITOR target) {
        stopSession();

        std::uint64_t generation = 0;
        WinrtDevice device{nullptr};
        {
            std::lock_guard lock(shared->mutex);
            generation = ++shared->generation;
            shared->sessionClosed = false;
            ++shared->stats.sessionsStarted;
            device = shared->d3d.winrtDevice;
        }

        const auto interop =
            winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem newItem{nullptr};
        winrt::check_hresult(interop->CreateForMonitor(
            target, winrt::guid_of<wgc::IGraphicsCaptureItem>(), winrt::put_abi(newItem)));

        auto newPool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device, kPixelFormat, kFramePoolBuffers, newItem.Size());
        auto newSession = newPool.CreateCaptureSession(newItem);
        newSession.IsCursorCaptureEnabled(false);
        if (sessionHasProperty(L"IsBorderRequired")) {
            try {
                newSession.IsBorderRequired(false);
            } catch (const winrt::hresult_error&) {
                // 不允許時只是多一個黃色邊框，不影響擷取
            }
        }
        const bool systemThrottling = sessionHasProperty(L"MinUpdateInterval");
        if (systemThrottling) {
            newSession.MinUpdateInterval(
                std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(minFrameInterval));
        }
        {
            std::lock_guard lock(shared->mutex);
            shared->stats.systemThrottling = systemThrottling;
        }

        const std::shared_ptr<Shared> state = shared;
        frameArrived = newPool.FrameArrived(
            winrt::auto_revoke,
            [state, generation](const wgc::Direct3D11CaptureFramePool& sender,
                                const winrt::Windows::Foundation::IInspectable&) {
                onFrameArrived(*state, generation, sender);
            });
        itemClosed =
            newItem.Closed(winrt::auto_revoke,
                           [state, generation](const wgc::GraphicsCaptureItem&,
                                               const winrt::Windows::Foundation::IInspectable&) {
                               std::lock_guard lock(state->mutex);
                               if (generation == state->generation) {
                                   state->sessionClosed = true;
                               }
                           });

        newSession.StartCapture();
        monitor = target;
        item = newItem;
        pool = newPool;
        session = newSession;
    }

    void recreateDevice() {
        stopSession();
        D3D fresh = createD3D();
        std::lock_guard lock(shared->mutex);
        shared->d3d = std::move(fresh);
        shared->staging = nullptr;
        shared->stagingSize = {};
        shared->deviceLost = false;
    }

    bool needsNewDevice() {
        std::lock_guard lock(shared->mutex);
        return shared->deviceLost || shared->d3d.device->GetDeviceRemovedReason() != S_OK;
    }

    bool needsNewSession(HMONITOR target) {
        std::lock_guard lock(shared->mutex);
        return !session || monitor != target || shared->sessionClosed;
    }

    std::optional<core::ImageBgra> readRegion(const core::RectI& screenRect,
                                              std::chrono::milliseconds timeout) {
        if (screenRect.empty()) {
            return std::nullopt;
        }
        const core::PointI center = screenRect.center();
        const HMONITOR target = MonitorFromPoint({center.x, center.y}, MONITOR_DEFAULTTONULL);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (target == nullptr || !GetMonitorInfoW(target, &monitorInfo)) {
            return std::nullopt;
        }
        const RECT& m = monitorInfo.rcMonitor;
        const core::RectI monitorRect{m.left, m.top, m.right, m.bottom};

        if (needsNewDevice()) {
            recreateDevice();
        }
        if (needsNewSession(target)) {
            startSession(target);
        }

        std::unique_lock lock(shared->mutex);
        if (!shared->frameReady.wait_for(lock, timeout,
                                         [this] { return shared->latest != nullptr; })) {
            return std::nullopt;
        }

        const core::RectI local =
            core::intersect(core::screenToMonitorLocal(screenRect, monitorRect),
                            {0, 0, shared->latestSize.width, shared->latestSize.height});
        if (local.empty()) {
            return std::nullopt;
        }
        const core::SizeI size{local.width(), local.height()};

        // 只把需要的範圍從 GPU 複製到可讀回的暫存材質
        if (!shared->staging || shared->stagingSize != size) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(size.width);
            desc.Height = static_cast<UINT>(size.height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            shared->staging = nullptr;
            winrt::check_hresult(
                shared->d3d.device->CreateTexture2D(&desc, nullptr, shared->staging.put()));
            shared->stagingSize = size;
        }
        const D3D11_BOX box{static_cast<UINT>(local.left),  static_cast<UINT>(local.top),    0,
                            static_cast<UINT>(local.right), static_cast<UINT>(local.bottom), 1};
        shared->d3d.context->CopySubresourceRegion(shared->staging.get(), 0, 0, 0, 0,
                                                   shared->latest.get(), 0, &box);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr =
            shared->d3d.context->Map(shared->staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            shared->deviceLost = shared->deviceLost || isDeviceLost(hr);
            return std::nullopt;
        }
        core::ImageBgra image(size.width, size.height);
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
        for (int y = 0; y < size.height; ++y) {
            std::uint8_t* row = image.pixel(0, y);
            std::memcpy(row, source + static_cast<std::size_t>(y) * mapped.RowPitch,
                        image.stride());
            // 螢幕畫面本來就是不透明的；擷取結果的 alpha 沒有意義，統一設為 255
            for (int x = 0; x < size.width; ++x) {
                row[x * 4 + 3] = 255;
            }
        }
        shared->d3d.context->Unmap(shared->staging.get(), 0);
        return image;
    }
};

ScreenCapture::ScreenCapture() : ScreenCapture(Options{}) {}

ScreenCapture::ScreenCapture(Options options) {
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
        throw std::runtime_error("Windows.Graphics.Capture is not supported on this system");
    }
    try {
        impl_ = std::make_unique<Impl>(options.minFrameInterval);
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("failed to create the Direct3D device (HRESULT " +
                                 std::to_string(static_cast<std::uint32_t>(error.code())) + ")");
    }
}

ScreenCapture::~ScreenCapture() = default;

std::optional<core::ImageBgra> ScreenCapture::readRegion(const core::RectI& screenRect,
                                                         std::chrono::milliseconds timeout) {
    try {
        return impl_->readRegion(screenRect, timeout);
    } catch (const winrt::hresult_error& error) {
        OutputDebugStringW(error.message().c_str());
        std::lock_guard lock(impl_->shared->mutex);
        impl_->shared->deviceLost = impl_->shared->deviceLost || isDeviceLost(error.code());
        return std::nullopt;
    }
}

void ScreenCapture::reset() {
    impl_->stopSession();
}

CaptureStats ScreenCapture::stats() const {
    std::lock_guard lock(impl_->shared->mutex);
    return impl_->shared->stats;
}

}  // namespace tmw::platform
