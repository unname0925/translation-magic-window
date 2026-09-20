#include "platform/secret.h"

#include <windows.h>

#include <dpapi.h>
#include <wincrypt.h>

#include <vector>

#include "platform/win_error.h"

namespace tmw::platform {
namespace {

// 加密後的內容不是給人看的，所以用 base64；CryptBinaryToString 會加換行，這裡要求不加。
std::string toBase64(const std::vector<BYTE>& data) {
    DWORD size = 0;
    if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size)) {
        throwLastError("CryptBinaryToStringA");
    }
    std::string text(size, '\0');
    if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &size)) {
        throwLastError("CryptBinaryToStringA");
    }
    text.resize(size);
    return text;
}

std::optional<std::vector<BYTE>> fromBase64(std::string_view text) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
                              nullptr, &size, nullptr, nullptr)) {
        return std::nullopt;
    }
    std::vector<BYTE> data(size);
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
                              data.data(), &size, nullptr, nullptr)) {
        return std::nullopt;
    }
    data.resize(size);
    return data;
}

}  // namespace

std::string encryptSecret(std::string_view plaintext) {
    if (plaintext.empty()) {
        return {};
    }
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Translation Magic Window API key", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        throwLastError("CryptProtectData");
    }
    const std::vector<BYTE> encrypted(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return toBase64(encrypted);
}

std::optional<std::string> decryptSecret(std::string_view encrypted) {
    if (encrypted.empty()) {
        return std::string{};
    }
    std::optional<std::vector<BYTE>> data = fromBase64(encrypted);
    if (!data) {
        return std::nullopt;
    }
    DATA_BLOB input{static_cast<DWORD>(data->size()), data->data()};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &output)) {
        return std::nullopt;
    }
    std::string plaintext(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return plaintext;
}

}  // namespace tmw::platform
