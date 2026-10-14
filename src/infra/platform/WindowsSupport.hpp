#pragma once

// Utilitários internos da implementação Windows. Só incluído por fontes compilados no Windows.

#include <windows.h>

#include <string>
#include <string_view>

namespace mydb::infra::platform::windows {

/// UTF-8 → UTF-16, só na fronteira com a API do Windows (CLAUDE.md §2.6, regra 2).
inline std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(),
                              size);
    }
    return result;
}

/// UTF-16 → UTF-8.
inline std::string narrow(std::wstring_view utf16) {
    if (utf16.empty()) {
        return {};
    }
    const int size =
        ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), result.data(), size, nullptr,
                              nullptr);
    }
    return result;
}

/// Mensagem do sistema para um código de GetLastError(), em UTF-8 e sem a quebra de linha final.
inline std::string errorMessage(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): contrato de FORMAT_MESSAGE_ALLOCATE_BUFFER
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text = length > 0 && buffer != nullptr ? std::wstring(buffer, length) : std::wstring();
    if (buffer != nullptr) {
        ::LocalFree(buffer);
    }
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) {
        text.pop_back();
    }
    return narrow(text) + " (código " + std::to_string(error) + ")";
}

} // namespace mydb::infra::platform::windows
