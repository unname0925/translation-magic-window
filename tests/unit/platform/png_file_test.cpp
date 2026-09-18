#include "platform/png_file.h"

#include <windows.h>

#include <gtest/gtest.h>
#include <objbase.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace tmw::platform {
namespace {

// WIC 需要 COM；每個測試自己初始化和釋放
class PngFileTest : public ::testing::Test {
protected:
    void SetUp() override { comResult_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    void TearDown() override {
        if (SUCCEEDED(comResult_)) {
            CoUninitialize();
        }
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    const std::filesystem::path path_ =
        std::filesystem::temp_directory_path() /
        ("tmw_png_test_" + std::to_string(GetCurrentProcessId()) + ".png");

private:
    HRESULT comResult_ = E_FAIL;
};

core::ImageBgra makeGradient(int width, int height, bool varyAlpha) {
    core::ImageBgra image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* p = image.pixel(x, y);
            p[0] = static_cast<std::uint8_t>(x * 7);
            p[1] = static_cast<std::uint8_t>(y * 11);
            p[2] = static_cast<std::uint8_t>((x + y) * 3);
            p[3] = varyAlpha ? static_cast<std::uint8_t>(x * 5 + y) : 255;
        }
    }
    return image;
}

TEST_F(PngFileTest, RoundTripsOpaquePixelsExactly) {
    const core::ImageBgra image = makeGradient(37, 21, false);  // 故意用奇數尺寸
    savePng(image, path_);
    const core::ImageBgra loaded = loadImage(path_);
    EXPECT_EQ(loaded.width, 37);
    EXPECT_EQ(loaded.height, 21);
    EXPECT_EQ(loaded.pixels, image.pixels);
}

TEST_F(PngFileTest, RoundTripsAlpha) {
    const core::ImageBgra image = makeGradient(16, 9, true);
    savePng(image, path_);
    EXPECT_EQ(loadImage(path_).pixels, image.pixels);
}

TEST_F(PngFileTest, EmptyImageIsRejected) {
    EXPECT_THROW(savePng(core::ImageBgra{}, path_), std::runtime_error);
}

TEST_F(PngFileTest, MissingFileIsReported) {
    EXPECT_THROW(loadImage(path_.parent_path() / "tmw_does_not_exist.png"), std::runtime_error);
}

}  // namespace
}  // namespace tmw::platform
