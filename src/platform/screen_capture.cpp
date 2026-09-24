#include "platform/screen_capture.h"

#include <windows.h>

#include <d3d11_4.h>
#include <d3d11sdklayers.h>
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

struct StagingTexture {
    winrt::com_ptr<ID3D11Texture2D> texture;
    core::SizeI size;
};

D3D11_BOX toBox(const core::RectI& rect) {
    return {static_cast<UINT>(rect.left),  static_cast<UINT>(rect.top),    0,
            static_cast<UINT>(rect.right), static_cast<UINT>(rect.bottom), 1};
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
    // 讀回 CPU 用的暫存材質，依大小重複使用
    StagingTexture regionStaging;
    StagingTexture thumbnailStaging;
    // 產生縮圖用的 mipmap 材質：在 GPU 上逐級縮小一半，只讀回最小需要的那一級
    winrt::com_ptr<ID3D11Texture2D> mipTexture;
    winrt::com_ptr<ID3D11ShaderResourceView> mipView;
    core::SizeI mipSize;

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
    const Options options;

    HMONITOR monitor = nullptr;
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameArrived;
    wgc::GraphicsCaptureItem::Closed_revoker itemClosed;

    explicit Impl(const Options& captureOptions) : options(captureOptions) {
        shared->d3d = createD3D();
    }

    ~Impl() {
        stopSession();
        releaseCaches();
#ifndef NDEBUG
        // 除錯版：把 D3D 全部放掉之後列出還活著的物件（execution-plan 5.7）。
        // 訊息會送到除錯輸出（Visual Studio 的「輸出」視窗，或 DebugView）。
        // 沒安裝「圖形工具」選用功能時除錯層建立不起來，try_as 會是空的，就跳過。
        const auto debug = shared->d3d.device.try_as<ID3D11Debug>();
        shared->d3d = {};
        if (debug) {
            debug->ReportLiveDeviceObjects(
                static_cast<D3D11_RLDO_FLAGS>(D3D11_RLDO_SUMMARY | D3D11_RLDO_IGNORE_INTERNAL));
        }
#endif
    }

    // 放掉為了重複使用而留著的材質。平常不做（留著才不用每次重建），
    // 只有要問「還有什麼沒被釋放」的時候才需要先清乾淨。
    void releaseCaches() {
        std::lock_guard lock(shared->mutex);
        shared->regionStaging = {};
        shared->thumbnailStaging = {};
        shared->mipView = nullptr;
        shared->mipTexture = nullptr;
        shared->mipSize = {};
    }

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
        newSession.IsCursorCaptureEnabled(options.captureCursor);
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
                std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
                    options.minFrameInterval));
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
        shared->regionStaging = {};
        shared->thumbnailStaging = {};
        shared->mipTexture = nullptr;
        shared->mipView = nullptr;
        shared->mipSize = {};
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

    // 確認裝置和工作階段可用，並等到第一張畫面。
    // 成功時 lock 會持有 shared->mutex，並回傳要裁切的範圍（螢幕擷取畫面內的座標）。
    std::optional<core::RectI> prepare(const core::RectI& screenRect,
                                       std::chrono::milliseconds timeout,
                                       std::unique_lock<std::mutex>& lock) {
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

        lock = std::unique_lock(shared->mutex);
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
        return local;
    }

    // 把 source 的一個子資源（box 不為 nullptr 時只取其中一塊）複製到暫存材質，再讀回 CPU。
    // 呼叫時必須持有 shared->mutex。
    std::optional<core::ImageBgra> readback(ID3D11Texture2D* source, UINT subresource,
                                            const D3D11_BOX* box, core::SizeI size,
                                            StagingTexture& staging) {
        if (!staging.texture || staging.size != size) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(size.width);
            desc.Height = static_cast<UINT>(size.height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            staging.texture = nullptr;
            winrt::check_hresult(
                shared->d3d.device->CreateTexture2D(&desc, nullptr, staging.texture.put()));
            staging.size = size;
        }
        shared->d3d.context->CopySubresourceRegion(staging.texture.get(), 0, 0, 0, 0, source,
                                                   subresource, box);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr =
            shared->d3d.context->Map(staging.texture.get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            shared->deviceLost = shared->deviceLost || isDeviceLost(hr);
            return std::nullopt;
        }
        core::ImageBgra image(size.width, size.height);
        const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData);
        for (int y = 0; y < size.height; ++y) {
            std::uint8_t* row = image.pixel(0, y);
            std::memcpy(row, bytes + static_cast<std::size_t>(y) * mapped.RowPitch, image.stride());
            // 螢幕畫面本來就是不透明的；擷取結果的 alpha 沒有意義，統一設為 255
            for (int x = 0; x < size.width; ++x) {
                row[x * 4 + 3] = 255;
            }
        }
        shared->d3d.context->Unmap(staging.texture.get(), 0);
        return image;
    }

    std::optional<core::ImageBgra> readRegion(const core::RectI& screenRect,
                                              std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock;
        const std::optional<core::RectI> local = prepare(screenRect, timeout, lock);
        if (!local) {
            return std::nullopt;
        }
        const D3D11_BOX box = toBox(*local);
        return readback(shared->latest.get(), 0, &box, {local->width(), local->height()},
                        shared->regionStaging);
    }

    std::optional<core::ImageBgra> readThumbnail(const core::RectI& screenRect, int maxSide,
                                                 std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock;
        const std::optional<core::RectI> local = prepare(screenRect, timeout, lock);
        if (!local) {
            return std::nullopt;
        }
        const core::SizeI size{local->width(), local->height()};
        const D3D11_BOX box = toBox(*local);

        // 選擇 mipmap 等級：每一級長寬各縮小一半，取第一個最長邊不超過 maxSide 的等級
        const int limit = std::max(1, maxSide);
        UINT level = 0;
        while (std::max(size.width >> level, size.height >> level) > limit) {
            ++level;
        }
        if (level == 0) {
            return readback(shared->latest.get(), 0, &box, size, shared->thumbnailStaging);
        }

        if (!shared->mipTexture || shared->mipSize != size) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(size.width);
            desc.Height = static_cast<UINT>(size.height);
            desc.MipLevels = 0;  // 完整的 mipmap 鏈，一路縮到 1×1
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            shared->mipTexture = nullptr;
            shared->mipView = nullptr;
            winrt::check_hresult(
                shared->d3d.device->CreateTexture2D(&desc, nullptr, shared->mipTexture.put()));
            winrt::check_hresult(shared->d3d.device->CreateShaderResourceView(
                shared->mipTexture.get(), nullptr, shared->mipView.put()));
            shared->mipSize = size;
        }
        shared->d3d.context->CopySubresourceRegion(shared->mipTexture.get(), 0, 0, 0, 0,
                                                   shared->latest.get(), 0, &box);
        shared->d3d.context->GenerateMips(shared->mipView.get());

        const core::SizeI levelSize{std::max(1, size.width >> level),
                                    std::max(1, size.height >> level)};
        return readback(shared->mipTexture.get(), level, nullptr, levelSize,
                        shared->thumbnailStaging);
    }
};

