#pragma once

// Conversões de texto na fronteira com o Qt: o core é UTF-8 em std::string (CLAUDE.md §6).

#include <QString>

#include <filesystem>
#include <string>
#include <string_view>

namespace mydb::ui {

[[nodiscard]] inline QString toQt(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] inline std::string toStd(const QString& text) {
    return text.toStdString();
}

[[nodiscard]] inline QString pathToQt(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), // NOLINT: char8_t → char, mesmo UTF-8
                             static_cast<qsizetype>(text.size()));
}

[[nodiscard]] inline std::filesystem::path toPath(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    return {std::u8string(utf8.begin(), utf8.end())};
}

} // namespace mydb::ui
