#include "platform/overlay_renderer.h"

#include <windows.h>

#include <d2d1.h>
#include <dwrite_3.h>
#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include "platform/text_encoding.h"
#include "platform/win_error.h"

namespace tmw::platform {
namespace {

// 譯文和框邊之間留的空間（像素）
constexpr float kPadding = 2.0f;
constexpr wchar_t kLocale[] = L"zh-TW";
// 直排用非 UI 版：UI 版為了橫排的介面把行高壓縮過
constexpr wchar_t kHorizontalFont[] = L"Microsoft JhengHei UI";
constexpr wchar_t kVerticalFont[] = L"Microsoft JhengHei";

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(what) + " failed (HRESULT " +
                                 std::to_string(static_cast<std::uint32_t>(hr)) + ")");
    }
}

D2D1_COLOR_F toColor(core::Rgba color) {
    return D2D1::ColorF(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

// ルビ的字是正文的一半（和日文的振り仮名一樣）
constexpr float kRubyScale = 0.5f;

struct RubyLines {
    float spacing = 0.0f;   // 一行（一欄）的寬度
    float baseline = 0.0f;  // 從行的起點到基線
};

RubyLines rubyLines(float fontSize, bool vertical) {
    const float ruby = fontSize * kRubyScale;
    const float spacing = fontSize * 1.3f + ruby;
    // 橫排：上方先留ルビ的高度，再放正文（基線大約在字的頂端往下 1 個字高）。
    // 直排：DirectWrite 的直排基線在欄的中央，欄的起點在右邊；正文往左偏，右側留給ルビ
    return vertical ? RubyLines{spacing, ruby + fontSize * 0.65f}
                    : RubyLines{spacing, ruby + fontSize * 1.05f};
}

// UTF-8 的第 characters 個字，在 UTF-16 裡的位置（DirectWrite 用 UTF-16）
UINT32 utf16Index(std::string_view utf8, int characters) {
    UINT32 out = 0;
    std::size_t at = 0;
    for (int i = 0; i < characters && at < utf8.size(); ++i) {
        const auto lead = static_cast<unsigned char>(utf8[at]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        out += length == 4 ? 2 : 1;  // 4 個位元組的字在 UTF-16 是一對代理字元
        at += length;
    }
    return out;
}

struct Box {
    float width = 0.0f;
    float height = 0.0f;
};

Box textBox(const core::OverlayItem& item) {
    const float padding =
        std::min(kPadding, std::min(item.rect.width(), item.rect.height()) / 4.0f);
    return Box{std::max(1.0f, item.rect.width() - padding * 2),
               std::max(1.0f, item.rect.height() - padding * 2)};
}

}  // namespace

struct OverlayRenderer::Impl {
    winrt::com_ptr<ID2D1Factory> d2d;
    winrt::com_ptr<IDWriteFactory> dwrite;
    winrt::com_ptr<ID2D1DCRenderTarget> target;
    HDC memoryDc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    std::uint8_t* bits = nullptr;
    core::SizeI bitmapSize{};

    ~Impl() {
        releaseBitmap();
        if (memoryDc != nullptr) {
            DeleteDC(memoryDc);
        }
    }

    void releaseBitmap() {
        if (bitmap == nullptr) {
            return;
        }
        SelectObject(memoryDc, previousBitmap);
        DeleteObject(bitmap);
        bitmap = nullptr;
        bits = nullptr;
        bitmapSize = {};
    }

    void ensureBitmap(core::SizeI size) {
        if (bitmap != nullptr && size == bitmapSize) {
            return;
        }
        releaseBitmap();
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = size.width;
        info.bmiHeader.biHeight = -size.height;  // 負值：由上到下排列
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(memoryDc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (bitmap == nullptr) {
            throwLastError("CreateDIBSection failed for the overlay");
        }
        bits = static_cast<std::uint8_t*>(pixels);
        bitmapSize = size;
        previousBitmap = SelectObject(memoryDc, bitmap);
    }

    // 內建字型（M4-04）：從檔案載入、不安裝到系統。沒有時用微軟正黑體
    winrt::com_ptr<IDWriteFontCollection> fontCollection;
    std::wstring fontFamily;

    winrt::com_ptr<IDWriteTextFormat> textFormat(bool vertical, float size) {
        const wchar_t* family = !fontFamily.empty() ? fontFamily.c_str()
                                : vertical          ? kVerticalFont
                                                    : kHorizontalFont;
        winrt::com_ptr<IDWriteTextFormat> format;
        check(dwrite->CreateTextFormat(family, fontCollection.get(), DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                       kLocale, format.put()),
              "IDWriteFactory::CreateTextFormat");
        return format;
    }

    winrt::com_ptr<IDWriteTextLayout> layout(const core::OverlayItem& item, float fontSize) {
        const winrt::com_ptr<IDWriteTextFormat> format = textFormat(item.vertical, fontSize);
        if (item.vertical) {
            // 漫畫的直排：一欄由上到下，欄由右到左
            check(format->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM),
                  "SetReadingDirection");
            check(format->SetFlowDirection(DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT),
                  "SetFlowDirection");
        }
        // 整段置中在框裡（橫排是上下置中，直排是左右置中）
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        if (!item.ruby.empty()) {
            // 每一行（直排是每一欄）多留ルビ的位置：橫排在字的上方，直排在字的右側
            const RubyLines lines = rubyLines(fontSize, item.vertical);
            check(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lines.spacing,
                                         lines.baseline),
                  "SetLineSpacing");
        }

        const std::wstring text = utf8ToWide(item.text);
        const Box box = textBox(item);
        winrt::com_ptr<IDWriteTextLayout> out;
        check(dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.get(),
                                       box.width, box.height, out.put()),
              "IDWriteFactory::CreateTextLayout");
        return out;
    }

    // ルビ：橫排畫在詞的上方、直排畫在詞的右側，置中對齊那個詞。詞被拆到兩行時畫在第一段旁邊。
    void drawRuby(const core::OverlayItem& item, IDWriteTextLayout* text, D2D1_POINT_2F origin,
                  float fontSize, ID2D1Brush* brush) {
        const float size = fontSize * kRubyScale;
        const winrt::com_ptr<IDWriteTextFormat> format = textFormat(item.vertical, size);
        if (item.vertical) {
            check(format->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM),
                  "SetReadingDirection");
            check(format->SetFlowDirection(DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT),
                  "SetFlowDirection");
        }
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        for (const core::OverlayRuby& ruby : item.ruby) {
            const UINT32 start = utf16Index(item.text, ruby.start);
            const UINT32 length = utf16Index(item.text, ruby.start + ruby.length) - start;
            DWRITE_HIT_TEST_METRICS where[4]{};
            UINT32 count = 0;
            if (FAILED(
                    text->HitTestTextRange(start, length, origin.x, origin.y, where, 4, &count)) ||
                count == 0) {
                continue;
            }
            const DWRITE_HIT_TEST_METRICS& m = where[0];
            const std::wstring reading = utf8ToWide(ruby.text);
            // 讀音比詞長時往兩邊伸出去（和日文的ルビ一樣）
            const float reach = (m.width + m.height) + size * static_cast<float>(reading.size());
            D2D1_RECT_F box{};
            if (item.vertical) {
                const float centerY = m.top + m.height / 2;
                const float right = m.left + m.width;
                box = D2D1::RectF(right - size * 1.2f, centerY - reach, right, centerY + reach);
            } else {
                const float centerX = m.left + m.width / 2;
                box = D2D1::RectF(centerX - reach, m.top, centerX + reach, m.top + size * 1.3f);
            }
            target->DrawText(reading.c_str(), static_cast<UINT32>(reading.size()), format.get(),
                             box, brush);
        }
    }

    bool fits(const core::OverlayItem& item, float fontSize) {
        const winrt::com_ptr<IDWriteTextLayout> candidate = layout(item, fontSize);
        DWRITE_TEXT_METRICS metrics{};
        check(candidate->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
        const Box box = textBox(item);
        constexpr float kSlack = 0.5f;
        return metrics.width <= box.width + kSlack && metrics.height <= box.height + kSlack;
    }
};