ScreenCapture::ScreenCapture() : ScreenCapture(Options{}) {}

ScreenCapture::ScreenCapture(Options options) {
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
        throw std::runtime_error("Windows.Graphics.Capture is not supported on this system");
    }
    try {
        impl_ = std::make_unique<Impl>(options);
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

std::optional<core::ImageBgra> ScreenCapture::readThumbnail(const core::RectI& screenRect,
                                                            int maxSide,
                                                            std::chrono::milliseconds timeout) {
    try {
        return impl_->readThumbnail(screenRect, maxSide, timeout);
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

unsigned long ScreenCapture::d3dDeviceReferences() const {
    std::lock_guard lock(impl_->shared->mutex);
    ID3D11Device* device = impl_->shared->d3d.device.get();
    if (device == nullptr) {
        return 0;
    }
    // 每一個沒被釋放的材質、表面或工作階段都抓著裝置，所以參考計數就是「還有多少東西活著」。
    // 看的是「跑完一輪前後有沒有變」，絕對值取決於 D3D 內部怎麼實作，沒有意義。
    device->AddRef();
    return device->Release();
}

CaptureStats ScreenCapture::stats() const {
    std::lock_guard lock(impl_->shared->mutex);
    return impl_->shared->stats;
}

}  // namespace tmw::platform
