#include "infra/platform/IPlatform.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace mydb::infra::platform {

namespace {

bool startsWithNoCase(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() &&
           std::ranges::equal(prefix, text.substr(0, prefix.size()), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

} // namespace

bool isAllowedExternalUrl(std::string_view url) noexcept {
    constexpr std::array<std::string_view, 3> kAllowedPrefixes{"http://", "https://", "mailto:"};
    const bool hasControlChar = std::ranges::any_of(url, [](char c) { return static_cast<unsigned char>(c) < 0x20; });
    if (hasControlChar) {
        return false;
    }
    return std::ranges::any_of(kAllowedPrefixes, [url](std::string_view prefix) {
        return url.size() > prefix.size() && startsWithNoCase(url, prefix);
    });
}

} // namespace mydb::infra::platform