OverlayRenderer::OverlayRenderer() : impl_(std::make_unique<Impl>()) {
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                            impl_->d2d.put_void()),
          "D2D1CreateFactory");
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(impl_->dwrite.put())),
          "DWriteCreateFactory");
    // 軟體繪製：覆蓋層只有幾段字，不值得占用顯示卡（OCR 正在用它），而且每台電腦畫出來一樣，
    // 影像比對測試才穩定
    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    check(impl_->d2d->CreateDCRenderTarget(&properties, impl_->target.put()),
          "ID2D1Factory::CreateDCRenderTarget");
    // 分層視窗帶 alpha，ClearType 的彩色邊會變成雜色
    impl_->target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    impl_->memoryDc = CreateCompatibleDC(nullptr);
    if (impl_->memoryDc == nullptr) {
        throwLastError("CreateCompatibleDC failed for the overlay");
    }
}

OverlayRenderer::~OverlayRenderer() = default;

bool OverlayRenderer::setFont(const std::filesystem::path& file) {
    impl_->fontCollection = nullptr;
    impl_->fontFamily.clear();
    if (file.empty()) {
        return true;
    }
    try {
        // Windows 10 起的字型集合：直接用檔案，不必安裝
        const auto factory = impl_->dwrite.as<IDWriteFactory5>();
        winrt::com_ptr<IDWriteFontSetBuilder1> builder;
        check(factory->CreateFontSetBuilder(builder.put()), "CreateFontSetBuilder");
        winrt::com_ptr<IDWriteFontFile> fontFile;
        check(factory->CreateFontFileReference(file.c_str(), nullptr, fontFile.put()),
              "CreateFontFileReference");
        check(builder->AddFontFile(fontFile.get()), "AddFontFile");
        winrt::com_ptr<IDWriteFontSet> set;
        check(builder->CreateFontSet(set.put()), "CreateFontSet");
        winrt::com_ptr<IDWriteFontCollection1> collection;
        check(factory->CreateFontCollectionFromFontSet(set.get(), collection.put()),
              "CreateFontCollectionFromFontSet");
        if (collection->GetFontFamilyCount() == 0) {
            return false;
        }
        winrt::com_ptr<IDWriteFontFamily> family;
        check(collection->GetFontFamily(0, family.put()), "GetFontFamily");
        winrt::com_ptr<IDWriteLocalizedStrings> names;
        check(family->GetFamilyNames(names.put()), "GetFamilyNames");
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists) {
            index = 0;
        }
        UINT32 length = 0;
        check(names->GetStringLength(index, &length), "GetStringLength");
        std::wstring name(length + 1, L'\0');
        check(names->GetString(index, name.data(), length + 1), "GetString");
        name.resize(length);
        impl_->fontCollection = collection;
        impl_->fontFamily = std::move(name);
        return true;
    } catch (const std::exception&) {
        impl_->fontCollection = nullptr;
        impl_->fontFamily.clear();
        return false;
    } catch (const winrt::hresult_error&) {
        impl_->fontCollection = nullptr;
        impl_->fontFamily.clear();
        return false;
    }
}

