#pragma once

#include "core/errors/DbError.hpp"
#include "ui/common/QtText.hpp"

#include <QString>

#include <exception>

namespace mydb::ui {

/// Mensagem de erro para o usuário: o texto e, se for um DbError com dica, a dica num parágrafo à parte.
[[nodiscard]] inline QString errorText(const std::exception& error) {
    QString text = QString::fromUtf8(error.what());
    if (const auto* db = dynamic_cast<const core::DbError*>(&error); db != nullptr && !db->hint().empty()) {
        text += QStringLiteral("\n\n") + toQt(db->hint());
    }
    return text;
}

[[nodiscard]] inline QString errorText(const std::exception_ptr& error) {
    if (!error) {
        return {};
    }
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return errorText(e);
    } catch (...) {
        return QObject::tr("Erro desconhecido.");
    }
}

} // namespace mydb::ui
