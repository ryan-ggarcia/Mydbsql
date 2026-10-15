#include "drivers/mysql/MySqlServerVersion.hpp"

#include <array>
#include <charconv>

namespace mydb::drivers::mysql {

namespace {

constexpr std::string_view kMariaDbCompatPrefix = "5.5.5-";

} // namespace

MySqlServerVersion MySqlServerVersion::parse(std::string_view versionText) {
    MySqlServerVersion version;
    version.text = std::string(versionText);
    version.mariaDb = versionText.find("MariaDB") != std::string_view::npos;

    std::string_view digits = versionText;
    if (version.mariaDb && digits.starts_with(kMariaDbCompatPrefix)) {
        digits.remove_prefix(kMariaDbCompatPrefix.size());
    }

    std::array<std::uint32_t, 3> parts{};
    const char* cursor = digits.data();
    const char* end = digits.data() + digits.size();
    for (std::size_t i = 0; i < parts.size() && cursor < end; ++i) {
        const auto [next, error] = std::from_chars(cursor, end, parts.at(i));
        if (error != std::errc{}) {
            break;
        }
        cursor = next;
        if (cursor == end || *cursor != '.') {
            break;
        }
        ++cursor;
    }
    version.number = (parts[0] * 10000) + (parts[1] * 100) + parts[2];
    return version;
}

} // namespace mydb::drivers::mysql