std::wstring OverlayRenderer::fontFamily() const {
    return impl_->fontFamily;
}

float OverlayRenderer::fitFontSize(const core::OverlayItem& item) {
    const Box box = textBox(item);
    // 上限：原文的字（不知道時是框的短邊）
    int high = static_cast<int>(std::min(box.width, box.height));
    if (item.lineThickness > 0) {
        high = std::min(high, item.lineThickness);
    }
    int low = static_cast<int>(kMinFontSize);
    if (high <= low || !impl_->fits(item, static_cast<float>(low))) {
        return kMinFontSize;
    }
    // low 一定放得下；找最大的
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (impl_->fits(item, static_cast<float>(middle))) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return static_cast<float>(low);
}

core::ImageBgra OverlayRenderer::render(core::SizeI size,
                                        std::span<const core::OverlayItem> items) {
    core::ImageBgra out(size.width, size.height);
    if (out.empty()) {
        return out;
    }
    impl_->ensureBitmap(size);
    const RECT bounds{0, 0, size.width, size.height};
    check(impl_->target->BindDC(impl_->memoryDc, &bounds), "ID2D1DCRenderTarget::BindDC");

    impl_->target->BeginDraw();
    impl_->target->Clear(D2D1::ColorF(0, 0, 0, 0));
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    check(impl_->target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), brush.put()),
          "CreateSolidColorBrush");
    for (const core::OverlayItem& item : items) {
        if (item.rect.empty()) {
            continue;
        }
        const D2D1_RECT_F rect =
            D2D1::RectF(static_cast<float>(item.rect.left), static_cast<float>(item.rect.top),
                        static_cast<float>(item.rect.right), static_cast<float>(item.rect.bottom));
        impl_->target->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
        if (item.patch.width == item.rect.width() && item.patch.height == item.rect.height()) {
            // 背景修補的結果（M4-01）：不透明的 BGRA，預乘 alpha 和原本一樣
            winrt::com_ptr<ID2D1Bitmap> patch;
            check(impl_->target->CreateBitmap(
                      D2D1::SizeU(static_cast<UINT32>(item.patch.width),
                                  static_cast<UINT32>(item.patch.height)),
                      item.patch.pixels.data(), static_cast<UINT32>(item.patch.stride()),
                      D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                               D2D1_ALPHA_MODE_PREMULTIPLIED)),
                      patch.put()),
                  "ID2D1RenderTarget::CreateBitmap");
            impl_->target->DrawBitmap(patch.get(), rect);
        } else {
            brush->SetColor(toColor(item.background));
            impl_->target->FillRectangle(rect, brush.get());
        }

        const float fontSize = fitFontSize(item);
        const winrt::com_ptr<IDWriteTextLayout> text = impl_->layout(item, fontSize);
        const Box box = textBox(item);
        const D2D1_POINT_2F origin =
            D2D1::Point2F(rect.left + (item.rect.width() - box.width) / 2,
                          rect.top + (item.rect.height() - box.height) / 2);
        const auto draw = [&](D2D1_POINT_2F at) {
            impl_->target->DrawTextLayout(at, text.get(), brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            impl_->drawRuby(item, text.get(), at, fontSize, brush.get());
        };
        if (item.outline) {
            // 描邊（M4-02）：往八個方向各偏移一點先畫一次描邊色，填色再蓋在中間
            const float width = std::max(1.0f, std::round(fontSize / 16.0f));
            brush->SetColor(toColor(*item.outline));
            for (const auto [dx, dy] :
                 {std::pair{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}) {
                draw(D2D1::Point2F(origin.x + dx * width, origin.y + dy * width));
            }
        }
        brush->SetColor(toColor(item.foreground));
        draw(origin);
        impl_->target->PopAxisAlignedClip();
    }
    check(impl_->target->EndDraw(), "ID2D1RenderTarget::EndDraw");
    GdiFlush();
    std::memcpy(out.pixels.data(), impl_->bits, out.pixels.size());
    return out;
}

}  // namespace tmw::platform
