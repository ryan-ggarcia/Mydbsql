#pragma once

// Formatação de valores para as tabelas das abas Host e Database.

#include "ui/models/RowsModel.hpp"

#include <QDateTime>
#include <QLocale>
#include <QTimeZone>

#include <chrono>
#include <cstdint>
#include <optional>

namespace mydb::ui {

[[nodiscard]] inline RowsModel::Cell numberCell(std::optional<std::int64_t> value) {
    if (!value) {
        return {};
    }
    return RowsModel::Cell{.display = QLocale().toString(static_cast<qlonglong>(*value)),
                           .sort = static_cast<qlonglong>(*value)};
}

[[nodiscard]] inline RowsModel::Cell sizeCell(std::optional<std::int64_t> bytes) {
    if (!bytes) {
        return {};
    }
    return RowsModel::Cell{.display = QLocale().formattedDataSize(*bytes, 1, QLocale::DataSizeTraditionalFormat),
                           .sort = static_cast<qlonglong>(*bytes)};
}

[[nodiscard]] inline RowsModel::Cell decimalCell(double value, int decimals) {
    return RowsModel::Cell{.display = QLocale().toString(value, 'f', decimals), .sort = value};
}

/// Data como o servidor informou (sem conversão de fuso), no formato ISO para ficar inequívoca.
[[nodiscard]] inline RowsModel::Cell timeCell(std::optional<std::chrono::system_clock::time_point> time) {
    if (!time) {
        return {};
    }
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time->time_since_epoch()).count();
    const QDateTime value = QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC);
    return RowsModel::Cell{.display = value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                           .sort = static_cast<qlonglong>(seconds)};
}

} // namespace mydb::ui
