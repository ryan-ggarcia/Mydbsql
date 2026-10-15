#include "infra/platform/SystemCredentialStore.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace mydb::infra::platform {

namespace {

constexpr std::string_view kService = "Mydbsql";

struct CfReleaser {
    void operator()(CFTypeRef object) const noexcept {
        if (object != nullptr) {
            CFRelease(object);
        }
    }
};

template <typename T>
using CfPtr = std::unique_ptr<std::remove_pointer_t<T>, CfReleaser>;

CfPtr<CFStringRef> cfString(std::string_view text) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a API recebe bytes UTF-8
    return CfPtr<CFStringRef>(CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
                                                      static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false));
}

CfPtr<CFDataRef> cfData(std::string_view bytes) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a API recebe bytes
    return CfPtr<CFDataRef>(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.data()),
                                         static_cast<CFIndex>(bytes.size())));
}

std::string statusMessage(OSStatus status) {
    const CfPtr<CFStringRef> message(SecCopyErrorMessageString(status, nullptr));
    std::string text = "código " + std::to_string(status);
    if (message) {
        if (const char* utf8 = CFStringGetCStringPtr(message.get(), kCFStringEncodingUTF8); utf8 != nullptr) {
            text = std::string(utf8) + " (" + text + ")";
        }
    }
    return text;
}

class MacCredentialStore final : public core::ICredentialStore {
public:
    [[nodiscard]] std::optional<std::string> read(std::string_view key) override {
        const auto query = baseQuery(key);
        CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching(query.get(), &result);
        if (status == errSecItemNotFound) {
            return std::nullopt;
        }
        if (status != errSecSuccess) {
            throw std::runtime_error("Keychain: leitura falhou: " + statusMessage(status));
        }
        const CfPtr<CFDataRef> data(static_cast<CFDataRef>(result));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): o dado é um bloco de bytes
        const auto* bytes = reinterpret_cast<const char*>(CFDataGetBytePtr(data.get()));
        return std::string(bytes, static_cast<std::size_t>(CFDataGetLength(data.get())));
    }

    void write(std::string_view key, std::string_view secret) override {
        const auto query = baseQuery(key);
        const auto data = cfData(secret);
        const CfPtr<CFMutableDictionaryRef> update(CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFDictionarySetValue(update.get(), kSecValueData, data.get());
        OSStatus status = SecItemUpdate(query.get(), update.get());
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query.get(), kSecValueData, data.get());
            status = SecItemAdd(query.get(), nullptr);
        }
        if (status != errSecSuccess) {
            throw std::runtime_error("Keychain: gravação falhou: " + statusMessage(status));
        }
    }

    void erase(std::string_view key) override {
        const OSStatus status = SecItemDelete(baseQuery(key).get());
        if (status != errSecSuccess && status != errSecItemNotFound) {
            throw std::runtime_error("Keychain: remoção falhou: " + statusMessage(status));
        }
    }

    [[nodiscard]] bool persistent() const noexcept override { return true; }

private:
    static CfPtr<CFMutableDictionaryRef> baseQuery(std::string_view key) {
        CfPtr<CFMutableDictionaryRef> query(CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        const auto service = cfString(kService);
        const auto account = cfString(key);
        CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query.get(), kSecAttrService, service.get());
        CFDictionarySetValue(query.get(), kSecAttrAccount, account.get());
        return query;
    }
};

} // namespace

std::unique_ptr<core::ICredentialStore> createSystemCredentialStore() {
    return std::make_unique<MacCredentialStore>();
}

} // namespace mydb::infra::platform
