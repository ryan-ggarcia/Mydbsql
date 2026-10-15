#include "infra/platform/SystemCredentialStore.hpp"
#include "infra/platform/WindowsSupport.hpp"

#include <wincred.h>
#include <windows.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace mydb::infra::platform {

namespace {

constexpr std::wstring_view kTargetPrefix = L"Mydbsql/";

struct CredentialDeleter {
    void operator()(PCREDENTIALW credential) const noexcept { ::CredFree(credential); }
};

class WindowsCredentialStore final : public core::ICredentialStore {
public:
    [[nodiscard]] std::optional<std::string> read(std::string_view key) override {
        const std::wstring target = targetFor(key);
        PCREDENTIALW raw = nullptr;
        if (::CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &raw) == FALSE) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_NOT_FOUND) {
                return std::nullopt;
            }
            throw std::runtime_error("Credential Manager: leitura falhou: " + windows::errorMessage(error));
        }
        const std::unique_ptr<CREDENTIALW, CredentialDeleter> credential(raw);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): o blob é um bloco de bytes
        const auto* bytes = reinterpret_cast<const char*>(credential->CredentialBlob);
        return std::string(bytes, credential->CredentialBlobSize);
    }

    void write(std::string_view key, std::string_view secret) override {
        if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            throw std::runtime_error("Credential Manager: segredo grande demais");
        }
        std::wstring target = targetFor(key);
        std::wstring user = L"Mydbsql";
        std::string blob(secret);
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.UserName = user.data();
        credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a API recebe bytes
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE; // só neste usuário e nesta máquina
        if (::CredWriteW(&credential, 0) == FALSE) {
            throw std::runtime_error("Credential Manager: gravação falhou: " + windows::errorMessage(::GetLastError()));
        }
        ::SecureZeroMemory(blob.data(), blob.size());
    }

    void erase(std::string_view key) override {
        const std::wstring target = targetFor(key);
        if (::CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == FALSE) {
            const DWORD error = ::GetLastError();
            if (error != ERROR_NOT_FOUND) {
                throw std::runtime_error("Credential Manager: remoção falhou: " + windows::errorMessage(error));
            }
        }
    }

    [[nodiscard]] bool persistent() const noexcept override { return true; }

private:
    static std::wstring targetFor(std::string_view key) { return std::wstring(kTargetPrefix) + windows::widen(key); }
};

} // namespace

std::unique_ptr<core::ICredentialStore> createSystemCredentialStore() {
    return std::make_unique<WindowsCredentialStore>();
}

} // namespace mydb::infra::platform
